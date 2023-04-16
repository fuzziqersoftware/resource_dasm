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

  for (const auto& sec : this->sections) {
    mem->allocate_at(sec.address, sec.data.size());
    mem->memcpy(sec.address, sec.data.data(), sec.data.size());
  }
  if (this->bss_address && this->bss_size) {
    mem->allocate_at(this->bss_address, this->bss_size);
    mem->memset(this->bss_address, 0, this->bss_size);
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
  fprintf(stream, "[DOL file: %s]\n", this->filename.c_str());
  fprintf(stream, "  BSS section: %08" PRIX32 " in memory, %08" PRIX32 " bytes\n",
      this->bss_address, this->bss_size);
  fprintf(stream, "  entrypoint: %08" PRIX32 "\n", this->entrypoint);
  for (const auto& sec : this->sections) {
    fprintf(stream,
        "  %s section %hhu: %08" PRIX32 " in file, %08" PRIX32 " in memory, %08zX bytes\n",
        sec.is_text ? "text" : "data", sec.section_num, sec.offset, sec.address, sec.data.size());
  }

  fputc('\n', stream);

  multimap<uint32_t, string> effective_labels;
  if (labels) {
    effective_labels = *labels;
  }
  effective_labels.emplace(this->entrypoint, "start");

  for (const auto& sec : this->sections) {
    fprintf(stream, "\n.%s%hhu:\n", sec.is_text ? "text" : "data", sec.section_num);
    if (sec.is_text) {
      string disassembly = PPC32Emulator::disassemble(
          sec.data.data(), sec.data.size(), sec.address, &effective_labels);
      fwritex(stream, disassembly);
      if (print_hex_view_for_code) {
        fprintf(stream, "\n.%s%hhu:\n", sec.is_text ? "text" : "data", sec.section_num);
        print_data(stream, sec.data, sec.address);
      }
    } else {
      print_data(stream, sec.data, sec.address);
    }
  }
}
