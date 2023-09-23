#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>

std::string decode_mac_roman(const char* data, size_t size, bool for_filename = false);
std::string decode_mac_roman(const std::string& data, bool for_filename = false);
std::string decode_mac_roman(char data, bool for_filename = false);

std::string string_for_resource_type(uint32_t type, bool for_filename = false);
std::string raw_string_for_resource_type(uint32_t type);

constexpr bool should_escape_mac_roman_filename_char(char ch) {
  return (static_cast<uint8_t>(ch) < 0x20) || (ch == '/') || (ch == ':');
}
