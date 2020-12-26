#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Image.hh>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "ResourceFile.hh"



////////////////////////////////////////////////////////////////////////////////
/* NOTES
 * <scenario_name> - scenario metadata
 * data_bd - land tileset definitions
 * data_ci - some very simple strings (0x100 bytes allocated to each)
 * data_cs - ?
 * data_custom_N_bd - custom land tileset definitions
 * data_dd - land action point codes
 * data_ddd - dungeon action point codes
 * data_des - monster descriptions
 * data_dl - dungeon levels
 * data_ed - simple encounters
 * data_ed2 - complex encounters
 * data_ed3 - extra aps
 * data_edcd - extra codes
 * data_ld - level data (tile map)
 * data_md - monster data (including NPCs)
 * data_md2 - map data (includes descriptions)
 * data_menu - ?
 * data_ni - whatever it is, the elements are 0x64 bytes in size
 * data_od - yes/no encounter (option) answer strings
 * data_race - ?
 * data_rd - land map metadata (incl. random rectangles)
 * data_rdd - dungeon map metadata (incl. random rectangles)
 * data_ri - scenario restrictions (races/castes that can't play it)
 * data_sd - ?
 * data_sd2 - strings
 * data_solids - ?
 * data_td - ?
 * data_td2 - rogue encounters
 * data_td3 - time encounters
 * global - global information (starting loc, start/shop/temple/etc. xaps, ...)
 * layout - land level layout map
 * scenario - global metadata
 * scenario.rsf - resources (images, sounds, etc.)
 * save/data_a1 - ?
 * save/data_b1 - action point codes
 * save/data_c1 - ?
 * save/data_d1 - ?
 * save/data_e1 - ?
 * save/data_f1 - ?
 * save/data_g1 - ?
 * save/data_h1 - ?
 * save/data_i1 - characters + npcs
 * save/data_td3 - ?
 */



////////////////////////////////////////////////////////////////////////////////
// DATA MD2

struct PartyMap {
  struct {
    int16_t icon_id;
    int16_t x;
    int16_t y;
  } annotations[10];
  int16_t x;
  int16_t y;
  int16_t level_num;
  int16_t picture_id;
  int16_t tile_size;
  int16_t text_id;
  int16_t is_dungeon;
  int16_t unknown[5];

  uint8_t description_valid_chars;
  char description[0xFF];

  void byteswap();
};

std::vector<PartyMap> load_party_map_index(const std::string& filename);
std::string disassemble_party_map(int index, const PartyMap& t);
std::string disassemble_all_party_maps(const std::vector<PartyMap>& t);



////////////////////////////////////////////////////////////////////////////////
// DATA CUSTOM N BD

struct TileDefinition {
  uint16_t sound_id;
  uint16_t time_per_move;
  uint16_t solid_type; // 0 = not solid, 1 = solid to 1-box chars, 2 = solid
  uint16_t is_shore;
  uint16_t is_need_boat; // 1 = is boat, 2 = need boat
  uint16_t is_path;
  uint16_t blocks_los;
  uint16_t need_fly_float;
  uint16_t special_type; // 1 = trees, 2 = desert, 3 = shrooms, 4 = swamp, 5 = snow
  int16_t unknown5;
  int16_t battle_expansion[9];
  int16_t unknown6;

  void byteswap();
};

struct TileSetDefinition {
  TileDefinition tiles[201];
  uint16_t base_tile_id;

  void byteswap();
};

TileSetDefinition load_tileset_definition(const std::string& filename);
Image generate_tileset_definition_legend(const TileSetDefinition& ts,
    const std::string& land_type, const std::string& rsf_name);



////////////////////////////////////////////////////////////////////////////////
// SCENARIO.RSF

std::unordered_map<int16_t, Image> get_picts(const std::string& rsf_name);
std::unordered_map<int16_t, ResourceFile::DecodedColorIconResource> get_cicns(
    const std::string& rsf_name);
std::unordered_map<int16_t, std::string> get_snds(const std::string& rsf_name);
std::unordered_map<int16_t, std::pair<std::string, bool>> get_texts(const std::string& rsf_name);


////////////////////////////////////////////////////////////////////////////////
// LAYOUT

struct LevelNeighbors {
  int16_t x;
  int16_t y;
  int16_t left;
  int16_t right;
  int16_t top;
  int16_t bottom;

  LevelNeighbors();
};

struct LandLayout {
  int16_t layout[8][16];

  LandLayout();
  LandLayout(const LandLayout& l);
  int num_valid_levels();
  void byteswap();
};

LandLayout load_land_layout(const std::string& filename);
LevelNeighbors get_level_neighbors(const LandLayout& l, int16_t id);
std::vector<LandLayout> get_connected_components(const LandLayout& l);
Image generate_layout_map(const LandLayout& l,
    const std::unordered_map<int16_t, std::string>& level_id_to_image_name);



////////////////////////////////////////////////////////////////////////////////
// GLOBAL

struct GlobalMetadata {
  int16_t start_xap;
  int16_t death_xap;
  int16_t quit_xap;
  int16_t reserved1_xap;
  int16_t shop_xap;
  int16_t temple_xap;
  int16_t reserved2_xap;
  int16_t unknown[23];

  void byteswap();
};

GlobalMetadata load_global_metadata(const std::string& filename);
std::string disassemble_globals(const GlobalMetadata& g);



////////////////////////////////////////////////////////////////////////////////
// SCENARIO NAME

struct ScenarioMetadata {
  int32_t recommended_starting_levels;
  int32_t unknown1;
  int32_t start_level;
  int32_t start_x;
  int32_t start_y;
  // many unknown fields follow

  void byteswap();
};

ScenarioMetadata load_scenario_metadata(const std::string& filename);



////////////////////////////////////////////////////////////////////////////////
// DATA EDCD

struct ECodes {
  int16_t data[5];

  void byteswap();
};

std::vector<ECodes> load_ecodes_index(const std::string& filename);



////////////////////////////////////////////////////////////////////////////////
// DATA TD

struct Treasure {
  int16_t item_ids[20];
  int16_t victory_points;
  int16_t gold;
  int16_t gems;
  int16_t jewelry;

  void byteswap();
};

std::vector<Treasure> load_treasure_index(const std::string& filename);
std::string disassemble_treasure(int index, const Treasure& t);
std::string disassemble_all_treasures(const std::vector<Treasure>& t);



////////////////////////////////////////////////////////////////////////////////
// DATA ED

struct SimpleEncounter {
  int8_t choice_codes[4][8];
  int16_t choice_args[4][8];
  int8_t choice_result_index[4];
  int8_t can_backout;
  int8_t max_times;
  int16_t unknown;
  int16_t prompt;
  struct {
    uint8_t valid_chars;
    char text[79];
  } choice_text[4];

  void byteswap();
};

std::vector<SimpleEncounter> load_simple_encounter_index(
    const std::string& filename);
std::string disassemble_simple_encounter(int index, const SimpleEncounter& e,
    const std::vector<ECodes> ecodes, const std::vector<std::string>& strings);
std::string disassemble_all_simple_encounters(
    const std::vector<SimpleEncounter>& e, const std::vector<ECodes> ecodes,
    const std::vector<std::string>& strings);



////////////////////////////////////////////////////////////////////////////////
// DATA ED2

struct ComplexEncounter {
  int8_t choice_codes[4][8];
  int16_t choice_args[4][8];
  int8_t action_result;
  int8_t speak_result;
  int8_t actions_selected[8];
  int16_t spell_codes[10];
  int8_t spell_result_codes[10];
  int16_t item_codes[5];
  int8_t item_result_codes[5];
  int8_t can_backout;
  int8_t has_rogue_encounter;
  int8_t max_times;
  int16_t rogue_encounter_id;
  int8_t rogue_reset_flag;
  int8_t unknown;
  int16_t prompt;
  struct {
    uint8_t valid_chars;
    char text[39];
  } action_text[8];
  struct {
    uint8_t valid_chars;
    char text[39];
  } speak_text;

  void byteswap();
};

std::vector<ComplexEncounter> load_complex_encounter_index(
    const std::string& filename);
std::string disassemble_complex_encounter(int index, const ComplexEncounter& e,
    const std::vector<ECodes> ecodes, const std::vector<std::string>& strings);
std::string disassemble_all_complex_encounters(
    const std::vector<ComplexEncounter>& e, const std::vector<ECodes> ecodes,
    const std::vector<std::string>& strings);



////////////////////////////////////////////////////////////////////////////////
// DATA TD2

struct RogueEncounter {
  int8_t actions_available[8];
  int8_t trap_affects_rogue_only;
  int8_t is_trapped;
  int8_t percent_modify[8];
  int8_t success_result_codes[8];
  int8_t failure_result_codes[8];
  int16_t success_string_ids[8];
  int16_t failure_string_ids[8];
  int16_t success_sound_ids[8];
  int16_t failure_sound_ids[8];

  int16_t trap_spell;
  int16_t trap_damage_low;
  int16_t trap_damage_high;
  int16_t num_lock_tumblers;
  int16_t prompt_string;
  int16_t trap_sound;
  int16_t trap_spell_power_level;
  int16_t prompt_sound;
  int16_t percent_per_level_to_open;
  int16_t percent_per_level_to_disable;

  void byteswap();
};

std::vector<RogueEncounter> load_rogue_encounter_index(
    const std::string& filename);
std::string disassemble_rogue_encounter(int index, const RogueEncounter& e,
    const std::vector<ECodes> ecodes, const std::vector<std::string>& strings);
std::string disassemble_all_rogue_encounters(
    const std::vector<RogueEncounter>& e, const std::vector<ECodes> ecodes,
    const std::vector<std::string>& strings);



////////////////////////////////////////////////////////////////////////////////
// DATA TD3

struct TimeEncounter {
  int16_t day;
  int16_t increment;
  int16_t percent_chance;
  int16_t xap_id;
  int16_t required_level;
  int16_t required_rect;
  int16_t required_x;
  int16_t required_y;
  int16_t required_item_id;
  int16_t required_quest;
  int16_t land_or_dungeon; // 1 = land, 2 = dungeon
  int8_t unknown[0x12];

  void byteswap();
};

std::vector<TimeEncounter> load_time_encounter_index(
    const std::string& filename);
std::string disassemble_time_encounter(int index, const TimeEncounter& e);
std::string disassemble_all_time_encounters(
    const std::vector<TimeEncounter>& e);



////////////////////////////////////////////////////////////////////////////////
// DATA RD

struct RandomRect {
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
};

struct MapMetadata {
  std::string land_type;
  std::vector<RandomRect> random_rects;
};

std::vector<MapMetadata> load_map_metadata_index(const std::string& filename);



////////////////////////////////////////////////////////////////////////////////
// DATA DD
// DATA ED3

struct APInfo {
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

std::vector<std::vector<APInfo>> load_ap_index(const std::string& filename);
std::vector<APInfo> load_xap_index(const std::string& filename);
std::string disassemble_opcode(int16_t ap_code, int16_t arg_code,
    const std::vector<ECodes>& ecodes, const std::vector<std::string>& strings);
std::string disassemble_xap(int16_t ap_num, const APInfo& ap,
    const std::vector<ECodes>& ecodes, const std::vector<std::string>& strings,
    const std::vector<MapMetadata>& land_metadata,
    const std::vector<MapMetadata>& dungeon_metadata);
std::string disassemble_xaps(const std::vector<APInfo>& aps,
    const std::vector<ECodes>& ecodes, const std::vector<std::string>& strings,
    const std::vector<MapMetadata>& land_metadata,
    const std::vector<MapMetadata>& dungeon_metadata);
std::string disassemble_ap(int16_t level_num, int16_t ap_num, const APInfo& ap,
    const std::vector<ECodes>& ecodes, const std::vector<std::string>& strings,
    int dungeon);
std::string disassemble_level_aps(int16_t level_num,
    const std::vector<APInfo>& aps, const std::vector<ECodes>& ecodes,
    const std::vector<std::string>& strings, int dungeon);
std::string disassemble_all_aps(const std::vector<std::vector<APInfo>>& aps,
    const std::vector<ECodes>& ecodes, const std::vector<std::string>& strings,
    int dungeon);



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

struct MapData {
  int16_t data[90][90];

  void byteswap();
  void transpose();
};

std::vector<MapData> load_dungeon_map_index(const std::string& filename);
Image generate_dungeon_map(const MapData& data, const MapMetadata& metadata,
    const std::vector<APInfo>& aps, int level_num);



////////////////////////////////////////////////////////////////////////////////
// DATA LD

std::vector<MapData> load_land_map_index(const std::string& filename);
std::unordered_set<std::string> all_land_types();
void populate_custom_tileset_configuration(const std::string& land_type,
    const TileSetDefinition& def);
void populate_image_caches(const std::string& the_family_jewels_name);
void add_custom_pattern(const std::string& land_type, Image& img);
Image generate_land_map(const MapData& data, const MapMetadata& metadata,
    const std::vector<APInfo>& aps, int level_num, const LevelNeighbors& n,
    int16_t start_x, int16_t start_y, const std::string& rsf_name);



////////////////////////////////////////////////////////////////////////////////
// DATA SD2

std::vector<std::string> load_string_index(const std::string& filename);
