#pragma once

#include <string>

using namespace std;



std::string decode_mac_roman(const char* data, size_t size, bool for_filename = false);
std::string decode_mac_roman(const std::string& data, bool for_filename = false);
std::string decode_mac_roman(char data, bool for_filename = false);

std::string string_for_resource_type(uint32_t type, bool for_filename = false);
std::string raw_string_for_resource_type(uint32_t type);

constexpr bool should_escape_mac_roman_filename_char(char ch) {
  return (unsigned(ch) < 0x20) || (ch == '/') || (ch == ':');
}

// TODO: This isn't a text codec; it's more of a data codec... but this
// currently seems like the most appropriate place for this function.
std::string unpack_bits(const void* data, size_t size);
std::string unpack_bits(const std::string& data);
std::string pack_bits(const void* data, size_t size);
std::string pack_bits(const std::string& data);
