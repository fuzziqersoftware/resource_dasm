#include "Formats.hh"

#include <stdint.h>

#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <string>

#include "../ResourceFile.hh"

using namespace std;



struct CBagEntry {
  be_uint32_t type;
  be_int16_t id;
  be_uint16_t unknown_a1;
  be_uint32_t data_offset;
  be_uint32_t data_size;
  uint8_t name_length;
  char name[0x3F];
} __attribute__((packed));

ResourceFile parse_cbag(const string& data) {
  StringReader r(data);

  uint32_t count = r.get_u32b();

  ResourceFile ret(IndexFormat::CBAG);
  for (size_t z = 0; z < count; z++) {
    const auto& entry = r.get<CBagEntry>();
    string name(entry.name, min<size_t>(sizeof(entry.name), entry.name_length));
    string data = r.pread(entry.data_offset, entry.data_size);
    ResourceFile::Resource res(entry.type, entry.id, 0, move(name), move(data));
    ret.add(move(res));
  }
  return ret;
}
