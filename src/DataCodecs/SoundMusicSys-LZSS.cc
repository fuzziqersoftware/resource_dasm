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
using namespace phosg;

namespace ResourceDASM {

string decompress_soundmusicsys_lzss(const void* vsrc, size_t size) {
  StringReader r(vsrc, size);
  string ret;

  for (;;) {
    if (r.eof()) {
      return ret;
    }
    uint8_t control_bits = r.get_u8();

    for (uint8_t control_mask = 0x01; control_mask; control_mask <<= 1) {
      if (control_bits & control_mask) {
        if (r.eof()) {
          return ret;
        }
        ret += static_cast<char>(r.get_s8());

      } else {
        if (r.where() >= r.size() - 1) {
          return ret;
        }
        uint16_t params = r.get_u16b();

        size_t copy_offset = ret.size() - ((1 << 12) - (params & 0x0FFF));
        uint8_t count = ((params >> 12) & 0x0F) + 3;
        size_t copy_end_offset = copy_offset + count;

        for (; copy_offset != copy_end_offset; copy_offset++) {
          ret += ret.at(copy_offset);
        }
      }
    }
  }
  return ret;
}

string decompress_soundmusicsys_lzss(const string& data) {
  return decompress_soundmusicsys_lzss(data.data(), data.size());
}

} // namespace ResourceDASM
