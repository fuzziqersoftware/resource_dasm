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

namespace ResourceDASM {

struct MohawkFileHeader {
  phosg::be_uint32_t signature; // 'MHWK'
  phosg::be_uint32_t remaining_file_size; // == file_size - 8
  phosg::be_uint32_t resource_signature; // 'RSRC'
  phosg::be_uint16_t version;
  phosg::be_uint16_t compation_type;
  phosg::be_uint32_t file_size;
  phosg::be_uint32_t resource_dir_offset;
  phosg::be_uint16_t file_table_offset; // relative to resource dir base
  phosg::be_uint16_t file_table_size;
} __attribute__((packed));

struct ResourceTypeTable {
  phosg::be_uint16_t name_list_offset;
  phosg::be_uint16_t count;
  struct TypeEntry {
    phosg::be_uint32_t type;
    phosg::be_uint16_t resource_table_offset;
    phosg::be_uint16_t name_table_offset;
  } __attribute__((packed));
  TypeEntry entries[0];

  static uint32_t size_for_count(uint16_t count) {
    return sizeof(ResourceTypeTable) + count * sizeof(TypeEntry);
  }
} __attribute__((packed));

struct ResourceTable {
  phosg::be_uint16_t count;
  struct ResourceEntry {
    phosg::be_int16_t resource_id;
    phosg::be_uint16_t file_table_index;
  } __attribute__((packed));
  ResourceEntry entries[0];

  static uint32_t size_for_count(uint16_t count) {
    return sizeof(ResourceTable) + count * sizeof(ResourceEntry);
  }
} __attribute__((packed));

struct ResourceNameTable {
  phosg::be_uint16_t count;
  struct NameEntry {
    phosg::be_uint16_t name_offset;
    phosg::be_uint16_t resource_index;
  } __attribute__((packed));
  NameEntry entries[0];
} __attribute__((packed));

struct ResourceFileTable {
  phosg::be_uint32_t count;
  struct FileEntry {
    phosg::be_uint32_t data_offset;
    phosg::be_uint16_t size_low;
    uint8_t size_high;
    uint8_t flags;
    phosg::be_uint16_t unknown;

    uint32_t size() const {
      return this->size_low | (static_cast<uint32_t>(this->size_high) << 16);
    }
  } __attribute__((packed));
  FileEntry entries[0];

  static uint32_t size_for_count(uint16_t count) {
    return sizeof(ResourceFileTable) + count * sizeof(FileEntry);
  }
} __attribute__((packed));

struct ResourceEntry {
  uint32_t type;
  int16_t id;
  uint32_t offset;
  uint32_t size;
};

static std::vector<ResourceEntry> load_index(phosg::StringReader& r) {
  MohawkFileHeader h = r.get<MohawkFileHeader>();
  if (h.signature != 0x4D48574B) {
    throw std::runtime_error("file is not a mohawk archive");
  }
  if (h.resource_signature != 0x52535243) {
    throw std::runtime_error("file is not a mohawk resource archive");
  }

  uint16_t type_table_count = r.pget_u16b(h.resource_dir_offset + 2);
  const auto& type_table = r.pget<ResourceTypeTable>(
      h.resource_dir_offset, ResourceTypeTable::size_for_count(type_table_count));

  uint32_t file_table_offset = h.resource_dir_offset + h.file_table_offset;
  uint32_t file_table_count = r.pget_u32b(file_table_offset);
  std::string file_table_data = r.pread(file_table_offset, ResourceFileTable::size_for_count(file_table_count));
  const ResourceFileTable* file_table = reinterpret_cast<ResourceFileTable*>(file_table_data.data());

  std::vector<ResourceEntry> ret;
  for (size_t type_index = 0; type_index < type_table.count; type_index++) {
    const auto& type_table_entry = type_table.entries[type_index];

    uint32_t res_table_offset = h.resource_dir_offset + type_table_entry.resource_table_offset;
    uint16_t res_table_count = r.pget_u16b(res_table_offset);
    const auto& res_table = r.pget<ResourceTable>(res_table_offset, ResourceTable::size_for_count(res_table_count));

    for (size_t res_index = 0; res_index < res_table.count; res_index++) {
      const auto& res_entry = res_table.entries[res_index];
      if ((res_entry.file_table_index < 1) || (res_entry.file_table_index > file_table_count)) {
        throw std::runtime_error("file entry reference out of range");
      }
      const auto& file_entry = file_table->entries[res_entry.file_table_index - 1];
      ret.emplace_back(ResourceEntry{type_table_entry.type, res_entry.resource_id, file_entry.data_offset, file_entry.size()});
    }
  }

  return ret;
}

ResourceFile parse_mohawk(const std::string& data) {
  phosg::StringReader r(data.data(), data.size());

  ResourceFile ret(IndexFormat::MOHAWK);
  std::vector<ResourceEntry> resource_entries = load_index(r);
  for (const auto& e : resource_entries) {
    ret.add(ResourceFile::Resource{e.type, e.id, r.pread(e.offset, e.size)});
  }

  return ret;
}

} // namespace ResourceDASM
