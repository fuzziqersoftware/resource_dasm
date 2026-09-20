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

namespace ResourceDASM {

ELFFile::ELFFile(const std::string& filename) : ELFFile(filename, phosg::load_file(filename)) {}
ELFFile::ELFFile(const std::string& filename, std::string&& data) : filename(filename), data(std::move(data)) {
  this->parse();
}
ELFFile::ELFFile(const std::string& filename, const std::string& data) : filename(filename), data(data) {
  this->parse();
}
ELFFile::ELFFile(const std::string& filename, const void* data, size_t size)
    : filename(filename), data(reinterpret_cast<const char*>(data), size) {
  this->parse();
}

void ELFFile::parse() {
  phosg::StringReader r(this->data);
  this->identifier = r.get<Identifier>();
  if (this->identifier.magic != 0x7F454C46) { // '\x7FELF'
    throw std::runtime_error("incorrect signature");
  }

  if (this->identifier.format_version != 1) {
    throw std::runtime_error("unsupported format version");
  }

  if (this->identifier.width == 1) {
    if (this->identifier.endianness == 1) {
      this->parse_t<false, false>(r);
    } else if (this->identifier.endianness == 2) {
      this->parse_t<true, false>(r);
    } else {
      throw std::runtime_error("unsupported endianness");
    }
  } else if (this->identifier.width == 2) {
    if (this->identifier.endianness == 1) {
      this->parse_t<false, true>(r);
    } else if (this->identifier.endianness == 2) {
      this->parse_t<true, true>(r);
    } else {
      throw std::runtime_error("unsupported endianness");
    }
  } else {
    throw std::runtime_error("unsupported field width");
  }
}

template <bool IsBE, bool Is64>
void ELFFile::parse_t(phosg::StringReader& r) {
  const auto& header = r.get<Header<IsBE, Is64>>();
  this->type = header.type;
  this->architecture = header.architecture;
  this->entrypoint_addr = header.entrypoint_addr;
  this->flags = header.flags;

  r.go(header.program_header_offset);
  this->programs.clear();
  while (this->programs.size() < header.program_header_entry_count) {
    const auto& prog_entry = r.get<ProgramHeaderEntry<IsBE, Is64>>();
    auto& prog = this->programs.emplace_back();
    prog.type = prog_entry.type;
    prog.offset = prog_entry.offset;
    prog.virtual_addr = prog_entry.virtual_addr;
    prog.physical_addr = prog_entry.physical_addr;
    prog.physical_size = prog_entry.physical_size;
    prog.loaded_size = prog_entry.loaded_size;
    prog.flags = prog_entry.flags;
    prog.alignment = prog_entry.alignment;
    prog.data = r.pread(prog.offset, prog.physical_size);
  }

  r.go(header.section_header_offset);
  this->sections.clear();
  std::vector<uint32_t> sec_name_offsets;
  while (this->sections.size() < header.section_header_entry_count) {
    const auto& sec_entry = r.get<SectionHeaderEntry<IsBE, Is64>>();
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

  // Get the section names from the names section (if possible)
  try {
    phosg::StringReader names_r(this->sections.at(header.names_section_index).data);
    for (size_t x = 0; x < this->sections.size(); x++) {
      auto& sec = this->sections[x];
      uint32_t name_offset = sec_name_offsets.at(x);
      sec.name = names_r.get_cstr(name_offset);
    }
  } catch (const std::exception&) {
  }
}

static const char* name_for_abi(uint16_t abi) {
  static const std::vector<const char*> names({
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
  } catch (const std::out_of_range&) {
    return "Unknown";
  }
}

static std::string name_for_file_type(uint16_t type) {
  if ((type & 0xFF00) == 0xFE00) {
    return std::format("(OS-specific {:02X})", type & 0xFF);
  }
  if ((type & 0xFF00) == 0xFF00) {
    return std::format("(architecture-specific {:02X})", type & 0xFF);
  }
  static const std::vector<const char*> names({
      /* 00 */ "Unspecified",
      /* 01 */ "Relocatable file",
      /* 02 */ "Executable file",
      /* 03 */ "Shared object",
      /* 04 */ "Core dump",
  });
  try {
    return names.at(type);
  } catch (const std::out_of_range&) {
    return "Unknown";
  }
}

static std::string name_for_section_type(uint32_t type) {
  if ((type & 0xF0000000) == 0x60000000) {
    return std::format("(OS-specific {:08X})", type & 0x0FFFFFFF);
  }
  if ((type & 0xF0000000) == 0x70000000) {
    return std::format("(architecture-specific {:08X})", type & 0x0FFFFFFF);
  }
  static const std::vector<const char*> names({
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
  } catch (const std::out_of_range&) {
    return "Unknown";
  }
}

static const char* name_for_architecture(uint16_t arch) {
  static const std::unordered_map<uint16_t, const char*> names({
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
  } catch (const std::out_of_range&) {
    return "Unknown";
  }
}

static std::string string_for_section_flags(uint32_t flags) {
  std::vector<std::string> tokens;
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
  return phosg::join(tokens, ", ");
}

void ELFFile::print(
    FILE* stream,
    const std::multimap<uint32_t, std::string>* labels,
    bool print_hex_view_for_code,
    bool all_sections_as_code) const {
  phosg::fwrite_fmt(stream, "[ELF file: {}]\n", this->filename);
  phosg::fwrite_fmt(stream, "  width: {:02X} ({})\n",
      this->identifier.width, (this->identifier.width == 1) ? "32-bit" : "64-bit");
  phosg::fwrite_fmt(stream, "  endianness: {:02X} ({})\n",
      this->identifier.width, (this->identifier.width == 1) ? "little-endian" : "big-endian");
  phosg::fwrite_fmt(stream, "  OS ABI: {:02X} ({})\n", this->identifier.os_abi, name_for_abi(this->identifier.os_abi));
  std::string version_args_str = phosg::format_data_string(
      this->identifier.version_args, sizeof(this->identifier.version_args));
  phosg::fwrite_fmt(stream, "  version arguments: {}\n", version_args_str);
  std::string type_str = name_for_file_type(this->type);
  phosg::fwrite_fmt(stream, "  file type: {:04X} ({})\n", this->type, type_str);
  phosg::fwrite_fmt(stream, "  architecture: {:04X} ({})\n", this->architecture, name_for_architecture(this->architecture));
  phosg::fwrite_fmt(stream, "  entrypoint: {:08X}\n", this->entrypoint_addr);
  phosg::fwrite_fmt(stream, "  flags: {:08X}\n", this->flags);

  for (size_t x = 0; x < this->sections.size(); x++) {
    const auto& sec = this->sections[x];
    phosg::fwrite_fmt(stream, "\n[section {} header]\n", x);
    phosg::fwrite_fmt(stream, "  name: {}\n", sec.name);
    phosg::fwrite_fmt(stream, "  type: {:08X} ({})\n", sec.type, name_for_section_type(sec.type));
    phosg::fwrite_fmt(stream, "  flags: {:08X} ({})\n", sec.flags, string_for_section_flags(sec.flags));
    phosg::fwrite_fmt(stream, "  virtual address: {:08X}\n", sec.virtual_addr);
    phosg::fwrite_fmt(stream, "  file offset: {:08X}\n", sec.offset);
    phosg::fwrite_fmt(stream, "  file size: {:08X}\n", sec.physical_size);
    phosg::fwrite_fmt(stream, "  linked section number: {:08X}\n", sec.linked_section_num);
    phosg::fwrite_fmt(stream, "  information: {:08X}\n", sec.info);
    phosg::fwrite_fmt(stream, "  alignment: {:08X}\n", sec.alignment);
    phosg::fwrite_fmt(stream, "  contents entry size: {:08X}\n", sec.entry_size);
    if (!sec.data.empty()) {
      if (all_sections_as_code || (sec.flags & 0x00000004)) { // Executable
        std::string disassembly;
        if (this->architecture == 0x0003) { // X86
          disassembly = X86Emulator::disassemble(sec.data.data(), sec.data.size(), sec.virtual_addr, labels);
        } else if (this->architecture == 0x0004) { // M68K
          disassembly = M68KEmulator::disassemble(sec.data.data(), sec.data.size(), sec.virtual_addr, labels);
        } else if (this->architecture == 0x0014) { // PPC32
          disassembly = PPC32Emulator::disassemble(sec.data.data(), sec.data.size(), sec.virtual_addr, labels);
        }

        if (disassembly.empty()) {
          phosg::fwrite_fmt(stream, "[section {:X} data] // Architecture not supported for disassembly\n", x);
          phosg::print_data(stream, sec.data, sec.virtual_addr);
        } else {
          phosg::fwritex(stream, disassembly);
          if (print_hex_view_for_code) {
            phosg::fwrite_fmt(stream, "[section {:X} data] // Architecture not supported for disassembly\n", x);
            phosg::print_data(stream, sec.data, sec.virtual_addr);
          }
        }
      } else if (!sec.data.empty()) {
        phosg::fwrite_fmt(stream, "[section {:X} data]\n", x);
        phosg::print_data(stream, sec.data, sec.virtual_addr);
      }
    }
  }
}

std::string ELFFile::serialize(const SerializeInput& inp) {
  if (inp.is_be && inp.is_64) {
    return ELFFile::serialize_t<true, true>(inp);
  } else if (inp.is_be) {
    return ELFFile::serialize_t<true, false>(inp);
  } else if (inp.is_64) {
    return ELFFile::serialize_t<false, true>(inp);
  } else {
    return ELFFile::serialize_t<false, false>(inp);
  }
}

template <bool IsBE, bool Is64>
std::string ELFFile::serialize_t(const SerializeInput& inp) {
  size_t alignment_mask = (1 << (inp.segment_file_alignment_bits - 1));
  size_t program_header_offset = sizeof(Identifier) + sizeof(Header<IsBE, Is64>);
  size_t program_header_bytes = sizeof(ProgramHeaderEntry<IsBE, Is64>) * inp.segments.size();
  size_t section_header_offset = program_header_offset + program_header_bytes;
  size_t section_header_bytes = sizeof(SectionHeaderEntry<IsBE, Is64>) * (inp.segments.size() + 1);
  size_t data_start_offset = (section_header_offset + section_header_bytes + alignment_mask) & (~alignment_mask);

  phosg::StringWriter program_headers_w;
  phosg::StringWriter section_headers_w;
  phosg::StringWriter names_section_w;
  phosg::StringWriter data_w;

  names_section_w.write(".shstrtab", 10);

  for (const auto& seg : inp.segments) {
    uint32_t name_offset = 9; // If no name is given, point to the \0 after the .shstrtab name (which is always first)
    if (!seg.name.empty()) {
      name_offset = names_section_w.size();
      names_section_w.write(seg.name);
      names_section_w.put_u8(0);
    }

    data_w.extend_to((data_w.size() + alignment_mask) & (~alignment_mask));

    size_t file_data_offset = data_start_offset + data_w.size();

    // Annoyingly, fields must be specified in declaration order (even though they're keyed by field name) and the 32
    // and 64-bit field orders differ, so we can't just do program_headers_w.put(ProgramHeaderEntry<IsBE, Is64>{...})
    ProgramHeaderEntry<IsBE, Is64> prog_header;
    prog_header.type = seg.program_type,
    prog_header.flags = seg.program_flags,
    prog_header.offset = file_data_offset,
    prog_header.virtual_addr = seg.virtual_addr,
    prog_header.physical_addr = seg.physical_addr,
    prog_header.physical_size = seg.data.size(),
    prog_header.loaded_size = std::max<size_t>(seg.data.size(), seg.loaded_size),
    prog_header.alignment = seg.alignment,
    program_headers_w.put(prog_header);

    section_headers_w.put(SectionHeaderEntry<IsBE, Is64>{
        .name_offset = name_offset,
        .type = seg.section_type,
        .flags = seg.section_flags,
        .virtual_addr = seg.virtual_addr,
        .offset = file_data_offset,
        .physical_size = seg.data.size(),
        .linked_section_num = seg.linked_section_num,
        .info = seg.section_info,
        .alignment = seg.alignment,
        .entry_size = seg.section_entry_size,
    });

    data_w.write(seg.data);
  }

  // Write names section (unlike the above sections, it does not get a program entry)
  {
    data_w.extend_to((data_w.size() + alignment_mask) & (~alignment_mask));
    section_headers_w.put(SectionHeaderEntry<IsBE, Is64>{
        .name_offset = 0, // We wrote ".shstrtab" to names_section_w first, so it's at offset 0
        .type = 0x03, // SHT_STRTAB
        .flags = 0x00000020, // SHF_STRINGS
        .offset = data_start_offset + data_w.size(),
        .physical_size = names_section_w.size(),
    });
    data_w.write(names_section_w.str());
  }

  if (program_headers_w.size() != program_header_bytes) {
    throw std::logic_error("Generated incorrect program header size");
  }
  if (section_headers_w.size() != section_header_bytes) {
    throw std::logic_error("Generated incorrect section header size");
  }

  phosg::StringWriter w;
  w.put(Identifier{.width = Is64 ? 2 : 1, .endianness = IsBE ? 2 : 1, .os_abi = inp.os_abi});
  w.put(Header<IsBE, Is64>{
      .type = inp.type,
      .architecture = inp.architecture,
      .entrypoint_addr = inp.entrypoint_addr,
      .program_header_offset = program_header_offset,
      .section_header_offset = section_header_offset,
      .flags = inp.flags,
      .program_header_entry_count = inp.segments.size(),
      .section_header_entry_count = inp.segments.size() + 1,
      .names_section_index = inp.segments.size(),
  });
  if (w.size() != program_header_offset) {
    throw std::logic_error("Generated incorrect ELF header size");
  }
  w.write(program_headers_w.str());
  if (w.size() != section_header_offset) {
    throw std::logic_error("Generated incorrect ELF header size");
  }
  w.write(section_headers_w.str());
  w.extend_to(data_start_offset);
  w.write(data_w.str());
  return std::move(w.str());
}

} // namespace ResourceDASM
