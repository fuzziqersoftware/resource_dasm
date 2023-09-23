#include "Decoders.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <exception>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

string decompress_PSCR_v1(StringReader& r) {
  StringWriter w;

  r.skip(2); // Size field; we use StringReader to check bounds instead
  string short_const_table = r.readx(0x08);
  string long_const_table = r.readx(0x80);

  while (!r.eof()) {
    uint8_t cmd = r.get_u8();
    if (cmd == 0) {
      // 00000000 XXXXXXXX: Write byte XX
      w.put_u8(r.get_u8());

    } else if (cmd & 0x80) {
      // 1WWWCCCC: Write short_const_table[W] (C + 1) times
      uint8_t v = short_const_table[(cmd >> 4) & 7];
      size_t count = (cmd & 0x0F) + 1;
      for (size_t x = 0; x < count; x++) {
        w.put_u8(v);
      }

    } else {
      // 0WWWWWWW: Write long_const_table[W-1]
      // Note: cmd is 01-7F here, so it's always safe to subtract 1. Also, looks
      // like the last byte in the long const table never gets used...
      w.put_u8(long_const_table[cmd - 1]);
    }
  }

  return w.str();
}

string decompress_PSCR_v2(StringReader& r) {
  size_t data_bytes = r.get_u16b();
  string const_table = r.readx(8);

  if (r.remaining() < data_bytes) {
    throw runtime_error("data extends beyond end of resource");
  }
  size_t extra_bytes = r.remaining() - data_bytes;

  StringWriter w;
  while (r.remaining() > extra_bytes) {
    uint8_t cmd = r.get_u8();

    // 1CCCCXXX: Write (C + 1) bytes of const_table[X]
    if (cmd & 0x80) {
      uint8_t v = const_table[cmd & 7];
      size_t count = ((cmd >> 3) & 0x0F) + 1;
      for (size_t x = 0; x < count; x++) {
        w.put_u8(v);
      }

      // 00CCCCCC: Write (C + 1) bytes from input to output
    } else if ((cmd & 0x40) == 0) {
      w.write(r.read(cmd + 1));

      // 011XXXCC CCCCCCCC: Write (C + 1) bytes of const_table[X]
    } else if ((cmd & 0x20) != 0) {
      uint8_t v = const_table[(cmd >> 2) & 7];
      size_t count = (((cmd & 3) << 8) | r.get_u8()) + 1;
      for (size_t x = 0; x < count; x++) {
        w.put_u8(v);
      }

      // 010CCCCC VVVVVVVV: Write (C + 1) bytes of V
    } else {
      uint8_t v = r.get_u8();
      size_t count = (cmd & 0x1F) + 1;
      for (size_t x = 0; x < count; x++) {
        w.put_u8(v);
      }
    }
  }

  return w.str();
}

Image decode_PSCR(const string& data, bool is_v2) {
  StringReader r(data);
  string decompressed_data = is_v2
      ? decompress_PSCR_v2(r)
      : decompress_PSCR_v1(r);
  return decode_monochrome_image(decompressed_data.data(), decompressed_data.size(), 512, 342);
}

Image decode_PBLK(const string& data) {
  StringReader r(data);
  string decompressed_data = decompress_PSCR_v2(r);
  return decode_monochrome_image(decompressed_data.data(), decompressed_data.size(), 128, 120);
}

string decompress_PPCT(StringReader& r, size_t expected_bits) {
  if (expected_bits & 7) {
    throw runtime_error("expected bits is not a multiple of 8");
  }

  BitWriter w;
  for (;;) {
    uint8_t z = r.get_u8();
    if (z == 0x80) {
      // 10000000: Stop
      break;
    } else if (z & 0x80) {
      // 1VXXXXXX: Write (X + 7) bits, all with value V
      uint8_t count = (z & 0x3F) + 7;
      bool v = !!(z & 0x40);
      for (uint8_t x = 0; x < count; x++) {
        w.write(v);
      }
    } else {
      // 0VVVVVVV: Write 7 data bits (values VVVVVVV)
      for (uint8_t x = 0; x < 7; x++) {
        w.write(!!(z & 0x40));
        z <<= 1;
      }
    }
  }

  if (expected_bits != 0) {
    if (w.size() > expected_bits) {
      // Some sprites overflow the boundaries of the output buffer by a few
      // bits. A few of them overflow by a lot of bits (80 or more), but the
      // images appear correct, so... I guess it's OK to just always ignore the
      // extra output.
      w.truncate(expected_bits);
    } else {
      // Similarly, some sprites can end early if their lower-right corners are
      // white. Just extend the result to the required length.
      while (w.size() < expected_bits) {
        w.write(false);
      }
    }
  }

  return w.str();
}

struct PPCTHeader {
  // The type field is always 0-9, but there are really only two types:
  // 0, 3, or 9; or the others (1, 2, 4, 5, 6, 7, 8). The code treats all types
  // within those two groups as identical to each other.
  be_uint16_t type;
  be_uint16_t num_images;
  be_uint16_t width_words;
  be_uint16_t image_height_pixels;
  be_uint16_t unknown3;
  be_uint16_t unknown4;
  be_uint16_t unknown5;
  // Some useful values aren't contains in the header but can be easily computed
  // from its values:
  //   width = width_words * 16
  //   height = num_images * image_height_pixels (*2 if type is 0, 3, or 9)
  //   decompressed size = num_images * unknown3 (*2 if type is 0, 3, or 9)
} __attribute__((packed));

Image decode_PPCT(const string& data) {
  StringReader r(data);
  const auto& h = r.get<PPCTHeader>();
  size_t width = h.width_words << 4;
  size_t height = h.num_images * h.image_height_pixels;

  bool use_ppct_v2 = false;
  uint16_t type = h.type;
  if (type >= 1000) {
    type = type % 1000;
    use_ppct_v2 = true;
  }
  bool has_masks = false;
  if (type == 0 || type == 3 || type == 9) {
    has_masks = true;
    height *= 2;
  }
  if (type > 9) {
    throw runtime_error("unknown type");
  }
  if (type == 5) {
    // It looks like this is handled by the PPCT v2 decompressor as well. Verify this.
    throw runtime_error("type == 5");
  }
  string decompressed_data = use_ppct_v2
      ? decompress_PSCR_v2(r)
      : decompress_PPCT(r, width * height);
  Image decoded = decode_monochrome_image(
      decompressed_data.data(), decompressed_data.size(), width, height);
  if (has_masks) {
    Image ret(decoded.get_width(), h.num_images * h.image_height_pixels, true);
    for (size_t image_index = 0; image_index < h.num_images; image_index++) {
      for (size_t y = 0; y < h.image_height_pixels; y++) {
        size_t src_y = image_index * 2 * h.image_height_pixels + y;
        size_t dest_y = image_index * h.image_height_pixels + y;
        for (size_t x = 0; x < width; x++) {
          uint32_t mask_pixel = decoded.read_pixel(x, src_y + h.image_height_pixels);
          if (mask_pixel & 0xFFFFFF00) {
            ret.write_pixel(x, dest_y, 0x00000000);
          } else {
            ret.write_pixel(x, dest_y, decoded.read_pixel(x, src_y));
          }
        }
      }
    }
    return ret;
  } else {
    return decoded;
  }
}
