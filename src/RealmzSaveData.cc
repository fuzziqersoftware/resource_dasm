#include "RealmzSaveData.hh"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <filesystem>
#include <phosg/Encoding.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "IndexFormats/Formats.hh"
#include "ResourceFile.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {

RealmzSaveData::RealmzSaveData(RealmzScenarioData& scen, const std::string& save_dir)
    : scenario(scen),
      land_level_states(this->load_land_level_states(std::format("{}/Data A1", save_dir))),
      dungeon_level_states(this->load_dungeon_level_states(std::format("{}/Data B1", save_dir))),
      shop_states(RealmzScenarioData::load_shop_index(std::format("{}/Data E1", save_dir))),
      simple_encounters(RealmzScenarioData::load_simple_encounter_index(std::format("{}/Data F1", save_dir))),
      complex_encounters(RealmzScenarioData::load_complex_encounter_index(std::format("{}/Data G1", save_dir))),
      rogue_encounters(RealmzScenarioData::load_rogue_encounter_index(std::format("{}/Data H1", save_dir))),
      game_state(this->load_game_state(std::format("{}/Data I1", save_dir))),
      time_encounters(RealmzScenarioData::load_time_encounter_index(std::format("{}/Data TD3", save_dir))) {}

std::vector<RealmzSaveData::LandLevelState> RealmzSaveData::load_land_level_states(const std::string& filename) {
  auto ret = phosg::load_vector_file<LandLevelState>(filename);
  for (auto& st : ret) {
    st.map.transpose();
    for (size_t y = 0; y < 90; y++) {
      for (size_t x = y + 1; x < 90; x++) {
        uint8_t t = st.los_revealed[y][x];
        st.los_revealed[y][x] = st.los_revealed[x][y];
        st.los_revealed[x][y] = t;
      }
    }
  }
  return ret;
}

ImageRGB888 RealmzSaveData::generate_land_map(
    int16_t level_num, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h, bool show_random_rects) const {
  const auto& level_state = this->land_level_states.at(level_num);
  RealmzScenarioData::MapMetadata meta = level_state.metadata.parse();
  int16_t partyx = -1, partyy = -1;
  if (!this->game_state.common.indung && (this->game_state.common.landlevel == level_num)) {
    partyx = this->game_state.common.partyx + this->game_state.common.lookx;
    partyy = this->game_state.common.partyy + this->game_state.common.looky;
  }
  return this->scenario.generate_land_map(
      level_num,
      x0,
      y0,
      w,
      h,
      show_random_rects,
      partyx,
      partyy,
      &level_state.map,
      &meta,
      level_state.aps,
      &level_state.los_revealed[0][0]);
}

ImageRGB888 RealmzSaveData::generate_dungeon_map(
    int16_t level_num, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h, bool show_random_rects) const {
  const auto& level_state = this->dungeon_level_states.at(level_num);
  RealmzScenarioData::MapMetadata meta = level_state.metadata.parse();
  return this->scenario.generate_dungeon_map(
      level_num, x0, y0, w, h, show_random_rects, true, &level_state.map, &meta, level_state.aps);
}

ImageRGB888 RealmzSaveData::generate_layout_map(
    const RealmzScenarioData::LandLayout& l, bool show_random_rects) const {
  auto generate_level_map = [this](int16_t level_num, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h, bool show_random_rects) -> ImageRGB888 {
    return this->generate_land_map(level_num, x0, y0, w, h, show_random_rects);
  };
  return this->scenario.generate_layout_map(l, show_random_rects, generate_level_map);
}

std::vector<RealmzSaveData::DungeonLevelState> RealmzSaveData::load_dungeon_level_states(const std::string& filename) {
  return phosg::load_vector_file<DungeonLevelState>(filename);
}

// ImageRGB888 RealmzSaveData::generate_dungeon_map(
//     int16_t level_num, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h) const {
//   TODO: Implement this. Similar to the above; tile discovered state is in the bitflags though
// }

RealmzSaveData::GameState RealmzSaveData::load_game_state(const std::string& filename) {
  return phosg::load_object_file<GameState>(filename);
}

std::string RealmzSaveData::disassemble_game_state() const {
  const auto& gs = this->game_state;

  // Figure out which format version it is
  size_t mac_points = 0;
  size_t windows_points = 0;
  for (size_t z = 0; z < 6; z++) {
    if (gs.mac.character[z].version < 0) {
      mac_points++;
    }
    if (gs.windows.character[z].version < 0) {
      windows_points++;
    }
  }
  if ((mac_points == 6) == (windows_points == 6)) {
    throw std::runtime_error(std::format("Unable to determine source platform for save file ({}/{})", mac_points, windows_points));
  }
  bool is_windows = (windows_points == 6);

  std::vector<std::string> strn_128;
  try {
    auto dec = this->scenario.scenario_rsf.decode_STRN(128);
    strn_128 = std::move(dec.strs);
  } catch (const std::exception& e) {
  }

  auto line_for_array = [&](const char* prefix, auto* arr, size_t size, auto&& str_fn) -> std::string {
    std::deque<std::string> tokens;
    for (size_t z = 0; z < size; z++) {
      std::string token = str_fn(z, arr[z]);
      if (!token.empty()) {
        tokens.emplace_back(std::move(token));
      }
    }
    return std::format("{}=[{}]", prefix, phosg::join(tokens, ", "));
  };

  auto format_item = [this](const RealmzSaveData::GameState::Item& item) -> std::string {
    std::vector<std::string> tokens;
    tokens.emplace_back(std::format("({})", item.id));
    try {
      const auto& strs = this->scenario.strings_for_item(item.id);
      tokens.emplace_back(strs.display_name());
    } catch (const out_of_range&) {
      tokens.emplace_back("(missing)");
    }
    if (item.is_equipped) {
      tokens.emplace_back("(equipped)");
    }
    if (!item.is_identified) {
      tokens.emplace_back("(unidentified)");
    }
    if (item.charge > 0) {
      tokens.emplace_back(std::format("x{}", item.charge));
    }
    return phosg::join(tokens, " ");
  };

  std::deque<std::string> ret;
  ret.emplace_back("===== GAME STATE");
  auto disassemble_platform_specific_section = [this, &ret, &line_for_array, &format_item]<typename T>(const T& sec) -> void {
    ret.emplace_back(std::format("  scenario_number={}", sec.scenario_number));
    ret.emplace_back(std::format("  difficulty_level={}", sec.difficulty_level));
    ret.emplace_back(std::format("  fatigue={}", sec.fatigue));
    ret.emplace_back(line_for_array("  party_conditions", sec.party_conditions, 10, [](size_t z, int16_t v) -> std::string {
      if (v < 0) {
        return std::format("{} ({}, permanent)", RealmzGlobalData::name_for_party_condition(z), v);
      } else if (v > 0) {
        return std::format("{} ({} rounds)", RealmzGlobalData::name_for_party_condition(z), v);
      } else {
        return "";
      }
    }));
    ret.emplace_back(std::format("  experience_multiplier={}", sec.experience_multiplier));
    ret.emplace_back(std::format("  treasure_multiplier={}", sec.treasure_multiplier));
    for (size_t char_index = 0; char_index < 6; char_index++) {
      const GameState::Character& ch = sec.character[char_index];
      ret.emplace_back(std::format("  characters[{}]=", char_index));
      ret.emplace_back(std::format("    version={}", ch.version));
      int16_t expected_checksum1 = ch.level + ch.max_hp + ch.attack_bonus;
      if (ch.checksum1 == expected_checksum1) {
        ret.emplace_back(std::format("    checksum1={} (correct)", ch.checksum1));
      } else {
        ret.emplace_back(std::format("    checksum1={} (INCORRECT: expected {})", ch.checksum1, expected_checksum1));
      }
      ret.emplace_back(std::format("    chance_to_hit={}", ch.chance_to_hit));
      ret.emplace_back(std::format("    dodge_missile={}", ch.dodge_missile));
      ret.emplace_back(std::format("    missile_adjust={}", ch.missile_adjust));
      ret.emplace_back(std::format("    two_hand_adjust={}", ch.two_hand_adjust));
      ret.emplace_back(std::format("    traitor={}", ch.traitor));
      ret.emplace_back(std::format("    half_attacks_per_round={}", ch.half_attacks_per_round));
      ret.emplace_back(std::format("    was_attacked={}", ch.was_attacked));
      ret.emplace_back(std::format("    is_guarding={}", ch.is_guarding));
      ret.emplace_back(std::format("    target_id={}", ch.target_id));
      ret.emplace_back(std::format("    item_count={}", ch.item_count));
      ret.emplace_back(std::format("    weapon_sound={}", ch.weapon_sound));
      ret.emplace_back(std::format("    underneath={}", ch.underneath));
      ret.emplace_back(std::format("    face={}", ch.face));
      ret.emplace_back(std::format("    attack_bonus={}", ch.attack_bonus));
      ret.emplace_back(std::format("    vitality_bonus={}", ch.vitality_bonus));
      ret.emplace_back(std::format("    position={}", ch.position));
      ret.emplace_back(std::format("    luck_bonus={}", ch.luck_bonus));
      ret.emplace_back(std::format("    brawn_bonus={}", ch.brawn_bonus));
      ret.emplace_back(std::format("    magic_resistance={}", ch.magic_resistance));
      ret.emplace_back(std::format("    movement_adjust={}", ch.movement_adjust));
      ret.emplace_back(std::format("    armor_rating={}", ch.armor_rating));
      ret.emplace_back(std::format("    damage_plus={}", ch.damage_plus));
      ret.emplace_back(std::format("    race={}", ch.race));
      ret.emplace_back(std::format("    caste={}", ch.caste));
      ret.emplace_back(std::format("    spell_class={}", ch.spell_class));
      const char* gender_name;
      if (ch.gender == 1) {
        gender_name = "male";
      } else if (ch.gender == 1) {
        gender_name = "female";
      } else {
        gender_name = "unknown";
      }
      ret.emplace_back(std::format("    gender={} ({})", ch.gender, gender_name));
      ret.emplace_back(std::format("    level={}", ch.level));
      ret.emplace_back(std::format("    remaining_movement={}", ch.remaining_movement));
      ret.emplace_back(std::format("    movement_per_round={}", ch.movement_per_round));
      ret.emplace_back(std::format("    remaining_half_attacks={}", ch.remaining_half_attacks));
      ret.emplace_back(std::format("    spell_count_per_level=[{}, {}, {}, {}, {}, {}, {}]", ch.spell_count_per_level[0],
          ch.spell_count_per_level[1], ch.spell_count_per_level[2], ch.spell_count_per_level[3],
          ch.spell_count_per_level[4], ch.spell_count_per_level[5], ch.spell_count_per_level[6]));
      ret.emplace_back(std::format("    current_hp={}", ch.current_hp));
      ret.emplace_back(std::format("    max_hp={}", ch.max_hp));
      ret.emplace_back(std::format("    portrait_id={}", ch.portrait_id));
      ret.emplace_back(std::format("    battle_icon_id={}", ch.battle_icon_id));
      ret.emplace_back(std::format("    current_sp={}", ch.current_sp));
      ret.emplace_back(std::format("    max_sp={}", ch.max_sp));
      ret.emplace_back(std::format("    used_hands={}", ch.used_hands));
      ret.emplace_back(std::format("    melee_weapon_number={}", ch.melee_weapon_number));
      ret.emplace_back(std::format("    missile_weapon_number={}", ch.missile_weapon_number));
      ret.emplace_back(std::format("    bare_hand_damage={}", ch.bare_hand_damage));
      ret.emplace_back(line_for_array("    conditions", ch.conditions, 40, [](size_t z, int16_t v) -> std::string {
        if (v < 0) {
          return std::format("{} ({}, permanent)", RealmzGlobalData::name_for_condition(z), v);
        } else if (v > 0) {
          return std::format("{} ({} rounds)", RealmzGlobalData::name_for_condition(z), v);
        } else {
          return "";
        }
      }));
      ret.emplace_back(line_for_array("    special_bonuses", ch.special_bonuses, 12, [](size_t z, int16_t v) -> std::string {
        return (v == 0) ? "" : std::format("+{} {}", v, RealmzGlobalData::name_for_special_bonus(z));
      }));
      ret.emplace_back(line_for_array("    armor_item_ids", ch.armor_item_ids, 20, [this](size_t z, int16_t v) -> std::string {
        try {
          const auto& strs = this->scenario.strings_for_item(v);
          return std::format("[{}] = {} ({})", z, v, strs.display_name());
        } catch (const out_of_range&) {
          return std::format("[{}] = {} (missing)", z, v);
        }
      }));
      ret.emplace_back(line_for_array("    special_ability_chances", ch.special_ability_chances, 15, [](size_t z, int16_t v) -> std::string {
        return (v == 0) ? "" : std::format("{}% {}", v, RealmzGlobalData::name_for_special_ability(z));
      }));
      ret.emplace_back(std::format("    drv_charm={}", ch.drv_charm));
      ret.emplace_back(std::format("    drv_heat={}", ch.drv_heat));
      ret.emplace_back(std::format("    drv_cold={}", ch.drv_cold));
      ret.emplace_back(std::format("    drv_electric={}", ch.drv_electric));
      ret.emplace_back(std::format("    drv_chemical={}", ch.drv_chemical));
      ret.emplace_back(std::format("    drv_mental={}", ch.drv_mental));
      ret.emplace_back(std::format("    drv_magic={}", ch.drv_magic));
      ret.emplace_back(std::format("    drv_special={}", ch.drv_special));
      ret.emplace_back(std::format("    current_age_group={}", ch.current_age_group));
      int16_t expected_checksum2 = ch.chance_to_hit + ch.max_sp + ch.brawn + ch.knowledge;
      if (ch.checksum2 == expected_checksum2) {
        ret.emplace_back(std::format("    checksum2={} (correct)", ch.checksum2));
      } else {
        ret.emplace_back(std::format("    checksum2={} (INCORRECT: expected {})", ch.checksum2, expected_checksum2));
      }
      ret.emplace_back(line_for_array("    items", ch.items, 30, [&ch, &format_item](size_t z, const GameState::Item& v) -> std::string {
        return (z < static_cast<size_t>(ch.item_count)) ? format_item(v) : "";
      }));
      ret.emplace_back(line_for_array("    scrolls", ch.scrolls, 5, [this](size_t, const GameState::Scroll& v) -> std::string {
        int16_t spell_number = v.spell_class * 1000 + v.spell_level * 100 + v.spell_number;
        if (spell_number == 0) {
          return "";
        }
        try {
          return std::format("({}) {} x{}", spell_number, this->scenario.name_for_spell(spell_number), v.power_level);
        } catch (const out_of_range&) {
          return std::format("({}) (missing) x{}", spell_number, v.power_level);
        }
      }));
      ret.emplace_back(std::format("    age={}", ch.age));
      ret.emplace_back(std::format("    victory_points={}", ch.victory_points));
      ret.emplace_back(std::format("    current_load={}", ch.current_load));
      ret.emplace_back(std::format("    max_load={}", ch.max_load));
      ret.emplace_back(std::format("    gold={}", ch.gold));
      ret.emplace_back(std::format("    gems={}", ch.gems));
      ret.emplace_back(std::format("    jewelry={}", ch.jewelry));
      ret.emplace_back(std::format("    has_turned={}", ch.has_turned));
      ret.emplace_back(std::format("    can_heal={}", ch.can_heal));
      ret.emplace_back(std::format("    can_identify_items={}", ch.can_identify_items));
      ret.emplace_back(std::format("    can_discover_magic={}", ch.can_discover_magic));
      ret.emplace_back(std::format("    selected_weapon={}", ch.selected_weapon));
      ret.emplace_back(std::format("    is_bleeding={}", ch.is_bleeding));
      ret.emplace_back(std::format("    is_in_battle={}", ch.is_in_battle));
      ret.emplace_back(std::format("    brawn={}", ch.brawn));
      ret.emplace_back(std::format("    knowledge={}", ch.knowledge));
      ret.emplace_back(std::format("    judgment={}", ch.judgment));
      ret.emplace_back(std::format("    agility={}", ch.agility));
      ret.emplace_back(std::format("    vitality={}", ch.vitality));
      ret.emplace_back(std::format("    luck={}", ch.luck));
      for (size_t z = 0; z < 7; z++) {
        ret.emplace_back(line_for_array("    learned_spells", ch.learned_spells[z], 10, [](size_t, uint8_t v) -> std::string {
          return std::format("{}", v);
        }));
      }
      ret.emplace_back(std::format("    name=\"{}\"", phosg::escape_quotes(ch.name)));
      int16_t expected_checksum3 = ch.armor_rating + 2 * ch.max_hp + ch.agility + ch.magic_resistance;
      if (ch.checksum3 == expected_checksum3) {
        ret.emplace_back(std::format("    checksum3={} (correct)", ch.checksum3));
      } else {
        ret.emplace_back(std::format("    checksum3={} (INCORRECT: expected {})", ch.checksum3, expected_checksum3));
      }
      ret.emplace_back(std::format("    prestige_damage_taken={}", ch.prestige_damage_taken));
      ret.emplace_back(std::format("    prestige_damage_given={}", ch.prestige_damage_given));
      ret.emplace_back(std::format("    prestige_hits_given={}", ch.prestige_hits_given));
      ret.emplace_back(std::format("    prestige_hits_taken={}", ch.prestige_hits_taken));
      ret.emplace_back(std::format("    prestige_missed_attacks={}", ch.prestige_missed_attacks));
      ret.emplace_back(std::format("    prestige_dodged_attacks={}", ch.prestige_dodged_attacks));
      ret.emplace_back(std::format("    prestige_enemies_killed={}", ch.prestige_enemies_killed));
      ret.emplace_back(std::format("    prestige_times_killed={}", ch.prestige_times_killed));
      ret.emplace_back(std::format("    prestige_times_unconscious={}", ch.prestige_times_unconscious));
      ret.emplace_back(std::format("    prestige_combat_spells_cast={}", ch.prestige_combat_spells_cast));
      ret.emplace_back(std::format("    prestige_undead_destroyed={}", ch.prestige_undead_destroyed));
      ret.emplace_back(std::format("    prestige_undead_turned={}", ch.prestige_undead_turned));
      ret.emplace_back(std::format("    prestige_penalty_points={}", ch.prestige_penalty_points));
      ret.emplace_back(line_for_array("    spell_shortcuts", ch.spell_shortcuts, 10, [this](size_t, const GameState::SpellShortcut& v) -> std::string {
        int16_t spell_number = v.spell_class * 1000 + v.spell_level * 100 + v.spell_number;
        if (spell_number == 0) {
          return "";
        }
        try {
          return std::format("({}) {} x{}", spell_number, this->scenario.name_for_spell(spell_number), v.power_level);
        } catch (const out_of_range&) {
          return std::format("({}) (missing) x{}", spell_number, v.power_level);
        }
      }));
      ret.emplace_back(std::format("    max_spells_per_round={}", ch.max_spells_per_round));
      ret.emplace_back(std::format("    spells_cast_this_round={}", ch.spells_cast_this_round));
    }
  };

  if (is_windows) {
    disassemble_platform_specific_section(this->game_state.windows);
  } else {
    disassemble_platform_specific_section(this->game_state.mac);
  }

  const auto& com = this->game_state.common;
  ret.emplace_back(std::format("  delta_x={}", com.delta_x));
  ret.emplace_back(std::format("  delta_y={}", com.delta_y));
  ret.emplace_back(std::format("  scenario_path=\"{}\"", phosg::escape_quotes(com.scenario_path)));
  ret.emplace_back(std::format("  killparty={}", com.killparty));
  ret.emplace_back(std::format("  charnum={}", com.charnum));
  ret.emplace_back(std::format("  head={}", com.head));
  ret.emplace_back(std::format("  currentshop={}", com.currentshop));
  for (size_t z = 0; z < 12; z++) {
    std::vector<std::string> tokens;
    for (size_t w = 0; w < 10; w++) {
      tokens.emplace_back(std::format("{}", com.quest_flags[z * 10 + w]));
    }
    ret.emplace_back(std::format("  quest_flags[{}-{}]=[{}]", z * 10, z * 10 + 9, phosg::join(tokens, ", ")));
  }
  {
    std::vector<std::string> tokens;
    for (size_t w = 0; w < 8; w++) {
      tokens.emplace_back(std::format("{}", com.quest_flags[120 + w]));
    }
    ret.emplace_back(std::format("  quest_flags[120-127]=[{}]", phosg::join(tokens, ", ")));
  }
  ret.emplace_back(std::format("  commandkey={}", com.commandkey));
  ret.emplace_back(std::format("  cl={}", com.cl));
  ret.emplace_back(std::format("  cr={}", com.cr));
  ret.emplace_back(std::format("  charselectnew={}", com.charselectnew));
  {
    std::vector<std::string> tokens;
    for (size_t w = 0; w < 20; w++) {
      tokens.emplace_back(std::format("{}", com.maps_found[w]));
    }
    ret.emplace_back(std::format("  maps_found=[{}]", phosg::join(tokens, ", ")));
  }
  ret.emplace_back(std::format("  inscroll={}", com.inscroll));
  ret.emplace_back(std::format("  indung={}", com.indung));
  ret.emplace_back(std::format("  view={}", com.view));
  ret.emplace_back(std::format("  editon={}", com.editon));
  ret.emplace_back(std::format("  incamp={}", com.incamp));
  ret.emplace_back(std::format("  initems={}", com.initems));
  ret.emplace_back(std::format("  inswap={}", com.inswap));
  ret.emplace_back(std::format("  inbooty={}", com.inbooty));
  ret.emplace_back(std::format("  shopavail={}", com.shopavail));
  ret.emplace_back(std::format("  campavail={}", com.campavail));
  ret.emplace_back(std::format("  intemple={}", com.intemple));
  ret.emplace_back(std::format("  inshop={}", com.inshop));
  ret.emplace_back(std::format("  swapavail={}", com.swapavail));
  ret.emplace_back(std::format("  templeavail={}", com.templeavail));
  ret.emplace_back(std::format("  tradeavail={}", com.tradeavail));
  ret.emplace_back(std::format("  canshop={}", com.canshop));
  ret.emplace_back(std::format("  shopequip={}", com.shopequip));
  ret.emplace_back(std::format("  lastcaste={}", com.lastcaste));
  ret.emplace_back(line_for_array("  lastspell", com.lastspell, 6, [](size_t, const int8_t* v) -> std::string {
    return std::format("({}, {})", v[0], v[1]);
  }));
  ret.emplace_back(std::format("  combatround={}", com.combatround));
  ret.emplace_back(std::format("  bigbadbug={}", com.bigbadbug));
  ret.emplace_back(std::format("  x={}", com.x));
  ret.emplace_back(std::format("  y={}", com.y));
  ret.emplace_back(std::format("  wallx={}", com.wallx));
  ret.emplace_back(std::format("  wally={}", com.wally));
  ret.emplace_back(std::format("  dunglevel={}", com.dunglevel));
  ret.emplace_back(std::format("  partyx={}", com.partyx));
  ret.emplace_back(std::format("  partyy={}", com.partyy));
  ret.emplace_back(std::format("  reclevel={}", com.reclevel));
  ret.emplace_back(std::format("  maxlevel={}", com.maxlevel));
  ret.emplace_back(std::format("  landlevel={}", com.landlevel));
  ret.emplace_back(std::format("  lookx={}", com.lookx));
  ret.emplace_back(std::format("  looky={}", com.looky));
  ret.emplace_back(std::format("  fieldx={}", com.fieldx));
  ret.emplace_back(std::format("  fieldy={}", com.fieldy));
  ret.emplace_back(std::format("  floorx={}", com.floorx));
  ret.emplace_back(std::format("  floory={}", com.floory));
  ret.emplace_back(std::format("  pool_gold={}", com.pool_gold));
  ret.emplace_back(std::format("  pool_gems={}", com.pool_gems));
  ret.emplace_back(std::format("  pool_jewelry={}", com.pool_jewelry));
  ret.emplace_back(std::format("  time_secs={}", com.time_secs));
  ret.emplace_back(std::format("  time_mins={}", com.time_mins));
  ret.emplace_back(std::format("  time_hours={}", com.time_hours));
  ret.emplace_back(std::format("  time_day_of_month={}", com.time_day_of_month));
  ret.emplace_back(std::format("  time_month={}", com.time_month));
  ret.emplace_back(std::format("  time_year={}", com.time_year));
  ret.emplace_back(std::format("  time_day_of_week={}", com.time_day_of_week));
  ret.emplace_back(std::format("  time_day_of_year={}", com.time_day_of_year));
  ret.emplace_back(std::format("  time_is_dst={}", com.time_is_dst));
  ret.emplace_back(std::format("  multiview={}", com.multiview));
  ret.emplace_back(std::format("  updatedir={}", com.updatedir));
  ret.emplace_back(std::format("  monsterset={}", com.monsterset));
  ret.emplace_back(std::format("  bankavailable={}", com.bankavailable));
  if (is_windows) {
    ret.emplace_back(std::format("  bank_gold={}", com.bank.le.gold));
    ret.emplace_back(std::format("  bank_gems={}", com.bank.le.gems));
    ret.emplace_back(std::format("  bank_jewelry={}", com.bank.le.jewelry));
    ret.emplace_back(std::format("  templecost={}", com.bank.le.templecost));
  } else {
    ret.emplace_back(std::format("  bank_gold={}", com.bank.be.gold));
    ret.emplace_back(std::format("  bank_gems={}", com.bank.be.gems));
    ret.emplace_back(std::format("  bank_jewelry={}", com.bank.be.jewelry));
    ret.emplace_back(std::format("  templecost={}", com.bank.be.templecost));
  }
  ret.emplace_back(std::format("  inboat={}", com.inboat));
  ret.emplace_back(std::format("  boatright={}", com.boatright));
  ret.emplace_back(std::format("  canencounter={}", com.canencounter));
  ret.emplace_back(std::format("  xydisplayflag={}", com.xydisplayflag));
  for (size_t z = 0; z < 20; z++) {
    ret.emplace_back(std::format("  npcs[{}]=", z));
    ret.emplace_back(this->scenario.disassemble_monster(com.npcs[z], 2));
  }
  ret.emplace_back(std::format("  heldover={}", com.heldover));
  ret.emplace_back(std::format("  deduction={}", com.deduction));
  ret.emplace_back(std::format("  savedserial={}", com.savedserial));
  ret.emplace_back(std::format("  canpriestturn={}", com.canpriestturn));
  ret.emplace_back(std::format("  musictoggle={}",
      phosg::format_data_string(&com.musictoggle[0], sizeof(com.musictoggle))));
  ret.emplace_back(line_for_array("  doauto", com.doauto, 6, [](size_t, uint8_t v) -> std::string {
    return std::format("{}", v);
  }));
  for (size_t z = 0; z < 6; z++) {
    ret.emplace_back(line_for_array("  spell_shortcuts", com.spell_shortcuts[z], 10, [this](size_t, const GameState::SpellShortcut& v) -> std::string {
      int16_t spell_number = v.spell_class * 1000 + v.spell_level * 100 + v.spell_number;
      if (spell_number == 0) {
        return "";
      }
      try {
        return std::format("({}) {} x{}", spell_number, this->scenario.name_for_spell(spell_number), v.power_level);
      } catch (const out_of_range&) {
        return std::format("({}) (missing) x{}", spell_number, v.power_level);
      }
    }));
  }
  ret.emplace_back(line_for_array("  notes", com.notes, 6, [](size_t z, uint8_t v) -> std::string {
    return v ? std::format("{}", z) : "";
  }));
  ret.emplace_back(std::format("  cancamp={}", com.cancamp));
  for (size_t z = 0; z < 6; z++) {
    ret.emplace_back(line_for_array("  stored_items", com.stored_items[z], 30, [&format_item](size_t, const GameState::Item& v) -> std::string {
      return format_item(v);
    }));
  }
  ret.emplace_back(std::format("  stored_gold={}", com.stored_gold));
  ret.emplace_back(std::format("  stored_gems={}", com.stored_gems));
  ret.emplace_back(std::format("  stored_jewelry={}", com.stored_jewelry));
  ret.emplace_back(std::format("  registration_name=\"{}\"", phosg::escape_quotes(com.registration_name)));
  ret.emplace_back(std::format("  testlocation={}", com.testlocation));
  ret.emplace_back(std::format("  spellcasting={}", com.spellcasting));
  ret.emplace_back(std::format("  spellcharging={}", com.spellcharging));
  ret.emplace_back(std::format("  monstercasting={}", com.monstercasting));
  ret.emplace_back(std::format("  spareboolean={}", com.spareboolean));
  return phosg::join(ret, "\n");
}

std::string RealmzSaveData::disassemble_all_land_level_states() const {
  std::deque<std::string> blocks;
  for (size_t level_num = 0; level_num < this->land_level_states.size(); level_num++) {
    const auto& level = this->land_level_states[level_num];
    for (size_t z = 0; z < 100; z++) {
      blocks.emplace_back(this->scenario.disassemble_level_ap(level.aps[z], level_num, z, false));
    }
    auto rrs = level.metadata.parse_random_rects();
    for (size_t z = 0; z < rrs.size(); z++) {
      blocks.emplace_back(this->scenario.disassemble_level_rr(rrs[z], level_num, z, false));
    }
  }
  return phosg::join(blocks, "");
}

std::string RealmzSaveData::disassemble_all_dungeon_level_states() const {
  std::deque<std::string> blocks;
  for (size_t level_num = 0; level_num < this->dungeon_level_states.size(); level_num++) {
    const auto& level = this->dungeon_level_states[level_num];
    for (size_t z = 0; z < 100; z++) {
      blocks.emplace_back(this->scenario.disassemble_level_ap(level.aps[z], level_num, z, true));
    }
    auto rrs = level.metadata.parse_random_rects();
    for (size_t z = 0; z < rrs.size(); z++) {
      blocks.emplace_back(this->scenario.disassemble_level_rr(rrs[z], level_num, z, true));
    }
  }
  return phosg::join(blocks, "");
}

std::string RealmzSaveData::disassemble_all_shops() const {
  std::deque<std::string> blocks;
  for (size_t z = 0; z < this->shop_states.size(); z++) {
    blocks.emplace_back(this->scenario.disassemble_shop(this->shop_states[z], z));
  }
  return phosg::join(blocks, "");
}

std::string RealmzSaveData::disassemble_all_simple_encounters() const {
  std::deque<std::string> blocks;
  for (size_t z = 0; z < this->simple_encounters.size(); z++) {
    blocks.emplace_back(this->scenario.disassemble_simple_encounter(this->simple_encounters[z], z));
  }
  return phosg::join(blocks, "");
}

std::string RealmzSaveData::disassemble_all_complex_encounters() const {
  std::deque<std::string> blocks;
  for (size_t z = 0; z < this->complex_encounters.size(); z++) {
    blocks.emplace_back(this->scenario.disassemble_complex_encounter(this->complex_encounters[z], z));
  }
  return phosg::join(blocks, "");
}

std::string RealmzSaveData::disassemble_all_rogue_encounters() const {
  std::deque<std::string> blocks;
  for (size_t z = 0; z < this->rogue_encounters.size(); z++) {
    blocks.emplace_back(this->scenario.disassemble_rogue_encounter(this->rogue_encounters[z], z));
  }
  return phosg::join(blocks, "");
}

std::string RealmzSaveData::disassemble_all_time_encounters() const {
  std::deque<std::string> blocks;
  for (size_t z = 0; z < this->time_encounters.size(); z++) {
    blocks.emplace_back(this->scenario.disassemble_time_encounter(this->time_encounters[z], z));
  }
  return phosg::join(blocks, "");
}

} // namespace ResourceDASM
