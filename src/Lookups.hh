#pragma once

#include <map>
#include <string>

using namespace std;

extern const map<int64_t, string> REGION_NAMES;
extern const map<int64_t, string> STANDARD_FONT_NAMES;

const char* name_for_region_code(uint16_t region_code);
const char* name_for_font_id(uint16_t font_id);
