#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "Image.hh"

using namespace std;

int16_t byteswap16(int16_t a);
int32_t byteswap16(int32_t a);
FILE* fopen_or_throw(const char* fname, const char* mode);
uint64_t num_elements_in_file(FILE* f, size_t size);
string string_printf(const char* fmt, ...);
string escape_quotes(const string& s);
string first_file_that_exists(const char* fname, ...);



////////////////////////////////////////////////////////////////////////////////
// DATA EDCD

struct ecodes {
  int16_t data[5];

  void byteswap();
};

vector<ecodes> load_ecodes_index(const string& filename);



////////////////////////////////////////////////////////////////////////////////
// DATA RD

struct random_rect {
  int16_t top;
  int16_t left;
  int16_t bottom;
  int16_t right;
  int16_t times_in_10k;
  int16_t battle_low;
  int16_t battle_high;
  int16_t xap_num[3];
  int16_t xap_chance[3];
  int8_t percent_option;
  int16_t sound;
  int16_t text;

  bool is_empty() const;
};

struct map_metadata {
  string land_type;
  vector<random_rect> random_rects;
};

vector<map_metadata> load_map_metadata_index(const string& filename);



////////////////////////////////////////////////////////////////////////////////
// DATA DD
// DATA ED3

struct ap_info {
  int32_t location_code;
  uint8_t to_level;
  uint8_t to_x;
  uint8_t to_y;
  uint8_t percent_chance;
  int16_t command_codes[8];
  int16_t argument_codes[8];

  void byteswap();
  int8_t get_x() const;
  int8_t get_y() const;
  int8_t get_level_num() const;
};

vector<vector<ap_info>> load_ap_index(const string& filename);
vector<ap_info> load_xap_index(const string& filename);
string disassemble_opcode(int16_t ap_code, int16_t arg_code,
    const vector<ecodes>& ecodes, const vector<string>* strings);
string disassemble_ap(int16_t level_num, int16_t ap_num, const ap_info& ap,
    const vector<ecodes>& ecodes, const vector<string>* strings);
string disassemble_level_aps(int16_t level_num, const vector<ap_info>& aps,
    const vector<ecodes>& ecodes, const vector<string>* strings);
string disassemble_all_aps(const vector<vector<ap_info>>& aps,
    const vector<ecodes>& ecodes, const vector<string>* strings);



////////////////////////////////////////////////////////////////////////////////
// DATA DL

#define DUNGEON_TILE_WALL          0x0001
#define DUNGEON_TILE_VERT_DOOR     0x0002
#define DUNGEON_TILE_HORIZ_DOOR    0x0004
#define DUNGEON_TILE_STAIRS        0x0008
#define DUNGEON_TILE_COLUMNS       0x0010
#define DUNGEON_TILE_UNMAPPED      0x0080

#define DUNGEON_TILE_SECRET_UP     0x0100
#define DUNGEON_TILE_SECRET_RIGHT  0x0200
#define DUNGEON_TILE_SECRET_DOWN   0x0400
#define DUNGEON_TILE_SECRET_LEFT   0x0800
#define DUNGEON_TILE_SECRET_ANY    0x0F00
#define DUNGEON_TILE_ARCHWAY       0x0000
#define DUNGEON_TILE_HAS_AP        0x1000
#define DUNGEON_TILE_BATTLE_BLANK  0x2000

#define DUNGEON_TILE_ASCII_IRRELEVANT_MASK  ~(DUNGEON_TILE_COLUMNS | \
    DUNGEON_TILE_UNMAPPED | DUNGEON_TILE_BATTLE_BLANK | DUNGEON_TILE_HAS_AP \
    | 0x4000)

struct map_data {
  int16_t data[90][90];

  void byteswap();
  void transpose();
};

vector<map_data> load_dungeon_map_index(const string& filename);
Image generate_dungeon_map(const map_data& data, const map_metadata& metadata,
    const vector<ap_info>& aps, int level_num);



////////////////////////////////////////////////////////////////////////////////
// DATA LD

vector<map_data> load_land_map_index(const string& filename);
unordered_set<string> all_land_types();
Image generate_land_map(const map_data& data, const map_metadata& metadata,
    const vector<ap_info>& aps, int level_num);



////////////////////////////////////////////////////////////////////////////////
// DATA SD2

vector<string> load_string_index(const string& filename);
