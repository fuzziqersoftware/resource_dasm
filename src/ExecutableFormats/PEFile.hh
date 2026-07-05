#pragma once

#include <inttypes.h>

#include <map>
#include <memory>
#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <vector>

#include "../Emulators/MemoryContext.hh"

namespace ResourceDASM {

struct MZHeader {
  phosg::be_uint16_t signature; // 'MZ' (4D5A)
  uint8_t dos_header[0x3A];
  phosg::le_uint32_t pe_header_offset;
} __attribute__((packed));

struct PEHeader {
  phosg::be_uint32_t signature; // 'PE\0\0' (0x50450000)
  phosg::le_uint16_t architecture; // there are many of these; 014C-014E = x86 + 32-bit variants
  phosg::le_uint16_t num_sections;
  phosg::le_uint32_t build_timestamp;
  phosg::le_uint32_t deprecated_symbol_table_rva;
  // 10
  phosg::le_uint32_t deprecated_symbol_table_size;
  phosg::le_uint16_t optional_header_size; // Number of bytes after the following flags field
  phosg::le_uint16_t flags; // 0002: executable, 0200: not relocatable, 2000: is a DLL
  phosg::le_uint16_t magic; // 0x010B or 0x020B
  phosg::le_uint16_t linker_version;
  phosg::le_uint32_t total_code_size;
  // 20
  phosg::le_uint32_t total_initialized_data_size;
  phosg::le_uint32_t total_uninitialized_data_size;
  phosg::le_uint32_t entrypoint_rva;
  phosg::le_uint32_t code_base_rva;
  // 30
  phosg::le_uint32_t data_base_rva;
  phosg::le_uint32_t image_base;
  phosg::le_uint32_t loaded_section_alignment;
  phosg::le_uint32_t file_section_alignment;
  // 40
  phosg::le_uint16_t os_version[2];
  phosg::le_uint16_t image_version[2];
  phosg::le_uint16_t subsystem_version[2];
  phosg::le_uint32_t win32_version; // Always zero?
  // 50
  phosg::le_uint32_t virtual_image_size;
  phosg::le_uint32_t total_header_size;
  phosg::le_uint32_t checksum; // Unused
  phosg::le_uint16_t subsystem; // 1: native, 2: GUI, 3: console, 5: OS/2 console, 7: Posix console
  phosg::le_uint16_t dll_flags;
  // 60
  phosg::le_uint32_t stack_reserve_size;
  phosg::le_uint32_t stack_commit_size;
  phosg::le_uint32_t heap_reserve_size;
  phosg::le_uint32_t heap_commit_size;
  // 70
  phosg::le_uint32_t loader_flags; // Unused?
  phosg::le_uint32_t data_directory_count; // Usually 16, but can be fewer
  phosg::le_uint32_t export_table_rva;
  phosg::le_uint32_t export_table_size;
  // 80
  phosg::le_uint32_t import_table_rva;
  phosg::le_uint32_t import_table_size;
  phosg::le_uint32_t resource_table_rva;
  phosg::le_uint32_t resource_table_size;
  // 90
  phosg::le_uint32_t exception_table_rva;
  phosg::le_uint32_t exception_table_size;
  phosg::le_uint32_t certificate_table_rva;
  phosg::le_uint32_t certificate_table_size;
  // A0
  phosg::le_uint32_t relocation_table_rva;
  phosg::le_uint32_t relocation_table_size;
  phosg::le_uint32_t debug_data_rva;
  phosg::le_uint32_t debug_data_size;
  // B0
  phosg::le_uint32_t architecture_data_rva;
  phosg::le_uint32_t architecture_data_size;
  phosg::le_uint32_t global_ptr_rva;
  phosg::le_uint32_t unused; // 0
  // C0
  phosg::le_uint32_t tls_table_rva;
  phosg::le_uint32_t tls_table_size;
  phosg::le_uint32_t load_config_table_rva;
  phosg::le_uint32_t load_config_table_size;
  // D0
  phosg::le_uint32_t bound_import_rva;
  phosg::le_uint32_t bound_import_size;
  phosg::le_uint32_t import_address_table_rva;
  phosg::le_uint32_t import_address_table_size;
  // E0
  phosg::le_uint32_t delay_import_descriptor_rva;
  phosg::le_uint32_t delay_import_descriptor_size;
  phosg::le_uint32_t clr_runtime_header_rva;
  phosg::le_uint32_t clr_runtime_header_size;
  // F0
  phosg::le_uint32_t unused_rva; // 0
  phosg::le_uint32_t unused_size; // 0
} __attribute__((packed));

struct PESectionHeader {
  char name[8];
  phosg::le_uint32_t loaded_size;
  phosg::le_uint32_t rva;
  phosg::le_uint32_t file_data_size;
  phosg::le_uint32_t file_data_rva;
  phosg::le_uint32_t relocations_rva;
  phosg::le_uint32_t line_numbers_rva;
  phosg::le_uint16_t num_relocations;
  phosg::le_uint16_t num_line_numbers;
  phosg::le_uint32_t flags;
} __attribute__((packed));

struct PEImportLibraryHeader {
  phosg::le_uint32_t lookup_table_rva;
  phosg::le_uint32_t timestamp;
  phosg::le_uint32_t forwarder_chain;
  phosg::le_uint32_t name_rva;
  phosg::le_uint32_t address_ptr_table_rva;
} __attribute__((packed));

struct PEImportTableEntry {
  phosg::le_uint32_t value;

  inline bool is_null() const {
    return this->value == 0;
  }
  inline bool is_ordinal() const {
    return this->value & 0x80000000;
  }
  inline uint16_t ordinal() const {
    if (!this->is_ordinal()) {
      throw std::logic_error("import entry is by name, not ordinal");
    }
    return this->value & 0xFFFF;
  }
  inline uint32_t name_table_entry_rva() const {
    if (this->is_ordinal()) {
      throw std::logic_error("import entry is by ordinal, not name");
    }
    return this->value & 0x7FFFFFFF;
  }
} __attribute__((packed));

struct PEImportTableNameEntry {
  phosg::le_uint16_t ordinal_hint;
  char name[0];
} __attribute__((packed));

struct PEExportTableHeader {
  phosg::le_uint32_t flags;
  phosg::le_uint32_t timestamp;
  phosg::le_uint16_t version[2];
  phosg::le_uint32_t name_rva;
  phosg::le_uint32_t ordinal_base;
  phosg::le_uint32_t num_entries;
  phosg::le_uint32_t num_names; // Not necessarily equal to num_entries
  phosg::le_uint32_t address_table_rva; // Table of phosg::le_uint32_ts (RVAs of functions)
  // The following two tables are both num_names in length and have a 1-1
  // correspondence between entries
  phosg::le_uint32_t name_pointer_table_rva; // Table of phosg::le_uint32_ts (RVAs of function names)
  phosg::le_uint32_t ordinal_table_rva; // Table of phosg::le_uint16_ts (ordinal numbers of functions named in above table)
} __attribute__((packed));

class PEFile {
public:
  explicit PEFile(const char* filename);
  PEFile(const char* filename, const std::string& data);
  PEFile(const char* filename, const void* data, size_t size);
  ~PEFile() = default;

  uint32_t load_into(std::shared_ptr<MemoryContext> mem) const;

  std::multimap<uint32_t, std::string> labels_for_loaded_imports(uint32_t image_base) const;
  std::multimap<uint32_t, std::string> labels_for_loaded_exports(uint32_t image_base) const;

  const PEHeader& unloaded_header() const;

  void print(
      FILE* stream,
      const std::multimap<uint32_t, std::string>* labels = nullptr,
      bool print_hex_view_for_code = false,
      bool all_sections_as_code = false) const;

  phosg::StringReader read_from_rva(uint32_t rva, uint32_t size) const;

  template <typename T>
  const T& read_from_rva(uint32_t rva) const {
    phosg::StringReader r = this->read_from_rva(rva, sizeof(T));
    const T* ret = &r.get<T>();
    return *ret;
  }

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

  struct ImportLibrary {
    struct Function {
      uint16_t ordinal; // If name is not blank, this is only a hint
      std::string name; // If blank, function is imported by ordinal only
      uint32_t addr_rva; // Addr where the function pointer goes during binding
    };
    std::string name;
    std::vector<Function> imports;
  };

  std::vector<Section> sections;
  std::unordered_map<std::string, ImportLibrary> import_libs;

  std::string export_lib_name;
  uint32_t ordinal_base; // Ordinal number of ordinal_to_export_rva[0]
  std::vector<uint32_t> export_rvas;
  // The values in this map are already biased by ordinal_base (that is, to look
  // up the appropriate RVA, you must subtract ordinal_base from the value from
  // this map first, then look in export_rvas)
  std::unordered_map<std::string, size_t> export_name_to_ordinal;

  // TODO: parse relocation data, etc.
};

} // namespace ResourceDASM
