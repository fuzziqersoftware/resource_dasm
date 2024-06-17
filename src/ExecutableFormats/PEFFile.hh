#pragma once

#include <inttypes.h>

#include <map>
#include <memory>
#include <phosg/Encoding.hh>
#include <vector>

#include "../Emulators/MemoryContext.hh"

////////////////////////////////////////////////////////////////////////////////
// Overall structure

// PEF files have, in this order:
// - PEFHeader
// - PEFSectionHeader[PEFHeader.section_count]
// - Section name table
// - Section contents

struct PEFHeader {
  be_uint32_t magic1; // 'Joy!'
  be_uint32_t magic2; // 'peff'
  be_uint32_t arch; // 'pwpc' or 'm68k'
  be_uint32_t format_version;
  be_uint32_t timestamp;
  be_uint32_t old_def_version;
  be_uint32_t old_imp_version;
  be_uint32_t current_version;
  be_uint16_t section_count; // total section count
  be_uint16_t inst_section_count; // sections required for execution
  be_uint32_t reserved;
} __attribute__((packed));

enum class PEFSectionKind {
  EXECUTABLE_READONLY = 0, // uncompressed, read-only, executable
  UNPACKED_DATA = 1, // uncompressed, read/write, followed by zeroes if needed
  PATTERN_DATA = 2,
  CONSTANT = 3, // uncompressed, read-only, non-executable
  LOADER = 4, // imports, exports, entry points
  DEBUG_RESERVED = 5, // reserved
  EXECUTABLE_READWRITE = 6, // uncompressed (?), read/write, executable
  EXCEPTION_RESERVED = 7, // reserved
  TRACEBACK_RESERVED = 8, // reserved
};

const char* name_for_section_kind(PEFSectionKind k);

enum PEFShareKind {
  PROCESS = 1, // shared within each process, copied for other processes
  GLOBAL = 4, // shared with all processes
  PROTECTED = 5, // shared with all processes, read-only unless privileged mode
};

const char* name_for_share_kind(PEFShareKind k);

struct PEFSectionHeader {
  be_int32_t name_offset; // -1 = no name
  be_uint32_t default_address;
  be_uint32_t total_size;
  be_uint32_t unpacked_size;
  be_uint32_t packed_size;
  be_uint32_t container_offset;
  uint8_t section_kind; // PEFSectionKind enum
  uint8_t share_kind;
  uint8_t alignment;
  uint8_t reserved;
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// Loader section structure

// The loader section has, in this order:
// - PEFLoaderSectionHeader
// - PEFLoaderImportLibrary[header.imported_lib_count]
// - PEFLoaderImportSymbol[header.imported_symbol_count]
// - PEFLoaderRelocationHeader[header.rel_section_count]
// - Relocations
// - String table
// - Export hash table
// - Export key table
// - Exported symbol table

struct PEFLoaderSectionHeader {
  be_int32_t main_symbol_section_index; // -1 if no main symbol
  be_uint32_t main_symbol_offset; // offset within the section
  be_int32_t init_symbol_section_index; // -1 if no init symbol
  be_uint32_t init_symbol_offset; // offset within the section
  be_int32_t term_symbol_section_index; // -1 if no term symbol
  be_uint32_t term_symbol_offset; // offset within the section
  be_uint32_t imported_lib_count;
  be_uint32_t imported_symbol_count;
  be_uint32_t rel_section_count; // number of sections containing relocations
  be_uint32_t rel_commands_offset; // from beginning of loader section
  be_uint32_t string_table_offset; // from beginning of loader section
  be_uint32_t export_hash_offset; // from beginning of loader section
  be_uint32_t export_hash_power; // number of entries is 2^export_hash_power
  be_uint32_t exported_symbol_count;
} __attribute__((packed));

enum PEFImportLibraryFlags {
  // If library not found, don't fail - just set all import addrs to zero
  WEAK_IMPORT = 0x40,
  // Library must be initialized before the client fragment
  EARLY_INIT_REQUIRED = 0x80,
};

struct PEFLoaderImportLibrary {
  be_uint32_t name_offset; // from beginning of loader string table
  be_uint32_t old_imp_version;
  be_uint32_t current_version;
  be_uint32_t imported_symbol_count; // number of symbols imported from this lib
  be_uint32_t start_index; // first import's index in imported symbol table
  uint8_t options; // bits in PEFImportLibraryFlags
  uint8_t reserved1;
  be_uint16_t reserved2;
} __attribute__((packed));

enum PEFLoaderImportSymbolType {
  CODE = 0,
  DATA = 1,
  TVECT = 2,
  TOC = 3,
  GLUE = 4,
};

enum PEFLoaderImportSymbolFlags {
  WEAK = 0x80,
};

struct PEFLoaderImportSymbol {
  be_uint32_t u;

  inline uint8_t flags() const {
    return (this->u >> 28) & 0x0F;
  }
  inline uint8_t type() const {
    return (this->u >> 24) & 0x0F;
  }
  inline uint32_t name_offset() const {
    return this->u & 0x00FFFFFF;
  }
} __attribute__((packed));

struct PEFLoaderRelocationHeader {
  be_uint16_t section_index;
  be_uint16_t reserved;
  // Some relocation commands are multiple words, so this isn't necessarily the
  // same as the command count
  be_uint32_t word_count;
  be_uint32_t start_offset;
} __attribute__((packed));

struct PEFLoaderExportHashEntry {
  be_uint32_t u;

  inline uint16_t chain_count() const {
    return (this->u >> 18) & 0x3FFF;
  }
  inline uint16_t start_index() const {
    return this->u & 0x3FFFF;
  }
} __attribute__((packed));

struct PEFLoaderExportHashKey {
  be_uint16_t symbol_length;
  be_uint16_t hash;
} __attribute__((packed));

struct PEFLoaderExportSymbol {
  be_uint32_t type_and_name;
  be_uint32_t value; // usually offset from section start
  be_uint16_t section_index;

  inline uint8_t flags() const {
    return (this->type_and_name >> 28) & 0x0F;
  }
  inline uint8_t type() const {
    return (this->type_and_name >> 24) & 0x0F;
  }
  inline uint32_t name_offset() const {
    return this->type_and_name & 0x00FFFFFF;
  }
} __attribute__((packed));

class PEFFile {
public:
  explicit PEFFile(const char* filename);
  PEFFile(const char* filename, const std::string& data);
  PEFFile(const char* filename, const void* data, size_t size);
  ~PEFFile() = default;

  void print(
      FILE* stream,
      const std::multimap<uint32_t, std::string>* labels = nullptr,
      bool print_hex_view_for_code = false) const;

  void load_into(const std::string& lib_name, std::shared_ptr<MemoryContext> mem,
      uint32_t base_addr = 0);

  struct ExportSymbol {
    std::string name;
    uint16_t section_index;
    uint32_t value;
    uint8_t flags;
    uint8_t type;

    void print(FILE* stream) const;
  };

  struct ImportSymbol {
    std::string lib_name;
    std::string name;
    uint8_t flags;
    uint8_t type;

    void print(FILE* stream) const;
  };

  inline const std::map<std::string, ExportSymbol> exports() const {
    return this->export_symbols;
  }
  inline const std::vector<ImportSymbol> imports() const {
    return this->import_symbols;
  }
  inline const ExportSymbol& main() const {
    return this->main_symbol;
  }
  inline const ExportSymbol& init() const {
    return this->init_symbol;
  }
  inline const ExportSymbol& term() const {
    return this->term_symbol;
  }
  inline bool is_ppc() const {
    return this->arch_is_ppc;
  }

private:
  void parse(const void* data, size_t size);
  void parse_loader_section(const void* data, size_t size);

  const std::string filename;

  struct Section {
    std::string name;
    uint32_t default_address;
    uint32_t total_size;
    uint32_t unpacked_size;
    uint32_t packed_size;
    PEFSectionKind section_kind;
    PEFShareKind share_kind;
    uint8_t alignment;
    std::string data;
    std::string relocation_program;
  };

  uint32_t file_timestamp;
  uint32_t old_def_version;
  uint32_t old_imp_version;
  uint32_t current_version;
  bool arch_is_ppc;

  // If the name is blank for any of these, they aren't exported
  ExportSymbol main_symbol;
  ExportSymbol init_symbol;
  ExportSymbol term_symbol;

  std::vector<Section> sections;
  std::map<std::string, ExportSymbol> export_symbols;
  std::vector<ImportSymbol> import_symbols;
};
