#include "Decoders.hh"

#include <stdint.h>
#include <string.h>

#include <stdexcept>
#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <phosg/Image.hh>
#include <string>

#include "../IndexFormats/Formats.hh"

using namespace std;



static const uint32_t SHPD_type = 0x53485044;
static const uint32_t SHPT_type = 0x53485054;

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



static Image decode_masked_mono_image(
    StringReader& r, size_t width, size_t height, SHPDVersion version) {
  // Monochrome images are encoded in very similar ways in all games that use
  // SHPD resources. THe width is rounded up to a word boundary (16 pixels), and
  // the image data consists of alternating words of mask and image data. The
  // pixels are arranged in reading order, so the first two words specify the
  // mask and color values for the leftmost 16 pixels in the top row. The next
  // two words specify the values for the next 16 pixels in the top row, etc.
  width = (width + 15) & (~15);
  Image ret(width, height, true);
  for (size_t y = 0; y < height; y++) {
    for (size_t x = 0; x < width; x += 16) {
      uint16_t mask_bits = r.get_u16b();
      uint16_t color_bits = r.get_u16b();
      for (size_t xx = 0; xx < 16; xx++) {
        // Prince of Persia appears to use a different default compositing
        // mode - it looks like AND rather than MASK_COPY
        if (version == SHPDVersion::PRINCE_OF_PERSIA) {
          if (mask_bits & 0x8000) {
            if (color_bits & 0x8000) {
              ret.write_pixel(x + xx, y, 0x000000FF);
            } else {
              ret.write_pixel(x + xx, y, 0x00000000);
            }
          } else {
            if (color_bits & 0x8000) {
              ret.write_pixel(x + xx, y, 0x000000FF);
            } else {
              ret.write_pixel(x + xx, y, 0xFFFFFFFF);
            }
          }
        } else {
          if (mask_bits & 0x8000) {
            ret.write_pixel(x + xx, y, 0x00000000);
          } else {
            if (color_bits & 0x8000) {
              ret.write_pixel(x + xx, y, 0x000000FF);
            } else {
              ret.write_pixel(x + xx, y, 0xFFFFFFFF);
            }
          }
        }
        mask_bits <<= 1;
        color_bits <<= 1;
      }
    }
  }
  return ret;
}

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

static Image decode_prince_of_persia_color_image(
    StringReader& r, size_t w, size_t h, const vector<ColorTableEntry>& clut) {
  // Prince of Persia uses a much more complex encoding scheme for its sprites.
  // The input is a series of commands, documented in the comments below.

  Image ret(w, h, true);
  ret.clear(0x00000000);

  vector<pair<size_t, size_t>> loc_stack; // [(count, offset)]

  size_t x = 0, y = 0;
  for (;;) {
    uint8_t cmd = r.get_u8();
    // cmd is like RGGCCCCC
    // R = move to next row
    // G = opcode (meanings described in comments below)
    // C = count (if 0x1F, use the following byte instead and add 0x1F)

    if (cmd & 0x80) {
      y++;
      x = 0;
    }

    size_t count = cmd & 0x1F;
    if (count == 0x1F) {
      count = r.get_u8() + 0x20;
    } else {
      count++;
    }

    if (cmd & 0x40) {
      if (cmd & 0x20) {
        // R11CCCCC: Loop control
        // If C == 0, go back to previous loop point if there are still
        // iterations to run.
        // If C != 0, push the current location on the stack, along with the
        // count. The commands from here through the corresponding R1100000
        // command will run (C + 1) times. (For example, if C == 1, we'll push
        // (1, r.where()), then the intermediate commands will run, then the
        // F1100000 command at the end will see the count as 1 and will
        // decrement it and jump back.)
        count--;
        if (count != 0) {
          loc_stack.emplace_back(count, r.where());
          continue;
        }
        if (loc_stack.empty()) {
          break;
        }
        auto& item = loc_stack[loc_stack.size() - 1];
        if (item.first == 0) {
          loc_stack.pop_back();
        } else {
          item.first--;
          r.go(item.second);
        }

      } else {
        // R10CCCCC: Write (C + 1) transparent pixels
        x += count;
      }

    } else if (cmd & 0x20) {
      // R01CCCCC <data>: Write (C + 1) bytes directly from input
      for (size_t z = 0; z < count; z++) {
        const auto& c = clut.at(r.get_u8()).c.as8();
        ret.write_pixel(x, y, c.r, c.g, c.b, 0xFF);
        x++;
      }

    } else {
      // R0000000: Stop
      // R00CCCCC WWWWWWWW: Write (C + 1) bytes of single color W
      // (It makes sense for them to include the stop opcode here - to write a
      // single byte, the command R0100000 could be used instead.)
      // Note that we incremented C by 1 earlier for convenience, so we check
      // for 1 rather than 0 here.
      if (count == 1) {
        break;
      }
      const auto& c = clut.at(r.get_u8()).c.as8();
      for (size_t z = 0; z < count; z++) {
        ret.write_pixel(x, y, c.r, c.g, c.b, 0xFF);
        x++;
      }
    }
  }

  return ret;
}



vector<Image> decode_SHPD_images(
    ResourceFile& rf,
    int16_t shpd_id,
    const string& data,
    const vector<ColorTableEntry>& clut,
    SHPDVersion version) {
  StringReader r(data);
  vector<Image> ret;

  if (version == SHPDVersion::LEMMINGS_V1 || version == SHPDVersion::LEMMINGS_V2) {
    // Lemmings SHPD image data consists of a list of offsets, each pointing to
    // an image data segment. The segments are composed of a short header (12
    // bytes in v2, 8 bytes in v1) followed by the image data.
    uint32_t offsets_end_offset = r.get_u32b(false);
    if (offsets_end_offset == 0) {
      // If the first 4 bytes are zero, the image is a single image in PICT
      // format instead of a list of images.
      ResourceFile::Resource pict_res(RESOURCE_TYPE_PICT, 0, data);
      ResourceFile pict_rf;
      pict_rf.add(pict_res);
      ret.emplace_back(pict_rf.decode_PICT(0).image);

    } else {
      bool is_v2 = (version == SHPDVersion::LEMMINGS_V2);
      while (r.where() < offsets_end_offset) {
        uint32_t start_offset = r.get_u32b();
        if (start_offset == 0) {
          continue;
        }
        StringReader image_r = r.sub(start_offset);
        image_r.skip(is_v2 ? 8 : 4); // Unknown; probably includes clip offsets
        size_t width = image_r.get_u16b();
        size_t height = image_r.get_u16b();
        if (!clut.empty()) {
          ret.emplace_back(decode_lemmings_color_image(image_r, width, height, clut));
        } else {
          ret.emplace_back(decode_masked_mono_image(image_r, width, height, version));
        }
      }
    }

  } else if (version == SHPDVersion::PRINCE_OF_PERSIA) {
    // Prince of Persia has SHPT resources that further split the SHPDs into
    // sub-images. (This is similar to how Lemmings uses a list of offsets at
    // the beginning, but the offsets are stored in a separate resource.)
    auto res = rf.get_resource(SHPT_type, shpd_id);
    StringReader shpt_r(res->data);
    while (!shpt_r.eof()) {
      uint32_t start_offset = shpt_r.get_u32b();
      if (start_offset == 0xFFFFFFFF) {
        continue;
      }
      uint32_t end_offset = shpt_r.eof() ? r.size() : shpt_r.get_u32b(false);

      StringReader image_r = r.sub(start_offset, end_offset - start_offset);

      // Unlike Lemmings, the width and height are the first fields in the
      // header, not the last.
      size_t width = image_r.get_u16b();
      size_t height = image_r.get_u16b();
      image_r.skip(4); // Looks like clipping offsets
      if (!clut.empty()) {
        ret.emplace_back(decode_prince_of_persia_color_image(image_r, width, height, clut));
      } else {
        ret.emplace_back(decode_masked_mono_image(image_r, width, height, version));
      }
    }

  } else {
    throw logic_error("invalid SHPD version");
  }
  return ret;
}



unordered_map<string, Image> decode_SHPD_collection(
    const string& resource_fork_contents,
    const string& data_fork_contents,
    const vector<ColorTableEntry>& clut,
    SHPDVersion version) {
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

    auto images = decode_SHPD_images(rf, id, data, clut, version);
    for (size_t x = 0; x < images.size(); x++) {
      ret.emplace(string_printf("%hd_%s_%zu", id, res->name.c_str(), x), move(images[x]));
    }
  }
  return ret;
}
