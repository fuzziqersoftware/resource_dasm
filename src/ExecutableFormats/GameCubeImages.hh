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

enum GCRegionCode {
  NTSC_J = 0,
  NTSC_U = 1,
  PAL = 2,
  REGION_FREE = 3,
  NTSC_K = 4,
};

struct GCMHeader {
  /* 0000 */ phosg::be_uint32_t game_id = 0;
  /* 0004 */ phosg::be_uint16_t company_id = 0;
  /* 0006 */ uint8_t disc_id = 0;
  /* 0007 */ uint8_t version = 0;
  /* 0008 */ uint8_t audio_streaming = 1;
  /* 0009 */ uint8_t stream_buffer_size = 0;
  /* 000A */ uint8_t unused1[0x0E];
  /* 0018 */ phosg::be_uint32_t wii_magic = 0;
  /* 001C */ phosg::be_uint32_t gc_magic = 0xC2339F3D;
  /* 0020 */ char name[0x60];
  /* 0080 */ uint8_t unknown_a1[0x0380];
  /* 0400 */ phosg::be_uint32_t debug_offset = 0;
  /* 0404 */ phosg::be_uint32_t debug_addr = 0;
  /* 0408 */ uint8_t unused2[0x18];
  /* 0420 */ phosg::be_uint32_t dol_offset = 0;
  /* 0424 */ phosg::be_uint32_t fst_offset = 0;
  /* 0428 */ phosg::be_uint32_t fst_size = 0;
  /* 042C */ phosg::be_uint32_t fst_max_size = 0; // == fst_size for single-disc games
  /* 0430 */ phosg::be_uint32_t unknown_a2[5];
  /* 0444 */ phosg::be_uint32_t memory_size; // == 0x01800000 for GameCube games
  /* 0448 */ phosg::be_uint32_t unknown_a3[4];
  /* 0458 */ phosg::be_uint32_t region_code; // GCRegionCode enum
  /* 045C */ uint8_t unused[0x1FE4];
  /* 2440 */
} __attribute__((packed));
static_assert(sizeof(GCMHeader) == 0x2440);

struct TGCHeader {
  /* 00 */ phosg::be_uint32_t magic = 0;
  /* 04 */ phosg::be_uint32_t unknown1 = 0;
  /* 08 */ phosg::be_uint32_t header_size = 0;
  /* 0C */ phosg::be_uint32_t unknown2 = 0;
  /* 10 */ phosg::be_uint32_t fst_offset = 0;
  /* 14 */ phosg::be_uint32_t fst_size = 0;
  /* 18 */ phosg::be_uint32_t fst_max_size = 0;
  /* 1C */ phosg::be_uint32_t dol_offset = 0;
  /* 20 */ phosg::be_uint32_t dol_size = 0;
  /* 24 */ phosg::be_uint32_t file_area = 0;
  /* 28 */ phosg::be_uint32_t file_area_size = 0;
  /* 2C */ phosg::be_uint32_t banner_offset = 0;
  /* 30 */ phosg::be_uint32_t banner_size = 0;
  /* 34 */ phosg::be_uint32_t file_offset_base = 0;
  /* 38 */ uint8_t unused[0x7FC8] = {0};
} __attribute__((packed));
static_assert(sizeof(TGCHeader) == 0x8000);

struct ApploaderHeader {
  /* 00 */ char date[0x10] = {0};
  /* 10 */ phosg::be_uint32_t entrypoint = 0;
  /* 14 */ phosg::be_uint32_t size = 0;
  /* 18 */ phosg::be_uint32_t trailer_size = 0;
  /* 1C */ phosg::be_uint32_t unknown_a1 = 0;
  /* 20 */
  // Apploader code follows immediately (loaded to 0x81200000)
} __attribute__((packed));
static_assert(sizeof(ApploaderHeader) == 0x20);

struct FSTEntry {
  // There are three types of FST entries: the root entry, directory entries, and file entries. There is only one root
  // entry, and it is always the first entry in the FST. The meanings of some fields are different for each type.

  // The high byte of this field specifies whether the entry is a directory (nonzero) or a file (zero). The low 3 bytes
  // specify an offset into the string table where the file's name begins. (This offset is relative to the start of the
  // string table, which is immediately after the last entry.) This field is ignored (and always zero) for the root
  // entry.
  phosg::be_uint32_t dir_flag_string_offset = 0;

  // For the root entry, this field is unused and should be zero. For directory entries, this is the entry number of
  // the parent directory. For file entries, this is the offset in bytes in the disc image where the file's data begins
  union {
    phosg::be_uint32_t parent_entry_num;
    phosg::be_uint32_t file = 0;
  } __attribute__((packed)) offset;
  // For the root entry, this is the total number of entries in the FST, including the root entry. For directory
  // entries, this is the entry number of the first entry after this one that is NOT within the directory. For file
  // entries, this is the file's size in bytes.
  union {
    phosg::be_uint32_t end_entry_num;
    phosg::be_uint32_t file = 0;
  } __attribute__((packed)) size;

  bool is_dir() const {
    return this->dir_flag_string_offset & 0xFF000000;
  }
  uint32_t string_offset() const {
    return this->dir_flag_string_offset & 0x00FFFFFF;
  }
} __attribute__((packed));
static_assert(sizeof(FSTEntry) == 0x0C);

} // namespace ResourceDASM
