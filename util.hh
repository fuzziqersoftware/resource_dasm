#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

using namespace std;


int16_t byteswap16(int16_t a);
int32_t byteswap32(int32_t a);
FILE* fopen_or_throw(const char* fname, const char* mode);
uint64_t num_elements_in_file(FILE* f, size_t size);
string string_printf(const char* fmt, ...);
string escape_quotes(const string& s);
string first_file_that_exists(const vector<string>& names);

void save_file(const string& filename, const void* data, size_t size);
void save_file(const string& filename, const vector<uint8_t>& data);
void save_file(const string& filename, const string& data);

template <typename T>
vector<T> load_direct_file_data(const string& filename) {
  FILE* f = fopen_or_throw(filename.c_str(), "rb");
  uint64_t num = num_elements_in_file(f, sizeof(T));

  vector<T> all_info(num);
  fread(all_info.data(), sizeof(T), num, f);
  fclose(f);

  for (auto& e : all_info)
    e.byteswap();
  return all_info;
}

template <typename T>
T load_direct_file_data_single(const string& filename) {
  FILE* f = fopen_or_throw(filename.c_str(), "rb");

  T t;
  fread(&t, sizeof(T), 1, f);
  fclose(f);

  t.byteswap();
  return t;
}
