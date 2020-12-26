#pragma once

#include <inttypes.h>

#include <map>
#include <phosg/Encoding.hh>
#include <memory>
#include <vector>

#include "MemoryContext.hh"



////////////////////////////////////////////////////////////////////////////////
// Overall structure

// PEFF files have, in this order:
// - PEFFHeader
// - PEFFSectionHeader[PEFFHeader.section_count]
// - Section name table
// - Section contents

struct PEFFHeader {
  uint32_t magic1; // 'Joy!'
  uint32_t magic2; // 'peff'
  uint32_t arch; // 'pwpc'
  uint32_t format_version;
  uint32_t timestamp;
  uint32_t old_def_version;
  uint32_t old_imp_version;
  uint32_t current_version;
  uint16_t section_count; // total section count
  uint16_t inst_section_count; // sections required for execution
  uint32_t reserved;

  inline void byteswap() {
    this->magic1 = bswap32(this->magic1);
    this->magic2 = bswap32(this->magic2);
    this->arch = bswap32(this->arch);
    this->format_version = bswap32(this->format_version);
    this->timestamp = bswap32(this->timestamp);
    this->old_def_version = bswap32(this->old_def_version);
    this->old_imp_version = bswap32(this->old_imp_version);
    this->current_version = bswap32(this->current_version);
    this->section_count = bswap16(this->section_count);
    this->inst_section_count = bswap16(this->inst_section_count);
    this->reserved = bswap32(this->reserved);
  }
};

enum class PEFFSectionKind {
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

const char* name_for_section_kind(PEFFSectionKind k);

enum PEFFShareKind {
  PROCESS = 1, // shared within each process, copied for other processes
  GLOBAL = 4, // shared with all processes
  PROTECTED = 5, // shared with all processes, read-only unless privileged mode
};

const char* name_for_share_kind(PEFFShareKind k);

struct PEFFSectionHeader {
  int32_t name_offset; // -1 = no name
  uint32_t default_address;
  uint32_t total_size;
  uint32_t unpacked_size;
  uint32_t packed_size;
  uint32_t container_offset;
  uint8_t section_kind; // PEFFSectionKind enum
  uint8_t share_kind;
  uint8_t alignment;
  uint8_t reserved;

  inline void byteswap() {
    this->name_offset = bswap32(this->name_offset);
    this->default_address = bswap32(this->default_address);
    this->total_size = bswap32(this->total_size);
    this->unpacked_size = bswap32(this->unpacked_size);
    this->packed_size = bswap32(this->packed_size);
    this->container_offset = bswap32(this->container_offset);
  }
};



////////////////////////////////////////////////////////////////////////////////
// Loader section structure

// The loader section has, in this order:
// - PEFFLoaderSectionHeader
// - PEFFLoaderImportLibrary[header.imported_lib_count]
// - PEFFLoaderImportSymbol[header.imported_symbol_count]
// - PEFFLoaderRelocationHeader[header.rel_section_count]
// - Relocations (http://mirror.informatimago.com/next/developer.apple.com/documentation/mac/runtimehtml/RTArch-98.html)
// - String table
// - Export hash table
// - Export key table
// - Exported symbol table

struct PEFFLoaderSectionHeader {
  int32_t main_symbol_section_index; // -1 if no main symbol
  uint32_t main_symbol_offset; // offset within the section
  int32_t init_symbol_section_index; // -1 if no init symbol
  uint32_t init_symbol_offset; // offset within the section
  int32_t term_symbol_section_index; // -1 if no term symbol
  uint32_t term_symbol_offset; // offset within the section
  uint32_t imported_lib_count;
  uint32_t imported_symbol_count;
  uint32_t rel_section_count; // number of sections containing relocations
  uint32_t rel_commands_offset; // from beginning of loader section
  uint32_t string_table_offset; // from beginning of loader section
  uint32_t export_hash_offset; // from beginning of loader section
  uint32_t export_hash_power; // number of entries is 2^export_hash_power
  uint32_t exported_symbol_count;

  inline void byteswap() {
    this->main_symbol_section_index = bswap32(this->main_symbol_section_index);
    this->main_symbol_offset = bswap32(this->main_symbol_offset);
    this->init_symbol_section_index = bswap32(this->init_symbol_section_index);
    this->init_symbol_offset = bswap32(this->init_symbol_offset);
    this->term_symbol_section_index = bswap32(this->term_symbol_section_index);
    this->term_symbol_offset = bswap32(this->term_symbol_offset);
    this->imported_lib_count = bswap32(this->imported_lib_count);
    this->imported_symbol_count = bswap32(this->imported_symbol_count);
    this->rel_section_count = bswap32(this->rel_section_count);
    this->rel_commands_offset = bswap32(this->rel_commands_offset);
    this->string_table_offset = bswap32(this->string_table_offset);
    this->export_hash_offset = bswap32(this->export_hash_offset);
    this->export_hash_power = bswap32(this->export_hash_power);
    this->exported_symbol_count = bswap32(this->exported_symbol_count);
  }
};

enum PEFFImportLibraryFlags {
  // If library not found, don't fail - just set all import addrs to zero
  WEAK_IMPORT = 0x40,
  // Library must be initialized before the client fragment
  EARLY_INIT_REQUIRED = 0x80,
};

struct PEFFLoaderImportLibrary {
  uint32_t name_offset; // from beginning of loader string table
  uint32_t old_imp_version;
  uint32_t current_version;
  uint32_t imported_symbol_count; // number of symbols imported from this lib
  uint32_t start_index; // first import's index in imported symbol table
  uint8_t options; // bits in PEFFImportLibraryFlags
  uint8_t reserved1;
  uint16_t reserved2;

  inline void byteswap() {
    this->name_offset = bswap32(this->name_offset);
    this->old_imp_version = bswap32(this->old_imp_version);
    this->current_version = bswap32(this->current_version);
    this->imported_symbol_count = bswap32(this->imported_symbol_count);
    this->start_index = bswap32(this->start_index);
  }
};

enum PEFFLoaderImportSymbolType {
  CODE = 0,
  DATA = 1,
  TVECT = 2,
  TOC = 3,
  GLUE = 4,
};

enum PEFFLoaderImportSymbolFlags {
  WEAK = 0x80,
};

struct PEFFLoaderImportSymbol {
  uint32_t u;

  inline uint8_t flags() const {
    return (this->u >> 28) & 0x0F;
  }
  inline uint8_t type() const {
    return (this->u >> 24) & 0x0F;
  }
  inline uint32_t name_offset() const {
    return this->u & 0x00FFFFFF;
  }

  inline void byteswap() {
    this->u = bswap32(this->u);
  }
};

struct PEFFLoaderRelocationHeader {
  uint16_t section_index;
  uint16_t reserved;
  // some relocation commands are multiple words, so this isn't necessarily the
  // command count
  uint32_t word_count;
  uint32_t start_offset;

  inline void byteswap() {
    this->section_index = bswap16(this->section_index);
    this->word_count = bswap32(this->word_count);
    this->start_offset = bswap32(this->start_offset);
  }
};

struct PEFFLoaderExportHashEntry {
  uint32_t u;

  inline uint16_t chain_count() const {
    return (this->u >> 18) & 0x3FFF;
  }
  inline uint16_t start_index() const {
    return this->u & 0x3FFFF;
  }

  inline void byteswap() {
    this->u = bswap32(this->u);
  }
};

struct PEFFLoaderExportHashKey {
  uint16_t symbol_length;
  uint16_t hash;

  inline void byteswap() {
    this->symbol_length = bswap16(this->symbol_length);
    this->hash = bswap16(this->hash);
  }
};

struct PEFFLoaderExportSymbol {
  uint32_t type_and_name;
  uint32_t value; // usually offset from section start
  uint16_t section_index;

  inline uint8_t flags() const {
    return (this->type_and_name >> 28) & 0x0F;
  }
  inline uint8_t type() const {
    return (this->type_and_name >> 24) & 0x0F;
  }
  inline uint32_t name_offset() const {
    return this->type_and_name & 0x00FFFFFF;
  }

  inline void byteswap() {
    this->type_and_name = bswap32(this->type_and_name);
    this->value = bswap32(this->value);
    this->section_index = bswap16(this->section_index);
  }
} __attribute__((packed));




class PEFFFile {
public:
  explicit PEFFFile(const char* filename);
  PEFFFile(const char* filename, const std::string& data);
  ~PEFFFile() = default;

  void print(FILE* stream) const;

  void load_into(const std::string& lib_name, std::shared_ptr<MemoryContext> mem);

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

private:
  void parse(const std::string& data);
  void parse_loader_section(const std::string& data);

  const std::string filename;

  struct Section {
    std::string name;
    uint32_t default_address;
    uint32_t total_size;
    uint32_t unpacked_size;
    uint32_t packed_size;
    PEFFSectionKind section_kind;
    PEFFShareKind share_kind;
    uint8_t alignment;
    std::string data;
    std::string relocation_program;
  };

  uint32_t file_timestamp;
  uint32_t old_def_version;
  uint32_t old_imp_version;
  uint32_t current_version;

  // If the name is blank for any of these, they aren't exported
  ExportSymbol main_symbol;
  ExportSymbol init_symbol;
  ExportSymbol term_symbol;

  std::vector<Section> sections;
  std::map<std::string, ExportSymbol> export_symbols;
  std::vector<ImportSymbol> import_symbols;
};
