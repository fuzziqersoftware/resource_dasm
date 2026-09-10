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

namespace ResourceDASM {

struct SHPDResource {
  phosg::be_uint32_t offset;
  phosg::be_uint32_t compressed_size; // If 0, data is not compressed
  phosg::be_uint32_t decompressed_size;
} __attribute__((packed));

static phosg::ImageRGBA8888N decode_lemmings_color_image(
    phosg::StringReader& r, size_t width, size_t height, const std::vector<ColorTableEntry>& clut) {
  // Lemmings color images are encoded in a fairly simple format: each command is a single byte. If the high bit is
  // set, then (cmd & 0x7F) + 1 pixels are skipped (transparent). If the high bit is not set, then (cmd + 1) pixels
  // (bytes) are written directly from the input stream.
  phosg::ImageRGBA8888N ret(width, height);
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
        ret.write(x, y, clut.at(v).c.rgba8888());
        advance_x(1);
      }
    }
  }
  return ret;
}

std::map<size_t, DecodedSHPDImage> decode_SHPD_images(
    ResourceFile& rf,
    int16_t shpd_id,
    const std::string& data,
    const std::vector<ColorTableEntry>& clut,
    SHPDVersion version) {
  phosg::StringReader r(data);
  std::map<size_t, DecodedSHPDImage> ret;

  if (version == SHPDVersion::LEMMINGS_V1 || version == SHPDVersion::LEMMINGS_V2) {
    // Lemmings SHPD image data consists of a list of offsets, each pointing to an image data segment. The segments are
    // composed of a short header (8 bytes in v1, 12 bytes in v2) followed by the image data.
    uint32_t offsets_end_offset = r.get_u32b(false);
    if (offsets_end_offset == 0) {
      // If the first 4 bytes are zero, the image is a single image in PICT format instead of a list of images.
      auto decoded = ResourceFile::decode_PICT_only(data.data(), data.size());
      if (decoded.image.empty()) {
        throw std::runtime_error("Non-PICT data in PICT-encoded SHPD");
      }
      ret.emplace(ret.size(), DecodedSHPDImage{.origin_x = 0, .origin_y = 0, .image = std::move(decoded.image)});

    } else {
      bool is_v2 = (version == SHPDVersion::LEMMINGS_V2);
      while (r.where() < offsets_end_offset) {
        uint32_t start_offset = r.get_u32b();
        if (start_offset != 0) {
          phosg::StringReader image_r = r.sub(start_offset);
          if (is_v2) {
            image_r.skip(4); // Unknown what these bytes are for
          }
          int16_t origin_x = image_r.get_u16b();
          int16_t origin_y = image_r.get_u16b();
          size_t width = image_r.get_u16b();
          size_t height = image_r.get_u16b();
          auto image = clut.empty()
              ? decode_presage_mono_image(image_r, width, height, false).convert_monochrome_to_color()
              : decode_lemmings_color_image(image_r, width, height, clut);
          size_t index = (r.where() - sizeof(uint32_t)) / sizeof(uint32_t);
          ret.emplace(index, DecodedSHPDImage{.origin_x = origin_x, .origin_y = origin_y, .image = std::move(image)});
        }
      }
    }

  } else if (version == SHPDVersion::PRINCE_OF_PERSIA) {
    // Prince of Persia has SHPT resources that further split the SHPDs into sub-images. (This is similar to how
    // Lemmings uses a list of offsets at the beginning, but in Prince of Persia the offsets are stored in a separate
    // resource.)
    auto res = rf.get_resource(RESOURCE_TYPE_SHPT, shpd_id);
    phosg::StringReader shpt_r(res->data);
    while (!shpt_r.eof()) {
      uint32_t start_offset = shpt_r.get_u32b();
      if (start_offset == 0xFFFFFFFF) {
        continue;
      }
      uint32_t end_offset = shpt_r.eof() ? r.size() : shpt_r.get_u32b(false);

      phosg::StringReader image_r = r.sub(start_offset, end_offset - start_offset);

      // Unlike Lemmings, the width and height are the first fields in the header, not the last.
      size_t width = image_r.get_u16b();
      size_t height = image_r.get_u16b();
      int16_t origin_x = image_r.get_u16b();
      int16_t origin_y = image_r.get_u16b();
      // Prince of Persia appears to use a different default compositing mode for monochrome; looks like AND rather
      // than MASK_COPY
      auto image = clut.empty()
          ? decode_presage_mono_image(image_r, width, height, true).convert_monochrome_to_color()
          : apply_clut(decode_presage_v1_commands(image_r, width, height), clut);
      size_t index = (shpt_r.where() - sizeof(uint32_t)) / sizeof(uint32_t);
      ret.emplace(index, DecodedSHPDImage{.origin_x = origin_x, .origin_y = origin_y, .image = std::move(image)});
    }

  } else {
    throw std::logic_error("invalid SHPD version");
  }
  return ret;
}

std::unordered_map<size_t, DecodedSHPDImage> decode_SHPD(
    ResourceFile& rf,
    const std::string& data_fork_contents,
    int16_t res_id,
    const std::vector<ColorTableEntry>& clut,
    SHPDVersion version) {
  phosg::StringReader r(data_fork_contents);
  std::unordered_map<size_t, DecodedSHPDImage> ret;
  auto res = rf.get_resource(RESOURCE_TYPE_SHPD, res_id);
  if (res->data.size() != sizeof(SHPDResource)) {
    throw std::runtime_error(std::format("incorrect resource size: expected {:X} bytes, received {:X} bytes",
        sizeof(SHPDResource), res->data.size()));
  }
  const auto* shpd = reinterpret_cast<const SHPDResource*>(res->data.data());

  std::string data;
  if (shpd->compressed_size == 0) {
    data = r.preadx(shpd->offset, shpd->decompressed_size);
  } else {
    phosg::StringReader sub_r = r.sub(shpd->offset, shpd->compressed_size);
    data = decompress_presage_lzss(sub_r, shpd->decompressed_size);
    if (shpd->decompressed_size != data.size()) {
      throw std::runtime_error(std::format(
          "incorrect decompressed data size: expected {:X} bytes, received {:X} bytes",
          shpd->decompressed_size, data.size()));
    }
  }

  for (auto& [image_index, entry] : decode_SHPD_images(rf, res_id, data, clut, version)) {
    ret.emplace(image_index, std::move(entry));
  }
  return ret;
}

} // namespace ResourceDASM
