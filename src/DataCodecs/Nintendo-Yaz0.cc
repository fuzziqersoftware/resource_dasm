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

struct Yaz0Header {
  static constexpr uint32_t EXPECTED_SIGNATURE = resource_type("Yaz0");
  phosg::be_uint32_t signature = EXPECTED_SIGNATURE;
  phosg::be_uint32_t decompressed_size = 0;
  uint8_t unknown_a1[8] = {};
} __attribute__((packed));

std::string decompress_Yaz0(const void* data, size_t size) {
  // Based on algorithm as described in YAGCD (https://hitmen.c02.at/files/yagcd/yagcd/chap16.html#sec16.2)
  phosg::StringReader r(data, size);
  phosg::StringWriter w;

  const auto& header = r.get<Yaz0Header>();
  if (header.signature != Yaz0Header::EXPECTED_SIGNATURE) {
    throw std::runtime_error("Input is not in Yaz0 format");
  }

  uint16_t control_bits = 0;
  while (!r.eof()) {
    if (!(control_bits & 0x80)) {
      control_bits = (r.get_u8() << 8) | 0x00FF;
    }

    if (control_bits & 0x8000) {
      w.put_u8(r.get_u8());
    } else {
      uint16_t params = r.get_u16b();
      size_t backref_size = (params >> 12) & 0xF;
      if (backref_size == 0) {
        backref_size = r.get_u8() + 0x12;
      } else {
        backref_size += 2;
      }
      size_t backref_offset = (params & 0x0FFF) + 1;
      for (size_t z = 0; z < backref_size; z++) {
        w.put_u8(w.str().at(w.str().size() - backref_offset));
      }
    }

    control_bits <<= 1;
  }

  if (w.size() != header.decompressed_size) {
    throw std::runtime_error(std::format(
        "Decompression produced incorrect amount of data (expected 0x{:X}, received 0x{:X})",
        header.decompressed_size, w.size()));
  }

  return std::move(w.str());
}

std::string decompress_Yaz0(const std::string& data) {
  return decompress_Yaz0(data.data(), data.size());
}

} // namespace ResourceDASM
