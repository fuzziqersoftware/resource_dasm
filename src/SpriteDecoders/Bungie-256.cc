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



struct Header {
  be_uint16_t unknown_a0;
  be_uint32_t unknown_a1_offset;
  be_uint32_t image_metas_offset;
  be_uint32_t image_data_offset;
  be_uint32_t image_data_size;
  uint8_t unknown_a2[4];
  be_uint16_t num_color_table_entries;
} __attribute__((packed));

struct UnknownA1Entry {
  be_uint16_t format;
  be_uint16_t unknown_a1;
  be_uint16_t image_number;
  be_uint16_t unknown_a2[5];
  be_uint32_t unknown_a3[4];
} __attribute__((packed));

struct ImageMetaEntry {
  be_uint32_t data_offset; // Relative to image_data_offset
  be_uint16_t width;
  be_uint16_t height;
  be_uint32_t unknown_a1[2];
} __attribute__((packed));

vector<Image> decode_256(const string& data) {
  string decompressed_data = unpack_bungie_packbits(data);

  StringReader r(decompressed_data);
  const auto& header = r.get<Header>();

  unordered_map<uint16_t, Color> color_table;
  for (size_t z = 0; z < header.num_color_table_entries; z++) {
    uint16_t id = r.get_u16b();
    if (!color_table.emplace(id, r.get<Color>()).second) {
      throw runtime_error(string_printf("duplicate color table entry: %04hX", id));
    }
  }

  StringReader image_metas_r = r.sub(
      header.image_metas_offset, header.image_data_offset - header.image_metas_offset);
  StringReader unknown_a1s_r = r.sub(
      header.unknown_a1_offset, header.image_data_offset - header.unknown_a1_offset);
  StringReader image_data_r = r.sub(
      header.image_data_offset, header.image_data_size);

  vector<Image> ret;
  while (!image_metas_r.eof()) {
    uint16_t format = (unknown_a1s_r.eof() ? 0x0006 : unknown_a1s_r.get<UnknownA1Entry>().format.load());
    const auto& image_meta = image_metas_r.get<ImageMetaEntry>();
    image_data_r.go(image_meta.data_offset);

    // It seems format 6 has data in row-major order, while all other formats
    // have data in column-major order.
    if (format == 6) {
      auto& img = ret.emplace_back(image_meta.width, image_meta.height, true);
      for (size_t y = 0; y < image_meta.height; y++) {
        for (size_t x = 0; x < image_meta.width; x++) {
          uint8_t id = image_data_r.get_u8();
          auto c_it = color_table.find(id);
          if (c_it != color_table.end()) {
            auto c8 = c_it->second.as8();
            img.write_pixel(x, y, c8.r, c8.g, c8.b);
          }
        }
      }
    } else {
      auto& img = ret.emplace_back(image_meta.width, image_meta.height, true);
      for (size_t x = 0; x < image_meta.width; x++) {
        for (size_t y = 0; y < image_meta.height; y++) {
          uint8_t id = image_data_r.get_u8();
          auto c_it = color_table.find(id);
          if (c_it != color_table.end()) {
            auto c8 = c_it->second.as8();
            img.write_pixel(x, y, c8.r, c8.g, c8.b);
          }
        }
      }
    }
  }

  return ret;
}
