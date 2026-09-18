#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <phosg/Arguments.hh>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>
#include <unordered_map>
#include <vector>

#include "DataCodecs/Codecs.hh"
#include "TextCodecs.hh"

// Based on description at https://hitmen.c02.at/files/yagcd/yagcd/chap15.html#sec15.3

struct RARCHeader {
  static constexpr uint32_t EXPECTED_SIGNATURE = 0x52415243; // 'RARC'
  phosg::be_uint32_t signature;
  phosg::be_uint32_t file_size; // Total (including this header)
  phosg::be_uint32_t unknown_a1;
  phosg::be_uint32_t data_segment_offset; // Does NOT include this header (add 0x20 to get absolute offset)
  phosg::be_uint32_t data_segment_size1; // TODO: Which of these is actually the data segment size?
  phosg::be_uint32_t data_segment_size2;
  uint8_t unknown_a2[8];
} __attribute__((packed));
static_assert(sizeof(RARCHeader) == 0x20, "RARCHeader size is incorrect");

struct RARCIndexHeader { // Immediately follows RARCHeader
  phosg::be_uint32_t node_count;
  phosg::be_uint32_t nodes_offset;
  phosg::be_uint32_t entry_count;
  phosg::be_uint32_t entries_offset;
  phosg::be_uint32_t string_table_size;
  phosg::be_uint32_t string_table_offset;
  uint8_t unknown_a2[8];
} __attribute__((packed));
static_assert(sizeof(RARCIndexHeader) == 0x20, "RARCIndexHeader size is incorrect");

struct RARCDirectoryNode {
  phosg::be_uint32_t node_type;
  phosg::be_uint32_t filename_offset; // Relative to start of string table
  phosg::be_uint16_t unknown_a1;
  phosg::be_uint16_t file_entry_count;
  phosg::be_uint32_t first_file_entry_index;
} __attribute__((packed));
static_assert(sizeof(RARCDirectoryNode) == 0x10, "RARCDirectoryNode size is incorrect");

struct RARCFileEntry {
  phosg::be_uint16_t file_id;
  phosg::be_uint16_t unknown_a1;
  phosg::be_uint16_t flags; // 0200 = directory, 1100 = file (probably a bitfield; TODO: RE this further)
  phosg::be_uint16_t filename_offset; // Relative to start of string table
  phosg::be_uint32_t data_offset_or_node_index; // File: relative to data segment; directory; index into node table
  phosg::be_uint32_t data_size;
  phosg::be_uint32_t unknown_a3;
} __attribute__((packed));
static_assert(sizeof(RARCFileEntry) == 0x14, "RARCFileEntry size is incorrect");

int main(int argc, char** argv) {
  phosg::Arguments args(argv + 1, argc - 1);
  bool is_szs = args.get<bool>("szs");
  std::string input_filename = args.get<std::string>(0);
  std::string output_dir = args.get<std::string>(1, false);
  args.assert_none_unused();

  if (output_dir.empty()) {
    output_dir = input_filename + ".out";
  }
  std::filesystem::create_directory(output_dir);

  std::string data = phosg::load_file(args.get<std::string>(0));
  if (is_szs) {
    data = ResourceDASM::decompress_Yaz0(data);
  }

  phosg::StringReader root_r(data);
  auto header = root_r.get<RARCHeader>();
  auto index_header = root_r.get<RARCIndexHeader>();
  auto nodes = root_r.pget_span<RARCDirectoryNode>(
      index_header.nodes_offset + sizeof(RARCHeader), index_header.node_count);
  auto entries = root_r.pget_span<RARCFileEntry>(
      index_header.entries_offset + sizeof(RARCHeader), index_header.entry_count);
  auto strings_r = root_r.subx(index_header.string_table_offset + sizeof(RARCHeader), index_header.string_table_size);
  auto data_r = root_r.subx(header.data_segment_offset + sizeof(RARCHeader), header.data_segment_size1);

  std::vector<bool> nodes_used(index_header.node_count, false);
  std::vector<bool> entries_used(index_header.entry_count, false);
  std::function<void(size_t, const std::string&)> extract_node = [&](size_t node_index, const std::string& output_path) -> void {
    if (node_index == 0xFFFFFFFF) {
      return; // This is the parent of the root directory (an invalid node); skip it
    }
    if (node_index >= nodes.size()) {
      throw std::runtime_error(std::format("Invalid node index 0x{:X}", node_index));
    }
    if (nodes_used[node_index]) {
      return;
    }
    nodes_used[node_index] = true;
    const auto& node = nodes[node_index];
    phosg::log_info_f("(Node {}) ... {}", node_index, output_path);
    std::filesystem::create_directory(output_path);
    for (size_t z = 0; z < node.file_entry_count; z++) {
      size_t entry_index = node.first_file_entry_index + z;
      if (entry_index >= entries.size()) {
        throw std::runtime_error("Invalid entry index");
      }
      const auto& entry = entries[entry_index];
      entries_used[entry_index] = true;
      auto path = std::format("{}/{}", output_path, strings_r.pget_cstr(entry.filename_offset));
      if (entry.flags == 0x0200) { // Directory
        extract_node(entry.data_offset_or_node_index, path);
      } else if (entry.flags == 0x1100) { // File
        phosg::log_info_f("(Node {} entry {}) ... {} ({} bytes)", node_index, entry_index, path, entry.data_size);
        phosg::save_file(path, data_r.pgetv(entry.data_offset_or_node_index, entry.data_size), entry.data_size);
      } else { // Unknown
        throw std::runtime_error("Unknown entry type");
      }
    }
  };
  extract_node(0, output_dir);

  for (size_t z = 0; z < nodes_used.size(); z++) {
    if (!nodes_used[z]) {
      phosg::log_warning_f("Node {} was not referenced", z);
    }
  }
  for (size_t z = 0; z < entries_used.size(); z++) {
    if (!entries_used[z]) {
      phosg::log_warning_f("Entry {} was not referenced", z);
    }
  }

  return 0;
}
