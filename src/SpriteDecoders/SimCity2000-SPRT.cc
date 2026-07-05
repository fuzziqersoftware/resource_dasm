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

namespace ResourceDASM {

struct SpriteEntry {
  phosg::be_uint16_t id;
  phosg::be_uint32_t offset;
  phosg::be_uint16_t height;
  phosg::be_uint16_t width;
} __attribute__((packed));

static phosg::ImageRGBA8888N decode_sprite_entry(
    phosg::StringReader& r, uint16_t width, uint16_t height, const std::vector<ColorTableEntry>& pltt) {
  // SC2K sprites are encoded as byte streams. Opcodes are phosg::be_uint16_ts, where the low byte specifies the
  // command number and the high byte specifies a count (which is only used by some commands). Some opcodes are
  // followed by multiple data bytes (possibly an odd number), but opcodes are always word-aligned. There are only 5
  // opcodes.

  phosg::ImageRGBA8888N ret(width, height);
  ret.clear(0xFFFFFF00); // All transparent by default

  int16_t y = -1;
  int16_t x = 0;

  for (;;) {
    uint16_t opcode = r.get_u16b();
    switch (opcode & 0xFF) {
      case 0: // No-op
        break;
      case 1: // End of row
        y++;
        x = 0;
        break;
      case 2: // End of stream
        return ret;
      case 3: // Skip pixels to the right
        x += (opcode >> 8);
        break;
      case 4: { // Write pixels
        uint16_t end_x = x + (opcode >> 8);
        for (; x < end_x; x++) {
          ret.write(x, y, pltt.at(r.get_u8()).c.rgba8888());
        }
        // Opcodes are always word-aligned, so skip a byte if needed
        if (opcode & 0x0100) {
          r.skip(1);
        }
        break;
      }
      default:
        throw std::runtime_error(std::format("invalid opcode: {:04X}", opcode));
    }
  }
}

std::vector<phosg::ImageRGBA8888N> decode_SPRT(const std::string& data, const std::vector<ColorTableEntry>& pltt) {
  phosg::StringReader r(data);
  uint16_t count = r.get_u16b();

  std::vector<phosg::ImageRGBA8888N> ret;
  for (size_t x = 0; x < count; x++) {
    const auto& entry = r.get<SpriteEntry>();
    auto sub_r = r.sub(entry.offset);
    ret.emplace_back(decode_sprite_entry(sub_r, entry.width, entry.height, pltt));
  }

  return ret;
}

} // namespace ResourceDASM
