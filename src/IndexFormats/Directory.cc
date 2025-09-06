#include "Formats.hh"

#include <stdint.h>

#include <filesystem>
#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <string>

#include "../ResourceFile.hh"
#include "../TextCodecs.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {

ResourceFile load_resource_file_from_directory(const string& dir_path) {
  ResourceFile ret;
  for (const auto& type_item : std::filesystem::directory_iterator(dir_path)) {
    if (!type_item.is_directory()) {
      continue;
    }

    string type_item_name = type_item.path().filename().string();
    uint32_t type = resource_type_for_raw_string(unescape_hex_bytes_for_filename(type_item_name));

    for (const auto& res_item : std::filesystem::directory_iterator(std::filesystem::path(dir_path) / type_item_name)) {
      if (!res_item.is_regular_file()) {
        continue;
      }

      const auto& ext_it = ResourceFile::raw_filename_extension_for_type.find(type);
      string file_extension = (ext_it != ResourceFile::raw_filename_extension_for_type.end())
          ? std::format(".{}", ext_it->second)
          : ".bin";

      string res_item_name = res_item.path().filename().string();
      if (!res_item_name.ends_with(file_extension)) {
        continue;
      }

      // Filename is eiher like 20.bin (ID only) or 20_Resource_name.bin (ID +
      // name; name has _XX => escaped byte). Trim off the extension first
      res_item_name.resize(res_item_name.size() - file_extension.size());

      size_t offset = 0;
      int32_t res_id = stol(res_item_name, &offset, 10);
      if (res_id < -0x8000 || res_id > 0x7FFF) {
        throw std::runtime_error(std::format("Invalid resource ID: {}/{}.bin", type_item_name, res_item_name));
      }
      if (offset > res_item_name.size()) {
        throw std::runtime_error(std::format("Invalid resource filename (parse error): {}/{}.bin", type_item_name, res_item_name));
      }

      string res_name;
      if (offset < res_item_name.size()) {
        // Has resource name
        if (res_item_name[offset] != '_') {
          throw std::runtime_error(std::format("Invalid resource filename (missing separator): {}/{}.bin", type_item_name, res_item_name));
        }
        res_name = unescape_hex_bytes_for_filename(res_item_name.substr(offset + 1));
      }

      auto res = make_shared<ResourceFile::Resource>();
      res->type = type;
      res->id = res_id;
      res->flags = 0;
      res->name = res_name;
      res->data = phosg::load_file(res_item.path().string());
      // Hack: the PICT file format has 0x200 unused bytes before the actual
      // header, but the resource format omits this field
      if (res->type == RESOURCE_TYPE_PICT) {
        res->data = res->data.substr(0x200);
      }
      ret.add(res);
    }
  }

  return ret;
}

void save_resource_file_to_directory(const ResourceFile& rf, const std::string& dir_path) {
  std::filesystem::path base_path = dir_path;
  // TODO: This is kinda dumb. It'd be nice if we could use a generator to just
  // iterate the shared_ptr<const Resource> objects directly
  for (auto [res_type, res_id] : rf.all_resources()) {
    auto res = rf.get_resource(res_type, res_id);
    string type_item_name = escape_hex_bytes_for_filename(raw_string_for_resource_type(res_type));
    std::filesystem::create_directories(base_path / type_item_name);
    string res_item_name = res->name.empty()
        ? std::format("{}.bin", res->id)
        : std::format("{}_{}.bin", res->id, escape_hex_bytes_for_filename(res->name));
    phosg::save_file((base_path / type_item_name / res_item_name).string(), res->data);
  }
}

} // namespace ResourceDASM
