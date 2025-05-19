#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>

#include "Cli.hh"

#include <cstring>
#include <stdexcept>

using namespace std;
using namespace phosg;

namespace ResourceDASM {

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
  long id = strtol(str, &end, 0);
  if (end != str_end) {
    throw invalid_argument(std::format("Illegal resource ID '{}'", str));
  }
  if (id < MIN_RES_ID || id > MAX_RES_ID) {
    throw invalid_argument(std::format("Resource ID {} is out of range ({}..{})", id, MIN_RES_ID, MAX_RES_ID));
  }
  return id;
}

void parse_cli_ids(const char* str, ResourceIDs& ids) {
  ResourceIDs excludes(ResourceIDs::Init::NONE);

  ids.reset(ResourceIDs::Init::NONE);
  for (const string& range : split(str, ',')) {
    const char* crange = range.c_str();
    const char* crange_end = crange + range.size();
    ResourceIDs* range_ids = &ids;
    // Tilde at beginning excludes, not includes, IDs
    if (crange[0] == '~') {
      range_ids = &excludes;
      ++crange;
    }
    if (const char* ddot = strstr(crange, "..")) {
      // <min id>..<max id>, where both <min id> and <max id> are optional
      int16_t min = ddot > crange ? parse_resource_id(crange, ddot) : MIN_RES_ID;
      int16_t max = ddot + 2 < crange_end ? parse_resource_id(ddot + 2, crange_end) : MAX_RES_ID;
      for (int id = min; id <= max; ++id) {
        *range_ids += id;
      }
    } else {
      *range_ids += parse_resource_id(crange, crange_end);
    }
  }

  // If there were only exclusions and no inclusions, exclude from the full
  // set of resource IDs
  if (!excludes.empty()) {
    if (ids.empty()) {
      ids.reset(ResourceIDs::Init::ALL);
    }
    ids -= excludes;
  }
  if (ids.empty()) {
    throw invalid_argument(std::format("Empty set of resource IDs '{}'", str));
  }
}

uint32_t parse_cli_type_ids(const char* str, ResourceIDs* ids) {
  size_t num_chars;
  uint32_t type = parse_cli_type(str, '\0', &num_chars);

  if (ids && str[num_chars] == ':') {
    // Parse resource ID range(s)
    if (str[num_chars + 1]) {
      parse_cli_ids(str + num_chars + 1, *ids);
    } else {
      throw invalid_argument(std::format("No resource IDs after '{}'", str));
    }
  } else {
    if (str[num_chars] != '\0') {
      throw invalid_argument(std::format("Unexpected character after type: '{}'", str));
    }
    if (ids) {
      // No resource ID range(s) = all resource IDs
      ids->reset(ResourceIDs::Init::ALL);
    }
  }
  return type;
}

} // namespace ResourceDASM
