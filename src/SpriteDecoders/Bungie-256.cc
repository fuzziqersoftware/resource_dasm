#include "Decoders.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <exception>
#include <phosg/Encoding.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <vector>

#include "../DataCodecs/Codecs.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {

struct PDHeader {
  be_uint16_t format_version;
  be_uint32_t unknown_a1_offset;
  be_uint32_t image_metas_offset;
  be_uint32_t image_data_offset;
  be_uint32_t image_data_size;
  uint8_t unknown_a2[4];
  be_uint16_t num_color_table_entries;
} __attribute__((packed));

struct PDUnknownA1Entry {
  be_uint16_t format;
  be_uint16_t unknown_a1;
  be_uint16_t image_number;
  be_uint16_t unknown_a2[5];
  be_uint32_t unknown_a3[4];
} __attribute__((packed));

struct PDImageMetaEntry {
  be_uint32_t data_offset; // Relative to image_data_offset
  be_uint16_t width;
  be_uint16_t height;
  be_uint32_t unknown_a1[2];
} __attribute__((packed));

vector<ImageRGBA8888N> decode_pathways_256(const string& data) {
  string decompressed_data = unpack_pathways(data);

  StringReader r(decompressed_data);
  const auto& header = r.get<PDHeader>();

  unordered_map<uint16_t, Color> color_table;
  for (size_t z = 0; z < header.num_color_table_entries; z++) {
    uint16_t id = r.get_u16b();
    if (!color_table.emplace(id, r.get<Color>()).second) {
      throw runtime_error(std::format("duplicate color table entry: {:04X}", id));
    }
  }

  StringReader image_metas_r = r.sub(
      header.image_metas_offset, header.image_data_offset - header.image_metas_offset);
  StringReader unknown_a1s_r = r.sub(
      header.unknown_a1_offset, header.image_data_offset - header.unknown_a1_offset);
  StringReader image_data_r = r.sub(
      header.image_data_offset, header.image_data_size);

  vector<ImageRGBA8888N> ret;
  while (!image_metas_r.eof()) {
    uint16_t format = (unknown_a1s_r.eof() ? 0x0006 : unknown_a1s_r.get<PDUnknownA1Entry>().format.load());
    const auto& image_meta = image_metas_r.get<PDImageMetaEntry>();
    image_data_r.go(image_meta.data_offset);

    // It seems format 6 has data in row-major order, while all other formats
    // have data in column-major order.
    if (format == 6) {
      auto& img = ret.emplace_back(image_meta.width, image_meta.height);
      for (size_t y = 0; y < image_meta.height; y++) {
        for (size_t x = 0; x < image_meta.width; x++) {
          uint8_t id = image_data_r.get_u8();
          auto c_it = color_table.find(id);
          if (c_it != color_table.end()) {
            img.write(x, y, c_it->second.as8().rgba8888());
          }
        }
      }
    } else {
      auto& img = ret.emplace_back(image_meta.width, image_meta.height);
      for (size_t x = 0; x < image_meta.width; x++) {
        for (size_t y = 0; y < image_meta.height; y++) {
          uint8_t id = image_data_r.get_u8();
          auto c_it = color_table.find(id);
          if (c_it != color_table.end()) {
            img.write(x, y, c_it->second.as8().rgba8888());
          }
        }
      }
    }
  }

  return ret;
}

struct MHeader {
  be_uint16_t format_version;
  be_uint16_t unknown_a0;
  be_uint16_t unknown_a1; // Must be zero apparently?
  be_uint16_t num_color_table_entries;
  be_uint16_t unknown_a2;
  be_uint32_t color_table_offset;
  be_uint16_t num_name_entries;
  be_uint32_t name_offset_table_offset;
  be_uint16_t num_metadata_headers;
  be_uint32_t metadata_headers_offset_table_offset;
  be_uint16_t num_images;
  be_uint32_t image_data_offsets_table_offset;
  be_uint16_t unknown_a4;
  be_uint32_t total_size;
} __attribute__((packed));

struct MMetadataHeader {
  be_uint32_t unknown_a1;
  be_uint32_t image_number;
  uint8_t unknown_a2[10];
  be_uint16_t width;
  be_int16_t height; // Can be negative!
  uint8_t unknown_a3[12];
} __attribute__((packed));

struct MImageHeader {
  be_uint16_t width;
  be_uint16_t height;
  be_uint16_t row_bytes; // May be FFFF if image is compressed
  be_uint32_t unknown_a2;
  uint8_t unknown_a3[0x10];
  // Variable-length fields:
  // be_uint16_t unknown_a4[height][2];
  // uint8_t unknown_a5[4];
  // uint8_t pixels[width * height];
} __attribute__((packed));

vector<ImageRGBA8888N> decode_marathon_256(const string& data) {
  StringReader r(data);
  const auto& header = r.get<MHeader>();

  unordered_map<uint16_t, Color> color_table;
  r.go(header.color_table_offset);
  for (size_t z = 0; z < header.num_color_table_entries; z++) {
    uint16_t id = r.get_u16b();
    if (!color_table.emplace(id, r.get<Color>()).second) {
      throw runtime_error(std::format("duplicate color table entry: {:04X}", id));
    }
  }

  r.go(header.image_data_offsets_table_offset);

  vector<ImageRGBA8888N> ret;
  while (ret.size() < header.num_images) {
    auto image_r = r.sub(r.get_u32b());

    auto image_header = image_r.get<MImageHeader>();
    bool is_column_major = image_header.unknown_a2 & 0x80000000;
    size_t num_lines = is_column_major ? image_header.width : image_header.height;
    size_t line_length = is_column_major ? image_header.height : image_header.width;

    image_r.skip(4 + (4 * num_lines));

    StringReader alpha_r;
    StringReader data_r;

    string decompressed_data;
    string alpha_data;
    if (image_header.row_bytes == 0xFFFF) {
      // Image has transparency and is (sort of) compressed
      decompressed_data.resize(image_header.width * image_header.height, '\x00');
      alpha_data.resize(image_header.width * image_header.height, '\x00');
      for (size_t y = 0; y < num_lines; y++) {
        size_t x = 0;
        for (;;) {
          int16_t command = image_r.get_s16b();
          if (command == 0) {
            break;
          } else if (command < 0) {
            x += (-command);
          } else {
            size_t end_x = x + command;
            for (; x < end_x; x++) {
              decompressed_data[y * line_length + x] = image_r.get_u8();
              alpha_data[y * line_length + x] = '\xFF';
            }
          }
        }
      }

      alpha_r = StringReader(alpha_data);
      data_r = StringReader(decompressed_data);

    } else {
      // Image has no transparency and is not compressed
      alpha_data.resize(image_header.width * image_header.height, '\xFF');
      alpha_r = StringReader(alpha_data);
      data_r = image_r.sub(image_r.where());
    }

    auto& img = ret.emplace_back(image_header.width, image_header.height);
    if (is_column_major) {
      for (size_t x = 0; x < image_header.width; x++) {
        for (size_t y = 0; y < image_header.height; y++) {
          uint8_t alpha = alpha_r.get_u8();
          uint8_t id = data_r.get_u8();
          auto c_it = color_table.find(id);
          if (alpha != 0 && c_it != color_table.end()) {
            img.write(x, y, c_it->second.as8().rgba8888(alpha));
          } else {
            img.write(x, y, 0x00000000);
          }
        }
      }
    } else {
      for (size_t y = 0; y < image_header.height; y++) {
        for (size_t x = 0; x < image_header.width; x++) {
          uint8_t alpha = alpha_r.get_u8();
          uint8_t id = data_r.get_u8();
          auto c_it = color_table.find(id);
          if (alpha != 0 && c_it != color_table.end()) {
            img.write(x, y, c_it->second.as8().rgba8888(alpha));
          } else {
            img.write(x, y, 0x00000000);
          }
        }
      }
    }
  }

  return ret;
}

} // namespace ResourceDASM
