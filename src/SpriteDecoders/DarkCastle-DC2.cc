#include "Decoders.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <exception>
#include <phosg/Encoding.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;



struct InputFormat {
  be_int16_t height;
  be_int16_t width;
  uint8_t bits_per_pixel; // Actually bits per pixel - 1, but whatever
  uint8_t unknown[2];
  uint8_t generate_transparency_map;
} __attribute__((packed));

uint32_t get_bits_at_offset(const void* data, size_t bit_offset, size_t count) {
  // Note: the original implementation has two special cases of this function
  // where count=1 and count=2 respectively

  // Note: the original implementation is ((offset >> 4) << 1); this should be
  // equivalent. The right shift is signed in the original implementation, but
  // args to this function always appear to be positive so it shouldn't matter.
  const uint8_t* u8_data = reinterpret_cast<const uint8_t*>(data);
  size_t byte_offset = (bit_offset >> 3) & 0xFFFFFFFE;
  uint32_t value = *reinterpret_cast<const be_uint32_t*>(u8_data + byte_offset) << (bit_offset & 0x0F);
  return (value >> (32 - count));
}


Image decode_DC2(const string& data) {
  StringReader sr(data);
  BitReader br(data);
  const auto& input = sr.get<InputFormat>();
  br.skip(8 * sizeof(InputFormat));

  size_t max_color = 1 << input.bits_per_pixel;
  size_t color_table_size = max_color - 2;

  // The color table size is determined by bits_per_pixel. Color 0 is always
  // black (and is not included in the table), and the last color is always
  // transparent (and is not included in the table).
  vector<Color8> color_table;
  while (color_table.size() < color_table_size) {
    auto& c = color_table.emplace_back();
    uint16_t rgb555 = br.read(16);
    c.r = (((rgb555 >> 10) & 0x1F) * 0xFF) / 0x1F;
    c.g = (((rgb555 >> 5) & 0x1F) * 0xFF) / 0x1F;
    c.b = (((rgb555 >> 0) & 0x1F) * 0xFF) / 0x1F;
  }

  // TODO: This computation can probably be done more efficiently, but I'm lazy
  uint8_t chunk_count_bits;
  {
    uint8_t max_chunk_count;
    for (chunk_count_bits = 7, max_chunk_count = 0x80;
         (chunk_count_bits > 3) && (max_chunk_count >= input.width);
         chunk_count_bits--, max_chunk_count >>= 1);
  }

  // Start reading the bit stream and executing its commands
  StringWriter w;
  size_t output_limit = input.height * input.width;
  uint8_t transparent_color = max_color - 1;
  while (w.str().size() < output_limit) {

    uint8_t opcode = br.read(3);
    size_t chunk_count;
    switch (opcode) {
      case 0:
        // Write chunk_count + 1 zeroes to output
        chunk_count = br.read(chunk_count_bits);
        for (size_t x = 0; x < chunk_count + 1; x++) {
          w.put_u8(0);
        }
        break;

      case 1: {
        chunk_count = br.read(chunk_count_bits);
        uint8_t color = br.read(input.bits_per_pixel);

        if (color == transparent_color) {
          color = 0xFF;
        }

        // Write chunk_count + 1 copies of color to output
        for (size_t x = 0; x < chunk_count + 1; x++) {
          w.put_u8(color);
        }
        break;
      }

      case 2: {
        chunk_count = br.read(chunk_count_bits);

        uint8_t values[2];
        values[0] = br.read(input.bits_per_pixel);
        if (values[0] == transparent_color) {
          values[0] = 0xFF;
        }
        values[1] = br.read(input.bits_per_pixel);
        if (values[1] == transparent_color) {
          values[1] = 0xFF;
        }

        // Write first color followed by a bitstream-determined alternation of
        // the two colors. Note that we write exactly the count instead of
        // count + 1, presumably because the first color is always written to
        // save 1 bit. Nice hyper-optimization, Delta Tao. Was it worth it?
        w.put_u8(values[0]);
        for (size_t x = 1; x < chunk_count + 1; x++) {
          w.put_u8(values[br.read(1)]);
        }
        break;
      }

      case 3: {
        chunk_count = br.read(chunk_count_bits);

        uint8_t values[4];
        values[0] = br.read(input.bits_per_pixel);
        if (values[0] == transparent_color) {
          values[0] = 0xFF;
        }
        values[1] = br.read(input.bits_per_pixel);
        if (values[1] == transparent_color) {
          values[1] = 0xFF;
        }
        values[2] = br.read(input.bits_per_pixel);
        if (values[2] == transparent_color) {
          values[2] = 0xFF;
        }
        values[3] = br.read(input.bits_per_pixel);
        if (values[3] == transparent_color) {
          values[3] = 0xFF;
        }

        // Similar to opcode 2 (above), but 4 possible values instead of 2
        w.put_u8(values[0]);
        for (size_t x = 1; x < chunk_count + 1; x++) {
          w.put_u8(values[br.read(2)]);
        }
        break;
      }

      default: // 4, 5, 6, or 7
        // Opcodes 4, 5, and 6 write 1, 2, or 3 colors directly from the
        // bitstream. Opcode 7 writes a variable number of colors directly from
        // the bitstream.
        if (opcode == 7) {
          chunk_count = br.read(chunk_count_bits);
        } else {
          chunk_count = opcode - 4;
        }

        // Copy chunk_count + 1 items from the input bitstream to the output
        for (size_t x = 0; x < chunk_count + 1; x++) {
          uint8_t value = br.read(input.bits_per_pixel);
          if (value == transparent_color) {
            value = 0xFF;
          }
          w.put_u8(value);
        }
    }
  }

  const string& colorstream = w.str();
  if (colorstream.size() > output_limit) {
    // Note: the original implementation logged this string and then returned
    // anyway, even though it probably caused memory corruption because it
    // overstepped the bounds of the output buffer.
    // InterfaceLib::DebugStr("Uh-Oh. too many pixels.");
    throw runtime_error("Uh-Oh. too many pixels.");
  }

  // Convert the colorstream into an Image
  Image ret(input.width, input.height, true);
  for (ssize_t y = 0; y < input.height; y++) {
    for (ssize_t x = 0; x < input.width; x++) {
      uint8_t color_index = colorstream.at(y * input.width + x);
      if (color_index == 0) {
        ret.write_pixel(x, y, 0x00000000);
      } else if (color_index == 0xFF) {
        ret.write_pixel(x, y, 0x000000FF);
      } else {
        const auto& c = color_table.at(color_index - 1);
        ret.write_pixel(x, y, c.r, c.g, c.b);
      }
    }
  }

  return ret;
}
