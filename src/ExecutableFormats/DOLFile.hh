#pragma once

#include <inttypes.h>

#include <map>
#include <memory>
#include <phosg/Encoding.hh>
#include <vector>

#include "../Emulators/MemoryContext.hh"

namespace ResourceDASM {

class DOLFile {
public:
  struct Header {
    phosg::be_uint32_t text_offset[7];
    phosg::be_uint32_t data_offset[11];
    phosg::be_uint32_t text_address[7];
    phosg::be_uint32_t data_address[11];
    phosg::be_uint32_t text_size[7];
    phosg::be_uint32_t data_size[11];
    phosg::be_uint32_t bss_address;
    phosg::be_uint32_t bss_size;
    phosg::be_uint32_t entrypoint;
    phosg::be_uint32_t unused[7];
  } __attribute__((packed));

  static void check_address_range(uint32_t start, uint32_t size, const char* name);

  explicit DOLFile(const char* filename);
  DOLFile(const char* filename, const std::string& data);
  DOLFile(const char* filename, const void* data, size_t size);
  ~DOLFile() = default;

  void load_into(std::shared_ptr<MemoryContext> mem) const;

  void print(
      FILE* stream,
      const std::multimap<uint32_t, std::string>* labels = nullptr,
      bool print_hex_view_for_code = false,
      bool all_sections_as_code = false) const;

  const std::string filename;

  struct Section {
    uint32_t offset;
    uint32_t address;
    std::string data;
    uint8_t section_num;
    bool is_text;
  };

  std::vector<Section> sections;
  uint32_t bss_address;
  uint32_t bss_size;
  uint32_t entrypoint;

private:
  void parse(const void* data, size_t size);
};

} // namespace ResourceDASM
