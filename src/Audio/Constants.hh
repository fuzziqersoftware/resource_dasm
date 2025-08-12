#pragma once

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <memory>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>
#include <unordered_map>
#include <vector>

namespace ResourceDASM {
namespace Audio {

uint8_t note_for_name(const char* name);
const char* name_for_note(uint8_t note);
double frequency_for_note(uint8_t note);

} // namespace Audio
} // namespace ResourceDASM
