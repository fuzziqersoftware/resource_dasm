#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

#include "resource_fork.hh"

using namespace std;



Image decode_btSP_sprite(const string& data, const vector<color>& clut) {
  if (data.size() < 8) {
    throw invalid_argument("not enough data");
  }
  if (data.size() & 3) {
    throw invalid_argument("size must be a multiple of 4");
  }

  // known commands:
  // 01 XX XX XX - copy X bytes directly to output
  // 02 00 00 XX - skip X bytes (write transparent)
  // 03 00 00 00 - newline
  // 04 00 00 00 - end

  // height doesn't appear to be stored anywhere, so precompute it by reading
  // the stream
  StringReader r(data.data(), data.size());
  uint16_t height = 1;
  uint16_t width = r.get_u16r();
  r.get_u16(); // unknown what this field does

  while (!r.eof()) {
    uint8_t cmd = r.get_u8();
    switch (cmd) {
      case 1: {
        uint32_t count = r.get_u24r();
        count = (count + 3) & (~3); // round up to 4-byte boundary
        r.go(r.where() + count);
        break;
      }
      case 2:
        r.get_u24r();
        break;
      case 3:
        r.get_u24();
        height++;
        break;
      case 4:
        r.get_u24();
        break;
      default:
        throw runtime_error(string_printf("unknown command: %02hhX", cmd));
    }
  }

  // go back to the beginning to actually execute the commands
  r.go(4);

  Image ret(width, height, true);
  size_t x = 0, y = 0;
  while (!r.eof()) {
    uint8_t cmd = r.get_u8();
    switch (cmd) {

      case 1: {
        uint32_t count = r.get_u24r();
        for (uint32_t z = 0; z < count; z++) {
          uint8_t v = r.get_u8();
          const color& c = clut.at(v);
          ret.write_pixel(x, y, c.r, c.g, c.b);
          x++;
        }
        // commands are padded to 4-byte boundary
        while (count & 3) {
          r.get_u8();
          count++;
        }
        break;
      }

      case 2: {
        uint32_t count = r.get_u24r();
        for (uint32_t z = 0; z < count; z++) {
          ret.write_pixel(x, y, 0x00, 0x00, 0x00, 0x00);
          x++;
        }
        break;
      }

      case 3:
        if (r.get_u24() != 0) {
          throw runtime_error("newline command with nonzero argument");
        }
        x = 0;
        y++;
        break;

      case 4:
        if (r.get_u24() != 0) {
          throw runtime_error("end-of-stream command with nonzero argument");
        }
        if (!r.eof()) {
          throw runtime_error("end-of-stream command not at end of stream");
        }
        break;

      default:
        throw runtime_error(string_printf("unknown command: %02hhX", cmd));
    }
  }

  return ret;
}



Image decode_HrSp_sprite(const string& data, const vector<color>& clut) {
  if (data.size() < 20) {
    throw invalid_argument("not enough data");
  }
  if (data.size() & 3) {
    throw invalid_argument("size must be a multiple of 4");
  }

  StringReader r(data.data(), data.size());
  r.go(4);
  uint16_t height = r.get_u16r();
  uint16_t width = r.get_u16r();
  r.go(16);

  // known commands:
  // 00 00 00 00 - end
  // 01 XX XX XX - row frame (the next row begins when we've executed this many more bytes)
  // 02 XX XX XX - write X bytes to current position
  // 03 XX XX XX - write X transparent bytes

  Image ret(width, height, true);
  size_t x = 0, y = 0;
  size_t next_row_begin_offset = static_cast<size_t>(-1);
  while (!r.eof()) {
    if (r.where() == next_row_begin_offset) {
      x = 0;
      y++;
    }

    uint8_t cmd = r.get_u8();
    switch (cmd) {

      case 0:
        if (r.get_u24() != 0) {
          throw runtime_error("end-of-stream command with nonzero argument");
        }
        if (!r.eof()) {
          throw runtime_error("end-of-stream command not at end of stream");
        }
        break;

      case 1:
        next_row_begin_offset = r.get_u24r();
        next_row_begin_offset += r.where();
        break;

      case 2: {
        uint32_t count = r.get_u24r();
        for (uint32_t z = 0; z < count; z++) {
          uint8_t v = r.get_u8();
          const color& c = clut.at(v);
          ret.write_pixel(x, y, c.r, c.g, c.b);
          x++;
        }
        // commands are padded to 4-byte boundary
        while (count & 3) {
          r.get_u8();
          count++;
        }
        break;
      }

      case 3: {
        uint32_t count = r.get_u24r();
        for (uint32_t z = 0; z < count; z++) {
          ret.write_pixel(x, y, 0x00, 0x00, 0x00, 0x00);
          x++;
        }
        break;
      }

      default:
        throw runtime_error(string_printf("unknown command: %02hhX", cmd));
    }
  }

  return ret;
}
