#include "Decoders.hh"

#include <stdint.h>
#include <string.h>

#include <stdexcept>
#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <phosg/Image.hh>
#include <string>

#include "../DataCodecs/Codecs.hh"

using namespace std;



static Image decode_PPSS_lzss_section(StringReader& r, const vector<ColorTableEntry>& clut) {
  uint16_t w = r.get_u16b();
  uint16_t h = r.get_u16b();

  size_t max_output_bytes = w * h;
  size_t compressed_bytes = r.remaining();
  const void* compressed_data = r.getv(compressed_bytes);
  string decompressed = decompress_flashback_lzss(
      compressed_data, compressed_bytes, max_output_bytes);
  if (decompressed.size() < max_output_bytes) {
    throw runtime_error("decompression did not produce enough output");
  }

  Image ret(w, h, true);
  StringReader decompressed_r(decompressed);
  for (size_t y = 0; y < h; y++) {
    for (size_t x = 0; x < w; x++) {
      auto c = clut.at(decompressed_r.get_u8()).c.as8();
      ret.write_pixel(x, y, c.r, c.g, c.b);
    }
  }

  return ret;
}

static Image decode_PPSS_commands_section(StringReader& r, const vector<ColorTableEntry>& clut) {
  uint16_t w = r.get_u16b();
  uint16_t h = r.get_u16b();
  r.skip(4); // Unknown - could be origin coordinates

  Image ret(w, h, true);
  ssize_t x = 0;
  ssize_t y = 0;

  bool should_stop = false;
  vector<pair<size_t, size_t>> loc_stack; // [(count, offset)]
  while (!should_stop) {
    uint8_t cmd = r.get_u8();
    if (cmd & 0x80) { // next row
      y++;
      x = 0;
    }
    size_t count = cmd & 0x1F;
    if (count == 0) {
      count = r.get_u16b();
    }
    switch (cmd & 0x60) {
      case 0x00:
        // R00CCCCC: Loop control
        if (count != 1) {
          loc_stack.emplace_back(count - 1, r.where());
        } else if (loc_stack.empty()) {
          should_stop = true;
        } else {
          auto& item = loc_stack.back();
          if (item.first == 0) {
            loc_stack.pop_back();
          } else {
            item.first--;
            r.go(item.second);
          }
        }
        break;

      case 0x20:
        // R01CCCCC: Skip C bytes (write transparent)
        x += count;
        break;

      case 0x40:
        // R1000001: Stop
        if (count == 1) {
          should_stop = true;

        // R10CCCCC VVVVVVVV: Write C bytes of V
        } else {
          uint8_t v = r.get_u8();
          auto c = clut.at(v).c.as8();
          for (; count > 0; count--) {
            ret.write_pixel(x, y, c.r, c.g, c.b);
            x++;
          }
        }
        break;

      case 0x60:
        // R11CCCCC: Write C bytes directly from the input
        for (; count > 0; count--) {
          uint8_t v = r.get_u8();
          auto c = clut.at(v).c.as8();
          ret.write_pixel(x, y, c.r, c.g, c.b);
          x++;
        }
        break;
    }
  }

  return ret;
}

vector<Image> decode_PPSS(const string& data, const vector<ColorTableEntry>& clut) {
  StringReader r(data);

  // If the high bit isn't set in the first byte, assume it's compressed
  string decompressed_data;
  if (!(r.get_u8(false) & 0x80)) {
    decompressed_data = decompress_flashback_lzss(data);
    r = StringReader(decompressed_data);
  }

  uint16_t format = r.get_u16b();
  size_t num_images = r.get_u16b();
  r.skip(4); // Unknown

  vector<Image> ret;
  for (size_t z = 0; z < num_images; z++) {
    size_t start_offset = r.get_u32b();
    if (start_offset != 0) {
      StringReader section_r = r.sub(start_offset);
      if (format == 0xC211) {
        ret.emplace_back(decode_PPSS_commands_section(section_r, clut));
      } else if (format == 0xC103) {
        ret.emplace_back(decode_PPSS_lzss_section(section_r, clut));
      }
    }
  }

  return ret;
}
