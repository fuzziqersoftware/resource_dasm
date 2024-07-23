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
      throw runtime_error(string_printf(
          "multiple import entries for module %08" PRIX32, import_entry.from_module_id.load()));
    }
  }
}

void RELFile::print(
    FILE* stream,
    const multimap<uint32_t, string>* labels,
    bool print_hex_view_for_code) const {
  fprintf(stream, "[REL file: %s]\n", this->filename.c_str());
  fprintf(stream, "  module id: %08" PRIX32 "\n", this->header.module_id.load());
  if (this->name.empty()) {
    fprintf(stream, "  internal name missing\n");
  } else {
    fprintf(stream, "  internal name: %s\n", this->name.c_str());
  }
  fprintf(stream, "  format version: %08" PRIX32 "\n", this->header.format_version.load());
  fprintf(stream, "  BSS size: %08" PRIX32 "\n", this->header.bss_size.load());
  fprintf(stream, "  on_load: %02hhX:%08" PRIX32 "\n",
      this->header.on_load_section, this->header.on_load_offset.load());
  fprintf(stream, "  on_unload: %02hhX:%08" PRIX32 "\n",
      this->header.on_unload_section, this->header.on_unload_offset.load());
  fprintf(stream, "  on_missing: %02hhX:%08" PRIX32 "\n",
      this->header.on_missing_section, this->header.on_missing_offset.load());
  if (this->header.format_version > 1) {
    fprintf(stream, "  alignment: %08" PRIX32 "\n", this->header.alignment.load());
    fprintf(stream, "  BSS alignment: %08" PRIX32 "\n", this->header.bss_alignment.load());
    if (this->header.format_version > 2) {
      fprintf(stream, "  (unknown): %08" PRIX32 "\n", this->header.unknown_a1.load());
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

    fprintf(stream, "[Import relocation table for module %08" PRIX32 ": %zu instructions]\n",
        module_id, instructions.size());

    size_t current_section = 0;
    size_t offset = 0;
    for (const auto& inst : instructions) {
      offset += inst.offset;
      const char* type_name = RELRelocationInstruction::name_for_type(inst.type);
      fprintf(stream, "  (%02zX:%08zX) +%04hX %02hhX:%08" PRIX32 " %s\n",
          current_section,
          offset,
          inst.offset.load(),
          inst.section_index,
          inst.symbol_offset.load(),
          type_name);
      if (inst.type == RELRelocationInstruction::Type::STOP) {
        throw logic_error("STOP instruction in parsed relocation table");
      } else if (inst.type == RELRelocationInstruction::SECTION) {
        current_section = inst.section_index;
        offset = 0;
      } else if ((inst.type != RELRelocationInstruction::Type::NONE) &&
          (inst.type != RELRelocationInstruction::Type::NOP)) {
        size_t patch_offset = this->sections.at(current_section).offset + offset;
        string label_name = string_printf("reloc_mod%08" PRIX32 "_%02hhX_%08" PRIX32 "_%s",
            module_id, inst.section_index, inst.offset.load(), type_name);
        effective_labels.emplace(patch_offset, std::move(label_name));
      }
    }
  }
  fputc('\n', stream);

  for (const auto& section : this->sections) {
    fprintf(stream, "\n[Section %02" PRIX32 " (%s): %" PRIX32 " bytes]\n", section.index,
        section.has_code ? "code" : "data", section.size);
    if (!section.data.empty()) {
      if (section.has_code) {
        string disassembly = PPC32Emulator::disassemble(
            section.data.data(), section.data.size(), section.offset, &effective_labels);
        fwritex(stream, disassembly);
        if (print_hex_view_for_code) {
          fprintf(stream, "\n[Section %02" PRIX32 " (%s): %" PRIX32 " bytes]\n", section.index,
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
