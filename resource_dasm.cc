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
#include <phosg/JSON.hh>
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



void write_decoded_file(const string& out_dir, const string& base_filename,
    uint32_t type, int16_t id, const string& after, const string& data) {
  string filename = output_prefix(out_dir, base_filename, type, id) + after;
  save_file(filename.c_str(), data);
  fprintf(stderr, "... %s\n", filename.c_str());
}

void write_decoded_image(const string& out_dir, const string& base_filename,
    uint32_t type, int16_t id, const string& after, const Image& img) {

  string filename = output_prefix(out_dir, base_filename, type, id) + after;
  img.save(filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", filename.c_str());
}

void write_decoded_curs(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_curs(id, type);

  string after = string_printf("_%hu_%hu.bmp", decoded.hotspot_x, decoded.hotspot_y);
  write_decoded_image(out_dir, base_filename, type, id, after, decoded.bitmap);
}

void write_decoded_crsr(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_crsr(id, type);

  string after = string_printf("_%hu_%hu.bmp", decoded.hotspot_x, decoded.hotspot_y);

  write_decoded_image(out_dir, base_filename, type, id, "_bitmap.bmp", decoded.bitmap);
  write_decoded_image(out_dir, base_filename, type, id, after, decoded.image);
}

void write_decoded_ppat(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_ppat(id, type);

  Image tiled = tile_image(decoded.first, 8, 8);
  write_decoded_image(out_dir, base_filename, type, id, ".bmp", decoded.first);
  write_decoded_image(out_dir, base_filename, type, id, "_tiled.bmp", tiled);

  tiled = tile_image(decoded.second, 8, 8);
  write_decoded_image(out_dir, base_filename, type, id, "_bitmap.bmp", decoded.second);
  write_decoded_image(out_dir, base_filename, type, id, "_bitmap_tiled.bmp", tiled);
}

void write_decoded_pptN(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_pptN(id, type);

  for (size_t x = 0; x < decoded.size(); x++) {
    string after = string_printf("_%zu.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, after, decoded[x].first);

    Image tiled = tile_image(decoded[x].first, 8, 8);
    after = string_printf("_%zu_tiled.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, after, tiled);

    after = string_printf("_%zu_bitmap.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, after, decoded[x].second);

    tiled = tile_image(decoded[x].second, 8, 8);
    after = string_printf("_%zu_bitmap_tiled.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, after, tiled);
  }
}

void write_decoded_color_table(const string& out_dir,
    const string& base_filename, uint32_t type, int16_t id,
    const vector<Color>& decoded) {
  Image img(100, 16 * decoded.size(), false);
  img.clear(0x00, 0x00, 0x00);
  for (size_t z = 0; z < decoded.size(); z++) {
    img.fill_rect(0, 16 * z, 16, 16, decoded[z].r >> 8, decoded[z].g >> 8, decoded[z].b >> 8);

    ssize_t x = 20, y = 16 * z + 4, width = 0;
    img.draw_text(x, y, &width, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
        0x00, "#");
    x += width;

    img.draw_text(x, y, &width, NULL, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
        0x00, "%04hX", decoded[z].r);
    x += width;

    img.draw_text(x, y, &width, NULL, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00,
        0x00, "%04hX", decoded[z].g);
    x += width;

    img.draw_text(x, y, &width, NULL, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00,
        0x00, "%04hX", decoded[z].b);
    x += width;
  }
  write_decoded_image(out_dir, base_filename, type, id, ".bmp", img);
}

void write_decoded_pltt(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  // always write the raw for this resource type because the decoded version
  // loses precision
  write_decoded_file(out_dir, base_filename, type, id, ".bin", res.get_resource_data(type, id));

  auto decoded = res.decode_pltt(id, type);
  write_decoded_color_table(out_dir, base_filename, type, id, decoded);
}

void write_decoded_clut(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  // always write the raw for this resource type because the decoded version
  // loses precision
  write_decoded_file(out_dir, base_filename, type, id, ".bin", res.get_resource_data(type, id));

  auto decoded = res.decode_clut(id, type);
  write_decoded_color_table(out_dir, base_filename, type, id, decoded);
}

void write_decoded_pat(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  Image decoded = res.decode_pat(id, type);

  Image tiled = tile_image(decoded, 8, 8);
  write_decoded_image(out_dir, base_filename, type, id, ".bmp", decoded);
  write_decoded_image(out_dir, base_filename, type, id, "_tiled.bmp", tiled);
}

void write_decoded_patN(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_patN(id, type);

  for (size_t x = 0; x < decoded.size(); x++) {
    string after = string_printf("_%zu.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, after, decoded[x]);

    Image tiled = tile_image(decoded[x], 8, 8);
    after = string_printf("_%zu_tiled.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, after, tiled);
  }
}

void write_decoded_sicn(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_sicn(id, type);

  for (size_t x = 0; x < decoded.size(); x++) {
    string after = string_printf("_%zu.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, after, decoded[x]);
  }
}

void write_decoded_icnN(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_icnN(id, type);
  write_decoded_image(out_dir, base_filename, type, id, ".bmp", decoded);
}

void write_decoded_icsN(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_icsN(id, type);
  write_decoded_image(out_dir, base_filename, type, id, ".bmp", decoded);
}

void write_decoded_cicn(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_cicn(id, type);

  write_decoded_image(out_dir, base_filename, type, id, ".bmp", decoded.image);

  if (decoded.bitmap.get_width() && decoded.bitmap.get_height()) {
    write_decoded_image(out_dir, base_filename, type, id, "_bitmap.bmp", decoded.bitmap);
  }
}

void write_decoded_icl8(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_icl8(id, type);
  write_decoded_image(out_dir, base_filename, type, id, ".bmp", decoded);
}

void write_decoded_ics8(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_ics8(id, type);
  write_decoded_image(out_dir, base_filename, type, id, ".bmp", decoded);
}

void write_decoded_icl4(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_icl4(id, type);
  write_decoded_image(out_dir, base_filename, type, id, ".bmp", decoded);
}

void write_decoded_ics4(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_ics4(id, type);
  write_decoded_image(out_dir, base_filename, type, id, ".bmp", decoded);
}

void write_decoded_icon(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_icon(id, type);
  write_decoded_image(out_dir, base_filename, type, id, ".bmp", decoded);
}

void write_decoded_pict(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  auto decoded = res.decode_pict(id, type);
  write_decoded_image(out_dir, base_filename, type, id, ".bmp", decoded);
}

void write_decoded_snd(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  string decoded = res.decode_snd(id, type);
  write_decoded_file(out_dir, base_filename, type, id, ".wav", decoded);
}

void write_decoded_text(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  string decoded = res.decode_text(id, type);
  write_decoded_file(out_dir, base_filename, type, id, ".txt", decoded);
}

void write_decoded_styl(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  string decoded = res.decode_styl(id, type);
  write_decoded_file(out_dir, base_filename, type, id, ".rtf", decoded);
}

void write_decoded_str(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  pair<string, string> decoded = res.decode_str(id, type);

  write_decoded_file(out_dir, base_filename, type, id, ".txt", decoded.first);
  if (!decoded.second.empty()) {
    write_decoded_file(out_dir, base_filename, type, id, "_data.bin", decoded.second);
  }
}

void write_decoded_strN(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {
  vector<string> decoded = res.decode_strN(id, type);

  for (size_t x = 0; x < decoded.size(); x++) {
    string after = string_printf("_%lu.txt", x);
    write_decoded_file(out_dir, base_filename, type, id, after, decoded[x]);
  }
}

void write_decoded_song(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id) {

  auto song = res.decode_song(id, type);

  string midi_contents;
  uint32_t midi_type = 0;
  static const vector<uint32_t> midi_types({RESOURCE_TYPE_MIDI, RESOURCE_TYPE_Midi, RESOURCE_TYPE_midi});
  for (uint32_t type : midi_types) {
    if (res.resource_exists(type, song.midi_id)) {
      midi_type = type;
      break;
    }
  }
  if (midi_type == 0) {
    throw runtime_error("SONG refers to missing MIDI");
  }

  string midi_filename = output_prefix(out_dir, base_filename, midi_type, song.midi_id) + ".midi";

  vector<shared_ptr<JSONObject>> instruments;

  auto add_instrument = [&](uint16_t id, const ResourceFile::decoded_inst& inst) {

    vector<shared_ptr<JSONObject>> key_regions_list;
    for (const auto& rgn : inst.key_regions) {
      string snd_filename = output_prefix(out_dir, base_filename, RESOURCE_TYPE_SND, rgn.snd_id) + ".wav";
      vector<shared_ptr<JSONObject>> key_region_list;
      key_region_list.emplace_back(new JSONObject(static_cast<int64_t>(rgn.key_low)));
      key_region_list.emplace_back(new JSONObject(static_cast<int64_t>(rgn.key_high)));
      key_region_list.emplace_back(new JSONObject(static_cast<int64_t>(rgn.base_note)));
      key_region_list.emplace_back(new JSONObject(snd_filename));

      key_regions_list.emplace_back(new JSONObject(key_region_list));
    }

    unordered_map<string, shared_ptr<JSONObject>> inst_dict;
    inst_dict.emplace("id", new JSONObject(static_cast<int64_t>(id)));
    inst_dict.emplace("regions", new JSONObject(key_regions_list));

    instruments.emplace_back(new JSONObject(inst_dict));
  };

  // TODO: is it correct to include all instruments if there are no overrides?
  // also, should we include non-overridden instruments if there are overrides?
  // SO MANY QUESTIONS!
  if (song.instrument_overrides.empty()) {
    for (int16_t id : res.all_resources_of_type(RESOURCE_TYPE_INST)) {
      add_instrument(id, res.decode_inst(id));
    }
  } else {
    for (const auto& it : song.instrument_overrides) {
      add_instrument(it.first, res.decode_inst(it.second));
    }
  }

  unordered_map<string, shared_ptr<JSONObject>> base_dict;
  base_dict.emplace("sequence_type", new JSONObject("MIDI"));
  base_dict.emplace("sequence_filename", new JSONObject(midi_filename));
  base_dict.emplace("instruments", new JSONObject(instruments));

  shared_ptr<JSONObject> json(new JSONObject(base_dict));
  string json_filename = output_prefix(out_dir, base_filename, type, id) + ".json";
  string json_data = json->format();

  write_decoded_file(out_dir, base_filename, type, id, "_smssynth_env.json", json_data);
}



typedef void (*resource_decode_fn)(const string& out_dir,
    const string& base_filename, ResourceFile& file, uint32_t type, int16_t id);

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
  {RESOURCE_TYPE_PPTN, write_decoded_pptN},
  {RESOURCE_TYPE_PLTT, write_decoded_pltt},
  {RESOURCE_TYPE_CLUT, write_decoded_clut},
  {RESOURCE_TYPE_TEXT, write_decoded_text},
  {RESOURCE_TYPE_STYL, write_decoded_styl},
  {RESOURCE_TYPE_SICN, write_decoded_sicn},
  {RESOURCE_TYPE_SND , write_decoded_snd},
  {RESOURCE_TYPE_SONG, write_decoded_song},
  {RESOURCE_TYPE_STR , write_decoded_str},
  {RESOURCE_TYPE_STRN, write_decoded_strN},
});

static const unordered_map<uint32_t, const char*> type_to_ext({
  {RESOURCE_TYPE_MOOV, "mov"},
  {RESOURCE_TYPE_MIDI, "midi"},
  {RESOURCE_TYPE_Midi, "midi"},
  {RESOURCE_TYPE_midi, "midi"},
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
      decode_fn(out_dir, base_filename, rf, type, id);
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
    fprintf(stderr, "failed on %s: %s\n", filename.c_str(), e.what());
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
    SingleResourceFile rf(decode_type, 0, data.data(), data.size());

    try {
      decode_fn(filename, "", rf, decode_type, 0);
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
