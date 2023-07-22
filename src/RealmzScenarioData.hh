#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Image.hh>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "RealmzGlobalData.hh"
#include "ResourceFile.hh"

/**
 * Scenario file contents:
 *   <Scenario Name>: Scenario metadata
 *   Data A: ? (Legacy)
 *   Data BD: Battle setups
 *   Data CI: Some very simple strings (0x100 bytes allocated to each)
 *   Data CS: Author metadata? Last 0x100 bytes are a pstring (author name)
 *   Data Custom N BD: Custom land tileset definitions. (Implemented in
 *     RealmzGlobalData since they're the same format as the standard tilesets)
 *   Data DD: Land AP codes
 *   Data DDD: Dungeon AP codes
 *   Data DES: Monster descriptions
 *   Data DL: Dungeon levels
 *   Data ED: Simple encounters
 *   Data ED2: Complex encounters
 *   Data ED3: Extra APs
 *   Data EDCD: Extra codes (E-Codes in Divinity)
 *   Data LD: Level data (tile map)
 *   Data MD: Monster data (including NPCs). Previously named Data MD1 or Data
 *     MD-1; some scenarios have duplicates of this file with those names
 *   Data MD2: Map data (includes descriptions)
 *   Data MENU: ?
 *   Data NI: Custom items (200 entries for IDs 800-999)
 *   Data OD: Yes/no encounter (option) answer strings. This file is optional;
 *     if it's missing then these strings come from Data SD2 instead
 *   Data Race: Race definitions?
 *   Data RD: Land map metadata (incl. random rectangles)
 *   Data RDD: Dungeon map metadata (incl. random rectangles)
 *   Data RI: Scenario restrictions (races/castes that can't play it)
 *   Data S: ? (Legacy)
 *   Data SD: Shops
 *   Data SD2: Strings
 *   Data Solids: A single byte for each negative tile ID (0x400 of them); if
 *     the byte is 1, the tile is solid. Tile -1 is the first in the file
 *   Data SID: ? (Legacy)
 *   Data TD: Treasures
 *   Data TD2: Rogue encounters
 *   Data TD3: Time encounters
 *   Global: Global information (start loc, start/shop/temple/etc. XAPs, ...)
 *   Layout: Land level layout map
 *   Scenario: Global metadata
 *   Scenario.rsf: Resources (images, sounds, etc.)
 *
 * Save file contents:
 *   save/Data A1: Land level state (SavedLandLevelState[level_count])
 *   save/Data B1: Dungeon level state (SavedDungeonLevelState[level_count])
 *   save/Data C1: ?
 *   save/Data D1: ?
 *   save/Data E1: Shop contents (same as Data SD in scenario dir)
 *   save/Data F1: Simple encounters (same as Data ED in scenario dir)
 *   save/Data G1: Complex encounters (same as Data ED2 in scenario dir)
 *   save/Data H1: Rogue encounters (same as Data TD2 in scenario dir)
 *   save/Data I1: Characters and allies
 *   save/Data TD3: Time encounters (same as Data TD3 in scenario dir)
 *
 * See RealmzGlobalData.hh for Data Files formats (shared by all scenarios).
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
    be_int16_t layout[8][16];

    LandLayout();
    LandLayout(const LandLayout& l);
    LandLayout& operator=(const LandLayout& l);

    size_t num_valid_levels() const;
    LevelNeighbors get_level_neighbors(int16_t id) const;
    std::vector<LandLayout> get_connected_components() const;
  } __attribute__((packed));

  LandLayout load_land_layout(const std::string& filename);
  Image generate_layout_map(const LandLayout& l);

  //////////////////////////////////////////////////////////////////////////////
  // GLOBAL

  struct GlobalMetadata {
    be_int16_t start_xap;
    be_int16_t death_xap;
    be_int16_t quit_xap;
    be_int16_t reserved1_xap;
    be_int16_t shop_xap;
    be_int16_t temple_xap;
    be_int16_t reserved2_xap;
    be_int16_t unknown[23];
  } __attribute__((packed));

  GlobalMetadata load_global_metadata(const std::string& filename);
  std::string disassemble_globals();

  //////////////////////////////////////////////////////////////////////////////
  // <SCENARIO NAME>

  struct ScenarioMetadata {
    be_int32_t recommended_starting_levels;
    be_int32_t unknown1;
    be_int32_t start_level;
    be_int32_t start_x;
    be_int32_t start_y;
    // many unknown fields follow
  } __attribute__((packed));

  ScenarioMetadata load_scenario_metadata(const std::string& filename);

  //////////////////////////////////////////////////////////////////////////////
  // DATA EDCD

  struct ECodes {
    be_int16_t data[5];
  } __attribute__((packed));

  std::vector<ECodes> load_ecodes_index(const std::string& filename);

  //////////////////////////////////////////////////////////////////////////////
  // DATA TD

  struct Treasure {
    be_int16_t item_ids[20];
    be_int16_t victory_points;
    be_int16_t gold;
    be_int16_t gems;
    be_int16_t jewelry;
  } __attribute__((packed));

  std::vector<Treasure> load_treasure_index(const std::string& filename);
  std::string disassemble_treasure(size_t index);
  std::string disassemble_all_treasures();

  //////////////////////////////////////////////////////////////////////////////
  // DATA ED

  struct SimpleEncounter {
    int8_t choice_codes[4][8];
    be_int16_t choice_args[4][8];
    int8_t choice_result_index[4];
    int8_t can_backout;
    int8_t max_times;
    be_int16_t unknown;
    be_int16_t prompt;
    struct {
      uint8_t valid_chars;
      char text[79];
    } __attribute__((packed)) choice_text[4];
  } __attribute__((packed));

  std::vector<SimpleEncounter> load_simple_encounter_index(
      const std::string& filename);
  std::string disassemble_simple_encounter(size_t index);
  std::string disassemble_all_simple_encounters();

  //////////////////////////////////////////////////////////////////////////////
  // DATA ED2

  struct ComplexEncounter {
    int8_t choice_codes[4][8];
    be_int16_t choice_args[4][8];
    int8_t action_result;
    int8_t speak_result;
    int8_t actions_selected[8];
    be_int16_t spell_codes[10];
    int8_t spell_result_codes[10];
    be_int16_t item_codes[5];
    int8_t item_result_codes[5];
    int8_t can_backout;
    int8_t has_rogue_encounter;
    int8_t max_times;
    be_int16_t rogue_encounter_id;
    int8_t rogue_reset_flag;
    int8_t unknown;
    be_int16_t prompt;
    struct {
      uint8_t valid_chars;
      char text[39];
    } __attribute__((packed)) action_text[8];
    struct {
      uint8_t valid_chars;
      char text[39];
    } __attribute__((packed)) speak_text;
  } __attribute__((packed));

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
    be_int16_t success_string_ids[8];
    be_int16_t failure_string_ids[8];
    be_int16_t success_sound_ids[8];
    be_int16_t failure_sound_ids[8];

    be_int16_t trap_spell;
    be_int16_t trap_damage_low;
    be_int16_t trap_damage_high;
    be_int16_t num_lock_tumblers;
    be_int16_t prompt_string;
    be_int16_t trap_sound;
    be_int16_t trap_spell_power_level;
    be_int16_t prompt_sound;
    be_int16_t percent_per_level_to_open;
    be_int16_t percent_per_level_to_disable;
  } __attribute__((packed));

  std::vector<RogueEncounter> load_rogue_encounter_index(
      const std::string& filename);
  std::string disassemble_rogue_encounter(size_t index);
  std::string disassemble_all_rogue_encounters();

  //////////////////////////////////////////////////////////////////////////////
  // DATA TD3

  struct TimeEncounter {
    be_int16_t day;
    be_int16_t increment;
    be_int16_t percent_chance;
    be_int16_t xap_id;
    be_int16_t required_level;
    be_int16_t required_rect;
    be_int16_t required_x;
    be_int16_t required_y;
    be_int16_t required_item_id;
    be_int16_t required_quest;
    be_int16_t land_or_dungeon; // 1 = land, 2 = dungeon
    int8_t unknown[0x12];
  } __attribute__((packed));

  std::vector<TimeEncounter> load_time_encounter_index(
      const std::string& filename);
  std::string disassemble_time_encounter(size_t index);
  std::string disassemble_all_time_encounters();

  //////////////////////////////////////////////////////////////////////////////
  // DATA RD

  struct MapMetadataFile {
    struct Coords {
      be_int16_t top;
      be_int16_t left;
      be_int16_t bottom;
      be_int16_t right;
    } __attribute__((packed));
    struct BattleRange {
      be_int16_t low;
      be_int16_t high;
    } __attribute__((packed));

    Coords coords[20];
    be_int16_t times_in_10k[20];
    BattleRange battle_range[20];
    be_int16_t xap_num[20][3];
    be_int16_t xap_chance[20][3];
    int8_t land_type;
    int8_t unknown[0x16];
    int8_t percent_option[20];
    int8_t unused;
    be_int16_t sound[20];
    be_int16_t text[20];
  } __attribute__((packed));

  // Random rectangles are stored in parallel arrays in the map metadata file;
  // this structure is a parsed representation of a rect and doesn't reflect the
  // storage format (hence not using the le/be int types here, and this struct
  // not having __attribute__((packed))). The above struct represents the
  // storage format.
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
    be_int32_t location_code;
    uint8_t to_level;
    uint8_t to_x;
    uint8_t to_y;
    uint8_t percent_chance;
    be_int16_t command_codes[8];
    be_int16_t argument_codes[8];

    int8_t get_x() const;
    int8_t get_y() const;
    int8_t get_level_num() const;
  } __attribute__((packed));

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

  struct MapData {
    be_int16_t data[90][90];

    void transpose();
  } __attribute__((packed));

  std::vector<MapData> load_dungeon_map_index(const std::string& filename);
  std::string generate_dungeon_map_text(int16_t level_num);
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
  std::string generate_land_map_text(int16_t level_num);
  Image generate_land_map(
      int16_t level_num,
      uint8_t x0,
      uint8_t y0,
      uint8_t w,
      uint8_t h,
      std::unordered_set<int16_t>* used_negative_tiles = nullptr,
      std::unordered_map<std::string, std::unordered_set<uint8_t>>* used_positive_tiles = nullptr);

  //////////////////////////////////////////////////////////////////////////////
  // DATA SD2

  std::vector<std::string> load_string_index(const std::string& filename);

  //////////////////////////////////////////////////////////////////////////////
  // DATA OD

  std::vector<std::string> load_option_string_index(const std::string& filename);

  //////////////////////////////////////////////////////////////////////////////
  // DATA MD2

  struct PartyMap {
    struct {
      be_int16_t icon_id;
      be_int16_t x;
      be_int16_t y;
    } __attribute__((packed)) annotations[10];
    be_int16_t x;
    be_int16_t y;
    be_int16_t level_num;
    be_int16_t picture_id;
    be_int16_t tile_size;
    be_int16_t text_id;
    be_int16_t is_dungeon;
    be_int16_t unknown[5];

    uint8_t description_valid_chars;
    char description[0xFF];
  } __attribute__((packed));

  std::vector<PartyMap> load_party_map_index(const std::string& filename);
  std::string disassemble_party_map(size_t index);
  Image render_party_map(size_t index);
  std::string disassemble_all_party_maps();

  /////////////////////////////////////////////////////////////////////////////
  // DATA MD

  struct MonsterDefinition {
    /* 0000 */ uint8_t stamina;
    /* 0001 */ uint8_t bonus_stamina;
    /* 0002 */ uint8_t agility;
    /* 0003 */ uint8_t description_index; // Seems to always match monster ID?
    /* 0004 */ uint8_t movement;
    /* 0005 */ uint8_t armor_rating;
    /* 0006 */ uint8_t magic_resistance;
    /* 0007 */ int8_t required_weapon_id; // 0 = any, -1 = blunt, -2 = sharp
    /* 0008 */ uint8_t traitor;
    /* 0009 */ uint8_t size;
    /* 000A */ uint8_t magic_using;
    /* 000B */ uint8_t undead;
    /* 000C */ uint8_t demon_devil;
    /* 000D */ uint8_t reptilian;
    /* 000E */ uint8_t very_evil;
    /* 000F */ uint8_t intelligent;
    /* 0010 */ uint8_t giant_size;
    /* 0011 */ uint8_t non_humanoid;
    /* 0012 */ uint8_t num_physical_attacks;
    /* 0013 */ uint8_t num_magic_attacks;
    struct Attack {
      uint8_t min_damage;
      uint8_t max_damage;
      // Values for form:
      // 20 = nothing
      // 21 = pummel
      // 22 = claw
      // 23 = bite
      // 24 = unused
      // 25 = unused
      // 26 = unused
      // 27 = punch/kick
      // 28 = club
      // 29 = slime
      // 2A = sting
      uint8_t form;
      // Values for special_condition:
      // 00 = nothing
      // 01 = cause fear
      // 02 = paralyze
      // 03 = curse
      // 04 = stupefy
      // 05 = entangle
      // 06 = poison
      // 07 = confuse
      // 08 = drain spell points
      // 09 = drain experience
      // 0A = charm
      // 0B = fire damage
      // 0C = cold damage
      // 0D = electric damage
      // 0E = chemical damage
      // 0F = mental damage
      // 10 = cause disease
      // 11 = cause age
      // 12 = cause blindness
      // 13 = turn to stone
      uint8_t special_condition;
    } __attribute__((packed));
    /* 0014 */ Attack attacks[5];
    /* 0028 */ uint8_t damage_plus;
    /* 0029 */ uint8_t cast_spell_percent;
    /* 002A */ uint8_t run_away_percent;
    /* 002B */ uint8_t surrender_percent;
    /* 002C */ uint8_t use_missile_percent;
    /* 002D */ int8_t summon_flag; // 0 = no, 1 = yes, -1 = is NPC
    /* 002E */ int8_t drv_adjust_heat;
    /* 002F */ int8_t drv_adjust_cold;
    /* 0030 */ int8_t drv_adjust_electric;
    /* 0031 */ int8_t drv_adjust_chemical;
    /* 0032 */ int8_t drv_adjust_mental;
    /* 0033 */ int8_t drv_adjust_magic;
    /* 0034 */ uint8_t immune_to_charm;
    /* 0035 */ uint8_t immune_to_heat;
    /* 0036 */ uint8_t immune_to_cold;
    /* 0037 */ uint8_t immune_to_electric;
    /* 0038 */ uint8_t immune_to_chemical;
    /* 0039 */ uint8_t immune_to_mental;
    /* 003A */ be_uint16_t treasure_items[3];
    /* 0040 */ be_uint16_t spells[10];
    /* 0054 */ be_uint16_t held_items[6];
    /* 0060 */ be_uint16_t weapon;
    /* 0062 */ be_uint16_t icon;
    /* 0064 */ be_uint16_t spell_points;
    /* 0066 */ uint8_t unknown_a1[0x10];
    /* 0076 */ uint8_t hide_in_bestiary_menu;
    /* 0077 */ uint8_t unknown_a2[2];
    /* 0079 */ uint8_t magic_plus_required_to_hit;
    /* 007A */ int8_t conditions[0x28];
    /* 00A2 */ uint8_t unknown_a3[4];
    /* 00A6 */ be_uint16_t macro_number;
    /* 00A8 */ uint8_t unknown_a4[2];
    /* 00AA */ char name[0x28];
    /* 00D2 */
  } __attribute__((packed));

  std::vector<MonsterDefinition> load_monster_index(const std::string& filename);
  std::string disassemble_monster(size_t index);
  std::string disassemble_all_monsters();

  //////////////////////////////////////////////////////////////////////////////
  // DATA BD

  struct BattleDefinition {
    // monster_ids defines the tilemap for the battle. Presumably monsters placed
    // with "force friends" have negative IDs here. The map is column-major (that
    // is, it's indexed as [x][y]).
    /* 0000 */ be_int16_t monster_ids[13][13];
    /* 0152 */ uint8_t bonus_distance; // "Distance 1 to" in Divinity
    /* 0153 */ uint8_t unknown_a1;
    /* 0154 */ be_uint16_t before_string;
    /* 0156 */ be_uint16_t after_string;
    /* 0158 */ be_int16_t macro_number; // Negative for some reason
  } __attribute__((packed));

  std::vector<BattleDefinition> load_battle_index(const std::string& filename);
  std::string disassemble_battle(size_t index);
  std::string disassemble_all_battles();

  //////////////////////////////////////////////////////////////////////////////
  // DATA NI

  struct ItemDefinition {
    /* 00 */ be_int16_t strength_bonus;
    /* 02 */ be_uint16_t item_id; // Could also be string index (they're sequential anyway)
    /* 04 */ be_int16_t icon_id;
    /* 06 */ be_uint16_t weapon_type;
    /* 08 */ be_int16_t blade_type; // 0 = non-weapon, -1 = blunt, -2 = sharp
    /* 0A */ be_int16_t charge_count; // -1 = no charges
    /* 0C */ be_int16_t luck_bonus;
    /* 0E */ be_int16_t movement;
    /* 10 */ be_int16_t armor_rating;
    /* 12 */ be_int16_t magic_resist;
    /* 14 */ be_int16_t magic_plus;
    /* 16 */ be_int16_t spell_points;
    /* 18 */ be_int16_t sound_id;
    /* 1A */ be_int16_t weight;
    /* 1C */ be_int16_t cost; // If negative, item is unique
    /* 1E */ be_uint16_t required_hands;
    /* 20 */ be_uint16_t disguise_item_id; // ID of item that cursed item appears to be
    /* 22 */ be_uint16_t wear_class;
    // item_category_flags is one bit per flag, arranged in column-major order
    // as listed in Divinity (that is, Small Blunt Weapons is 800000000000000,
    // Medium Blunt Weapons is 4000000000000000, etc.)
    /* 24 */ be_uint64_t category_flags;
    // Race/caste flags are also listed in the same order as in Divinity,
    // starting with the high bit (8000) at the top of each group
    /* 2C */ be_uint16_t not_usable_by_race_flags;
    /* 2E */ be_uint16_t not_usable_by_caste_flags;
    /* 30 */ be_uint16_t specific_race; // For item category flag
    /* 32 */ be_uint16_t specific_caste; // For item category flag
    /* 34 */ be_uint16_t usable_by_race_flags;
    /* 36 */ be_uint16_t usable_by_caste_flags;
    /* 38 */ uint8_t unknown_a2[0x0E];
    /* 46 */ be_int16_t damage;
    /* 48 */ uint8_t unknown_a3[2];
    /* 4A */ be_int16_t heat_bonus_damage;
    /* 4C */ be_int16_t cold_bonus_damage;
    /* 4E */ be_int16_t electric_bonus_damage;
    /* 50 */ be_int16_t undead_bonus_damage;
    /* 52 */ be_int16_t demon_bonus_damage;
    /* 54 */ be_int16_t evil_bonus_damage;
    /* 56 */ be_int16_t specials[5];
    /* 60 */ be_uint16_t weight_per_charge;
    /* 62 */ be_uint16_t drop_on_empty;
    /* 64 */
  } __attribute__((packed));

  std::vector<ItemDefinition> load_custom_item_index(const std::string& filename);
  std::string disassemble_custom_item(size_t index);
  std::string disassemble_all_custom_items();

  //////////////////////////////////////////////////////////////////////////////
  // DATA SD

  struct Shop {
    /* 0000 */ be_uint16_t item_ids[1000];
    /* 07D0 */ uint8_t item_counts[1000];
    /* 0BB8 */ be_uint16_t inflation_percent;
    /* 0BBA */
  } __attribute__((packed));

  std::vector<Shop> load_shop_index(const std::string& filename);
  std::string disassemble_shop(size_t index);
  std::string disassemble_all_shops();

  //////////////////////////////////////////////////////////////////////////////
  // DATA A1

  struct SavedLandLevelState {
    /* 0000 */ APInfo aps[100];
    /* 0FA0 */ be_uint16_t tiles[90][90];
    /* 4EE8 */ MapMetadataFile metadata;
    /* 516C */ uint8_t los_revealed[90][90];
    /* 7110 */
  } __attribute__((packed));

  // TODO: Write parsers for this struct and Data B1/I1, and add support for disassembling saved games

  //////////////////////////////////////////////////////////////////////////////
  // DATA B1

  struct SavedDungeonLevelState {
    /* 0000 */ APInfo aps[100];
    /* 0FA0 */ be_uint16_t tiles[90][90];
    /* 4EE8 */ MapMetadataFile metadata;
    /* 516C */
  } __attribute__((packed));

  //////////////////////////////////////////////////////////////////////////////
  // DATA I1

  struct SavedCharacterState {
    // TODO: Fill in the unknown fields in this structure and its children
    struct Character {
      /* 0000 */ uint8_t unknown_a1[0x26];
      /* 0026 */ be_int16_t chance_to_hit;
      /* 0028 */ be_int16_t dodge_missile;
      /* 002A */ be_int16_t missile_adjust;
      /* 002C */ be_int16_t two_hand_adjust; // Floored at 0
      /* 002E */ uint8_t unknown_a2[0x1C];
      /* 004A */ be_int16_t magic_resistance;
      /* 004C */ be_int16_t movement_adjust;
      /* 004E */ be_int16_t armor_rating;
      /* 0050 */ be_int16_t damage_plus;
      /* 0052 */ be_int16_t race;
      /* 0054 */ be_int16_t caste;
      /* 0056 */ be_int16_t unknown_a3;
      /* 0058 */ be_int16_t gender; // 1 = male, 2 = female
      /* 005A */ be_int16_t level;
      /* 005C */ be_int16_t unknown_a4;
      /* 005E */ be_int16_t movement;
      /* 0060 */ uint8_t unknown_a5[0x10];
      /* 0070 */ be_int16_t current_hp;
      /* 0072 */ be_int16_t max_hp;
      /* 0074 */ uint8_t unknown_a6[4];
      /* 0078 */ be_int16_t current_sp;
      /* 007A */ be_int16_t max_sp;
      /* 007C */ uint8_t unknown_a7[6];
      /* 0082 */ be_int16_t bare_hand_damage;
      /* 0084 */ be_int16_t conditions[40];
      /* 00D4 */ be_int16_t bonus_vs_magic_using;
      /* 00D6 */ be_int16_t bonus_vs_undead;
      /* 00D8 */ be_int16_t bonus_vs_demon_devil;
      /* 00DA */ be_int16_t bonus_vs_reptilian;
      /* 00DC */ be_int16_t bonus_vs_very_evil;
      /* 00DE */ be_int16_t bonus_vs_intelligent;
      /* 00E0 */ be_int16_t bonus_vs_giant_size;
      /* 00E2 */ be_int16_t bonus_vs_non_humanoid;
      /* 00E4 */ uint8_t unknown_a8[0x30];
      /* 0114 */ ActionSuccessChances special_abilities;
      /* 0130 */ uint8_t unknown_a9[2];
      /* 0132 */ be_int16_t drv_charm;
      /* 0134 */ be_int16_t drv_heat;
      /* 0136 */ be_int16_t drv_cold;
      /* 0138 */ be_int16_t drv_electric;
      /* 013A */ be_int16_t drv_chemical;
      /* 013C */ be_int16_t drv_mental;
      /* 013E */ be_int16_t drv_magic;
      /* 0140 */ be_int16_t drv_special;
      /* 0142 */ uint8_t unknown_a10[0xD0];
      /* 0212 */ be_int32_t victory_points; // Negative number
      /* 0216 */ be_int16_t current_load;
      /* 0218 */ be_int16_t max_load;
      /* 021A */ be_int16_t gold;
      /* 021C */ be_int16_t gems;
      /* 021E */ be_int16_t jewelry;
      /* 0220 */ uint8_t unknown_a11[7];
      /* 0227 */ int8_t brawn;
      /* 0228 */ int8_t knowledge;
      /* 0229 */ int8_t judgment;
      /* 022A */ int8_t agility;
      /* 022B */ int8_t vitality;
      /* 022C */ int8_t luck;
      /* 022D */ uint8_t unknown_a12[0x54];
      /* 0281 */ char name[20]; // Max length is 19; enforced during creation
      /* 0295 */ uint8_t unknown_a13[0xD];
      /* 02A2 */ be_int32_t prestige_damage_taken;
      /* 02A6 */ be_int32_t prestige_damage_given;
      /* 02AA */ be_int32_t prestige_hits_given;
      /* 02AE */ be_int32_t prestige_hits_taken;
      /* 02B2 */ be_int32_t prestige_missed_attacks;
      /* 02B6 */ be_int32_t prestige_dodged_attacks;
      /* 02BA */ be_int32_t prestige_enemies_killed;
      /* 02BE */ be_int32_t prestige_times_killed;
      /* 02C2 */ be_int32_t prestige_times_unconscious;
      /* 02C6 */ be_int32_t prestige_combat_spells_cast;
      /* 02CA */ be_int32_t prestige_undead_destroyed;
      /* 02CE */ be_int32_t prestige_undead_turned;
      /* 02D2 */ be_int32_t prestige_penalty_points; // Subtracted (negative is good)
      struct SpellShortcutItem {
        uint16_t spell_class;
        uint16_t spell_level;
        uint16_t spell_number;
        uint16_t power_level;
      } __attribute__((packed));
      /* 02D6 */ SpellShortcutItem spell_shortcuts[10];
      /* 0326 */ uint8_t unknown_a14[0x42];
      /* 0368 */
    } __attribute__((packed));
    struct NPC {
      /* 00 */ uint8_t unknown_a1[0xD2];
      /* D2 */
    } __attribute__((packed));
    /* 0000 */ Character characters[6];
    /* 1470 */ uint8_t unknown_a1[0x24];
    /* 1494 */ char scenario_path[0x100]; // e.g. ":Scenarios:City of Bywater"
    /* 1594 */ uint8_t unknown_a2[0x138];
    /* 16CC */ NPC npcs[20];
    /* 2734 */ uint8_t unknown_a3[0x1257];
    /* 398B */
  } __attribute__((packed));

  //////////////////////////////////////////////////////////////////////////////

  const std::string& name_for_spell(uint16_t id) const;
  std::string desc_for_spell(uint16_t id) const;
  const ItemInfo& info_for_item(uint16_t id) const;
  std::string desc_for_item(uint16_t id) const;

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
  std::vector<ItemDefinition> custom_items;
  std::vector<Shop> shops;
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
  std::vector<std::string> option_strings;
  std::vector<PartyMap> party_maps;
  std::vector<BattleDefinition> battles;
  std::vector<MonsterDefinition> monsters;

  std::unordered_map<uint16_t, ItemInfo> item_info;
  std::unordered_map<uint16_t, std::string> spell_names;
};
