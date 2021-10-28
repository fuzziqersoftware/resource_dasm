#include "PEFFFile.hh"

#include <inttypes.h>
#include <string.h>

#include <map>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>

#include "M68KEmulator.hh"
#include "PPC32Emulator.hh"
#include "MemoryContext.hh"

using namespace std;



const char* name_for_section_kind(PEFFSectionKind k) {
  switch (k) {
    case PEFFSectionKind::EXECUTABLE_READONLY:
      return "EXECUTABLE_READONLY";
    case PEFFSectionKind::UNPACKED_DATA:
      return "UNPACKED_DATA";
    case PEFFSectionKind::PATTERN_DATA:
      return "PATTERN_DATA";
    case PEFFSectionKind::CONSTANT:
      return "CONSTANT";
    case PEFFSectionKind::LOADER:
      return "LOADER";
    case PEFFSectionKind::DEBUG_RESERVED:
      return "DEBUG_RESERVED";
    case PEFFSectionKind::EXECUTABLE_READWRITE:
      return "EXECUTABLE_READWRITE";
    case PEFFSectionKind::EXCEPTION_RESERVED:
      return "EXCEPTION_RESERVED";
    case PEFFSectionKind::TRACEBACK_RESERVED:
      return "TRACEBACK_RESERVED";
    default:
      return "__UNKNOWN__";
  }
}

const char* name_for_share_kind(PEFFShareKind k) {
  switch (k) {
    case PEFFShareKind::PROCESS:
      return "PROCESS";
    case PEFFShareKind::GLOBAL:
      return "GLOBAL";
    case PEFFShareKind::PROTECTED:
      return "PROTECTED";
    default:
      return "__UNKNOWN__";
  }
}



PEFFFile::PEFFFile(const char* filename) : filename(filename) {
  const string data = load_file(filename);
  this->parse(data.data(), data.size());
}

PEFFFile::PEFFFile(const char* filename, const string& data) :
    filename(filename) {
  this->parse(data.data(), data.size());
}

PEFFFile::PEFFFile(const char* filename, const void* data, size_t size) :
    filename(filename) {
  this->parse(data, size);
}



static uint64_t read_pattern_varint(StringReader& r) {
  uint8_t b;
  uint64_t ret = 0;
  do {
    b = r.get_u8();
    ret = (ret << 7) | (b & 0x7F);
  } while (b & 0x80);
  return ret;
}

static string decompress_pattern_data(const string& data) {
  string ret;
  StringReader r(data.data(), data.size());
  while (!r.eof()) {
    uint8_t b = r.get_u8();
    uint8_t op = (b >> 5) & 0x07;
    uint32_t count = b & 0x1F;
    if (count == 0) {
      count = read_pattern_varint(r);
    }

    switch (op) {
      case 0: // zero
        ret.resize(ret.size() + count, '\0');
        break;
      case 1: // write block
        ret.append(r.read(count));
        break;
      case 2: { // write block repeatedly
        uint32_t repeat_count = read_pattern_varint(r) + 1;
        string data = r.read(count);
        for (; repeat_count; repeat_count--) {
          ret.append(data);
        }
        break;
      }
      case 3: { // interleave repeat block with write block
        uint32_t common_size = count;
        uint32_t custom_size = read_pattern_varint(r);
        uint32_t custom_section_count = read_pattern_varint(r);
        string common_data = r.read(common_size);
        for (; custom_section_count; custom_section_count--) {
          ret.append(common_data);
          ret.append(r.read(custom_size));
        }
        ret.append(common_data);
        break;
      }
      case 4: { // interleave zero with write block
        uint32_t zero_size = count;
        uint32_t custom_size = read_pattern_varint(r);
        uint32_t custom_section_count = read_pattern_varint(r);
        for (; custom_section_count; custom_section_count--) {
          ret.resize(ret.size() + zero_size, '\0');
          ret.append(r.read(custom_size));
        }
        ret.resize(ret.size() + zero_size, '\0');
        break;
      }
      default:
        throw runtime_error("invalid opcode in pattern data");
    }
  }

  return ret;
}

static void disassemble_relocation_program(FILE* stream, const string& data) {
  StringReader r(data.data(), data.size());

  while (!r.eof()) {
    size_t op_start_offset = r.where();
    uint16_t cmd = r.get_u16r();

    string op_dasm;
    if ((cmd & 0xC000) == 0x0000) {
      uint8_t count = cmd & 0x3F;
      uint8_t skip_count = (cmd >> 6) & 0xFF;
      op_dasm = string_printf("reloc_skip_then_add_sect_d      skip_words=%hhu, num_words=%hhu", skip_count, count);
    } else if ((cmd & 0xE000) == 0x4000) {
      uint16_t length = (cmd & 0x01FF) + 1;
      if ((cmd & 0x1E00) == 0x0000) {
        op_dasm = string_printf("reloc_v_add_sect_c              num_words=%hu", length);
      } else if ((cmd & 0x1E00) == 0x0200) {
        op_dasm = string_printf("reloc_v_add_sect_d              num_words=%hu", length);
      } else if ((cmd & 0x1E00) == 0x0400) {
        op_dasm = string_printf("reloc_v_add_sect_c_sect_d_none  num_3_word_blocks=%hu", length);
      } else if ((cmd & 0x1E00) == 0x0600) {
        op_dasm = string_printf("reloc_v_add_sect_c_sect_d       num_2_word_blocks=%hu", length);
      } else if ((cmd & 0x1E00) == 0x0800) {
        op_dasm = string_printf("reloc_v_add_sect_d_none         num_2_word_blocks=%hu", length);
      } else if ((cmd & 0x1E00) == 0x0A00) {
        op_dasm = string_printf("reloc_v_add_imports             num_words=%hu", length);
      } else {
        op_dasm = string_printf("__invalid_reloc_v__             count=%hu", length);
      }
    } else if ((cmd & 0xE000) == 0x6000) {
      uint16_t index = cmd & 0x01FF;
      if ((cmd & 0x1E00) == 0x0000) {
        op_dasm = string_printf("reloc_i_add_import              index=0x%hX", index);
      } else if ((cmd & 0x1E00) == 0x0200) {
        op_dasm = string_printf("reloc_i_set_sect_c              section_index=0x%hX", index);
      } else if ((cmd & 0x1E00) == 0x0400) {
        op_dasm = string_printf("reloc_i_set_sect_d              section_index=0x%hX", index);
      } else if ((cmd & 0x1E00) == 0x0600) {
        op_dasm = string_printf("reloc_i_add_sec_addr            section_index=0x%hX", index);
      } else {
        op_dasm = string_printf("__invalid_reloc_i__             index=0x%hX", index);
      }
    } else if ((cmd & 0xF000) == 0x8000) {
      uint16_t delta = (cmd & 0x0FFF) + 1;
      op_dasm = string_printf("reloc_incr_reloc_addr           delta=0x%hX", delta);
    } else if ((cmd & 0xF000) == 0x9000) {
      uint8_t blocks = ((cmd >> 8) & 0x0F) + 1;
      uint16_t times = (cmd & 0x00FF) + 1;
      op_dasm = string_printf("reloc_repeat                    blocks=%hhu (dest=0x%zX), times=%hu",
          blocks, op_start_offset - blocks * 2, times);
    } else if ((cmd & 0xFC00) == 0xA000) {
      uint32_t offset = ((cmd & 0x03FF) << 16) | r.get_u16r();
      op_dasm = string_printf("reloc_set_position              offset=0x%" PRIX32, offset);
    } else if ((cmd & 0xFC00) == 0xA400) {
      uint32_t index = ((cmd & 0x03FF) << 16) | r.get_u16r();
      op_dasm = string_printf("reloc_i_add_import              index=0x%" PRIX32, index);
    } else if ((cmd & 0xFC00) == 0xB000) {
      uint8_t blocks = ((cmd >> 6) & 0x0F) + 1;
      uint32_t times = ((cmd & 0x003F) << 16) | r.get_u16r();
      op_dasm = string_printf("reloc_repeat                    blocks=%hhu (dest=0x%zX), times=%" PRIu32,
          blocks, op_start_offset - blocks * 2, times);
    } else if ((cmd & 0xFC00) == 0xB400) {
      uint8_t subcmd = (cmd >> 6) & 0x0F;
      uint32_t index = ((cmd & 0x003F) << 16) | r.get_u16r();
      if (subcmd == 0x0) {
        op_dasm = string_printf("reloc_i_add_sec_addr            index=%" PRIu32, index);
      } else if (subcmd == 0x1) {
        op_dasm = string_printf("reloc_i_set_sect_c              index=%" PRIu32, index);
      } else if (subcmd == 0x2) {
        op_dasm = string_printf("reloc_i_set_sect_d              index=%" PRIu32, index);
      } else {
        op_dasm = string_printf("__invalid_reloc_ext_lg__        index=%" PRIu32, index);
      }
    }

    size_t op_end_offset = r.where();
    r.go(op_start_offset);
    string data_str;
    while (r.where() < op_end_offset) {
      data_str += string_printf("%04hX ", r.get_u16r());
    }
    if (data_str.size() < 10) {
      data_str.resize(10, ' ');
    }

    fprintf(stream, "  %04zX:  %s %s\n", op_start_offset, data_str.c_str(), op_dasm.c_str());
  }
}



void PEFFFile::parse_loader_section(const void* data, size_t size) {
  StringReader r(data, size);

  PEFFLoaderSectionHeader header = r.get<PEFFLoaderSectionHeader>();
  header.byteswap();

  if (header.main_symbol_section_index >= 0) {
    this->main_symbol.name = "[main]";
    this->main_symbol.section_index = header.main_symbol_section_index;
    this->main_symbol.value = header.main_symbol_offset;
    this->main_symbol.flags = 0;
    this->main_symbol.type = 0;
  }
  if (header.init_symbol_section_index >= 0) {
    this->init_symbol.name = "[init]";
    this->init_symbol.section_index = header.init_symbol_section_index;
    this->init_symbol.value = header.init_symbol_offset;
    this->init_symbol.flags = 0;
    this->init_symbol.type = 0;
  }
  if (header.term_symbol_section_index >= 0) {
    this->term_symbol.name = "[term]";
    this->term_symbol.section_index = header.term_symbol_section_index;
    this->term_symbol.value = header.term_symbol_offset;
    this->term_symbol.flags = 0;
    this->term_symbol.type = 0;
  }

  map<size_t, string> import_library_start_indexes;
  unordered_set<string> weak_import_library_names;
  for (size_t x = 0; x < header.imported_lib_count; x++) {
    PEFFLoaderImportLibrary lib = r.get<PEFFLoaderImportLibrary>();
    lib.byteswap();

    if (header.string_table_offset + lib.name_offset >= size) {
      throw runtime_error("library name out of range");
    }
    const char* name = reinterpret_cast<const char*>(data)
        + header.string_table_offset + lib.name_offset;
    import_library_start_indexes.emplace(lib.start_index, name);
    if (lib.options & PEFFImportLibraryFlags::WEAK_IMPORT) {
      weak_import_library_names.emplace(name);
    }
  }

  string current_lib_name = "__missing__";
  bool current_lib_weak = false;
  for (size_t x = 0; x < header.imported_symbol_count; x++) {
    PEFFLoaderImportSymbol sym = r.get<PEFFLoaderImportSymbol>();
    sym.byteswap();

    try {
      current_lib_name = import_library_start_indexes.at(x);
      current_lib_weak = weak_import_library_names.count(current_lib_name);
    } catch (const out_of_range&) { }

    if (header.string_table_offset + sym.name_offset() >= size) {
      throw runtime_error("symbol name out of range");
    }
    const char* name = reinterpret_cast<const char*>(data)
        + header.string_table_offset + sym.name_offset();

    ImportSymbol imp_sym;
    imp_sym.lib_name = current_lib_name;
    imp_sym.name = name;
    imp_sym.flags = sym.flags() | (current_lib_weak ? PEFFLoaderImportSymbolFlags::WEAK : 0);
    imp_sym.type = sym.type();
    this->import_symbols.emplace_back(move(imp_sym));
  }

  for (size_t x = 0; x < header.rel_section_count; x++) {
    PEFFLoaderRelocationHeader rel = r.get<PEFFLoaderRelocationHeader>();
    rel.byteswap();

    if (this->sections.size() <= rel.section_index) {
      // TODO: do we need to support the loader section appearing before other
      // sections?
      throw runtime_error("relocation program refers to nonexistent section");
    }
    if (!this->sections[rel.section_index].relocation_program.empty()) {
      throw runtime_error("section has multiple relocation programs");
    }
    this->sections[rel.section_index].relocation_program = string(
        reinterpret_cast<const char*>(data)
          + header.rel_commands_offset + rel.start_offset,
        rel.word_count * 2);
  }

  r.go(header.export_hash_offset);
  size_t hash_export_count = 0;
  for (ssize_t x = 0; x < (1 << header.export_hash_power); x++) {
    PEFFLoaderExportHashEntry ent = r.get<PEFFLoaderExportHashEntry>();
    ent.byteswap();
    hash_export_count += ent.chain_count();
  }
  if (hash_export_count != header.exported_symbol_count) {
    throw runtime_error("hash key count does not match imported symbol count");
  }
  vector<uint16_t> symbol_name_lengths(hash_export_count, 0);
  for (size_t x = 0; x < hash_export_count; x++) {
    PEFFLoaderExportHashKey key = r.get<PEFFLoaderExportHashKey>();
    key.byteswap();
    symbol_name_lengths[x] = key.symbol_length;
  }
  for (size_t x = 0; x < hash_export_count; x++) {
    PEFFLoaderExportSymbol sym = r.get<PEFFLoaderExportSymbol>();
    sym.byteswap();

    string name(reinterpret_cast<const char*>(data)
          + header.string_table_offset + sym.name_offset(),
        symbol_name_lengths[x]);
    ExportSymbol exp_sym;
    exp_sym.name = name;
    exp_sym.section_index = sym.section_index;
    exp_sym.value = sym.value;
    exp_sym.flags = sym.flags();
    exp_sym.type = sym.type();
    this->export_symbols.emplace(move(name), move(exp_sym));
  }
}

void PEFFFile::parse(const void* data, size_t size) {
  StringReader r(data, size);

  PEFFHeader header = r.get<PEFFHeader>();
  header.byteswap();
  if (header.magic1 != 0x4A6F7921) {
    throw runtime_error("file does not have Joy! signature");
  }
  if (header.magic2 != 0x70656666) {
    throw runtime_error("file does not have peff signature");
  }
  if (header.arch != 0x70777063 && header.arch != 0x6D36386B) {
    throw runtime_error("file is not for the pwpc or m68k architecture");
  }
  if (header.format_version != 0x00000001) {
    throw runtime_error("file format version is not 1");
  }

  this->file_timestamp = header.timestamp;
  this->old_def_version = header.old_def_version;
  this->old_imp_version = header.old_imp_version;
  this->current_version = header.current_version;
  this->arch_is_ppc = (header.arch == 0x70777063);

  size_t section_name_table_offset = r.where() + sizeof(PEFFSectionHeader) * header.section_count;

  for (size_t x = 0; x < header.section_count; x++) {
    PEFFSectionHeader sec_header = r.get<PEFFSectionHeader>();
    sec_header.byteswap();

    auto sec_kind = static_cast<PEFFSectionKind>(sec_header.section_kind);

    auto sec_data = r.pread(sec_header.container_offset, sec_header.packed_size);
    if (sec_kind == PEFFSectionKind::PATTERN_DATA) {
      string decompressed_data = decompress_pattern_data(sec_data);
      sec_data = move(decompressed_data);
    } else if (sec_kind == PEFFSectionKind::LOADER) {
      this->parse_loader_section(data, size);
      sec_data.clear();
    }

    string name;
    if (sec_header.name_offset >= 0) {
      name = sec_data.data() + section_name_table_offset + sec_header.name_offset;
    }

    Section sec;
    sec.name = name;
    sec.default_address = sec_header.default_address,
    sec.total_size = sec_header.total_size,
    sec.unpacked_size = sec_header.unpacked_size,
    sec.packed_size = sec_header.packed_size,
    sec.section_kind = sec_kind,
    sec.share_kind = static_cast<PEFFShareKind>(sec_header.share_kind),
    sec.alignment = sec_header.alignment,
    sec.data = move(sec_data);
    this->sections.emplace_back(move(sec));
  }
}



void PEFFFile::ExportSymbol::print(FILE* stream) const {
  if (this->name.empty()) {
    fprintf(stream, "[missing export symbol]");
  } else {
    fprintf(stream, "[export \"%s\" %hu:%08" PRIX32 "]", this->name.c_str(),
        this->section_index, this->value);
  }
}

void PEFFFile::ImportSymbol::print(FILE* stream) const {
  fprintf(stream, "[import %s:%s (%hhX%hhX)]", this->lib_name.c_str(),
      this->name.c_str(), this->flags, this->type);
}

void PEFFFile::print(FILE* stream) const {
  fprintf(stream, "[PEFF file: %s]\n", this->filename.c_str());
  fprintf(stream, "  file_timestamp: %08" PRIX32 "\n", this->file_timestamp);
  fprintf(stream, "  old_def_version: %08" PRIX32 "\n", this->old_def_version);
  fprintf(stream, "  old_imp_version: %08" PRIX32 "\n", this->old_imp_version);
  fprintf(stream, "  current_version: %08" PRIX32 "\n", this->current_version);

  fputs("  main: ", stream);
  this->main_symbol.print(stream);
  fputs("\n  init: ", stream);
  this->init_symbol.print(stream);
  fputs("\n  term: ", stream);
  this->term_symbol.print(stream);
  fputc('\n', stream);

  for (size_t x = 0; x < this->sections.size(); x++) {
    const auto& sec = this->sections[x];
    fprintf(stream, "  [section %zX] name %s\n", x, sec.name.empty() ? "__missing__" : sec.name.c_str());
    fprintf(stream, "  [section %zX] default_address %08" PRIX32 "\n", x, sec.default_address);
    fprintf(stream, "  [section %zX] total_size %" PRIX32 "\n", x, sec.total_size);
    fprintf(stream, "  [section %zX] unpacked_size %" PRIX32 "\n", x, sec.unpacked_size);
    fprintf(stream, "  [section %zX] packed_size %" PRIX32 "\n", x, sec.packed_size);
    fprintf(stream, "  [section %zX] section_kind %s\n", x, name_for_section_kind(sec.section_kind));
    fprintf(stream, "  [section %zX] share_kind %s\n", x, name_for_share_kind(sec.share_kind));
    fprintf(stream, "  [section %zX] alignment %02hhX\n", x, sec.alignment);
    if (sec.section_kind == PEFFSectionKind::EXECUTABLE_READONLY || 
        sec.section_kind == PEFFSectionKind::EXECUTABLE_READWRITE) {
      string disassembly = this->arch_is_ppc
          ? PPC32Emulator::disassemble(sec.data.data(), sec.data.size(), 0)
          : M68KEmulator::disassemble(sec.data.data(), sec.data.size(), 0, nullptr);
      fwritex(stream, disassembly);
    } else if (!sec.data.empty()) {
      fprintf(stream, "  [section %zX] data\n", x);
      print_data(stream, sec.data);
    }
    if (!sec.relocation_program.empty()) {
      fprintf(stream, "  [section %zX] relocation program\n", x);
      disassemble_relocation_program(stream, sec.relocation_program);
    }
  }

  for (const auto& it : this->export_symbols) {
    const auto& name = it.first;
    const auto& sym = it.second;

    fprintf(stream, "  export %s => ", name.c_str());
    sym.print(stream);
    fputc('\n', stream);
  }

  for (size_t x = 0; x < this->import_symbols.size(); x++) {
    const auto& sym = this->import_symbols[x];

    fprintf(stream, "  import %zu => ", x);
    sym.print(stream);
    fputc('\n', stream);
  }
}



void PEFFFile::load_into(const string& lib_name, shared_ptr<MemoryContext> mem,
    uint32_t base_addr) {
  vector<uint32_t> section_addrs;
  for (const auto& section : this->sections) {
    if (section.total_size < section.data.size()) {
      throw runtime_error("section total size is smaller than data size");
    }
    if (section.total_size == 0) {
      section_addrs.emplace_back(0);
      continue;
    }

    // data was already unpacked; just copy it in and zero the extra space
    uint32_t section_addr;
    if (base_addr == 0) {
      section_addr = mem->allocate(section.total_size);
    } else {
      section_addr = mem->allocate_at(base_addr, section.total_size);
      size_t page_size = mem->get_page_size();
      base_addr = (base_addr + section.total_size + (page_size - 1)) & (~(page_size - 1));
    }
    if (section_addr == 0) {
      throw runtime_error("cannot allocate memory for section");
    }

    void* section_mem = mem->at(section_addr);
    memcpy(section_mem, section.data.data(), section.data.size());
    memset(reinterpret_cast<uint8_t*>(section_mem) + section.data.size(), 0,
        section.total_size - section.data.size());
    section_addrs.emplace_back(section_addr);
  }

  auto get_import_symbol_addr = [&](uint32_t index) -> uint32_t {
    const auto& sym = this->import_symbols.at(index);
    string name = sym.lib_name + ":" + sym.name;
    try {
      return mem->get_symbol_addr(name.c_str());
    } catch (const out_of_range&) {
      if (!(sym.flags & PEFFLoaderImportSymbolFlags::WEAK)) {
        throw;
      } else {
        return 0;
      }
    }
  };

  auto add_at_addr = [&](uint32_t addr, uint32_t delta) -> void {
    uint32_t value = bswap32(mem->read<uint32_t>(addr));
    mem->write<uint32_t>(addr, bswap32(value + delta));
  };

  // run relocation programs
  for (size_t x = 0; x < this->sections.size(); x++) {
    auto& section = this->sections[x];
    StringReader r(section.relocation_program.data(), section.relocation_program.size());

    uint32_t section_addr = section_addrs[x];
    uint32_t pending_repeat_count = 0;
    uint32_t reloc_address = section_addr;
    uint32_t import_index = 0;
    // TODO: either of these can be initialized to zero if the relevant section
    // is missing or not instantiated
    uint32_t section_c = section_addrs[0] - this->sections[0].default_address;
    uint32_t section_d = section_addrs[1] - this->sections[1].default_address;

    while (!r.eof()) {
      uint16_t cmd = r.get_u16r();

      if ((cmd & 0xC000) == 0x0000) {
        uint8_t count = cmd & 0x3F;
        uint8_t skip_count = (cmd >> 6) & 0xFF;
        reloc_address += skip_count * 4;
        for (; count; count--, reloc_address += 4) {
          add_at_addr(reloc_address, section_d);
        }
      } else if ((cmd & 0xE000) == 0x4000) {
        uint16_t count = (cmd & 0x01FF) + 1;
        if ((cmd & 0x1E00) == 0x0000) {
          for (; count; count--, reloc_address += 4) {
            add_at_addr(reloc_address, section_c);
          }
        } else if ((cmd & 0x1E00) == 0x0200) {
          for (; count; count--, reloc_address += 4) {
            add_at_addr(reloc_address, section_d);
          }
        } else if ((cmd & 0x1E00) == 0x0400) {
          for (; count; count--, reloc_address += 12) {
            add_at_addr(reloc_address, section_c);
            add_at_addr(reloc_address + 4, section_d);
          }
        } else if ((cmd & 0x1E00) == 0x0600) {
          for (; count; count--, reloc_address += 8) {
            add_at_addr(reloc_address, section_c);
            add_at_addr(reloc_address + 4, section_d);
          }
        } else if ((cmd & 0x1E00) == 0x0800) {
          for (; count; count--, reloc_address += 8) {
            add_at_addr(reloc_address, section_d);
          }
        } else if ((cmd & 0x1E00) == 0x0A00) {
          for (; count; count--, reloc_address += 4, import_index++) {
            add_at_addr(reloc_address, get_import_symbol_addr(import_index));
          }
        } else {
          throw runtime_error("invalid relocation command");
        }
      } else if ((cmd & 0xE000) == 0x6000) {
        uint16_t index = cmd & 0x01FF;
        if ((cmd & 0x1E00) == 0x0000) {
          add_at_addr(reloc_address, get_import_symbol_addr(index));
          reloc_address += 4;
          import_index = index + 1;
        } else if ((cmd & 0x1E00) == 0x0200) {
          section_c = section_addrs.at(index);
        } else if ((cmd & 0x1E00) == 0x0400) {
          section_d = section_addrs.at(index);
        } else if ((cmd & 0x1E00) == 0x0600) {
          add_at_addr(reloc_address, section_addrs.at(index));
        } else {
          throw runtime_error("invalid relocation command");
        }
      } else if ((cmd & 0xF000) == 0x8000) {
        uint16_t delta = (cmd & 0x0FFF) + 1;
        reloc_address += delta;
      } else if ((cmd & 0xF000) == 0x9000) {
        uint8_t blocks = ((cmd >> 8) & 0x0F) + 1;
        uint16_t times = (cmd & 0x00FF) + 1;
        if (pending_repeat_count == 0) {
          pending_repeat_count = times;
          r.go(r.where() - 2 * blocks);
        } else if (pending_repeat_count != 1) {
          pending_repeat_count--;
          r.go(r.where() - 2 * blocks);
        } else {
          pending_repeat_count = 0;
        }
      } else if ((cmd & 0xFC00) == 0xA000) {
        uint32_t offset = ((cmd & 0x03FF) << 16) | r.get_u16r();
        reloc_address = section_addr + offset;
      } else if ((cmd & 0xFC00) == 0xA400) {
        uint32_t index = ((cmd & 0x03FF) << 16) | r.get_u16r();
        add_at_addr(reloc_address, get_import_symbol_addr(index));
        reloc_address += 4;
        import_index = index + 1;
      } else if ((cmd & 0xFC00) == 0xB000) {
        uint8_t blocks = ((cmd >> 6) & 0x0F) + 1;
        uint32_t times = ((cmd & 0x003F) << 16) | r.get_u16r();
        if (pending_repeat_count == 0) {
          pending_repeat_count = times;
          r.go(r.where() - 2 * blocks);
        } else if (pending_repeat_count != 1) {
          pending_repeat_count--;
          r.go(r.where() - 2 * blocks);
        } else {
          pending_repeat_count = 0;
        }
      } else if ((cmd & 0xFC00) == 0xB400) {
        uint8_t subcmd = (cmd >> 6) & 0x0F;
        uint32_t index = ((cmd & 0x003F) << 16) | r.get_u16r();
        if (subcmd == 0x0) {
          add_at_addr(reloc_address, section_addrs.at(index));
        } else if (subcmd == 0x1) {
          section_c = section_addrs.at(index);
        } else if (subcmd == 0x2) {
          section_d = section_addrs.at(index);
        } else {
          throw runtime_error("invalid relocation command");
        }
      }
    }
  }

  // register exported symbols
  auto register_export_symbol = [&](const ExportSymbol& exp) {
    string name = lib_name + ":" + exp.name;
    uint32_t sec_base = section_addrs.at(exp.section_index);
    mem->set_symbol_addr(name.c_str(), sec_base + exp.value);
  };
  if (!this->main_symbol.name.empty()) {
    register_export_symbol(this->main_symbol);
  }
  if (!this->init_symbol.name.empty()) {
    register_export_symbol(this->init_symbol);
  }
  if (!this->term_symbol.name.empty()) {
    register_export_symbol(this->term_symbol);
  }
  for (const auto& it : this->export_symbols) {
    register_export_symbol(it.second);
  }
  for (size_t x = 0; x < section_addrs.size(); x++) {
    if (!section_addrs[x]) {
      continue;
    }
    string name = string_printf("%s:section:%zu", lib_name.c_str(), x);
    mem->set_symbol_addr(name.c_str(), section_addrs.at(x));
  }
}
