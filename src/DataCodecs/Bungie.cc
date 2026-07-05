#include "Codecs.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

namespace ResourceDASM {

std::string unpack_pathways(const void* data, size_t size) {
  phosg::StringReader r(data, size);
  phosg::StringWriter w;

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

std::string unpack_pathways(const std::string& data) {
  return unpack_pathways(data.data(), data.size());
}

std::string decrypt_encrypt_odyssey(const std::string& data) {
  std::string ret;
  ret.reserve(data.size());
  for (size_t z = 0; z < data.size(); z++) {
    ret.push_back(data[z] ^ (z & 0xFF));
  }
  return ret;
}

} // namespace ResourceDASM
