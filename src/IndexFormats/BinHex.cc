#include "Formats.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

#include "../TextCodecs.hh"

namespace ResourceDASM {

static const std::string BINHEX_ALPHABET = "!\"#$%&\'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr";

static std::array<int8_t, 0x100> generate_decode_map() {
  std::array<int8_t, 0x100> ret;
  for (size_t z = 0; z < 0x100; z++) {
    ret[z] = -1;
  }
  for (size_t z = 0; z < BINHEX_ALPHABET.size(); z++) {
    ret[BINHEX_ALPHABET[z]] = z;
  }
  return ret;
}

static const std::array<int8_t, 0x100> BINHEX_DECODE_MAP = generate_decode_map();

static constexpr bool is_return(uint8_t v) {
  return (v == 0x0A) || (v == 0x0D);
}

static uint16_t checksum(const void* data, size_t size, uint16_t crc = 0) {
  phosg::StringReader r(data, size);
  while (!r.eof()) {
    uint8_t v = r.get_u8();
    for (size_t z = 0; z < 8; z++) {
      bool t = crc & 0x8000;
      crc = (crc << 1) | (v >> 7);
      if (t) {
        crc ^= 0x1021;
      }
      v = (v << 1) & 0xFF;
    }
  }
  return crc;
}

DecodedBinHex parse_binhex(const std::string& data) {
  size_t sentinel_offset = data.find("(This file must be converted with BinHex ");
  if (sentinel_offset == std::string_view::npos) {
    throw std::runtime_error("Input is not BinHex-encoded");
  }
  // The sentinel string must start at the beginning of the file or the beginning of a line
  if ((sentinel_offset == 0) || ((data[sentinel_offset - 1] != '\n') && (data[sentinel_offset - 1] == '\r'))) {
    throw std::runtime_error("Input is not BinHex-encoded");
  }
  size_t start_offset = data.find(')', sentinel_offset + 41);
  if (start_offset == std::string_view::npos) {
    throw std::runtime_error("No newline follows BinHex sentinel");
  }
  phosg::StringReader r(data);
  r.go(start_offset + 1);

  // Skip any return characters after the sentinel text
  while (is_return(r.get_u8(false))) {
    r.skip(1);
  }

  if (r.get_u8() != ':') {
    throw std::runtime_error("BinHex sentinel is not followed by start byte");
  }

  // Decode 6-bit encoding (similar to base64)
  std::string decoded;
  size_t input_char_count = 0;
  uint8_t pending_bits = 0;
  for (;;) {
    uint8_t v = r.get_u8();
    if (is_return(v)) {
      continue;
    } else if (v == ':') {
      break;
    } else if (BINHEX_DECODE_MAP[v] < 0) {
      throw std::runtime_error("Invalid character in BinHex data segment");
    } else if ((input_char_count & 3) == 0) {
      pending_bits = BINHEX_DECODE_MAP[v] << 2;
    } else if ((input_char_count & 3) == 1) {
      decoded.push_back(pending_bits | (BINHEX_DECODE_MAP[v] >> 4));
      pending_bits = BINHEX_DECODE_MAP[v] << 4;
    } else if ((input_char_count & 3) == 2) {
      decoded.push_back(pending_bits | (BINHEX_DECODE_MAP[v] >> 2));
      pending_bits = BINHEX_DECODE_MAP[v] << 6;
    } else {
      decoded.push_back(pending_bits | BINHEX_DECODE_MAP[v]);
      // Don't need to clear pending_bits here since the 0 case above will overwrite it
    }
    input_char_count++;
  }

  // Decode BinHex RLE
  std::string decompressed;
  phosg::StringReader decoded_r(decoded);
  while (!decoded_r.eof()) {
    uint8_t v = decoded_r.get_u8();
    if (v == 0x90) {
      size_t count = decoded_r.get_u8();
      if (count == 0) {
        decompressed.push_back(0x90);
      } else {
        decompressed.resize(decompressed.size() + count - 1, decompressed.back());
      }
    } else {
      decompressed.push_back(v);
    }
  }

  // Parse the header
  phosg::StringReader decompressed_r(decompressed);
  DecodedBinHex ret;
  ret.file_name = decode_mac_roman(decompressed_r.read(decompressed_r.get_u8()), true);
  if (decompressed_r.get_u8() != 0) {
    throw std::runtime_error("Incorrect filename format in decoded header");
  }
  ret.file_type = decompressed_r.get_u32b();
  ret.creator_code = decompressed_r.get_u32b();
  ret.finder_flags = decompressed_r.get_u16b();
  size_t data_fork_size = decompressed_r.get_u32b();
  size_t resource_fork_size = decompressed_r.get_u32b();
  size_t header_checksum_offset = decompressed_r.where();
  uint16_t header_checksum = decompressed_r.get_u16b();
  ret.data_fork = decompressed_r.read(data_fork_size);
  uint16_t data_fork_checksum = decompressed_r.get_u16b();
  ret.resource_fork = decompressed_r.read(resource_fork_size);
  uint16_t resource_fork_checksum = decompressed_r.get_u16b();

  // Check checksums
  uint16_t computed_header_checksum = checksum(
      decompressed_r.pgetv(0, header_checksum_offset), header_checksum_offset);
  computed_header_checksum = checksum("\0\0", 2, computed_header_checksum);
  if (header_checksum != computed_header_checksum) {
    throw std::runtime_error(std::format("Header checksum is incorrect (expected 0x{:04X}, received 0x{:04X})",
        computed_header_checksum, header_checksum));
  }

  uint16_t computed_data_fork_checksum = checksum(ret.data_fork.data(), ret.data_fork.size());
  computed_data_fork_checksum = checksum("\0\0", 2, computed_data_fork_checksum);
  if (data_fork_checksum != computed_data_fork_checksum) {
    throw std::runtime_error(std::format("Data fork checksum is incorrect (expected 0x{:04X}, received 0x{:04X})",
        computed_data_fork_checksum, data_fork_checksum));
  }

  uint16_t computed_resource_fork_checksum = checksum(ret.resource_fork.data(), ret.resource_fork.size());
  computed_resource_fork_checksum = checksum("\0\0", 2, computed_resource_fork_checksum);
  if (resource_fork_checksum != computed_resource_fork_checksum) {
    throw std::runtime_error(std::format("Resource fork checksum is incorrect (expected 0x{:04X}, received 0x{:04X})",
        computed_resource_fork_checksum, resource_fork_checksum));
  }

  return ret;
}

ResourceFile parse_binhex_resource_fork(const std::string& data) {
  auto decoded = parse_binhex(data);
  return parse_resource_fork(decoded.resource_fork);
}

} // namespace ResourceDASM
