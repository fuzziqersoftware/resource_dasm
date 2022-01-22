#include "DOLFile.hh"

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



struct DOLHeader {
  uint32_t text_offset[7];
  uint32_t data_offset[11];
  uint32_t text_address[7];
  uint32_t data_address[11];
  uint32_t text_size[7];
  uint32_t data_size[11];
  uint32_t bss_address;
  uint32_t bss_size;
  uint32_t entrypoint;
  uint32_t unused[7];

  void byteswap() {
    for (size_t x = 0; x < 7; x++) {
      this->text_offset[x] = bswap32(this->text_offset[x]);
      this->text_address[x] = bswap32(this->text_address[x]);
      this->text_size[x] = bswap32(this->text_size[x]);
    }
    for (size_t x = 0; x < 11; x++) {
      this->data_offset[x] = bswap32(this->data_offset[x]);
      this->data_address[x] = bswap32(this->data_address[x]);
      this->data_size[x] = bswap32(this->data_size[x]);
    }
    this->bss_address = bswap32(this->bss_address);
    this->bss_size = bswap32(this->bss_size);
    this->entrypoint = bswap32(this->entrypoint);
  }
};

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

void DOLFile::parse(const void* data, size_t size) {
  StringReader r(data, size);

  DOLHeader header = r.get_sw<DOLHeader>();

  for (size_t x = 0; x < 7; x++) {
    if (header.text_offset[x] && header.text_size[x]) {
      auto& sec = this->text_sections.emplace_back();
      sec.offset = header.text_offset[x];
      sec.address = header.text_address[x];
      sec.data = r.pread(sec.offset, header.text_size[x]);
      sec.section_num = x;
    }
  }

  for (size_t x = 0; x < 11; x++) {
    if (header.data_offset[x] && header.data_offset[x]) {
      auto& sec = this->data_sections.emplace_back();
      sec.offset = header.data_offset[x];
      sec.address = header.data_address[x];
      sec.data = r.pread(sec.offset, header.data_size[x]);
      sec.section_num = x;
    }
  }

  this->bss_address = header.bss_address;
  this->bss_size = header.bss_size;
  this->entrypoint = header.entrypoint;
}

void DOLFile::print(FILE* stream, const multimap<uint32_t, string>* labels) const {
  fprintf(stream, "[DOL file: %s]\n", this->filename.c_str());
  fprintf(stream, "  BSS section: %08" PRIX32 " in memory, %08" PRIX32 " bytes\n",
      this->bss_address, this->bss_size);
  fprintf(stream, "  entrypoint: %08" PRIX32 "\n", this->entrypoint);
  for (const auto& section : text_sections) {
    fprintf(stream,
        "  text section %hhu: %08" PRIX32 " in file, %08" PRIX32 " in memory, %08zX bytes\n",
        section.section_num, section.offset, section.address, section.data.size());
  }
  for (const auto& section : data_sections) {
    fprintf(stream,
        "  data section %hhu: %08" PRIX32 " in file, %08" PRIX32 " in memory, %08zX bytes\n",
        section.section_num, section.offset, section.address, section.data.size());
  }

  fputc('\n', stream);

  multimap<uint32_t, string> effective_labels;
  if (labels) {
    effective_labels = *labels;
  }
  effective_labels.emplace(this->entrypoint, "start");

  for (const auto& section : this->text_sections) {
    fprintf(stream, ".text%hhu:\n", section.section_num);
    string disassembly = PPC32Emulator::disassemble(
        section.data.data(), section.data.size(), section.address, &effective_labels);
    fwritex(stream, disassembly);
    fputc('\n', stream);
  }

  for (const auto& section : this->data_sections) {
    fprintf(stream, ".data%hhu:\n", section.section_num);
    print_data(stream, section.data);
    fputc('\n', stream);
  }
}
