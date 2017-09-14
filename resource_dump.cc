#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <unordered_map>
#include <vector>

#include "resource_fork.hh"
#include "util.hh"

using namespace std;


void print_usage(const char* name) {
  printf("usage: %s [--copy-handler=FROM,TO | --raw] filename [out_dir]\n",
      name);
}

void decode_cicn(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {

  Image img = decode_cicn(data, size, 0xFF, 0xFF, 0xFF);

  uint32_t type_sw = bswap32(type);
  string decoded_filename = string_printf("%s/%s_%.4s_%d.bmp", out_dir.c_str(),
      base_filename.c_str(), (const char*)&type_sw, id);
  img.save(decoded_filename.c_str(), Image::WindowsBitmap);
  printf("... %s\n", decoded_filename.c_str());
}

void decode_pict(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {

  Image img = decode_pict(data, size);

  uint32_t type_sw = bswap32(type);
  string decoded_filename = string_printf("%s/%s_%.4s_%d.bmp", out_dir.c_str(),
      base_filename.c_str(), (const char*)&type_sw, id);
  img.save(decoded_filename.c_str(), Image::WindowsBitmap);
  printf("... %s\n", decoded_filename.c_str());
}

void decode_snd(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {

  vector<uint8_t> decoded = decode_snd(data, size);

  uint32_t type_sw = bswap32(type);
  string decoded_filename = string_printf("%s/%s_%.4s_%d.wav", out_dir.c_str(),
      base_filename.c_str(), (const char*)&type_sw, id);
  save_file(decoded_filename, decoded.data(), decoded.size());
  printf("... %s\n", decoded_filename.c_str());
}

void decode_strN(const string& out_dir, const string& base_filename,
    const void* data, size_t size, uint32_t type, int16_t id) {

  vector<string> decoded = decode_strN(data, size);

  uint32_t type_sw = bswap32(type);
  for (size_t x = 0; x < decoded.size(); x++) {
    string decoded_filename = string_printf("%s/%s_%.4s_%d_%lu.txt",
        out_dir.c_str(), base_filename.c_str(), (const char*)&type_sw, id, x);
    save_file(decoded_filename, decoded[x]);
    printf("... %s\n", decoded_filename.c_str());
  }
}



typedef void (*resource_decode_fn)(const string& out_dir,
    const string& base_filename, const void* data, size_t size, uint32_t type,
    int16_t id);

static unordered_map<uint32_t, resource_decode_fn> type_to_decode_fn({
  {RESOURCE_TYPE_CICN, decode_cicn},
  {RESOURCE_TYPE_PICT, decode_pict},
  {RESOURCE_TYPE_SND , decode_snd},
  //{RESOURCE_TYPE_STRN, decode_strN},
});

static const unordered_map<uint32_t, const char*> type_to_ext({
  {RESOURCE_TYPE_TEXT, "txt"},
});



enum class SaveRawBehavior {
  Never = 0,
  IfDecodeFails = 1,
  Always = 2,
};

void export_resource(const char* base_filename, const char* resource_filename,
    const char* out_dir, uint32_t type, int16_t id, SaveRawBehavior save_raw) {
  const char* out_ext = "raw";
  if (type_to_ext.count(type)) {
    out_ext = type_to_ext.at(type);
  }

  uint32_t rtype = bswap32(type);
  string out_filename = string_printf("%s/%s_%.4s_%d.%s", out_dir,
      base_filename, (const char*)&rtype, id, out_ext);

  void* data;
  size_t size;
  try {
    load_resource_from_file(resource_filename, type, id, &data, &size);
  } catch (const runtime_error& e) {
    fprintf(stderr, "warning: failed to load resource %08X:%d: %s\n", type, id,
        e.what());
    return;
  }

  // save the raw contents, if requested
  if (save_raw == SaveRawBehavior::Always) {
    save_file(out_filename, data, size);
    printf("... %s\n", out_filename.c_str());
  }

  // decode if possible
  resource_decode_fn decode_fn = type_to_decode_fn[type];
  if (decode_fn) {
    try {
      decode_fn(out_dir, base_filename, data, size, type, id);
    } catch (const runtime_error& e) {
      fprintf(stderr, "warning: failed to decode %.4s %d: %s\n",
          (const char*)&rtype, id, e.what());

      // write the raw version if decoding failed and we didn't write it already
      if (save_raw == SaveRawBehavior::IfDecodeFails) {
        save_file(out_filename, data, size);
        printf("... %s\n", out_filename.c_str());
      }
    }
  }

  free(data);
}



void disassemble_file(const string& filename, const string& out_dir,
    const unordered_set<uint32_t>& target_types,
    const unordered_set<int16_t>& target_ids, SaveRawBehavior save_raw) {

  // open resource fork if present
  string resource_fork_filename = first_file_that_exists({
      filename + "/..namedfork/rsrc",
      filename + "/rsrc"});

  // compute the base filename
  size_t last_slash_pos = filename.rfind('/');
  string base_filename = (last_slash_pos == string::npos) ? filename :
      filename.substr(last_slash_pos + 1);

  // get the resources from the file
  vector<pair<uint32_t, int16_t>> resources;
  try {
    resources = enum_file_resources(resource_fork_filename.c_str());
  } catch (const runtime_error& e) {
    fprintf(stderr, "warning: can't enumerate resources; skipping file (%s)\n",
        e.what());
    return;
  }

  for (const auto& it : resources) {
    if (!target_types.empty() && !target_types.count(it.first)) {
      continue;
    }
    if (!target_ids.empty() && !target_ids.count(it.second)) {
      continue;
    }
    export_resource(base_filename.c_str(), resource_fork_filename.c_str(),
        out_dir.c_str(), it.first, it.second, save_raw);
  }
}

void disassemble_path(const string& filename, const string& out_dir,
    const unordered_set<uint32_t>& target_types,
    const unordered_set<int16_t>& target_ids, SaveRawBehavior save_raw) {

  if (isdir(filename)) {
    printf(">>> %s (directory)\n", filename.c_str());

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
      disassemble_path(filename + "/" + item, sub_out_dir, target_types,
          target_ids, save_raw);
    }
  } else {
    printf(">>> %s\n", filename.c_str());
    disassemble_file(filename, out_dir, target_types, target_ids, save_raw);
  }
}



int main(int argc, char* argv[]) {

  printf("fuzziqer software macos resource fork disassembler\n\n");

  string filename;
  string out_dir;
  SaveRawBehavior save_raw = SaveRawBehavior::IfDecodeFails;
  unordered_set<uint32_t> target_types;
  unordered_set<int16_t> target_ids;
  for (int x = 1; x < argc; x++) {
    if (argv[x][0] == '-') {
      if (!strncmp(argv[x], "--copy-handler=", 15)) {
        if (strlen(argv[x]) != 24 || argv[x][19] != ',') {
          printf("incorrect format for --copy-handler: %s\n", argv[x]);
          return 1;
        }
        uint32_t from_type = bswap32(*(uint32_t*)&argv[x][15]);
        uint32_t to_type = bswap32(*(uint32_t*)&argv[x][20]);
        if (!type_to_decode_fn.count(from_type)) {
          printf("no handler exists for type %.4s\n", (const char*)&from_type);
          return 1;
        }
        printf("note: treating %.4s resources as %.4s\n", (const char*)&to_type,
            (const char*)&from_type);
        type_to_decode_fn[to_type] = type_to_decode_fn[from_type];

      } else if (!strncmp(argv[x], "--target-type=", 14)) {
        if (strlen(argv[x]) != 18) {
          printf("incorrect format for --target-type: %s\n", argv[x]);
          return 1;
        }
        uint32_t target_type = bswap32(*(uint32_t*)&argv[x][14]);
        target_types.emplace(target_type);
        printf("note: added %08" PRIX32 " (%.4s) to target types\n",
            target_type, (const char*)&target_type);

      } else if (!strncmp(argv[x], "--target-id=", 12)) {
        int16_t target_id = strtol(&argv[x][12], NULL, 0);
        target_ids.emplace(target_id);
        printf("note: added %04" PRIX16 " (%" PRId16 ") to target types\n",
            target_id, target_id);

      } else if (!strcmp(argv[x], "--skip-decode")) {
        printf("note: skipping all decoding steps\n");
        type_to_decode_fn.clear();

      } else if (!strcmp(argv[x], "--skip-raw")) {
        printf("note: only writing decoded resources\n");
        save_raw = SaveRawBehavior::Never;

      } else if (!strcmp(argv[x], "--save-raw")) {
        printf("note: writing all raw resources\n");
        save_raw = SaveRawBehavior::Always;

      } else {
        printf("unknown option: %s\n", argv[x]);
        return 1;
      }
    } else {
      if (filename.empty())
        filename = argv[x];
      else if (out_dir.empty())
        out_dir = argv[x];
      else {
        print_usage(argv[0]);
        return 1;
      }
    }
  }

  if (filename.empty()) {
    print_usage(argv[0]);
    return 1;
  }

  if (out_dir.empty()) {
    out_dir = filename + ".out";
  }
  mkdir(out_dir.c_str(), 0777);

  disassemble_path(filename, out_dir, target_types, target_ids, save_raw);

  return 0;
}
