#pragma once

#include <inttypes.h>

#include <map>
#include <phosg/Encoding.hh>
#include <memory>
#include <vector>



class DOLFile {
public:
  explicit DOLFile(const char* filename);
  DOLFile(const char* filename, const std::string& data);
  DOLFile(const char* filename, const void* data, size_t size);
  ~DOLFile() = default;

  void print(FILE* stream, const std::multimap<uint32_t, std::string>* labels = nullptr) const;

private:
  void parse(const void* data, size_t size);

  const std::string filename;

  struct Section {
    uint32_t offset;
    uint32_t address;
    std::string data;
    uint8_t section_num;
  };

  std::vector<Section> text_sections;
  std::vector<Section> data_sections;
  uint32_t bss_address;
  uint32_t bss_size;
  uint32_t entrypoint;
};
