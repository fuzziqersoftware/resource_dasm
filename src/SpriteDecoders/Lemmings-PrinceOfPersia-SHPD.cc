#include "Decoders.hh"

#include <stdint.h>
#include <string.h>

#include <phosg/Encoding.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

#include "../DataCodecs/Codecs.hh"
#include "../IndexFormats/Formats.hh"

using namespace std;

static constexpr uint32_t SHPD_type = 0x53485044;
static constexpr uint32_t SHPT_type = 0x53485054;

struct SHPDResource {
  be_uint32_t offset;
  be_uint32_t compressed_size; // If 0, data is not compressed
  be_uint32_t decompressed_size;
} __attribute__((packed));

static Image decode_lemmings_color_image(
    StringReader& r,
    size_t width,
    size_t height,
    const vector<ColorTableEntry>& clut) {
  // Lemmings color images are encoded in a fairly simple format: each command
  // is a single byte. If the high bit is set, then (cmd & 0x7F) + 1 pixels are
  // skipped (transparent). If the high bit is not set, then (cmd + 1) pixels
  // (bytes) are written directly from the input stream.
  Image ret(width, height, true);
  size_t x = 0, y = 0;
  auto advance_x = [&](size_t count) {
    x += count;
    while (x >= width) {
      x -= width;
      y++;
    }
  };
  while (y < height) {
    uint8_t cmd = r.get_u8();
    if (cmd & 0x80) {
      advance_x(cmd - 0x7F);
    } else {
      size_t count = cmd + 1;
      for (size_t z = 0; z < count; z++) {
        uint8_t v = r.get_u8();
        auto c = clut.at(v).c.as8();
        ret.write_pixel(x, y, c.r, c.g, c.b, 0xFF);
        advance_x(1);
      }
    }
  }
  return ret;
}

vector<DecodedSHPDImage> decode_SHPD_images(
    ResourceFile& rf,
    int16_t shpd_id,
    const string& data,
    const vector<ColorTableEntry>& clut,
    SHPDVersion version) {
  StringReader r(data);
  vector<DecodedSHPDImage> ret;

  if (version == SHPDVersion::LEMMINGS_V1 || version == SHPDVersion::LEMMINGS_V2) {
    // Lemmings SHPD image data consists of a list of offsets, each pointing to
    // an image data segment. The segments are composed of a short header (8
    // bytes in v1, 12 bytes in v2) followed by the image data.
    uint32_t offsets_end_offset = r.get_u32b(false);
    if (offsets_end_offset == 0) {
      // If the first 4 bytes are zero, the image is a single image in PICT
      // format instead of a list of images.
      ResourceFile::Resource pict_res(RESOURCE_TYPE_PICT, 0, data);
      ResourceFile pict_rf;
      pict_rf.add(pict_res);
      auto& img = ret.emplace_back();
      img.origin_x = 0;
      img.origin_y = 0;
      img.image = std::move(pict_rf.decode_PICT(0).image);

    } else {
      bool is_v2 = (version == SHPDVersion::LEMMINGS_V2);
      while (r.where() < offsets_end_offset) {
        auto& img = ret.emplace_back();

        uint32_t start_offset = r.get_u32b();
        if (start_offset == 0) {
          img.origin_x = 0;
          img.origin_y = 0;
        } else {
          StringReader image_r = r.sub(start_offset);
          if (is_v2) {
            image_r.skip(4); // Unknown what these bytes are for
          }
          img.origin_x = image_r.get_u16b();
          img.origin_y = image_r.get_u16b();
          size_t width = image_r.get_u16b();
          size_t height = image_r.get_u16b();
          if (!clut.empty()) {
            img.image = decode_lemmings_color_image(image_r, width, height, clut);
          } else {
            img.image = decode_presage_mono_image(image_r, width, height, false);
          }
        }
      }
    }

  } else if (version == SHPDVersion::PRINCE_OF_PERSIA) {
    // Prince of Persia has SHPT resources that further split the SHPDs into
    // sub-images. (This is similar to how Lemmings uses a list of offsets at
    // the beginning, but in Prince of Persia the offsets are stored in a
    // separate resource.)
    auto res = rf.get_resource(SHPT_type, shpd_id);
    StringReader shpt_r(res->data);
    while (!shpt_r.eof()) {
      uint32_t start_offset = shpt_r.get_u32b();
      if (start_offset == 0xFFFFFFFF) {
        continue;
      }
      uint32_t end_offset = shpt_r.eof() ? r.size() : shpt_r.get_u32b(false);

      StringReader image_r = r.sub(start_offset, end_offset - start_offset);
      auto& img = ret.emplace_back();

      // Unlike Lemmings, the width and height are the first fields in the
      // header, not the last.
      size_t width = image_r.get_u16b();
      size_t height = image_r.get_u16b();
      img.origin_x = image_r.get_u16b();
      img.origin_y = image_r.get_u16b();
      if (!clut.empty()) {
        img.image = decode_presage_v1_commands(image_r, width, height, clut);
      } else {
        // Prince of Persia appears to use a different default compositing mode;
        // it looks like AND rather than MASK_COPY
        img.image = decode_presage_mono_image(image_r, width, height, true);
      }
    }

  } else {
    throw logic_error("invalid SHPD version");
  }
  return ret;
}

unordered_map<string, DecodedSHPDImage> decode_SHPD_collection(
    ResourceFile& rf,
    const string& data_fork_contents,
    const vector<ColorTableEntry>& clut,
    SHPDVersion version) {
  StringReader r(data_fork_contents);
  unordered_map<string, DecodedSHPDImage> ret;
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
      data = decompress_presage_lzss(sub_r, shpd->decompressed_size);
      if (shpd->decompressed_size != data.size()) {
        throw runtime_error(string_printf(
            "incorrect decompressed data size: expected %" PRIX32 " bytes, received %zX bytes",
            shpd->decompressed_size.load(), data.size()));
      }
    }

    auto images = decode_SHPD_images(rf, id, data, clut, version);
    for (size_t x = 0; x < images.size(); x++) {
      ret.emplace(string_printf("%hd_%s_%zu", id, res->name.c_str(), x), std::move(images[x]));
    }
  }
  return ret;
}

unordered_map<string, Image> decode_SHPD_collection_images_only(
    ResourceFile& rf,
    const string& data_fork_contents,
    const vector<ColorTableEntry>& clut,
    SHPDVersion version) {
  auto decoded = decode_SHPD_collection(rf, data_fork_contents, clut, version);
  unordered_map<string, Image> ret;
  for (auto& it : decoded) {
    ret.emplace(it.first, std::move(it.second.image));
  }
  return ret;
}
