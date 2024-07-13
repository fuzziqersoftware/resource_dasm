#pragma once

#include "ResourceIDs.hh"

#include <stddef.h>
#include <stdint.h>

namespace ResourceDASM {

uint32_t parse_cli_type(const char* str, char end_char = '\0', size_t* num_chars_consumed = nullptr);

// Parses a comma-separate list of resource IDs, where each entry is:
//
//  <id>
//  <min id>..<max id>
//  ~<id>
//  ~<min id>..<max id>
//
// Both <min id> and <max id> are optional and default to -32768 and 32767,
// respectively. The prefix `~` complements the ID(s).
//
void parse_cli_ids(const char* str, ResourceIDs& ids);

// Possible formats of `str` when `ids` is not NULL:
//
//  <type>
//  <type>:<ids>[,<ids>]*
//
uint32_t parse_cli_type_ids(const char* str, ResourceIDs* ids = nullptr);

} // namespace ResourceDASM
