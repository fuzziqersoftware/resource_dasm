#pragma once

#include <inttypes.h>

#include <map>
#include <phosg/Encoding.hh>
#include <memory>
#include <vector>

#include "MemoryContext.hh"



struct MZHeader {
  be_uint16_t signature; // 'MZ' (4D5A)
  uint8_t dos_header[0x3A];
  le_uint32_t pe_header_offset;
} __attribute__((packed));

struct PEHeader {
  be_uint32_t signature; // 'PE\0\0' (0x50450000)
  le_uint16_t architecture; // there are many of these; 014C-014E = x86 + 32-bit variants
  le_uint16_t num_sections;
  le_uint32_t build_timestamp;
  le_uint32_t deprecated_symbol_table_rva;
  // 10
  le_uint32_t deprecated_symbol_table_size;
  le_uint16_t optional_header_size; // Number of bytes after the following flags field
  le_uint16_t flags; // 0002: executable, 0200: not relocatable, 2000: is a DLL
  le_uint16_t magic; // 0x010B or 0x020B
  le_uint16_t linker_version;
  le_uint32_t total_code_size;
  // 20
  le_uint32_t total_initialized_data_size;
  le_uint32_t total_uninitialized_data_size;
  le_uint32_t entrypoint_rva;
  le_uint32_t code_base_rva;
  // 30
  le_uint32_t data_base_rva;
  le_uint32_t image_base;
  le_uint32_t loaded_section_alignment;
  le_uint32_t file_section_alignment;
  // 40
  le_uint16_t os_version[2];
  le_uint16_t image_version[2];
  le_uint16_t subsystem_version[2];
  le_uint32_t win32_version; // Always zero?
  // 50
  le_uint32_t virtual_image_size;
  le_uint32_t total_header_size;
  le_uint32_t checksum; // Unused
  le_uint16_t subsystem; // 1: native, 2: GUI, 3: console, 5: OS/2 console, 7: Posix console
  le_uint16_t dll_flags;
  // 60
  le_uint32_t stack_reserve_size;
  le_uint32_t stack_commit_size;
  le_uint32_t heap_reserve_size;
  le_uint32_t heap_commit_size;
  // 70
  le_uint32_t loader_flags; // Unused?
  le_uint32_t data_directory_count; // Usually 16, but can be fewer
  le_uint32_t export_table_rva;
  le_uint32_t export_table_size;
  // 80
  le_uint32_t import_table_rva;
  le_uint32_t import_table_size;
  le_uint32_t resource_table_rva;
  le_uint32_t resource_table_size;
  // 90
  le_uint32_t exception_table_rva;
  le_uint32_t exception_table_size;
  le_uint32_t certificate_table_rva;
  le_uint32_t certificate_table_size;
  // A0
  le_uint32_t relocation_table_rva;
  le_uint32_t relocation_table_size;
  le_uint32_t debug_data_rva;
  le_uint32_t debug_data_size;
  // B0
  le_uint32_t architecture_data_rva;
  le_uint32_t architecture_data_size;
  le_uint32_t global_ptr_rva;
  le_uint32_t unused; // 0
  // C0
  le_uint32_t tls_table_rva;
  le_uint32_t tls_table_size;
  le_uint32_t load_config_table_rva;
  le_uint32_t load_config_table_size;
  // D0
  le_uint32_t bound_import_rva;
  le_uint32_t bound_import_size;
  le_uint32_t import_address_table_rva;
  le_uint32_t import_address_table_size;
  // E0
  le_uint32_t delay_import_descriptor_rva;
  le_uint32_t delay_import_descriptor_size;
  le_uint32_t clr_runtime_header_rva;
  le_uint32_t clr_runtime_header_size;
  // F0
  le_uint32_t unused_rva; // 0
  le_uint32_t unused_size; // 0

  void apply_image_base();
} __attribute__((packed));

struct PESectionHeader {
  char name[8];
  le_uint32_t loaded_size;
  le_uint32_t rva;
  le_uint32_t file_data_size;
  le_uint32_t file_data_rva;
  le_uint32_t relocations_rva;
  le_uint32_t line_numbers_rva;
  le_uint16_t num_relocations;
  le_uint16_t num_line_numbers;
  le_uint32_t flags;

  void apply_image_base(uint32_t image_base);
} __attribute__((packed));

struct PEImportDLLHeader {
  le_uint32_t lookup_table_rva;
  le_uint32_t flags;
  le_uint32_t timestamp;
  le_uint32_t name_ptr_table_rva;
  le_uint32_t address_ptr_table_rva;

  void apply_image_base(uint32_t image_base);
} __attribute__((packed));

struct PEExportTableHeader {
  le_uint32_t flags;
  le_uint32_t timestamp;
  le_uint16_t version[2];
  le_uint32_t name_rva;
  le_uint32_t first_ordinal;
  le_uint32_t num_entries;
  le_uint32_t num_names; // Not necessarily equal to num_entries
  le_uint32_t entry_table_rva; // Table of le_uint32_ts (RVAs of functions)
  le_uint32_t name_table_rva;
  le_uint32_t ordinal_table_rva;

  void apply_image_base(uint32_t image_base);
} __attribute__((packed));



class PEFile {
public:
  explicit PEFile(const char* filename);
  PEFile(const char* filename, const std::string& data);
  PEFile(const char* filename, const void* data, size_t size);
  ~PEFile() = default;

  void load_into(std::shared_ptr<MemoryContext> mem);

  PEHeader loaded_header() const;
  const PEHeader& unloaded_header() const;

  void print(FILE* stream, const std::multimap<uint32_t, std::string>* labels = nullptr) const;

private:
  void parse(const void* data, size_t size);

  const std::string filename;

  PEHeader header;

  struct Section {
    std::string name;
    uint32_t address;
    uint32_t size;
    std::string data;

    uint32_t rva;
    uint32_t file_offset;
    uint32_t relocations_rva;
    uint32_t line_numbers_rva;
    uint16_t num_relocations;
    uint16_t num_line_numbers;
    uint32_t flags;
  };

  std::vector<Section> sections;

  // TODO: parse import/export tables, relocation data, etc.
};
