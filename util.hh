#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Filesystem.hh>
#include <vector>
#include <string>


std::string escape_quotes(const std::string& s);
std::string first_file_that_exists(const std::vector<std::string>& names);

template <typename T>
std::vector<T> load_direct_file_data(const std::string& filename) {
  auto f = fopen_unique(filename.c_str(), "rb");
  uint64_t num = fstat(f.get()).st_size / sizeof(T);

  std::vector<T> all_info(num);
  fread(all_info.data(), sizeof(T), num, f.get());

  for (auto& e : all_info) {
    e.byteswap();
  }
  return all_info;
}

template <typename T>
T load_direct_file_data_single(const std::string& filename) {
  auto f = fopen_unique(filename.c_str(), "rb");

  T t;
  fread(&t, sizeof(T), 1, f.get());

  t.byteswap();
  return t;
}
