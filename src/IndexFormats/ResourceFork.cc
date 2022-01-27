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



#pragma pack(push)
#pragma pack(1)



struct ResourceForkHeader {
  uint32_t resource_data_offset;
  uint32_t resource_map_offset;
  uint32_t resource_data_size;
  uint32_t resource_map_size;

  void byteswap() {
    this->resource_data_offset = bswap32(this->resource_data_offset);
    this->resource_map_offset = bswap32(this->resource_map_offset);
    this->resource_data_size = bswap32(this->resource_data_size);
    this->resource_map_size = bswap32(this->resource_map_size);
  }
};

struct ResourceMapHeader {
  uint8_t reserved[16];
  uint32_t reserved_handle;
  uint16_t reserved_file_ref_num;
  uint16_t attributes;
  uint16_t resource_type_list_offset; // relative to start of this struct
  uint16_t resource_name_list_offset; // relative to start of this struct

  void byteswap() {
    this->attributes = bswap16(this->attributes);
    this->resource_type_list_offset = bswap16(this->resource_type_list_offset);
    this->resource_name_list_offset = bswap16(this->resource_name_list_offset);
  }
};

struct ResourceTypeListEntry {
  uint32_t resource_type;
  uint16_t num_items; // actually num_items - 1
  uint16_t reference_list_offset; // relative to start of type list

  void byteswap() {
    this->resource_type = bswap32(this->resource_type);
    this->num_items = bswap16(this->num_items);
    this->reference_list_offset = bswap16(this->reference_list_offset);
  }
};

struct ResourceReferenceListEntry {
  int16_t resource_id;
  uint16_t name_offset;
  uint32_t attributes_and_offset; // attr = high 8 bits; offset relative to resource data segment start
  uint32_t reserved;

  void byteswap() {
    this->resource_id = bswap16(this->resource_id);
    this->name_offset = bswap16(this->name_offset);
    this->attributes_and_offset = bswap32(this->attributes_and_offset);
  }
};



ResourceFile parse_resource_fork(const std::string& data) {
  // If the resource fork is empty, treat it as a valid index with no contents
  if (data.empty()) {
    return ResourceFile();
  }

  StringReader r(data.data(), data.size());

  ResourceForkHeader header = r.pget<ResourceForkHeader>(0);
  header.byteswap();

  ResourceMapHeader map_header = r.pget<ResourceMapHeader>(
      header.resource_map_offset);
  map_header.byteswap();

  // Overflow is ok here: the value 0xFFFF actually does mean the list is empty
  size_t type_list_offset = header.resource_map_offset + map_header.resource_type_list_offset;
  uint16_t num_resource_types = bswap16(r.pget<uint16_t>(type_list_offset)) + 1;

  vector<ResourceTypeListEntry> type_list_entries;
  for (size_t x = 0; x < num_resource_types; x++) {
    size_t entry_offset = type_list_offset + 2 + x * sizeof(ResourceTypeListEntry);
    ResourceTypeListEntry type_list_entry = r.pget<ResourceTypeListEntry>(entry_offset);
    type_list_entry.byteswap();
    type_list_entries.emplace_back(type_list_entry);
  }

  ResourceFile ret;
  for (const auto& type_list_entry : type_list_entries) {
    size_t base_offset = map_header.resource_type_list_offset +
        header.resource_map_offset + type_list_entry.reference_list_offset;
    for (size_t x = 0; x <= type_list_entry.num_items; x++) {
      ResourceReferenceListEntry ref_entry = r.pget<ResourceReferenceListEntry>(
          base_offset + x * sizeof(ResourceReferenceListEntry));
      ref_entry.byteswap();

      string name;
      if (ref_entry.name_offset != 0xFFFF) {
        size_t abs_name_offset = header.resource_map_offset + map_header.resource_name_list_offset + ref_entry.name_offset;
        uint8_t name_len = r.pget<uint8_t>(abs_name_offset);
        name = r.pread(abs_name_offset + 1, name_len);
      }

      size_t data_offset = header.resource_data_offset + (ref_entry.attributes_and_offset & 0x00FFFFFF);
      size_t data_size = bswap32(r.pget<uint32_t>(data_offset));
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

#pragma pack(pop)
