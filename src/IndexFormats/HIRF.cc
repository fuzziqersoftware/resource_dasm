#include "Formats.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <exception>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <vector>

#include "../ResourceFile.hh"

using namespace std;

struct HIRFFileHeader {
  be_uint32_t magic; // 'IREZ'
  be_uint32_t version; // == 1
  be_uint32_t num_resources;
} __attribute__((packed));

struct HIRFTopLevelResourceHeader {
  be_uint32_t next_res_offset; // For last resource: >= the file size
  be_uint32_t type;
  be_uint32_t id;
  uint8_t name_length;
  // A variable-length field follows, which means the rest of the struct has to
  // be handled manually at the read callsite:
  // char name[name_length];
  // uint32_t size;
} __attribute__((packed));

ResourceFile parse_hirf(const string& data) {
  StringReader r(data.data(), data.size());

  const auto& header = r.get<HIRFFileHeader>();
  if (header.magic != 0x4952455A) {
    throw runtime_error("file is not a HIRF archive");
  }
  if (header.version != 1) {
    throw runtime_error("unsupported HIRF version");
  }

  ResourceFile ret(IndexFormat::HIRF);
  while (!r.eof()) {
    const auto& res_header = r.get<HIRFTopLevelResourceHeader>();
    string name = r.read(res_header.name_length);
    uint32_t size = r.get_u32b();

    ResourceFile::Resource res(res_header.type, res_header.id, r.read(size));
    ret.add(std::move(res));

    r.go(res_header.next_res_offset);
  }

  return ret;
}
