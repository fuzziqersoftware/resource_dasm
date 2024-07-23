#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Image.hh>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "RealmzScenarioData.hh"
#include "ResourceFile.hh"

namespace ResourceDASM {

using namespace phosg;

// See RealmzGlobalData.hh for a description of what each file contains.

struct RealmzSaveData {
  RealmzSaveData(RealmzScenarioData& global, const std::string& save_dir);
  ~RealmzSaveData() = default;

  //////////////////////////////////////////////////////////////////////////////
  // DATA A1

  struct SavedLandLevelState {
    /* 0000 */ RealmzScenarioData::APInfo aps[100];
    /* 0FA0 */ be_uint16_t tiles[90][90];
    /* 4EE8 */ RealmzScenarioData::MapMetadataFile metadata;
    /* 516C */ uint8_t los_revealed[90][90];
    /* 7110 */
  } __attribute__((packed));

  // TODO: Write parsers for this struct and Data B1/I1, and add support for disassembling saved games

  //////////////////////////////////////////////////////////////////////////////
  // DATA B1

  struct SavedDungeonLevelState {
    /* 0000 */ RealmzScenarioData::APInfo aps[100];
    /* 0FA0 */ be_uint16_t tiles[90][90];
    /* 4EE8 */ RealmzScenarioData::MapMetadataFile metadata;
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
      /* 0114 */ RealmzGlobalData::SpecialAbilities special_abilities;
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

  RealmzScenarioData& scenario;
};

} // namespace ResourceDASM
