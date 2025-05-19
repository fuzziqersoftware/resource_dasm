#include "DOLFile.hh"

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

using namespace std;
using namespace phosg;

namespace ResourceDASM {

void DOLFile::check_address_range(
    uint32_t start, uint32_t size, const char* name) {
  if (size == 0) {
    return;
  }
  uint32_t end = start + size;
  if ((start < 0x80000000) || (start >= 0x81800000) ||
      (end < 0x80000000) || (end > 0x81800000)) {
    throw runtime_error(string(name) + " out of range");
  }
}

struct DOLHeader {
  be_uint32_t text_offset[7];
  be_uint32_t data_offset[11];
  be_uint32_t text_address[7];
  be_uint32_t data_address[11];
  be_uint32_t text_size[7];
  be_uint32_t data_size[11];
  be_uint32_t bss_address;
  be_uint32_t bss_size;
  be_uint32_t entrypoint;
  be_uint32_t unused[7];
} __attribute__((packed));

DOLFile::DOLFile(const char* filename)
    : filename(filename) {
  string data = load_file(filename);
  this->parse(data.data(), data.size());
}

DOLFile::DOLFile(const char* filename, const string& data)
    : filename(filename) {
  this->parse(data.data(), data.size());
}

DOLFile::DOLFile(const char* filename, const void* data, size_t size)
    : filename(filename) {
  this->parse(data, size);
}

void DOLFile::load_into(shared_ptr<MemoryContext> mem) const {
  uint32_t min_addr = this->bss_address ? this->bss_address : 0xFFFFFFFF;
  uint32_t max_addr = this->bss_address ? (this->bss_address + this->bss_size) : 0;
  for (const auto& sec : this->sections) {
    min_addr = min<uint32_t>(min_addr, sec.address);
    max_addr = max<uint32_t>(max_addr, sec.address + sec.data.size());
  }
  mem->preallocate_arena(min_addr, max_addr - min_addr);

  // Sometimes the BSS overlaps other sections, so we trim it down as needed
  vector<pair<uint32_t, uint32_t>> bss_sections;
  if (this->bss_address && this->bss_size) {
    bss_sections.emplace_back(this->bss_address, this->bss_address + this->bss_size);
  }
  for (const auto& sec : this->sections) {
    uint32_t sec_end = sec.address + sec.data.size();
    for (size_t z = 0; z < bss_sections.size(); z++) {
      uint32_t bss_start = bss_sections[z].first;
      uint32_t bss_end = bss_sections[z].second;
      if (bss_start < sec.address && bss_end > sec_end) {
        bss_sections[z].second = sec.address;
        bss_sections.emplace_back(sec_end, bss_end);
      } else if (bss_start < sec.address && bss_end > sec.address) {
        bss_sections[z].second = sec.address;
      } else if (bss_start < sec_end && bss_end > sec_end) {
        bss_sections[z].first = sec_end;
      }
    }
    mem->allocate_at(sec.address, sec.data.size());
    mem->memcpy(sec.address, sec.data.data(), sec.data.size());
  }
  for (const auto& bss_section : bss_sections) {
    uint32_t bss_start = bss_section.first;
    size_t bss_size = bss_section.second - bss_section.first;
    mem->allocate_at(bss_start, bss_size);
    mem->memset(bss_start, 0, bss_size);
  }
}

void DOLFile::parse(const void* data, size_t size) {
  StringReader r(data, size);

  const auto& header = r.get<DOLHeader>();

  for (size_t x = 0; x < 7; x++) {
    if (header.text_offset[x] && header.text_size[x]) {
      this->check_address_range(
          header.text_address[x], header.text_size[x], "text section");
      auto& sec = this->sections.emplace_back();
      sec.offset = header.text_offset[x];
      sec.address = header.text_address[x];
      sec.data = r.pread(sec.offset, header.text_size[x]);
      sec.section_num = x;
      sec.is_text = true;
    }
  }

  for (size_t x = 0; x < 11; x++) {
    if (header.data_offset[x] && header.data_offset[x]) {
      this->check_address_range(
          header.data_address[x], header.data_size[x], "text section");
      auto& sec = this->sections.emplace_back();
      sec.offset = header.data_offset[x];
      sec.address = header.data_address[x];
      sec.data = r.pread(sec.offset, header.data_size[x]);
      sec.section_num = x;
      sec.is_text = false;
    }
  }

  this->check_address_range(header.bss_address, header.bss_size, "bss section");
  this->bss_address = header.bss_address;
  this->bss_size = header.bss_size;
  this->check_address_range(header.entrypoint, 4, "entrypoint");
  this->entrypoint = header.entrypoint;
}

void DOLFile::print(
    FILE* stream,
    const multimap<uint32_t, string>* labels,
    bool print_hex_view_for_code) const {
  fwrite_fmt(stream, "[DOL file: {}]\n", this->filename);
  uint32_t bss_mem_addr_end = this->bss_address + this->bss_size - 1;
  fwrite_fmt(stream, "  BSS section: {:08X}-{:08X} in memory ({:08X} bytes)\n",
      this->bss_address, bss_mem_addr_end, this->bss_size);
  fwrite_fmt(stream, "  entrypoint: {:08X}\n", this->entrypoint);
  for (const auto& sec : this->sections) {
    uint32_t file_offset_end = sec.offset + sec.data.size() - 1;
    uint32_t mem_addr_end = sec.address + sec.data.size() - 1;
    fwrite_fmt(stream,
        "  {} section {}: {:08X}-{:08X} in file, {:08X}-{:08X} in memory ({:08X} bytes)\n",
        sec.is_text ? "text" : "data", sec.section_num, sec.offset, file_offset_end, sec.address, mem_addr_end, sec.data.size());
  }

  fputc('\n', stream);

  multimap<uint32_t, string> effective_labels;
  if (labels) {
    effective_labels = *labels;
  }
  effective_labels.emplace(this->entrypoint, "start");

  for (const auto& sec : this->sections) {
    fwrite_fmt(stream, "\n.{}{}:\n", sec.is_text ? "text" : "data", sec.section_num);
    if (sec.is_text) {
      string disassembly = PPC32Emulator::disassemble(
          sec.data.data(), sec.data.size(), sec.address, &effective_labels);
      fwritex(stream, disassembly);
      if (print_hex_view_for_code) {
        fwrite_fmt(stream, "\n.{}{}:\n", sec.is_text ? "text" : "data", sec.section_num);
        print_data(stream, sec.data, sec.address);
      }
    } else {
      print_data(stream, sec.data, sec.address);
    }
  }
}

} // namespace ResourceDASM
