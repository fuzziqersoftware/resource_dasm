#include "System.hh"

#include <stdint.h>

#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

static const vector<uint16_t> const_table0({
    // clang-format off
    // 4B
                            0x0000, 0x4EBA, 0x0008, 0x4E75, 0x000C,
    // 50
    0x4EAD, 0x2053, 0x2F0B, 0x6100, 0x0010, 0x7000, 0x2F00, 0x486E,
    0x2050, 0x206E, 0x2F2E, 0xFFFC, 0x48E7, 0x3F3C, 0x0004, 0xFFF8,
    // 60
    0x2F0C, 0x2006, 0x4EED, 0x4E56, 0x2068, 0x4E5E, 0x0001, 0x588F,
    0x4FEF, 0x0002, 0x0018, 0x6000, 0xFFFF, 0x508F, 0x4E90, 0x0006,
    // 70
    0x266E, 0x0014, 0xFFF4, 0x4CEE, 0x000A, 0x000E, 0x41EE, 0x4CDF,
    0x48C0, 0xFFF0, 0x2D40, 0x0012, 0x302E, 0x7001, 0x2F28, 0x2054,
    // 80
    0x6700, 0x0020, 0x001C, 0x205F, 0x1800, 0x266F, 0x4878, 0x0016,
    0x41FA, 0x303C, 0x2840, 0x7200, 0x286E, 0x200C, 0x6600, 0x206B,
    // 90
    0x2F07, 0x558F, 0x0028, 0xFFFE, 0xFFEC, 0x22D8, 0x200B, 0x000F,
    0x598F, 0x2F3C, 0xFF00, 0x0118, 0x81E1, 0x4A00, 0x4EB0, 0xFFE8,
    // A0
    0x48C7, 0x0003, 0x0022, 0x0007, 0x001A, 0x6706, 0x6708, 0x4EF9,
    0x0024, 0x2078, 0x0800, 0x6604, 0x002A, 0x4ED0, 0x3028, 0x265F,
    // B0
    0x6704, 0x0030, 0x43EE, 0x3F00, 0x201F, 0x001E, 0xFFF6, 0x202E,
    0x42A7, 0x2007, 0xFFFA, 0x6002, 0x3D40, 0x0C40, 0x6606, 0x0026,
    // C0
    0x2D48, 0x2F01, 0x70FF, 0x6004, 0x1880, 0x4A40, 0x0040, 0x002C,
    0x2F08, 0x0011, 0xFFE4, 0x2140, 0x2640, 0xFFF2, 0x426E, 0x4EB9,
    // D0
    0x3D7C, 0x0038, 0x000D, 0x6006, 0x422E, 0x203C, 0x670C, 0x2D68,
    0x6608, 0x4A2E, 0x4AAE, 0x002E, 0x4840, 0x225F, 0x2200, 0x670A,
    // E0
    0x3007, 0x4267, 0x0032, 0x2028, 0x0009, 0x487A, 0x0200, 0x2F2B,
    0x0005, 0x226E, 0x6602, 0xE580, 0x670E, 0x660A, 0x0050, 0x3E00,
    // F0
    0x660C, 0x2E00, 0xFFEE, 0x206D, 0x2040, 0xFFE0, 0x5340, 0x6008,
    0x0480, 0x0068, 0x0B7C, 0x4400, 0x41E8, 0x4841,
    // clang-format on
});

static const vector<uint16_t> const_table1({
    // clang-format off
    // D5
                                            0x0000, 0x0001, 0x0002,
    0x0003, 0x2E01, 0x3E01, 0x0101, 0x1E01, 0xFFFF, 0x0E01, 0x3100,
    // E0
    0x1112, 0x0107, 0x3332, 0x1239, 0xED10, 0x0127, 0x2322, 0x0137,
    0x0706, 0x0117, 0x0123, 0x00FF, 0x002F, 0x070E, 0xFD3C, 0x0135,
    // F0
    0x0115, 0x0102, 0x0007, 0x003E, 0x05D5, 0x0201, 0x0607, 0x0708,
    0x3001, 0x0133, 0x0010, 0x1716, 0x373E, 0x3637,
    // clang-format on
});

static uint32_t read_encoded_int(StringReader& r) {
  uint32_t ret = r.get_u8();
  if (!(ret & 0x80)) {
    return ret;
  }
  if (ret == 0xFF) {
    return r.get_u32b();
  }
  ret = ((ret - 0xC0) << 8) | r.get_u8();
  // sign-extend from 15 bits
  return (ret & 0x4000) ? (ret | 0xFFFF8000) : ret;
}

string decompress_system01(
    const CompressedResourceHeader& header,
    const void* source,
    size_t size,
    bool is_system1) {
  StringReader r(source, size);
  StringWriter w;
  // Allocate an extra byte (see comment at the end for why)
  w.str().reserve(header.decompressed_size + 1);

  // In the original code, the working buffer is formatted like this:
  // uint16_t offset_offset; // offset to the next slot in the buffer (4 at start)
  // uint16_t string_start_offset_0; // offset to string data
  // uint16_t string_start_offset_1; // offset to string data
  // ... (more offsets)
  // uint16_t string_start_offset_N-1; // offset to string data
  // ... (unused space)
  // char string_data[length[N-1]]
  // char string_data[length[N-2]]
  // ... (more memoized strings)
  // char string_data[length[0]] (buffer ends after this)
  //
  // length[x] is string_start_offset[x] - string_start_offset[x + 1]
  //
  // We replace this with a vector<string>, since that's what it really is.
  vector<string> memo;

  // Note: Why do we & 0xFFFF in a bunch of places? The original code uses the
  // dbf instruction, which operates only on the lower 16 bits of the register
  // operand. In some rare cases, a compressed resource will have nonzero
  // garbage data in the high 16 bits of a 32-bit count field! To use the
  // correct count, we have to mask out the high bits.

  auto execute_extension_command = +[](StringReader& r, StringWriter& w) {
    switch (r.get_u8()) {
      case 0: { // <segnum> <count-1> <index>... - export table
        uint16_t index = 6;
        uint16_t segment_num = read_encoded_int(r);
        uint32_t count = read_encoded_int(r) & 0xFFFF;
        for (uint32_t x = 0; x < count; x++) {
          index += (read_encoded_int(r) - 6);
          w.put_u16b(0x3F3C);
          w.put_u16b(segment_num);
          w.put_u16b(0xA9F0);
          w.put_u16b(index);
        }
        w.put_u16b(0x3F3C);
        w.put_u16b(segment_num);
        w.put_u16b(0xA9F0);
        break;
      }

      case 1: { // <tgoff> <a5dlt> <count-1> <a5off> - jump table
        // <tgoff> <a5dlt> <count-1> <a5off> <a5off>... - if a5dlt is zero
        uint16_t target_offset = read_encoded_int(r);
        uint16_t a5_offset_delta = read_encoded_int(r);
        uint32_t count = (read_encoded_int(r) & 0xFFFF) + 1;
        uint16_t a5_offset = read_encoded_int(r);
        for (uint32_t x = 0; x < count; x++) {
          if (x) {
            target_offset -= 8;
            if (a5_offset_delta == 0) {
              a5_offset = read_encoded_int(r);
            } else {
              a5_offset += a5_offset_delta;
            }
          }
          w.put_u16b(0x6100);
          w.put_u16b(target_offset);
          w.put_u16b(0x4EED);
          w.put_u16b(a5_offset);
        }
        break;
      }

      case 2: { // <value> <count> - run-length encoded bytes
        uint8_t v = read_encoded_int(r);
        for (uint32_t count = (read_encoded_int(r) & 0xFFFF) + 1; count; count--) {
          w.put_u8(v);
        }
        break;
      }

      case 3: { // <value> <count> - run-length encoded words
        uint16_t v = read_encoded_int(r);
        for (uint32_t count = (read_encoded_int(r) & 0xFFFF) + 1; count; count--) {
          w.put_u16b(v);
        }
        break;
      }

      case 4: { // <start> <count-1> <diff8>... - words with difference encoding
        uint16_t v = read_encoded_int(r);
        uint32_t count = (read_encoded_int(r) & 0xFFFF) + 1;
        for (size_t x = 0; x < count; x++) {
          if (x) {
            uint16_t delta = r.get_u8();
            if (delta & 0x80) {
              delta |= 0xFF00;
            }
            v += delta;
          }
          w.put_u16b(v);
        }
        break;
      }

      case 5: { // <start> <count-1> <diff>... - words with difference encoding
        uint16_t v = read_encoded_int(r);
        uint32_t count = (read_encoded_int(r) & 0xFFFF) + 1;
        for (size_t x = 0; x < count; x++) {
          if (x) {
            v += read_encoded_int(r);
          }
          w.put_u16b(v);
        }
        break;
      }

      case 6: { // <start> <count-1> <diff>... - longs with difference encoding
        uint32_t v = read_encoded_int(r);
        uint32_t count = (read_encoded_int(r) & 0xFFFF) + 1;
        for (size_t x = 0; x < count; x++) {
          if (x) {
            v += read_encoded_int(r);
          }
          w.put_u32b(v);
        }
        break;
      }
    }
  };

  if (is_system1) {
    for (;;) {
      uint8_t command = r.get_u8();
      if (command < 0x10) { // <data> - raw data (fixed size)
        w.write(r.read(command + 1));
      } else if (command < 0x20) { // <data> - raw data (fixed size), memoize
        memo.emplace_back(r.read(command - 0x0F));
        w.write(memo.back());
      } else if (command < 0xD0) { // write memo string, fixed slot
        w.write(memo.at(command - 0x20));
      } else if (command == 0xD0) { // <size> <data> - raw data
        w.write(r.read(read_encoded_int(r)));
      } else if (command == 0xD1) { // <size> <data> - raw data, memoize
        memo.emplace_back(r.read(read_encoded_int(r)));
        w.write(memo.back());
      } else if (command == 0xD2) { // <slot8> - write memo string, slot + 0xB0
        w.write(memo.at(r.get_u8() + 0xB0));
      } else if (command == 0xD3) { // <slot8> - write memo string, slot + 0x1B0
        w.write(memo.at(r.get_u8() + 0x1B0));
      } else if (command == 0xD4) { // <slot16> - write memo string, slot + 0xB0
        w.write(memo.at(r.get_u16b() + 0xB0));
      } else if (command < 0xFE) { // write const word
        w.put_u16b(const_table1.at(command - 0xD5));
      } else if (command == 0xFE) { // extensions
        execute_extension_command(r, w);
      } else if (command == 0xFF) { // end of stream
        break;
      } else {
        throw logic_error("impossible command");
      }
    }
  } else {
    for (;;) {
      uint8_t command = r.get_u8();
      if (command == 0) { // <size> <data> - raw data; size is in words
        w.write(r.read(read_encoded_int(r) * 2));
      } else if (command < 0x10) { // <data> - raw data (fixed size)
        w.write(r.read(command * 2));
      } else if (command == 0x10) { // <size16> <data> - raw data, memoize
        memo.emplace_back(r.read(read_encoded_int(r) * 2));
        w.write(memo.back());
      } else if (command < 0x20) { // <data> - raw data (fixed size), memoize
        memo.emplace_back(r.read((command - 0x10) * 2));
        w.write(memo.back());
      } else if (command == 0x20) { // <slot8> - write memo string, slot + 0x28
        w.write(memo.at(r.get_u8() + 0x28));
      } else if (command == 0x21) { // <slot8> - write memo string, slot + 0x128
        w.write(memo.at(r.get_u8() + 0x128));
      } else if (command == 0x22) { // <slot16> - write memo string, slot + 0x28
        w.write(memo.at(r.get_u16b() + 0x28));
      } else if (command < 0x4B) { // write memo string, fixed slot
        w.write(memo.at(command - 0x23));
      } else if (command < 0xFE) { // write const word
        w.put_u16b(const_table0.at(command - 0x4B));
      } else if (command == 0xFE) { // extensions
        execute_extension_command(r, w);
      } else if (command == 0xFF) { // end of stream
        break;
      } else {
        throw logic_error("impossible command");
      }
    }
  }

  // Sometimes compressed resources write a few extra bytes at the end of the
  // output, presumably because they used some kind of word encoding and were
  // too lazy to trim off the extra byte, or used a faulty compressor. This is
  // probably technically a buffer overflow on actual classic Mac systems,
  // unless the Resource Manager explicitly allocates extra space for
  // decompression buffers. We just trim off the excess.
  if (w.str().size() > header.decompressed_size) {
    w.str().resize(header.decompressed_size);
  }

  return w.str();
}

string decompress_system0(
    const CompressedResourceHeader& header,
    const void* source,
    size_t size) {
  return decompress_system01(header, source, size, false);
}

string decompress_system1(
    const CompressedResourceHeader& header,
    const void* source,
    size_t size) {
  return decompress_system01(header, source, size, true);
}
