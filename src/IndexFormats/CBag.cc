#include "Formats.hh"

#include <stdint.h>

#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <string>

#include "../ResourceFile.hh"

namespace ResourceDASM {

struct CBagEntry {
  phosg::be_uint32_t type;
  phosg::be_int16_t id;
  phosg::be_uint16_t unknown_a1;
  phosg::be_uint32_t data_offset;
  phosg::be_uint32_t data_size;
  uint8_t name_length;
  char name[0x3F];
} __attribute__((packed));

ResourceFile parse_cbag(const std::string& data) {
  phosg::StringReader r(data);

  uint32_t count = r.get_u32b();

  ResourceFile ret(IndexFormat::CBAG);
  for (size_t z = 0; z < count; z++) {
    const auto& entry = r.get<CBagEntry>();
    std::string name(entry.name, std::min<size_t>(sizeof(entry.name), entry.name_length));
    std::string data = r.pread(entry.data_offset, entry.data_size);
    ResourceFile::Resource res(entry.type, entry.id, 0, std::move(name), std::move(data));
    ret.add(std::move(res));
  }
  return ret;
}

} // namespace ResourceDASM
