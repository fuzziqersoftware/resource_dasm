#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>

#include "Cli.hh"

#include <stdexcept>

using namespace std;


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


static int16_t parse_resource_id(const char* str, const char* str_end) {
  char* end;
  long  id = strtol(str, &end, 0);
  if (end != str_end) {
    throw invalid_argument(string_printf("Illegal resource ID '%s'", str));
  }
  if (id < MIN_RES_ID || id > MAX_RES_ID) {
    throw invalid_argument(string_printf("Resource ID %ld is out of range (%d..%d)", id, MIN_RES_ID, MAX_RES_ID));
  }
  return id;
}


uint32_t parse_cli_type_ids(const char* str, ResourceIDs* ids) {
  size_t    num_chars;
  uint32_t  type = parse_cli_type(str, '\0', &num_chars);
  
  if (ids && str[num_chars] == ':') {
    // Parse resource ID range(s)
    ids->reset(false);
    auto ranges = split(str + num_chars + 1, ',');
    if (ranges.size() == 0) {
      throw invalid_argument(string_printf("No resource IDs after '%s'", str));
    }
    for (const string& range : ranges) {
      const char* crange = range.c_str();
      if (auto ddot = range.find(".."); ddot != string::npos) {
        // <min id>..<max id>, where both <min id> and <max id> are optional
        int16_t min = ddot > 0 ? parse_resource_id(crange, crange + ddot) : MIN_RES_ID;
        int16_t max = ddot + 2 < range.size() ? parse_resource_id(crange + ddot + 2, crange + range.size()) : MAX_RES_ID;
        for (int id = min; id <= max; ++id) {
          *ids += id;
        }
      } else {
        *ids += parse_resource_id(crange, crange + range.size());
      }
    }
  } else {
    if (str[num_chars] != '\0') {
      throw invalid_argument(string_printf("Unexpected character after type: '%s'", str));
    }
    if (ids) {
      // No resource ID range(s) = all resource IDs
      ids->reset(true);
    }
  }
  return type;
}
