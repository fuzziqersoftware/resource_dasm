#include "Decoders.hh"

#include <stdint.h>
#include <string.h>

#include <stdexcept>
#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <phosg/Image.hh>
#include <string>

#include "../IndexFormats/ResourceFork.hh"

using namespace std;



static const uint32_t SHPD_type = 0x53485044;

struct SHPDResource {
  be_uint32_t offset;
  be_uint32_t compressed_size; // If 0, data is not compressed
  be_uint32_t decompressed_size;
} __attribute__((packed));



string decompress_SHPD_data(StringReader& r) {
  StringWriter w;
  while (!r.eof()) {
    uint8_t control_bits = r.get_u8();
    for (uint8_t bits = 8; !r.eof() && bits; bits--) {
      bool is_backreference = control_bits & 1;
      control_bits >>= 1;
      if (is_backreference) {
        uint16_t params = r.get_u16b();
        size_t offset = (params & 0xFFF) + 1;
        size_t count = ((params >> 12) & 0xF) + 3;
        for (size_t x = 0; x < count; x++) {
          w.put_u8(w.str().at(w.str().size() - offset));
        }
      } else {
        w.put_u8(r.get_u8());
      }
    }
  }
  return w.str();
}

vector<Image> decode_SHPD_images(const string& data,
    const vector<ColorTableEntry>& clut) {
  StringReader r(data);
  vector<Image> ret;
  uint32_t offsets_end_offset = r.get_u32b(false);
  if (offsets_end_offset == 0) {
    // Looks like it's just a PICT if the first 4 bytes are zero
    ResourceFile::Resource pict_res(RESOURCE_TYPE_PICT, 0, data);
    ResourceFile pict_rf;
    pict_rf.add(pict_res);
    ret.emplace_back(pict_rf.decode_PICT(0).image);
  } else {
    while (r.where() < offsets_end_offset) {
      uint32_t start_offset = r.get_u32b();
      if (start_offset == 0) {
        continue;
      }
      StringReader image_r = r.sub(start_offset);
      image_r.skip(4); // unknown (probably includes color flags)
      size_t width = image_r.get_u16b();
      size_t height = image_r.get_u16b();
      if (!clut.empty()) {
        Image& img = ret.emplace_back(width, height, true);
        size_t x = 0, y = 0;
        auto advance_x = [&](size_t count) {
          x += count;
          while (x >= width) {
            x -= width;
            y++;
          }
        };
        while (y < height) {
          uint8_t cmd = image_r.get_u8();
          if (cmd & 0x80) {
            advance_x(cmd - 0x7F);
          } else {
            size_t count = cmd + 1;
            for (size_t z = 0; z < count; z++) {
              uint8_t v = image_r.get_u8();
              auto c = clut.at(v).c.as8();
              img.write_pixel(x, y, c.r, c.g, c.b, 0xFF);
              advance_x(1);
            }
          }
        }
      } else {
        width = (width + 15) & (~15);
        Image& img = ret.emplace_back(width, height, true);
        for (size_t y = 0; y < height; y++) {
          for (size_t x = 0; x < width; x += 16) {
            uint16_t mask_bits = image_r.get_u16b();
            uint16_t color_bits = image_r.get_u16b();
            for (size_t xx = 0; xx < 16; xx++) {
              if (mask_bits & 0x8000) {
                img.write_pixel(x + xx, y, 0x00000000);
              } else {
                if (color_bits & 0x8000) {
                  img.write_pixel(x + xx, y, 0x000000FF);
                } else {
                  img.write_pixel(x + xx, y, 0xFFFFFFFF);
                }
              }
              mask_bits <<= 1;
              color_bits <<= 1;
            }
          }
        }
      }
    }
  }
  return ret;
}



unordered_map<string, Image> decode_SHPD_collection(
    const string& resource_fork_contents,
    const string& data_fork_contents,
    const vector<ColorTableEntry>& clut) {
  StringReader r(data_fork_contents);
  auto rf = parse_resource_fork(resource_fork_contents);
  unordered_map<string, Image> ret;
  for (const auto& id : rf.all_resources_of_type(SHPD_type)) {
    auto res = rf.get_resource(SHPD_type, id);
    if (res->data.size() != sizeof(SHPDResource)) {
      throw runtime_error(string_printf(
          "incorrect resource size: expected %zX bytes, received %zX bytes",
          sizeof(SHPDResource), res->data.size()));
    }
    const auto* shpd = reinterpret_cast<const SHPDResource*>(res->data.data());

    string data;
    if (shpd->compressed_size == 0) {
      data = r.preadx(shpd->offset, shpd->decompressed_size);
    } else {
      StringReader sub_r = r.sub(shpd->offset, shpd->compressed_size);
      data = decompress_SHPD_data(sub_r);
      if (shpd->decompressed_size != data.size()) {
        throw runtime_error(string_printf(
            "incorrect decompressed data size: expected %" PRIX32 " bytes, received %zX bytes",
            shpd->decompressed_size.load(), data.size()));
      }
    }

    auto images = decode_SHPD_images(data, clut);
    for (size_t x = 0; x < images.size(); x++) {
      ret.emplace(string_printf("%hd_%s_%zu", id, res->name.c_str(), x), move(images[x]));
    }
  }
  return ret;
}
