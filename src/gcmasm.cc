#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <filesystem>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>
#include <unordered_set>

#include "ExecutableFormats/GameCubeImages.hh"

struct FST {
  std::vector<ResourceDASM::FSTEntry> entries;
  phosg::StringWriter strings;

  size_t add_string(const std::string& s) {
    size_t offset = strings.size();
    strings.write(s);
    strings.put_u8(0);
    return offset;
  }

  size_t bytes() const {
    return this->entries.size() * sizeof(ResourceDASM::FSTEntry) + this->strings.size();
  }

  void write(FILE* f) const {
    phosg::fwritex(f, this->entries.data(), sizeof(ResourceDASM::FSTEntry) * this->entries.size());
    phosg::fwritex(f, this->strings.str());
    while (ftell(f) & 0xFF) {
      fputc(0, f);
    }
  }
};

struct File {
  std::string src_path;
  std::string name;
  size_t image_offset;
  size_t size;

  explicit File(const std::string& src_path)
      : src_path(src_path), name(phosg::basename(this->src_path)), size(std::filesystem::file_size(this->src_path)) {
    phosg::log_info_f("Add file: {} (as {})", this->src_path, this->name);
  }

  std::string data() const {
    return phosg::load_file(this->src_path);
  }
};

struct Directory {
  std::string src_path;
  std::string name;
  std::unordered_map<std::string, std::shared_ptr<Directory>> directories;
  std::unordered_map<std::string, std::shared_ptr<File>> files;

  explicit Directory(const std::string& src_path) : src_path(src_path), name(phosg::basename(this->src_path)) {
    phosg::log_info_f("Add directory: {} (as {})", this->src_path, this->name);
    for (const auto& item : std::filesystem::directory_iterator(src_path)) {
      std::string item_path = src_path + "/" + item.path().filename().string();
      if (std::filesystem::is_directory(item_path)) {
        this->directories.emplace(item.path().filename().string(), new Directory(item_path));
      } else if (std::filesystem::is_regular_file(item_path)) {
        this->files.emplace(item.path().filename().string(), new File(item_path));
      } else {
        throw std::runtime_error("non-file, non-directory object in tree: " + item_path);
      }
    }
    phosg::log_info_f("End directory: {} (as {})", this->src_path, this->name);
  }
};

size_t align(size_t offset, size_t alignment) {
  return (offset + alignment - 1) & ~(alignment - 1);
}

size_t allocate_image_offsets(Directory& dir, size_t min_offset) {
  for (auto& it : dir.directories) {
    min_offset = allocate_image_offsets(*it.second, min_offset);
  }
  for (auto& it : dir.files) {
    // Streaming audio files in particular must be 32 KiB aligned, but we don't attempt to detect those so we align
    // everything to 32 KiB.
    it.second->image_offset = align(min_offset, 0x8000);
    min_offset = it.second->image_offset + it.second->size;
  }
  return min_offset;
}

FST generate_fst(const Directory& root) {
  FST fst;

  std::function<void(const Directory&, int64_t)> add_dir = [&](const Directory& dir, int64_t parent_entry_num) -> void {
    size_t entry_num = fst.entries.size();
    auto& entry = fst.entries.emplace_back();
    if (parent_entry_num < 0) {
      entry.dir_flag_string_offset = 0x01000000;
      entry.offset.parent_entry_num = 0x00000000;
    } else {
      entry.dir_flag_string_offset = 0x01000000 | fst.add_string(dir.name);
      entry.offset.parent_entry_num = parent_entry_num;
    }
    for (const auto& it : dir.directories) {
      add_dir(*it.second, entry_num);
    }
    for (const auto& it : dir.files) {
      auto& entry = fst.entries.emplace_back();
      entry.dir_flag_string_offset = fst.add_string(it.second->name);
      entry.offset.file = it.second->image_offset;
      entry.size.file = it.second->size;
    }
    // Note: entry is probably a broken reference here because fst.entries has likely been reallocated
    fst.entries[entry_num].size.end_entry_num = fst.entries.size();
  };
  add_dir(root, -1);

  return fst;
}

struct HeaderParams {
  int64_t game_id = -1;
  int32_t company_id = -1;
  int16_t disc_id = -1;
  int16_t version = -1;
  int16_t audio_streaming = -1;
  int16_t stream_buffer_size = -1;
  const char* internal_name = nullptr;
  int64_t region_code = -1;
  bool tgc = false;
};

void compile_image(FILE* out, const std::string& in_path, const HeaderParams& header_params) {
  Directory root_dir(in_path);
  phosg::log_info_f("All files collected");

  auto default_dol_it = root_dir.files.find("default.dol");
  if (default_dol_it == root_dir.files.end()) {
    throw std::runtime_error("default.dol not present in root directory");
  }
  std::shared_ptr<File> default_dol = default_dol_it->second;
  root_dir.files.erase(default_dol_it);
  phosg::log_info_f("default.dol found");

  auto apploader_bin_it = root_dir.files.find("apploader.bin");
  if (apploader_bin_it == root_dir.files.end()) {
    throw std::runtime_error("apploader.bin not present in root directory");
  }
  std::shared_ptr<File> apploader_bin = apploader_bin_it->second;
  root_dir.files.erase(apploader_bin_it);
  phosg::log_info_f("apploader.bin found");

  std::shared_ptr<File> header_bin;
  auto header_bin_it = root_dir.files.find("__gcm_header__.bin");
  if ((header_bin_it != root_dir.files.end()) && (header_bin_it->second->size == 0x2440)) {
    header_bin = header_bin_it->second;
    root_dir.files.erase(header_bin_it);
    phosg::log_info_f("__gcm_header__.bin found");
  }

  size_t apploader_offset = 0x2440;
  size_t default_dol_offset = align(apploader_offset + apploader_bin->size, 0x100);
  size_t file_data_start_offset = align(default_dol_offset + default_dol->size, 0x100);

  size_t fst_offset = align(allocate_image_offsets(root_dir, file_data_start_offset), 0x100);

  auto fst = generate_fst(root_dir);

  {
    size_t file_size = fst_offset + fst.bytes();
    std::string size_str = phosg::format_size(fst_offset + fst.bytes());
    phosg::log_info_f("File size: {} bytes ({})", file_size, size_str);
  }

  std::string header_data;
  if (header_bin) {
    header_data = header_bin->data();
    if (header_data.size() != 0x2440) {
      throw std::runtime_error("__gcm_header__.bin is incorrect size");
    }
  } else {
    header_data.resize(0x2440, '\0');
  }

  ResourceDASM::GCMHeader* header = reinterpret_cast<ResourceDASM::GCMHeader*>(header_data.data());
  if (header_params.game_id >= 0) {
    header->game_id = header_params.game_id;
  }
  if (header_params.company_id >= 0) {
    header->company_id = header_params.company_id;
  }
  if (header_params.disc_id >= 0) {
    header->disc_id = header_params.disc_id;
  }
  if (header_params.version >= 0) {
    header->version = header_params.version;
  }
  if (header_params.audio_streaming >= 0) {
    header->audio_streaming = header_params.audio_streaming;
  } else if (!header_bin) {
    header->audio_streaming = 1;
  }
  if (header_params.stream_buffer_size >= 0) {
    header->stream_buffer_size = header_params.stream_buffer_size;
  }
  if (header_params.internal_name) {
    memset(header->name, 0, sizeof(header->name));
    strcpy(header->name, header_params.internal_name);
  }
  header->dol_offset = default_dol_offset;
  header->fst_offset = fst_offset;
  header->fst_size = fst.bytes();
  header->fst_max_size = header->fst_size; // TODO: Support multi-disc games here
  if (!header_bin) {
    header->memory_size = 0x01800000;
  }
  if (header_params.region_code >= 0) {
    header->region_code = header_params.region_code;
  } else if (!header_bin) {
    header->region_code = 1;
  }

  size_t gcm_offset;
  if (header_params.tgc) {
    gcm_offset = sizeof(ResourceDASM::TGCHeader);

    std::string tgc_header_data;
    tgc_header_data.resize(gcm_offset, '\0');

    ResourceDASM::TGCHeader* tgc_header = reinterpret_cast<ResourceDASM::TGCHeader*>(tgc_header_data.data());
    tgc_header->magic = 0xAE0F38A2;
    tgc_header->header_size = gcm_offset;
    tgc_header->unknown2 = 0x00100000;
    tgc_header->fst_offset = header->fst_offset + gcm_offset;
    tgc_header->fst_size = header->fst_size;
    tgc_header->fst_max_size = header->fst_size;
    tgc_header->dol_offset = header->dol_offset + gcm_offset;
    tgc_header->dol_size = default_dol->size;
    tgc_header->file_area = gcm_offset;
    tgc_header->file_area_size = fst_offset - gcm_offset;
    tgc_header->file_offset_base = 0;

    fseek(out, 0, SEEK_SET);
    phosg::fwritex(out, tgc_header_data);
    phosg::log_info_f("TGC header written");
  } else {
    gcm_offset = 0;
  }

  fseek(out, gcm_offset, SEEK_SET);
  phosg::fwritex(out, header_data);
  phosg::log_info_f("GCM header written");

  fseek(out, apploader_offset + gcm_offset, SEEK_SET);
  phosg::fwritex(out, apploader_bin->data());
  phosg::log_info_f("Apploader written");
  fseek(out, default_dol_offset + gcm_offset, SEEK_SET);
  phosg::fwritex(out, default_dol->data());
  phosg::log_info_f("default.dol written");
  fseek(out, fst_offset + gcm_offset, SEEK_SET);
  fst.write(out);
  phosg::log_info_f("FST written");

  std::function<void(FILE*, const Directory&)> write_files_data = [&](FILE* out, const Directory& dir) -> void {
    for (const auto& it : dir.directories) {
      write_files_data(out, *it.second);
    }
    for (const auto& it : dir.files) {
      fseek(out, it.second->image_offset + gcm_offset, SEEK_SET);
      phosg::fwritex(out, it.second->data());
      phosg::log_info_f("{} written", it.second->name);
    }
  };
  write_files_data(out, root_dir);

  phosg::log_info_f("Complete");
}

void print_usage() {
  phosg::fwrite_fmt(stderr, "\
Usage: gcmasm <directory> [options] [output-filename]\n\
\n\
gcmasm will get the header data from a file named __gcm_header__.bin in the\n\
given directory. If this file is missing, --game-id must be given, and --name\n\
probably should be given.\n\
\n\
Options:\n\
  --game-id=GGGGCC\n\
      Set the 4-byte game ID (GGGG) and 2-byte company ID (CC).\n\
  --disc-id=NUMBER\n\
      Set the disc number for multi-disc games (default 0).\n\
  --version=VERSION\n\
      Set the revision number (default 0).\n\
  --enable-streaming\n\
      Enable audio streaming (default).\n\
  --disable-streaming\n\
      Disable audio streaming.\n\
  --stream-buffer-size=SIZE\n\
      Set stream buffer size (default 0).\n\
  --name=\"NAME\"\n\
      Set internal name.\n\
  --region=REGIONCODE\n\
      Set region code (0=JP, 1=NA, 2=EU, 3=region-free, 4=KR).\n\
  --tgc\n\
      Repack as TGC instead of GCM.\n\
");
}

int main(int argc, char** argv) {
  if (argc < 2) {
    phosg::fwrite_fmt(stderr, "Usage: gcmasm <directory> [options]\n");
    return 1;
  }

  const char* dir_path = nullptr;
  std::string out_path;
  HeaderParams header_params;
  for (int x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "--game-id=", 10)) {
      if (strlen(argv[x]) != 16) {
        throw std::runtime_error("incorrect game ID length");
      }
      header_params.game_id = *reinterpret_cast<const phosg::be_uint32_t*>(&argv[x][10]);
      header_params.company_id = *reinterpret_cast<const phosg::be_uint16_t*>(&argv[x][14]);
    } else if (!strncmp(argv[x], "--disc-id=", 10)) {
      header_params.disc_id = strtoul(&argv[x][10], nullptr, 0);
    } else if (!strncmp(argv[x], "--version=", 10)) {
      header_params.version = strtoul(&argv[x][10], nullptr, 0);
    } else if (!strcmp(argv[x], "--enable-streaming")) {
      header_params.audio_streaming = 1;
    } else if (!strcmp(argv[x], "--disable-streaming")) {
      header_params.audio_streaming = 0;
    } else if (!strncmp(argv[x], "--stream-buffer-size=", 21)) {
      header_params.stream_buffer_size = strtoul(&argv[x][21], nullptr, 0);
    } else if (!strncmp(argv[x], "--name=", 7)) {
      header_params.internal_name = &argv[x][7];
    } else if (!strncmp(argv[x], "--region=", 9)) {
      header_params.region_code = strtoul(&argv[x][9], nullptr, 0);
    } else if (!strncmp(argv[x], "--tgc", 5)) {
      header_params.tgc = true;
    } else if (!dir_path) {
      dir_path = argv[x];
    } else if (out_path.empty()) {
      out_path = argv[x];
    } else {
      throw std::runtime_error(std::format("excess command line argument: {}", argv[x]));
    }
  }

  if (!dir_path) {
    throw std::runtime_error("no directory given");
  }

  if (out_path.empty()) {
    out_path = dir_path;
    while (out_path.ends_with("/")) {
      out_path.resize(out_path.size() - 1);
    }
    out_path += ".gcm";
  }

  auto out = phosg::fopen_unique(out_path, "wb");
  compile_image(out.get(), dir_path, header_params);

  return 0;
}
