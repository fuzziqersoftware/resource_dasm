#include "DCData.hh"

#include <stdint.h>

#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <string>

#include "../ResourceFile.hh"

using namespace std;



struct ResourceHeader {
  uint32_t unknown1;
  uint16_t resource_count;
  uint16_t unknown2[2];

  void byteswap() {
    this->resource_count = bswap16(this->resource_count);
  }
} __attribute__((packed));

struct ResourceEntry {
  uint32_t offset;
  uint32_t size;
  uint32_t type;
  int16_t id;

  void byteswap() {
    this->offset = bswap32(this->offset);
    this->size = bswap32(this->size);
    this->type = bswap32(this->type);
    this->id = bswap16(this->id);
  }
} __attribute__((packed));

ResourceFile parse_dc_data(const string& data) {
  StringReader r(data);
  auto h = r.get_sw<ResourceHeader>();

  ResourceFile ret;
  for (size_t x = 0; x < h.resource_count; x++) {
    auto e = r.get_sw<ResourceEntry>();
    string data = r.preadx(e.offset, e.size);
    ret.add(ResourceFile::Resource(e.type, e.id, move(data)));
  }

  return ret;
}
