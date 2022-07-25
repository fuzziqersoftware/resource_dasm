#include "PEFile.hh"

#include <inttypes.h>
#include <string.h>

#include <map>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>

#include "../Emulators/MemoryContext.hh"
#include "../Emulators/X86Emulator.hh"

using namespace std;



PEFile::PEFile(const char* filename) : PEFile(filename, load_file(filename)) { }

PEFile::PEFile(const char* filename, const string& data)
  : PEFile(filename, data.data(), data.size()) { }

PEFile::PEFile(const char* filename, const void* data, size_t size)
  : filename(filename) {
  this->parse(data, size);
}

void PEFile::load_into(shared_ptr<MemoryContext> mem) {
  // Since we may be loading on a system with a larger page size than the system
  // the PE was compiled for, preallocate an arena for the entire thing because
  // we may have to do fixed-address allocations across arena boundaries if we
  // don't preallocate.
  uint32_t min_addr = 0xFFFFFFFF, max_addr = 0x00000000;
  for (const auto& section : this->sections) {
    if (section.address < min_addr) {
      min_addr = section.address;
    }
    uint32_t end_addr = section.address + section.size;
    if (end_addr > max_addr) {
      max_addr = end_addr;
    }
  }
  mem->preallocate_arena(min_addr, max_addr - min_addr);

  for (const auto& section : this->sections) {
    if (section.size == 0) {
      continue;
    }
    size_t bytes_to_copy = min<size_t>(section.size, section.data.size());
    mem->allocate_at(section.address, section.size);
    void* section_mem = mem->at<void>(section.address, bytes_to_copy);
    memcpy(section_mem, section.data.data(), bytes_to_copy);
    memset(reinterpret_cast<uint8_t*>(section_mem) + bytes_to_copy, 0,
        section.size - bytes_to_copy);
  }
}

multimap<uint32_t, string> PEFile::labels_for_loaded_imports() const {
  multimap<uint32_t, string> ret;
  for (const auto& lib_it : this->import_libs) {
    const auto& lib = lib_it.second;
    for (const auto& imp : lib.imports) {
      string name = imp.name.empty()
          ? string_printf("%s:<Ordinal%04hX>", lib.name.c_str(), imp.ordinal)
          : string_printf("%s:%s", lib.name.c_str(), imp.name.c_str());
      ret.emplace(imp.addr_rva + this->header.image_base, move(name));
    }
  }
  return ret;
}

const PEHeader& PEFile::unloaded_header() const {
  return this->header;
}

StringReader PEFile::read_from_rva(uint32_t rva, uint32_t size) const {
  for (const auto& sec : this->sections) {
    uint32_t offset_within_section = rva - sec.rva;
    if (offset_within_section >= sec.data.size()) {
      continue;
    }
    // If size extends beyond the end of the section, truncate the reader to the
    // end of the section.
    size_t r_size = min<size_t>(sec.data.size() - offset_within_section, size);
    return StringReader(sec.data.data() + offset_within_section, r_size);
  }
  throw out_of_range("rva not within any initialized section");
}

void PEFile::parse(const void* data, size_t size) {
  StringReader r(data, size);

  const auto& mz_header = r.get<MZHeader>();
  if (mz_header.signature != 0x4D5A) {
    throw runtime_error("file does not have MZ signature");
  }
  r.go(mz_header.pe_header_offset);

  this->header = r.get<PEHeader>();
  if (this->header.signature != 0x50450000) {
    throw runtime_error("file does not have PE signature");
  }
  if (this->header.magic == 0x020B) {
    throw runtime_error("PE32+ format is not implemented");
  }
  if (this->header.magic != 0x010B) {
    throw runtime_error("file has incorrect magic value");
  }

  r.go(mz_header.pe_header_offset + offsetof(PEHeader, magic) + this->header.optional_header_size);
  while (this->sections.size() < this->header.num_sections) {
    const auto& sec_header = r.get<PESectionHeader>();

    Section sec;
    sec.rva = sec_header.rva;
    sec.file_offset = sec_header.file_data_rva;
    sec.relocations_rva = sec_header.relocations_rva;
    sec.line_numbers_rva = sec_header.line_numbers_rva;
    sec.num_relocations = sec_header.num_relocations;
    sec.num_line_numbers = sec_header.num_line_numbers;
    sec.flags = sec_header.flags;

    sec.name.assign(sec_header.name, 8);
    strip_trailing_zeroes(sec.name);
    sec.address = sec_header.rva + this->header.image_base;
    sec.size = sec_header.loaded_size;
    sec.data = r.preadx(sec_header.file_data_rva, sec_header.file_data_size);

    this->sections.emplace_back(move(sec));
  }

  // Now that sections have been read, we can use read_from_rva to parse
  // internal structures

  if (this->header.import_table_rva) {
    auto r = this->read_from_rva(this->header.import_table_rva, this->header.import_table_size);
    while (!r.eof()) {
      const auto& lib_entry = r.get<PEImportLibraryHeader>();
      if (lib_entry.lookup_table_rva == 0) {
        break;
      }

      string name;
      {
        auto name_r = this->read_from_rva(lib_entry.name_rva);
        name = name_r.get_cstr();
      }
      if (name.empty()) {
        throw runtime_error("import library entry name is blank");
      }

      auto& lib = this->import_libs[name];
      lib.name = name;

      auto lookup_table_r = this->read_from_rva(lib_entry.lookup_table_rva);
      while (!lookup_table_r.eof()) {
        uint32_t addr_addr = lib_entry.address_ptr_table_rva + lookup_table_r.where();
        const auto& imp_entry = lookup_table_r.get<PEImportTableEntry>();
        if (imp_entry.is_null()) {
          break;
        }
        if (imp_entry.is_ordinal()) {
          lib.imports.emplace_back(ImportLibrary::Function{
              imp_entry.ordinal(), "", addr_addr});
        } else {
          auto name_r = this->read_from_rva(imp_entry.name_table_entry_rva());
          uint16_t ordinal_hint = name_r.get_u16l();
          string name = name_r.get_cstr();
          lib.imports.emplace_back(ImportLibrary::Function{
              ordinal_hint, move(name), addr_addr});
        }
      }
    }
  }
}

static const char* name_for_architecture(uint16_t architecture) {
  static const unordered_map<uint16_t, const char*> names({
    {0x014C, "x86/i386"},
    {0x0166, "MIPS little-endian"},
    {0x0169, "MIPS little-endian WCE v2"},
    {0x01A2, "Hitachi SH3"},
    {0x01A3, "Hitachi SH3 DSP"},
    {0x01A6, "Hitachi SH4"},
    {0x01A8, "Hitachi SH5"},
    {0x01C0, "ARM little-endian"},
    {0x01C2, "Thumb"},
    {0x01C4, "ARM Thumb-2 little-endian"},
    {0x01D3, "Matsushita AM33"},
    {0x01F0, "PowerPC little-endian"},
    {0x01F1, "PowerPC with FPU"},
    {0x0200, "IA-64/Itanium"},
    {0x0266, "MIPS16"},
    {0x0366, "MIPS with FPU"},
    {0x0466, "MIPS16 with FPU"},
    {0x0EBC, "EFI bytecode"},
    {0x5032, "RISC-V 32-bit addressing"},
    {0x5064, "RISC-V 64-bit addressing"},
    {0x5128, "RISC-V 128-bit addressing"},
    {0x6232, "LoongArch 32-bit"},
    {0x6264, "LoongArch 64-bit"},
    {0x8664, "AMD64"},
    {0x9041, "Mitsubishi M32R little endian"},
    {0xAA64, "ARM64 little-endian"},
  });
  try {
    return names.at(architecture);
  } catch (const out_of_range&) {
    return "unknown";
  }
}

static string string_for_flags(uint16_t flags) {
  vector<const char*> tokens;
  if (flags & 0x0001) {
    tokens.emplace_back("RELOCS_STRIPPED");
  }
  if (flags & 0x0002) {
    tokens.emplace_back("EXECUTABLE_IMAGE");
  }
  if (flags & 0x0004) {
    tokens.emplace_back("LINE_NUMS_STRIPPED");
  }
  if (flags & 0x0008) {
    tokens.emplace_back("LOCAL_SYMS_STRIPPED");
  }
  if (flags & 0x0010) {
    tokens.emplace_back("AGGRESSIVELY_TRIM_WORKING_SET");
  }
  if (flags & 0x0020) {
    tokens.emplace_back("LARGE_ADDRESS_AWARE");
  }
  if (flags & 0x0080) {
    tokens.emplace_back("LITTLE_ENDIAN");
  }
  if (flags & 0x0100) {
    tokens.emplace_back("32BIT_MACHINE");
  }
  if (flags & 0x0200) {
    tokens.emplace_back("DEBUG_STRIPPED");
  }
  if (flags & 0x0400) {
    tokens.emplace_back("REMOVABLE_RUN_FROM_SWAP");
  }
  if (flags & 0x0800) {
    tokens.emplace_back("NET_RUN_FROM_SWAP");;
  }
  if (flags & 0x1000) {
    tokens.emplace_back("IS_SYSTEM_FILE");
  }
  if (flags & 0x2000) {
    tokens.emplace_back("IS_DLL");
  }
  if (flags & 0x4000) {
    tokens.emplace_back("UNIPROCESSOR_SYSTEM_ONLY");
  }
  if (flags & 0x8000) {
    tokens.emplace_back("BIG_ENDIAN");
  }
  if (tokens.empty()) {
    return "none";
  } else {
    return join(tokens, ",");
  }
}

static const char* name_for_subsystem(uint16_t subsystem) {
  static const vector<const char*> names({
    "unknown", // 0
    "native", // 1
    "windows_gui", // 2
    "windows_char", // 3
    "unknown", // 4
    "os2_char", // 5
    "unknown", // 6
    "posix_char", // 7
    "windows9x_native", // 8
    "windows_ce_gui", // 9
    "efi", // 10
    "boot_service_driver", // 11
    "efi_runtime_driver", // 12
    "efi_rom", // 13
    "xbox", // 14
    "unknown", // 15
    "windows_boot_application", // 16
  });
  try {
    return names.at(subsystem);
  } catch (const out_of_range&) {
    return "unknown";
  }
}

static string string_for_dll_flags(uint16_t flags) {
  vector<const char*> tokens;
  if (flags & 0x0020) {
    tokens.emplace_back("HIGH_ENTROPY_ADDRESS_SPACE");
  }
  if (flags & 0x0040) {
    tokens.emplace_back("RELOCATABLE");
  }
  if (flags & 0x0080) {
    tokens.emplace_back("FORCE_INTEGRITY_CHECKS");
  }
  if (flags & 0x0100) {
    tokens.emplace_back("NX_COMPATIBLE");
  }
  if (flags & 0x0200) {
    tokens.emplace_back("NO_ISOLATION");
  }
  if (flags & 0x0400) {
    tokens.emplace_back("NO_SEH");
  }
  if (flags & 0x0800) {
    tokens.emplace_back("DO_NOT_BIND");
  }
  if (flags & 0x1000) {
    tokens.emplace_back("MUST_EXECUTE_IN_APPCONTAINER");
  }
  if (flags & 0x2000) {
    tokens.emplace_back("IS_WDM_DRIVER");
  }
  if (flags & 0x4000) {
    tokens.emplace_back("GUARD_CONTROL_FLOW");
  }
  if (flags & 0x8000) {
    tokens.emplace_back("TERMINAL_SERVER_AWARE");
  }
  if (tokens.empty()) {
    return "none";
  } else {
    return join(tokens, ",");
  }
}

static string string_for_section_flags(uint32_t flags) {
  vector<const char*> tokens;
  if (flags & 0x00000008) {
    tokens.emplace_back("NO_PADDING");
  }
  if (flags & 0x00000020) {
    tokens.emplace_back("CONTAINS_CODE");
  }
  if (flags & 0x00000040) {
    tokens.emplace_back("CONTAINS_INITIALIZED_DATA");
  }
  if (flags & 0x00000080) {
    tokens.emplace_back("CONTAINS_UNINITIALIZED_DATA");
  }
  if (flags & 0x00000100) {
    tokens.emplace_back("LNK_OTHER");
  }
  if (flags & 0x00000200) {
    tokens.emplace_back("LNK_INFO");
  }
  if (flags & 0x00000800) {
    tokens.emplace_back("LNK_REMOVE");
  }
  if (flags & 0x00001000) {
    tokens.emplace_back("LNK_COMDAT");
  }
  if (flags & 0x00008000) {
    tokens.emplace_back("GPREL");
  }
  if (flags & 0x00020000) {
    tokens.emplace_back("MEM_PURGEABLE/MEM_16BIT");
  }
  if (flags & 0x00040000) {
    tokens.emplace_back("MEM_LOCKED");
  }
  if (flags & 0x00080000) {
    tokens.emplace_back("MEM_PRELOAD");
  }
  if ((flags & 0x00F00000) == 0x00100000) {
    tokens.emplace_back("ALIGN_1");
  }
  if ((flags & 0x00F00000) == 0x00200000) {
    tokens.emplace_back("ALIGN_2");
  }
  if ((flags & 0x00F00000) == 0x00300000) {
    tokens.emplace_back("ALIGN_4");
  }
  if ((flags & 0x00F00000) == 0x00400000) {
    tokens.emplace_back("ALIGN_8");
  }
  if ((flags & 0x00F00000) == 0x00500000) {
    tokens.emplace_back("ALIGN_16");
  }
  if ((flags & 0x00F00000) == 0x00600000) {
    tokens.emplace_back("ALIGN_32");
  }
  if ((flags & 0x00F00000) == 0x00700000) {
    tokens.emplace_back("ALIGN_64");
  }
  if ((flags & 0x00F00000) == 0x00800000) {
    tokens.emplace_back("ALIGN_128");
  }
  if ((flags & 0x00F00000) == 0x00900000) {
    tokens.emplace_back("ALIGN_256");
  }
  if ((flags & 0x00F00000) == 0x00A00000) {
    tokens.emplace_back("ALIGN_512");
  }
  if ((flags & 0x00F00000) == 0x00B00000) {
    tokens.emplace_back("ALIGN_1024");
  }
  if ((flags & 0x00F00000) == 0x00C00000) {
    tokens.emplace_back("ALIGN_2048");
  }
  if ((flags & 0x00F00000) == 0x00D00000) {
    tokens.emplace_back("ALIGN_4096");
  }
  if ((flags & 0x00F00000) == 0x00E00000) {
    tokens.emplace_back("ALIGN_8192");
  }
  if (flags & 0x01000000) {
    tokens.emplace_back("LNK_NRELOC_OVFL");
  }
  if (flags & 0x02000000) {
    tokens.emplace_back("MEM_DISCARDABLE");
  }
  if (flags & 0x04000000) {
    tokens.emplace_back("MEM_NOT_CACHED");
  }
  if (flags & 0x08000000) {
    tokens.emplace_back("MEM_NOT_PAGED");
  }
  if (flags & 0x10000000) {
    tokens.emplace_back("MEM_SHARED");
  }
  if (flags & 0x20000000) {
    tokens.emplace_back("MEM_EXECUTE");
  }
  if (flags & 0x40000000) {
    tokens.emplace_back("MEM_READ");
  }
  if (flags & 0x80000000) {
    tokens.emplace_back("MEM_WRITE");
  }
  if (tokens.empty()) {
    return "none";
  } else {
    return join(tokens, ",");
  }
}

static const char* name_for_magic(uint16_t magic) {
  if (magic == 0x010B) {
    return "PE32";
  } else if (magic == 0x020B) {
    return "PE32+";
  } else {
    return "unknown";
  }
}

void PEFile::print(
    FILE* stream,
    const multimap<uint32_t, string>* labels,
    bool print_hex_view_for_code) const {
  fprintf(stream, "[PE file: %s]\n", this->filename.c_str());
  fprintf(stream, "  architecture: %04hX (%s)\n", this->header.architecture.load(), name_for_architecture(this->header.architecture));
  fprintf(stream, "  num_sections: %04hX\n", this->header.num_sections.load());
  fprintf(stream, "  build_timestamp: %08" PRIX32 "\n", this->header.build_timestamp.load());
  fprintf(stream, "  symbol_table: rva=%08" PRIX32 " size=%08" PRIX32 " (deprecated)\n", this->header.deprecated_symbol_table_rva.load(), this->header.deprecated_symbol_table_size.load());
  string flags_str = string_for_flags(this->header.flags);
  fprintf(stream, "  flags: %04hX (%s)\n", this->header.flags.load(), flags_str.c_str());
  fprintf(stream, "  magic: %04hX (%s)\n", this->header.magic.load(), name_for_magic(this->header.magic));
  fprintf(stream, "  linker_version: %04hX\n", this->header.linker_version.load());
  fprintf(stream, "  total_code_size: %08" PRIX32 "\n", this->header.total_code_size.load());
  fprintf(stream, "  total_initialized_data_size: %08" PRIX32 "\n", this->header.total_initialized_data_size.load());
  fprintf(stream, "  total_uninitialized_data_size: %08" PRIX32 "\n", this->header.total_uninitialized_data_size.load());
  fprintf(stream, "  entrypoint_rva: %08" PRIX32 " (loaded as %08" PRIX32 ")\n", this->header.entrypoint_rva.load(), this->header.entrypoint_rva + this->header.image_base);
  fprintf(stream, "  code_base_rva: %08" PRIX32 " (loaded as %08" PRIX32 ")\n", this->header.code_base_rva.load(), this->header.code_base_rva + this->header.image_base);
  fprintf(stream, "  data_base_rva: %08" PRIX32 " (loaded as %08" PRIX32 ")\n", this->header.data_base_rva.load(), this->header.data_base_rva + this->header.image_base);
  fprintf(stream, "  image_base: %08" PRIX32 "\n", this->header.image_base.load());
  fprintf(stream, "  loaded_section_alignment: %08" PRIX32 "\n", this->header.loaded_section_alignment.load());
  fprintf(stream, "  file_section_alignment: %08" PRIX32 "\n", this->header.file_section_alignment.load());
  fprintf(stream, "  os_version: %04hX.%04hX\n", this->header.os_version[0].load(), this->header.os_version[1].load());
  fprintf(stream, "  image_version: %04hX.%04hX\n", this->header.image_version[0].load(), this->header.image_version[1].load());
  fprintf(stream, "  subsystem_version: %04hX.%04hX\n", this->header.subsystem_version[0].load(), this->header.subsystem_version[1].load());
  fprintf(stream, "  win32_version: %08" PRIX32 "\n", this->header.win32_version.load());
  fprintf(stream, "  virtual_image_size: %08" PRIX32 "\n", this->header.virtual_image_size.load());
  fprintf(stream, "  total_header_size: %08" PRIX32 "\n", this->header.total_header_size.load());
  fprintf(stream, "  checksum: %08" PRIX32 " (unused)\n", this->header.checksum.load());
  fprintf(stream, "  subsystem: %04hX (%s)\n", this->header.subsystem.load(), name_for_subsystem(this->header.subsystem));
  string dll_flags_str = string_for_dll_flags(this->header.dll_flags);
  fprintf(stream, "  dll_flags: %04hX (%s)\n", this->header.dll_flags.load(), dll_flags_str.c_str());
  fprintf(stream, "  stack_reserve_size: %08" PRIX32 "\n", this->header.stack_reserve_size.load());
  fprintf(stream, "  stack_commit_size: %08" PRIX32 "\n", this->header.stack_commit_size.load());
  fprintf(stream, "  heap_reserve_size: %08" PRIX32 "\n", this->header.heap_reserve_size.load());
  fprintf(stream, "  heap_commit_size: %08" PRIX32 "\n", this->header.heap_commit_size.load());
  fprintf(stream, "  loader_flags: %08" PRIX32 "\n", this->header.loader_flags.load());
  fprintf(stream, "  data_directory_count: %08" PRIX32 "\n", this->header.data_directory_count.load());
  fprintf(stream, "  directory(export_table): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.export_table_rva.load(), this->header.export_table_rva + this->header.image_base, this->header.export_table_size.load());
  fprintf(stream, "  directory(import_table): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.import_table_rva.load(), this->header.import_table_rva + this->header.image_base, this->header.import_table_size.load());
  fprintf(stream, "  directory(resource_table): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.resource_table_rva.load(), this->header.resource_table_rva + this->header.image_base, this->header.resource_table_size.load());
  fprintf(stream, "  directory(exception_table): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.exception_table_rva.load(), this->header.exception_table_rva + this->header.image_base, this->header.exception_table_size.load());
  fprintf(stream, "  directory(certificate_table): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.certificate_table_rva.load(), this->header.certificate_table_rva + this->header.image_base, this->header.certificate_table_size.load());
  fprintf(stream, "  directory(relocation_table): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.relocation_table_rva.load(), this->header.relocation_table_rva + this->header.image_base, this->header.relocation_table_size.load());
  fprintf(stream, "  directory(debug_data): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.debug_data_rva.load(), this->header.debug_data_rva + this->header.image_base, this->header.debug_data_size.load());
  fprintf(stream, "  directory(architecture_data): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.architecture_data_rva.load(), this->header.architecture_data_rva + this->header.image_base, this->header.architecture_data_size.load());
  fprintf(stream, "  directory(global_ptr): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") unused=%08" PRIX32 "\n", this->header.global_ptr_rva.load(), this->header.global_ptr_rva + this->header.image_base, this->header.unused.load());
  fprintf(stream, "  directory(tls_table): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.tls_table_rva.load(), this->header.tls_table_rva + this->header.image_base, this->header.tls_table_size.load());
  fprintf(stream, "  directory(load_config_table): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.load_config_table_rva.load(), this->header.load_config_table_rva + this->header.image_base, this->header.load_config_table_size.load());
  fprintf(stream, "  directory(bound_import): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.bound_import_rva.load(), this->header.bound_import_rva + this->header.image_base, this->header.bound_import_size.load());
  fprintf(stream, "  directory(import_address_table): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.import_address_table_rva.load(), this->header.import_address_table_rva + this->header.image_base, this->header.import_address_table_size.load());
  fprintf(stream, "  directory(delay_import_descriptor): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.delay_import_descriptor_rva.load(), this->header.delay_import_descriptor_rva + this->header.image_base, this->header.delay_import_descriptor_size.load());
  fprintf(stream, "  directory(clr_runtime_header): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.clr_runtime_header_rva.load(), this->header.clr_runtime_header_rva + this->header.image_base, this->header.clr_runtime_header_size.load());
  fprintf(stream, "  directory(unused): rva=%08" PRIX32 " (loaded as %08" PRIX32 ") size=%08" PRIX32 "\n", this->header.unused_rva.load(), this->header.unused_rva + this->header.image_base, this->header.unused_size.load());

  if (!this->import_libs.empty()) {
    fprintf(stream, "[import table]\n");

    for (const auto& imp_lib_it : this->import_libs) {
      const auto& lib = imp_lib_it.second;
      fprintf(stream, "  [library: %s]\n", lib.name.c_str());
      for (const auto& imp : lib.imports) {
        if (imp.name.empty()) {
          fprintf(stream, "    (ordinal:%04hX) -> %08" PRIX32 " (at %08" PRIX32 " when loaded)\n",
              imp.ordinal, imp.addr_rva, imp.addr_rva + this->header.image_base);
        } else {
          fprintf(stream, "    %s (hint:%04hX) -> %08" PRIX32 " (at %08" PRIX32 " when loaded)\n",
              imp.name.c_str(), imp.ordinal, imp.addr_rva, imp.addr_rva + this->header.image_base);
        }
      }
    }
  }

  multimap<uint32_t, string> all_labels = this->labels_for_loaded_imports();
  if (labels) {
    for (const auto& it : *labels) {
      all_labels.emplace(it.first, it.second);
    }
  }

  for (size_t x = 0; x < this->sections.size(); x++) {
    const auto& sec = this->sections[x];
    fprintf(stream, "\n[section %zu header]\n", x);

    fprintf(stream, "  name: %s\n", sec.name.c_str());
    fprintf(stream, "  rva: %08" PRIX32 " (loaded as %08" PRIX32 ")\n", sec.rva, sec.address);
    fprintf(stream, "  loaded_size: %08" PRIX32 "\n", sec.size);
    fprintf(stream, "  file_offset: %08" PRIX32 "\n", sec.file_offset);
    fprintf(stream, "  relocations_rva: %08" PRIX32 "\n", sec.relocations_rva);
    fprintf(stream, "  line_numbers_rva: %08" PRIX32 "\n", sec.line_numbers_rva);
    fprintf(stream, "  num_relocations: %04hX\n", sec.num_relocations);
    fprintf(stream, "  num_line_numbers: %04hX\n", sec.num_line_numbers);
    string sec_flags_str = string_for_section_flags(sec.flags);
    fprintf(stream, "  flags: %08" PRIX32 " (%s)\n", sec.flags, sec_flags_str.c_str());

    if (!sec.data.empty()) {
      if ((this->header.architecture == 0x014C) && (sec.flags & 0x00000020)) {
        string disassembly = X86Emulator::disassemble(sec.data.data(), sec.data.size(), sec.address, &all_labels);
        fprintf(stream, "[section %zX disassembly]\n", x);
        fwritex(stream, disassembly);
        if (print_hex_view_for_code) {
          fprintf(stream, "[section %zX data]\n", x);
          print_data(stream, sec.data, sec.address);
        }
      } else if (!sec.data.empty()) {
        fprintf(stream, "[section %zX data]\n", x);
        print_data(stream, sec.data, sec.address);
      }
    }
  }
}
