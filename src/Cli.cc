#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>

#include "Cli.hh"


uint32_t parse_cli_type(const char* str, char end_char, size_t* num_chars_consumed) {
  union {
    uint8_t bytes[4];
    be_uint32_t type;
  } dest;
  dest.type = 0x20202020;

  size_t src_offset = 0;
  size_t dest_offset = 0;
  while ((dest_offset < 4) && str[src_offset] && (str[src_offset] != end_char)) {
    if (str[src_offset] == '%') {
      src_offset++;
      uint8_t value = value_for_hex_char(str[src_offset++]) << 4;
      value |= value_for_hex_char(str[src_offset++]);
      dest.bytes[dest_offset++] = value;
    } else {
      dest.bytes[dest_offset++] = str[src_offset++];
    }
  }

  if (num_chars_consumed) {
    *num_chars_consumed = src_offset;
  }

  return dest.type;
}
