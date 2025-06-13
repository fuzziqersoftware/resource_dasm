#include "RELFile.hh"

#include <inttypes.h>
#include <string.h>

#include <map>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>

#include "../Emulators/PPC32Emulator.hh"
#include "DOLFile.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {

const char* RELRelocationInstruction::name_for_type(Type type) {
  switch (type) {
    case Type::NONE:
      return "none";
    case Type::ADDR32:
      return "addr32";
    case Type::ADDR24:
      return "addr24";
    case Type::ADDR16:
      return "addr16";
    case Type::ADDR16L:
      return "addr16l";
    case Type::ADDR16H:
      return "addr16h";
    case Type::ADDR16S:
      return "addr16s";
    case Type::ADDR14:
      return "addr14";
    case Type::ADDR14T:
      return "addr14t";
    case Type::ADDR14N:
      return "addr14n";
    case Type::REL24:
      return "rel24";
    case Type::REL14:
      return "rel14";
    case Type::NOP:
      return "nop";
    case Type::SECTION:
      return "section";
    case Type::STOP:
      return "stop";
    default:
      return "(unknown)";
  }
}

RELFile::RELFile(const char* filename)
    : filename(filename) {
  string data = load_file(filename);
  this->parse(data.data(), data.size());
}

RELFile::RELFile(const char* filename, const string& data)
    : filename(filename) {
  this->parse(data.data(), data.size());
}

RELFile::RELFile(const char* filename, const void* data, size_t size)
    : filename(filename) {
  this->parse(data, size);
}

void RELFile::parse(const void* data, size_t size) {
  StringReader r(data, size);

  this->header = r.get<RELHeader>();

  // Read module name
  if (this->header.module_name_offset) {
    this->name = r.preadx(
        this->header.module_name_offset, this->header.module_name_size);
  }

  // Read section headers and data
  r.go(this->header.section_headers_offset);
  while (this->sections.size() < this->header.num_sections) {
    const auto& sec_header = r.get<RELSectionHeader>();
    auto& sec = this->sections.emplace_back();
    sec.index = this->sections.size() - 1;
    sec.offset = sec_header.offset();
    sec.size = sec_header.size;
    sec.has_code = sec_header.has_code();
    if (sec.offset) {
      sec.data = r.preadx(sec.offset, sec.size);
    }
  }

  // Read import table
  StringReader inst_r = r.subx(0);
  r.go(this->header.import_table_offset);
  while (this->import_table.size() < (this->header.import_table_size / 8)) {
    const auto& import_entry = r.get<RELImportEntry>();
    vector<RELRelocationInstruction> rel_instructions;
    inst_r.go(import_entry.relocations_offset);
    do {
      rel_instructions.emplace_back(inst_r.get<RELRelocationInstruction>());
    } while (rel_instructions.back().type != RELRelocationInstruction::Type::STOP);
    rel_instructions.pop_back(); // Don't include the STOP in the parsed list
    if (!this->import_table.emplace(import_entry.from_module_id, std::move(rel_instructions)).second) {
      throw runtime_error(std::format(
          "multiple import entries for module {:08X}", import_entry.from_module_id));
    }
  }
}

void RELFile::print(
    FILE* stream,
    const multimap<uint32_t, string>* labels,
    bool print_hex_view_for_code,
    bool all_sections_as_code) const {
  fwrite_fmt(stream, "[REL file: {}]\n", this->filename);
  fwrite_fmt(stream, "  module id: {:08X}\n", this->header.module_id);
  if (this->name.empty()) {
    fwrite_fmt(stream, "  internal name missing\n");
  } else {
    fwrite_fmt(stream, "  internal name: {}\n", this->name);
  }
  fwrite_fmt(stream, "  format version: {:08X}\n", this->header.format_version);
  fwrite_fmt(stream, "  BSS size: {:08X}\n", this->header.bss_size);
  fwrite_fmt(stream, "  on_load: {:02X}:{:08X}\n", this->header.on_load_section, this->header.on_load_offset);
  fwrite_fmt(stream, "  on_unload: {:02X}:{:08X}\n", this->header.on_unload_section, this->header.on_unload_offset);
  fwrite_fmt(stream, "  on_missing: {:02X}:{:08X}\n", this->header.on_missing_section, this->header.on_missing_offset);
  if (this->header.format_version > 1) {
    fwrite_fmt(stream, "  alignment: {:08X}\n", this->header.alignment);
    fwrite_fmt(stream, "  BSS alignment: {:08X}\n", this->header.bss_alignment);
    if (this->header.format_version > 2) {
      fwrite_fmt(stream, "  (unknown): {:08X}\n", this->header.unknown_a1);
    }
  }
  fputc('\n', stream);

  multimap<uint32_t, string> effective_labels;
  if (labels) {
    effective_labels = *labels;
  }
  if (this->header.on_load_section) {
    effective_labels.emplace(
        this->sections.at(this->header.on_load_section).offset + this->header.on_load_offset, "on_load");
  }
  if (this->header.on_unload_section) {
    effective_labels.emplace(
        this->sections.at(this->header.on_unload_section).offset + this->header.on_unload_offset, "on_unload");
  }
  if (this->header.on_missing_section) {
    effective_labels.emplace(
        this->sections.at(this->header.on_missing_section).offset + this->header.on_missing_offset, "on_missing");
  }

  for (const auto& imp_it : this->import_table) {
    uint32_t module_id = imp_it.first;
    const auto& instructions = imp_it.second;

    fwrite_fmt(stream, "[Import relocation table for module {:08X}: {} instructions]\n",
        module_id, instructions.size());

    size_t current_section = 0;
    size_t offset = 0;
    for (const auto& inst : instructions) {
      offset += inst.offset;
      const char* type_name = RELRelocationInstruction::name_for_type(inst.type);
      fwrite_fmt(stream, "  ({:02X}:{:08X}) +{:04X} {:02X}:{:08X} {}\n",
          current_section,
          offset,
          inst.offset,
          inst.section_index,
          inst.symbol_offset,
          type_name);
      if (inst.type == RELRelocationInstruction::Type::STOP) {
        throw logic_error("STOP instruction in parsed relocation table");
      } else if (inst.type == RELRelocationInstruction::SECTION) {
        current_section = inst.section_index;
        offset = 0;
      } else if ((inst.type != RELRelocationInstruction::Type::NONE) &&
          (inst.type != RELRelocationInstruction::Type::NOP)) {
        size_t patch_offset = this->sections.at(current_section).offset + offset;
        string label_name = std::format("reloc_mod{:08X}_{:02X}_{:08X}_{}",
            module_id, inst.section_index, inst.offset, type_name);
        effective_labels.emplace(patch_offset, std::move(label_name));
      }
    }
  }
  fputc('\n', stream);

  for (const auto& section : this->sections) {
    fwrite_fmt(stream, "\n[Section {:02X} ({}): {:X} bytes]\n", section.index,
        section.has_code ? "code" : "data", section.size);
    if (!section.data.empty()) {
      if (all_sections_as_code || section.has_code) {
        string disassembly = PPC32Emulator::disassemble(
            section.data.data(), section.data.size(), section.offset, &effective_labels);
        fwritex(stream, disassembly);
        if (print_hex_view_for_code) {
          fwrite_fmt(stream, "\n[Section {:02X} ({}): {:X} bytes]\n", section.index,
              section.has_code ? "code" : "data", section.size);
          print_data(stream, section.data, section.offset);
        }
      } else {
        print_data(stream, section.data, section.offset);
      }
    }
  }
}

} // namespace ResourceDASM
