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

string decompress_presage_lzss(StringReader& r, size_t max_output_bytes) {
  size_t decompressed_size = max_output_bytes ? max_output_bytes : r.get_u32b();

  StringWriter w;
  while (w.size() < decompressed_size) {
    uint8_t control_bits = r.get_u8();
    for (size_t x = 0; (x < 8) && (w.size() < decompressed_size); x++) {
      bool is_backreference = control_bits & 1;
      control_bits >>= 1;
      if (is_backreference) {
        uint16_t args = r.get_u16b();
        size_t offset = w.size() - ((args & 0x0FFF) + 1);
        size_t count = ((args >> 12) & 0x000F) + 3;
        for (size_t x = 0; x < count; x++) {
          w.put_u8(w.str().at(offset++));
        }
      } else {
        w.put_u8(r.get_u8());
      }
    }
  }

  return std::move(w.str());
}

string decompress_presage_lzss(const void* data, size_t size, size_t max_output_bytes) {
  StringReader r(data, size);
  return decompress_presage_lzss(r, max_output_bytes);
}

string decompress_presage_lzss(const string& data, size_t max_output_bytes) {
  return decompress_presage_lzss(data.data(), data.size(), max_output_bytes);
}
