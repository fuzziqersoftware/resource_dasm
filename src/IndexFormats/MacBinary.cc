#include "Formats.hh"

#include <stdint.h>

#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <string>

#include "../ResourceFile.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {

uint16_t macbinary_crc16(const void* data, size_t size) {
  StringReader r(data, size);
  uint16_t crc = 0;
  while (!r.eof()) {
    uint16_t ch = r.get_u8() << 8;
    for (size_t z = 0; z < 8; z++) {
      if ((ch ^ crc) & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
      ch <<= 1;
    }
  }
  return crc;
}

constexpr uint32_t MACBINARY3_SIGNATURE = 0x6D42494E; // 'mBIN'

struct MacBinaryHeader {
  /* 00 */ uint8_t legacy_version; // Should be zero for file versions 2 and 3
  /* 01 */ uint8_t filename_length;
  /* 02 */ char filename[0x3F];
  /* 41 */ be_uint32_t file_type;
  /* 45 */ be_uint32_t creator;
  /* 49 */ uint8_t finder_flags_high;
  /* 4A */ uint8_t unused1;
  /* 4B */ be_uint16_t pos_in_window_v;
  /* 4D */ be_uint16_t pos_in_window_h;
  /* 4F */ be_uint16_t folder_id;
  /* 51 */ uint8_t is_protected;
  /* 52 */ uint8_t zero_flag; // All versions of MacBinary expect this to be 0
  /* 53 */ be_uint32_t data_fork_bytes;
  /* 57 */ be_uint32_t resource_fork_bytes;
  /* 5B */ be_uint32_t creation_date;
  /* 5F */ be_uint32_t modified_date;
  /* 63 */ be_uint16_t get_info_comment_length;
  /* 65 */ uint8_t finder_flags_low;
  /* 66 */ be_uint32_t macbinary3_signature; // == MACBINARY3_SIGNATURE ('mBin')
  /* 6A */ uint8_t filename_script;
  /* 6B */ uint8_t extended_finder_flags;
  /* 6C */ uint8_t unused2[8];
  /* 74 */ be_uint32_t total_files_length; // Actually unused
  /* 78 */ be_uint16_t extra_header_bytes;
  /* 7A */ uint8_t upload_program_version;
  /* 7B */ uint8_t min_macbinary_version;
  /* 7C */ be_uint16_t checksum;
  /* 7E */ uint8_t unused3[2];
  /* 80 (end) */

  void assert_valid() const {
    if (this->zero_flag != 0) {
      throw runtime_error("input is not a MacBinary file (zero flag is nonzero)");
    }
    if (this->filename_length > 0x3F) {
      throw runtime_error("input is not a MacBinary file (file name is too long)");
    }
    if (this->data_fork_bytes >= 0x00800000) {
      throw runtime_error("input is not a MacBinary file (data fork is too long)");
    }
    if (this->resource_fork_bytes >= 0x00800000) {
      throw runtime_error("input is not a MacBinary file (resource fork is too long)");
    }
  }

  bool is_v3() const {
    return this->is_v2_or_later() &&
        (this->macbinary3_signature == MACBINARY3_SIGNATURE);
  }
  bool is_v2_or_later() const {
    return this->is_v1_or_later() &&
        (this->legacy_version == 0) &&
        (this->checksum == this->compute_checksum());
  }
  bool is_v1_or_later() const {
    return (this->zero_flag == 0);
  }

  void assert_v1_unused_fields_valid() const {
    if (this->finder_flags_low != 0) {
      throw runtime_error("input is not a MacBinary v1 file (low Finder flags are nonzero)");
    }
    if (this->macbinary3_signature != 0) {
      throw runtime_error("input is not a MacBinary v1 file (v3 signature is nonzero)");
    }
    if (this->filename_script != 0) {
      throw runtime_error("input is not a MacBinary v1 file (file name script is nonzero)");
    }
    if (this->extended_finder_flags != 0) {
      throw runtime_error("input is not a MacBinary v1 file (extended Finder flags are nonzero)");
    }
    if (memcmp(this->unused2, "\0\0\0\0\0\0\0\0", 8)) {
      throw runtime_error("input is not a MacBinary v1 file (unused field is nonzero)");
    }
    if (this->total_files_length != 0) {
      throw runtime_error("input is not a MacBinary v1 file (total files length field is nonzero)");
    }
    if (this->extra_header_bytes != 0) {
      throw runtime_error("input is not a MacBinary v1 file (secondary header length is nonzero)");
    }
    if (this->upload_program_version != 0) {
      throw runtime_error("input is not a MacBinary v1 file (upload program version is nonzero)");
    }
    if (this->min_macbinary_version != 0) {
      throw runtime_error("input is not a MacBinary v1 file (minimum MacBinary version is nonzero)");
    }
    if (this->checksum != 0) {
      throw runtime_error("input is not a MacBinary v1 file (header checksum is nonzero)");
    }
  }

  uint16_t compute_checksum() const {
    return macbinary_crc16(this, offsetof(MacBinaryHeader, checksum));
  }
} __attribute__((packed));

pair<StringReader, ResourceFile> parse_macbinary(const string& data) {
  StringReader r(data);

  const auto& header = r.get<MacBinaryHeader>();

  // First, check some fields that are common to all versions
  header.assert_valid();

  // MacBinary 3: signature is present
  if (!header.is_v2_or_later()) {
    if (header.is_v1_or_later()) {
      header.assert_v1_unused_fields_valid();
    } else {
      throw runtime_error("input is not a MacBinary file");
    }
  }

  // Data blocks always start on an 0x80-byte boundary
  size_t data_fork_offset = ((sizeof(header) + header.extra_header_bytes) + 0x7F) & (~0x7F);
  size_t resource_fork_offset = ((data_fork_offset + header.data_fork_bytes) + 0x7F) & (~0x7F);

  StringReader data_r = r.subx(data_fork_offset, header.data_fork_bytes);
  StringReader resource_r = r.subx(resource_fork_offset, header.resource_fork_bytes);
  return make_pair(data_r, parse_resource_fork(resource_r));
}

ResourceFile parse_macbinary_resource_fork(const string& data) {
  return parse_macbinary(data).second;
}

} // namespace ResourceDASM
