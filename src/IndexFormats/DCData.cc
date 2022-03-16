#include "DCData.hh"

#include <stdint.h>

#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <string>

#include "../ResourceFile.hh"

using namespace std;



struct ResourceHeader {
  be_uint32_t unknown1;
  be_uint16_t resource_count;
  be_uint16_t unknown2[2];
} __attribute__((packed));

struct ResourceEntry {
  be_uint32_t offset;
  be_uint32_t size;
  be_uint32_t type;
  be_int16_t id;
} __attribute__((packed));

ResourceFile parse_dc_data(const string& data) {
  StringReader r(data);
  const auto& h = r.get<ResourceHeader>();

  ResourceFile ret;
  for (size_t x = 0; x < h.resource_count; x++) {
    const auto& e = r.get<ResourceEntry>();
    string data = r.preadx(e.offset, e.size);
    ret.add(ResourceFile::Resource(e.type, e.id, move(data)));
  }

  return ret;
}
