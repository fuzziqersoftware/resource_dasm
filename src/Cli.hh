#pragma once

#include <stddef.h>
#include <stdint.h>

uint32_t parse_cli_type(const char* str, char end_char = '\0', size_t* num_chars_consumed = nullptr);
