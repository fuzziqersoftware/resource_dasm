#pragma once

#include <inttypes.h>

#include <map>
#include <stdexcept>
#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <memory>
#include <vector>

#include "../Emulators/MemoryContext.hh"



// The file begins with an ELFIdentifier immediately followed by an ELFHeader.
// The ELFHeader may have different endianness or widths for some fields, hence
// the split structs here.

struct ELFIdentifier {
  be_uint32_t magic; // '\x7FELF' (0x7F454C46)
  uint8_t width; // 1 = 32-bit, 2 = 64-bit
  uint8_t endianness; // 1 = little-endian, 2 = big-endian
  uint8_t format_version; // 1
  uint8_t os_abi;
  uint8_t version_args[8];
} __attribute__((packed));

template <typename u16t, typename u32t, typename longt>
struct ELFHeader {
  u16t type;
  u16t architecture;
  u32t format_version; // 1
  longt entrypoint_addr;
  longt program_header_offset;
  longt section_header_offset;
  u32t flags;
  u16t header_size; // Size of this struct + the preceding ELFIdentifier
  u16t program_header_entry_size;
  u16t program_header_entry_count;
  u16t section_header_entry_size;
  u16t section_header_entry_count;
  u16t names_section_index;
} __attribute__((packed));

struct ELFHeader32BE : ELFHeader<be_uint16_t, be_uint32_t, be_uint32_t> { };
struct ELFHeader32LE : ELFHeader<le_uint16_t, le_uint32_t, le_uint32_t> { };
struct ELFHeader64BE : ELFHeader<be_uint16_t, be_uint32_t, be_uint64_t> { };
struct ELFHeader64LE : ELFHeader<le_uint16_t, le_uint32_t, le_uint64_t> { };

template <typename u32t>
struct ELFProgramHeaderEntry32 {
  u32t type;
  u32t offset;
  u32t virtual_addr;
  u32t physical_addr;
  u32t physical_size;
  u32t loaded_size;
  u32t flags;
  u32t alignment;
} __attribute__((packed));

struct ELFProgramHeaderEntry32BE : ELFProgramHeaderEntry32<be_uint32_t> { };
struct ELFProgramHeaderEntry32LE : ELFProgramHeaderEntry32<le_uint32_t> { };

template <typename u32t, typename u64t>
struct ELFProgramHeaderEntry64 {
  u32t type;
  u32t flags;
  u64t offset;
  u64t virtual_addr;
  u64t physical_addr;
  u64t physical_size;
  u64t loaded_size;
  u64t alignment;
} __attribute__((packed));

struct ELFProgramHeaderEntry64BE : ELFProgramHeaderEntry64<be_uint32_t, be_uint64_t> { };
struct ELFProgramHeaderEntry64LE : ELFProgramHeaderEntry64<le_uint32_t, le_uint64_t> { };

template <typename u32t, typename longt>
struct ELFSectionHeaderEntry {
  u32t name_offset; // Offset into .shstrtab section
  u32t type;
  longt flags;
  longt virtual_addr;
  longt offset;
  longt physical_size;
  u32t linked_section_num;
  u32t info;
  longt alignment;
  longt entry_size; // Zero if section doesn't contain fixed-size entries
} __attribute__((packed));



class ELFFile {
public:
  explicit ELFFile(const char* filename);
  ELFFile(const char* filename, const std::string& data);
  ELFFile(const char* filename, const void* data, size_t size);
  ~ELFFile() = default;

  void print(
      FILE* stream,
      const std::multimap<uint32_t, std::string>* labels = nullptr,
      bool print_hex_view_for_code = false) const;

private:
  void parse(const void* data, size_t size);

  template <typename u16t, typename u32t, typename longt>
  void parse_t(StringReader& r);

  const std::string filename;

  ELFIdentifier identifier;

  uint16_t type;
  uint16_t architecture;
  uint64_t entrypoint_addr;
  uint32_t flags;

  struct Section {
    std::string name;
    uint32_t type;
    uint64_t flags;
    uint64_t virtual_addr;
    uint64_t offset;
    uint64_t physical_size;
    uint32_t linked_section_num;
    uint32_t info;
    uint64_t alignment;
    uint64_t entry_size;
    std::string data;
  };

  std::vector<Section> sections;

  // TODO: parse program headers too
};
