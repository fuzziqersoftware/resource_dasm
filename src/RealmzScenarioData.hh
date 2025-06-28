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

namespace ResourceDASM {

using namespace phosg;

// See RealmzGlobalData.hh for a description of what each file contains.
// TODO: Add disassembly for Data Race and Data Caste here. It seems they were
// never fully implemented in Realmz anyway, so there msy not be any useful
// examples of them in scenario data in the wild.

struct RealmzScenarioData {
  RealmzScenarioData(
      RealmzGlobalData& global,
      const std::string& scenario_dir,
      const std::string& scenario_name);
  ~RealmzScenarioData() = default;

  //////////////////////////////////////////////////////////////////////////////
  // <SCENARIO NAME>

  struct ScenarioMetadata {
    be_int32_t recommended_starting_levels;
    be_int32_t unknown_a1;
    be_int32_t start_level;
    be_int32_t start_x;
    be_int32_t start_y;
    uint8_t unknown_a2[0x28];
    uint8_t author_name_bytes;
    char author_name[0xFF];
  } __attribute__((packed));

  static ScenarioMetadata load_scenario_metadata(const std::string& filename);
  std::string disassemble_scenario_metadata() const;

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

  static std::vector<BattleDefinition> load_battle_index(const std::string& filename);
  std::string disassemble_battle(size_t index) const;
  std::string disassemble_all_battles() const;

  //////////////////////////////////////////////////////////////////////////////
  // DATA DD, DATA DDD, DATA ED3

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

  static std::vector<std::vector<APInfo>> load_ap_index(const std::string& filename);
  static std::vector<APInfo> load_xap_index(const std::string& filename);
  std::string disassemble_opcode(int16_t ap_code, int16_t arg_code) const;
  std::string disassemble_xap(int16_t ap_num) const;
  std::string disassemble_all_xaps() const;
  std::string disassemble_level_ap(int16_t level_num, int16_t ap_num, bool dungeon) const;
  std::string disassemble_level_rr(int16_t level_num, int16_t rr_num, bool dungeon) const;
  std::string disassemble_level_aps(int16_t level_num, bool dungeon) const;
  std::string disassemble_level_rrs(int16_t level_num, bool dungeon) const;
  std::string disassemble_all_level_aps_and_rrs(bool dungeon) const;

  //////////////////////////////////////////////////////////////////////////////
  // DATA DL

  struct MapData {
    be_int16_t data[90][90];

    void transpose();
  } __attribute__((packed));

  static std::vector<MapData> load_dungeon_map_index(const std::string& filename);
  std::string generate_dungeon_map_json(int16_t level_num) const;
  Image generate_dungeon_map(int16_t level_num, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h) const;

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

  static std::vector<SimpleEncounter> load_simple_encounter_index(const std::string& filename);
  std::string disassemble_simple_encounter(size_t index) const;
  std::string disassemble_all_simple_encounters() const;

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

  static std::vector<ComplexEncounter> load_complex_encounter_index(const std::string& filename);
  std::string disassemble_complex_encounter(size_t index) const;
  std::string disassemble_all_complex_encounters() const;

  //////////////////////////////////////////////////////////////////////////////
  // DATA EDCD

  struct ECodes {
    be_int16_t data[5];
  } __attribute__((packed));

  static std::vector<ECodes> load_ecodes_index(const std::string& filename);

  //////////////////////////////////////////////////////////////////////////////
  // DATA LD

  static std::vector<MapData> load_land_map_index(const std::string& filename);
  std::unordered_set<std::string> all_land_types() const;
  void populate_custom_tileset_configuration(const std::string& land_type, const RealmzGlobalData::TileSetDefinition& def);
  void populate_image_caches(ResourceFile& the_family_jewels_rsf);
  void add_custom_pattern(const std::string& land_type, Image& img);
  std::string generate_land_map_json(int16_t level_num) const;
  Image generate_land_map(
      int16_t level_num,
      uint8_t x0,
      uint8_t y0,
      uint8_t w,
      uint8_t h,
      std::unordered_set<int16_t>* used_negative_tiles = nullptr,
      std::unordered_map<std::string, std::unordered_set<uint8_t>>* used_positive_tiles = nullptr) const;

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

  static std::vector<MonsterDefinition> load_monster_index(const std::string& filename);
  std::string disassemble_monster(size_t index) const;
  std::string disassemble_all_monsters() const;

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

  static std::vector<PartyMap> load_party_map_index(const std::string& filename);
  std::string disassemble_party_map(size_t index) const;
  Image render_party_map(size_t index) const;
  std::string disassemble_all_party_maps() const;

  //////////////////////////////////////////////////////////////////////////////
  // DATA NI (same format as Data ID in global data, but only entries 800-999)

  // Use RealmzGlobalData::load_item_definitions to load this file
  std::string disassemble_all_custom_item_definitions() const;

  //////////////////////////////////////////////////////////////////////////////
  // DATA OD

  static std::vector<std::string> load_option_string_index(const std::string& filename);

  //////////////////////////////////////////////////////////////////////////////
  // DATA RD, DATA RDD

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

  static std::vector<MapMetadata> load_map_metadata_index(const std::string& filename);

  //////////////////////////////////////////////////////////////////////////////
  // DATA RI

  struct Restrictions {
    uint8_t description_bytes;
    char description[0xFF];
    be_int16_t max_characters;
    be_int16_t max_level_per_character;
    uint8_t forbidden_races[30];
    uint8_t forbidden_castes[30];
  } __attribute__((packed));

  static Restrictions load_restrictions(const std::string& filename);
  std::string disassemble_restrictions() const;

  //////////////////////////////////////////////////////////////////////////////
  // DATA SD

  struct Shop {
    /* 0000 */ be_uint16_t item_ids[1000];
    /* 07D0 */ uint8_t item_counts[1000];
    /* 0BB8 */ be_uint16_t inflation_percent;
    /* 0BBA */
  } __attribute__((packed));

  static std::vector<Shop> load_shop_index(const std::string& filename);
  std::string disassemble_shop(size_t index) const;
  std::string disassemble_all_shops() const;

  //////////////////////////////////////////////////////////////////////////////
  // DATA SD2, DATA DES

  static std::vector<std::string> load_string_index(const std::string& filename);

  //////////////////////////////////////////////////////////////////////////////
  // DATA SOLIDS

  static std::vector<bool> load_solids(const std::string& filename);
  std::string disassemble_solids() const;

  //////////////////////////////////////////////////////////////////////////////
  // DATA TD

  struct Treasure {
    be_int16_t item_ids[20];
    be_int16_t victory_points;
    be_int16_t gold;
    be_int16_t gems;
    be_int16_t jewelry;
  } __attribute__((packed));

  static std::vector<Treasure> load_treasure_index(const std::string& filename);
  std::string disassemble_treasure(size_t index) const;
  std::string disassemble_all_treasures() const;

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

  static std::vector<RogueEncounter> load_rogue_encounter_index(const std::string& filename);
  std::string disassemble_rogue_encounter(size_t index) const;
  std::string disassemble_all_rogue_encounters() const;

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

  static std::vector<TimeEncounter> load_time_encounter_index(const std::string& filename);
  std::string disassemble_time_encounter(size_t index) const;
  std::string disassemble_all_time_encounters() const;

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

  static GlobalMetadata load_global_metadata(const std::string& filename);
  std::string disassemble_global_metadata() const;

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

  static LandLayout load_land_layout(const std::string& filename);
  Image generate_layout_map(const LandLayout& l) const;

  //////////////////////////////////////////////////////////////////////////////

  const std::string& name_for_spell(uint16_t id) const;
  std::string desc_for_spell(uint16_t id) const;
  const RealmzGlobalData::ItemStrings& strings_for_item(uint16_t id) const;
  std::string desc_for_item(uint16_t id, const char* space = "") const;

  RealmzGlobalData& global;
  std::string scenario_dir;
  std::string name;
  std::unordered_map<std::string, RealmzGlobalData::TileSetDefinition> land_type_to_tileset_definition;
  std::unordered_map<std::string, Image> positive_pattern_cache;
  ResourceFile scenario_rsf;
  LandLayout layout;
  GlobalMetadata global_metadata;
  ScenarioMetadata scenario_metadata;
  Restrictions restrictions;
  std::vector<ECodes> ecodes;
  std::vector<RealmzGlobalData::ItemDefinition> custom_item_definitions;
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
  std::vector<std::string> monster_descriptions;
  std::vector<std::string> option_strings;
  std::vector<PartyMap> party_maps;
  std::vector<BattleDefinition> battles;
  std::vector<MonsterDefinition> monsters;
  std::vector<bool> solids;

  std::unordered_map<uint16_t, RealmzGlobalData::ItemStrings> item_strings;
  std::map<uint16_t, std::string> spell_names;
};

} // namespace ResourceDASM
