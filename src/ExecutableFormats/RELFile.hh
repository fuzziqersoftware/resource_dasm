#pragma once

#include <inttypes.h>

#include <map>
#include <unordered_map>
#include <phosg/Encoding.hh>
#include <memory>
#include <vector>



struct RELHeader {
  be_uint32_t module_id;
  be_uint32_t next_module; // Used at runtime only (unused in file)
  be_uint32_t prev_module; // Used at runtime only (unused in file)
  be_uint32_t num_sections;
  be_uint32_t section_headers_offset;
  be_uint32_t module_name_offset; // Can be 0 if module is internally unnamed
  be_uint32_t module_name_size;
  be_uint32_t format_version; // 1, 2, or 3
  be_uint32_t bss_size;
  be_uint32_t relocation_table_offset;
  be_uint32_t import_table_offset;
  be_uint32_t import_table_size;
  uint8_t on_load_section; // 0 = no on_load function
  uint8_t on_unload_section; // 0 = no on_unload function
  uint8_t on_missing_section; // 0 = no on_missing function
  uint8_t unused;
  be_uint32_t on_load_offset; // Offset within on_load_section
  be_uint32_t on_unload_offset; // Offset within on_load_section
  be_uint32_t on_missing_offset; // Offset within on_load_section
  be_uint32_t alignment; // Not present if format_version == 1
  be_uint32_t bss_alignment; // Not present if format_version == 1
  be_uint32_t unknown_a1; // Onlypresent if format_version == 3
} __attribute__((packed));

struct RELSectionHeader {
  be_uint32_t type_offset; // Low bit is set if section contains code
  be_uint32_t size;

  inline bool has_code() const {
    return this->type_offset & 1;
  }
  inline uint32_t offset() const {
    return this->type_offset & (~1);
  }
} __attribute__((packed));

struct RELImportEntry {
  be_uint32_t from_module_id;
  be_uint32_t relocations_offset;
} __attribute__((packed));

struct RELRelocationInstruction {
  enum Type : uint8_t {
    NONE = 0x00, // Do nothing
    ADDR32 = 0x01, // Write the absolute address
    ADDR24 = 0x02, // Write the low 3 bytes of the address, but leave bottom two buts alone
    ADDR16 = 0x03, // Write the low 2 bytes of the address
    ADDR16L = 0x04, // Write the low 2 bytes of the address
    ADDR16H = 0x05, // Write the high 2 bytes of the address
    ADDR16S = 0x06, // Write the high 2 bytes of the address - 0x10000
    ADDR14 = 0x07, // Write the low 14 bits of the address
    ADDR14T = 0x08, // Write the low 14 bits of the address
    ADDR14N = 0x09, // Write the low 14 bits of the address
    REL24 = 0x0A, // Write the offset field of a b instruction
    REL14 = 0x0B, // Write the offset field of a bc instruction
    NOP = 0xC9, // Do nothing (but update the offset)
    SECTION = 0xCA, // Change to section <section_index> and set offset to 0
    STOP = 0xCB, // Stop executing relocation instructions
  };

  be_uint16_t offset; // Bytes after previous relocation entry
  Type type;
  // These fields describe where the target symbol is in the impoted module. So
  // the target address is computed by adding the imported module's base address
  // and the appropriate section offset to this offset.
  uint8_t section_index;
  be_uint32_t symbol_offset;

  static const char* name_for_type(Type type);
} __attribute__((packed));



class RELFile {
public:
  explicit RELFile(const char* filename);
  RELFile(const char* filename, const std::string& data);
  RELFile(const char* filename, const void* data, size_t size);
  ~RELFile() = default;

  void print(FILE* stream, const std::multimap<uint32_t, std::string>* labels = nullptr) const;

private:
  void parse(const void* data, size_t size);

  const std::string filename;

  struct Section {
    uint32_t index;
    uint32_t offset;
    uint32_t size;
    bool has_code;
    std::string data;
  };

  std::string name;
  std::vector<Section> sections;
  RELHeader header;
  std::unordered_map<uint32_t, std::vector<RELRelocationInstruction>> import_table;
};
