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
      characters(this->load_character_states(std::format("{}/Data I1", save_dir))),
      time_encounters(RealmzScenarioData::load_time_encounter_index(std::format("{}/Data TD3", save_dir))) {}

std::vector<RealmzSaveData::LandLevelState> RealmzSaveData::load_land_level_states(const std::string& filename) {
  return phosg::load_vector_file<LandLevelState>(filename);
}

// ImageRGB888 RealmzSaveData::generate_land_map(
//     int16_t level_num, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h) const {
//   TODO: Implement this. Render the scenario map, then draw the LOS darkness tiles over it maybe
// }

std::vector<RealmzSaveData::DungeonLevelState> RealmzSaveData::load_dungeon_level_states(const std::string& filename) {
  return phosg::load_vector_file<DungeonLevelState>(filename);
}

// ImageRGB888 RealmzSaveData::generate_dungeon_map(
//     int16_t level_num, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h) const {
//   TODO: Implement this. Similar to the above; tile discovered state is in the bitflags though
// }

std::vector<RealmzSaveData::CharacterState> RealmzSaveData::load_character_states(const std::string& filename) {
  return phosg::load_vector_file<CharacterState>(filename);
}

// std::string RealmzSaveData::disassemble_character(size_t index) const {
//   TODO: Implement this.
// }

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

// std::string RealmzSaveData::disassemble_all_characters() const {
//   TODO: Implement this.
// }

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
