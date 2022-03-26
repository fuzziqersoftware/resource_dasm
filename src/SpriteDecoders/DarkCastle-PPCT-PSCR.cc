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



string decompress_PSCR(const string& data) {
  StringReader r(data);
  StringWriter w;

  r.skip(2); // Size field; we use StringReader to check bounds instead
  string short_const_table = r.readx(0x08);
  string long_const_table = r.readx(0x80);

  while (!r.eof()) {
    uint8_t cmd = r.get_u8();
    if (cmd == 0) {
      w.put_u8(r.get_u8());

    } else if (cmd & 0x80) {
      uint8_t v = short_const_table[(cmd >> 4) & 7];
      size_t count = (cmd & 0x0F) + 1;
      for (size_t x = 0; x < count; x++) {
        w.put_u8(v);
      }

    } else {
      // Note: cmd is 01-7F here, so it's always safe to subtract 1. Also, looks
      // like the last byte in the long const table never gets used...
      w.put_u8(long_const_table[cmd - 1]);
    }
  }

  return w.str();
}

Image decode_PSCR(const std::string& data) {
  string decompressed_data = decompress_PSCR(data);
  return decode_monochrome_image(decompressed_data.data(), decompressed_data.size(), 512, 342);
}



string decompress_bitmap_data(StringReader& r, size_t expected_bits) {
  if (expected_bits & 7) {
    throw runtime_error("expected bits is not a multiple of 8");
  }
  // Commands go like this (binary):
  // 0xxxxxxx - write 7 data bits
  // 10000000 - stop
  // 10xxxxxx - write x zero bits
  // 11xxxxxx - write x one bits
  BitWriter w;
  for (;;) {
    uint8_t z = r.get_u8();
    if (z == 0x80) {
      break;
    } else if (z & 0x80) {
      uint8_t count = (z & 0x3F) + 7;
      bool v = !!(z & 0x40);
      for (uint8_t x = 0; x < count; x++) {
        w.write(v);
      }
    } else {
      for (uint8_t x = 0; x < 7; x++) {
        w.write(!!(z & 0x40));
        z <<= 1;
      }
    }
  }

  if (w.size() > expected_bits) {
    // The compression format doesn't have a way to specify only a few bits at
    // once, so some sprites actually overflow the boundaries of the output
    // buffer by a few bits. If they overflow by 6 or fewer bits, just truncate
    // the extra bits off.
    if (w.size() - expected_bits <= 6) {
      w.truncate(expected_bits);
    } else {
      throw runtime_error(string_printf(
          "decompression produced too much data (%zX expected, %zX received)",
          expected_bits, w.size()));
    }
  } else {
    // Similarly, some sprites can end early if their lower-right corners are
    // white. Just extend the result to the required length.
    while (w.size() < expected_bits) {
      w.write(false);
    }
  }

  return w.str();
}

struct PPCTHeader {
  be_uint16_t type; // 0-4; 0,3 appear to be identical; 1,2,4 identical too
  // width = unknown1 * 16
  // height = unknown0 * unknown2 (or that *2 if type is 0 or 3)
  // decompressed size = unknown0 * unknown3 (or that *2 if type is 0 or 3)
  be_uint16_t num_images;
  be_uint16_t width_words;
  be_uint16_t image_height_pixels;
  be_uint16_t unknown3;
  be_uint16_t unknown4;
  be_uint16_t unknown5;
} __attribute__((packed));

Image decode_PPCT(const std::string& data) {
  StringReader r(data);
  const auto& h = r.get<PPCTHeader>();
  size_t width = h.width_words << 4;
  size_t height = h.num_images * h.image_height_pixels;
  if (h.type == 0 || h.type == 3) {
    height *= 2;
  }
  string decompressed_data = decompress_bitmap_data(r, width * height);
  return decode_monochrome_image(
      decompressed_data.data(), decompressed_data.size(), width, height);
}
