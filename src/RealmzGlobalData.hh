#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Image.hh>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ResourceFile.hh"

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
 *   Data S: ?
 *   Data Snow BD: Snow tileset definition
 *   Data SUB BD: Cave tileset definition
 *   Data Swamp BD: Abyss (swamp) tileset definition
 *   Portraits.rsf: Character portraits
 *   Scenario Names.rsf: Scenario names
 *   Tacticals.rsf: Character battle icons
 *   The Family Jewels.rsf: Everything else - negative tiles, item icons,
 *     projectile icons, spell icons, monster icons, UI icons and pictures, UI
 *     control definitions, cursors, dialog definitions, fonts, menu bars,
 *     patterns, sounds, strings for everything else
 *
 * See RealmzScenarioData.hh for formats used for scenario-specific data.
 */

struct TileDefinition {
  be_uint16_t sound_id;
  be_uint16_t time_per_move;
  be_uint16_t solid_type; // 0 = not solid, 1 = solid to 1-box chars, 2 = solid
  be_uint16_t is_shore;
  be_uint16_t is_need_boat; // 1 = is boat, 2 = need boat
  be_uint16_t is_path;
  be_uint16_t blocks_los;
  be_uint16_t need_fly_float;
  be_uint16_t special_type; // 1 = trees, 2 = desert, 3 = shrooms, 4 = swamp, 5 = snow
  int16_t unknown5;
  be_int16_t battle_expansion[9];
  int16_t unknown6;
} __attribute__((packed));

struct TileSetDefinition {
  TileDefinition tiles[201];
  be_uint16_t base_tile_id;
} __attribute__((packed));

TileSetDefinition load_tileset_definition(const std::string& filename);

struct ItemInfo {
  std::string unidentified_name;
  std::string name;
  std::string description;
};

struct Range {
  be_uint16_t low;
  be_uint16_t high;
} __attribute__((packed));

struct ActionSuccessChances {
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

struct DRVsChances {
  /* 00 */ be_int16_t drv_penalty_charm;
  /* 02 */ be_int16_t drv_penalty_heat;
  /* 04 */ be_int16_t drv_penalty_cold;
  /* 06 */ be_int16_t drv_penalty_electric;
  /* 08 */ be_int16_t drv_penalty_chemical;
  /* 0A */ be_int16_t drv_penalty_mental;
  /* 0C */ be_int16_t drv_penalty_magical;
  /* 0E */
} __attribute__((packed));

struct RaceDefinition {
  /* 0000 */ be_uint16_t magic_using_hit_chance_adj;
  /* 0002 */ be_uint16_t undead_hit_chance_adj;
  /* 0004 */ be_uint16_t demon_hit_chance_adj;
  /* 0006 */ be_uint16_t reptilian_hit_chance_adj;
  /* 0008 */ be_uint16_t evil_hit_chance_adj;
  /* 000A */ be_uint16_t intelligent_hit_chance_adj;
  /* 000C */ be_uint16_t giant_hit_chance_adj;
  /* 000E */ be_uint16_t non_humanoid_hit_chance_adj;
  /* 0010 */ ActionSuccessChances action_chance_adj;
  /* 002C */ DRVsChances drv_chance_adj;
  /* 003A */ uint8_t unknown_a1[2];
  /* 003C */ be_int16_t brawn_adj;
  /* 003E */ be_int16_t knowledge_adj;
  /* 0040 */ be_int16_t judgment_adj;
  /* 0042 */ be_int16_t agility_adj;
  /* 0044 */ be_int16_t vitality_adj;
  /* 0046 */ be_int16_t luck_adj;
  /* 0048 */ Range brawn_range;
  /* 004C */ Range knowledge_range;
  /* 0050 */ Range judgment_range;
  /* 0054 */ Range agility_range;
  /* 0058 */ Range vitality_range;
  /* 005C */ Range luck_range;
  /* 0060 */ uint8_t unknown_a2[8];
  /* 0070 */ be_int16_t condition_levels[0x28];
  /* 00C0 */ uint8_t unknown_a3[4];
  /* 00C4 */ be_uint16_t base_movement;
  /* 00C6 */ be_int16_t magic_resistance_adj;
  /* 00C8 */ be_int16_t two_handed_weapon_adj;
  /* 00CA */ be_int16_t missile_weapon_adj;
  /* 00CC */ be_uint16_t base_half_attacks;
  /* 00CE */ be_uint16_t max_attacks_per_round;
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
  /* 0102 */ AgeAdjustments age_adjustments[5];
  /* 014D */ uint8_t can_regenerate;
  /* 014E */ be_int16_t icon_set_number; // Not the same namespace as icon number
  /* 0150 */ be_uint64_t item_usability_flags;
  // Race flags are in the "possible castes" config window in Divinity,
  // arranged (like everything else) in column-major order, with 8000
  // representing the first flag
  /* 0158 */ be_uint16_t race_flags;
  /* 015A */ uint8_t unknown_a4[0x3E]; // Possibly actually unused
} __attribute__((packed));

struct CasteDefinition {
  /* 0000 */ ActionSuccessChances action_chances_start;
  /* 001C */ ActionSuccessChances action_changes_level_up_delta;
  /* 0038 */ be_int16_t drv_penalty_charm;
  /* 003A */ be_int16_t drv_penalty_heat;
  /* 003C */ be_int16_t drv_penalty_cold;
  /* 003E */ be_int16_t drv_penalty_electric;
  /* 0040 */ be_int16_t drv_penalty_chemical;
  /* 0042 */ be_int16_t drv_penalty_mental;
  /* 0044 */ be_int16_t drv_penalty_magical;
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
  /* 0054 */ be_int16_t sorcerer_spell_capability;
  /* 005A */ be_int16_t priest_spell_capability;
  /* 0060 */ be_int16_t enchanter_spell_capability;
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
  /* 00FC */ be_int16_t movement_adj;
  /* 00FE */ be_int16_t magic_resistance_mult;
  /* 0100 */ be_int16_t two_handed_weapon_adj;
  /* 0102 */ be_uint16_t max_stamina_bonus;
  /* 0104 */ be_uint16_t bonus_half_attacks_per_round;
  /* 0106 */ be_uint16_t max_attacks_per_round;
  // Note: These are NOT cumulative - that is, each value in this array
  // specifies how many VPs beyond the previous level's threshold are required
  // to achieve the following level.
  /* 0108 */ be_uint32_t victory_points_per_level[30];
  /* 0180 */ be_uint16_t starting_gold;
  /* 0182 */ be_uint16_t starting_items[20];
  // [0] in this array specifies at which level the character should have 3/2
  // attacks/round, [1] is for 2/1, [2] is for 5/2, etc.
  /* 01AA */ uint8_t attacks_per_round_level_thresholds[10];
  /* 01B4 */ be_uint64_t item_class_usability_flags;
  /* 01BC */ be_int16_t portrait_id;
  /* 01BE */ be_uint16_t max_spells_per_round;
  /* 01C0 */ uint8_t unknown_a4[0x80]; // Possibly actually unused
} __attribute__((packed));

struct RealmzGlobalData {
  explicit RealmzGlobalData(const std::string& dir);
  ~RealmzGlobalData() = default;

  void load_default_tilesets();
  static std::unordered_map<uint16_t, ItemInfo> parse_item_info(ResourceFile& rsf);
  static std::unordered_map<uint16_t, std::string> parse_spell_names(ResourceFile& rsf);

  const ItemInfo& info_for_item(uint16_t id) const;
  const std::string& name_for_spell(uint16_t id) const;

  std::string dir;
  ResourceFile global_rsf;
  ResourceFile portraits_rsf;
  ResourceFile names_rsf;
  ResourceFile data_id_rsf;

  std::vector<std::string> race_names;
  std::vector<std::string> caste_names;
  std::unordered_map<uint16_t, ItemInfo> item_info;
  std::unordered_map<uint16_t, std::string> spell_names;

  std::unordered_map<std::string, TileSetDefinition> land_type_to_tileset_definition;
};

std::string first_file_that_exists(const std::vector<std::string>& names);
int16_t resource_id_for_land_type(const std::string& land_type);
Image generate_tileset_definition_legend(
    const TileSetDefinition& ts, const Image& positive_pattern);
