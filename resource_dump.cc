#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <functional>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <unordered_map>
#include <vector>

#include "resource_fork.hh"
#include "util.hh"

using namespace std;



static string output_prefix(const string& out_dir, const string& base_filename,
    uint32_t type, int16_t id) {
  if (base_filename.empty()) {
    return out_dir;
  }

  uint32_t type_sw = bswap32(type);
  return string_printf("%s/%s_%.4s_%d", out_dir.c_str(),
      base_filename.c_str(), (const char*)&type_sw, id);
}

static Image tile_image(const Image& i, size_t tile_x, size_t tile_y) {
  size_t w = i.get_width(), h = i.get_height();
  Image ret(w * tile_x, h * tile_y);
  for (size_t y = 0; y < tile_y; y++) {
    for (size_t x = 0; x < tile_x; x++) {
      ret.blit(i, w * x, h * y, w, h, 0, 0);
    }
  }
  return ret;
}



void write_decoded_image(Image (*fn)(const void*, size_t),
    const string& out_dir, const string& base_filename, const void* data,
    size_t size, uint32_t type, int16_t id) {

  Image img = fn(data, size);

  string decoded_filename = output_prefix(out_dir, base_filename, type, id) + ".bmp";
  img.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());
}

void write_decoded_image_masked(pair<Image, Image> (*fn)(const void*, size_t),
    const string& out_dir, const string& base_filename, const void* data,
    size_t size, uint32_t type, int16_t id) {

  pair<Image, Image> imgs = fn(data, size);

  string decoded_filename = output_prefix(out_dir, base_filename, type, id) + ".bmp";
  imgs.first.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());

  decoded_filename = output_prefix(out_dir, base_filename, type, id) + "_mask.bmp";
  imgs.second.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());
}

void write_decoded_curs(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  auto decoded = ResourceFile::decode_curs(data, size);
  string prefix = output_prefix(out_dir, base_filename, type, id);

  string decoded_filename = prefix + "_mask.bmp";
  decoded.mask.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());

  decoded_filename = string_printf("%s_%" PRIu16 "_%" PRIu16 ".bmp",
      prefix.c_str(), decoded.hotspot_x, decoded.hotspot_y);
  decoded.bitmap.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());
}

void write_decoded_crsr(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  auto decoded = ResourceFile::decode_crsr(data, size);
  string prefix = output_prefix(out_dir, base_filename, type, id);

  string decoded_filename = prefix + "_bitmap.bmp";
  decoded.bitmap.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());

  decoded_filename = prefix + "_mask.bmp";
  decoded.mask.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());

  decoded_filename = string_printf("%s_%" PRIu16 "_%" PRIu16 ".bmp",
      prefix.c_str(), decoded.hotspot_x, decoded.hotspot_y);
  decoded.image.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());
}

void write_decoded_ppat(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  auto decoded = ResourceFile::decode_ppat(data, size);
  string prefix = output_prefix(out_dir, base_filename, type, id);

  string decoded_filename = prefix + ".bmp";
  decoded.first.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());

  Image tiled = tile_image(decoded.first, 8, 8);
  decoded_filename = prefix + "_tiled.bmp";
  tiled.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());

  decoded_filename = prefix + "_bitmap.bmp";
  decoded.second.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());

  tiled = tile_image(decoded.second, 8, 8);
  decoded_filename = prefix + "_bitmap_tiled.bmp";
  tiled.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());
}

void write_decoded_pat(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  Image decoded = ResourceFile::decode_pat(data, size);
  string prefix = output_prefix(out_dir, base_filename, type, id);

  string decoded_filename = string_printf("%s.bmp", prefix.c_str());
  decoded.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());

  Image tiled = tile_image(decoded, 8, 8);
  decoded_filename = string_printf("%s_tiled.bmp", prefix.c_str());
  tiled.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());
}

void write_decoded_patN(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  auto decoded = ResourceFile::decode_patN(data, size);
  string prefix = output_prefix(out_dir, base_filename, type, id);

  for (size_t x = 0; x < decoded.size(); x++) {
    string decoded_filename = string_printf("%s_%zu.bmp", prefix.c_str(), x);
    decoded[x].save(decoded_filename.c_str(), Image::WindowsBitmap);
    fprintf(stderr, "... %s\n", decoded_filename.c_str());

    Image tiled = tile_image(decoded[x], 8, 8);
    decoded_filename = string_printf("%s_%zu_tiled.bmp", prefix.c_str(), x);
    tiled.save(decoded_filename.c_str(), Image::WindowsBitmap);
    fprintf(stderr, "... %s\n", decoded_filename.c_str());
  }
}

void write_decoded_sicn(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  auto decoded = ResourceFile::decode_sicn(data, size);
  string prefix = output_prefix(out_dir, base_filename, type, id);

  for (size_t x = 0; x < decoded.size(); x++) {
    string decoded_filename = string_printf("%s_%zu.bmp", prefix.c_str(), x);
    decoded[x].save(decoded_filename.c_str(), Image::WindowsBitmap);
    fprintf(stderr, "... %s\n", decoded_filename.c_str());
  }
}

void write_decoded_icnN(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  write_decoded_image_masked(ResourceFile::decode_icnN, out_dir, base_filename,
      data, size, type, id);
}

void write_decoded_icsN(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  write_decoded_image_masked(ResourceFile::decode_icsN, out_dir, base_filename,
      data, size, type, id);
}

void write_decoded_cicn(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {

  auto decoded = ResourceFile::decode_cicn(data, size);
  string prefix = output_prefix(out_dir, base_filename, type, id);

  string decoded_filename = prefix + ".bmp";
  decoded.image.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());

  decoded_filename = prefix + "_mask.bmp";
  decoded.mask.save(decoded_filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());

  if (decoded.bitmap.get_width() && decoded.bitmap.get_height()) {
    decoded_filename = prefix + "_bitmap.bmp";
    decoded.bitmap.save(decoded_filename.c_str(), Image::WindowsBitmap);
    fprintf(stderr, "... %s\n", decoded_filename.c_str());
  }
}

void write_decoded_icl8(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  write_decoded_image(ResourceFile::decode_icl8, out_dir, base_filename, data,
      size, type, id);
}

void write_decoded_ics8(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  write_decoded_image(ResourceFile::decode_ics8, out_dir, base_filename, data,
      size, type, id);
}

void write_decoded_icl4(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  write_decoded_image(ResourceFile::decode_icl4, out_dir, base_filename, data,
      size, type, id);
}

void write_decoded_ics4(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  write_decoded_image(ResourceFile::decode_ics4, out_dir, base_filename, data,
      size, type, id);
}

void write_decoded_icon(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  write_decoded_image(ResourceFile::decode_icon, out_dir, base_filename, data,
      size, type, id);
}

void write_decoded_pict(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {
  write_decoded_image(ResourceFile::decode_pict, out_dir, base_filename, data,
      size, type, id);
}

void write_decoded_snd(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {

  vector<uint8_t> decoded = ResourceFile::decode_snd(data, size);

  string decoded_filename = output_prefix(out_dir, base_filename, type, id) + ".wav";
  save_file(decoded_filename, decoded.data(), decoded.size());
  fprintf(stderr, "... %s\n", decoded_filename.c_str());
}

void write_decoded_text(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {

  string decoded = ResourceFile::decode_text(data, size);

  string decoded_filename = output_prefix(out_dir, base_filename, type, id) + ".txt";
  save_file(decoded_filename, decoded);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());
}

void write_decoded_str(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {

  pair<string, string> decoded = ResourceFile::decode_str(data, size);

  string decoded_filename = output_prefix(out_dir, base_filename, type, id) + ".txt";
  save_file(decoded_filename, decoded.first);
  fprintf(stderr, "... %s\n", decoded_filename.c_str());

  if (!decoded.second.empty()) {
    string decoded_filename = output_prefix(out_dir, base_filename, type, id) + "_data.bin";
    save_file(decoded_filename, decoded.second);
    fprintf(stderr, "... %s\n", decoded_filename.c_str());
  }
}

void write_decoded_strN(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {

  vector<string> decoded = ResourceFile::decode_strN(data, size);

  string prefix = output_prefix(out_dir, base_filename, type, id);
  for (size_t x = 0; x < decoded.size(); x++) {
    string decoded_filename = string_printf("%s_%lu.txt", prefix.c_str(), x);
    save_file(decoded_filename, decoded[x]);
    fprintf(stderr, "... %s\n", decoded_filename.c_str());
  }
}



typedef void (*resource_decode_fn)(const string& out_dir,
    const string& base_filename, const void* data, size_t size, uint32_t type,
    int16_t id);

static unordered_map<uint32_t, resource_decode_fn> type_to_decode_fn({
  {RESOURCE_TYPE_CICN, write_decoded_cicn},
  {RESOURCE_TYPE_CRSR, write_decoded_crsr},
  {RESOURCE_TYPE_CURS, write_decoded_curs},
  {RESOURCE_TYPE_ICL8, write_decoded_icl8},
  {RESOURCE_TYPE_ICS8, write_decoded_ics8},
  {RESOURCE_TYPE_ICL4, write_decoded_icl4},
  {RESOURCE_TYPE_ICS4, write_decoded_ics4},
  {RESOURCE_TYPE_ICNN, write_decoded_icnN},
  {RESOURCE_TYPE_ICSN, write_decoded_icsN},
  {RESOURCE_TYPE_ICON, write_decoded_icon},
  {RESOURCE_TYPE_PAT , write_decoded_pat},
  {RESOURCE_TYPE_PATN, write_decoded_patN},
  {RESOURCE_TYPE_PICT, write_decoded_pict},
  {RESOURCE_TYPE_PPAT, write_decoded_ppat},
  {RESOURCE_TYPE_TEXT, write_decoded_text},
  {RESOURCE_TYPE_SICN, write_decoded_sicn},
  {RESOURCE_TYPE_SND , write_decoded_snd},
  {RESOURCE_TYPE_STR , write_decoded_str},
  {RESOURCE_TYPE_STRN, write_decoded_strN},
});

static const unordered_map<uint32_t, const char*> type_to_ext({
  {RESOURCE_TYPE_MOOV, "mov"},
});



enum class SaveRawBehavior {
  Never = 0,
  IfDecodeFails,
  Always,
};

void export_resource(const char* base_filename, ResourceFile& rf,
    const char* out_dir, uint32_t type, int16_t id, SaveRawBehavior save_raw,
    DebuggingMode decompress_debug = DebuggingMode::Disabled) {
  const char* out_ext = "bin";
  if (type_to_ext.count(type)) {
    out_ext = type_to_ext.at(type);
  }

  uint32_t rtype = bswap32(type);

  // filter the type so it only contains valid filename sharacters
  char type_str[5] = "\0\0\0\0";
  memcpy(&type_str, &rtype, 4);
  for (size_t x = 0; x < 4; x++) {
    if (type_str[x] < 0x20 || type_str[x] > 0x7E || type_str[x] == '/') {
      type_str[x] = '_';
    }
  }
  string out_filename = string_printf("%s/%s_%.4s_%d.%s", out_dir,
      base_filename, type_str, id, out_ext);

  string data;
  bool decompression_failed = false;
  try {
    data = rf.get_resource_data(type, id, true, decompress_debug);
  } catch (const exception& e) {
    auto type_str = string_for_resource_type(type);
    if (rf.resource_is_compressed(type, id)) {
      fprintf(stderr, "warning: failed to load resource %s:%d: %s (retrying without decompression)\n",
          type_str.c_str(), id, e.what());
      try {
        data = rf.get_resource_data(type, id, false);
        decompression_failed = true;
      } catch (const exception& e) {
        fprintf(stderr, "warning: failed to load resource %s:%d: %s\n",
            type_str.c_str(), id, e.what());
        return;
      }
    } else {
      fprintf(stderr, "warning: failed to load resource %s:%d: %s\n",
          type_str.c_str(), id, e.what());
      return;
    }
  }

  bool write_raw = (save_raw == SaveRawBehavior::Always);

  // decode if possible
  resource_decode_fn decode_fn = type_to_decode_fn[type];
  if (!decompression_failed && decode_fn) {
    try {
      decode_fn(out_dir, base_filename, data.data(), data.size(), type, id);
    } catch (const runtime_error& e) {
      fprintf(stderr, "warning: failed to decode %.4s %d: %s\n",
          (const char*)&rtype, id, e.what());

      // write the raw version if decoding failed and we didn't write it already
      if (save_raw == SaveRawBehavior::IfDecodeFails) {
        write_raw = true;
      }
    }
  } else if (save_raw == SaveRawBehavior::IfDecodeFails) {
    write_raw = true;
  }

  if (write_raw) {
    save_file(out_filename, data);
    fprintf(stderr, "... %s\n", out_filename.c_str());
  }
}



void disassemble_file(const string& filename, const string& out_dir,
    bool use_data_fork, const unordered_set<uint32_t>& target_types,
    const unordered_set<int16_t>& target_ids, SaveRawBehavior save_raw,
    DebuggingMode decompress_debug = DebuggingMode::Disabled) {

  // open resource fork if present
  string resource_fork_filename = use_data_fork ? filename :
      first_file_that_exists({
        filename + "/..namedfork/rsrc",
        filename + "/rsrc"});

  // compute the base filename
  size_t last_slash_pos = filename.rfind('/');
  string base_filename = (last_slash_pos == string::npos) ? filename :
      filename.substr(last_slash_pos + 1);

  // get the resources from the file
  try {
    ResourceFile rf(resource_fork_filename.c_str());
    auto resources = rf.all_resources();

    for (const auto& it : resources) {
      if (!target_types.empty() && !target_types.count(it.first)) {
        continue;
      }
      if (!target_ids.empty() && !target_ids.count(it.second)) {
        continue;
      }
      export_resource(base_filename.c_str(), rf, out_dir.c_str(), it.first,
          it.second, save_raw, decompress_debug);
    }
  } catch (const exception& e) {
    fprintf(stderr, "failed on %s: %s", filename.c_str(), e.what());
  }
}

void disassemble_path(const string& filename, const string& out_dir,
    bool use_data_fork, const unordered_set<uint32_t>& target_types,
    const unordered_set<int16_t>& target_ids, SaveRawBehavior save_raw,
    DebuggingMode decompress_debug = DebuggingMode::Disabled) {

  if (isdir(filename)) {
    fprintf(stderr, ">>> %s (directory)\n", filename.c_str());

    unordered_set<string> items;
    try {
      items = list_directory(filename);
    } catch (const runtime_error& e) {
      fprintf(stderr, "warning: can\'t list directory: %s\n", e.what());
      return;
    }

    vector<string> sorted_items;
    sorted_items.insert(sorted_items.end(), items.begin(), items.end());
    sort(sorted_items.begin(), sorted_items.end());

    size_t last_slash_pos = filename.rfind('/');
    string base_filename = (last_slash_pos == string::npos) ? filename :
        filename.substr(last_slash_pos + 1);

    string sub_out_dir = out_dir + "/" + base_filename;
    mkdir(sub_out_dir.c_str(), 0777);

    for (const string& item : sorted_items) {
      disassemble_path(filename + "/" + item, sub_out_dir, use_data_fork,
          target_types, target_ids, save_raw, decompress_debug);
    }
  } else {
    fprintf(stderr, ">>> %s\n", filename.c_str());
    disassemble_file(filename, out_dir, use_data_fork, target_types, target_ids,
        save_raw, decompress_debug);
  }
}



void print_usage(const char* argv0) {
  fprintf(stderr, "\
Usage: %s [options] filename [out_directory]\n\
\n\
If out_directory is not given, the directory <filename>.out is created, and the\n\
output is written there.\n\
\n\
Options:\n\
  --decode-type=TYPE\n\
      Decode the file\'s data fork as if it\'s a single resource of this type.\n\
      If this option is given, all other options are ignored.\n\
  --target-type=TYPE\n\
      Only dump resources of this type (can be given multiple times).\n\
  --target-id=ID\n\
      Only dump resources with this numeric ID (can be given multiple times).\n\
  --skip-decode\n\
      Don\'t decode resources to modern formats; dump raw contents only.\n\
  --save-raw=no\n\
      Don\'t save any raw files; only save decoded resources.\n\
  --save-raw=if-decode-fails\n\
      Only save a raw file if the resource can\'t be decoded (default).\n\
  --save-raw=yes\n\
      Save raw files even for resources that are successfully decoded.\n\
  --copy-handler=TYP1,TYP2\n\
      Decode TYP2 resources as if they were TYP1.\n\
  --data-fork\n\
      Disassemble the file\'s data fork as if it were the resource fork.\n\
\n", argv0);
}

int main(int argc, char* argv[]) {

  fprintf(stderr, "fuzziqer software macos resource fork disassembler\n\n");

  string filename;
  string out_dir;
  bool use_data_fork = false;
  SaveRawBehavior save_raw = SaveRawBehavior::IfDecodeFails;
  unordered_set<uint32_t> target_types;
  unordered_set<int16_t> target_ids;
  uint32_t decode_type = 0;
  DebuggingMode decompress_debug = DebuggingMode::Disabled;
  for (int x = 1; x < argc; x++) {
    if (argv[x][0] == '-') {
      if (!strncmp(argv[x], "--decode-type=", 14)) {
        if (strlen(argv[x]) != 18) {
          fprintf(stderr, "incorrect format for --decode-type: %s\n", argv[x]);
          return 1;
        }
        decode_type = bswap32(*(uint32_t*)&argv[x][14]);

      } else if (!strncmp(argv[x], "--copy-handler=", 15)) {
        if (strlen(argv[x]) != 24 || argv[x][19] != ',') {
          fprintf(stderr, "incorrect format for --copy-handler: %s\n", argv[x]);
          return 1;
        }
        uint32_t from_type = bswap32(*(uint32_t*)&argv[x][15]);
        uint32_t to_type = bswap32(*(uint32_t*)&argv[x][20]);
        if (!type_to_decode_fn.count(from_type)) {
          fprintf(stderr, "no handler exists for type %.4s\n", (const char*)&from_type);
          return 1;
        }
        fprintf(stderr, "note: treating %.4s resources as %.4s\n", (const char*)&to_type,
            (const char*)&from_type);
        type_to_decode_fn[to_type] = type_to_decode_fn[from_type];

      } else if (!strncmp(argv[x], "--target-type=", 14)) {
        if (strlen(argv[x]) != 18) {
          fprintf(stderr, "incorrect format for --target-type: %s\n", argv[x]);
          return 1;
        }
        uint32_t target_type = bswap32(*(uint32_t*)&argv[x][14]);
        target_types.emplace(target_type);
        fprintf(stderr, "note: added %08" PRIX32 " (%.4s) to target types\n",
            target_type, &argv[x][14]);

      } else if (!strncmp(argv[x], "--target-id=", 12)) {
        int16_t target_id = strtol(&argv[x][12], NULL, 0);
        target_ids.emplace(target_id);
        fprintf(stderr, "note: added %04" PRIX16 " (%" PRId16 ") to target ids\n",
            target_id, target_id);

      } else if (!strcmp(argv[x], "--skip-decode")) {
        fprintf(stderr, "note: skipping all decoding steps\n");
        type_to_decode_fn.clear();

      } else if (!strcmp(argv[x], "--save-raw=no")) {
        fprintf(stderr, "note: only writing decoded resources\n");
        save_raw = SaveRawBehavior::Never;

      } else if (!strcmp(argv[x], "--save-raw=if-decode-fails")) {
        fprintf(stderr, "note: writing raw resources if decode fails\n");
        save_raw = SaveRawBehavior::IfDecodeFails;

      } else if (!strcmp(argv[x], "--save-raw=yes")) {
        fprintf(stderr, "note: writing all raw resources\n");
        save_raw = SaveRawBehavior::Always;

      } else if (!strcmp(argv[x], "--data-fork")) {
        fprintf(stderr, "note: reading data forks as resource forks\n");
        use_data_fork = true;

      } else if (!strcmp(argv[x], "--decompress-debug")) {
        fprintf(stderr, "note: decompression debugging enabled\n");
        decompress_debug = DebuggingMode::Passive;

      } else if (!strcmp(argv[x], "--decompress-debug-interactive")) {
        fprintf(stderr, "note: interactive decompression debugging enabled\n");
        decompress_debug = DebuggingMode::Interactive;

      } else {
        fprintf(stderr, "unknown option: %s\n", argv[x]);
        return 1;
      }
    } else {
      if (filename.empty()) {
        filename = argv[x];
      } else if (out_dir.empty()) {
        out_dir = argv[x];
      } else {
        print_usage(argv[0]);
        return 1;
      }
    }
  }

  if (filename.empty()) {
    print_usage(argv[0]);
    return 1;
  }

  if (decode_type) {
    if (!out_dir.empty()) {
      print_usage(argv[0]);
      return 1;
    }

    resource_decode_fn decode_fn = type_to_decode_fn[decode_type];
    if (!decode_fn) {
      fprintf(stderr, "error: cannot decode resources of this type\n");
      return 2;
    }

    string data = load_file(filename);
    try {
      decode_fn(filename, "", data.data(), data.size(), decode_type, 0);
    } catch (const runtime_error& e) {
      fprintf(stderr, "error: failed to decode %s: %s\n",
          filename.c_str(), e.what());
      return 3;
    }

    return 0;
  }

  if (out_dir.empty()) {
    out_dir = filename + ".out";
  }
  mkdir(out_dir.c_str(), 0777);

  disassemble_path(filename, out_dir, use_data_fork, target_types, target_ids,
      save_raw, decompress_debug);

  return 0;
}
