#include "ELFFile.hh"

#include <inttypes.h>
#include <string.h>

#include <map>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>

#include "../Emulators/M68KEmulator.hh"
#include "../Emulators/MemoryContext.hh"
#include "../Emulators/PPC32Emulator.hh"
#include "../Emulators/X86Emulator.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {

ELFFile::ELFFile(const char* filename) : ELFFile(filename, load_file(filename)) {}

ELFFile::ELFFile(const char* filename, const string& data)
    : ELFFile(filename, data.data(), data.size()) {}

ELFFile::ELFFile(const char* filename, const void* data, size_t size)
    : filename(filename) {
  this->parse(data, size);
}

void ELFFile::parse(const void* data, size_t size) {
  StringReader r(data, size);
  this->identifier = r.get<ELFIdentifier>();
  if (this->identifier.magic != 0x7F454C46) { // '\x7FELF'
    throw runtime_error("incorrect signature");
  }

  if (this->identifier.format_version != 1) {
    throw runtime_error("unsupported format version");
  }

  if (this->identifier.width == 1) {
    if (this->identifier.endianness == 1) {
      this->parse_t<le_uint16_t, le_uint32_t, le_uint32_t>(r);
    } else if (this->identifier.endianness == 2) {
      this->parse_t<be_uint16_t, be_uint32_t, be_uint32_t>(r);
    } else {
      throw runtime_error("unsupported endianness");
    }
  } else if (this->identifier.width == 2) {
    if (this->identifier.endianness == 1) {
      this->parse_t<le_uint16_t, le_uint32_t, le_uint64_t>(r);
    } else if (this->identifier.endianness == 2) {
      this->parse_t<be_uint16_t, be_uint32_t, be_uint64_t>(r);
    } else {
      throw runtime_error("unsupported endianness");
    }
  } else {
    throw runtime_error("unsupported field width");
  }
}

template <typename U16T, typename U32T, typename LongT>
void ELFFile::parse_t(StringReader& r) {
  const auto& header = r.get<ELFHeader<U16T, U32T, LongT>>();
  this->type = header.type;
  this->architecture = header.architecture;
  this->entrypoint_addr = header.entrypoint_addr;
  this->flags = header.flags;

  r.go(header.section_header_offset);
  this->sections.clear();
  vector<uint32_t> sec_name_offsets;
  while (this->sections.size() < header.section_header_entry_count) {
    const auto& sec_entry = r.get<ELFSectionHeaderEntry<U32T, LongT>>();
    sec_name_offsets.emplace_back(sec_entry.name_offset);
    auto& sec = this->sections.emplace_back();
    sec.type = sec_entry.type;
    sec.flags = sec_entry.flags;
    sec.virtual_addr = sec_entry.virtual_addr;
    sec.offset = sec_entry.offset;
    sec.physical_size = sec_entry.physical_size;
    sec.linked_section_num = sec_entry.linked_section_num;
    sec.info = sec_entry.info;
    sec.alignment = sec_entry.alignment;
    sec.entry_size = sec_entry.entry_size;
    sec.data = r.pread(sec.offset, sec.physical_size);
  }

  // Get the names from the names section (if possible)
  try {
    StringReader names_r(this->sections.at(header.names_section_index).data);
    for (size_t x = 0; x < this->sections.size(); x++) {
      auto& sec = this->sections[x];
      uint32_t name_offset = sec_name_offsets.at(x);
      sec.name = names_r.get_cstr(name_offset);
    }
  } catch (const exception&) {
  }
}

static const char* name_for_abi(uint16_t abi) {
  static const vector<const char*> names({
      /* 00 */ "System V",
      /* 01 */ "HP-UX",
      /* 02 */ "NetBSD",
      /* 03 */ "Linux",
      /* 04 */ "GNU Hurd",
      /* 05 */ "Unknown",
      /* 06 */ "Solaris",
      /* 07 */ "AIX",
      /* 08 */ "IRIX",
      /* 09 */ "FreeBSD",
      /* 0A */ "Tru64",
      /* 0B */ "Modesto",
      /* 0C */ "OpenBSD",
      /* 0D */ "OpenVMS",
      /* 0E */ "NonStop Kernel",
      /* 0F */ "AROS",
      /* 10 */ "FenixOS",
      /* 11 */ "CloudABI",
      /* 12 */ "OpenVOS",
  });
  try {
    return names.at(abi);
  } catch (const out_of_range&) {
    return "Unknown";
  }
}

static string name_for_file_type(uint16_t type) {
  if ((type & 0xFF00) == 0xFE00) {
    return std::format("(OS-specific {:02X})", type & 0xFF);
  }
  if ((type & 0xFF00) == 0xFF00) {
    return std::format("(architecture-specific {:02X})", type & 0xFF);
  }
  static const vector<const char*> names({
      /* 00 */ "Unspecified",
      /* 01 */ "Relocatable file",
      /* 02 */ "Executable file",
      /* 03 */ "Shared object",
      /* 04 */ "Core dump",
  });
  try {
    return names.at(type);
  } catch (const out_of_range&) {
    return "Unknown";
  }
}

static string name_for_section_type(uint32_t type) {
  if ((type & 0xF0000000) == 0x60000000) {
    return std::format("(OS-specific {:08X})", type & 0x0FFFFFFF);
  }
  if ((type & 0xF0000000) == 0x70000000) {
    return std::format("(architecture-specific {:08X})", type & 0x0FFFFFFF);
  }
  static const vector<const char*> names({
      /* 00 */ "Unused",
      /* 01 */ "Program data",
      /* 02 */ "Symbol table",
      /* 03 */ "String table",
      /* 04 */ "Relocation table with addends",
      /* 05 */ "Symbol hash table",
      /* 06 */ "Dynamic linker data",
      /* 07 */ "Notes",
      /* 08 */ "BSS section",
      /* 09 */ "Relocation table without addends",
      /* 0A */ "Reserved",
      /* 0B */ "Dynamic linker symbol table",
      /* 0E */ "Constructor array",
      /* 0F */ "Destructor array",
      /* 10 */ "Pre-constructor array",
      /* 11 */ "Section group",
      /* 12 */ "Extended section indices",
  });
  try {
    return names.at(type);
  } catch (const out_of_range&) {
    return "Unknown";
  }
}

static const char* name_for_architecture(uint16_t arch) {
  static const unordered_map<uint16_t, const char*> names({
      {0x0000, "Unspecified"},
      {0x0001, "AT&T WE 32100"},
      {0x0002, "SPARC"},
      {0x0003, "x86"},
      {0x0004, "Motorola 68000"},
      {0x0005, "Motorola 88000"},
      {0x0006, "Intel MCU"},
      {0x0007, "Intel 80860"},
      {0x0008, "MIPS"},
      {0x0009, "IBM System/370"},
      {0x000A, "MIPS RS3000 (little-endian)"},
      {0x000E, "HP PA-RISC"},
      {0x0013, "Intel 80960"},
      {0x0014, "PowerPC 32-bit"},
      {0x0015, "PowerPC 64-bit"},
      {0x0016, "S390/S390x"},
      {0x0017, "IBM SPU/SPC"},
      {0x0024, "NEC V800"},
      {0x0025, "Fujitsu FR20"},
      {0x0026, "TRW RH-32"},
      {0x0027, "Motorola RCE"},
      {0x0028, "ARM"},
      {0x0029, "Digital Alpha"},
      {0x002A, "SuperH"},
      {0x002B, "SPARC Version 9"},
      {0x002C, "Siemens TriCore embedded"},
      {0x002D, "Argonaut RISC Core"},
      {0x002E, "Hitachi H8/300"},
      {0x002F, "Hitachi H8/300H"},
      {0x0030, "Hitachi H8S"},
      {0x0031, "Hitachi H8/500"},
      {0x0032, "IA-64"},
      {0x0033, "Stanford MIPS-X"},
      {0x0034, "Motorola ColdFire"},
      {0x0035, "Motorola M68HC12"},
      {0x0036, "Fujitsu MMA Multimedia Accelerator"},
      {0x0037, "Siemens PCP"},
      {0x0038, "Sony nCPU embedded RISC"},
      {0x0039, "Denso NDR1"},
      {0x003A, "Motorola Star*Core"},
      {0x003B, "Toyota ME16"},
      {0x003C, "STMicroelectronics ST100"},
      {0x003D, "Advanced Logic Corp. TinyJ embedded"},
      {0x003E, "AMD64"},
      {0x008C, "TMS320C6000 family"},
      {0x00AF, "MCST Elbrus e2k"},
      {0x00B7, "ARM64 (ARMv8/aarch64)"},
      {0x00F3, "RISC-V"},
      {0x00F7, "Berkeley Packet Filter"},
      {0x0101, "WDC 65C816"},
  });
  try {
    return names.at(arch);
  } catch (const out_of_range&) {
    return "Unknown";
  }
}

static string string_for_section_flags(uint32_t flags) {
  vector<string> tokens;
  if (flags & 0x00000001) {
    tokens.emplace_back("writable");
  }
  if (flags & 0x00000002) {
    tokens.emplace_back("allocated");
  }
  if (flags & 0x00000004) {
    tokens.emplace_back("executable");
  }
  if (flags & 0x00000010) {
    tokens.emplace_back("mergeable");
  }
  if (flags & 0x00000020) {
    tokens.emplace_back("contains cstrings");
  }
  if (flags & 0x00000040) {
    tokens.emplace_back("info field has section index");
  }
  if (flags & 0x00000080) {
    tokens.emplace_back("preserve link order");
  }
  if (flags & 0x00000100) {
    tokens.emplace_back("non-conforming");
  }
  if (flags & 0x00000200) {
    tokens.emplace_back("group");
  }
  if (flags & 0x00000400) {
    tokens.emplace_back("TLS");
  }
  if (flags & 0x0FF00000) {
    tokens.emplace_back(std::format("OS-specific {:02X}", (flags >> 20) & 0xFF));
  }
  if (flags & 0xF0000000) {
    tokens.emplace_back(std::format("architecture-specific {:02X}", (flags >> 28) & 0x0F));
  }
  if (flags & 0x000FF808) {
    tokens.emplace_back(std::format("unknown {:02X}", flags & 0x000FF808));
  }
  return join(tokens, ", ");
}

void ELFFile::print(
    FILE* stream,
    const multimap<uint32_t, string>* labels,
    bool print_hex_view_for_code,
    bool all_sections_as_code) const {
  fwrite_fmt(stream, "[ELF file: {}]\n", this->filename);
  fwrite_fmt(stream, "  width: {:02X} ({})\n", this->identifier.width, (this->identifier.width == 1) ? "32-bit" : "64-bit");
  fwrite_fmt(stream, "  endianness: {:02X} ({})\n", this->identifier.width, (this->identifier.width == 1) ? "little-endian" : "big-endian");
  fwrite_fmt(stream, "  OS ABI: {:02X} ({})\n", this->identifier.os_abi, name_for_abi(this->identifier.os_abi));
  string version_args_str = format_data_string(this->identifier.version_args, sizeof(this->identifier.version_args));
  fwrite_fmt(stream, "  version arguments: {}\n", version_args_str);
  string type_str = name_for_file_type(this->type);
  fwrite_fmt(stream, "  file type: {:04X} ({})\n", this->type, type_str);
  fwrite_fmt(stream, "  architecture: {:04X} ({})\n", this->architecture, name_for_architecture(this->architecture));
  fwrite_fmt(stream, "  entrypoint: {:08X}\n", this->entrypoint_addr);
  fwrite_fmt(stream, "  flags: {:08X}\n", this->flags);

  for (size_t x = 0; x < this->sections.size(); x++) {
    const auto& sec = this->sections[x];
    fwrite_fmt(stream, "\n[section {} header]\n", x);
    fwrite_fmt(stream, "  name: {}\n", sec.name);
    string sec_type_str = name_for_section_type(sec.type);
    fwrite_fmt(stream, "  type: {:08X} ({})\n", sec.type, sec_type_str);
    string sec_flags_str = string_for_section_flags(sec.flags);
    fwrite_fmt(stream, "  flags: {:08X} ({})\n", sec.flags, sec_flags_str);
    fwrite_fmt(stream, "  virtual address: {:08X}\n", sec.virtual_addr);
    fwrite_fmt(stream, "  file offset: {:08X}\n", sec.offset);
    fwrite_fmt(stream, "  file size: {:08X}\n", sec.physical_size);
    fwrite_fmt(stream, "  linked section number: {:08X}\n", sec.linked_section_num);
    fwrite_fmt(stream, "  information: {:08X}\n", sec.info);
    fwrite_fmt(stream, "  alignment: {:08X}\n", sec.alignment);
    fwrite_fmt(stream, "  contents entry size: {:08X}\n", sec.entry_size);
    if (!sec.data.empty()) {
      if (all_sections_as_code || (sec.flags & 0x00000004)) { // Executable
        string disassembly;
        if (this->architecture == 0x0003) { // X86
          disassembly = X86Emulator::disassemble(sec.data.data(), sec.data.size(), sec.virtual_addr, labels);
        } else if (this->architecture == 0x0004) { // M68K
          disassembly = M68KEmulator::disassemble(sec.data.data(), sec.data.size(), sec.virtual_addr, labels);
        } else if (this->architecture == 0x0014) { // PPC32
          disassembly = PPC32Emulator::disassemble(sec.data.data(), sec.data.size(), sec.virtual_addr, labels);
        }

        if (disassembly.empty()) {
          fwrite_fmt(stream, "[section {:X} data] // Architecture not supported for disassembly\n", x);
          print_data(stream, sec.data, sec.virtual_addr);
        } else {
          fwritex(stream, disassembly);
          if (print_hex_view_for_code) {
            fwrite_fmt(stream, "[section {:X} data] // Architecture not supported for disassembly\n", x);
            print_data(stream, sec.data, sec.virtual_addr);
          }
        }
      } else if (!sec.data.empty()) {
        fwrite_fmt(stream, "[section {:X} data]\n", x);
        print_data(stream, sec.data, sec.virtual_addr);
      }
    }
  }
}

} // namespace ResourceDASM
