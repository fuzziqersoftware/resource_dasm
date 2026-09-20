#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>

namespace ResourceDASM {

std::string decode_mac_roman(const char* data, size_t size, bool for_filename = false);
std::string decode_mac_roman(std::string_view data, bool for_filename = false);
std::string decode_mac_roman(char data, bool for_filename = false);

std::string string_for_resource_type(uint32_t type, bool for_filename = false);
std::string raw_string_for_resource_type(uint32_t type);
uint32_t resource_type_for_raw_string(std::string_view s);

constexpr bool should_escape_mac_roman_filename_char(char ch) {
  return (static_cast<uint8_t>(ch) < 0x20) || (ch == '/') || (ch == ':');
}

std::string escape_hex_bytes_for_filename(std::string_view s);
std::string unescape_hex_bytes_for_filename(std::string_view s);

} // namespace ResourceDASM
