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



static Image decode_PPSS_lzss_section(StringReader& r, size_t w, size_t h, const vector<ColorTableEntry>& clut) {
  size_t max_output_bytes = w * h;
  size_t compressed_bytes = r.remaining();
  const void* compressed_data = r.getv(compressed_bytes);
  string decompressed = decompress_presage_lzss(
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

Image decode_presage_mono_image(
    StringReader& r, size_t width, size_t height, bool use_and_compositing) {
  // Monochrome images are encoded in very similar ways in all games that use
  // this library. The width is rounded up to a word boundary (16 pixels), and
  // the image data consists of alternating words of mask and image data. The
  // pixels are arranged in reading order, so the first two words specify the
  // mask and color values (in that order) for the leftmost 16 pixels in the top
  // row. The next two words specify the values for the next 16 pixels in the
  // top row, etc.
  width = (width + 15) & (~15);
  Image ret(width, height, true);
  for (size_t y = 0; y < height; y++) {
    for (size_t x = 0; x < width; x += 16) {
      uint16_t mask_bits = r.get_u16b();
      uint16_t color_bits = r.get_u16b();
      for (size_t z = 0; z < 16; z++) {
        if (use_and_compositing) {
          if (color_bits & 0x8000) {
            ret.write_pixel(x + z, y, 0x000000FF);
          } else {
            ret.write_pixel(x + z, y, (mask_bits & 0x8000) ? 0x00000000 : 0xFFFFFFFF);
          }
        } else {
          if (mask_bits & 0x8000) {
            ret.write_pixel(x + z, y, 0x00000000);
          } else {
            ret.write_pixel(x + z, y, (color_bits & 0x8000) ? 0x000000FF : 0xFFFFFFFF);
          }
        }
        mask_bits <<= 1;
        color_bits <<= 1;
      }
    }
  }
  return ret;
}

Image decode_presage_v1_commands(
    StringReader& r, size_t w, size_t h, const vector<ColorTableEntry>& clut) {
  // This format was used in Prince of Persia.
  // The input is a series of commands, documented in the comments below.

  Image ret(w, h, true);
  ret.clear(0x00000000);

  vector<pair<size_t, size_t>> loc_stack; // [(count, offset)]

  size_t x = 0, y = 0;
  bool should_stop = false;
  while (!should_stop) {
    uint8_t cmd = r.get_u8();
    // The bits in cmd are RGGCCCCC:
    // R = move to next row before executing this command
    // G = opcode (meanings described in comments below)
    // C = count (if == 0x1F, use the following byte instead and add 0x1F to it)

    if (cmd & 0x80) {
      y++;
      x = 0;
    }

    // Most opcodes do things (count + 1) times, so add 1 here for convenience.
    size_t count = cmd & 0x1F;
    if (count == 0x1F) {
      count = r.get_u8() + 0x20;
    } else {
      count++;
    }

    switch (cmd & 0x60) {
      case 0x00:
        // R0000000: Stop
        // R00CCCCC WWWWWWWW: Write (C + 1) bytes of single color W
        // (It makes sense for them to include the stop opcode here - to write a
        // single byte, the command R0100000 could be used instead.)
        // Note that we incremented C by 1 earlier for convenience, so we check
        // for 1 rather than 0 here.
        if (count == 1) {
          should_stop = true;
        } else {
          const auto& c = clut.at(r.get_u8()).c.as8();
          for (size_t z = 0; z < count; z++) {
            ret.write_pixel(x, y, c.r, c.g, c.b, 0xFF);
            x++;
          }
        }
        break;
      case 0x20:
        // R01CCCCC <data>: Write (C + 1) bytes directly from input
        for (size_t z = 0; z < count; z++) {
          const auto& c = clut.at(r.get_u8()).c.as8();
          ret.write_pixel(x, y, c.r, c.g, c.b, 0xFF);
          x++;
        }
        break;
      case 0x40:
        // R10CCCCC: Write (C + 1) transparent pixels
        x += count;
        break;
      case 0x60:
        // R11CCCCC: Loop control
        // If C == 0, go back to previous loop point if there are still
        // iterations to run.
        // If C != 0, push the current location on the stack, along with the
        // count. The commands from here through the corresponding R1100000
        // command will run (C + 1) times. (For example, if C == 1, we'll push
        // (1, r.where()), then the intermediate commands will run, then the
        // R1100000 command at the end will see the count as 1 and will
        // decrement it and jump back. When it gets to the end command again, it
        // will see (0, r.where()); it will then remove it and not jump back.)
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
        break;
    }
  }

  return ret;
}

Image decode_presage_v2_commands(
    StringReader& r, size_t w, size_t h, const vector<ColorTableEntry>& clut) {
  // This format was used in Flashback and Mario Teaches Typing. It's similar
  // to v1, but the command numbers are changed and extended counts are now
  // words instead of bytes. The stop opcodes are also different.
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
    decompressed_data = decompress_presage_lzss(data);
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
      uint16_t w = section_r.get_u16b();
      uint16_t h = section_r.get_u16b();
      if (format == 0xC211) {
        section_r.skip(4); // Unknown - could be origin coordinates
        ret.emplace_back(decode_presage_v2_commands(section_r, w, h, clut));
      } else if (format == 0xC103) {
        ret.emplace_back(decode_PPSS_lzss_section(section_r, w, h, clut));
      } else {
        throw runtime_error("unknown PPSS format");
      }
    }
  }

  return ret;
}

vector<Image> decode_Pak(const string& data, const vector<ColorTableEntry>& clut) {
  StringReader r(data);

  uint16_t format = r.get_u16b();
  size_t num_images = r.get_u16b();
  r.skip(2); // Unknown

  vector<Image> ret;
  for (size_t z = 0; z < num_images; z++) {
    size_t start_offset = r.get_u32b();
    if (start_offset != 0) {
      StringReader section_r = r.sub(start_offset);
      section_r.skip(4); // Unknown - could be origin coordinates
      uint16_t w = section_r.get_u16b();
      uint16_t h = section_r.get_u16b();
      if (format == 0x8002) {
        ret.emplace_back(decode_presage_v2_commands(section_r, w, h, clut));
      } else if (format == 0x8101) {
        ret.emplace_back(decode_presage_mono_image(section_r, w, h, false));
      } else {
        throw runtime_error("unknown Pak format");
      }
    }
  }

  return ret;
}
