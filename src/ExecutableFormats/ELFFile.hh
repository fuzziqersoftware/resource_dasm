#pragma once

#include <inttypes.h>

#include <map>
#include <memory>
#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
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

template <typename U16T, typename U32T, typename LongT>
struct ELFHeader {
  U16T type;
  U16T architecture;
  U32T format_version; // 1
  LongT entrypoint_addr;
  LongT program_header_offset;
  LongT section_header_offset;
  U32T flags;
  U16T header_size; // Size of this struct + the preceding ELFIdentifier
  U16T program_header_entry_size;
  U16T program_header_entry_count;
  U16T section_header_entry_size;
  U16T section_header_entry_count;
  U16T names_section_index;
} __attribute__((packed));

struct ELFHeader32BE : ELFHeader<be_uint16_t, be_uint32_t, be_uint32_t> {};
struct ELFHeader32LE : ELFHeader<le_uint16_t, le_uint32_t, le_uint32_t> {};
struct ELFHeader64BE : ELFHeader<be_uint16_t, be_uint32_t, be_uint64_t> {};
struct ELFHeader64LE : ELFHeader<le_uint16_t, le_uint32_t, le_uint64_t> {};

template <typename U32T>
struct ELFProgramHeaderEntry32 {
  U32T type;
  U32T offset;
  U32T virtual_addr;
  U32T physical_addr;
  U32T physical_size;
  U32T loaded_size;
  U32T flags;
  U32T alignment;
} __attribute__((packed));

struct ELFProgramHeaderEntry32BE : ELFProgramHeaderEntry32<be_uint32_t> {};
struct ELFProgramHeaderEntry32LE : ELFProgramHeaderEntry32<le_uint32_t> {};

template <typename U32T, typename U64T>
struct ELFProgramHeaderEntry64 {
  U32T type;
  U32T flags;
  U64T offset;
  U64T virtual_addr;
  U64T physical_addr;
  U64T physical_size;
  U64T loaded_size;
  U64T alignment;
} __attribute__((packed));

struct ELFProgramHeaderEntry64BE : ELFProgramHeaderEntry64<be_uint32_t, be_uint64_t> {};
struct ELFProgramHeaderEntry64LE : ELFProgramHeaderEntry64<le_uint32_t, le_uint64_t> {};

template <typename U32T, typename LongT>
struct ELFSectionHeaderEntry {
  U32T name_offset; // Offset into .shstrtab section
  U32T type;
  LongT flags;
  LongT virtual_addr;
  LongT offset;
  LongT physical_size;
  U32T linked_section_num;
  U32T info;
  LongT alignment;
  LongT entry_size; // Zero if section doesn't contain fixed-size entries
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

  template <typename U16T, typename U32T, typename LongT>
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
