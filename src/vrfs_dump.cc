#include <inttypes.h>
#include <stdint.h>
#include <sys/stat.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

using namespace std;
using namespace phosg;

struct VRFSBlock {
  be_uint32_t type; // 'VRFS'
  uint8_t unknown_a1[0x7C];
} __attribute__((packed));

struct DirectoryBlock {
  be_uint32_t type; // 'dir '
  be_uint32_t num_subdirectories;
  be_uint32_t num_files;
  uint8_t unknown_a2[0x14];
  be_uint16_t name_length;
  // Variable-length fields:
  // char name[name_length];
} __attribute__((packed));

struct FileBlock {
  be_uint32_t type; // 'file'
  be_uint32_t size;
  be_uint32_t unknown_a1;
  be_uint32_t unknown_a2; // Usually '????' (3F3F3F3F)
  uint8_t unknown_a3[0x10];
  be_uint16_t name_length;
  // Variable length fields:
  // char name[name_length];
  // uint8_t data[size];
} __attribute__((packed));

void mkdirx(const string& path, mode_t mode) {
  if (mkdir(path.c_str(), mode) && (errno != EEXIST)) {
    throw runtime_error(std::format(
        "cannot create directory {} ({})", path, errno));
  }
}

void chdirx(const string& path) {
  if (chdir(path.c_str())) {
    throw runtime_error(std::format(
        "cannot switch to directory {} ({})", path, errno));
  }
}

void print_usage() {
  fwrite_fmt(stderr, "Usage: vrfs_dump input-filename [output-dir]\n\n");
}

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    print_usage();
    return 1;
  }

  string input_filename = argv[1];
  string output_dir = (argc > 2) ? argv[2] : (input_filename + ".out");

  mkdirx(output_dir, 0755);
  chdirx(output_dir);

  struct DirectoryStackEntry {
    size_t num_directories_remaining;
    size_t num_files_remaining;

    inline bool done() const {
      return this->num_directories_remaining == 0 && this->num_files_remaining == 0;
    }
  };
  vector<DirectoryStackEntry> dir_stack;
  auto clear_dir_stack = [&]() {
    while (!dir_stack.empty() && dir_stack.back().done()) {
      dir_stack.pop_back();
      chdirx("..");
    }
  };

  string data = load_file(input_filename);
  StringReader r(data);
  while (!r.eof()) {
    switch (r.get_u32b(false)) {
      case 0x56524653: // 'VRFS'
        r.skip(sizeof(VRFSBlock));
        break;
      case 0x64697220: { // 'dir '
        if (!dir_stack.empty()) {
          auto& entry = dir_stack.back();
          if (entry.num_directories_remaining == 0) {
            throw runtime_error("directory block order is incorrect");
          }
          entry.num_directories_remaining--;
        }
        const auto& header = r.get<DirectoryBlock>();
        string name = r.read(header.name_length);
        fwrite_fmt(stderr, "(dir) {} ({} subdirectories, {} files)\n",
            name, header.num_subdirectories, header.num_files);
        dir_stack.emplace_back(DirectoryStackEntry{header.num_subdirectories, header.num_files});
        if (!name.empty()) {
          mkdirx(name, 0755);
          chdirx(name);
        }
        clear_dir_stack();
        break;
      }
      case 0x66696C65: { // 'file'
        if (dir_stack.empty()) {
          throw runtime_error("file outside of any directory");
        } else {
          auto& entry = dir_stack.back();
          entry.num_files_remaining--;
        }
        const auto& header = r.get<FileBlock>();
        string name = r.read(header.name_length);
        save_file(name, r.read(header.size));
        fwrite_fmt(stderr, "(file) {} (0x{:X} bytes)\n", name, header.size);
        clear_dir_stack();
        break;
      }
      default:
        throw runtime_error(std::format("unsupported block type: {:08X}", r.get_u32b(false)));
    }
  }

  return 0;
}
