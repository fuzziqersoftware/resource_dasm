#pragma once

#include <inttypes.h>

#include <map>
#include <memory>
#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <vector>

#include "../Emulators/MemoryContext.hh"

struct XBEHeader {
  /* 0000 */ be_uint32_t signature; // 'XBEH' (0x58424548)
  /* 0004 */ uint8_t code_signature[0x100];
  /* 0104 */ le_uint32_t base_addr;
  /* 0108 */ le_uint32_t header_size;
  /* 010C */ le_uint32_t image_size;
  /* 0110 */ le_uint32_t image_header_size;
  /* 0114 */ le_uint32_t creation_time;
  /* 0118 */ le_uint32_t certificate_addr;
  /* 011C */ le_uint32_t num_sections;
  /* 0120 */ le_uint32_t section_headers_addr;
  /* 0124 */ le_uint32_t init_flags;
  /* 0128 */ le_uint32_t entrypoint_addr_encoded;
  /* 012C */ le_uint32_t tls_addr;
  /* 0130 */ le_uint32_t stack_size;
  /* 0134 */ le_uint32_t pe_heap_reserve;
  /* 0138 */ le_uint32_t pe_heap_commit;
  /* 013C */ le_uint32_t pe_base_addr;
  /* 0140 */ le_uint32_t pe_size;
  /* 0144 */ le_uint32_t pe_checksum;
  /* 0148 */ le_uint32_t pe_creation_time;
  /* 014C */ le_uint32_t debug_path_addr;
  /* 0150 */ le_uint32_t debug_filename_addr;
  /* 0154 */ le_uint32_t utf16_debug_filename_addr;
  /* 0158 */ le_uint32_t kernel_thunk_table_addr_encoded;
  /* 015C */ le_uint32_t import_directory_addr;
  /* 0160 */ le_uint32_t num_library_versions;
  /* 0164 */ le_uint32_t library_versions_addr;
  /* 0168 */ le_uint32_t kernel_library_version_addr;
  /* 016C */ le_uint32_t xapi_library_version_addr;
  /* 0170 */ le_uint32_t logo_bitmap_addr;
  /* 0174 */ le_uint32_t logo_bitmap_size;
  /* 0178 */ le_uint64_t unknown_a1;
  /* 0180 */ le_uint32_t unknown_a2;
  /* 0184 */
} __attribute__((packed));

class XBEFile {
public:
  explicit XBEFile(const char* filename);
  XBEFile(const char* filename, const std::string& data);
  XBEFile(const char* filename, std::string&& data);
  XBEFile(const char* filename, const void* data, size_t size);
  ~XBEFile() = default;

  bool is_within_addr_range(uint32_t addr, uint32_t size) const;
  uint32_t entrypoint_addr() const;
  uint32_t kernel_thunk_table_addr() const;

  uint32_t load_into(std::shared_ptr<MemoryContext> mem);

  void print(
      FILE* stream,
      const std::multimap<uint32_t, std::string>* labels = nullptr,
      bool print_hex_view_for_code = false) const;

  StringReader read_from_addr(uint32_t addr, uint32_t size) const;

  template <typename T>
  const T& read_from_addr(uint32_t addr) const {
    StringReader r = this->read_from_addr(addr, sizeof(T));
    const T* ret = &r.get<T>();
    return *ret;
  }

  struct Section {
    /* 00 */ le_uint32_t flags;
    /* 04 */ le_uint32_t addr;
    /* 08 */ le_uint32_t size;
    /* 0C */ le_uint32_t file_offset;
    /* 10 */ le_uint32_t file_size;
    /* 14 */ le_uint32_t name_addr;
    /* 18 */ le_uint32_t reference_index;
    /* 1C */ le_uint32_t head_reference_addr;
    /* 20 */ le_uint32_t tail_reference_addr;
    /* 24 */ uint8_t content_sha1[0x14];
    /* 38 */
  } __attribute__((packed));
  std::vector<Section> sections;

private:
  void parse();

  const std::string filename;
  std::string data;
  StringReader r;
  const XBEHeader* header;

  uint32_t base_addr;
};
