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
#include "RealmzGlobalData.hh"



/**
 * Scenario file contents:
 * 
 * <Scenario Name> - scenario metadata
 * Data BD - land tileset definitions
 * Data CI - some very simple strings (0x100 bytes allocated to each)
 * Data CS - author metadata? last 0x100 bytes are a pstring of the author name
 * Data Custom N BD - custom land tileset definitions (these are implemented in
 *                    RealmzGlobalData since they're the same format as the
 *                    standard tilesets)
 * Data DD - land action point codes
 * Data DDD - dungeon action point codes
 * Data DES - monster descriptions
 * Data DL - dungeon levels
 * Data ED - simple encounters
 * Data ED2 - complex encounters
 * Data ED3 - extra aps
 * Data EDCD - extra codes
 * Data LD - level data (tile map)
 * Data MD - monster data (including NPCs). previously named Data MD1 or Data
 *           MD-1; some scenarios have duplicates of this file with those names
 * Data MD2 - map data (includes descriptions)
 * Data MENU - ?
 * Data NI - probably custom items; there are 200 entries of 0x64 bytes each;
 *           first field is an ID ranging from 800-999
 * Data OD - yes/no encounter (option) answer strings
 * Data Race - ?
 * Data RD - land map metadata (incl. random rectangles)
 * Data RDD - dungeon map metadata (incl. random rectangles)
 * Data RI - scenario restrictions (races/castes that can't play it)
 * Data SD - ? (this appears to always be the same size?)
 * Data SD2 - strings
 * Data Solids - a single byte for each negative tile ID (0x400 of them); if the
 *               byte is 1, the tile is solid. tile -1 is the first in the file.
 * Data TD - ?
 * Data TD2 - rogue encounters
 * Data TD2 - time encounters
 * Global - global information (starting loc, start/shop/temple/etc. xaps, ...)
 * Layout - land level layout map
 * Scenario - global metadata
 * Scenario.rsf - resources (images, sounds, etc.)
 * 
 * Save file contents:
 * 
 * save/Data A1 - ?
 * save/Data B1 - action point codes
 * save/Data C1 - ?
 * save/Data D1 - ?
 * save/Data E1 - ?
 * save/Data F1 - ?
 * save/Data G1 - ?
 * save/Data H1 - ?
 * save/Data I1 - characters + npcs
 * save/Data TD3 - ? (presumably time encounters, as above)
 */



struct RealmzScenarioData {
  RealmzScenarioData(
      RealmzGlobalData& global,
      const std::string& scenario_dir,
      const std::string& scenario_name);
  ~RealmzScenarioData() = default;



  //////////////////////////////////////////////////////////////////////////////
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
    LandLayout& operator=(const LandLayout& l);

    size_t num_valid_levels() const;
    LevelNeighbors get_level_neighbors(int16_t id) const;
    std::vector<LandLayout> get_connected_components() const;

    void byteswap();
  };

  LandLayout load_land_layout(const std::string& filename);
  Image generate_layout_map(const LandLayout& l,
      const std::unordered_map<int16_t, std::string>& level_id_to_image_name);



  //////////////////////////////////////////////////////////////////////////////
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
  std::string disassemble_globals();



  //////////////////////////////////////////////////////////////////////////////
  // <SCENARIO NAME>

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



  //////////////////////////////////////////////////////////////////////////////
  // DATA EDCD

  struct ECodes {
    int16_t data[5];
    void byteswap();
  };

  std::vector<ECodes> load_ecodes_index(const std::string& filename);



  //////////////////////////////////////////////////////////////////////////////
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
  std::string disassemble_treasure(size_t index);
  std::string disassemble_all_treasures();



  //////////////////////////////////////////////////////////////////////////////
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
  std::string disassemble_simple_encounter(size_t index);
  std::string disassemble_all_simple_encounters();



  //////////////////////////////////////////////////////////////////////////////
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
  std::string disassemble_complex_encounter(size_t index);
  std::string disassemble_all_complex_encounters();



  //////////////////////////////////////////////////////////////////////////////
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
  std::string disassemble_rogue_encounter(size_t index);
  std::string disassemble_all_rogue_encounters();



  //////////////////////////////////////////////////////////////////////////////
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
  std::string disassemble_time_encounter(size_t index);
  std::string disassemble_all_time_encounters();



  //////////////////////////////////////////////////////////////////////////////
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



  //////////////////////////////////////////////////////////////////////////////
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
  std::string disassemble_opcode(int16_t ap_code, int16_t arg_code);
  std::string disassemble_xap(int16_t ap_num);
  std::string disassemble_all_xaps();
  std::string disassemble_level_ap(int16_t level_num, int16_t ap_num, bool dungeon);
  std::string disassemble_level_aps(int16_t level_num, bool dungeon);
  std::string disassemble_all_level_aps(bool dungeon);



  //////////////////////////////////////////////////////////////////////////////
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
  Image generate_dungeon_map(int16_t level_num, uint8_t x0, uint8_t y0,
      uint8_t w, uint8_t h);



  //////////////////////////////////////////////////////////////////////////////
  // DATA LD

  std::vector<MapData> load_land_map_index(const std::string& filename);
  std::unordered_set<std::string> all_land_types();
  void populate_custom_tileset_configuration(const std::string& land_type,
      const TileSetDefinition& def);
  void populate_image_caches(ResourceFile& the_family_jewels_rsf);
  void add_custom_pattern(const std::string& land_type, Image& img);
  Image generate_land_map(int16_t level_num, uint8_t x0, uint8_t y0, uint8_t w,
      uint8_t h);



  //////////////////////////////////////////////////////////////////////////////
  // DATA SD2

  std::vector<std::string> load_string_index(const std::string& filename);



  //////////////////////////////////////////////////////////////////////////////
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
  std::string disassemble_party_map(size_t index);
  Image render_party_map(size_t index);
  std::string disassemble_all_party_maps();



  RealmzGlobalData& global;
  std::string scenario_dir;
  std::string name;
  std::unordered_map<std::string, TileSetDefinition> land_type_to_tileset_definition;
  std::unordered_map<std::string, Image> positive_pattern_cache;
  ResourceFile scenario_rsf;
  LandLayout layout;
  GlobalMetadata global_metadata;
  ScenarioMetadata scenario_metadata;
  std::vector<ECodes> ecodes;
  std::vector<Treasure> treasures;
  std::vector<SimpleEncounter> simple_encounters;
  std::vector<ComplexEncounter> complex_encounters;
  std::vector<RogueEncounter> rogue_encounters;
  std::vector<TimeEncounter> time_encounters;
  std::vector<MapMetadata> dungeon_metadata;
  std::vector<MapMetadata> land_metadata;
  std::vector<std::vector<APInfo>> dungeon_aps;
  std::vector<std::vector<APInfo>> land_aps;
  std::vector<APInfo> xaps;
  std::vector<MapData> dungeon_maps;
  std::vector<MapData> land_maps;
  std::vector<std::string> strings;
  std::vector<PartyMap> party_maps;
};
