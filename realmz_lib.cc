#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "Image.hh"

#include "realmz_lib.hh"

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
  for (char c : s) {
    if (c == '\"')
      ret += "\\\"";
    else
      ret += c;
  }
  return ret;
}

const char* first_file_that_exists(const char* fname, ...) {
  struct stat st;
  if (stat(fname, &st))
    return fname;

  va_list va;
  va_start(va, fmt);
  const char* ret = NULL;
  while (!ret && (fname = va_arg(va, const char*)))
    if (stat(fname, &st))
      ret = fname;
  va_end(va);

  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// DATA EDCD

void ecodes::byteswap() {
  for (int x = 0; x < 5; x++)
    this->data[x] = byteswap16(this->data[x]);
}

vector<ecodes> load_ecodes_index(const string& filename) {
  FILE* f = fopen_or_throw(filename.c_str(), "rb");
  int num_codes = num_elements_in_file(f, sizeof(int16_t) * 5);

  vector<ecodes> data(num_codes);
  fread(data.data(), sizeof(ecodes), num_codes, f);
  for (auto& ecode : data)
    ecode.byteswap();

  return data;
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
  {6,  "custom1"},
  {7,  "custom2"},
  {8,  "custom3"},
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
    const vector<random_rect>& random_rects, int tile_size, uint8_t r,
    uint8_t g, uint8_t b, uint8_t br, uint8_t bg, uint8_t bb, uint8_t ba) {

  for (size_t x = 0; x < random_rects.size(); x++) {
    if (random_rects[x].is_empty())
      continue;

    const random_rect& rect = random_rects[x];
    int xp_left = rect.left * tile_size;
    int xp_right = rect.right * tile_size + tile_size - 1;
    int yp_top = rect.top * tile_size;
    int yp_bottom = rect.bottom * tile_size + tile_size - 1;

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
    const vector<ecodes>& ecodes, const vector<string>* strings) {

  if (opcode_definitions.count(ap_code) == 0)
    return "[bad opcode]";

  const char* name = opcode_definitions.at(ap_code).name;
  int num_args = opcode_definitions.at(ap_code).num_arguments;
  switch (num_args) {
    case 0:
      return name;
    case 1:
      if (arg_code && strings &&
          opcode_definitions.at(ap_code).string_args.count(0) &&
          ((size_t)abs(arg_code) < strings->size()))
        return string_printf("%-24s \"%s\"#%d", name,
            escape_quotes((*strings)[abs(arg_code)]).c_str(), arg_code);
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
        if (arg_value && strings && string_args.count(x) &&
            ((size_t)abs(arg_value) < strings->size()))
          ret += string_printf("\"%s\"#%d",
              escape_quotes((*strings)[abs(arg_value)]).c_str(), arg_value);
        else
          ret += string_printf("%d", arg_value);
      }
      return ret;
    }
  }
  return "[internal error]";
}

string disassemble_ap(int16_t level_num, int16_t ap_num, const ap_info& ap,
    const vector<ecodes>& ecodes, const vector<string>* strings) {

  string data = string_printf("==== AP level=%d id=%d x=%d y=%d to_level=%d to_x=%d to_y=%d prob=%d\n",
      level_num, ap_num, ap.get_x(), ap.get_y(), ap.to_level, ap.to_x, ap.to_y, ap.percent_chance);
  for (int x = 0; x < 8; x++)
    if (ap.command_codes[x] || ap.argument_codes[x])
      data += string_printf("  %01d> %s\n", x, disassemble_opcode(
          ap.command_codes[x], ap.argument_codes[x], ecodes, strings).c_str());

  return data;
}

string disassemble_level_aps(int16_t level_num, const vector<ap_info>& aps,
    const vector<ecodes>& ecodes, const vector<string>* strings) {
  string ret;
  for (size_t x = 0; x < aps.size(); x++)
    ret += disassemble_ap(level_num, x, aps[x], ecodes, strings);
  return ret;
}

string disassemble_all_aps(const vector<vector<ap_info>>& aps,
    const vector<ecodes>& ecodes, const vector<string>* strings) {
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

Image generate_dungeon_map(const map_data& mdata, const map_metadata& metadata,
    const vector<ap_info>& aps, int level_num) {

  Image map(90 * 16, 90 * 16);
  Image pattern("patterns/dungeon.ppm");

  unordered_map<uint16_t, vector<int>> loc_to_ap_nums;
  for (size_t x = 0; x < aps.size(); x++)
    loc_to_ap_nums[location_sig(aps[x].get_x(), aps[x].get_y())].push_back(x);

  for (int y = 0; y < 90; y++) {
    for (int x = 0; x < 90; x++) {
      int16_t data = mdata.data[y][x];
      if (data & DUNGEON_TILE_SECRET_ANY)
        data |= DUNGEON_TILE_SECRET_ANY;

      int xp = x * 16;
      int yp = y * 16;
      map.FillRect(xp, yp, 16, 16, 0, 0, 0, 0xFF);
      if (data & DUNGEON_TILE_WALL)
        map.MaskBlit(pattern, xp, yp, 16, 16, 0, 0, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_VERT_DOOR)
        map.MaskBlit(pattern, xp, yp, 16, 16, 16, 0, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_HORIZ_DOOR)
        map.MaskBlit(pattern, xp, yp, 16, 16, 32, 0, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_STAIRS)
        map.MaskBlit(pattern, xp, yp, 16, 16, 48, 0, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_COLUMNS)
        map.MaskBlit(pattern, xp, yp, 16, 16, 0, 16, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_SECRET_ANY)
        map.MaskBlit(pattern, xp, yp, 16, 16, 32, 16, 0xFF, 0xFF, 0xFF);
      if (data & DUNGEON_TILE_UNMAPPED)
        map.MaskBlit(pattern, xp, yp, 16, 16, 48, 16, 0xFF, 0xFF, 0xFF);

      int text_xp = xp + 1;
      int text_yp = yp + 1;
      for (const auto& ap_num : loc_to_ap_nums[location_sig(x, y)]) {
        map.DrawText(text_xp, text_yp, NULL, NULL, 0xFF, 0xFF, 0xFF, 0, 0, 0,
            0x80, "%d", ap_num);
        text_yp += 8;
      }
    }
  }

  // finally, draw random rects
  draw_random_rects(map, metadata.random_rects, 16, 0xFF, 0xFF, 0xFF, 0, 0, 0,
      0x80);

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
  {"desert",  0x009B},
  {"indoor",  0x006F},
  {"snow",    0x009B},
});

unordered_set<string> all_land_types() {
  unordered_set<string> all;
  for (const auto& it : land_type_to_background_data)
    all.insert(it.first);
  return all;
}

static unordered_map<int16_t, Image> negative_tile_image_cache;
static unordered_map<string, Image> positive_pattern_cache;

Image generate_land_map(const map_data& mdata, const map_metadata& metadata,
    const vector<ap_info>& aps, int level_num) {

  unordered_map<uint16_t, vector<int>> loc_to_ap_nums;
  for (size_t x = 0; x < aps.size(); x++)
    loc_to_ap_nums[location_sig(aps[x].get_x(), aps[x].get_y())].push_back(x);

  int16_t background_data = land_type_to_background_data.at(metadata.land_type);
  Image map(90 * 32, 90 * 32);

  string positive_pattern_name = "patterns/" + metadata.land_type + ".ppm";
  if (positive_pattern_cache.count(positive_pattern_name) == 0) {
    positive_pattern_cache.emplace(positive_pattern_name, positive_pattern_name.c_str());
  }
  Image& positive_pattern = positive_pattern_cache.at(positive_pattern_name);

  for (int y = 0; y < 90; y++) {
    for (int x = 0; x < 90; x++) {
      int16_t data = mdata.data[y][x];

      bool has_ap = false;
      bool use_negative_tile = false;
      if (data <= 0) {
        data = -data;
        use_negative_tile = true;
      }
      while (data > 1000) {
        data -= 1000;
        has_ap = true;
      }

      int xp = x * 32;
      int yp = y * 32;
      int text_xp = xp + 2;
      int text_yp = yp + 2;

      // draw the tile itself
      if (use_negative_tile) { // masked tile
        if (negative_tile_image_cache.count(data) == 0) {
          try {
            negative_tile_image_cache.emplace(data,
                string_printf("patterns/tile_%d.bmp", -data).c_str());
          } catch (const runtime_error& e) { }
        }

        if (negative_tile_image_cache.count(data) == 0) {
          map.FillRect(xp, yp, 32, 32, 0, 0, 0, 0xFF);
          map.DrawText(text_xp, text_yp, NULL, NULL, 0xFF, 0xFF, 0xFF, 0, 0, 0,
              0x80, "%04hX", -data);
          text_yp += 8;

        } else {
          int source_id = background_data - 1;
          int sxp = (source_id % 20) * 32;
          int syp = (source_id / 20) * 32;
          map.Blit(positive_pattern, xp, yp, 32, 32, sxp, syp);
          map.MaskBlit(negative_tile_image_cache.at(data), xp, yp, 32, 32, 0, 0, 0xFF, 0xFF, 0xFF);
        }

      } else if (data <= 200) { // standard tile
        int source_id = data - 1;
        int sxp = (source_id % 20) * 32;
        int syp = (source_id / 20) * 32;
        map.Blit(positive_pattern, xp, yp, 32, 32, sxp, syp);

      } else { // monster or some shit
        map.FillRect(xp, yp, 32, 32, 0, 0, 0, 0xFF);
        map.DrawText(text_xp, text_yp, NULL, NULL, 0xFF, 0xFF, 0xFF, 0, 0, 0,
            0x80, "%04hX", data);
        text_yp += 8;
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

      // draw APs if present
      for (const auto& ap_num : loc_to_ap_nums[location_sig(x, y)]) {
        map.DrawText(text_xp, text_yp, NULL, NULL, 0xFF, 0xFF, 0xFF, 0, 0, 0,
            0x80, "%d", ap_num);
        text_yp += 8;
      }
    }
  }

  // finally, draw random rects
  draw_random_rects(map, metadata.random_rects, 32, 0xFF, 0xFF, 0xFF, 0, 0, 0,
      0x80);

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
