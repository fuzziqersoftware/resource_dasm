#include "Decoders.hh"

#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

using namespace std;
using namespace phosg;

namespace ResourceDASM {

ImageRGBA8888 decode_btSP(const string& data, const vector<ColorTableEntry>& clut) {
  if (data.size() < 8) {
    throw invalid_argument("not enough data");
  }
  if (data.size() & 3) {
    throw invalid_argument("size must be a multiple of 4");
  }

  // Height doesn't appear to be stored anywhere, so precompute it by reading
  // the stream
  StringReader r(data.data(), data.size());
  uint16_t height = 1;
  uint16_t width = r.get_u16b();
  r.skip(2); // Unknown what this field does

  while (!r.eof()) {
    // See the below loop for descriptions of what these commands actually do
    uint8_t cmd = r.get_u8();
    switch (cmd) {
      case 1: {
        uint32_t count = r.get_u24b();
        count = (count + 3) & (~3); // Round up to 4-byte boundary
        r.skip(count);
        break;
      }
      case 2:
        r.skip(3);
        break;
      case 3:
        r.skip(3);
        height++;
        break;
      case 4:
        r.skip(3);
        break;
      default:
        throw runtime_error(std::format("unknown command: {:02X}", cmd));
    }
  }

  // Go back to the beginning to actually execute the commands
  r.go(4);

  ImageRGBA8888 ret(width, height);
  size_t x = 0, y = 0;
  while (!r.eof()) {
    uint8_t cmd = r.get_u8();
    switch (cmd) {

      case 1: {
        // 01 XX XX XX <data>: Copy X bytes directly to output
        uint32_t count = r.get_u24b();
        for (uint32_t z = 0; z < count; z++) {
          uint8_t v = r.get_u8();
          ret.write(x, y, clut.at(v).c.as8().rgba8888());
          x++;
        }
        // Commands are padded to 4-byte boundary
        while (count & 3) {
          r.get_u8();
          count++;
        }
        break;
      }

      case 2: {
        // 02 00 00 XX: Skip X bytes (write transparent)
        uint32_t count = r.get_u24b();
        for (uint32_t z = 0; z < count; z++) {
          ret.write(x, y, 0x00000000);
          x++;
        }
        break;
      }

      case 3:
        // 03 00 00 00: Move to beginning of next row
        if (r.get_u24b() != 0) {
          throw runtime_error("newline command with nonzero argument");
        }
        x = 0;
        y++;
        break;

      case 4:
        // 04 00 00 00: End sprite data
        if (r.get_u24b() != 0) {
          throw runtime_error("end-of-stream command with nonzero argument");
        }
        if (!r.eof()) {
          throw runtime_error("end-of-stream command not at end of stream");
        }
        break;

      default:
        throw runtime_error(std::format("unknown command: {:02X}", cmd));
    }
  }

  return ret;
}

static ImageRGBA8888 decode_HrSp_commands(
    StringReader& r,
    size_t width,
    size_t height,
    const vector<ColorTableEntry>& clut) {
  ImageRGBA8888 ret(width, height);
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
        // 00 00 00 00: End sprite data
        if (r.get_u24b() != 0) {
          throw runtime_error("end-of-stream command with nonzero argument");
        }
        if (!r.eof()) {
          throw runtime_error("end-of-stream command not at end of stream");
        }
        break;

      case 1:
        // 01 XX XX XX: Start row frame (the next row begins when we've executed
        //   this many more bytes from the input, measured from the end of the
        //   XX bytes)
        next_row_begin_offset = r.get_u24b();
        next_row_begin_offset += r.where();
        break;

      case 2: {
        // 02 XX XX XX: Write X bytes to current position
        uint32_t count = r.get_u24b();
        for (uint32_t z = 0; z < count; z++) {
          uint8_t v = r.get_u8();
          ret.write(x, y, clut.at(v).c.as8().rgba8888());
          x++;
        }
        // Commands are padded to 4-byte boundary
        while (count & 3) {
          r.get_u8();
          count++;
        }
        break;
      }

      case 3: {
        // 03 XX XX XX: Write X transparent bytes
        uint32_t count = r.get_u24b();
        for (uint32_t z = 0; z < count; z++) {
          ret.write(x, y, 0x00000000);
          x++;
        }
        break;
      }

      default:
        throw runtime_error(std::format("unknown command: {:02X}", cmd));
    }
  }

  return ret;
}

ImageRGBA8888 decode_HrSp(const string& data, const vector<ColorTableEntry>& clut, size_t header_size) {
  if (header_size < 8) {
    throw logic_error("header size is too small");
  }
  if (header_size & 3) {
    throw logic_error("header size must be a multiple of 4");
  }
  if (data.size() < header_size + 4) {
    throw invalid_argument("not enough data");
  }
  if (data.size() & 3) {
    throw invalid_argument("size must be a multiple of 4");
  }

  StringReader r(data.data(), data.size());
  r.go(4);
  uint16_t height = r.get_u16b();
  uint16_t width = r.get_u16b();
  r.go(header_size);

  return decode_HrSp_commands(r, width, height, clut);
}

vector<ImageRGBA8888> decode_SprD(const string& data, const vector<ColorTableEntry>& clut) {
  StringReader r(data.data(), data.size());

  vector<ImageRGBA8888> ret;
  while (!r.eof()) {
    r.skip(4);
    uint16_t height = r.get_u16b();
    uint16_t width = r.get_u16b();
    uint32_t command_bytes = r.get_u32b();
    size_t end_offset = r.where() + command_bytes;
    r.skip(8);
    StringReader sub_r = r.sub(r.where(), end_offset - r.where());
    ret.emplace_back(decode_HrSp_commands(sub_r, width, height, clut));
    r.go(end_offset);
  }

  return ret;
}

} // namespace ResourceDASM
