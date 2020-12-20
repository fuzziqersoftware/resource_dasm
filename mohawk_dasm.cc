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

#include "resource_fork.hh"

using namespace std;



struct MohawkFileHeader {
  uint32_t signature; // 'MHWK'
  uint32_t remaining_file_size; // == file_size - 8
  uint32_t resource_signature; // 'RSRC'
  uint16_t version;
  uint16_t unused1;
  uint32_t file_size;
  uint32_t resource_dir_offset;
  uint16_t file_table_offset; // relative to resource dir base
  uint16_t file_table_size;

  void byteswap() {
    this->signature = bswap32(this->signature);
    this->remaining_file_size = bswap32(this->remaining_file_size);
    this->resource_signature = bswap32(this->resource_signature);
    this->version = bswap16(this->version);
    this->unused1 = bswap16(this->unused1);
    this->file_size = bswap32(this->file_size);
    this->resource_dir_offset = bswap32(this->resource_dir_offset);
    this->file_table_offset = bswap16(this->file_table_offset);
    this->file_table_size = bswap16(this->file_table_size);
  }
} __attribute__((packed));

struct ResourceTypeTable {
  uint16_t name_list_offset;
  uint16_t count;
  struct TypeEntry {
    uint32_t type;
    uint16_t resource_table_offset;
    uint16_t name_table_offset;
  } __attribute__((packed));
  TypeEntry entries[0];

  void byteswap() {
    this->name_list_offset = bswap16(this->name_list_offset);
    this->count = bswap16(this->count);
    for (size_t x = 0; x < this->count; x++) {
      auto& e = this->entries[x];
      // note: we intentionally don't byteswap type
      e.resource_table_offset = bswap16(e.resource_table_offset);
      e.name_table_offset = bswap16(e.name_table_offset);
    }
  }

  static uint32_t size_for_count(uint16_t count) {
    return sizeof(ResourceTypeTable) + count * sizeof(TypeEntry);
  }
} __attribute__((packed));

struct ResourceTable {
  uint16_t count;
  struct ResourceEntry {
    uint16_t resource_id;
    uint16_t file_table_index;
  } __attribute__((packed));
  ResourceEntry entries[0];

  void byteswap() {
    this->count = bswap16(this->count);
    for (size_t x = 0; x < this->count; x++) {
      auto& e = this->entries[x];
      e.resource_id = bswap16(e.resource_id);
      e.file_table_index = bswap16(e.file_table_index);
    }
  }

  static uint32_t size_for_count(uint16_t count) {
    return sizeof(ResourceTable) + count * sizeof(ResourceEntry);
  }
} __attribute__((packed));

struct ResourceNameTable {
  uint16_t count;
  struct NameEntry {
    uint16_t name_offset;
    uint16_t resource_index;
  } __attribute__((packed));
  NameEntry entries[0];

  void byteswap() {
    this->count = bswap16(this->count);
    for (size_t x = 0; x < this->count; x++) {
      auto& e = this->entries[x];
      e.name_offset = bswap16(e.name_offset);
      e.resource_index = bswap16(e.resource_index);
    }
  }
} __attribute__((packed));

struct ResourceFileTable {
  uint32_t count;
  struct FileEntry {
    uint32_t data_offset;
    uint16_t size_low;
    uint8_t size_high;
    uint8_t flags;
    uint16_t unknown;

    uint32_t size() const {
      return this->size_low | (static_cast<uint32_t>(this->size_high) << 16);
    }
  } __attribute__((packed));
  FileEntry entries[0];

  void byteswap() {
    this->count = bswap32(this->count);
    for (size_t x = 0; x < this->count; x++) {
      auto& e = this->entries[x];
      e.data_offset = bswap32(e.data_offset);
      e.size_low = bswap16(e.size_low);
    }
  }

  static uint32_t size_for_count(uint16_t count) {
    return sizeof(ResourceFileTable) + count * sizeof(FileEntry);
  }
} __attribute__((packed));



struct ResourceEntry {
  uint32_t type;
  uint16_t id;
  uint32_t offset;
  uint32_t size;

  ResourceEntry(uint32_t type, uint16_t id, uint32_t offset, uint32_t size) :
      type(type), id(id), offset(offset), size(size) { }
};

vector<ResourceEntry> load_index(int fd) {
  MohawkFileHeader h = preadx<MohawkFileHeader>(fd, 0);
  h.byteswap();
  if (h.signature != 0x4D48574B) {
    throw runtime_error("file does not appear to be a mohawk archive");
  }
  if (h.resource_signature != 0x52535243) {
    throw runtime_error("file does not appear to be a mohawk resource archive");
  }

  uint16_t type_table_count = preadx<uint16_t>(fd, h.resource_dir_offset + 2);
  type_table_count = bswap16(type_table_count);
  string type_table_data = preadx(fd, ResourceTypeTable::size_for_count(type_table_count), h.resource_dir_offset);
  ResourceTypeTable* type_table = reinterpret_cast<ResourceTypeTable*>(const_cast<char*>(type_table_data.data()));
  type_table->byteswap();

  uint32_t file_table_offset = h.resource_dir_offset + h.file_table_offset;
  uint32_t file_table_count = preadx<uint32_t>(fd, file_table_offset);
  file_table_count = bswap32(file_table_count);
  string file_table_data = preadx(fd, ResourceFileTable::size_for_count(file_table_count), file_table_offset);
  ResourceFileTable* file_table = reinterpret_cast<ResourceFileTable*>(const_cast<char*>(file_table_data.data()));
  file_table->byteswap();

  vector<ResourceEntry> ret;
  for (size_t type_index = 0; type_index < type_table->count; type_index++) {
    const auto& type_table_entry = type_table->entries[type_index];

    uint32_t res_table_offset = h.resource_dir_offset + type_table_entry.resource_table_offset;
    uint16_t res_table_count = preadx<uint16_t>(fd, res_table_offset);
    res_table_count = bswap16(res_table_count);
    string res_table_data = preadx(fd, ResourceTable::size_for_count(res_table_count), res_table_offset);
    ResourceTable* res_table = reinterpret_cast<ResourceTable*>(const_cast<char*>(res_table_data.data()));
    res_table->byteswap();

    for (size_t res_index = 0; res_index < res_table->count; res_index++) {
      const auto& res_entry = res_table->entries[res_index];
      const auto& file_entry = file_table->entries[res_entry.file_table_index - 1];

      ret.emplace_back(type_table_entry.type, res_entry.resource_id,
          file_entry.data_offset, file_entry.size());
    }
  }

  return ret;
}



struct ResourceDataHeader {
  uint32_t signature;
  uint32_t size;
  uint32_t type;

  void byteswap() {
    this->signature = bswap32(this->signature);
    this->size = bswap32(this->size);
    // note: we intentionally don't byteswap the type
  }
} __attribute__((packed));

string get_resource_data(int fd, const ResourceEntry& e) {
  ResourceDataHeader h = preadx<ResourceDataHeader>(fd, e.offset);
  h.byteswap();
  return preadx(fd, h.size - 4, e.offset + sizeof(ResourceDataHeader));
}



int main(int argc, char* argv[]) {
  printf("fuzziqer software mohawk archive disassembler\n\n");

  if (argc <= 1) {
    fprintf(stderr, "no filename given\n");
    return 1;
  }

  scoped_fd fd(argv[1], O_RDONLY);

  vector<ResourceEntry> resources = load_index(fd);

  for (const auto& it : resources) {
    string filename_prefix = string_printf("%s_%.4s_%hd",
        argv[1], reinterpret_cast<const char*>(&it.type), it.id);
    try {
      string data = get_resource_data(fd, it);
      save_file(filename_prefix + ".bin", data);
      printf("... %s.bin\n", filename_prefix.c_str());

    } catch (const runtime_error& e) {
      printf("... %s (FAILED: %s)\n", filename_prefix.c_str(), e.what());
    }
  }

  return 0;
}
