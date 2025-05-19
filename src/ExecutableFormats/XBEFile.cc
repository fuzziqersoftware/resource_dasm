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
using namespace phosg;

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

void XBEFile::load_into(std::shared_ptr<MemoryContext> mem) const {
  uint32_t min_addr = 0xFFFFFFFF;
  uint32_t max_addr = 0x00000000;
  for (const auto& sec : this->sections) {
    min_addr = min<uint32_t>(min_addr, sec.addr);
    max_addr = max<uint32_t>(max_addr, sec.addr + sec.size);
  }
  mem->preallocate_arena(min_addr, max_addr - min_addr);

  StringReader r(this->data);
  for (const auto& sec : this->sections) {
    r.go(sec.file_offset);
    mem->allocate_at(sec.addr, sec.size);
    mem->memcpy(sec.addr, r.getv(sec.file_size), sec.file_size);
  }
}

void XBEFile::print(
    FILE* stream,
    const std::multimap<uint32_t, std::string>* labels,
    bool print_hex_view_for_code) const {
  fwrite_fmt(stream, "[XBE file: {}]\n", this->filename);
  string code_sig_str = format_data_string(this->header->code_signature, sizeof(this->header->code_signature));
  fwrite_fmt(stream, "  code signature: {}\n", code_sig_str);
  fwrite_fmt(stream, "  base_addr: {:08X}\n", this->header->base_addr);
  fwrite_fmt(stream, "  header_size: {:08X}\n", this->header->header_size);
  fwrite_fmt(stream, "  image_size: {:08X}\n", this->header->image_size);
  fwrite_fmt(stream, "  image_header_size: {:08X}\n", this->header->image_header_size);
  fwrite_fmt(stream, "  creation_time: {:08X}\n", this->header->creation_time);
  fwrite_fmt(stream, "  certificate_addr: {:08X}\n", this->header->certificate_addr);
  fwrite_fmt(stream, "  num_sections: {:08X}\n", this->header->num_sections);
  fwrite_fmt(stream, "  section_headers_addr: {:08X}\n", this->header->section_headers_addr);
  fwrite_fmt(stream, "  init_flags: {:08X}\n", this->header->init_flags);
  fwrite_fmt(stream, "  entrypoint_addr: {:08X} (decoded: {:08X})\n", this->header->entrypoint_addr_encoded, this->entrypoint_addr());
  fwrite_fmt(stream, "  tls_addr: {:08X}\n", this->header->tls_addr);
  fwrite_fmt(stream, "  stack_size: {:08X}\n", this->header->stack_size);
  fwrite_fmt(stream, "  pe_heap_reserve: {:08X}\n", this->header->pe_heap_reserve);
  fwrite_fmt(stream, "  pe_heap_commit: {:08X}\n", this->header->pe_heap_commit);
  fwrite_fmt(stream, "  pe_base_addr: {:08X}\n", this->header->pe_base_addr);
  fwrite_fmt(stream, "  pe_size: {:08X}\n", this->header->pe_size);
  fwrite_fmt(stream, "  pe_checksum: {:08X}\n", this->header->pe_checksum);
  fwrite_fmt(stream, "  pe_creation_time: {:08X}\n", this->header->pe_creation_time);
  fwrite_fmt(stream, "  debug_path_addr: {:08X}\n", this->header->debug_path_addr);
  fwrite_fmt(stream, "  debug_filename_addr: {:08X}\n", this->header->debug_filename_addr);
  fwrite_fmt(stream, "  utf16_debug_filename_addr: {:08X}\n", this->header->utf16_debug_filename_addr);
  fwrite_fmt(stream, "  kernel_thunk_table_addr: {:08X} (decoded: {:08X})\n", this->header->kernel_thunk_table_addr_encoded, this->kernel_thunk_table_addr());
  fwrite_fmt(stream, "  import_directory_addr: {:08X}\n", this->header->import_directory_addr);
  fwrite_fmt(stream, "  num_library_versions: {:08X}\n", this->header->num_library_versions);
  fwrite_fmt(stream, "  library_versions_addr: {:08X}\n", this->header->library_versions_addr);
  fwrite_fmt(stream, "  kernel_library_version_addr: {:08X}\n", this->header->kernel_library_version_addr);
  fwrite_fmt(stream, "  xapi_library_version_addr: {:08X}\n", this->header->xapi_library_version_addr);
  fwrite_fmt(stream, "  logo_bitmap_addr: {:08X}\n", this->header->logo_bitmap_addr);
  fwrite_fmt(stream, "  logo_bitmap_size: {:08X}\n", this->header->logo_bitmap_size);
  fwrite_fmt(stream, "  unknown_a1: {:016X}\n", this->header->unknown_a1);
  fwrite_fmt(stream, "  unknown_a2: {:08X}\n", this->header->unknown_a2);

  multimap<uint32_t, string> all_labels;
  all_labels.emplace(this->entrypoint_addr(), "start");
  if (labels) {
    for (const auto& it : *labels) {
      all_labels.emplace(it.first, it.second);
    }
  }

  for (size_t x = 0; x < this->sections.size(); x++) {
    const auto& sec = this->sections[x];
    fwrite_fmt(stream, "\n[section {} header]\n", x);

    const void* sec_data = this->read_from_addr(sec.addr, sec.file_size).getv(sec.file_size);
    auto content_sha1 = SHA1(sec_data, sec.file_size).bin();
    bool sha1_correct = (content_sha1.compare(0, content_sha1.size(), reinterpret_cast<const char*>(sec.content_sha1), sizeof(sec.content_sha1)) == 0);

    string name = this->r.pget_cstr(sec.name_addr - this->base_addr);
    fwrite_fmt(stream, "  name: {}\n", name);
    fwrite_fmt(stream, "  flags: {:08X}\n", sec.flags);
    fwrite_fmt(stream, "  addr: {:08X}\n", sec.addr);
    fwrite_fmt(stream, "  size: {:08X}\n", sec.size);
    fwrite_fmt(stream, "  file_offset: {:08X}\n", sec.file_offset);
    fwrite_fmt(stream, "  file_size: {:08X}\n", sec.file_size);
    fwrite_fmt(stream, "  reference_index: {:08X}\n", sec.reference_index);
    fwrite_fmt(stream, "  head_reference_addr: {:08X}\n", sec.head_reference_addr);
    fwrite_fmt(stream, "  tail_reference_addr: {:08X}\n", sec.tail_reference_addr);
    string sha1_str = format_data_string(sec.content_sha1, sizeof(sec.content_sha1));
    if (sha1_correct) {
      fwrite_fmt(stream, "  content_sha1: {} (correct)\n", sha1_str);
    } else {
      string expected_sha1_str = format_data_string(content_sha1);
      fwrite_fmt(stream, "  content_sha1: {} (expected {})\n", sha1_str, expected_sha1_str);
    }

    if (sec.file_size) {
      if (sec.flags & 0x00000004) {
        string disassembly = X86Emulator::disassemble(sec_data, sec.file_size, sec.addr, &all_labels);
        fwrite_fmt(stream, "[section {:X} disassembly]\n", x);
        fwritex(stream, disassembly);
        if (print_hex_view_for_code) {
          fwrite_fmt(stream, "[section {:X} data]\n", x);
          print_data(stream, sec_data, sec.file_size, sec.addr);
        }
      } else {
        fwrite_fmt(stream, "[section {:X} data]\n", x);
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
