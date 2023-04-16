#include "Codecs.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

#include "../ResourceFile.hh"

using namespace std;

string decompress_dinopark_tycoon_lzss(const void* data, size_t size) {
  StringReader r(data, size);
  if (r.get_u32b() != 0x4C5A5353) { // 'LZSS'
    throw runtime_error("data is not DinoPark Tycoon LZSS");
  }
  size_t compressed_size = r.get_u32b();
  size_t decompressed_size = r.get_u32b();
  r.skip(4); // Unknown field; seems to always be zero?

  if (r.remaining() < compressed_size) {
    throw runtime_error("not all compressed data is present");
  }

  StringWriter w;
  while (w.size() < decompressed_size) {
    uint8_t control_bits = r.get_u8();
    for (size_t x = 0; (x < 8) && (w.size() < decompressed_size); x++) {
      if (control_bits & 1) {
        w.put_u8(r.get_u8());
      } else {
        uint16_t args = r.get_u16l();
        size_t offset = w.size() - (args >> 6);
        size_t count = (args & 0x3F) + 3;
        for (size_t x = 0; x < count; x++) {
          w.put_u8(w.str().at(offset++));
        }
      }
      control_bits >>= 1;
    }
  }

  if (w.size() != decompressed_size) {
    throw runtime_error(string_printf(
        "decompression produced 0x%zX bytes (expected 0x%zX bytes)",
        w.size(), decompressed_size));
  }

  return move(w.str());
}

string decompress_dinopark_tycoon_lzss(const string& data) {
  return decompress_dinopark_tycoon_lzss(data.data(), data.size());
}

string decompress_dinopark_tycoon_rle(const void* data, size_t size) {
  StringReader r(data, size);
  if (r.get_u32b() != 0x524C4520) { // 'RLE '
    throw runtime_error("data is not DinoPark Tycoon RLE");
  }
  size_t compressed_size = r.get_u32b();
  size_t decompressed_size = r.get_u32b();
  r.skip(4); // Unknown field; seems to always be zero?

  if (r.remaining() < compressed_size) {
    throw runtime_error("not all compressed data is present");
  }

  StringWriter w;
  while (!r.eof()) {
    uint8_t cmd = r.get_u8();
    if (cmd & 0x80) {
      uint8_t v = r.get_u8();
      size_t count = 0x101 - cmd;
      for (size_t z = 0; z < count; z++) {
        w.put_u8(v);
      }
    } else {
      size_t count = cmd + 1;
      for (size_t z = 0; z < count; z++) {
        w.put_u8(r.get_u8());
      }
    }
  }

  if (w.size() != decompressed_size) {
    throw runtime_error(string_printf(
        "decompression produced 0x%zX bytes (expected 0x%zX bytes)",
        w.size(), decompressed_size));
  }

  return move(w.str());
}

string decompress_dinopark_tycoon_rle(const string& data) {
  return decompress_dinopark_tycoon_rle(data.data(), data.size());
}

string decompress_dinopark_tycoon_data(const void* data, size_t size) {
  StringReader r(data, size);
  uint32_t format = r.get_u32b();
  if (format == 0x4C5A5353) { // 'LZSS'
    return decompress_dinopark_tycoon_lzss(data, size);
  } else if (r.get_u32b() != 0x524C4520) { // 'RLE '
    return decompress_dinopark_tycoon_rle(data, size);
  } else {
    return string(reinterpret_cast<const char*>(data), size);
  }
}

string decompress_dinopark_tycoon_data(const string& data) {
  return decompress_dinopark_tycoon_data(data.data(), data.size());
}
