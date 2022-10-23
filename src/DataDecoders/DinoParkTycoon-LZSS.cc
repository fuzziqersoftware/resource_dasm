#include "Decoders.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

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

  string ret = decompress_soundmusicsys_lzss(r.getv(compressed_size), compressed_size);
  if (ret.size() != decompressed_size) {
    throw runtime_error(string_printf(
        "decompression produced 0x%zX bytes (expected 0x%zX bytes)",
        ret.size(), decompressed_size));
  }
  return ret;
}

string decompress_dinopark_tycoon_lzss(const string& data) {
  return decompress_dinopark_tycoon_lzss(data.data(), data.size());
}
