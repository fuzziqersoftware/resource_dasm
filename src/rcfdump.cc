#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

struct RCFHeader {
  char ident[0x20];
  phosg::be_uint32_t unknown;
  phosg::be_uint32_t index_offset;
} __attribute__((packed));

struct RCFIndexHeader {
  phosg::be_uint32_t count;
  phosg::be_uint32_t names_offset;
  phosg::be_uint32_t unknown[2];
} __attribute__((packed));

struct RCFIndexEntry {
  phosg::be_uint32_t crc32;
  phosg::be_uint32_t offset;
  phosg::be_uint32_t size;
} __attribute__((packed));

vector<string> parse_names_index(const string& data, size_t offset) {
  // For some reason this isn't reverse-endian... weird
  uint32_t num_names = *reinterpret_cast<const uint32_t*>(&data[offset]);
  offset += 8;

  vector<string> ret;
  while (ret.size() < num_names) {
    uint32_t len = *reinterpret_cast<const uint32_t*>(&data[offset]);
    ret.emplace_back(&data[offset + 4], len - 1);
    offset += (len + 8);
  }

  return ret;
}

unordered_map<string, RCFIndexEntry> get_index(const string& data, size_t offset) {
  RCFIndexHeader header;
  memcpy(&header, &data[offset], sizeof(RCFIndexHeader));
  offset += sizeof(RCFIndexHeader);

  vector<string> names = parse_names_index(data, header.names_offset);
  if (header.count != names.size()) {
    throw runtime_error("name count and file count do not match");
  }

  unordered_map<string, RCFIndexEntry> ret;
  while (ret.size() < header.count) {
    const string& name = names.at(ret.size());
    auto& entry = ret[name];

    memcpy(&entry, &data[offset], sizeof(RCFIndexEntry));
    offset += sizeof(RCFIndexEntry);
  }

  return ret;
}

int main(int argc, char* argv[]) {

  if (argc != 2) {
    phosg::fwrite_fmt(stderr, "Usage: rcfdump <filename>\n");
    return -1;
  }

  string data = phosg::load_file(argv[1]);
  RCFHeader header;
  memcpy(&header, data.data(), sizeof(RCFHeader));
  if (strcmp(header.ident, "RADCORE CEMENT LIBRARY")) {
    phosg::fwrite_fmt(stderr, "file does not appear to be an rcf archive\n");
    return 2;
  }

  auto index = get_index(data, header.index_offset);

  for (const auto& it : index) {
    const string& name = it.first;
    const auto& entry = it.second;
    phosg::fwrite_fmt(stdout, "... {:08X} {:08X} {:08X} {}\n", entry.crc32.load(), entry.offset.load(), entry.size.load(), name);

    phosg::save_file(name, data.substr(entry.offset, entry.size));
  }

  return 0;
}
