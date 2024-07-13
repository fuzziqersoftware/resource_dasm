#include "XBEFile.hh"

#include <inttypes.h>
#include <string.h>

#include <map>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Hash.hh>
#include <phosg/Strings.hh>
#include <string>

#include "../Emulators/MemoryContext.hh"
#include "../Emulators/X86Emulator.hh"

using namespace std;

namespace ResourceDASM {

XBEFile::XBEFile(const char* filename)
    : filename(filename),
      data(load_file(filename)) {
  this->parse();
}

XBEFile::XBEFile(const char* filename, const std::string& data)
    : filename(filename),
      data(data) {
  this->parse();
}

XBEFile::XBEFile(const char* filename, std::string&& data)
    : filename(filename),
      data(std::move(data)) {
  this->parse();
}

XBEFile::XBEFile(const char* filename, const void* data, size_t size)
    : filename(filename),
      data(reinterpret_cast<const char*>(data), size) {
  this->parse();
}

void XBEFile::parse() {
  this->r = StringReader(this->data);
  this->header = &this->r.pget<XBEHeader>(0);
  if (this->header->signature != 0x58424548) {
    throw runtime_error("not an XBE file");
  }

  this->base_addr = this->header->base_addr;
  auto sections_r = this->read_from_addr(this->header->section_headers_addr, this->header->num_sections * sizeof(Section));
  while (!sections_r.eof()) {
    this->sections.emplace_back(sections_r.get<Section>());
  }
}

bool XBEFile::is_within_addr_range(uint32_t addr, uint32_t size) const {
  return (addr >= this->base_addr) && (addr + size <= this->base_addr + this->data.size());
}

uint32_t XBEFile::entrypoint_addr() const {
  array<uint32_t, 3> keys = {0xE682F45B, 0x94859D4B, 0xA8FC57AB};
  for (uint32_t key : keys) {
    uint32_t candidate = key ^ this->header->entrypoint_addr_encoded;
    if (this->is_within_addr_range(candidate, 4)) {
      return candidate;
    }
  }
  return 0;
}

uint32_t XBEFile::kernel_thunk_table_addr() const {
  array<uint32_t, 3> keys = {0x46437DCD, 0xEFB1F152, 0x5B6D40B6};
  for (uint32_t key : keys) {
    uint32_t candidate = key ^ this->header->kernel_thunk_table_addr_encoded;
    if (this->is_within_addr_range(candidate, 4)) {
      return candidate;
    }
  }
  return 0;
}

uint32_t XBEFile::load_into(std::shared_ptr<MemoryContext> mem) {
  mem->allocate_at(this->base_addr, this->data.size());
  mem->memcpy(this->base_addr, this->data.data(), this->data.size());
  return this->base_addr;
}

void XBEFile::print(
    FILE* stream,
    const std::multimap<uint32_t, std::string>* labels,
    bool print_hex_view_for_code) const {
  fprintf(stream, "[XBE file: %s]\n", this->filename.c_str());
  string code_sig_str = format_data_string(this->header->code_signature, sizeof(this->header->code_signature));
  fprintf(stream, "  code signature: %s\n", code_sig_str.c_str());
  fprintf(stream, "  base_addr: %08" PRIX32 "\n", this->header->base_addr.load());
  fprintf(stream, "  header_size: %08" PRIX32 "\n", this->header->header_size.load());
  fprintf(stream, "  image_size: %08" PRIX32 "\n", this->header->image_size.load());
  fprintf(stream, "  image_header_size: %08" PRIX32 "\n", this->header->image_header_size.load());
  fprintf(stream, "  creation_time: %08" PRIX32 "\n", this->header->creation_time.load());
  fprintf(stream, "  certificate_addr: %08" PRIX32 "\n", this->header->certificate_addr.load());
  fprintf(stream, "  num_sections: %08" PRIX32 "\n", this->header->num_sections.load());
  fprintf(stream, "  section_headers_addr: %08" PRIX32 "\n", this->header->section_headers_addr.load());
  fprintf(stream, "  init_flags: %08" PRIX32 "\n", this->header->init_flags.load());
  fprintf(stream, "  entrypoint_addr: %08" PRIX32 " (decoded: %08" PRIX32 ")\n", this->header->entrypoint_addr_encoded.load(), this->entrypoint_addr());
  fprintf(stream, "  tls_addr: %08" PRIX32 "\n", this->header->tls_addr.load());
  fprintf(stream, "  stack_size: %08" PRIX32 "\n", this->header->stack_size.load());
  fprintf(stream, "  pe_heap_reserve: %08" PRIX32 "\n", this->header->pe_heap_reserve.load());
  fprintf(stream, "  pe_heap_commit: %08" PRIX32 "\n", this->header->pe_heap_commit.load());
  fprintf(stream, "  pe_base_addr: %08" PRIX32 "\n", this->header->pe_base_addr.load());
  fprintf(stream, "  pe_size: %08" PRIX32 "\n", this->header->pe_size.load());
  fprintf(stream, "  pe_checksum: %08" PRIX32 "\n", this->header->pe_checksum.load());
  fprintf(stream, "  pe_creation_time: %08" PRIX32 "\n", this->header->pe_creation_time.load());
  fprintf(stream, "  debug_path_addr: %08" PRIX32 "\n", this->header->debug_path_addr.load());
  fprintf(stream, "  debug_filename_addr: %08" PRIX32 "\n", this->header->debug_filename_addr.load());
  fprintf(stream, "  utf16_debug_filename_addr: %08" PRIX32 "\n", this->header->utf16_debug_filename_addr.load());
  fprintf(stream, "  kernel_thunk_table_addr: %08" PRIX32 " (decoded: %08" PRIX32 ")\n", this->header->kernel_thunk_table_addr_encoded.load(), this->kernel_thunk_table_addr());
  fprintf(stream, "  import_directory_addr: %08" PRIX32 "\n", this->header->import_directory_addr.load());
  fprintf(stream, "  num_library_versions: %08" PRIX32 "\n", this->header->num_library_versions.load());
  fprintf(stream, "  library_versions_addr: %08" PRIX32 "\n", this->header->library_versions_addr.load());
  fprintf(stream, "  kernel_library_version_addr: %08" PRIX32 "\n", this->header->kernel_library_version_addr.load());
  fprintf(stream, "  xapi_library_version_addr: %08" PRIX32 "\n", this->header->xapi_library_version_addr.load());
  fprintf(stream, "  logo_bitmap_addr: %08" PRIX32 "\n", this->header->logo_bitmap_addr.load());
  fprintf(stream, "  logo_bitmap_size: %08" PRIX32 "\n", this->header->logo_bitmap_size.load());
  fprintf(stream, "  unknown_a1: %016" PRIX64 "\n", this->header->unknown_a1.load());
  fprintf(stream, "  unknown_a2: %08" PRIX32 "\n", this->header->unknown_a2.load());

  multimap<uint32_t, string> all_labels;
  all_labels.emplace(this->entrypoint_addr(), "start");
  if (labels) {
    for (const auto& it : *labels) {
      all_labels.emplace(it.first, it.second);
    }
  }

  for (size_t x = 0; x < this->sections.size(); x++) {
    const auto& sec = this->sections[x];
    fprintf(stream, "\n[section %zu header]\n", x);

    const void* sec_data = this->read_from_addr(sec.addr, sec.file_size).getv(sec.file_size);
    auto content_sha1 = sha1(sec_data, sec.file_size);
    bool sha1_correct = (content_sha1.compare(0, content_sha1.size(), reinterpret_cast<const char*>(sec.content_sha1), sizeof(sec.content_sha1)) == 0);

    string name = this->r.pget_cstr(sec.name_addr - this->base_addr);
    fprintf(stream, "  name: %s\n", name.c_str());
    fprintf(stream, "  flags: %08" PRIX32 "\n", sec.flags.load());
    fprintf(stream, "  addr: %08" PRIX32 "\n", sec.addr.load());
    fprintf(stream, "  size: %08" PRIX32 "\n", sec.size.load());
    fprintf(stream, "  file_offset: %08" PRIX32 "\n", sec.file_offset.load());
    fprintf(stream, "  file_size: %08" PRIX32 "\n", sec.file_size.load());
    fprintf(stream, "  reference_index: %08" PRIX32 "\n", sec.reference_index.load());
    fprintf(stream, "  head_reference_addr: %08" PRIX32 "\n", sec.head_reference_addr.load());
    fprintf(stream, "  tail_reference_addr: %08" PRIX32 "\n", sec.tail_reference_addr.load());
    string sha1_str = format_data_string(sec.content_sha1, sizeof(sec.content_sha1));
    if (sha1_correct) {
      fprintf(stream, "  content_sha1: %s (correct)\n", sha1_str.c_str());
    } else {
      string expected_sha1_str = format_data_string(content_sha1);
      fprintf(stream, "  content_sha1: %s (expected %s)\n", sha1_str.c_str(), expected_sha1_str.c_str());
    }

    if (sec.file_size) {
      if (sec.flags & 0x00000004) {
        string disassembly = X86Emulator::disassemble(sec_data, sec.file_size, sec.addr, &all_labels);
        fprintf(stream, "[section %zX disassembly]\n", x);
        fwritex(stream, disassembly);
        if (print_hex_view_for_code) {
          fprintf(stream, "[section %zX data]\n", x);
          print_data(stream, sec_data, sec.file_size, sec.addr);
        }
      } else {
        fprintf(stream, "[section %zX data]\n", x);
        print_data(stream, sec_data, sec.file_size, sec.addr);
      }
    }
  }
}

StringReader XBEFile::read_from_addr(uint32_t addr, uint32_t size) const {
  if (addr >= this->base_addr && addr + size <= this->base_addr + this->header->header_size) {
    return this->r.sub(addr - this->base_addr, size);
  }
  for (const auto& sec : this->sections) {
    if (addr >= sec.addr && addr + size <= sec.addr + sec.file_size) {
      return this->r.sub(addr - sec.addr + sec.file_offset, size);
    }
  }
  throw out_of_range("address not within header or any section");
}

} // namespace ResourceDASM
