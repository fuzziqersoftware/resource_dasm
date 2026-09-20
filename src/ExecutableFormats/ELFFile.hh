#pragma once

#include <inttypes.h>

#include <map>
#include <memory>
#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <vector>

#include "../Emulators/MemoryContext.hh"

namespace ResourceDASM {

template <bool IsBE>
using U16T = std::conditional_t<IsBE, phosg::be_uint16_t, phosg::le_uint16_t>;
template <bool IsBE>
using U32T = std::conditional_t<IsBE, phosg::be_uint32_t, phosg::le_uint32_t>;
template <bool IsBE>
using U64T = std::conditional_t<IsBE, phosg::be_uint64_t, phosg::le_uint64_t>;
template <bool IsBE, bool Is64>
using LongT = std::conditional_t<Is64, U64T<IsBE>, U32T<IsBE>>;

class ELFFile {
public:
  explicit ELFFile(const std::string& filename);
  ELFFile(const std::string& filename, std::string&& data);
  ELFFile(const std::string& filename, const std::string& data);
  ELFFile(const std::string& filename, const void* data, size_t size);
  ~ELFFile() = default;

  void print(
      FILE* stream,
      const std::multimap<uint32_t, std::string>* labels = nullptr,
      bool print_hex_view_for_code = false,
      bool all_sections_as_code = false) const;

  struct SerializeInput {
    struct Segment {
      uint32_t program_type = 1; // Same as ProgramHeaderEntry::type
      uint32_t program_flags = 0; // Same as ProgramHeaderEntry::flags (memory permissions)
      uint32_t section_type = 1; // Same as SectionHeaderEntry::type
      uint64_t section_flags = 1; // Same as SectionHeaderEntry::flags
      uint64_t virtual_addr = 0;
      uint64_t physical_addr = 0;
      uint64_t loaded_size = 0; // If smaller than data.size(), data.size() is used
      uint64_t alignment = 0;
      uint64_t section_entry_size = 0; // Same as SectionHeaderEntry::entry_size
      uint32_t linked_section_num = 0; // Same as SectionHeaderEntry::linked_section_num
      uint32_t section_info = 0; // Same as SectionHeaderEntry::section_info
      std::string name = "";
      std::string data = "";
    };
    bool is_be = false;
    bool is_64 = false;
    uint8_t os_abi = 0;
    uint16_t type = 0; // Same as Header::type
    uint16_t architecture = 0; // Same as Header::architecture
    uint32_t flags = 0; // Same as Header::flags
    uint64_t entrypoint_addr = 0; // Same as Header::entrypoint_addr
    uint8_t segment_file_alignment_bits = 0; // Which power of 2 to align segment data to in the file
    std::vector<Segment> segments = {};
  };
  static std::string serialize(const SerializeInput& inp);
  template <bool IsBE, bool Is64>
  static std::string serialize_t(const SerializeInput& inp);

  // File structures

  template <bool IsBE>
  struct ProgramHeaderEntry32 {
    // Values for type:
    //   0 = PT_NULL (unused)
    //   1 = PT_LOAD (loadable)
    //   2 = PT_DYNAMIC (dynamic linking info)
    //   3 = PT_INTERP (interpreter info)
    //   4 = PT_NOTE
    //   5 = PT_SHLIB (reserved)
    //   6 = PT_PHDR (program headers)
    //   7 = PT_TLS (TLS template)
    /* 00  */ U32T<IsBE> type = 0;
    /* 04  */ U32T<IsBE> offset = 0;
    /* 08  */ U32T<IsBE> virtual_addr = 0;
    /* 0C  */ U32T<IsBE> physical_addr = 0;
    /* 10  */ U32T<IsBE> physical_size = 0;
    /* 14  */ U32T<IsBE> loaded_size = 0;
    /* 18  */ U32T<IsBE> flags = 0; // Bit field; 1 = executable, 2 = writeable, 4 = readable
    /* 1C  */ U32T<IsBE> alignment = 0;
    /* 20  */
  } __attribute__((packed));
  using ProgramHeaderEntry32LE = ProgramHeaderEntry32<false>;
  using ProgramHeaderEntry32BE = ProgramHeaderEntry32<true>;
  static_assert(sizeof(ProgramHeaderEntry32LE) == 0x20, "ProgramHeaderEntry32LE size is incorrect");
  static_assert(sizeof(ProgramHeaderEntry32BE) == 0x20, "ProgramHeaderEntry32BE size is incorrect");

  template <bool IsBE>
  struct ProgramHeaderEntry64 {
    // Fields have the same meanings as in ProgramHeaderEntry32
    /* 00 */ U32T<IsBE> type = 0;
    /* 04 */ U32T<IsBE> flags = 0;
    /* 08 */ U64T<IsBE> offset = 0;
    /* 10 */ U64T<IsBE> virtual_addr = 0;
    /* 18 */ U64T<IsBE> physical_addr = 0;
    /* 20 */ U64T<IsBE> physical_size = 0;
    /* 28 */ U64T<IsBE> loaded_size = 0;
    /* 30 */ U64T<IsBE> alignment = 0;
    /* 38 */
  } __attribute__((packed));
  using ProgramHeaderEntry64LE = ProgramHeaderEntry64<false>;
  using ProgramHeaderEntry64BE = ProgramHeaderEntry64<true>;
  static_assert(sizeof(ProgramHeaderEntry64LE) == 0x38, "ProgramHeaderEntry64LE size is incorrect");
  static_assert(sizeof(ProgramHeaderEntry64BE) == 0x38, "ProgramHeaderEntry64BE size is incorrect");

  template <bool IsBE, bool Is64>
  using ProgramHeaderEntry = std::conditional_t<Is64, ProgramHeaderEntry64<IsBE>, ProgramHeaderEntry32<IsBE>>;

  template <bool IsBE, bool Is64>
  struct SectionHeaderEntry {
    /* 00 / 00 */ U32T<IsBE> name_offset = 0; // Offset into .shstrtab section
    // Values for type:
    //   0x00 = SHT_NULL (unused)
    //   0x01 = SHT_PROGBITS (executable code / initialized data)
    //   0x02 = SHT_SYMTAB (symbol table)
    //   0x03 = SHT_STRTAB (string table)
    //   0x04 = SHT_RELA (relocation data with addends)
    //   0x05 = SHT_HASH (symbol hash table)
    //   0x06 = SHT_DYNAMIC (dynamic linking info)
    //   0x07 = SHT_NOTE
    //   0x08 = SHT_NOBITS (BSS)
    //   0x09 = SHT_REL (relocation data without addends)
    //   0x0A = SHT_SHLIB (reserved)
    //   0x0B = SHT_DYNSYM (dynamic symbol table)
    //   0x0E = SHT_INIT_ARRAY (constructor array)
    //   0x0F = SHT_FINI_ARRAY (destructor array)
    //   0x10 = SHT_PREINIT_ARRAY (pre-constructor array)
    //   0x11 = SHT_GROUP (section group)
    //   0x12 = SHT_SYMTAB_SHNDX (section indices)
    /* 04 / 04 */ U32T<IsBE> type = 0;
    // Flag bits:
    //   0x00000001 = SHF_WRITE (writable)
    //   0x00000002 = SHF_ALLOC (resident in memory during execution)
    //   0x00000004 = SHF_EXECINSTR (executable)
    //   0x00000010 = SHF_MERGE (mergeable)
    //   0x00000020 = SHF_STRINGS (contains C strings)
    //   0x00000040 = SHF_INFO_LINK
    //   0x00000080 = SHF_LINK_ORDER
    //   0x00000100 = SHF_OS_NONCONFORMING (has OS-specific behaviors)
    //   0x00000200 = SHF_GROUP
    //   0x00000400 = SHF_TLS (has TLS data)
    /* 08 / 08 */ LongT<IsBE, Is64> flags = 0;
    /* 0C / 10 */ LongT<IsBE, Is64> virtual_addr = 0;
    /* 10 / 18 */ LongT<IsBE, Is64> offset = 0;
    /* 14 / 20 */ LongT<IsBE, Is64> physical_size = 0;
    /* 18 / 28 */ U32T<IsBE> linked_section_num = 0;
    /* 1C / 2C */ U32T<IsBE> info = 0;
    /* 20 / 30 */ LongT<IsBE, Is64> alignment = 0;
    /* 24 / 38 */ LongT<IsBE, Is64> entry_size = 0; // Zero if section doesn't contain fixed-size entries
    /* 28 / 40 */
  } __attribute__((packed));
  using SectionHeaderEntry32LE = SectionHeaderEntry<false, false>;
  using SectionHeaderEntry32BE = SectionHeaderEntry<true, false>;
  using SectionHeaderEntry64LE = SectionHeaderEntry<false, true>;
  using SectionHeaderEntry64BE = SectionHeaderEntry<true, true>;
  static_assert(sizeof(SectionHeaderEntry32LE) == 0x28, "SectionHeaderEntry32LE size is incorrect");
  static_assert(sizeof(SectionHeaderEntry32BE) == 0x28, "SectionHeaderEntry32BE size is incorrect");
  static_assert(sizeof(SectionHeaderEntry64LE) == 0x40, "SectionHeaderEntry64LE size is incorrect");
  static_assert(sizeof(SectionHeaderEntry64BE) == 0x40, "SectionHeaderEntry64BE size is incorrect");

  // The file begins with an Identifier immediately followed by an Header. The Header may have different widths for
  // some fields, hence the split structs here.
  struct Identifier {
    /* 00 */ phosg::be_uint32_t magic = 0x7F454C46; // '\x7FELF'
    /* 04 */ uint8_t width = 1; // 1 = 32-bit, 2 = 64-bit
    /* 05 */ uint8_t endianness = 1; // 1 = little-endian, 2 = big-endian
    /* 06 */ uint8_t format_version = 1; // 1
    /* 07 */ uint8_t os_abi = 0;
    /* 08 */ uint8_t version_args[8] = {};
    /* 10 */
  } __attribute__((packed));
  static_assert(sizeof(Identifier) == 0x10, "Identifier size is incorrect");

  template <bool IsBE, bool Is64>
  struct Header {
    /* 32 / 64 */
    /* 10 / 10 */ U16T<IsBE> type = 0; // 0 = unknown, 1 = relocatable, 2 = executable, 3 = shared object, 4 = core
    // Values for architecture (there are many more; these are just the most relevant values for this project):
    //   0x00 = unspecified
    //   0x03 = x86
    //   0x04 = M68K
    //   0x14 = PPC32
    //   0x15 = PPC64
    //   0x2A = SuperH
    //   0x3E = AMD64
    /* 12 / 12 */ U16T<IsBE> architecture = 0;
    /* 14 / 14 */ U32T<IsBE> format_version = 1; // 1
    /* 18 / 18 */ LongT<IsBE, Is64> entrypoint_addr = 0;
    /* 1C / 20 */ LongT<IsBE, Is64> program_header_offset = 0;
    /* 20 / 28 */ LongT<IsBE, Is64> section_header_offset = 0;
    /* 24 / 30 */ U32T<IsBE> flags = 0;
    /* 28 / 34 */ U16T<IsBE> header_size = sizeof(Identifier) + sizeof(Header<IsBE, Is64>);
    /* 2A / 36 */ U16T<IsBE> program_header_entry_size = sizeof(ProgramHeaderEntry<IsBE, Is64>);
    /* 2C / 38 */ U16T<IsBE> program_header_entry_count = 0;
    /* 2E / 3A */ U16T<IsBE> section_header_entry_size = sizeof(SectionHeaderEntry<IsBE, Is64>);
    /* 30 / 3C */ U16T<IsBE> section_header_entry_count = 0;
    /* 32 / 3E */ U16T<IsBE> names_section_index = 0;
    /* 34 / 40 */
  } __attribute__((packed));
  struct Header32LE : Header<false, false> {};
  struct Header32BE : Header<true, false> {};
  struct Header64LE : Header<false, true> {};
  struct Header64BE : Header<true, true> {};
  static_assert(sizeof(Identifier) + sizeof(Header32LE) == 0x34, "Header32LE size is incorrect");
  static_assert(sizeof(Identifier) + sizeof(Header32BE) == 0x34, "Header32BE size is incorrect");
  static_assert(sizeof(Identifier) + sizeof(Header64LE) == 0x40, "Header64LE size is incorrect");
  static_assert(sizeof(Identifier) + sizeof(Header64BE) == 0x40, "Header64BE size is incorrect");

  // Parsed structures

  struct Program {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_addr;
    uint64_t physical_addr;
    uint64_t physical_size;
    uint64_t loaded_size;
    uint64_t alignment;
    std::string_view data;
  };
  struct Section {
    std::string_view name;
    uint32_t type;
    uint64_t flags;
    uint64_t virtual_addr;
    uint64_t offset;
    uint64_t physical_size;
    uint32_t linked_section_num;
    uint32_t info;
    uint64_t alignment;
    uint64_t entry_size;
    std::string_view data;
  };

  const std::string filename;
  const std::string data;

  Identifier identifier;

  uint16_t type;
  uint16_t architecture;
  uint64_t entrypoint_addr;
  uint32_t flags;
  std::vector<Program> programs;
  std::vector<Section> sections;

private:
  void parse();

  template <bool IsBE, bool Is64>
  void parse_t(phosg::StringReader& r);
};

} // namespace ResourceDASM
