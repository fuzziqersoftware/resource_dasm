#include "Formats.hh"

#include <stdint.h>

#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <string>

#include "../ResourceFile.hh"

namespace ResourceDASM {

struct ResourceHeader {
  phosg::be_uint32_t unknown1;
  phosg::be_uint16_t resource_count;
  phosg::be_uint16_t unknown2[2];
} __attribute__((packed));

struct ResourceEntry {
  phosg::be_uint32_t offset;
  phosg::be_uint32_t size;
  phosg::be_uint32_t type;
  phosg::be_int16_t id;
} __attribute__((packed));

ResourceFile parse_dc_data(const std::string& data) {
  phosg::StringReader r(data);
  const auto& h = r.get<ResourceHeader>();

  ResourceFile ret(IndexFormat::DC_DATA);
  for (size_t x = 0; x < h.resource_count; x++) {
    const auto& e = r.get<ResourceEntry>();
    ret.add(ResourceFile::Resource(e.type, e.id, r.preadx(e.offset, e.size)));
  }

  return ret;
}

} // namespace ResourceDASM
