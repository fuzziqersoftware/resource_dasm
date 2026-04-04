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
  RealmzSaveData(RealmzScenarioData& scen, const std::string& save_dir);
  ~RealmzSaveData() = default;

  //////////////////////////////////////////////////////////////////////////////
  // DATA A1

  struct LandLevelState {
    /* 0000 */ RealmzScenarioData::APInfo aps[100];
    /* 0FA0 */ be_uint16_t tiles[90][90];
    /* 4EE8 */ RealmzScenarioData::MapMetadataFile metadata;
    /* 516C */ uint8_t los_revealed[90][90];
    /* 7110 */
  } __attribute__((packed));

  static std::vector<LandLevelState> load_land_level_states(const std::string& filename);
  // ImageRGB888 generate_land_map(int16_t level_num, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h) const; // TODO

  //////////////////////////////////////////////////////////////////////////////
  // DATA B1

  struct DungeonLevelState {
    /* 0000 */ RealmzScenarioData::APInfo aps[100];
    /* 0FA0 */ be_uint16_t tiles[90][90];
    /* 4EE8 */ RealmzScenarioData::MapMetadataFile metadata;
    /* 516C */
  } __attribute__((packed));

  static std::vector<DungeonLevelState> load_dungeon_level_states(const std::string& filename);
  // ImageRGB888 generate_dungeon_map(int16_t level_num, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h) const; // TODO

  //////////////////////////////////////////////////////////////////////////////
  // DATA I1

  struct GameState {
    struct Item {
      int16_t id;
      uint8_t is_equipped;
      uint8_t is_identified;
      int16_t charge;
    } __attribute__((packed));

    struct Scroll {
      uint8_t spell_class;
      uint8_t spell_level;
      uint8_t spell_number;
      uint8_t power_level;
    } __attribute__((packed));

    struct SpellShortcut {
      /* 00 */ uint16_t spell_class;
      /* 02 */ uint16_t spell_level;
      /* 04 */ uint16_t spell_number;
      /* 06 */ uint16_t power_level;
      /* 08 */
    } __attribute__((packed));

    struct Character {
      /* 0000 */ be_int16_t version; // Realmz: character::version; should equal -3
      /* 0002 */ be_int16_t checksum1; // Realmz: character::verify1; should equal (level + max_hp + attack_bonus)
      /* 0004 */ be_int16_t chance_to_hit; // Realmz: character::tohit
      /* 0006 */ be_int16_t dodge_missile; // Realmz: character::dodge
      /* 0008 */ be_int16_t missile_adjust; // Realmz: character::missile
      /* 000A */ be_int16_t two_hand_adjust; // Realmz: character::twohand; floored at 0
      /* 000C */ be_int16_t traitor; // Temporary battle state; Realmz: character::traiter
      /* 000E */ be_int16_t half_attacks_per_round; // Realmz: character::normattacks
      /* 0010 */ be_int16_t was_attacked; // Temporary battle state; Realmz: character::beenattacked
      /* 0012 */ be_int16_t is_guarding; // Temporary battle state; Realmz: character::guarding
      /* 0014 */ be_int16_t target_id; // Temporary battle state; Realmz: character::target
      /* 0016 */ be_int16_t item_count; // Realmz: character::numitems
      /* 0018 */ be_int16_t weapon_sound; // Realmz: character::weaponsound
      /* 001A */ be_int16_t underneath; // Realmz: character::underneath
      /* 001C */ be_int16_t face; // Realmz: character::face
      /* 001E */ be_int16_t attack_bonus; // Realmz: character::attackbonus
      /* 0020 */ be_int16_t vitality_bonus; // Realmz: character::magco
      /* 0022 */ be_int16_t position; // Realmz: character::position
      /* 0024 */ be_int16_t luck_bonus; // Realmz: character::maglu
      /* 0026 */ be_int16_t brawn_bonus; // Realmz: character::magst
      /* 0028 */ be_int16_t magic_resistance; // Realmz: character::magres
      /* 002A */ be_int16_t movement_adjust; // Realmz: character::movebonus
      /* 002C */ be_int16_t armor_rating; // Realmz: character::ac
      /* 002E */ be_int16_t damage_plus; // Realmz: character::damage
      /* 0030 */ be_int16_t race; // Realmz: character::race
      /* 0032 */ be_int16_t caste; // Realmz: character::caste
      /* 0034 */ be_int16_t spell_class; // Realmz: character::spellcastertype
      /* 0036 */ be_int16_t gender; // Realmz: character::gender; 1 = male, 2 = female
      /* 0038 */ be_int16_t level; // Realmz: character::level
      /* 003A */ be_int16_t remaining_movement; // Presumably temporary battle state; Realmz: character::movement
      /* 003C */ be_int16_t movement_per_round; // Realmz: character::movementmax
      /* 003E */ be_int16_t remaining_half_attacks; // Realmz: character::attacks
      /* 0040 */ be_int16_t spell_count_per_level[7]; // Realmz: character::nspells
      /* 004E */ be_int16_t current_hp; // Realmz: character::stamina
      /* 0050 */ be_int16_t max_hp; // Realmz: character::staminamax
      /* 0052 */ be_int16_t portrait_id; // Realmz: character::pictid
      /* 0054 */ be_int16_t battle_icon_id; // Realmz: character::iconid
      /* 0056 */ be_int16_t current_sp; // Realmz: character::spellpoints
      /* 0058 */ be_int16_t max_sp; // Realmz: character::spellpointsmax
      /* 005A */ be_int16_t used_hands; // Realmz: character::nohands
      /* 005C */ be_int16_t melee_weapon_number; // Realmz: character::weaponnum
      /* 005E */ be_int16_t missile_weapon_number; // Realmz: character::missilenum
      /* 0060 */ be_int16_t bare_hand_damage; // Realmz: character::handtohand
      /* 0062 */ be_int16_t conditions[40]; // Realmz: character::condition
      /* 00B2 */ be_int16_t special_bonuses[12]; // Realmz: character::special; e.g. vs. magic using, vs. undead, etc.
      /* 00CA */ be_int16_t armor_item_ids[20]; // Realmz: character::armor; indexed by item type
      /* 00F2 */ be_int16_t special_ability_chances[15]; // Realmz: character::spec
      /* 0110 */ be_int16_t drv_charm; // Realmz: character::save[0]
      /* 0112 */ be_int16_t drv_heat; // Realmz: character::save[1]
      /* 0114 */ be_int16_t drv_cold; // Realmz: character::save[2]
      /* 0116 */ be_int16_t drv_electric; // Realmz: character::save[3]
      /* 0118 */ be_int16_t drv_chemical; // Realmz: character::save[4]
      /* 011A */ be_int16_t drv_mental; // Realmz: character::save[5]
      /* 011C */ be_int16_t drv_magic; // Realmz: character::save[6]
      /* 011E */ be_int16_t drv_special; // Realmz: character::save[7]
      /* 0120 */ be_int16_t current_age_group; // Realmz: character::currentagegroup
      /* 0122 */ be_int16_t checksum2; // Realmz: character::verify2; should equal (chance_to_hit + max_sp + brawn + knowledge)
      /* 0124 */ Item items[30]; // Realmz: character::items
      /* 01D8 */ Scroll scrolls[5]; // Realmz: character::scrollcase
      /* 01EC */ be_int32_t age; // Realmz: character::age
      /* 01F0 */ be_int32_t victory_points; // Realmz: character::exp; negative number
      /* 01F4 */ be_int16_t current_load; // Realmz: character::load
      /* 01F6 */ be_int16_t max_load; // Realmz: character::loadmax
      /* 01F8 */ be_int16_t gold; // Realmz: character::money[0]
      /* 01FA */ be_int16_t gems; // Realmz: character::money[1]
      /* 01FC */ be_int16_t jewelry; // Realmz: character::money[2]
      /* 01FE */ uint8_t has_turned; // Temporary battle state; Realmz: character::hasturned
      /* 01FF */ uint8_t can_heal; // Realmz: character::canheal
      /* 0200 */ uint8_t can_identify_items; // Realmz: character::canidentify
      /* 0201 */ uint8_t can_discover_magic; // Realmz: character::candetect
      /* 0202 */ uint8_t selected_weapon; // Temporary battle state; Realmz: character::toggle; 0 = melee, 1 = missile
      /* 0203 */ uint8_t is_bleeding; // Temporary battle state; Realmz: character::bleeding
      /* 0204 */ uint8_t is_in_battle; // Temporary battle state; Realmz: character::inbattle
      /* 0205 */ int8_t brawn; // Realmz: character::st
      /* 0206 */ int8_t knowledge; // Realmz: character::in
      /* 0207 */ int8_t judgment; // Realmz: character::wi
      /* 0208 */ int8_t agility; // Realmz: character::de
      /* 0209 */ int8_t vitality; // Realmz: character::co
      /* 020A */ int8_t luck; // Realmz: character::lu
      /* 020B */ uint8_t learned_spells[7][12]; // Realmz: character::cspells
      /* 025F */ char name[30]; // Realmz: character::name; max length is 19 (enforced during creation)
      /* 027D */ uint8_t unused1; // Padding byte not declared in original struct
      /* 027E */ be_int16_t checksum3; // Realmz: character::verify3; should equal (armor_rating + 2 * max_hp + agility + magic_resistance)
      /* 0280 */ be_int32_t prestige_damage_taken; // Realmz: character::damagetaken
      /* 0284 */ be_int32_t prestige_damage_given; // Realmz: character::damagegiven
      /* 0288 */ be_int32_t prestige_hits_given; // Realmz: character::hitsgiven
      /* 028C */ be_int32_t prestige_hits_taken; // Realmz: character::hitstaken
      /* 0290 */ be_int32_t prestige_missed_attacks; // Realmz: character::imissed
      /* 0294 */ be_int32_t prestige_dodged_attacks; // Realmz: character::umissed
      /* 0298 */ be_int32_t prestige_enemies_killed; // Realmz: character::kills
      /* 029C */ be_int32_t prestige_times_killed; // Realmz: character::deaths
      /* 02A0 */ be_int32_t prestige_times_unconscious; // Realmz: character::knockouts
      /* 02A4 */ be_int32_t prestige_combat_spells_cast; // Realmz: character::spellscast
      /* 02A8 */ be_int32_t prestige_undead_destroyed; // Realmz: character::destroyed
      /* 02AC */ be_int32_t prestige_undead_turned; // Realmz: character::turns
      /* 02B0 */ be_int32_t prestige_penalty_points; // Realmz: character::prestigepenelty; subtracted (negative is good)
      /* 02B4 */ SpellShortcut spell_shortcuts[10]; // Realmz: character::definespells
      /* 0304 */ be_int16_t max_spells_per_round; // Realmz: character::maxspellsattacks
      /* 0306 */ be_int16_t spells_cast_this_round; // Temporary battle state; Realmz: character::spellsofar
      /* 0308 */ uint8_t unused2[0x60];
      /* 0368 */
    } __attribute__((packed));

    struct WindowsFormat {
      /* 0000 */ be_int16_t scenario_number;
      /* 0002 */ be_int16_t difficulty_level; // Realmz: howhard
      /* 0004 */ be_int16_t fatigue; // Realmz: fat
      /* 0006 */ be_int16_t party_conditions[10];
      /* 001A */ be_float experience_multiplier; // Realmz: percent
      /* 001E */ be_float treasure_multiplier; // Realmz: hardpercent
      /* 0022 */ Character character[6];
      /* 1492 */
    } __attribute__((packed));

    struct MacFormat {
      /* 0000 */ Character character[6];
      /* 1470 */ be_int16_t scenario_number;
      /* 1472 */ be_int16_t difficulty_level; // Realmz: howhard
      /* 1474 */ be_int16_t fatigue; // Realmz: fat
      /* 1476 */ be_int16_t party_conditions[10];
      /* 148A */ be_float experience_multiplier; // Realmz: percent
      /* 148E */ be_float treasure_multiplier; // Realmz: hardpercent
      /* 1492 */
    } __attribute__((packed));

    struct CommonFormat {
      /* 1492 */ int8_t delta_x;
      /* 1493 */ int8_t delta_y;
      /* 1494 */ char scenario_path[0x100]; // C-string
      /* 1594 */ int8_t killparty;
      /* 1595 */ int8_t charnum;
      /* 1596 */ int8_t head;
      /* 1597 */ int8_t currentshop;
      /* 1598 */ int8_t quest_flags[0x80];
      /* 1618 */ int8_t commandkey;
      /* 1619 */ int8_t cl;
      /* 161A */ int8_t cr;
      /* 161B */ int8_t charselectnew;
      /* 161C */ int8_t maps_found[20];
      /* 1630 */ int8_t inscroll;
      /* 1631 */ int8_t indung;
      /* 1632 */ int8_t view;
      /* 1633 */ int8_t editon;
      /* 1634 */ int8_t incamp;
      /* 1635 */ int8_t initems;
      /* 1636 */ int8_t inswap;
      /* 1637 */ int8_t inbooty;
      /* 1638 */ int8_t shopavail;
      /* 1639 */ int8_t campavail;
      /* 163A */ int8_t intemple;
      /* 163B */ int8_t inshop;
      /* 163C */ int8_t swapavail;
      /* 163D */ int8_t templeavail;
      /* 163E */ int8_t tradeavail;
      /* 163F */ int8_t canshop;
      /* 1640 */ int8_t shopequip;
      /* 1641 */ int8_t lastcaste;
      /* 1642 */ int8_t lastspell[6][2];
      /* 164E */ int8_t combatround;
      /* 164F */ int8_t bigbadbug;
      /* 1650 */ be_int32_t x;
      /* 1654 */ be_int32_t y;
      /* 1658 */ be_int32_t wallx;
      /* 165C */ be_int32_t wally;
      /* 1660 */ be_int32_t dunglevel;
      /* 1664 */ be_int32_t partyx;
      /* 1668 */ be_int32_t partyy;
      /* 166C */ be_int32_t reclevel;
      /* 1670 */ be_int32_t maxlevel;
      /* 1674 */ be_int32_t landlevel;
      /* 1678 */ be_int32_t lookx;
      /* 167C */ be_int32_t looky;
      /* 1680 */ be_int32_t fieldx;
      /* 1684 */ be_int32_t fieldy;
      /* 1688 */ be_int32_t floorx;
      /* 168C */ be_int32_t floory;
      /* 1690 */ be_int32_t pool_gold;
      /* 1694 */ be_int32_t pool_gems;
      /* 1698 */ be_int32_t pool_jewelry;
      /* 169C */ be_int16_t time_secs;
      /* 169E */ be_int16_t time_mins;
      /* 16A0 */ be_int16_t time_hours;
      /* 16A2 */ be_int16_t time_day_of_month;
      /* 16A4 */ be_int16_t time_month;
      /* 16A6 */ be_int16_t time_year;
      /* 16A8 */ be_int16_t time_day_of_week;
      /* 16AA */ be_int16_t time_day_of_year;
      /* 16AC */ be_int16_t time_is_dst;
      /* 16AE */ int8_t multiview;
      /* 16AF */ int8_t updatedir;
      /* 16B0 */ int8_t monsterset;
      /* 16B1 */ int8_t bankavailable;
      // It seems they forgot to byteswap these fields, so they're stored little-endian on Windows
      union {
        struct {
          /* 16B2 */ le_int32_t gold;
          /* 16B6 */ le_int32_t gems;
          /* 16BA */ le_int32_t jewelry;
          /* 16BE */ le_int16_t templecost;
        } __attribute__((packed)) le;
        struct {
          /* 16B2 */ be_int32_t gold;
          /* 16B6 */ be_int32_t gems;
          /* 16BA */ be_int32_t jewelry;
          /* 16BE */ be_int16_t templecost;
        } __attribute__((packed)) be;
      } __attribute__((packed)) bank;
      /* 16C0 */ int8_t inboat;
      /* 16C1 */ int8_t boatright;
      /* 16C2 */ int8_t canencounter;
      /* 16C3 */ int8_t xydisplayflag;
      /* 16C4 */ int8_t unused[8];
      /* 16CC */ RealmzScenarioData::MonsterDefinition npcs[20];
      /* 2734 */ be_int16_t heldover;
      /* 2736 */ be_int16_t deduction;
      /* 2738 */ be_int32_t savedserial;
      /* 273C */ int8_t canpriestturn;
      /* 273D */ be_int16_t musictoggle[20];
      /* 2765 */ uint8_t doauto[6];
      /* 276B */ SpellShortcut spell_shortcuts[6][10]; // Realmz: definespells
      /* 294B */ uint8_t notes[3000];
      /* 3503 */ be_int16_t cancamp;
      /* 3505 */ Item stored_items[6][30];
      /* 393D */ be_int32_t stored_gold;
      /* 3941 */ be_int32_t stored_gems;
      /* 3945 */ be_int32_t stored_jewelry;
      /* 3949 */ char registration_name[40];
      /* 3971 */ be_int32_t testlocation;
      /* 3975 */ uint8_t spellcasting;
      /* 3976 */ uint8_t spellcharging;
      /* 3977 */ uint8_t monstercasting;
      /* 3978 */ uint8_t spareboolean;
      /* 3979 */
    } __attribute__((packed));

    union {
      WindowsFormat windows;
      MacFormat mac;
    } __attribute__((packed));
    CommonFormat common;
  } __attribute__((packed));

  static GameState load_game_state(const std::string& filename);
  std::string disassemble_game_state() const;

  std::string disassemble_all_land_level_states() const;
  std::string disassemble_all_dungeon_level_states() const;
  std::string disassemble_all_shops() const;
  std::string disassemble_all_simple_encounters() const;
  std::string disassemble_all_complex_encounters() const;
  std::string disassemble_all_rogue_encounters() const;
  std::string disassemble_all_time_encounters() const;

  RealmzScenarioData& scenario;

  std::vector<LandLevelState> land_level_states; // Data A1
  std::vector<DungeonLevelState> dungeon_level_states; // Data B1
  std::vector<RealmzScenarioData::Shop> shop_states; // Data E1
  std::vector<RealmzScenarioData::SimpleEncounter> simple_encounters; // Data F1
  std::vector<RealmzScenarioData::ComplexEncounter> complex_encounters; // Data G1
  std::vector<RealmzScenarioData::RogueEncounter> rogue_encounters; // Data H1
  GameState game_state; // Data I1
  std::vector<RealmzScenarioData::TimeEncounter> time_encounters; // Data TD3
};

} // namespace ResourceDASM
