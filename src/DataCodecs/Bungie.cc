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

string unpack_pathways(const void* data, size_t size) {
  StringReader r(data, size);
  StringWriter w;

  size_t decompressed_size = r.get_u32b();
  while (w.size() < decompressed_size) {
    uint8_t cmd = r.get_u8();
    if (cmd >= 0x80) {
      for (size_t count = cmd - 0x7F; count > 0; count--) {
        w.put_u8(r.get_u8());
      }
    } else {
      uint8_t v = r.get_u8();
      for (size_t count = cmd + 3; count; count--) {
        w.put_u8(v);
      }
    }
  }

  return std::move(w.str());
}

string unpack_pathways(const string& data) {
  return unpack_pathways(data.data(), data.size());
}

} // namespace ResourceDASM
