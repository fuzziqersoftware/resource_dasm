#include "Decoders.hh"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

using namespace std;



struct SpriteEntry {
  uint16_t id;
  uint32_t offset;
  uint16_t height;
  uint16_t width;

  void byteswap() {
    this->id = bswap16(this->id);
    this->offset = bswap32(this->offset);
    this->height = bswap16(this->height);
    this->width = bswap16(this->width);
  }
} __attribute__((packed));

static Image decode_sprite_entry(const void* vdata, uint16_t width,
    uint16_t height, const vector<ColorTableEntry>& pltt) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  // SC2K sprites are encoded as byte streams. Opcodes are 2 bytes; some opcodes
  // are followed by multiple bytes (possibly an odd number), but opcodes are
  // always word-aligned. There are only 5 opcodes.

  Image ret(width, height, true);
  ret.clear(0xFF, 0xFF, 0xFF, 0x00); // All transparent by default

  int16_t y = -1;
  int16_t x = 0;

  size_t offset = 0;
  for (;;) {
    uint16_t opcode = bswap16(*reinterpret_cast<const uint16_t*>(data));
    data += 2;
    offset += 2;

    switch (opcode & 0xFF) {
      case 0: // no-op
        break;
      case 1: // end of row
        y++;
        x = 0;
        break;
      case 2: // end of stream
        return ret;
      case 3: // skip pixels to the right
        x += (opcode >> 8);
        break;
      case 4: { // write pixels
        uint16_t end_x = x + (opcode >> 8);
        for (; x < end_x; x++) {
          uint8_t color = *(data++);
          offset++;
          Color8 c = pltt.at(color).c.as8();
          ret.write_pixel(x, y, c.r, c.g, c.b, 0xFF);
        }
        // Opcodes are always word-aligned, so adjust ptr if needed
        if (opcode & 0x0100) {
          data++;
        }
        break;
      }
      default:
        throw runtime_error(string_printf("invalid opcode: %04hX", opcode));
    }
  }
}

vector<Image> decode_SPRT(const string& data, const vector<ColorTableEntry>& pltt) {
  StringReader r(data);
  uint16_t count = r.get_u16r();

  vector<Image> ret;
  for (size_t x = 0; x < count; x++) {
    auto entry = r.get_sw<SpriteEntry>();
    ret.emplace_back(decode_sprite_entry(
        data.data() + entry.offset, entry.width, entry.height, pltt));
  }

  return ret;
}
