#include "Formats.hh"

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
  // Base offset for all resource data. In reference list entries, the offset in
  // attributes_and_offset (low 3 bytes) is relative to this offset.
  be_uint32_t resource_data_offset;
  // Offset to the ResourceMapHeader struct (from beginning of file)
  be_uint32_t resource_map_offset;
  // Size of all resource data
  be_uint32_t resource_data_size;
  // Size of resource map, including header and all entries
  be_uint32_t resource_map_size;
} __attribute__((packed));

struct ResourceMapHeader {
  // Reserved fields are all set to zero
  uint8_t reserved[16];
  be_uint32_t reserved_handle;
  be_uint16_t reserved_file_ref_num;
  // File attributes (unused??)
  be_uint16_t attributes;
  // Offsets to type list and name list, relative to start of this struct
  be_uint16_t resource_type_list_offset;
  be_uint16_t resource_name_list_offset;
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
  ResourceFile ret(IndexFormat::RESOURCE_FORK);

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



std::string serialize_resource_fork(const ResourceFile& rf) {
  // We currently parse an empty resource fork as a valid resource map with no
  // resources. It seems this is what Mac OS does too, so it should be safe to
  // serialize an empty ResourceFile as an empty string.
  auto all_res_ids = rf.all_resources();
  if (all_res_ids.empty()) {
    return "";
  }

  // First, count all resources by type
  unordered_map<uint32_t, size_t> type_to_count;
  for (const auto& it : all_res_ids) {
    try {
      type_to_count.at(it.first)++;
    } catch (const out_of_range&) {
      type_to_count[it.first] = 1;
    }
  }

  if (type_to_count.empty()) {
    throw logic_error("no resources present, but did not catch this before");
  }
  if (type_to_count.size() > 0xFFFF) {
    throw runtime_error("too many resource types present");
  }

  StringWriter type_list_w;
  type_list_w.put_u16b(type_to_count.size() - 1);
  size_t type_list_bytes = 2 + (8 * type_to_count.size());

  StringWriter data_w;
  StringWriter names_w;
  StringWriter reflist_w;
  uint64_t current_type = 0xFFFFFFFFFFFFFFFF;
  for (const auto& it : all_res_ids) {
    auto res = rf.get_resource(it.first, it.second);
    if (current_type != res->type) {
      current_type = res->type;
      size_t count = type_to_count.at(res->type);
      if (count == 0) {
        throw logic_error("no resources of this type, but did not catch this before");
      }
      if (count > 0xFFFF) {
        throw runtime_error("too many resources of this type");
      }
      size_t first_ref_offset = type_list_bytes + reflist_w.size();
      if (first_ref_offset > 0xFFFF) {
        throw runtime_error("reflist offset for type too large");
      }
      size_t start_offset = type_list_bytes + reflist_w.size();
      if (start_offset > 0xFFFF) {
        throw runtime_error("reference list too large");
      }
      ResourceTypeListEntry type_list_entry = {res->type, count - 1, start_offset};
      type_list_w.put(type_list_entry);
    }

    ResourceReferenceListEntry reflist_entry;
    reflist_entry.resource_id = res->id;
    reflist_entry.reserved = 0;

    if (data_w.size() > 0x00FFFFFF) {
      throw runtime_error("resource data segment is too large");
    }
    if (res->data.size() > 0xFFFFFFFF) {
      throw runtime_error("resource is too large to serialize");
    }
    reflist_entry.attributes_and_offset = (res->flags << 24) | data_w.size();
    data_w.put_u32b(res->data.size());
    data_w.write(res->data);

    if (!res->name.empty()) {
      reflist_entry.name_offset = names_w.size();
      if (names_w.size() >= 0xFFFF) {
        throw runtime_error("resource name segment is too large");
      }
      if (res->name.size() > 0xFF) {
        throw runtime_error("resource name is too long");
      }
      names_w.put_u8(res->name.size());
      names_w.write(res->name);

    } else {
      reflist_entry.name_offset = 0xFFFF;
    }

    reflist_w.put(reflist_entry);
  }

  if (type_list_w.size() != type_list_bytes) {
    throw logic_error("incorrect amount of data produced for type list");
  }

  StringWriter main_w;

  ResourceForkHeader header;
  // Note that a 112-byte reserved header follows the main header, and a
  // 128-byte application zone follows that, so the minimum offsets in the main
  // header's offset fields are 0x00000100. It's not clear if this rule is
  // enforced at load time by the Resource Manager (and we don't enforce it in
  // the parsing function above) but we'll generate the extra space since it's
  // clearly documented in Inside Macintosh.
  header.resource_data_offset = 0x100;
  header.resource_map_offset = data_w.size() + header.resource_data_offset;
  header.resource_data_size = data_w.size();
  header.resource_map_size = sizeof(ResourceMapHeader) + type_list_w.size() + reflist_w.size() + names_w.size();
  main_w.put(header);
  main_w.extend_to(0x100);
  main_w.write(data_w.str());

  if (main_w.size() != header.resource_map_offset) {
    throw logic_error("resource map written at incorrect offset");
  }

  size_t name_list_offset = sizeof(ResourceMapHeader) + type_list_w.size() + reflist_w.size();
  if (name_list_offset > 0xFFFF) {
    throw runtime_error("name list offset is too large");
  }

  ResourceMapHeader map_header;
  memset(map_header.reserved, 0, sizeof(map_header.reserved));
  map_header.reserved_handle = 0;
  map_header.reserved_file_ref_num = 0;
  map_header.attributes = 0; // TODO: Should this be a specific value?
  map_header.resource_type_list_offset = sizeof(map_header);
  map_header.resource_name_list_offset = name_list_offset;
  main_w.put(map_header);

  main_w.write(type_list_w.str());
  main_w.write(reflist_w.str());
  main_w.write(names_w.str());

  return main_w.str();
}
