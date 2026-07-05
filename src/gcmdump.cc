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

#include "ExecutableFormats/DOLFile.hh"
#include "ExecutableFormats/GameCubeImages.hh"

union ImageHeader {
  ResourceDASM::GCMHeader gcm;
  ResourceDASM::TGCHeader tgc;
} __attribute__((packed));

static std::string sanitize_filename(const std::string& name) {
  std::string ret = name;
  for (auto& ch : ret) {
    if (ch < 0x20 || ch > 0x7E) {
      ch = '_';
    }
  }
  return ret;
}

uint32_t dol_file_size(const ResourceDASM::DOLFile::Header* dol) {
  uint32_t x, max_offset = 0;
  for (x = 0; x < 7; x++) {
    uint32_t section_end_offset = dol->text_offset[x] + dol->text_size[x];
    if (section_end_offset > max_offset) {
      max_offset = section_end_offset;
    }
  }
  for (x = 0; x < 11; x++) {
    uint32_t section_end_offset = dol->data_offset[x] + dol->data_size[x];
    if (section_end_offset > max_offset) {
      max_offset = section_end_offset;
    }
  }
  return max_offset;
}

void parse_until(
    FILE* f,
    const ResourceDASM::FSTEntry* fst,
    const char* string_table,
    int start,
    int end,
    int64_t base_offset,
    const std::unordered_set<std::string>& target_filenames) {

  int x;
  std::string pwd = std::filesystem::current_path().string();
  pwd += '/';
  size_t pwd_end = pwd.size();
  for (x = start; x < end; x++) {
    if (fst[x].is_dir()) {
      phosg::fwrite_fmt(stderr, "> entry: {:08X} $ {:08X} {:08X} {:08X} {}{}/\n", x,
          fst[x].dir_flag_string_offset.load(),
          fst[x].offset.file.load(),
          fst[x].size.file.load(),
          pwd,
          &string_table[fst[x].string_offset()]);

      pwd += sanitize_filename(&string_table[fst[x].dir_flag_string_offset & 0x00FFFFFF]);
      std::filesystem::create_directories(pwd);
      std::filesystem::current_path(pwd);
      parse_until(f, fst, string_table, x + 1, fst[x].size.end_entry_num, base_offset, target_filenames);
      pwd.resize(pwd_end);
      std::filesystem::current_path(pwd.c_str());

      x = fst[x].size.end_entry_num - 1;

    } else {
      phosg::fwrite_fmt(stderr, "> entry: {:08X} $ {:08X} {:08X} {:08X} {}{}\n", x,
          fst[x].dir_flag_string_offset.load(),
          fst[x].offset.file.load(),
          fst[x].size.file.load(),
          pwd, &string_table[fst[x].string_offset()]);

      if (target_filenames.empty() ||
          target_filenames.count(&string_table[fst[x].string_offset()])) {
        std::string filename = sanitize_filename(&string_table[fst[x].string_offset()]);
        try {
          fseek(f, fst[x].offset.file + base_offset, SEEK_SET);
          phosg::save_file(filename, phosg::freadx(f, fst[x].size.file));
        } catch (const std::exception& e) {
          phosg::fwrite_fmt(stderr, "!!! failed to write file: {}\n", e.what());
        }
      }
    }
  }
}

enum Format {
  UNKNOWN = 0,
  GCM = 1,
  TGC = 2,
};

int main(int argc, char** argv) {

  if (argc < 2) {
    phosg::fwrite_fmt(stderr, "Usage: {} [--gcm|--tgc] <filename> [files_to_extract]\n", argv[0]);
    return -1;
  }

  Format format = Format::UNKNOWN;
  const char* filename = nullptr;
  std::unordered_set<std::string> target_filenames;
  for (int x = 1; x < argc; x++) {
    if (!strcmp(argv[x], "--gcm")) {
      format = Format::GCM;
    } else if (!strcmp(argv[x], "--tgc")) {
      format = Format::TGC;
    } else if (!filename) {
      filename = argv[x];
    } else {
      target_filenames.emplace(argv[x]);
    }
  }
  if (!filename) {
    phosg::fwrite_fmt(stderr, "no filename given\n");
    return -1;
  }

  auto f = phosg::fopen_unique(filename, "rb");

  std::string header_data(sizeof(ImageHeader), '\0');
  phosg::freadx(f.get(), header_data.data(), header_data.size());
  const ImageHeader* header = reinterpret_cast<const ImageHeader*>(header_data.data());
  if (format == Format::UNKNOWN) {
    if (header->gcm.gc_magic == 0xC2339F3D) {
      format = Format::GCM;
    } else if (header->tgc.magic == 0xAE0F38A2) {
      format = Format::TGC;
    } else {
      phosg::fwrite_fmt(stderr, "can\'t determine archive type of {}\n", filename);
      return -3;
    }
  }

  uint32_t gcm_offset, fst_offset, fst_size, dol_offset;
  int32_t base_offset;
  if (format == Format::GCM) {
    phosg::fwrite_fmt(stderr, "format: gcm ({})\n", header->gcm.name);
    gcm_offset = 0;
    fst_offset = header->gcm.fst_offset;
    fst_size = header->gcm.fst_size;
    base_offset = 0;
    dol_offset = header->gcm.dol_offset;

  } else if (format == Format::TGC) {
    phosg::fwrite_fmt(stderr, "format: tgc\n");
    gcm_offset = header->tgc.header_size;
    fst_offset = header->tgc.fst_offset;
    fst_size = header->tgc.fst_size;
    base_offset = header->tgc.file_area - header->tgc.file_offset_base;
    dol_offset = header->tgc.dol_offset;

  } else {
    phosg::fwrite_fmt(stderr, "can\'t determine format; use one of --tgc or --gcm\n");
    return -3;
  }

  // If there are target filenames and default.dol isn't specified, don't extract it
  if (target_filenames.empty() || target_filenames.count("default.dol")) {
    fseek(f.get(), dol_offset, SEEK_SET);
    std::string dol_data = phosg::freadx(f.get(), sizeof(ResourceDASM::DOLFile::Header));
    uint32_t dol_size = dol_file_size(reinterpret_cast<const ResourceDASM::DOLFile::Header*>(dol_data.data()));

    dol_data += phosg::freadx(f.get(), dol_size - sizeof(ResourceDASM::DOLFile::Header));

    phosg::save_file("default.dol", dol_data);
  }

  if (target_filenames.empty() || target_filenames.count("__gcm_header__.bin")) {
    fseek(f.get(), gcm_offset, SEEK_SET);
    phosg::save_file("__gcm_header__.bin", phosg::freadx(f.get(), 0x2440));
  }

  if (target_filenames.empty() || target_filenames.count("apploader.bin")) {
    fseek(f.get(), gcm_offset + 0x2440, SEEK_SET);
    std::string data = phosg::freadx(f.get(), sizeof(ResourceDASM::ApploaderHeader));
    const auto* header = reinterpret_cast<const ResourceDASM::ApploaderHeader*>(data.data());
    data += phosg::freadx(f.get(), header->size + header->trailer_size);
    phosg::save_file("apploader.bin", data);
  }

  fseek(f.get(), fst_offset, SEEK_SET);
  std::string fst_data = phosg::freadx(f.get(), fst_size);
  const ResourceDASM::FSTEntry* fst = reinterpret_cast<const ResourceDASM::FSTEntry*>(fst_data.data());

  // If there are target filenames and fst.bin isn't specified, don't extract it
  if (target_filenames.empty() || target_filenames.count("fst.bin")) {
    phosg::save_file("fst.bin", fst_data);
  }

  int num_entries = fst[0].size.end_entry_num;
  phosg::fwrite_fmt(stderr, "> root: {:08X} files\n", num_entries);

  char* string_table = (char*)fst + (sizeof(ResourceDASM::FSTEntry) * num_entries);
  parse_until(f.get(), fst, string_table, 1, num_entries, base_offset, target_filenames);

  return 0;
}
