#pragma once

#include <stdint.h>

#include <map>
#include <string>

extern const std::map<int64_t, std::string> REGION_NAMES;
extern const std::map<int64_t, std::string> STANDARD_FONT_NAMES;

const char* name_for_region_code(uint16_t region_code);
const char* name_for_font_id(uint16_t font_id);
