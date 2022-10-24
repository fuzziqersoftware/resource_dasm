#include "Codecs.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

using namespace std;



string unpack_bits(const void* data, size_t size) {
  StringReader r(data, size);
  StringWriter w;

  // Commands:
  // 0CCCCCCC <data> - write data (1 + C bytes of it) directly from the input
  // 1CCCCCCC DDDDDDDD - write (1 - C) bytes of D (C treated as negative number)
  // 10000000 - no-op (for some reason)
  while (!r.eof()) {
    int8_t cmd = r.get_s8();
    if (cmd == -128) {
      continue;
    } else if (cmd < 0) {
      uint8_t v = r.get_u8();
      size_t count = 1 - cmd;
      for (size_t z = 0; z < count; z++) {
        w.put_u8(v);
      }
    } else {
      size_t count = 1 + cmd;
      for (size_t z = 0; z < count; z++) {
        w.put_u8(r.get_u8());
      }
    }
  }

  return move(w.str());
}

string unpack_bits(const string& data) {
  return unpack_bits(data.data(), data.size());
}



string pack_bits(const void* data, size_t size) {
  // See unpack_bits (above) for descriptions of the commands.
  StringReader r(data, size);
  StringWriter w;

  size_t run_start_offset;
  while (!r.eof()) {
    uint8_t ch = r.get_u8();
    if (r.eof()) {
      // Only one byte left in the input; just write it verbatim
      w.put_u8(0x00);
      w.put_u8(ch);
      break;
    }

    run_start_offset = r.where() - 1;

    if (r.get_u8() == ch) { // Run of same byte
      while (((r.where() - run_start_offset) < 128) &&
             !r.eof() &&
             (r.get_u8(false) == ch)) {
        r.skip(1);
      }
      w.put_u8(1 - (r.where() - run_start_offset));
      w.put_u8(ch);

    } else { // Run of different bytes
      while (((r.where() - run_start_offset) < 128) &&
             !r.eof() &&
             (r.get_u8(false) != ch)) {
        ch = r.get_u8();
      }
      w.put_u8(r.where() - run_start_offset - 1);
      w.write(r.pread(run_start_offset, r.where() - run_start_offset));
    }
  }

  return move(w.str());
}

string pack_bits(const string& data) {
  return pack_bits(data.data(), data.size());
}



string decompress_packed_icns_data(const void* data, size_t size) {
  StringWriter w;
  StringReader r(data, size);
  while (!r.eof()) {
    uint16_t cmd = r.get_u8();
    if (cmd < 0x80) {
      // 00-7F: Write (cmd + 1) bytes directly from the input
      w.write(r.getv(cmd + 1), cmd + 1);
    } else {
      // 80-FF VV: Write (cmd - 0x80 + 3) bytes of VV
      size_t target_size = w.size() + (cmd - 0x80 + 3);
      uint8_t v = r.get_u8();
      while (w.size() < target_size) {
        w.put_u8(v);
      }
    }
  }
  return move(w.str());
}

string decompress_packed_icns_data(const string& data) {
  return decompress_packed_icns_data(data.data(), data.size());
}
