#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "Image.hh"

#include "realmz_lib.hh"
#include "resource_fork.hh"

using namespace std;

int16_t byteswap16(int16_t a) {
  return ((a >> 8) & 0x00FF) | ((a << 8) & 0xFF00);
}

int32_t byteswap32(int32_t a) {
  return ((a >> 24) & 0x000000FF) |
         ((a >> 8)  & 0x0000FF00) |
         ((a << 8)  & 0x00FF0000) |
         ((a << 24) & 0xFF000000);
}

FILE* fopen_or_throw(const char* filename, const char* mode) {
  FILE* f = fopen(filename, mode);
  if (!f)
    throw runtime_error("can\'t open file " + string(filename));
  return f;
}

uint64_t num_elements_in_file(FILE* f, size_t size) {
  fseek(f, 0, SEEK_END);
  uint64_t num = ftell(f) / size;
  fseek(f, 0, SEEK_SET);
  return num;
}

string string_printf(const char* fmt, ...) {
  char* result = NULL;

  va_list va;
  va_start(va, fmt);
  vasprintf(&result, fmt, va);
  va_end(va);

  if (result == NULL)
    throw bad_alloc();

  string ret(result);
  free(result);
  return ret;
}

string escape_quotes(const string& s) {
  string ret;
  for (size_t x = 0; x < s.size(); x++) {
    char ch = s[x];
    if (ch == '\"')
      ret += "\\\"";
    else if (ch < 0x20 || ch > 0x7E)
      ret += string_printf("\\x%02X", ch);
    else
      ret += ch;
  }
  return ret;
}

const char* first_file_that_exists(const char* fname, ...) {
  struct stat st;
  if (stat(fname, &st) == 0)
    return fname;

  va_list va;
  va_start(va, fname);
  const char* ret = NULL;
  while (!ret && (fname = va_arg(va, const char*)))
    if (stat(fname, &st) == 0)
      ret = fname;
  va_end(va);

  return ret;
}

template <typename T>
vector<T> load_direct_file_data(const string& filename) {
  FILE* f = fopen_or_throw(filename.c_str(), "rb");
  uint64_t num = num_elements_in_file(f, sizeof(T));

  vector<T> all_info(num);
  fread(all_info.data(), sizeof(T), num, f);
  fclose(f);

  for (auto& e : all_info)
    e.byteswap();
  return all_info;
}

template <typename T>
T load_direct_file_data_single(const string& filename) {
  FILE* f = fopen_or_throw(filename.c_str(), "rb");

  T t;
  fread(&t, sizeof(T), 1, f);
  fclose(f);

  t.byteswap();
  return t;
}

string render_string_reference(const vector<string>& strings, int index) {
  if (index == 0)
    return "0";
  if ((size_t)abs(index) >= strings.size())
    return string_printf("%d", index);
  return string_printf("\"%s\"#%d", escape_quotes(strings[abs(index)]).c_str(),
      index);
}

string parse_realmz_string(uint8_t valid_chars, const char* data) {
  if (valid_chars == 0xFF) {
    valid_chars = 0;
    while (data[valid_chars] != ' ')
      valid_chars++;
  }
  return string(data, valid_chars);
}



////////////////////////////////////////////////////////////////////////////////
// SCENARIO.RSF

unordered_map<int16_t, Image> get_picts(const string& rsf_name) {
  unordered_map<int16_t, Image> ret;

  for (const auto& it : enum_file_resources(rsf_name.c_str())) {
    if (it.first != RESOURCE_TYPE_PICT)
      continue;

    try {
      void* data;
      size_t size;
      load_resource_from_file(rsf_name.c_str(), it.first, it.second, &data,
          &size);
      try {
        ret.emplace(it.second, decode_pict(data, size));
      } catch (const runtime_error& e) {
        fprintf(stderr, "warning: failed to decode pict %d: %s\n", it.second,
            e.what());
      }
      free(data);

    } catch (const runtime_error& e) {
      fprintf(stderr, "warning: failed to load resource %08X:%d: %s\n",
          it.first, it.second, e.what());
    }
  }

  return ret;
}

unordered_map<int16_t, Image> get_cicns(const string& rsf_name) {
  unordered_map<int16_t, Image> ret;

  for (const auto& it : enum_file_resources(rsf_name.c_str())) {
    if (it.first != RESOURCE_TYPE_CICN)
      continue;

    try {
      void* data;
      size_t size;
      load_resource_from_file(rsf_name.c_str(), it.first, it.second, &data,
          &size);
      try {
        ret.emplace(it.second, decode_cicn32(data, size, 0xFF, 0xFF, 0xFF));
      } catch (const runtime_error& e) {
        fprintf(stderr, "warning: failed to decode cicn %d: %s\n", it.second,
            e.what());
      }
      free(data);

    } catch (const runtime_error& e) {
      fprintf(stderr, "warning: failed to load resource %08X:%d: %s\n",
          it.first, it.second, e.what());
    }
  }

  return ret;
}

unordered_map<int16_t, vector<uint8_t>> get_snds(const string& rsf_name,
    bool decode) {
  unordered_map<int16_t, vector<uint8_t>> ret;

  for (const auto& it : enum_file_resources(rsf_name.c_str())) {
    if (it.first != RESOURCE_TYPE_SND)
      continue;

    try {
      void* data;
      size_t size;
      load_resource_from_file(rsf_name.c_str(), it.first, it.second, &data,
          &size);
      if (decode) {
        try {
          ret[it.second] = decode_snd(data, size);
        } catch (const runtime_error& e) {
          fprintf(stderr, "warning: failed to decode sound %d: %s\n", it.second,
              e.what());
        }
      } else {
        ret[it.second].resize(size);
        memcpy(ret[it.second].data(), data, size);
      }
      free(data);

    } catch (const runtime_error& e) {
      fprintf(stderr, "warning: failed to load resource %08X:%d: %s\n",
          it.first, it.second, e.what());
    }
  }

  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// LAYOUT

level_neighbors::level_neighbors() : x(-1), y(-1), left(-1), right(-1), top(-1),
    bottom(-1) { }

land_layout::land_layout() {
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 16; x++)
      this->layout[y][x] = -1;
}

void land_layout::byteswap() {
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 16; x++)
      this->layout[y][x] = byteswap16(this->layout[y][x]);
}

land_layout load_land_layout(const string& filename) {
  land_layout l = load_direct_file_data_single<land_layout>(filename);
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 16; x++) {
      if (l.layout[y][x] == -1)
        l.layout[y][x] = 0;
      else if (l.layout[y][x] == 0)
        l.layout[y][x] = -1;
    }
  }
  return l;
}

level_neighbors get_level_neighbors(const land_layout& l, int16_t id) {
  level_neighbors n;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 16; x++) {
      if (l.layout[y][x] == id) {
        if (n.x != -1 || n.y != -1) {
          throw runtime_error("multiple entries for level");
        }

        n.x = x;
        n.y = y;
        if (x != 0)
          n.left = l.layout[y][x - 1];
        if (x != 15)
          n.right = l.layout[y][x + 1];
        if (y != 0)
          n.top = l.layout[y - 1][x];
        if (y != 7)
          n.bottom = l.layout[y + 1][x];
      }
    }
  }

  return n;
}

Image generate_layout_map(const land_layout& l,
    const unordered_map<int16_t, string>& level_id_to_image_name) {

  int min_x = 16, min_y = 8, max_x = -1, max_y = -1;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 16; x++) {
      if (l.layout[y][x] < 0)
        continue;
      if (x < min_x)
        min_x = x;
      if (x > max_x)
        max_x = x;
      if (y < min_y)
        min_y = y;
      if (y > max_y)
        max_y = y;
    }
  }

  if (max_x < min_x || max_y < min_y)
    throw runtime_error("layout has no valid levels");

  max_x++;
  max_y++;

  Image overall_map(90 * 32 * (max_x - min_x), 90 * 32 * (max_y - min_y));
  for (int y = 0; y < (max_y - min_y); y++) {
    for (int x = 0; x < (max_x - min_x); x++) {
      int16_t level_id = l.layout[y + min_y][x + min_x];
      if (level_id < 0)
        continue;

      Image this_level_map(level_id_to_image_name.at(level_id).c_str());

      level_neighbors n = get_level_neighbors(l, level_id);
      int sx = (n.left >= 0) ? 9 : 0;
      int sy = (n.top >= 0) ? 9 : 0;

      int xp = 90 * 32 * x;
      int yp = 90 * 32 * y;
      overall_map.Blit(this_level_map, xp, yp, 90 * 32, 90 * 32, sx, sy);
    }
  }

  return overall_map;
}



////////////////////////////////////////////////////////////////////////////////
// GLOBAL

void global_metadata::byteswap() {
  this->start_xap = byteswap16(this->start_xap);
  this->death_xap = byteswap16(this->death_xap);
  this->quit_xap = byteswap16(this->quit_xap);
  this->reserved1_xap = byteswap16(this->reserved1_xap);
  this->shop_xap = byteswap16(this->shop_xap);
  this->temple_xap = byteswap16(this->temple_xap);
  this->reserved2_xap = byteswap16(this->reserved2_xap);
}

global_metadata load_global_metadata(const string& filename) {
  return load_direct_file_data_single<global_metadata>(filename);
}

string disassemble_globals(const global_metadata& g) {
  return string_printf("==== GLOBAL METADATA\n"
      "  start_xap=%d\n"
      "  death_xap=%d\n"
      "  quit_xap=%d\n"
      "  reserved1_xap=%d\n"
      "  shop_xap=%d\n"
      "  temple_xap=%d\n"
      "  reserved2_xap=%d\n",
      g.start_xap, g.death_xap, g.quit_xap, g.reserved1_xap, g.shop_xap,
      g.temple_xap, g.reserved2_xap);
}



////////////////////////////////////////////////////////////////////////////////
// SCENARIO NAME

void scenario_metadata::byteswap() {
  this->recommended_starting_levels = byteswap32(this->recommended_starting_levels);
  this->unknown1 = byteswap32(this->unknown1);
  this->start_level = byteswap32(this->start_level);
  this->start_x = byteswap32(this->start_x);
  this->start_y = byteswap32(this->start_y);
}

scenario_metadata load_scenario_metadata(const string& filename) {
  return load_direct_file_data_single<scenario_metadata>(filename);
}



////////////////////////////////////////////////////////////////////////////////
// DATA EDCD

void ecodes::byteswap() {
  for (int x = 0; x < 5; x++)
    this->data[x] = byteswap16(this->data[x]);
}

vector<ecodes> load_ecodes_index(const string& filename) {
  return load_direct_file_data<ecodes>(filename);
}



////////////////////////////////////////////////////////////////////////////////
// DATA ED

void simple_encounter::byteswap() {
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 8; x++)
      this->choice_args[y][x] = byteswap16(this->choice_args[y][x]);
  this->unknown = byteswap16(this->unknown);
  this->prompt = byteswap16(this->prompt);
}

vector<simple_encounter> load_simple_encounter_index(const string& filename) {
  return load_direct_file_data<simple_encounter>(filename);
}

string disassemble_simple_encounter(int index, const simple_encounter& e,
    const vector<ecodes> ecodes, const vector<string>& strings) {

  string ret = string_printf("===== SIMPLE ENCOUNTER id=%d\n", index);

  ret += string_printf("  can_backout=%d\n", e.can_backout);
  ret += string_printf("  max_times=%d\n", e.max_times);
  ret += string_printf("  prompt=%s\n", render_string_reference(strings,
      e.prompt).c_str());
  for (int x = 0; x < 4; x++) {
    if (!e.choice_text[x].valid_chars)
      continue;
    string choice_text = parse_realmz_string(e.choice_text[x].valid_chars,
        e.choice_text[x].text);
    if (choice_text.empty())
      continue;
    ret += string_printf("  choice%d: result=%d text=\"%s\"\n", x,
        e.choice_result_index[x], escape_quotes(choice_text).c_str());
  }

  for (int x = 0; x < 4; x++) {
    int y;
    for (y = 0; y < 8; y++)
      if (e.choice_codes[x][y] || e.choice_args[x][y])
        break;
    if (y == 8)
      break; // option is blank; don't even print it

    for (int y = 0; y < 8; y++)
      if (e.choice_codes[x][y] || e.choice_args[x][y])
        ret += string_printf("  result%d/%d> %s\n", x + 1, y, disassemble_opcode(
            e.choice_codes[x][y], e.choice_args[x][y], ecodes, strings).c_str());
  }

  return ret;
}

string disassemble_all_simple_encounters(const vector<simple_encounter>& e,
    const vector<ecodes> ecodes, const vector<string>& strings) {
  string ret;
  for (size_t x = 0; x < e.size(); x++)
    ret += disassemble_simple_encounter(x, e[x], ecodes, strings);
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// DATA ED2

void complex_encounter::byteswap() {
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 8; x++)
      this->choice_args[y][x] = byteswap16(this->choice_args[y][x]);
  for (int x = 0; x < 10; x++)
    this->spell_codes[x] = byteswap16(this->spell_codes[x]);
  for (int x = 0; x < 10; x++)
    this->item_codes[x] = byteswap16(this->item_codes[x]);
  this->rogue_encounter_id = byteswap16(this->rogue_encounter_id);
  this->prompt = byteswap16(this->prompt);
}

vector<complex_encounter> load_complex_encounter_index(const string& filename) {
  return load_direct_file_data<complex_encounter>(filename);
}

string disassemble_complex_encounter(int index, const complex_encounter& e,
    const vector<ecodes> ecodes, const vector<string>& strings) {

  string ret = string_printf("===== COMPLEX ENCOUNTER id=%d\n", index);

  ret += string_printf("  can_backout=%d\n", e.can_backout);
  ret += string_printf("  max_times=%d\n", e.max_times);
  ret += string_printf("  prompt=%s\n", render_string_reference(strings,
      e.prompt).c_str());

  for (int x = 0; x < 10; x++) {
    if (!e.spell_codes[x])
      continue;
    ret += string_printf("  spell: id=%d result=%d\n", e.spell_codes[x],
        e.spell_result_codes[x]);
  }

  for (int x = 0; x < 5; x++) {
    if (!e.item_codes[x])
      continue;
    ret += string_printf("  item: id=%d result=%d\n", e.item_codes[x],
        e.item_result_codes[x]);
  }

  for (int x = 0; x < 5; x++) {
    if (!e.item_codes[x])
      continue;
    string action_text = parse_realmz_string(e.action_text[x].valid_chars,
        e.action_text[x].text);
    if (action_text.empty())
      continue;
    ret += string_printf("  action: selected=%d text=\"%s\"\n",
        e.actions_selected[x], escape_quotes(action_text).c_str());
  }
  ret += string_printf("  action_result=%d\n", e.action_result);

  ret += string_printf("  rogue_encounter: present=%d id=%d reset=%d\n",
      e.has_rogue_encounter, e.rogue_encounter_id, e.rogue_reset_flag);

  string speak_text = parse_realmz_string(e.speak_text.valid_chars,
      e.speak_text.text);
  if (!speak_text.empty())
    ret += string_printf("  speak: result=%d text=\"%s\"\n", e.speak_result,
        escape_quotes(speak_text).c_str());

  for (int x = 0; x < 4; x++) {
    int y;
    for (y = 0; y < 8; y++)
      if (e.choice_codes[x][y] || e.choice_args[x][y])
        break;
    if (y == 8)
      break; // option is blank; don't even print it

    for (int y = 0; y < 8; y++)
      if (e.choice_codes[x][y] || e.choice_args[x][y])
        ret += string_printf("  result%d/%d> %s\n", x + 1, y, disassemble_opcode(
            e.choice_codes[x][y], e.choice_args[x][y], ecodes, strings).c_str());
  }

  return ret;
}

string disassemble_all_complex_encounters(const vector<complex_encounter>& e,
    const vector<ecodes> ecodes, const vector<string>& strings) {
  string ret;
  for (size_t x = 0; x < e.size(); x++)
    ret += disassemble_complex_encounter(x, e[x], ecodes, strings);
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// DATA TD2

void rogue_encounter::byteswap() {
  for (int x = 0; x < 8; x++)
    this->success_string_ids[x] = byteswap16(this->success_string_ids[x]);
  for (int x = 0; x < 8; x++)
    this->failure_string_ids[x] = byteswap16(this->failure_string_ids[x]);
  for (int x = 0; x < 8; x++)
    this->success_sound_ids[x] = byteswap16(this->success_sound_ids[x]);
  for (int x = 0; x < 8; x++)
    this->failure_sound_ids[x] = byteswap16(this->failure_sound_ids[x]);

  this->trap_spell = byteswap16(this->trap_spell);
  this->trap_damage_low = byteswap16(this->trap_damage_low);
  this->trap_damage_high = byteswap16(this->trap_damage_high);
  this->num_lock_tumblers = byteswap16(this->num_lock_tumblers);
  this->prompt_string = byteswap16(this->prompt_string);
  this->trap_sound = byteswap16(this->trap_sound);
  this->trap_spell_power_level = byteswap16(this->trap_spell_power_level);
  this->prompt_sound = byteswap16(this->prompt_sound);
  this->percent_per_level_to_open = byteswap16(this->percent_per_level_to_open);
  this->percent_per_level_to_disable = byteswap16(this->percent_per_level_to_disable);
};

vector<rogue_encounter> load_rogue_encounter_index(const string& filename) {
  return load_direct_file_data<rogue_encounter>(filename);
}

static const vector<string> rogue_encounter_action_names({
  "acrobatic_act", "detect_trap", "disable_trap", "action3", "force_lock",
  "action5", "pick_lock", "action7",
});

string disassemble_rogue_encounter(int index, const rogue_encounter& e,
    const vector<ecodes> ecodes, const vector<string>& strings) {

  string ret = string_printf("===== ROGUE ENCOUNTER id=%d\n", index);

  ret += string_printf("  prompt: sound=%d, text=%s\n", e.prompt_sound,
      render_string_reference(strings, e.prompt_string).c_str());

  for (int x = 0; x < 8; x++) {
    if (!e.actions_available[x])
      continue;
    ret += string_printf("  action_%s: pct_mod=%d succ_result=%d fail_result=%d "
        "succ_str=%s fail_str=%s succ_snd=%d fail_snd=%d\n",
        rogue_encounter_action_names[x].c_str(), e.percent_modify[x],
        e.success_result_codes[x], e.failure_result_codes[x],
        render_string_reference(strings, e.success_string_ids[x]).c_str(),
        render_string_reference(strings, e.failure_string_ids[x]).c_str(),
        e.success_sound_ids[x], e.failure_sound_ids[x]);
  }

  if (e.is_trapped)
    ret += string_printf("  trap: rogue_only=%d spell=%d spell_power=%d "
        "damage_range=[%d,%d] sound=%d\n", e.trap_affects_rogue_only,
        e.trap_spell, e.trap_spell_power_level, e.trap_damage_low,
        e.trap_damage_high, e.trap_sound);

  ret += string_printf("  pct_per_level_to_open_lock=%d\n",
      e.percent_per_level_to_open);
  ret += string_printf("  pct_per_level_to_disable_trap=%d\n",
      e.percent_per_level_to_disable);
  ret += string_printf("  num_lock_tumblers=%d\n",
      e.num_lock_tumblers);

  return ret;
}

string disassemble_all_rogue_encounters(const vector<rogue_encounter>& e,
    const vector<ecodes> ecodes, const vector<string>& strings) {
  string ret;
  for (size_t x = 0; x < e.size(); x++)
    ret += disassemble_rogue_encounter(x, e[x], ecodes, strings);
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// DATA TD3

void time_encounter::byteswap() {
  this->day = byteswap16(this->day);
  this->increment = byteswap16(this->increment);
  this->percent_chance = byteswap16(this->percent_chance);
  this->xap_id = byteswap16(this->xap_id);
  this->required_level = byteswap16(this->required_level);
  this->required_rect = byteswap16(this->required_rect);
  this->required_x = byteswap16(this->required_x);
  this->required_y = byteswap16(this->required_y);
  this->required_item_id = byteswap16(this->required_item_id);
  this->required_quest = byteswap16(this->required_quest);
  this->land_or_dungeon = byteswap16(this->land_or_dungeon);
}

vector<time_encounter> load_time_encounter_index(const string& filename) {
  return load_direct_file_data<time_encounter>(filename);
}

string disassemble_time_encounter(int index, const time_encounter& e) {

  string ret = string_printf("===== TIME ENCOUNTER id=%d\n", index);

  ret += string_printf("  day=%d\n", e.day);
  ret += string_printf("  increment=%d\n", e.increment);
  ret += string_printf("  percent_chance=%d\n", e.percent_chance);
  ret += string_printf("  xap_id=%d\n", e.xap_id);
  ret += string_printf("  required_level: id=%d (%s)\n", e.required_level,
      e.land_or_dungeon == 1 ? "land" : "dungeon");
  ret += string_printf("  required_rect=%d\n", e.required_rect);
  ret += string_printf("  required_pos=(%d,%d)\n", e.required_x, e.required_y);
  ret += string_printf("  required_rect=%d\n", e.required_rect);
  ret += string_printf("  required_item_id=%d\n", e.required_item_id);
  ret += string_printf("  required_quest=%d\n", e.required_quest);

  return ret;
}

string disassemble_all_time_encounters(const vector<time_encounter>& e) {
  string ret;
  for (size_t x = 0; x < e.size(); x++)
    ret += disassemble_time_encounter(x, e[x]);
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// DATA RD

bool random_rect::is_empty() const {
  return this->times_in_10k == 0;
}

struct random_rect_coords {
  int16_t top;
  int16_t left;
  int16_t bottom;
  int16_t right;
};

struct random_rect_battle_range {
  int16_t low;
  int16_t high;
};

struct map_metadata_file {
  random_rect_coords coords[20];
  int16_t times_in_10k[20];
  random_rect_battle_range battle_range[20];
  int16_t xap_num[20][3];
  int16_t xap_chance[20][3];
  int8_t land_type;
  int8_t unknown[0x16]; // seriously wut
  int8_t percent_option[20];
  int8_t unused;
  int16_t sound[20];
  int16_t text[20];
};

static const unordered_map<uint8_t, string> land_type_to_string({
  {0,  "outdoor"},
  {1,  "reserved1"},
  {2,  "reserved2"},
  {3,  "cave"},
  {4,  "indoor"},
  {5,  "desert"},
  {6,  "custom_1"},
  {7,  "custom_2"},
  {8,  "custom_3"},
  {9,  "abyss"},
  {10, "snow"},
});



vector<map_metadata> load_map_metadata_index(const string& filename) {
  FILE* f = fopen_or_throw(filename.c_str(), "rb");
  int num = num_elements_in_file(f, sizeof(map_metadata_file));

  vector<map_metadata_file> file_data(num);
  fread(file_data.data(), sizeof(map_metadata_file), num, f);

  vector<map_metadata> data(num);
  for (int x = 0; x < num; x++) {
    data[x].land_type = land_type_to_string.at(file_data[x].land_type);
    for (int y = 0; y < 20; y++) {
      random_rect r;
      r.top            = byteswap16(file_data[x].coords[y].top);
      r.left           = byteswap16(file_data[x].coords[y].left);
      r.bottom         = byteswap16(file_data[x].coords[y].bottom);
      r.right          = byteswap16(file_data[x].coords[y].right);
      r.times_in_10k   = byteswap16(file_data[x].times_in_10k[y]);
      r.battle_low     = byteswap16(file_data[x].battle_range[y].low);
      r.battle_high    = byteswap16(file_data[x].battle_range[y].high);
      r.xap_num[0]     = byteswap16(file_data[x].xap_num[y][0]);
      r.xap_num[1]     = byteswap16(file_data[x].xap_num[y][1]);
      r.xap_num[2]     = byteswap16(file_data[x].xap_num[y][2]);
      r.xap_chance[0]  = byteswap16(file_data[x].xap_chance[y][0]);
      r.xap_chance[1]  = byteswap16(file_data[x].xap_chance[y][1]);
      r.xap_chance[2]  = byteswap16(file_data[x].xap_chance[y][2]);
      r.percent_option = file_data[x].percent_option[y];
      r.sound          = byteswap16(file_data[x].sound[y]);
      r.text           = byteswap16(file_data[x].text[y]);
      data[x].random_rects.push_back(r);
    }
  }

  return data;
}

static void draw_random_rects(Image& map,
    const vector<random_rect>& random_rects, int tile_size, int xpoff,
    int ypoff, uint8_t r, uint8_t g, uint8_t b, uint8_t br, uint8_t bg,
    uint8_t bb, uint8_t ba) {

  for (size_t x = 0; x < random_rects.size(); x++) {
    if (random_rects[x].is_empty())
      continue;

    random_rect rect = random_rects[x];
    if (rect.left < 0)
      rect.left = 0;
    if (rect.right > 89)
      rect.right = 89;
    if (rect.top < 0)
      rect.top = 0;
    if (rect.bottom > 89)
      rect.bottom = 89;
    int xp_left = rect.left * tile_size + xpoff;
    int xp_right = rect.right * tile_size + tile_size - 1 + xpoff;
    int yp_top = rect.top * tile_size + ypoff;
    int yp_bottom = rect.bottom * tile_size + tile_size - 1 + ypoff;

    map.FillRect(xp_left, yp_top, xp_right - xp_left, yp_bottom - yp_top, r, g,
        b, 0x10);

    map.DrawHorizontalLine(xp_left, xp_right, yp_top, r, g, b);
    map.DrawHorizontalLine(xp_left, xp_right, yp_bottom, r, g, b);
    map.DrawVerticalLine(xp_left, yp_top, yp_bottom, r, g, b);
    map.DrawVerticalLine(xp_right, yp_top, yp_bottom, r, g, b);

    string rectinfo = string_printf("%d/10000", rect.times_in_10k);
    if (rect.battle_low || rect.battle_high)
      rectinfo += string_printf(" b=[%d,%d]", rect.battle_low, rect.battle_high);
    if (rect.percent_option)
      rectinfo += string_printf(" o=%d%%", rect.percent_option);
    if (rect.sound)
      rectinfo += string_printf(" s=%d", rect.sound);
    if (rect.text)
      rectinfo += string_printf(" t=%d", rect.text);
    for (int y = 0; y < 3; y++)
      if (rect.xap_num[y] && rect.xap_chance[y])
        rectinfo += string_printf(" a%d=%d,%d%%", y, rect.xap_num[y], rect.xap_chance[y]);

    map.DrawText(xp_left + 2, yp_bottom - 9, NULL, NULL, r, g, b, br, bg, bb,
        ba, "%s", rectinfo.c_str());
    map.DrawText(xp_left + 2, yp_bottom - 17, NULL, NULL, r, g, b, br, bg, bb,
        ba, "%d", x);
  }
}



////////////////////////////////////////////////////////////////////////////////
// DATA DD

void ap_info::byteswap() {
  this->location_code = byteswap32(this->location_code);
  for (int x = 0; x < 8; x++) {
    this->command_codes[x] = byteswap16(this->command_codes[x]);
    this->argument_codes[x] = byteswap16(this->argument_codes[x]);
  }
}

int8_t ap_info::get_x() const {
  if (this->location_code < 0)
    return -1;
  return this->location_code % 100;
}

int8_t ap_info::get_y() const {
  if (this->location_code < 0)
    return -1;
  return (this->location_code / 100) % 100;
}

int8_t ap_info::get_level_num() const {
  if (this->location_code < 0)
    return -1;
  return (this->location_code / 10000) % 100;
}

vector<vector<ap_info>> load_ap_index(const string& filename) {
  vector<ap_info> all_info = load_xap_index(filename);

  vector<vector<ap_info>> level_ap_info(all_info.size() / 100);
  for (size_t x = 0; x < all_info.size(); x++) {
    level_ap_info[x / 100].push_back(all_info[x]);
  }

  return level_ap_info;
}

vector<ap_info> load_xap_index(const string& filename) {
  FILE* f = fopen_or_throw(filename.c_str(), "rb");
  uint64_t num_aps = num_elements_in_file(f, sizeof(ap_info));

  vector<ap_info> all_info(num_aps);
  fread(all_info.data(), sizeof(ap_info), num_aps, f);
  fclose(f);

  for (auto& ap : all_info)
    ap.byteswap();

  return all_info;
}

// valid values for arguments field:
// 0: no arguments
// 1: 1 argument (no e-codes)
// 2-10: use e-codes (two consecutive if required)
// -1: 1 argument, but it comes from e-codes

struct opcode_info {
  const char* name;
  int16_t num_arguments;
  unordered_set<int> string_args;
};

static const unordered_map<int16_t, opcode_info> opcode_definitions({
  {  1, {"string",                1,  {0}}},
  {  2, {"battle",                5,  {3}}},
  {  3, {"option",                5,  {}}},
  { -3, {"option_link",           5,  {}}},
  {  4, {"simple_enc",            1,  {}}},
  {  5, {"complex_enc",           1,  {}}},
  {  6, {"shop",                  1,  {}}},
  {  7, {"modify_ap",             5,  {}}},
  {  8, {"use_ap",                2,  {}}},
  {  9, {"sound",                 1,  {}}},
  { 10, {"treasure",              1,  {}}},
  { 11, {"victory_pts",           1,  {}}},
  { 12, {"change_tile",           5,  {}}},
  { 13, {"enable_ap",             5,  {}}},
  { 14, {"pick_chars",            1,  {}}},
  { 15, {"heal_picked",           5,  {}}},
  { 16, {"heal_party",            5,  {}}},
  { 17, {"spell_picked",          4,  {}}},
  { 18, {"spell_party",           4,  {}}},
  { 19, {"rand_string",           2,  {0, 1}}},
  { 20, {"tele_and_run",          5,  {}}},
  { 21, {"jmp_if_item",           5,  {}}},
  {-21, {"jmp_if_item_link",      5,  {}}},
  { 22, {"change_item",           5,  {}}},
  { 23, {"change_rect",           5,  {}}},
  {-23, {"change_rect_dungeon",   5,  {}}},
  { 24, {"exit_ap",               0,  {}}},
  { 25, {"exit_ap_delete",        0,  {}}},
  { 26, {"mouse_click",           0,  {}}},
  { 27, {"picture",               1,  {}}},
  { 28, {"redraw",                0,  {}}},
  { 29, {"give_map",              1,  {}}},
  { 30, {"pick_ability",          4,  {}}},
  { 31, {"jmp_if_ability",        5,  {}}},
  {-31, {"jmp_if_ability_link",   5,  {}}},
  { 32, {"temple",                1,  {}}},
  { 33, {"take_gold",             5,  {}}},
  { 34, {"break_enc",             0,  {}}},
  { 35, {"simple_enc_del",        1,  {}}},
  { 36, {"stash_items",           1,  {}}},
  { 37, {"enter_dungeon",         5,  {}}},
  { 38, {"jmp_if_item_enc",       5,  {}}},
  { 39, {"jmp_xap",               1,  {}}},
  { 40, {"jmp_party_cond",        4,  {}}},
  {-40, {"jmp_party_cond_link",   4,  {}}},
  { 41, {"simple_enc_del_any",    2,  {}}},
  { 42, {"jmp_random",            5,  {}}},
  {-42, {"jmp_random_link",       5,  {}}},
  { 43, {"give_cond",             4,  {}}},
  { 44, {"complex_enc_del",       1,  {}}},
  { 45, {"tele",                  4,  {}}},
  { 46, {"jmp_quest",             5,  {}}},
  {-46, {"jmp_quest_link",        5,  {}}},
  { 47, {"set_quest",             1,  {}}},
  { 48, {"pick_battle",           5,  {}}},
  { 49, {"bank",                  0,  {}}},
  { 50, {"pick_attribute",        5,  {}}},
  { 51, {"change_shop",           4,  {}}},
  { 52, {"pick_misc",             3,  {}}},
  { 53, {"pick_caste",            3,  {}}},
  { 54, {"change_time_enc",       5,  {}}},
  { 55, {"jmp_picked",            5,  {}}},
  {-55, {"jmp_picked_link",       5,  {}}},
  { 56, {"jmp_battle",            5,  {4}}},
  {-56, {"jmp_battle_link",       5,  {4}}},
  { 57, {"change_tileset",        3,  {}}},
  { 58, {"jmp_difficulty",        5,  {}}},
  {-58, {"jmp_difficulty_link",   5,  {}}},
  { 59, {"jmp_tile",              5,  {}}},
  {-59, {"jmp_tile_link",         5,  {}}},
  { 60, {"drop_money",            2,  {}}},
  { 61, {"incr_party_loc",        4,  {}}},
  { 62, {"story",                 1,  {}}},
  { 63, {"change_time",           4,  {}}},
  { 64, {"jmp_time",              5,  {}}},
  {-64, {"jmp_time_link",         5,  {}}},
  { 65, {"give_rand_item",        3,  {}}},
  { 66, {"allow_camping",         1,  {}}},
  { 67, {"jmp_item_charge",       5,  {}}},
  {-67, {"jmp_item_charge_link",  5,  {}}},
  { 68, {"change_fatigue",        2,  {}}},
  { 69, {"change_casting_flags",  3,  {}}}, // apparently e-code 4 isn't used and 5 must always be 1? ok whatever
  { 70, {"save_restore_loc",      -1, {}}},
  { 71, {"enable_coord_display",  1,  {}}},
  { 72, {"jmp_quest_range",       5,  {}}},
  {-72, {"jmp_quest_range_link",  5,  {}}},
  { 73, {"shop_restrict",         5,  {}}},
  { 74, {"give_spell_pts_picked", 3,  {}}},
  { 75, {"jmp_spell_pts",         5,  {}}},
  {-75, {"jmp_spell_pts_link",    5,  {}}},
  { 76, {"incr_quest_value",      5,  {}}},
  { 77, {"jmp_quest_value",       5,  {}}},
  {-77, {"jmp_quest_value_link",  5,  {}}},
  { 78, {"jmp_tile_params",       5,  {}}},
  {-78, {"jmp_tile_params_link",  5,  {}}},
  { 81, {"jmp_char_cond",         5,  {2}}},
  {-81, {"jmp_char_cond_link",    5,  {2}}},
  { 82, {"enable_turning",        0,  {}}},
  { 83, {"disable_turning",       0,  {}}},
  { 85, {"jmp_random_xap",        5,  {}}},
  {-85, {"jmp_random_xap_link",   5,  {}}},
  { 86, {"jmp_misc",              5,  {}}},
  {-86, {"jmp_misc_link",         5,  {}}},
  { 87, {"jmp_npc",               5,  {}}},
  {-87, {"jmp_npc_link",          5,  {}}},
  { 88, {"drop_npc",              1,  {}}},
  { 89, {"add_npc",               1,  {}}},
  { 90, {"take_victory_pts",      2,  {}}},
  { 91, {"drop_all_items",        0,  {}}},
  { 92, {"change_rect_size",      9,  {}}},
  { 93, {"enable_compass",        0,  {}}},
  { 94, {"disable_compass",       0,  {}}},
  { 95, {"change_direction",      1,  {}}},
  { 96, {"disable_dungeon_map",   0,  {}}},
  { 97, {"enable_dungeon_map",    0,  {}}},
  {100, {"end_battle",            0,  {}}},
  {101, {"back_up",               0,  {}}},
  {102, {"level_up_picked",       0,  {}}},
  {103, {"cont_boat_camping",     3,  {}}},
  {104, {"disable_rand_battles",  1,  {}}},
  {105, {"enable_allies",         1,  {}}},
  {106, {"set_dark_los",          4,  {}}},
  {107, {"pick_battle_2",         5,  {3}}},
  {108, {"change_picked",         2,  {}}},
  {111, {"ret",                   0,  {}}},
  {112, {"pop_ret",               0,  {}}},
  {119, {"revive_npc_after",      1,  {}}},
  {120, {"change_monster",        5,  {}}},
  {121, {"kill_lower_undead",     0,  {}}},
  {122, {"fumble_weapon",         2,  {}}},
  {123, {"rout_monsters",         5,  {}}},
  {124, {"summon_monster",        4,  {}}},
  {125, {"destroy_related",       2,  {}}},
  {126, {"macro_criteria",        5,  {}}},
  {127, {"cont_monster_present",  1,  {}}},
});

string disassemble_opcode(int16_t ap_code, int16_t arg_code,
    const vector<ecodes>& ecodes, const vector<string>& strings) {

  if (opcode_definitions.count(ap_code) == 0)
    return "[bad opcode]";

  const char* name = opcode_definitions.at(ap_code).name;
  int num_args = opcode_definitions.at(ap_code).num_arguments;
  switch (num_args) {
    case 0:
      return name;
    case 1:
      if (arg_code &&
          opcode_definitions.at(ap_code).string_args.count(0) &&
          ((size_t)abs(arg_code) < strings.size()))
        return string_printf("%-24s %s", name, render_string_reference(strings,
            arg_code).c_str());
      return string_printf("%-24s %d", name, arg_code);
    case -1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10: {
      string ret;

      bool ecodes_id_negative = arg_code < 0;
      if (ecodes_id_negative)
        arg_code *= -1;

      if (ecodes_id_negative)
        ret = string_printf("%-24s (-1)", name);
      else
        ret = string_printf("%-24s ", name);

      if ((size_t)arg_code >= ecodes.size()) {
        if (ecodes_id_negative)
          ret += ", ";
        ret += string_printf("[bad ecode id %04X]", arg_code);
        return ret;
      }

      // hack: arg 4 in these opcodes *sometimes* is a string id
      int16_t abs_ap_code = abs(ap_code);
      unordered_set<int> string_args = opcode_definitions.at(ap_code).string_args;
      if ((abs_ap_code == 21 || abs_ap_code == 55 || abs_ap_code == 87) &&
          ecodes[arg_code].data[2] == 2)
        string_args.insert(4);

      for (int x = 0; x < num_args; x++) {
        if (x > 0 || ecodes_id_negative)
          ret += ", ";
        int16_t arg_value = ecodes[arg_code].data[x];
        if (arg_value && string_args.count(x) &&
            ((size_t)abs(arg_value) < strings.size()))
          ret += render_string_reference(strings, arg_value);
        else
          ret += string_printf("%d", arg_value);
      }
      return ret;
    }
  }
  return "[internal error]";
}

string disassemble_ap(int16_t level_num, int16_t ap_num, const ap_info& ap,
    const vector<ecodes>& ecodes, const vector<string>& strings) {

  string data = string_printf("==== AP level=%d id=%d x=%d y=%d to_level=%d to_x=%d to_y=%d prob=%d\n",
      level_num, ap_num, ap.get_x(), ap.get_y(), ap.to_level, ap.to_x, ap.to_y, ap.percent_chance);
  for (int x = 0; x < 8; x++)
    if (ap.command_codes[x] || ap.argument_codes[x])
      data += string_printf("  %01d> %s\n", x, disassemble_opcode(
          ap.command_codes[x], ap.argument_codes[x], ecodes, strings).c_str());

  return data;
}

string disassemble_level_aps(int16_t level_num, const vector<ap_info>& aps,
    const vector<ecodes>& ecodes, const vector<string>& strings) {
  string ret;
  for (size_t x = 0; x < aps.size(); x++)
    ret += disassemble_ap(level_num, x, aps[x], ecodes, strings);
  return ret;
}

string disassemble_all_aps(const vector<vector<ap_info>>& aps,
    const vector<ecodes>& ecodes, const vector<string>& strings) {
  string ret;
  for (size_t x = 0; x < aps.size(); x++)
    ret += disassemble_level_aps(x, aps[x], ecodes, strings);
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// DATA DL

static uint16_t location_sig(uint8_t x, uint8_t y) {
  return ((uint16_t)x << 8) | y;
}

void map_data::byteswap() {
  for (int x = 0; x < 90; x++)
    for (int y = 0; y < 90; y++)
      this->data[x][y] = byteswap16(this->data[x][y]);
}

void map_data::transpose() {
  for (int y = 0; y < 90; y++) {
    for (int x = y + 1; x < 90; x++) {
      int16_t t = this->data[y][x];
      this->data[y][x] = this->data[x][y];
      this->data[x][y] = t;
    }
  }
}

vector<map_data> load_dungeon_map_index(const string& filename) {
  FILE* f = fopen_or_throw(filename.c_str(), "rb");
  int num_maps = num_elements_in_file(f, sizeof(map_data));

  vector<map_data> data(num_maps);
  fread(data.data(), sizeof(map_data), num_maps, f);
  for (auto& m : data)
    m.byteswap();

  return data;
}

static const unordered_map<int16_t, char> dungeon_ascii_of_data({
  {0x0000, ' '},
  {0x0001, '*'},
  {0x0002, '-'},
  {0x0003, '-'},
  {0x0004, '|'},
  {0x0005, '|'},
  {0x0008, 'S'},
  {0x0009, 'S'},
  {0x0F00, 'Q'},
  {0x0F01, 'Q'},
});

static Image dungeon_pattern(1, 1);

Image generate_dungeon_map(const map_data& mdata, const map_metadata& metadata,
    const vector<ap_info>& aps, int level_num) {

  Image map(90 * 16, 90 * 16);
  int pattern_x = 576, pattern_y = 320;

  unordered_map<uint16_t, vector<int>> loc_to_ap_nums;
  for (size_t x = 0; x < aps.size(); x++)
    loc_to_ap_nums[location_sig(aps[x].get_x(), aps[x].get_y())].push_back(x);

  for (int y = 89; y >= 0; y--) {
    for (int x = 89; x >= 0; x--) {
      int16_t data = mdata.data[y][x];

      int xp = x * 16;
      int yp = y * 16;
      map.FillRect(xp, yp, 16, 16, 0, 0, 0, 0xFF);
      if (data & DUNGEON_TILE_WALL)
        map.MaskBlit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 0,
            pattern_y + 0, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_VERT_DOOR)
        map.MaskBlit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 16,
            pattern_y + 0, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_HORIZ_DOOR)
        map.MaskBlit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 32,
            pattern_y + 0, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_STAIRS)
        map.MaskBlit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 48,
            pattern_y + 0, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_COLUMNS)
        map.MaskBlit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 0,
            pattern_y + 16, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_SECRET_UP)
        map.MaskBlit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 0,
            pattern_y + 32, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_SECRET_RIGHT)
        map.MaskBlit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 16,
            pattern_y + 32, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_SECRET_DOWN)
        map.MaskBlit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 32,
            pattern_y + 32, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_SECRET_LEFT)
        map.MaskBlit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 48,
            pattern_y + 32, 0xFF, 0xFF, 0xFF);

      int text_xp = xp + 1;
      int text_yp = yp + 1;

      // draw the coords if both are multiples of 10
      if (y % 10 == 0 && x % 10 == 0) {
        map.DrawText(text_xp, text_yp, NULL, NULL, 0xFF, 0xFF, 0xFF, 0, 0, 0,
            0x80, "%d,%d", x, y);
        text_yp += 8;
      }

      for (const auto& ap_num : loc_to_ap_nums[location_sig(x, y)]) {
        map.DrawText(text_xp, text_yp, NULL, NULL, 0xFF, 0xFF, 0xFF, 0, 0, 0,
            0x80, "%d", ap_num);
        text_yp += 8;
      }
    }
  }

  // finally, draw random rects
  draw_random_rects(map, metadata.random_rects, 16, 0, 0, 0xFF, 0xFF, 0xFF, 0,
      0, 0, 0x80);

  return map;
}



////////////////////////////////////////////////////////////////////////////////
// DATA LD

vector<map_data> load_land_map_index(const string& filename) {
  // format is the same as for dungeons, except it's in column-major order
  vector<map_data> data = load_dungeon_map_index(filename);
  for (auto& m : data)
    m.transpose();

  return data;
}

static const unordered_map<string, int16_t> land_type_to_background_data({
  {"outdoor", 0x009B},
  {"abyss",   0x009B},
  {"cave",    0x009B},
  {"desert",  0x00BF},
  {"indoor",  0x006F},
  {"snow",    0x009B},
});

static const unordered_map<string, int16_t> land_type_to_resource_id({
  {"custom_1", 306},
  {"custom_2", 307},
  {"custom_3", 308},
});

unordered_set<string> all_land_types() {
  unordered_set<string> all;
  for (const auto& it : land_type_to_background_data)
    all.insert(it.first);
  return all;
}

static unordered_map<int16_t, Image> default_negative_tile_image_cache;
static unordered_map<int16_t, Image> scenario_negative_tile_image_cache;
static unordered_map<string, Image> positive_pattern_cache;

void populate_image_caches(const string& the_family_jewels_name) {
  vector<pair<uint32_t, int16_t>> all_resources = enum_file_resources(
      the_family_jewels_name.c_str());

  for (const auto& it : all_resources) {
    if (it.first == RESOURCE_TYPE_CICN) {
      try {
        void* data;
        size_t size;
        load_resource_from_file(the_family_jewels_name.c_str(), it.first,
            it.second, &data, &size);
        try {
          default_negative_tile_image_cache.emplace(it.second,
              decode_cicn32(data, size, 0xFF, 0xFF, 0xFF));
        } catch (const runtime_error& e) {
          fprintf(stderr, "warning: failed to decode default cicn %d: %s\n",
              it.second, e.what());
        }
        free(data);

      } catch (const runtime_error& e) {
        fprintf(stderr, "warning: failed to load resource %08X:%d: %s\n",
            it.first, it.second, e.what());
      }
    }

    if (it.first == RESOURCE_TYPE_PICT) {
      string land_type;
      if (it.second == 300)
        land_type = "outdoor";
      else if (it.second == 302)
        land_type = "dungeon";
      else if (it.second == 303)
        land_type = "cave";
      else if (it.second == 304)
        land_type = "indoor";
      else if (it.second == 305)
        land_type = "desert";
      else if (it.second == 309)
        land_type = "abyss";
      else if (it.second == 310)
        land_type = "snow";

      if (land_type.size()) {
        try {
          void* data;
          size_t size;
          load_resource_from_file(the_family_jewels_name.c_str(), it.first,
              it.second, &data, &size);
          try {
            positive_pattern_cache.emplace(land_type, decode_pict(data, size));
            if (!land_type.compare("dungeon"))
              dungeon_pattern = positive_pattern_cache.at(land_type);
          } catch (const runtime_error& e) {
            fprintf(stderr, "warning: failed to decode default pict %d: %s\n",
                it.second, e.what());
          }
          free(data);

        } catch (const runtime_error& e) {
          fprintf(stderr, "warning: failed to load resource %08X:%d: %s\n",
              it.first, it.second, e.what());
        }
      }
    }
  }
}

void add_custom_pattern(const string& land_type, Image& img) {
  positive_pattern_cache.emplace(land_type, img);
}

Image generate_land_map(const map_data& mdata, const map_metadata& metadata,
    const vector<ap_info>& aps, int level_num, const level_neighbors& n,
    int16_t start_x, int16_t start_y, const string& rsf_file) {

  unordered_map<uint16_t, vector<int>> loc_to_ap_nums;
  for (size_t x = 0; x < aps.size(); x++)
    loc_to_ap_nums[location_sig(aps[x].get_x(), aps[x].get_y())].push_back(x);

  int horizontal_neighbors = (n.left != -1 ? 1 : 0) + (n.right != -1 ? 1 : 0);
  int vertical_neighbors = (n.top != -1 ? 1 : 0) + (n.bottom != -1 ? 1 : 0);

  int16_t background_data;
  if (land_type_to_background_data.count(metadata.land_type))
    background_data = land_type_to_background_data.at(metadata.land_type);
  else
    background_data = 0;
  Image map(90 * 32 + horizontal_neighbors * 9, 90 * 32 + vertical_neighbors * 9);

  // write neighbor directory
  if (n.left != -1) {
    string text = string_printf("TO LEVEL %d", n.left);
    for (int y = (n.top != -1 ? 10 : 1); y < 90 * 32; y += 10 * 32) {
      for (size_t yy = 0; yy < text.size(); yy++) {
        map.DrawText(2, y + 9 * yy, NULL, NULL, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0xFF, "%c", text[yy]);
      }
    }
  }
  if (n.right != -1) {
    string text = string_printf("TO LEVEL %d", n.right);
    int x = 32 * 90 + (n.left != -1 ? 11 : 2);
    for (int y = (n.top != -1 ? 10 : 1); y < 90 * 32; y += 10 * 32) {
      for (size_t yy = 0; yy < text.size(); yy++) {
        map.DrawText(x, y + 9 * yy, NULL, NULL, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0xFF, "%c", text[yy]);
      }
    }
  }
  if (n.top != -1) {
    string text = string_printf("TO LEVEL %d", n.top);
    for (int x = (n.left != -1 ? 10 : 1); x < 90 * 32; x += 10 * 32) {
      map.DrawText(x, 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0xFF, "%s", text.c_str());
    }
  }
  if (n.bottom != -1) {
    string text = string_printf("TO LEVEL %d", n.bottom);
    int y = 32 * 90 + (n.top != -1 ? 10 : 1);
    for (int x = (n.left != -1 ? 10 : 1); x < 90 * 32; x += 10 * 32) {
      map.DrawText(x, y, NULL, NULL, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0xFF, "%s", text.c_str());
    }
  }

  // load the positive pattern
  Image positive_pattern(1, 1);
  if (background_data == 0) { // custom pattern
    if (land_type_to_resource_id.count(metadata.land_type) == 0)
      throw runtime_error("unknown custom land type");

    int16_t resource_id = land_type_to_resource_id.at(metadata.land_type);
    void* image_data;
    size_t image_size;
    load_resource_from_file(rsf_file.c_str(), RESOURCE_TYPE_PICT, resource_id,
        &image_data, &image_size);
    positive_pattern = decode_pict(image_data, image_size);

  } else { // default pattern
    positive_pattern = positive_pattern_cache.at(metadata.land_type);
  }

  if (positive_pattern.Width() != 640 || positive_pattern.Height() != 320)
    throw runtime_error("positive pattern is the wrong size");

  for (int y = 0; y < 90; y++) {
    for (int x = 0; x < 90; x++) {
      int16_t data = mdata.data[y][x];

      bool has_ap = false;
      while (data <= -1000) {
        data += 1000;
        has_ap = true;
      }
      while (data > 1000) {
        data -= 1000;
        has_ap = true;
      }

      int xp = x * 32 + (n.left != -1 ? 9 : 0);
      int yp = y * 32 + (n.top != -1 ? 9 : 0);
      int text_xp = xp + 2;
      int text_yp = yp + 2;

      // draw the tile itself
      if (data < 0 || data > 200) { // masked tile

        // first try to construct it from the scenario resources
        if (scenario_negative_tile_image_cache.count(data) == 0) {
          try {
            void* image_data;
            size_t image_size;
            load_resource_from_file(rsf_file.c_str(), RESOURCE_TYPE_CICN,
                data, &image_data, &image_size);
            try {
              scenario_negative_tile_image_cache.emplace(data,
                  decode_cicn32(image_data, image_size, 0xFF, 0xFF, 0xFF));
            } catch (const runtime_error& e) {
              fprintf(stderr, "warning: failed to decode cicn %d: %s\n", data,
                  e.what());
            }
            free(image_data);
          } catch (const runtime_error& e) { }
        }

        // then copy it from the default resources if necessary
        if (scenario_negative_tile_image_cache.count(data) == 0 && 
            default_negative_tile_image_cache.count(data) != 0) {
          scenario_negative_tile_image_cache.emplace(data,
              default_negative_tile_image_cache.at(data));
        }

        // if we still don't have a tile, draw an error tile
        if (scenario_negative_tile_image_cache.count(data) == 0) {
          map.FillRect(xp, yp, 32, 32, 0, 0, 0, 0xFF);
          map.DrawText(text_xp, text_yp, NULL, NULL, 0xFF, 0xFF, 0xFF, 0, 0, 0,
              0x80, "%04hX", data);
          text_yp += 8;

        } else {
          if (background_data) {
            int source_id = background_data - 1;
            int sxp = (source_id % 20) * 32;
            int syp = (source_id / 20) * 32;
            map.Blit(positive_pattern, xp, yp, 32, 32, sxp, syp);
          } else
            map.FillRect(xp, yp, 32, 32, 0, 0, 0, 0xFF);

          map.MaskBlit(scenario_negative_tile_image_cache.at(data), xp, yp, 32,
              32, 0, 0, 0xFF, 0xFF, 0xFF);
        }

      } else if (data <= 200) { // standard tile
        int source_id = data - 1;
        int sxp = (source_id % 20) * 32;
        int syp = (source_id / 20) * 32;
        map.Blit(positive_pattern, xp, yp, 32, 32, sxp, syp);
      }

      // draw a red border if it has an AP
      if (has_ap) {
        map.DrawHorizontalLine(xp, xp + 31, yp, 0xFF, 0, 0);
        map.DrawHorizontalLine(xp, xp + 31, yp + 31, 0xFF, 0, 0);
        map.DrawVerticalLine(xp, yp, yp + 31, 0xFF, 0, 0);
        map.DrawVerticalLine(xp + 31, yp, yp + 31, 0xFF, 0, 0);
      }

      // draw the coords if both are multiples of 10
      if (y % 10 == 0 && x % 10 == 0) {
        map.DrawText(text_xp, text_yp, NULL, NULL, 0xFF, 0xFF, 0xFF, 0, 0, 0,
            0x80, "%d,%d", x, y);
        text_yp += 8;
      }

      // draw "START" if this is the start loc
      if (x == start_x && y == start_y) {
        map.DrawText(text_xp, text_yp, NULL, NULL, 0, 0xFF, 0xFF, 0, 0, 0,
            0x80, "START");
        text_yp += 8;
      }

      // draw APs if present
      for (const auto& ap_num : loc_to_ap_nums[location_sig(x, y)]) {
        map.DrawText(text_xp, text_yp, NULL, NULL, 0xFF, 0xFF, 0xFF, 0, 0, 0,
            0x80, "%d", ap_num);
        text_yp += 8;
      }
    }
  }

  // finally, draw random rects
  draw_random_rects(map, metadata.random_rects, 32, (n.left != -1 ? 9 : 0),
      (n.top != -1 ? 9 : 0), 0xFF, 0xFF, 0xFF, 0, 0, 0, 0x80);

  return map;
}



////////////////////////////////////////////////////////////////////////////////
// DATA SD2

vector<string> load_string_index(const string& filename) {
  FILE* f = fopen_or_throw(filename.c_str(), "rb");

  vector<string> all_strings;
  while (!feof(f)) {

    string s;
    uint8_t len;
    int x;
    len = fgetc(f);
    for (x = 0; x < len; x++)
      s += fgetc(f);
    for (; x < 0xFF; x++)
      fgetc(f);

    all_strings.push_back(s);
  }

  fclose(f);
  return all_strings;
}
