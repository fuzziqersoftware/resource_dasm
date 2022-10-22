#pragma once

#include "ResourceIDs.hh"

#include <stddef.h>
#include <stdint.h>

uint32_t parse_cli_type(const char* str, char end_char = '\0', size_t* num_chars_consumed = nullptr);

// Possible formats of `str` when `ids` is not NULL:
//
//  <type>
//  <type>:<ids>[,<ids>]*
//
// where <type> is a four character code, and <ids> is:
//
//  <id>
//  <min id>..<max id>
//
// Both <min id> and <max id> are optional and default to -32768 and 32767,
// respectively.
//
uint32_t parse_cli_type_ids(const char* str, ResourceIDs* ids = nullptr);
