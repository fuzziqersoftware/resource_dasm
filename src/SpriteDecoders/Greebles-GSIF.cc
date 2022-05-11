#include "Decoders.hh"

#include <stdint.h>
#include <string.h>

#include <stdexcept>
#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <phosg/Image.hh>
#include <string>

using namespace std;



struct GSIFHeader {
  be_uint32_t magic; // 'GSIF'
  be_uint16_t w;
  be_uint16_t h;
} __attribute__((packed));



Image decode_GSIF(const string& gsif_data, const std::vector<ColorTableEntry>& pltt) {
  StringReader r(gsif_data);
  const auto& header = r.get<GSIFHeader>();

  if (header.magic != 0x47534946) {
    throw runtime_error("incorrect GSIF signature");
  }

  Image ret(header.w, header.h);
  auto write_pixel = [&](size_t x, size_t y, uint8_t index) {
    if (!pltt.empty()) {
      auto c = pltt.at(index).c.as8();
      ret.write_pixel(x, y, c.r, c.g, c.b);
    } else {
      ret.write_pixel(x, y, index, index, index);
    }
  };

  for (size_t y = 0; y < header.h; y++) {
    uint16_t row_size = r.get_u16b();
    size_t expected_end_offset = r.where() + row_size;

    size_t x = 0;
    while ((x < header.w) && (r.where() < expected_end_offset)) {
      uint8_t cmd = r.get_u8();

      // 00-3F: (cmd+1) direct bytes
      if (cmd < 0x40) {
        for (size_t end_x = x + cmd + 1; x < end_x; x++) {
          write_pixel(x, y, r.get_u8());
        }

      // 40-5F: (c-3F) 8-byte 2-color blocks, with bitmask denoting which color
      // to use for each pixel. A 0 in the bitmask means to use the first color.
      // Example: 41 55 AA 33 88
      // - 41 = command (2x 8-byte 2-color blocks)
      // - 55 AA = color bytes
      // - 33 88 = bitmasks for the 16 pixels covered by the run
      // Resulting data from this example:
      //   55 55 AA AA 55 55 AA AA   AA 55 55 55 AA 55 55 55
      } else if (cmd < 0x60) {
        size_t block_count = cmd - 0x3F;
        uint8_t colors[2];
        colors[0] = r.get_u8();
        colors[1] = r.get_u8();
        for (; block_count; block_count--) {
          uint8_t bitmask = r.get_u8();
          for (size_t end_x = x + 8; x < end_x; x++) {
            write_pixel(x, y, colors[(bitmask >> 7) & 1]);
            bitmask <<= 1;
          }
        }

      // 60-7E: (c-5D) 4-byte 4-color blocks, with indexes in extra bytes. A 00
      // in the index field means to use the first color.
      // 7F: Same as above, but read another byte and do (v+22) blocks.
      // Example: 60 22 44 66 88 33 01 24
      // - 60 = command (3x 4-byte blocks)
      // - 22 44 66 88 = color bytes
      // - 33 01 24 = index bytes (as 12 2-bit values: 0 3 0 3 0 0 0 1 0 2 1 0)
      // Resulting data from this example:
      //   22 88 22 88 22 22 22 44 22 66 44 22
      } else if (cmd < 0x80) {
        size_t block_count = (cmd == 0x7F) ? (r.get_u8() + 0x22) : (cmd - 0x5D);
        uint8_t colors[4];
        colors[0] = r.get_u8();
        colors[1] = r.get_u8();
        colors[2] = r.get_u8();
        colors[3] = r.get_u8();
        for (; block_count; block_count--) {
          uint8_t bitmask = r.get_u8();
          for (size_t end_x = x + 4; x < end_x; x++) {
            write_pixel(x, y, colors[(bitmask >> 6) & 3]);
            bitmask <<= 2;
          }
        }

      // 80-FA: (c-7D) bytes of a single color
      // FB-FF: Same as above, but (((c-FB)<<8)|get_u8())+7E bytes instead
      // Example: 84 C0 => 7 bytes of C0
      } else {
        size_t count = (cmd < 0xFB)
            ? (cmd - 0x7D)
            : ((((cmd - 0xFB) << 8) | r.get_u8()) + 0x7E);
        uint8_t index = r.get_u8();
        for (size_t end_x = x + count; x < end_x; x++) {
          write_pixel(x, y, index);
        }
      }
    }

    if (x != header.w) {
      throw runtime_error("row did not produce enough data");
    }
    if (r.where() != expected_end_offset) {
      throw runtime_error("row ended at incorrect offset");
    }
  }
  return ret;
}
