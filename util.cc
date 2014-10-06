#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "Image.hh"

#include "realmz_lib.hh"
#include "resource_fork.hh"

using namespace std;

int16_t byteswap16(int16_t a) {
  return ((a >> 8) & 0x00FF) | ((a << 8) & 0xFF00);
}

int32_t byteswap32(int32_t a) {
  return ((a >> 24) & 0x000000FF) |
         ((a >> 8)  & 0x0000FF00) |
         ((a << 8)  & 0x00FF0000) |
         ((a << 24) & 0xFF000000);
}

FILE* fopen_or_throw(const char* filename, const char* mode) {
  FILE* f = fopen(filename, mode);
  if (!f)
    throw runtime_error("can\'t open file " + string(filename));
  return f;
}

uint64_t num_elements_in_file(FILE* f, size_t size) {
  fseek(f, 0, SEEK_END);
  uint64_t num = ftell(f) / size;
  fseek(f, 0, SEEK_SET);
  return num;
}

string string_printf(const char* fmt, ...) {
  char* result = NULL;

  va_list va;
  va_start(va, fmt);
  vasprintf(&result, fmt, va);
  va_end(va);

  if (result == NULL)
    throw bad_alloc();

  string ret(result);
  free(result);
  return ret;
}

string escape_quotes(const string& s) {
  string ret;
  for (size_t x = 0; x < s.size(); x++) {
    char ch = s[x];
    if (ch == '\"')
      ret += "\\\"";
    else if (ch < 0x20 || ch > 0x7E)
      ret += string_printf("\\x%02X", ch);
    else
      ret += ch;
  }
  return ret;
}

string first_file_that_exists(const vector<string>& names) {

  for (const auto& it : names){
    struct stat st;
    if (stat(it.c_str(), &st) == 0)
      return it;
  }

  return "";
}

void save_file(const string& filename, const void* data, size_t size) {
  FILE* f = fopen_or_throw(filename.c_str(), "wb");
  fwrite(data, size, 1, f);
  fclose(f);
}

void save_file(const string& filename, const vector<uint8_t>& data) {
  save_file(filename, data.data(), data.size());
}

void save_file(const string& filename, const string& data) {
  save_file(filename, data.data(), data.size());
}
