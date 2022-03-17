#include "ResourceFork.hh"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <phosg/Strings.hh>
#include <stdexcept>
#include <vector>
#include <string>

#include "../ResourceFile.hh"

using namespace std;



struct ResourceForkHeader {
  be_uint32_t resource_data_offset;
  be_uint32_t resource_map_offset;
  be_uint32_t resource_data_size;
  be_uint32_t resource_map_size;
} __attribute__((packed));

struct ResourceMapHeader {
  uint8_t reserved[16];
  be_uint32_t reserved_handle;
  be_uint16_t reserved_file_ref_num;
  be_uint16_t attributes;
  be_uint16_t resource_type_list_offset; // relative to start of this struct
  be_uint16_t resource_name_list_offset; // relative to start of this struct
} __attribute__((packed));

struct ResourceTypeListEntry {
  be_uint32_t resource_type;
  be_uint16_t num_items; // actually num_items - 1
  be_uint16_t reference_list_offset; // relative to start of type list
} __attribute__((packed));

struct ResourceReferenceListEntry {
  be_int16_t resource_id;
  be_uint16_t name_offset;
  be_uint32_t attributes_and_offset; // attr = high 8 bits; offset relative to resource data segment start
  be_uint32_t reserved;
} __attribute__((packed));



ResourceFile parse_resource_fork(const std::string& data) {
  ResourceFile ret(IndexFormat::ResourceFork);

  // If the resource fork is empty, treat it as a valid index with no contents
  if (data.empty()) {
    return ret;
  }

  StringReader r(data.data(), data.size());

  const auto& header = r.pget<ResourceForkHeader>(0);
  const auto& map_header = r.pget<ResourceMapHeader>(header.resource_map_offset);

  // Overflow is ok here: the value 0xFFFF actually does mean the list is empty
  size_t type_list_offset = header.resource_map_offset + map_header.resource_type_list_offset;
  uint16_t num_resource_types = r.pget_u16b(type_list_offset) + 1;

  vector<ResourceTypeListEntry> type_list_entries;
  for (size_t x = 0; x < num_resource_types; x++) {
    size_t entry_offset = type_list_offset + 2 + x * sizeof(ResourceTypeListEntry);
    const auto& type_list_entry = r.pget<ResourceTypeListEntry>(entry_offset);
    type_list_entries.emplace_back(type_list_entry);
  }

  for (const auto& type_list_entry : type_list_entries) {
    size_t base_offset = map_header.resource_type_list_offset +
        header.resource_map_offset + type_list_entry.reference_list_offset;
    for (size_t x = 0; x <= type_list_entry.num_items; x++) {
      const auto& ref_entry = r.pget<ResourceReferenceListEntry>(
          base_offset + x * sizeof(ResourceReferenceListEntry));

      string name;
      if (ref_entry.name_offset != 0xFFFF) {
        size_t abs_name_offset = header.resource_map_offset + map_header.resource_name_list_offset + ref_entry.name_offset;
        uint8_t name_len = r.pget<uint8_t>(abs_name_offset);
        name = r.pread(abs_name_offset + 1, name_len);
      }

      size_t data_offset = header.resource_data_offset + (ref_entry.attributes_and_offset & 0x00FFFFFF);
      size_t data_size = r.pget_u32b(data_offset);
      uint8_t attributes = (ref_entry.attributes_and_offset >> 24) & 0xFF;
      string data = r.preadx(data_offset + 4, data_size);
      ResourceFile::Resource res(
          type_list_entry.resource_type,
          ref_entry.resource_id,
          attributes,
          name,
          move(data));

      ret.add(move(res));
    }
  }

  return ret;
}

std::string serialize_resource_fork(const ResourceFile&) {
  // TODO
  throw runtime_error("serialize_resource_fork is not yet implemented");
}
