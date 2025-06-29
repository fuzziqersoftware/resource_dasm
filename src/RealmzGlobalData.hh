#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <algorithm>
#include <map>
#include <phosg/Image.hh>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ResourceFile.hh"

namespace ResourceDASM {

using namespace phosg;

/**
 * Data Files contents:
 *   Combat Data BD: Same format as tileset definitions
 *   Custom Names.rsf: Spell, race, and caste names (STR# resources)
 *   Data AD: ? (appears to be a large number of individual flags)
 *   Data Caste: Caste definitions
 *   Data Castle BD: Indoor tileset definition
 *   Data Desert BD: Desert tileset definition
 *   Data ID.rsf: Item names and descriptions
 *   Data ID: Item definitions (same format as Data NI in scenario data, but
 *     contains IDs 0-799)
 *   Data P BD: Outdoor tileset definition
 *   Data Race: Race definitions
 *   Data S: Spell definitions (5 classes * 7 levels * 15 spells = 525
 *     definitions total; in the first 3 classes, the last 3 spells in each
 *     level are unused)
 *   Data Snow BD: Snow tileset definition
 *   Data SUB BD: Cave tileset definition
 *   Data Swamp BD: Abyss (swamp) tileset definition
 *   Portraits.rsf: Character portraits (ResourceFile)
 *   Scenario Names.rsf: Scenario names (ResourceFile)
 *   Tacticals.rsf: Character battle icons (ResourceFile)
 *   The Family Jewels.rsf: Everything else - negative tiles, item icons,
 *     projectile icons, spell icons, monster icons, UI icons and pictures, UI
 *     control definitions, cursors, dialog definitions, fonts, menu bars,
 *     patterns, sounds, strings for everything else (ResourceFile)
 *
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
 *   Data Spell: Probably spell definitions, but it's not the same format as
 *     Data S in the global data
 *   Data SID: ? (Legacy)
 *   Data TD: Treasures
 *   Data TD2: Rogue encounters
 *   Data TD3: Time encounters
 *   Global: Global metadata (start loc, start/shop/temple/etc. XAPs, ...)
 *   Layout: Land level layout map
 *   Scenario: Scenario configuration
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

struct RealmzGlobalData {
  explicit RealmzGlobalData(const std::string& dir);
  ~RealmzGlobalData() = default;

  //////////////////////////////////////////////////////////////////////////////
  // Things that are apparently hardcoded and don't appear in resources

  static const char* name_for_condition(size_t condition_id);
  static const char* name_for_age_group(size_t age_group);
  // flag_index=0 means the highest flag (e.g. 8000000000000000), not the lowest
  static const char* name_for_item_category_flag(uint8_t flag_index);
  static const char* name_for_race_flag(uint8_t flag_index);
  static const char* name_for_caste_flag(uint8_t flag_index);

  //////////////////////////////////////////////////////////////////////////////
  // DATA * BD (tileset definitions)

  struct TileDefinition {
    be_int16_t sound_id;
    be_uint16_t time_per_move;
    be_uint16_t solid_type; // 0 = not solid, 1 = solid to 1-box chars, 2 = solid
    be_uint16_t is_shore;
    be_uint16_t is_need_boat; // 1 = is boat, 2 = need boat
    be_uint16_t is_path;
    be_uint16_t blocks_los;
    be_uint16_t need_fly_float;
    be_uint16_t special_type; // 1 = trees, 2 = desert, 3 = shrooms, 4 = swamp, 5 = snow
    be_int16_t unknown5;
    be_int16_t battle_expansion[3][3]; // Indexed as [y][x]
    be_int16_t unknown6;
  } __attribute__((packed));

  struct TileSetDefinition {
    TileDefinition tiles[201];
    be_int16_t base_tile_id;
  } __attribute__((packed));

  static TileSetDefinition load_tileset_definition(const std::string& filename);
  static int16_t pict_resource_id_for_land_type(const std::string& land_type);
  static ImageRGB888 generate_tileset_definition_legend(const TileSetDefinition& ts, const ImageRGBA8888& positive_pattern);
  static std::string disassemble_tileset_definition(const TileSetDefinition& ts, const char* name);

  //////////////////////////////////////////////////////////////////////////////
  // CUSTOM NAMES.RSF

  static std::vector<std::string> load_race_names(const ResourceFile& rsf);
  static std::vector<std::string> load_caste_names(const ResourceFile& rsf);
  static std::map<uint16_t, std::string> load_spell_names(const ResourceFile& rsf);
  const std::string& name_for_spell(uint16_t id) const;

  //////////////////////////////////////////////////////////////////////////////
  // DATA CASTE

  struct SpecialAbilities {
    /* 00 */ be_int16_t sneak_attack;
    /* 02 */ be_int16_t unknown_a1[2];
    /* 06 */ be_int16_t major_wound;
    /* 08 */ be_int16_t detect_secret;
    /* 0A */ be_int16_t acrobatic_act;
    /* 0C */ be_int16_t detect_trap;
    /* 0E */ be_int16_t disarm_trap;
    /* 10 */ be_int16_t unknown_a2;
    /* 12 */ be_int16_t force_lock;
    /* 14 */ be_int16_t unknown_a3;
    /* 16 */ be_int16_t pick_lock;
    /* 18 */ be_int16_t unknown_a4;
    /* 1A */ be_int16_t turn_undead;
    /* 1C */
  } __attribute__((packed));

  struct Range {
    be_int16_t low;
    be_int16_t high;
  } __attribute__((packed));

  struct DRVsAbilities {
    /* 00 */ be_int16_t charm;
    /* 02 */ be_int16_t heat;
    /* 04 */ be_int16_t cold;
    /* 06 */ be_int16_t electric;
    /* 08 */ be_int16_t chemical;
    /* 0A */ be_int16_t mental;
    /* 0C */ be_int16_t magical;
    /* 0E */
  } __attribute__((packed));

  struct CasteDefinition {
    /* 0000 */ SpecialAbilities special_abilities_start;
    /* 001C */ SpecialAbilities special_abilities_level_up_delta;
    /* 0038 */ DRVsAbilities drv_adjust;
    /* 0046 */ be_int16_t unknown_a1;
    /* 0048 */ be_int16_t brawn_adjust;
    /* 004A */ be_int16_t knowledge_adjust;
    /* 004C */ be_int16_t judgment_adjust;
    /* 004E */ be_int16_t agility_adjust;
    /* 0050 */ be_int16_t vitality_adjust;
    /* 0052 */ be_int16_t luck_adjust;
    struct SpellTypeCapability {
      be_uint16_t enabled;
      be_uint16_t start_skill_level;
      be_uint16_t max_spell_level;
    } __attribute__((packed));
    /* 0054 */ SpellTypeCapability sorcerer_spell_capability;
    /* 005A */ SpellTypeCapability priest_spell_capability;
    /* 0060 */ SpellTypeCapability enchanter_spell_capability;
    /* 0066 */ uint8_t unknown_a2[6];
    /* 006C */ Range brawn_range;
    /* 0070 */ Range knowledge_range;
    /* 0074 */ Range judgment_range;
    /* 0078 */ Range agility_range;
    /* 007C */ Range vitality_range;
    /* 0080 */ Range luck_range;
    // Note: Conditions are indexed in the same order as in the Divinity
    // conditions window, in column-major order like everything else.
    // (In Retreat/Running is first, then Helpless, then Hindered, etc.)
    /* 0084 */ be_int16_t condition_levels[0x28];
    /* 00D4 */ be_uint16_t missile_capable;
    /* 00D6 */ be_int16_t missile_bonus_damage;
    /* 00D8 */ be_int16_t stamina_start;
    /* 00DA */ be_int16_t stamina_level_up_delta;
    /* 00DC */ be_int16_t strength_damage_bonus;
    /* 00DE */ be_int16_t strength_damage_bonus_max;
    /* 00E0 */ be_int16_t dodge_missile_chance_start;
    /* 00E2 */ be_int16_t dodge_missile_chance_level_up_delta;
    /* 00E4 */ be_int16_t melee_hit_chance_start;
    /* 00E6 */ be_int16_t melee_hit_chance_level_up_bonus;
    /* 00E8 */ be_int16_t missile_hit_chance_start;
    /* 00EA */ be_int16_t missile_hit_chance_level_up_bonus;
    /* 00EC */ be_int16_t hand_to_hand_damage_start;
    /* 00EE */ be_int16_t hand_to_hand_damage_level_up_bonus;
    /* 00F0 */ uint8_t unknown_a3[8];
    /* 00F8 */ be_int16_t caste_category;
    /* 00FA */ be_uint16_t min_age_group; // 1 = Youth, 5 = Senior
    /* 00FC */ be_int16_t movement_adjust;
    /* 00FE */ be_int16_t magic_resistance_mult;
    /* 0100 */ be_int16_t two_handed_weapon_adjust;
    /* 0102 */ be_int16_t max_stamina_bonus;
    /* 0104 */ be_int16_t bonus_half_attacks_per_round;
    /* 0106 */ be_int16_t max_attacks_per_round;
    // Note: These are NOT cumulative - that is, each value in this array
    // specifies how many VPs beyond the previous level's threshold are required
    // to achieve the following level.
    /* 0108 */ be_uint32_t victory_points_per_level[30];
    /* 0180 */ be_uint16_t starting_gold;
    /* 0182 */ be_uint16_t starting_items[20];
    // [0] in this array specifies at which level the character should have 3/2
    // attacks/round, [1] is for 2/1, [2] is for 5/2, etc.
    /* 01AA */ uint8_t attacks_per_round_level_thresholds[10];
    /* 01B4 */ be_uint64_t can_use_item_categories;
    /* 01BC */ be_int16_t portrait_id;
    /* 01BE */ be_uint16_t max_spells_per_round;
    /* 01C0 */ uint8_t unknown_a4[0x80]; // Possibly actually unused
    /* 0240 */
  } __attribute__((packed));

  static std::vector<CasteDefinition> load_caste_definitions(const std::string& filename);
  std::string disassemble_caste_definition(const CasteDefinition& c, size_t index, const char* name) const;
  std::string disassemble_all_caste_definitions() const;

  //////////////////////////////////////////////////////////////////////////////
  // DATA ID

  struct ItemDefinition {
    /* 00 */ be_int16_t strength_bonus;
    /* 02 */ be_uint16_t item_id; // Could also be string index (they're sequential anyway)
    /* 04 */ be_int16_t icon_id;
    /* 06 */ be_uint16_t weapon_type;
    /* 08 */ be_int16_t blade_type; // 0 = non-weapon, -1 = blunt, -2 = sharp
    /* 0A */ be_int16_t required_hands;
    /* 0C */ be_int16_t luck_bonus;
    /* 0E */ be_int16_t movement;
    /* 10 */ be_int16_t armor_rating;
    /* 12 */ be_int16_t magic_resist;
    /* 14 */ be_int16_t magic_plus;
    /* 16 */ be_int16_t spell_points;
    /* 18 */ be_int16_t sound_id;
    /* 1A */ be_int16_t weight;
    /* 1C */ be_int16_t cost; // If negative, item is unique
    /* 1E */ be_int16_t charge_count; // -1 = no charges
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
    /* 60 */ be_int16_t weight_per_charge;
    /* 62 */ be_uint16_t drop_on_empty;
    /* 64 */
  } __attribute__((packed));

  struct ItemStrings {
    std::string unidentified_name;
    std::string name;
    std::string description;
  };

  static std::vector<ItemDefinition> load_item_definitions(const std::string& filename);
  std::string disassemble_item_definition(const ItemDefinition& i, size_t item_id, const ItemStrings* strings) const;
  std::string disassemble_all_item_definitions() const;

  //////////////////////////////////////////////////////////////////////////////
  // DATA ID.RSF

  static std::unordered_map<uint16_t, ItemStrings> load_item_strings(const ResourceFile& rsf);
  const ItemStrings& strings_for_item(uint16_t id) const;

  //////////////////////////////////////////////////////////////////////////////
  // DATA RACE

  struct RaceDefinition {
    /* 0000 */ be_int16_t magic_using_hit_chance_adjust;
    /* 0002 */ be_int16_t undead_hit_chance_adjust;
    /* 0004 */ be_int16_t demon_hit_chance_adjust;
    /* 0006 */ be_int16_t reptilian_hit_chance_adjust;
    /* 0008 */ be_int16_t evil_hit_chance_adjust;
    /* 000A */ be_int16_t intelligent_hit_chance_adjust;
    /* 000C */ be_int16_t giant_hit_chance_adjust;
    /* 000E */ be_int16_t non_humanoid_hit_chance_adjust;
    /* 0010 */ SpecialAbilities special_ability_adjust;
    /* 002C */ DRVsAbilities drv_adjust;
    /* 003A */ uint8_t unknown_a1[2];
    /* 003C */ be_int16_t brawn_adjust;
    /* 003E */ be_int16_t knowledge_adjust;
    /* 0040 */ be_int16_t judgment_adjust;
    /* 0042 */ be_int16_t agility_adjust;
    /* 0044 */ be_int16_t vitality_adjust;
    /* 0046 */ be_int16_t luck_adjust;
    /* 0048 */ Range brawn_range;
    /* 004C */ Range knowledge_range;
    /* 0050 */ Range judgment_range;
    /* 0054 */ Range agility_range;
    /* 0058 */ Range vitality_range;
    /* 005C */ Range luck_range;
    /* 0060 */ be_uint16_t unknown_a2[8];
    /* 0070 */ be_int16_t condition_levels[0x28];
    /* 00C0 */ uint8_t unknown_a3[4];
    /* 00C4 */ be_int16_t base_movement;
    /* 00C6 */ be_int16_t magic_resistance_adjust;
    /* 00C8 */ be_int16_t two_handed_weapon_adjust;
    /* 00CA */ be_int16_t missile_weapon_adjust;
    /* 00CC */ be_int16_t base_half_attacks;
    /* 00CE */ be_int16_t max_attacks_per_round;
    /* 00D0 */ uint8_t possible_castes[30];
    /* 00EE */ Range age_ranges[5];
    struct AgeAdjustments {
      /* 00 */ int8_t brawn;
      /* 01 */ int8_t knowledge;
      /* 02 */ int8_t judgement;
      /* 03 */ int8_t agility;
      /* 04 */ int8_t vitality;
      /* 05 */ int8_t luck;
      /* 06 */ int8_t magic_resistance;
      /* 07 */ int8_t movement;
      /* 08 */ int8_t drv_chance_charm;
      /* 09 */ int8_t drv_chance_heat;
      /* 0A */ int8_t drv_chance_cold;
      /* 0B */ int8_t drv_chance_electric;
      /* 0C */ int8_t drv_chance_chemical;
      /* 0D */ int8_t drv_chance_mental;
      /* 0E */ int8_t drv_chance_magic;
      /* 0F */
    } __attribute__((packed));
    /* 0102 */ AgeAdjustments age_adjust[5];
    /* 014D */ uint8_t can_regenerate;
    /* 014E */ be_int16_t icon_set_number; // Not the same namespace as icon number
    /* 0150 */ be_uint64_t can_use_item_categories;
    // Race flags are in the "possible castes" config window in Divinity,
    // arranged (like everything else) in column-major order, with 8000
    // representing the first flag (short, then elvish, etc.)
    /* 0158 */ be_uint16_t race_flags;
    /* 015A */ uint8_t unknown_a4[0x3E]; // Possibly actually unused
    /* */
  } __attribute__((packed));

  static std::vector<RaceDefinition> load_race_definitions(const std::string& filename);
  std::string disassemble_race_definition(const RaceDefinition& r, size_t index, const char* name) const;
  std::string disassemble_all_race_definitions() const;

  //////////////////////////////////////////////////////////////////////////////
  // DATA S

  struct SpellDefinition {
    /* 00 */ int8_t base_range;
    /* 01 */ int8_t power_range;
    /* 02 */ int8_t que_icon;
    /* 03 */ int8_t hit_chance_adjust;
    /* 04 */ int8_t drv_adjust;
    /* 05 */ int8_t num_attacks;
    /* 06 */ int8_t can_rotate;
    /* 07 */ int8_t drv_adjust_per_level;
    /* 08 */ int8_t resist_type;
    /* 09 */ int8_t resist_adjust_per_level;
    /* 0A */ int8_t base_cost; // TODO: Can be negative; what does that mean?
    /* 0B */ int8_t damage_base_low;
    /* 0C */ int8_t damage_base_high;
    /* 0D */ int8_t damage_per_level_low;
    /* 0E */ int8_t damage_per_level_high;
    /* 0F */ int8_t duration_base_low;
    /* 10 */ int8_t duration_base_high;
    /* 11 */ int8_t duration_per_level_low;
    /* 12 */ int8_t duration_per_level_high;
    /* 13 */ int8_t cast_icon;
    /* 14 */ int8_t resolution_icon;
    /* 15 */ int8_t cast_sound;
    /* 16 */ int8_t resolution_sound;
    /* 17 */ int8_t target_type;
    /* 18 */ int8_t size;
    /* 19 */ int8_t effect;
    /* 1A */ int8_t spell_class;
    /* 1B */ int8_t damage_type;
    /* 1C */ int8_t usable_in_combat;
    /* 1D */ int8_t usable_in_camp;
    /* 1E */
  } __attribute__((packed));

  static std::map<uint16_t, SpellDefinition> load_spell_definitions(const std::string& filename);
  std::string disassemble_spell_definition(const SpellDefinition& s, uint16_t spell_id, const char* name) const;
  std::string disassemble_all_spell_definitions() const;

  //////////////////////////////////////////////////////////////////////////////

  std::string dir;
  ResourceFile global_rsf; // The Family Jewels or Bag of Holding
  ResourceFile portraits_rsf;
  ResourceFile tacticals_rsf;
  ResourceFile custom_names_rsf;
  ResourceFile scenario_names_rsf;
  ResourceFile data_id_rsf;

  std::vector<std::string> race_names;
  std::vector<std::string> caste_names;
  std::map<uint16_t, std::string> spell_names;
  std::unordered_map<uint16_t, ItemStrings> item_strings;

  std::unordered_map<std::string, TileSetDefinition> land_type_to_tileset_definition;

  std::vector<CasteDefinition> caste_definitions;
  std::vector<ItemDefinition> item_definitions;
  std::vector<RaceDefinition> race_definitions;
  std::map<uint16_t, SpellDefinition> spell_definitions;
};

std::string first_file_that_exists(const std::vector<std::string>& names);

} // namespace ResourceDASM
