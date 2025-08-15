#pragma once

#include <phosg/Strings.hh>
#include <string>
#include <utility>

#include "../ResourceFile.hh"

namespace ResourceDASM {

using namespace phosg;

// AppleSingle-AppleDouble.cc
struct DecodedAppleSingle {
  std::string data_fork;
  ResourceFile resource_fork;
  std::string file_name;
  std::string comment;
  std::string bw_icon;
  std::string color_icon;
  std::string file_info;
  std::string dates;
  std::string finder_info;
  std::string mac_file_info;
  std::string prodos_file_info;
  std::string msdos_file_info;
  std::string afp_short_name;
  std::string afp_file_info;
  std::string afp_directory_id;

  std::string serialize() const;
};
DecodedAppleSingle parse_applesingle_appledouble(StringReader& r);
DecodedAppleSingle parse_applesingle_appledouble(const std::string& data);
ResourceFile parse_applesingle_appledouble_resource_fork(const std::string& data);

// CBag.cc
ResourceFile parse_cbag(const std::string& data);

// DCData.cc
ResourceFile parse_dc_data(const std::string& data);

// Directory.cc
ResourceFile load_resource_file_from_directory(const std::string& dir_path);
void save_resource_file_to_directory(const ResourceFile& rf, const std::string& dir_path);

// HIRF.cc
ResourceFile parse_hirf(const std::string& data);

// MacBinary.cc
std::pair<StringReader, ResourceFile> parse_macbinary(const std::string& data);
ResourceFile parse_macbinary_resource_fork(const std::string& data);

// Mohawk.cc
ResourceFile parse_mohawk(const std::string& data);

// ResourceFork.cc
ResourceFile parse_resource_fork(const std::string& data);
ResourceFile parse_resource_fork(StringReader& data);
std::string serialize_resource_fork(const ResourceFile& rf);

} // namespace ResourceDASM
