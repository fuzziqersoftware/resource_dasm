#include "RealmzScenarioData.hh"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <deque>
#include <mutex>
#include <phosg/Encoding.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "IndexFormats/Formats.hh"
#include "ResourceFile.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {

string first_file_in_dir(const std::string& dir, std::initializer_list<const char*> names) {
  std::vector<std::string> paths;
  for (const char* name : names) {
    paths.emplace_back(std::format("{}/{}", dir, name));
  }
  return first_file_that_exists(paths);
}

RealmzScenarioData::RealmzScenarioData(const RealmzGlobalData& global, const string& scenario_dir, const string& name)
    : global(global),
      scenario_dir(scenario_dir),
      name(name) {

  string scenario_metadata_name = this->scenario_dir + "/" + this->name;
  string global_metadata_name = first_file_in_dir(this->scenario_dir, {"global", "Global", "GLOBAL"});
  string restrictions_name = first_file_in_dir(this->scenario_dir, {"data_ri", "Data RI", "DATA RI"});
  string monster_index_name = first_file_in_dir(this->scenario_dir, {"data_md", "Data MD", "DATA MD", "data_md1", "Data MD1", "DATA MD1", "data_md_1", "data_md-1", "Data MD-1", "DATA MD-1"});
  string battle_index_name = first_file_in_dir(this->scenario_dir, {"data_bd", "Data BD", "DATA BD"});
  string dungeon_map_index_name = first_file_in_dir(this->scenario_dir, {"data_dl", "Data DL", "DATA DL"});
  string land_map_index_name = first_file_in_dir(this->scenario_dir, {"data_ld", "Data LD", "DATA LD"});
  string string_index_name = first_file_in_dir(this->scenario_dir, {"data_sd2", "Data SD2", "DATA SD2"});
  string monster_description_index_name = first_file_in_dir(this->scenario_dir, {"data_des", "Data DES", "DATA DES"});
  string option_string_index_name = first_file_in_dir(this->scenario_dir, {"data_od", "Data OD", "DATA OD"});
  string ecodes_index_name = first_file_in_dir(this->scenario_dir, {"data_edcd", "Data EDCD", "DATA EDCD"});
  string land_ap_index_name = first_file_in_dir(this->scenario_dir, {"data_dd", "Data DD", "DATA DD"});
  string dungeon_ap_index_name = first_file_in_dir(this->scenario_dir, {"data_ddd", "Data DDD", "DATA DDD"});
  string extra_ap_index_name = first_file_in_dir(this->scenario_dir, {"data_ed3", "Data ED3", "DATA ED3"});
  string land_metadata_index_name = first_file_in_dir(this->scenario_dir, {"data_rd", "Data RD", "DATA RD"});
  string dungeon_metadata_index_name = first_file_in_dir(this->scenario_dir, {"data_rdd", "Data RDD", "DATA RDD"});
  string simple_encounter_index_name = first_file_in_dir(this->scenario_dir, {"data_ed", "Data ED", "DATA ED"});
  string complex_encounter_index_name = first_file_in_dir(this->scenario_dir, {"data_ed2", "Data ED2", "DATA ED2"});
  string party_map_index_name = first_file_in_dir(this->scenario_dir, {"data_md2", "Data MD2", "DATA MD2"});
  string custom_item_index_name = first_file_in_dir(this->scenario_dir, {"data_ni", "Data NI", "DATA NI"});
  string shop_index_name = first_file_in_dir(this->scenario_dir, {"data_sd", "Data SD", "DATA SD"});
  string treasure_index_name = first_file_in_dir(this->scenario_dir, {"data_td", "Data TD", "DATA TD"});
  string rogue_encounter_index_name = first_file_in_dir(this->scenario_dir, {"data_td2", "Data TD2", "DATA TD2"});
  string time_encounter_index_name = first_file_in_dir(this->scenario_dir, {"data_td3", "Data TD3", "DATA TD3"});
  string solids_name = first_file_in_dir(this->scenario_dir, {"data_solids", "Data Solids", "DATA SOLIDS"});
  string scenario_resources_name = first_file_in_dir(this->scenario_dir, {"scenario.rsf", "Scenario.rsf", "SCENARIO.RSF", "scenario.rsrc", "Scenario.rsrc", "SCENARIO.RSRC", "scenario/rsrc", "Scenario/rsrc", "SCENARIO/rsrc", "scenario/..namedfork/rsrc", "Scenario/..namedfork/rsrc", "SCENARIO/..namedfork/rsrc"});

  this->monsters = this->load_monster_index(monster_index_name);
  this->battles = this->load_battle_index(battle_index_name);
  this->dungeon_maps = this->load_dungeon_map_index(dungeon_map_index_name);
  this->land_maps = this->load_land_map_index(land_map_index_name);
  this->strings = this->load_string_index(string_index_name);
  this->monster_descriptions = this->load_string_index(monster_description_index_name);
  this->option_strings = this->load_option_string_index(option_string_index_name);
  this->ecodes = this->load_ecodes_index(ecodes_index_name);
  this->dungeon_aps = this->load_ap_index(dungeon_ap_index_name);
  this->land_aps = this->load_ap_index(land_ap_index_name);
  this->xaps = this->load_xap_index(extra_ap_index_name);
  this->dungeon_metadata = this->load_map_metadata_index(dungeon_metadata_index_name);
  this->land_metadata = this->load_map_metadata_index(land_metadata_index_name);
  this->simple_encounters = this->load_simple_encounter_index(simple_encounter_index_name);
  this->complex_encounters = this->load_complex_encounter_index(complex_encounter_index_name);
  this->party_maps = this->load_party_map_index(party_map_index_name);
  this->custom_item_definitions = RealmzGlobalData::load_item_definitions(custom_item_index_name);
  this->shops = this->load_shop_index(shop_index_name);
  this->treasures = this->load_treasure_index(treasure_index_name);
  this->rogue_encounters = this->load_rogue_encounter_index(rogue_encounter_index_name);
  this->time_encounters = this->load_time_encounter_index(time_encounter_index_name);
  // Some scenarios apparently don't have global metadata
  if (!global_metadata_name.empty()) {
    this->global_metadata = this->load_global_metadata(global_metadata_name);
  }
  if (!restrictions_name.empty()) {
    this->restrictions = this->load_restrictions(restrictions_name);
  } else {
    this->restrictions.description_bytes = 0;
    memset(this->restrictions.description, 0, sizeof(this->restrictions.description));
    this->restrictions.max_characters = 0;
    this->restrictions.max_level_per_character = 0;
    memset(this->restrictions.forbidden_races, 0, sizeof(this->restrictions.forbidden_races));
    memset(this->restrictions.forbidden_castes, 0, sizeof(this->restrictions.forbidden_castes));
  }
  if (!solids_name.empty()) {
    this->solids = this->load_solids(solids_name);
  }

  this->scenario_metadata = this->load_scenario_metadata(scenario_metadata_name);
  this->scenario_rsf = parse_resource_fork(load_file(scenario_resources_name));

  this->item_strings = RealmzGlobalData::load_item_strings(this->scenario_rsf);
  this->spell_names = RealmzGlobalData::load_spell_names(this->scenario_rsf);

  // Load layout separately because it doesn't have to exist
  {
    string fname = first_file_in_dir(this->scenario_dir, {"layout", "Layout", "LAYOUT"});
    if (!fname.empty()) {
      this->layout = this->load_land_layout(fname);
    } else {
      fwrite_fmt(stderr, "note: this scenario has no land layout information\n");
    }
  }

  // Load tilesets
  for (int z = 1; z < 4; z++) {
    string fname = first_file_that_exists({std::format("{}/data_custom_{}_bd", this->scenario_dir, z),
        std::format("{}/Data Custom {} BD", this->scenario_dir, z),
        std::format("{}/DATA CUSTOM {} BD", this->scenario_dir, z)});
    if (!fname.empty()) {
      string land_type = std::format("custom_{}", z);
      this->land_type_to_tileset_definition.emplace(
          std::move(land_type), RealmzGlobalData::load_tileset_definition(fname));
    }
  }
}

const string& RealmzScenarioData::name_for_spell(uint16_t id) const {
  try {
    return this->spell_names.at(id);
  } catch (const out_of_range&) {
    return this->global.name_for_spell(id);
  }
}

string RealmzScenarioData::desc_for_spell(uint16_t id) const {
  try {
    return std::format("{}({})", id, this->global.name_for_spell(id));
  } catch (const out_of_range&) {
    return std::format("{}", id);
  }
}

const RealmzGlobalData::ItemStrings& RealmzScenarioData::strings_for_item(uint16_t id) const {
  try {
    return this->item_strings.at(id);
  } catch (const out_of_range&) {
    return this->global.strings_for_item(id);
  }
}

string RealmzScenarioData::desc_for_item(uint16_t id) const {
  try {
    return std::format("ITM{}({})", id, this->strings_for_item(id).display_name());
  } catch (const out_of_range&) {
  }
  return std::format("{}", id);
}

static string render_string_reference(const vector<string>& strings, int index) {
  if (index == 0) {
    return "0";
  }
  if ((size_t)abs(index) >= strings.size()) {
    return std::format("{}", index);
  }

  // Strings in Realmz scenarios often end with a bunch of spaces, which looks bad in the disassembly and serves no
  // purpose, so we trim them off here.
  string s = strings[abs(index)];
  strip_trailing_whitespace(s);
  return std::format("\"{}\"#{}", escape_quotes(s), index);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA MD2

vector<RealmzScenarioData::PartyMap> RealmzScenarioData::load_party_map_index(const string& filename) {
  return load_vector_file<PartyMap>(filename);
}

string RealmzScenarioData::disassemble_party_map(size_t index) const {
  const PartyMap& pm = this->party_maps.at(index);

  string ret = std::format("===== {} MAP id={} level={} x={} y={} tile_size={} [MAP{}]\n",
      (pm.is_dungeon ? "DUNGEON" : "LAND"), index, pm.level_num, pm.x, pm.y, pm.tile_size, index);
  if (pm.picture_id) {
    ret += std::format("  picture id={}\n", pm.picture_id);
  }
  if (pm.text_id) {
    ret += std::format("  text id={}\n", pm.text_id);
  }

  for (int x = 0; x < 10; x++) {
    if (!pm.annotations[x].icon_id && !pm.annotations[x].x && !pm.annotations[x].y) {
      continue;
    }
    ret += std::format("  annotation icon_id={} x={} y={}\n",
        pm.annotations[x].icon_id, pm.annotations[x].x, pm.annotations[x].y);
  }

  string description(pm.description, pm.description_valid_chars);
  ret += std::format("  description=\"{}\"\n", phosg::escape_quotes(description));
  return ret;
}

string RealmzScenarioData::disassemble_all_party_maps() const {
  deque<string> blocks;
  for (size_t z = 0; z < this->party_maps.size(); z++) {
    blocks.emplace_back(this->disassemble_party_map(z));
  }
  return join(blocks, "");
}

ImageRGB888 RealmzScenarioData::render_party_map(size_t index) const {
  const auto& pm = this->party_maps.at(index);

  if (!pm.tile_size) {
    throw runtime_error("tile size is zero");
  }

  double whf = 320.0 / pm.tile_size;
  size_t wh = static_cast<size_t>(ceil(whf));

  ImageRGB888 ret;
  if (pm.is_dungeon) {
    ret = generate_dungeon_map(pm.level_num, pm.x, pm.y, wh, wh);
  } else {
    ret = generate_land_map(pm.level_num, pm.x, pm.y, wh, wh);
  }

  size_t rendered_tile_size = pm.is_dungeon ? 16 : 32;
  for (int x = 0; x < 10; x++) {
    const auto& a = pm.annotations[x];
    if (!a.icon_id) {
      continue;
    }
    ImageRGBA8888N cicn;
    try {
      cicn = this->scenario_rsf.decode_cicn(a.icon_id).image;
    } catch (const out_of_range&) {
    }
    try {
      cicn = this->global.global_rsf.decode_cicn(a.icon_id).image;
    } catch (const out_of_range&) {
    }
    if (cicn.get_width() == 0 || cicn.get_height() == 0) {
      fwrite_fmt(stderr, "warning: map refers to missing cicn {}\n", a.icon_id);
    } else {
      // It appears that annotations should render centered on the tile on which they are defined, so we may need to
      // adjust dest x/y if the cicn size isn't the same as the tile size.
      ssize_t px = a.x * rendered_tile_size - (cicn.get_width() - rendered_tile_size) / 2;
      ssize_t py = a.y * rendered_tile_size - (cicn.get_height() - rendered_tile_size) / 2;
      ret.copy_from_with_blend(cicn, px, py, cicn.get_width(), cicn.get_height(), 0, 0);
    }
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LAYOUT

RealmzScenarioData::LevelNeighbors::LevelNeighbors() : x(-1), y(-1), left(-1), right(-1), top(-1), bottom(-1) {}

RealmzScenarioData::LandLayout::LandLayout() {
  for (size_t y = 0; y < 8; y++) {
    for (size_t x = 0; x < 16; x++) {
      this->layout[y][x] = -1;
    }
  }
}

RealmzScenarioData::LandLayout::LandLayout(const LandLayout& l) {
  for (size_t y = 0; y < 8; y++) {
    for (size_t x = 0; x < 16; x++) {
      this->layout[y][x] = l.layout[y][x];
    }
  }
}

RealmzScenarioData::LandLayout& RealmzScenarioData::LandLayout::operator=(
    const LandLayout& l) {
  for (size_t y = 0; y < 8; y++) {
    for (size_t x = 0; x < 16; x++) {
      this->layout[y][x] = l.layout[y][x];
    }
  }
  return *this;
}

size_t RealmzScenarioData::LandLayout::num_valid_levels() const {
  size_t count = 0;
  for (size_t y = 0; y < 8; y++) {
    for (size_t x = 0; x < 16; x++) {
      if (this->layout[y][x] >= 0) {
        count++;
      }
    }
  }
  return count;
}

RealmzScenarioData::LandLayout RealmzScenarioData::load_land_layout(const string& filename) {
  LandLayout l = load_object_file<LandLayout>(filename, true);
  for (size_t y = 0; y < 8; y++) {
    for (size_t x = 0; x < 16; x++) {
      if (l.layout[y][x] == -1) {
        l.layout[y][x] = 0;
      } else if (l.layout[y][x] == 0) {
        l.layout[y][x] = -1;
      }
    }
  }
  return l;
}

RealmzScenarioData::LevelNeighbors RealmzScenarioData::LandLayout::get_level_neighbors(int16_t id) const {
  LevelNeighbors n;
  for (size_t y = 0; y < 8; y++) {
    for (size_t x = 0; x < 16; x++) {
      if (this->layout[y][x] == id) {
        if (n.x != -1 || n.y != -1) {
          throw runtime_error("multiple entries for level");
        }

        n.x = x;
        n.y = y;
        if (x != 0) {
          n.left = this->layout[y][x - 1];
        }
        if (x != 15) {
          n.right = this->layout[y][x + 1];
        }
        if (y != 0) {
          n.top = this->layout[y - 1][x];
        }
        if (y != 7) {
          n.bottom = this->layout[y + 1][x];
        }
      }
    }
  }
  return n;
}

vector<RealmzScenarioData::LandLayout> RealmzScenarioData::LandLayout::get_connected_components() const {
  LandLayout remaining_components(*this);

  vector<LandLayout> ret;
  for (ssize_t y = 0; y < 8; y++) {
    for (ssize_t x = 0; x < 16; x++) {
      if (remaining_components.layout[y][x] == -1) {
        continue;
      }

      // This cell is the upper-left corner of a connected component; use flood-fill to copy it to this_component
      LandLayout this_component;
      set<pair<ssize_t, ssize_t>> to_fill;
      to_fill.insert(make_pair(x, y));
      while (!to_fill.empty()) {
        auto pt = *to_fill.begin();
        to_fill.erase(pt);
        if (pt.first < 0 || pt.first >= 16 || pt.second < 0 || pt.second >= 8) {
          continue;
        }
        if (remaining_components.layout[pt.second][pt.first] == -1) {
          continue;
        }
        this_component.layout[pt.second][pt.first] = remaining_components.layout[pt.second][pt.first];
        remaining_components.layout[pt.second][pt.first] = -1;
        to_fill.insert(make_pair(pt.first - 1, pt.second));
        to_fill.insert(make_pair(pt.first + 1, pt.second));
        to_fill.insert(make_pair(pt.first, pt.second - 1));
        to_fill.insert(make_pair(pt.first, pt.second + 1));
      }

      ret.emplace_back(this_component);
    }
  }
  return ret;
}

ImageRGB888 RealmzScenarioData::generate_layout_map(
    const LandLayout& l,
    bool show_random_rects,
    std::function<ImageRGB888(int16_t, uint8_t, uint8_t, uint8_t, uint8_t, bool)> generate_level_map) const {
  ssize_t min_x = 16, min_y = 8, max_x = -1, max_y = -1;
  for (ssize_t y = 0; y < 8; y++) {
    for (ssize_t x = 0; x < 16; x++) {
      if (l.layout[y][x] < 0) {
        continue;
      }

      // If the level has no valid neighbors, ignore it
      if ((x > 0) && (l.layout[y][x - 1] < 0) &&
          (x < 15) && (l.layout[y][x + 1] < 0) &&
          (y > 0) && (l.layout[y - 1][x] < 0) &&
          (y < 7) && (l.layout[y + 1][x] < 0)) {
        continue;
      }

      min_x = std::min<ssize_t>(min_x, x);
      max_x = std::max<ssize_t>(max_x, x);
      min_y = std::min<ssize_t>(min_y, y);
      max_y = std::max<ssize_t>(max_y, y);
    }
  }

  if (max_x < min_x || max_y < min_y) {
    throw runtime_error("layout has no valid levels");
  }

  max_x++;
  max_y++;

  ImageRGB888 overall_map(90 * 32 * (max_x - min_x), 90 * 32 * (max_y - min_y));
  for (ssize_t y = 0; y < (max_y - min_y); y++) {
    for (ssize_t x = 0; x < (max_x - min_x); x++) {
      int16_t level_id = l.layout[y + min_y][x + min_x];
      if (level_id < 0) {
        continue;
      }

      int xp = 90 * 32 * x;
      int yp = 90 * 32 * y;

      try {
        ImageRGB888 this_level_map = generate_level_map
            ? generate_level_map(level_id, 0, 0, 90, 90, show_random_rects)
            : this->generate_land_map(level_id, 0, 0, 90, 90, show_random_rects);

        // If get_level_neighbors fails, then we would not have written any boundary information on the original map,
        // so we can just ignore this
        int sx = 0, sy = 0;
        try {
          LevelNeighbors n = l.get_level_neighbors(level_id);
          sx = (n.left >= 0) ? 9 : 0;
          sy = (n.top >= 0) ? 9 : 0;
        } catch (const runtime_error&) {
        }

        overall_map.copy_from(this_level_map, xp, yp, 90 * 32, 90 * 32, sx, sy);

      } catch (const exception& e) {
        overall_map.write_rect(xp, yp, 90 * 32, 90 * 32, 0xFFFFFFFF);
        overall_map.draw_text(xp + 10, yp + 10, 0xFF0000FF, 0x00000000, "can\'t generate level map", level_id);
        overall_map.draw_text(xp + 10, yp + 20, 0x000000FF, 0x00000000, "{}", e.what());
      }
    }
  }

  return overall_map;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GLOBAL

RealmzScenarioData::GlobalMetadata RealmzScenarioData::load_global_metadata(const string& filename) {
  return load_object_file<GlobalMetadata>(filename, true);
}

string RealmzScenarioData::disassemble_global_metadata() const {
  BlockStringWriter w;
  w.write("===== GLOBAL METADATA");
  auto add_xap = [&](const char* name, int16_t xap_num) -> void {
    if (xap_num) {
      w.write_fmt("  {:<20}XAP{}", name, xap_num);
    } else {
      w.write_fmt("  {:<20}(none)", name);
    }
  };
  add_xap("on_start", this->global_metadata.start_xap);
  add_xap("on_death", this->global_metadata.death_xap);
  add_xap("on_quit", this->global_metadata.quit_xap);
  add_xap("on_reserved1", this->global_metadata.reserved1_xap);
  add_xap("on_shop", this->global_metadata.shop_xap);
  add_xap("on_temple", this->global_metadata.temple_xap);
  add_xap("on_reserved2", this->global_metadata.reserved2_xap);
  w.write("");
  return w.close("\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// <SCENARIO NAME>

RealmzScenarioData::ScenarioMetadata RealmzScenarioData::load_scenario_metadata(const string& filename) {
  // At some point between Realmz 3.1 and 5.1, the scenario data was extended from 24 bytes to the full
  // ScenarioMetadata struct as defined in this project. To handle earlier scenario versions, we accept shorter
  // versions of this file.
  ScenarioMetadata ret;
  ret.recommended_starting_levels = 0;
  ret.unknown_a1 = 0;
  ret.start_level = 0;
  ret.start_x = 0;
  ret.start_y = 0;
  memset(ret.unknown_a2, 0, sizeof(ret.unknown_a2));
  ret.author_name_bytes = 0;
  memset(ret.author_name, 0, sizeof(ret.author_name));

  auto f = fopen_unique(filename, "rb");
  size_t bytes_read = fread(&ret, 1, sizeof(ScenarioMetadata), f.get());
  if (bytes_read == 0) {
    throw runtime_error("no data read from scenario metadata");
  }
  return ret;
}

string RealmzScenarioData::disassemble_scenario_metadata() const {
  const auto& smd = this->scenario_metadata;
  BlockStringWriter w;
  w.write("===== SCENARIO METADATA");
  w.write_fmt("  recommended_levels  {}", smd.recommended_starting_levels);
  w.write_fmt("  a1                  {:08X}", smd.unknown_a1);
  w.write_fmt("  start_location      level={} x={} y={}", smd.start_level, smd.start_x, smd.start_y);
  string a2_str = format_data_string(smd.unknown_a2, sizeof(smd.unknown_a2));
  w.write_fmt("  a2                  {}", a2_str);
  string author_name = format_data_string(smd.author_name, smd.author_name_bytes);
  w.write_fmt("  author_name         {}", author_name);
  w.write("");
  return w.close("\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA RI

RealmzScenarioData::Restrictions RealmzScenarioData::load_restrictions(const string& filename) {
  return load_object_file<Restrictions>(filename);
}

string RealmzScenarioData::disassemble_restrictions() const {
  const auto& rst = this->restrictions;
  BlockStringWriter w;
  w.write("===== RESTRICTIONS");
  string desc = format_data_string(this->restrictions.description, this->restrictions.description_bytes);
  w.write_fmt("  description         {}", desc);
  w.write_fmt("  max_characters      {}", rst.max_characters);
  w.write_fmt("  max_character_level {}", rst.max_level_per_character);
  for (size_t z = 0; z < sizeof(rst.forbidden_races); z++) {
    if (rst.forbidden_races[z]) {
      try {
        w.write_fmt("  forbid_race         {}", this->global.race_names.at(z));
      } catch (const out_of_range&) {
        w.write_fmt("  forbid_race         {}", z);
      }
    }
  }
  for (size_t z = 0; z < sizeof(rst.forbidden_castes); z++) {
    if (rst.forbidden_castes[z]) {
      try {
        w.write_fmt("  forbid_caste        {}", this->global.caste_names.at(z));
      } catch (const out_of_range&) {
        w.write_fmt("  forbid_caste        {}", z);
      }
    }
  }
  w.write("");
  return w.close("\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA EDCD

vector<RealmzScenarioData::ECodes> RealmzScenarioData::load_ecodes_index(const string& filename) {
  return load_vector_file<ECodes>(filename);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA TD

vector<RealmzScenarioData::Treasure> RealmzScenarioData::load_treasure_index(const string& filename) {
  return load_vector_file<Treasure>(filename);
}

string RealmzScenarioData::disassemble_treasure(size_t index) const {
  const auto& t = this->treasures.at(index);

  string ret = std::format("===== TREASURE id={} [TSR{}]", index, index);

  if (t.victory_points < 0) {
    ret += std::format(" victory_points=rand(1,{})", -t.victory_points);
  } else if (t.victory_points > 0) {
    ret += std::format(" victory_points={}", t.victory_points);
  }

  if (t.gold < 0) {
    ret += std::format(" gold=rand(1,{})", -t.gold);
  } else if (t.gold > 0) {
    ret += std::format(" gold={}", t.gold);
  }

  if (t.gems < 0) {
    ret += std::format(" gems=rand(1,{})", -t.gems);
  } else if (t.gems > 0) {
    ret += std::format(" gems={}", t.gems);
  }

  if (t.jewelry < 0) {
    ret += std::format(" jewelry=rand(1,{})", -t.jewelry);
  } else if (t.jewelry > 0) {
    ret += std::format(" jewelry={}", t.jewelry);
  }

  ret += '\n';

  for (int x = 0; x < 20; x++) {
    if (t.item_ids[x]) {
      string desc = this->desc_for_item(t.item_ids[x]);
      ret += std::format("  {}\n", desc);
    }
  }

  return ret;
}

string RealmzScenarioData::disassemble_all_treasures() const {
  deque<string> blocks;
  for (size_t z = 0; z < this->treasures.size(); z++) {
    blocks.emplace_back(this->disassemble_treasure(z));
  }
  return join(blocks, "");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA ED

vector<RealmzScenarioData::SimpleEncounter> RealmzScenarioData::load_simple_encounter_index(const string& filename) {
  return load_vector_file<SimpleEncounter>(filename);
}

string RealmzScenarioData::disassemble_simple_encounter(const SimpleEncounter& e, size_t index) const {
  string prompt = render_string_reference(this->strings, e.prompt);
  string ret = std::format("===== SIMPLE ENCOUNTER id={} can_backout={} max_times={} prompt={} [SEC{}]\n",
      index, e.can_backout, e.max_times, prompt, index);

  vector<string> result_references[4];

  for (size_t x = 0; x < 4; x++) {
    string choice_text(e.choice_text[x].text,
        min(static_cast<size_t>(e.choice_text[x].valid_chars), static_cast<size_t>(sizeof(e.choice_text[x]) - 1)));
    strip_trailing_whitespace(choice_text);
    if (choice_text.empty()) {
      continue;
    }
    choice_text = escape_quotes(choice_text);
    ret += std::format("  choice{}: result={} text=\"{}\"\n", x, e.choice_result_index[x], escape_quotes(choice_text));
    if (e.choice_result_index[x] >= 1 && e.choice_result_index[x] <= 4) {
      result_references[e.choice_result_index[x] - 1].emplace_back(std::format("ACTIVATE ON CHOICE {}", x));
    }
  }

  for (size_t x = 0; x < 4; x++) {
    size_t y;
    for (y = 0; y < 8; y++) {
      if (e.choice_codes[x][y] || e.choice_args[x][y]) {
        break;
      }
    }
    if (y == 8 && result_references[x].empty()) {
      continue; // Option is blank; don't even print it
    }

    if (result_references[x].empty()) {
      ret += std::format("  result{} UNUSED\n", x + 1);
    } else {
      ret += std::format("  result{}\n", x + 1);
      for (const auto& ref : result_references[x]) {
        ret += std::format("    {}\n", ref);
      }
    }

    DisassemblyOrigin origin{
        .type = DisassemblyOrigin::Type::SIMPLE_ENCOUNTER, .level_num = -1, .ap_num = static_cast<ssize_t>(index)};
    for (size_t y = 0; y < 8; y++) {
      if (e.choice_codes[x][y] || e.choice_args[x][y]) {
        string dasm = this->disassemble_opcode(e.choice_codes[x][y], e.choice_args[x][y], origin);
        ret += std::format("    {}\n", dasm);
      }
    }
  }

  return ret;
}

string RealmzScenarioData::disassemble_all_simple_encounters() const {
  deque<string> blocks;
  for (size_t z = 0; z < this->simple_encounters.size(); z++) {
    blocks.emplace_back(this->disassemble_simple_encounter(this->simple_encounters[z], z));
  }
  return join(blocks, "");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA ED2

vector<RealmzScenarioData::ComplexEncounter> RealmzScenarioData::load_complex_encounter_index(const string& filename) {
  return load_vector_file<ComplexEncounter>(filename);
}

static const vector<const char*> rogue_encounter_action_names({
    "acrobatic_act",
    "detect_trap",
    "disable_trap",
    "action3",
    "force_lock",
    "action5",
    "pick_lock",
    "action7",
});

string RealmzScenarioData::disassemble_complex_encounter(const ComplexEncounter& e, size_t index) const {
  string prompt = render_string_reference(this->strings, e.prompt);
  string ret = std::format("===== COMPLEX ENCOUNTER id={} can_backout={} max_times={} prompt={} [CEC{}]\n",
      index, e.can_backout, e.max_times, prompt, index);

  vector<string> result_references[4];
  result_references[3].emplace_back("ACTIVATE DEFAULT");

  bool wrote_spell_header = false;
  for (size_t x = 0; x < 10; x++) {
    if (!e.spell_codes[x]) {
      continue;
    }
    if (!wrote_spell_header) {
      ret += "  spells\n";
      wrote_spell_header = true;
    }
    string spell_desc = this->desc_for_spell(e.spell_codes[x]);
    ret += std::format("    result={}, id={}\n", e.spell_result_codes[x], spell_desc);
    if (e.spell_result_codes[x] >= 1 && e.spell_result_codes[x] <= 4) {
      result_references[e.spell_result_codes[x] - 1].emplace_back("ACTIVATE ON SPELL " + spell_desc);
    }
  }

  bool wrote_item_header = false;
  for (size_t x = 0; x < 5; x++) {
    if (!e.item_codes[x]) {
      continue;
    }
    if (!wrote_item_header) {
      ret += "  items\n";
      wrote_item_header = true;
    }
    auto item_desc = this->desc_for_item(e.item_codes[x]);
    ret += std::format("    result={} id={}\n", e.item_result_codes[x], item_desc);
    if (e.item_result_codes[x] >= 1 && e.item_result_codes[x] <= 4) {
      result_references[e.item_result_codes[x] - 1].emplace_back("ACTIVATE ON ITEM " + item_desc);
    }
  }

  bool wrote_action_header = false;
  for (size_t x = 0; x < 5; x++) {
    string action_text(e.action_text[x].text, min((int)e.action_text[x].valid_chars, (int)sizeof(e.action_text[x]) - 1));
    strip_trailing_whitespace(action_text);
    if (action_text.empty()) {
      continue;
    }
    if (!wrote_action_header) {
      ret += std::format("  actions result={}\n", e.action_result);
      if (e.action_result >= 1 && e.action_result <= 4) {
        result_references[e.action_result - 1].emplace_back("ACTIVATE ON ACTION");
      }
      wrote_action_header = true;
    }
    action_text = escape_quotes(action_text);
    ret += std::format("    selected={} text=\"{}\"\n", e.actions_selected[x], escape_quotes(action_text));
  }

  if (e.has_rogue_encounter) {
    try {
      const auto& re = this->rogue_encounters.at(e.rogue_encounter_id);
      ret += std::format("  rogue_encounter id={} reset={}\n", e.rogue_encounter_id, e.rogue_reset_flag);
      for (size_t z = 0; z < 8; z++) {
        if (!re.actions_available[z]) {
          continue;
        }
        if (re.success_result_codes[z] >= 1 && re.success_result_codes[z] <= 4) {
          result_references[re.success_result_codes[z] - 1].emplace_back(std::format(
              "ACTIVATE ON ROGUE {} SUCCESS", rogue_encounter_action_names[z]));
        }
        if (re.failure_result_codes[z] >= 1 && re.failure_result_codes[z] <= 4) {
          result_references[re.failure_result_codes[z] - 1].emplace_back(std::format(
              "ACTIVATE ON ROGUE {} FAILURE", rogue_encounter_action_names[z]));
        }
      }
    } catch (const out_of_range&) {
      ret += std::format("  rogue encounter id={} (MISSING) reset={}\n", e.rogue_encounter_id, e.rogue_reset_flag);
    }
  }

  string speak_text(e.speak_text.text, min((int)e.speak_text.valid_chars, (int)sizeof(e.speak_text) - 1));
  strip_trailing_whitespace(speak_text);
  if (!speak_text.empty()) {
    speak_text = escape_quotes(speak_text);
    ret += std::format("  speak result={} text=\"{}\"\n", e.speak_result, escape_quotes(speak_text));
    if (e.speak_result >= 1 && e.speak_result <= 4) {
      result_references[e.speak_result - 1].emplace_back("ACTIVATE ON SPEAK");
    }
  }

  for (size_t x = 0; x < 4; x++) {
    size_t y;
    for (y = 0; y < 8; y++) {
      if (e.choice_codes[x][y] || e.choice_args[x][y]) {
        break;
      }
    }
    if (y == 8 && result_references[x].empty()) {
      continue; // Option is entirely blank; don't even print it
    }

    if (result_references[x].empty()) {
      ret += std::format("  result{} UNUSED\n", x + 1);
    } else {
      ret += std::format("  result{}\n", x + 1);
      for (const auto& ref : result_references[x]) {
        ret += std::format("    {}\n", ref);
      }
    }

    DisassemblyOrigin origin{
        .type = DisassemblyOrigin::Type::COMPLEX_ENCOUNTER, .level_num = -1, .ap_num = static_cast<ssize_t>(index)};
    for (size_t y = 0; y < 8; y++) {
      if (e.choice_codes[x][y] || e.choice_args[x][y]) {
        string dasm = this->disassemble_opcode(e.choice_codes[x][y], e.choice_args[x][y], origin);
        ret += std::format("    {}\n", dasm);
      }
    }
  }

  return ret;
}

string RealmzScenarioData::disassemble_all_complex_encounters() const {
  deque<string> blocks;
  for (size_t z = 0; z < this->complex_encounters.size(); z++) {
    blocks.emplace_back(this->disassemble_complex_encounter(this->complex_encounters[z], z));
  }
  return join(blocks, "");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA TD2

vector<RealmzScenarioData::RogueEncounter>
RealmzScenarioData::load_rogue_encounter_index(const string& filename) {
  return load_vector_file<RogueEncounter>(filename);
}

string RealmzScenarioData::disassemble_rogue_encounter(const RogueEncounter& e, size_t index) const {
  string prompt = render_string_reference(strings, e.prompt_string);
  string ret = std::format(
      "===== ROGUE ENCOUNTER id={} sound={} prompt={} pct_per_level_to_open_lock={} pct_per_level_to_disable_trap={} "
      "num_lock_tumblers={} [REC{}]\n",
      index, e.prompt_sound, prompt, e.percent_per_level_to_open,
      e.percent_per_level_to_disable, e.num_lock_tumblers, index);

  for (size_t x = 0; x < 8; x++) {
    if (!e.actions_available[x]) {
      continue;
    }
    string success_str = render_string_reference(strings, e.success_string_ids[x]);
    string failure_str = render_string_reference(strings, e.failure_string_ids[x]);

    ret += std::format(
        "  action_{} percent_modify={} success_result={} failure_result={} success_str={} failure_str={} "
        "success_sound={} failure_sound={}\n",
        rogue_encounter_action_names[x],
        e.percent_modify[x], e.success_result_codes[x],
        e.failure_result_codes[x], success_str, failure_str,
        e.success_sound_ids[x], e.failure_sound_ids[x]);
  }

  if (e.is_trapped) {
    string spell_desc = this->desc_for_spell(e.trap_spell);
    ret += std::format("  trap rogue_only={} spell={} spell_power={} damage_range=[{},{}] sound={}\n",
        e.trap_affects_rogue_only, spell_desc,
        e.trap_spell_power_level, e.trap_damage_low,
        e.trap_damage_high, e.trap_sound);
  }

  return ret;
}

string RealmzScenarioData::disassemble_all_rogue_encounters() const {
  deque<string> blocks;
  for (size_t z = 0; z < this->rogue_encounters.size(); z++) {
    blocks.emplace_back(this->disassemble_rogue_encounter(this->rogue_encounters[z], z));
  }
  return join(blocks, "");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA TD3

vector<RealmzScenarioData::TimeEncounter> RealmzScenarioData::load_time_encounter_index(const string& filename) {
  return load_vector_file<TimeEncounter>(filename);
}

string RealmzScenarioData::disassemble_time_encounter(const TimeEncounter& e, size_t index) const {
  string ret = std::format("===== TIME ENCOUNTER id={}", index);

  ret += std::format(" day={}", e.day);
  ret += std::format(" increment={}", e.increment);
  ret += std::format(" percent_chance={}", e.percent_chance);
  ret += std::format(" xap_id=XAP{}", e.xap_id);
  if (e.required_level != -1) {
    ret += std::format(" required_level: id={}({})", e.required_level, (e.land_or_dungeon == 1) ? "land" : "dungeon");
  }
  if (e.required_rect != -1) {
    ret += std::format(" required_rect={}", e.required_rect);
  }
  if (e.required_x != -1 || e.required_y != -1) {
    ret += std::format(" required_pos=({},{})", e.required_x, e.required_y);
  }
  if (e.required_item_id != -1) {
    ret += " required_item_id=";
    ret += this->desc_for_item(e.required_item_id);
  }
  if (e.required_quest != -1) {
    ret += std::format(" required_quest={}", e.required_quest);
  }

  ret += std::format(" [TEC{}]\n", index);
  return ret;
}

string RealmzScenarioData::disassemble_all_time_encounters() const {
  deque<string> blocks;
  for (size_t z = 0; z < this->time_encounters.size(); z++) {
    blocks.emplace_back(this->disassemble_time_encounter(this->time_encounters[z], z));
  }
  return join(blocks, "");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA RD

static const unordered_map<uint8_t, string> land_type_to_string({
    {0, "outdoor"},
    {1, "reserved1"},
    {2, "reserved2"},
    {3, "cave"},
    {4, "indoor"},
    {5, "desert"},
    {6, "custom_1"},
    {7, "custom_2"},
    {8, "custom_3"},
    {9, "abyss"},
    {10, "snow"},
});

std::vector<RealmzScenarioData::RandomRect> RealmzScenarioData::MapMetadataFile::parse_random_rects() const {
  std::vector<RealmzScenarioData::RandomRect> ret;
  for (size_t y = 0; y < 20; y++) {
    auto& r = ret.emplace_back();
    r.top = this->coords[y].top;
    r.left = this->coords[y].left;
    r.bottom = this->coords[y].bottom;
    r.right = this->coords[y].right;
    r.times_in_10k = this->times_in_10k[y];
    r.battle_low = this->battle_range[y].low;
    r.battle_high = this->battle_range[y].high;
    r.xap_refs[0].xap_num = this->xap_num[y][0];
    r.xap_refs[1].xap_num = this->xap_num[y][1];
    r.xap_refs[2].xap_num = this->xap_num[y][2];
    r.xap_refs[0].chance = this->xap_chance[y][0];
    r.xap_refs[1].chance = this->xap_chance[y][1];
    r.xap_refs[2].chance = this->xap_chance[y][2];
    r.percent_option = this->percent_option[y];
    r.sound = this->sound[y];
    r.text = this->text[y];
  }
  return ret;
}

RealmzScenarioData::MapMetadata RealmzScenarioData::MapMetadataFile::parse() const {
  RealmzScenarioData::MapMetadata ret;
  try {
    ret.land_type = land_type_to_string.at(this->land_type);
  } catch (const out_of_range& e) {
    ret.land_type = "unknown";
  }
  ret.is_dark = this->is_dark;
  ret.use_los = this->use_los;
  ret.random_rects = this->parse_random_rects();
  return ret;
}

vector<RealmzScenarioData::MapMetadata> RealmzScenarioData::load_map_metadata_index(const string& filename) {
  vector<MapMetadataFile> file_data = load_vector_file<MapMetadataFile>(filename);
  vector<MapMetadata> data;
  data.reserve(file_data.size());
  for (const auto& file_meta : file_data) {
    data.emplace_back(file_meta.parse());
  }
  return data;
}

static void draw_random_rects(ImageRGB888& map, const vector<RealmzScenarioData::RandomRect>& random_rects,
    size_t xpoff, size_t ypoff, bool is_dungeon, int16_t level_num, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h) {

  for (size_t z = 0; z < random_rects.size(); z++) {

    RealmzScenarioData::RandomRect rect = random_rects[z];
    // If the rect doesn't cover any tiles, skip it
    if (rect.left > rect.right || rect.top > rect.bottom) {
      continue;
    }

    // If the rect is completely outside of the drawing bounds, skip it
    if ((rect.right < x0) || (rect.bottom < y0) ||
        (rect.left > x0 + w) || (rect.top > y0 + h)) {
      continue;
    }

    // If the rect has no parameters set, skip it
    if (rect.top == 0 && rect.left == 0 && rect.bottom == 0 && rect.right == 0 &&
        rect.times_in_10k == 0 && rect.battle_low == 0 && rect.battle_high == 0 &&
        rect.xap_refs[0].is_empty() && rect.xap_refs[1].is_empty() && rect.xap_refs[2].is_empty() &&
        rect.percent_option == 0 && rect.sound == 0 && rect.text == 0) {
      continue;
    }

    // The bounds-checking logic is different for encounter-only rects (times_in_10k < 0) vs. normal rects. For the
    // former, the right and bottom edges are compared using strict inequality; for the latter, they are compared using
    // less-or-equal. To correct for this, we resize the rect if it's encounter-only here.
    if (rect.times_in_10k < 0) {
      rect.right--;
      rect.bottom--;
    }

    // If we get here, then the rect is nontrivial and is at least partially within the render window, so we should
    // draw it.

    // Clamp rect bounds to be within the render window
    if (rect.left < x0) {
      rect.left = x0;
    }
    if (rect.right > x0 + w - 1) {
      rect.right = x0 + w - 1;
    }
    if (rect.top < y0) {
      rect.top = y0;
    }
    if (rect.bottom > y0 + h - 1) {
      rect.bottom = y0 + h - 1;
    }

    ssize_t xp_left = (rect.left - x0) * 32 + xpoff;
    ssize_t xp_right = (rect.right - x0) * 32 + 32 - 1 + xpoff;
    ssize_t yp_top = (rect.top - y0) * 32 + ypoff;
    ssize_t yp_bottom = (rect.bottom - y0) * 32 + 32 - 1 + ypoff;

    ssize_t start_xx = (xp_left < 0) ? 0 : xp_left;
    ssize_t end_xx = (xp_right > static_cast<ssize_t>(map.get_width())) ? map.get_width() : xp_right;
    ssize_t start_yy = (yp_top < 0) ? 0 : yp_top;
    ssize_t end_yy = (yp_bottom > static_cast<ssize_t>(map.get_height())) ? map.get_height() : yp_bottom;
    for (ssize_t yy = start_yy; yy < end_yy; yy++) {
      for (ssize_t xx = start_xx; xx < end_xx; xx++) {
        uint32_t c = map.read(xx, yy);
        if (((xx + yy) / 8) & 1) {
          c = rgba8888((0xEF * get_r(c)) / 0xFF, (0xEF * get_g(c)) / 0xFF, (0xEF * get_b(c)) / 0xFF, 0xFF);
        } else {
          c = rgba8888(
              (0xFF0 + 0xEF * get_r(c)) / 0xFF, (0xFF0 + 0xEF * get_g(c)) / 0xFF, (0xFF0 + 0xEF * get_b(c)) / 0xFF, 0xFF);
        }
        map.write(xx, yy, c);
      }
    }

    uint32_t rect_color = (rect.times_in_10k < 0) ? 0xFF8000FF : 0xFFFFFFFF;
    map.draw_horizontal_line(xp_left, xp_right, yp_top, 0, rect_color);
    map.draw_horizontal_line(xp_left, xp_right, yp_bottom, 0, rect_color);
    map.draw_vertical_line(xp_left, yp_top, yp_bottom, 0, rect_color);
    map.draw_vertical_line(xp_right, yp_top, yp_bottom, 0, rect_color);

    string rectinfo;
    if (rect.times_in_10k == -1) {
      rectinfo = std::format("ENC XAP {}", rect.xap_refs[0].xap_num);

    } else {
      rectinfo = std::format("{}/10000", rect.times_in_10k);
      if (rect.battle_low || rect.battle_high) {
        rectinfo += std::format(" b=[{},{}]", rect.battle_low, rect.battle_high);
      }
      if (rect.percent_option) {
        rectinfo += std::format(" o={}%", rect.percent_option);
      }
      if (rect.sound) {
        rectinfo += std::format(" s={}", rect.sound);
      }
      if (rect.text) {
        rectinfo += std::format(" t={}", rect.text);
      }
      for (size_t y = 0; y < 3; y++) {
        if (rect.xap_refs[y].xap_num && rect.xap_refs[y].chance) {
          rectinfo += std::format(" XAP{}/{}%", rect.xap_refs[y].xap_num, rect.xap_refs[y].chance);
        }
      }
    }

    map.draw_text(xp_left + 2, yp_bottom - 8, nullptr, nullptr, rect_color, 0x00000080, "{}", rectinfo);
    map.draw_text(xp_left + 2, yp_bottom - 16, nullptr, nullptr, rect_color, 0x00000080, "{}RR{}/{}", is_dungeon ? 'D' : 'L', level_num, z);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA DD

vector<vector<RealmzScenarioData::APInfo>> RealmzScenarioData::load_ap_index(const string& filename) {
  vector<APInfo> all_info = RealmzScenarioData::load_xap_index(filename);
  vector<vector<APInfo>> level_ap_info(all_info.size() / 100);
  for (size_t x = 0; x < all_info.size(); x++) {
    level_ap_info[x / 100].push_back(all_info[x]);
  }
  return level_ap_info;
}

vector<RealmzScenarioData::APInfo> RealmzScenarioData::load_xap_index(const string& filename) {
  return load_vector_file<APInfo>(filename);
}

struct DisassemblyContext {
  const RealmzScenarioData& scen;
  const RealmzScenarioData::DisassemblyOrigin& origin;
  int16_t opcode;
  int16_t arg;
  mutable std::unordered_set<ssize_t> used_ecodes;

  const RealmzScenarioData::ECodes& ecodes(ssize_t offset = 0) const {
    ssize_t index = this->arg + offset;
    if ((index < 0) || (index >= static_cast<ssize_t>(this->scen.ecodes.size()))) {
      throw std::out_of_range(std::format("ecodes index out of range: {} (max: {})", index, this->scen.ecodes.size()));
    }
    this->used_ecodes.emplace(index);
    return this->scen.ecodes[index];
  }
  std::string render_string_reference(int16_t str_id) const {
    return ResourceDASM::render_string_reference(this->scen.strings, str_id);
  }
};

struct OpcodeDefinition {
  const char* name;
  const char* negative_name; // May be null
  std::vector<std::string> (*dasm_args)(const DisassemblyContext&);
};

static const OpcodeDefinition invalid_opcode_def = {
    ".invalid1",
    ".invalid2",
    [](const DisassemblyContext& d) -> std::vector<std::string> {
      try {
        const auto& ecodes = d.ecodes();
        return {std::format("[{}, {} => [{}, {}, {}, {}, {}]]",
            d.opcode, d.arg, ecodes.data[0], ecodes.data[1], ecodes.data[2], ecodes.data[3], ecodes.data[4])};
      } catch (const out_of_range&) {
        return {std::format("[{}, {} => (invalid ecodes index)]", d.opcode, d.arg)};
      }
    },
};

std::vector<std::string> dasm_battle_list(int16_t low, int16_t high) {
  if (high == 0) {
    return {std::format("BTL{}", abs(low))};
  } else {
    vector<string> ret;
    for (int z = abs(low); z < abs(high); z++) {
      ret.emplace_back(std::format("BTL{}", z));
    }
    return ret;
  }
}

std::string dasm_jump_target_xb(int16_t target_type, int16_t target_id, int16_t code_index) {
  string code_index_str = code_index ? std::format("+{}", code_index) : "";
  switch (target_type) {
    case -1:
      return std::format("exit_ap_delete", target_id, code_index_str); // TODO: This could be wrong
    case 0:
      return std::format("XAP{}{}", target_id, code_index_str);
    case 1:
      return std::format("SEC{}{}", target_id, code_index_str);
    case 2:
      return std::format("CEC{}{}", target_id, code_index_str);
    case 3:
      return std::format("exit_ap", target_id, code_index_str);
    default:
      return std::format("!(invalid xb jump target: {} {} {})", target_type, target_id, code_index_str);
  }
}

std::string dasm_jump_target_xsc012(int16_t target_type, int16_t target_id, int16_t code_index) {
  string code_index_str = code_index ? std::format("+{}", code_index) : "";
  switch (target_type) {
    case 0:
      return std::format("XAP{}{}", target_id, code_index_str);
    case 1:
      return std::format("SEC{}{}", target_id, code_index_str);
    case 2:
      return std::format("CEC{}{}", target_id, code_index_str);
    default:
      return std::format("!(invalid xsc012 jump target: {} {} {})", target_type, target_id, code_index_str);
  }
}

std::string dasm_jump_target_xsc123(int16_t target_type, int16_t target_id, int16_t code_index) {
  string code_index_str = code_index ? std::format("+{}", code_index) : "";
  switch (target_type) {
    case 1:
      return std::format("XAP{}{}", target_id, code_index_str);
    case 2:
      return std::format("SEC{}{}", target_id, code_index_str);
    case 3:
      return std::format("CEC{}{}", target_id, code_index_str);
    default:
      return std::format("!(invalid xsc123 jump target: {} {} {})", target_type, target_id, code_index_str);
  }
}

std::string dasm_jump_action_target_xb(int16_t action, int16_t target_type, int16_t target_id, int16_t code_index) {
  switch (action) {
    case 1:
      return std::format("target={}", dasm_jump_target_xb(target_type, target_id, code_index));
      break;
    case 2:
      return "target=exit_ap";
      break;
    case -2:
      return "target=exit_ap_delete";
      break;
    default:
      return std::format("!(invalid action: {})", action);
  }
}

std::vector<std::string> dasm_heal_picked_or_party_args(const DisassemblyContext& d) {
  const auto& [mult, low_range, high_range, sound, str_id] = d.ecodes().data;
  vector<string> ret{std::format("{} * rand({}, {})", mult, low_range, high_range)};
  if (sound) {
    ret.emplace_back(std::format("sound=SND{}", sound));
  }
  if (str_id) {
    ret.emplace_back(std::format("string={}", render_string_reference(d.scen.strings, str_id)));
  }
  return ret;
}

std::vector<std::string> dasm_spell_picked_or_party_args(const DisassemblyContext& d) {
  const auto& [spell_num, power_level, drv_modifier, cannot_drv, _] = d.ecodes().data;
  if (cannot_drv) {
    return {std::format("{} x{}", d.scen.desc_for_spell(spell_num), power_level), "cannot_drv"};
  } else {
    return {
        std::format("{} x{}", d.scen.desc_for_spell(spell_num), power_level),
        std::format("drv_adjust={}%", drv_modifier),
    };
  }
}

std::string name_for_attribute(int16_t attribute) {
  switch (attribute) {
    case 0:
      return "brawn";
    case 1:
      return "knowledge";
    case 2:
      return "judgment";
    case 3:
      return "agility";
    case 4:
      return "vitality";
    case 6:
      return "luck";
    default:
      return std::format("!(invalid attribute: {})", attribute);
  }
}

std::string name_for_race_class(int16_t race_class) {
  switch (race_class) {
    case 1:
      return "short";
    case 2:
      return "elvish";
    case 3:
      return "half-breed";
    case 4:
      return "goblinoid";
    case 5:
      return "reptilian";
    case 6:
      return "nether-worldly";
    case 7:
      return "goodly";
    case 8:
      return "neutral";
    case 9:
      return "evil";
    default:
      return std::format("!(invalid race class: {})", race_class);
  }
}

std::string name_for_caste_class(int16_t caste_class) {
  switch (caste_class) {
    case 1:
      return "warrior";
    case 2:
      return "thief";
    case 3:
      return "archer";
    case 4:
      return "sorcerer";
    case 5:
      return "priest";
    case 6:
      return "enchanter";
    case 7:
      return "warrior-wizard";
    default:
      return std::format("!(invalid caste class: {})", caste_class);
  }
}

std::string name_for_spell_type(int16_t type) {
  static const std::array<std::string, 8> names{
      "Charm", "Heat", "Cold", "Electrical", "Chemical", "Mental", "Magical", "Special"};
  try {
    return names.at(type);
  } catch (const std::out_of_range&) {
    return std::format("!(invalid spell type: {})", type);
  }
}

std::string dasm_ability_check(int16_t what, int16_t ability, int16_t success_mod) {
  string success_mod_str;
  if (success_mod > 0) {
    success_mod_str = std::format(" with +{}%", success_mod);
  } else if (success_mod < 0) {
    success_mod_str = std::format(" with -{}%", -success_mod);
  }
  if (what == 0) {
    try {
      return std::format("{}{}{}", (ability < 0) ? "inverted " : "", RealmzGlobalData::name_for_special_ability(abs(ability)), success_mod_str);
    } catch (const std::out_of_range&) {
      return std::format("!(invalid special ability: {}){}", ability, success_mod_str);
    }
  } else {
    return std::format("{}{}{}", (ability < 0) ? "inverted " : "", name_for_attribute(abs(ability)), success_mod_str);
  }
}

std::vector<std::string> dasm_tele_args(const DisassemblyContext& d) {
  const auto& [level, x, y, sound, str_id] = d.ecodes().data;
  std::vector<std::string> ret;
  if (level >= 0) {
    ret.emplace_back(std::format("level={}", level));
  }
  if (x >= 0) {
    ret.emplace_back(std::format("x={}", x));
  }
  if (y >= 0) {
    ret.emplace_back(std::format("y={}", y));
  }
  if (sound) {
    ret.emplace_back(std::format("sound=SND{}", sound));
  }
  if (str_id) {
    ret.emplace_back(std::format("string={}", render_string_reference(d.scen.strings, str_id)));
  }
  return ret;
}

std::vector<std::string> dasm_no_args(const DisassemblyContext&) {
  return {};
}

static const std::array<OpcodeDefinition, 128> opcode_defs{
    /* 0 */ invalid_opcode_def,
    /* 1 */ {
        "string",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          if (d.arg < 0) {
            return {d.render_string_reference(-d.arg), "no_wait"};
          } else {
            return {d.render_string_reference(d.arg)};
          }
        },
    },
    /* 2 */ {
        "battle",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [low, high, snd_or_lose_xap, string_id, treasure_mode] = d.ecodes().data;
          vector<string> ret = dasm_battle_list(low, high);
          if ((low < 0) || (high < 0)) {
            ret.emplace_back("surprise");
          }
          if (treasure_mode == 10) {
            ret.emplace_back(std::format("lose_xap=XAP{}", snd_or_lose_xap));
            ret.emplace_back(std::format("string={}", d.render_string_reference(string_id)));
          } else {
            ret.emplace_back(std::format("sound=SND{}", snd_or_lose_xap));
            ret.emplace_back(std::format("string={}", d.render_string_reference(string_id)));
            if (treasure_mode == 0) {
              ret.emplace_back("treasure_mode=all");
            } else if (treasure_mode == 5) {
              ret.emplace_back("treasure_mode=no_enemy");
            } else {
              ret.emplace_back(std::format("treasure_mode=!(invalid treasure mode: {})", treasure_mode));
            }
          }
          return ret;
        },
    },
    /* 3 */ {
        "option",
        "option_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [cont_opt, target_type, target_arg, left_prompt, right_prompt] = d.ecodes().data;
          bool left_continue = (cont_opt == 1);
          string target_str;
          switch (target_type) {
            case 0:
              target_str = "back_up";
              break;
            case 1:
              target_str = std::format("XAP{}", target_arg);
              break;
            case 2:
              target_str = std::format("SEC/{}", target_arg);
              break;
            case 3:
              target_str = std::format("CEC/{}", target_arg);
              break;
            case 4:
              target_str = "exit_ap_delete";
              break;
            default:
              target_str = std::format("!(invalid target: {} {})", target_type, target_arg);
          }

          // Guess: if the scenario has any option strings at all, use them; otherwise, use the normal string index?
          string left_str = (left_prompt == 0)
              ? render_string_reference(d.scen.option_strings.empty() ? d.scen.strings : d.scen.option_strings, left_prompt)
              : "Yes";
          string right_str = (right_prompt == 0)
              ? render_string_reference(d.scen.option_strings.empty() ? d.scen.strings : d.scen.option_strings, right_prompt)
              : "No";

          return {
              std::format("left=({}, target={})", left_str, left_continue ? "continue" : target_str),
              std::format("right=({}, target={})", right_str, left_continue ? target_str : "continue"),
          };
        },
    },
    /* 4 */ {
        "simple_enc",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {std::format("SEC{}", d.arg)};
        },
    },
    /* 5 */ {
        "complex_enc",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {std::format("CEC{}", d.arg)};
        },
    },
    /* 6 */ {
        "shop",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          if (d.arg < 0) {
            return {std::format("SHP{}", -d.arg), "auto_enter"};
          } else {
            return {std::format("SHP{}", d.arg)};
          }
        },
    },
    /* 7 */ {
        "modify_ap",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [level, id, source_xap, level_type, result_code] = d.ecodes().data;
          std::string target;
          if (level == -2) {
            target = std::format("SEC{}/{}", id, result_code);
          } else if (level == -3) {
            target = std::format("CEC{}/{}", id, result_code);
          } else if (level_type == 1) {
            target = std::format("LAP{}/{}", level, id);
          } else if (level_type == 2) {
            target = std::format("DAP{}/{}", level, id);
          } else {
            target = std::format("AP{}/{}", level, id);
          }
          return {target, std::format("source=XAP{}", source_xap)};
        },
    },
    /* 8 */ {
        "use_ap",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          switch (d.origin.type) {
            case RealmzScenarioData::DisassemblyOrigin::Type::LAND_AP:
              return {std::format("LAP{}/{}", d.origin.level_num, d.arg)};
            case RealmzScenarioData::DisassemblyOrigin::Type::DUNGEON_AP:
              return {std::format("DAP{}/{}", d.origin.level_num, d.arg)};
            case RealmzScenarioData::DisassemblyOrigin::Type::XAP:
              return {std::format("!(use_ap used in XAP; ap_num={})", d.arg)};
            case RealmzScenarioData::DisassemblyOrigin::Type::SIMPLE_ENCOUNTER:
              return {std::format("!(use_ap used in simple encounter; ap_num={})", d.arg)};
            case RealmzScenarioData::DisassemblyOrigin::Type::COMPLEX_ENCOUNTER:
              return {std::format("!(use_ap used in complex encounter; ap_num={})", d.arg)};
            default:
              return {std::format("!(use_ap used in unknown context; ap_num={})", d.arg)};
          }
        },
    },
    /* 9 */ {
        "sound",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          if (d.arg < 0) {
            return {std::format("SND{}", -d.arg), "pause"};
          } else {
            return {std::format("SND{}", d.arg)};
          }
        },
    },
    /* 10 */ {
        "treasure",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {std::format("TSR{}", d.arg)};
        },
    },
    /* 11 */ {
        "give_exp",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {std::format("{}", d.arg)};
        },
    },
    /* 12 */ {
        "change_tile",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [level, x, y, new_tile, level_type] = d.ecodes().data;
          return {
              std::format("{} level={}", level_type ? "dungeon" : "land", level),
              std::format("x={}", x),
              std::format("y={}", y),
              std::format("new_tile={}", new_tile),
          };
        },
    },
    /* 13 */ {
        "enable_ap",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [level, id, chance, id_low, id_high] = d.ecodes().data;
          return {
              std::format("{:c}AP{}/{}", (id_low < 0) ? 'D' : 'L', level, id),
              std::format("chance={}%", chance),
              std::format("id_low={}", id_low),
              std::format("id_high={}", id_high),
          };
        },
    },
    /* 14 */ {
        "pick_chars",
        "pick_chars",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          vector<string> ret{std::format("{}", abs(d.arg))};
          if (d.arg < 0) {
            ret.emplace_back("only_conscious");
          }
          if (d.opcode < 0) {
            ret.emplace_back("invert_selection");
          }
          return ret;
        },
    },
    /* 15 */ {"heal_picked", nullptr, dasm_heal_picked_or_party_args},
    /* 16 */ {"heal_party", nullptr, dasm_heal_picked_or_party_args},
    /* 17 */ {"spell_picked", nullptr, dasm_spell_picked_or_party_args},
    /* 18 */ {"spell_party", nullptr, dasm_spell_picked_or_party_args},
    /* 19 */ {
        "rand_string",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& ecodes = d.ecodes();
          int32_t low = ecodes.data[0];
          int32_t high = ecodes.data[1];
          if (low > high) {
            return {std::format("[{}, {}] (out of order)", low, high)};
          } else {
            vector<string> ret;
            for (ssize_t x = low; x <= high; x++) {
              ret.emplace_back(d.render_string_reference(x));
            }
            return ret;
          }
        },
    },
    /* 20 */ {"tele_and_run", nullptr, dasm_tele_args},
    /* 21 */ {
        "jmp_if_item",
        "jmp_if_item_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [item_id, target_type, nonposs_type, target_id, nonposs_target_id] = d.ecodes().data;
          vector<string> ret{
              d.scen.desc_for_item(item_id),
              std::format("on_success={}", dasm_jump_target_xsc012(target_type, target_id, 0)),
          };
          switch (nonposs_type) {
            case 0:
              ret.emplace_back(std::format("on_failure={}", dasm_jump_target_xsc012(target_type, nonposs_target_id, 0)));
              break;
            case 2:
              ret.emplace_back(std::format("on_failure=string_exit:{}",
                  d.render_string_reference(nonposs_target_id)));
              break;
            default:
              ret.emplace_back("on_failure=continue");
          }
          return ret;
        },
    },
    /* 22 */ {
        "change_item",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [item_id, count, action, charges, new_item_id] = d.ecodes().data;
          vector<string> ret{std::format("{} x{}", d.scen.desc_for_item(item_id), count)};
          switch (action) {
            case 1:
              ret.emplace_back("drop");
              break;
            case 2:
              ret.emplace_back(std::format("charges {:c}{}", (charges >= 0) ? '+' : '-', abs(charges)));
              break;
            case 3:
              ret.emplace_back(std::format("replace with {}", d.scen.desc_for_item(new_item_id)));
              break;
            default:
              ret.emplace_back(std::format("!(invalid action: {})", action));
          }
          return ret;
        },
    },
    /* 23 */ {
        "change_rect",
        "change_rect",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [level, id, chance, battle_low, battle_high] = d.ecodes().data;
          vector<string> ret{
              std::format("{:c}RR{}/{}", (d.opcode < 0) ? 'D' : 'L', level, id),
              std::format("chance={}/10000", chance),
          };
          if (battle_low >= 0 && battle_high >= 0) {
            ret.emplace_back(((battle_low == battle_high) || (battle_high == 0))
                    ? std::format("battle=BTL{}", battle_low)
                    : std::format("battle_range=[BTL{}, BTL{}]", battle_low, battle_high));
          } else if (battle_low >= 0) {
            ret.emplace_back(std::format("battle_range=[BTL{}, unchanged]", battle_low));
          } else if (battle_high >= 0) {
            ret.emplace_back(std::format("battle_range=[unchanged, BTL{}]", battle_high));
          }
          return ret;
        },
    },
    /* 24 */ {"exit_ap", nullptr, dasm_no_args},
    /* 25 */ {"exit_ap_delete", nullptr, dasm_no_args},
    /* 26 */ {"mouse_click", nullptr, dasm_no_args},
    /* 27 */ {
        "picture",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {std::format("PICT:{}", d.arg)};
        },
    },
    /* 28 */ {"redraw", nullptr, dasm_no_args},
    /* 29 */ {
        "give_map",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          if (d.arg < 0) {
            return {std::format("MAP{}", -d.arg), "auto_show"};
          } else {
            return {std::format("MAP{}", d.arg)};
          }
        },
    },
    /* 30 */ {
        "pick_ability",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [ability, success_mod, who, what, _] = d.ecodes().data;

          vector<string> ret{dasm_ability_check(what, ability, success_mod)};
          string who_str;
          switch (who) {
            case 1:
              ret.emplace_back("who=all");
              break;
            case 2:
              ret.emplace_back("who=alive");
              break;
            default:
              ret.emplace_back("who=picked");
          }
          return ret;
        },
    },
    /* 31 */ {
        "jmp_ability",
        "jmp_ability_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [ability, success_mod, what, success_xap, failure_xap] = d.ecodes().data;
          return {
              dasm_ability_check(what, ability, success_mod),
              std::format("on_success=XAP{}", success_xap),
              std::format("on_failure=XAP{}", failure_xap),
          };
        },
    },
    /* 32 */ {
        "temple",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {std::format("inflation={}%", d.arg)};
        },
    },
    /* 33 */ {
        "take_money",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [amount, action, target_type, target, code_index] = d.ecodes().data;
          vector<string> ret{std::format("{} {}", abs(amount), (amount < 0) ? "gems" : "gold")};
          switch (action) {
            case -1:
              ret.emplace_back("on_success=continue");
              ret.emplace_back("on_failure=jmp_back");
              break;
            case 0:
              ret.emplace_back("on_success=continue");
              ret.emplace_back(std::format("on_failure={}", dasm_jump_target_xb(target_type, target, code_index)));
              break;
            case 1:
              ret.emplace_back(std::format("on_success={}", dasm_jump_target_xb(target_type, target, code_index)));
              ret.emplace_back("on_failure=continue");
              break;
            case 2:
              ret.emplace_back(std::format("jump={}", dasm_jump_target_xb(target_type, target, code_index)));
              break;
            default:
              ret.emplace_back(std::format("!(invalid action: {})", action));
          }
          return ret;
        },
    },
    /* 34 */ {"break_enc", nullptr, dasm_no_args},
    /* 35 */ {
        "simple_enc_del",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {std::format("SEC/{}", d.arg)};
        },
    },
    /* 36 */ {
        "stash_items",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          switch (d.arg) {
            case 0:
              return {"restore"};
            case 1:
              return {"stash"};
            default:
              return {std::format("!(invalid argument: {})", d.arg)};
          }
        },
    },
    /* 37 */ {
        "set_dungeon",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [level_type, level, x, y, dir] = d.ecodes().data;
          vector<string> ret{
              std::format("{} level={}", (level_type == 0) ? "dungeon" : "land", level),
              std::format("x={}", x),
              std::format("y={}", y),
          };
          if (level_type == 0) {
            switch (abs(dir)) {
              case 1:
                ret.emplace_back("dir=north");
                break;
              case 2:
                ret.emplace_back("dir=east");
                break;
              case 3:
                ret.emplace_back("dir=south");
                break;
              case 4:
                ret.emplace_back("dir=west");
                break;
              default:
                ret.emplace_back(std::format("dir=!(invalid direction: {})", dir));
            }
            if (dir < 0) {
              ret.emplace_back("disable_2d");
            }
          }
          return ret;
        },
    },
    /* 38 */ {
        "jmp_if_item_enc",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [item_id, action, target_type, target_id, code_index] = d.ecodes().data;
          switch (action) {
            case 0:
              return {
                  d.scen.desc_for_item(item_id),
                  "on_success=continue",
                  std::format("on_failure={}", dasm_jump_target_xb(target_type, target_id, code_index)),
              };
            case 1:
              return {
                  d.scen.desc_for_item(item_id),
                  std::format("on_success={}", dasm_jump_target_xb(target_type, target_id, code_index)),
                  "on_failure=continue",
              };
            default:
              return {d.scen.desc_for_item(item_id), std::format("!(invalid action: {})", action)};
          }
        },
    },
    /* 39 */ {
        "jmp_xap",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {std::format("XAP{}", d.arg)};
        },
    },
    /* 40 */ {
        "jmp_party_cond",
        "jmp_party_cond_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [action, target_type, target_id, condition, _] = d.ecodes().data;
          vector<string> ret;
          try {
            ret.emplace_back(RealmzGlobalData::name_for_party_condition(condition));
          } catch (const std::out_of_range&) {
            ret.emplace_back(std::format("!(invalid party condition: {})", condition));
          }
          switch (action) {
            case 1:
              ret.emplace_back(std::format("on_success={}", dasm_jump_target_xsc123(target_type, target_id, 0)));
              ret.emplace_back("on_failure=continue");
              break;
            case 2:
              ret.emplace_back("on_success=continue");
              ret.emplace_back(std::format("on_failure={}", dasm_jump_target_xsc123(target_type, target_id, 0)));
              break;
            default:
              ret.emplace_back(std::format("!(invalid action: {})", action));
          }
          return ret;
        },
    },
    /* 41 */ {
        "somple_enc_del_any",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& ecodes = d.ecodes();
          return {std::format("SEC{}/{}", ecodes.data[0], ecodes.data[1])};
        },
    },
    /* 42 */ {
        "jmp_random",
        "jmp_random_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [percent, action, target_type, target_id, code_index] = d.ecodes().data;
          return {std::format("{}%", percent), dasm_jump_action_target_xb(action, target_type, target_id, code_index)};
        },
    },
    /* 43 */ {
        "give_cond",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [who, condition, duration, sound, _] = d.ecodes().data;
          vector<string> ret;
          try {
            ret.emplace_back(std::format("{} x{} ({})",
                RealmzGlobalData::name_for_condition(condition), abs(duration), (duration < 0) ? "permanent" : "temporary"));
          } catch (const std::out_of_range&) {
            ret.emplace_back(std::format("!(invalid condition: {}) x{} ({})",
                condition, abs(duration), (duration < 0) ? "permanent" : "temporary"));
          }
          switch (who) {
            case 0:
              ret.emplace_back("who=all");
              break;
            case 1:
              ret.emplace_back("who=picked");
              break;
            case 2:
              ret.emplace_back("who=alive");
              break;
            default:
              ret.emplace_back(std::format("who=!(invalid who: {})", who));
          }
          if (sound) {
            ret.emplace_back(std::format("sound=SND{}", sound));
          }
          return ret;
        },
    },
    /* 44 */ {
        "complex_enc_del",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {std::format("CEC/{}", d.arg)};
        },
    },
    /* 45 */ {"tele", nullptr, dasm_tele_args},
    /* 46 */ {
        "jmp_quest",
        "jmp_quest_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [flag_num, check, target_type, target_id, code_index] = d.ecodes().data;
          vector<string> ret{std::format("{}", flag_num)};
          switch (check) {
            case 0:
              ret.emplace_back(std::format("if_set={}", dasm_jump_target_xb(target_type, target_id, code_index)));
              ret.emplace_back("if_unset=continue");
              break;
            case 1:
              ret.emplace_back("if_set=continue");
              ret.emplace_back(std::format("if_unset={}", dasm_jump_target_xb(target_type, target_id, code_index)));
              break;
            default:
              ret.emplace_back(std::format("!(invalid check: {})", check));
          }
          return ret;
        },
    },
    /* 47 */ {
        "update_quest_flag",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          if (d.arg < 0) {
            return {std::format("clear {}", -d.arg)};
          } else {
            return {std::format("set {}", d.arg)};
          }
        },
    },
    /* 48 */ {
        "pick_battle",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [low, high, sound, str_id, treasure] = d.ecodes().data;
          vector<string> ret = dasm_battle_list(low, high);
          if (sound) {
            ret.emplace_back(std::format("sound=SND{}", sound));
          }
          if (str_id) {
            ret.emplace_back(std::format("string={}", d.render_string_reference(str_id)));
          }
          ret.emplace_back(std::format("treasure=TSR{}", treasure));
          return ret;
        },
    },
    /* 49 */ {"bank", nullptr, dasm_no_args},
    /* 50 */ {
        "pick_attribute",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [type, gender, race_caste, _, who] = d.ecodes().data;
          vector<string> ret;
          switch (type) {
            case 0:
              try {
                ret.emplace_back(std::format("race={}", d.scen.global.race_names.at(race_caste)));
              } catch (const std::out_of_range&) {
                ret.emplace_back(std::format("race=!(invalid race: {})", race_caste));
              }
              break;
            case 1:
              if (gender == 1) {
                ret.emplace_back("gender=male");
              } else if (gender == 2) {
                ret.emplace_back("gender=female");
              } else {
                ret.emplace_back(std::format("gender=!(invalid gender: {})", gender));
              }
              break;
            case 2:
              try {
                ret.emplace_back(std::format("caste={}", d.scen.global.caste_names.at(race_caste)));
              } catch (const std::out_of_range&) {
                ret.emplace_back(std::format("caste=!(invalid caste: {})", race_caste));
              }
              break;
            case 3:
              ret.emplace_back(std::format("race_class={}", name_for_race_class(race_caste)));
              break;
            case 4:
              ret.emplace_back(std::format("caste_class={}", name_for_caste_class(race_caste)));
              break;
            default:
              ret.emplace_back(std::format("!(invalid type: {} {})", type, race_caste));
          }
          switch (who) {
            case 0:
              ret.emplace_back("who=all");
              break;
            case 1:
              ret.emplace_back("who=alive");
              break;
            default:
              ret.emplace_back(std::format("who=!(invalid who: {})", who));
              break;
          }
          return ret;
        },
    },
    /* 51 */ {
        "change_shop",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [shop_id, inflation_percent_delta, item_id, item_count, _] = d.ecodes().data;
          vector<string> ret{std::format("SHP{}", shop_id)};
          if (inflation_percent_delta > 0) {
            ret.emplace_back(std::format("inflation +{}%", inflation_percent_delta));
          } else if (inflation_percent_delta < 0) {
            ret.emplace_back(std::format("inflation -{}%", -inflation_percent_delta));
          }
          if (item_id && item_count) {
            ret.emplace_back(std::format("item={} {:c}x{}",
                d.scen.desc_for_item(item_id), (item_count > 0) ? '+' : '-', abs(item_count)));
          }
          return ret;
        },
    },
    /* 52 */ {
        "pick_misc",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [type, param, who, _1, _2] = d.ecodes().data;
          vector<string> ret;
          switch (type) {
            case 0:
              ret.emplace_back(std::format("movement < {}", param));
              break;
            case 1:
              ret.emplace_back(std::format("party_order < {}", param));
              break;
            case 2:
              ret.emplace_back(std::format("has_item({})", d.scen.desc_for_item(param)));
              break;
            case 3:
              ret.emplace_back(std::format("random with {}% chance", param));
              break;
            case 4:
              ret.emplace_back(std::format("save vs. {}", name_for_attribute(param)));
              break;
            case 5:
              ret.emplace_back(std::format("save vs. {}", name_for_spell_type(param)));
              break;
            case 6:
              ret.emplace_back(std::format("selected character"));
              break;
            case 7:
              ret.emplace_back(std::format("item_is_equipped({})", d.scen.desc_for_item(param)));
              break;
            case 8:
              ret.emplace_back(std::format("party_order == {}", param));
              break;
            default:
              ret.emplace_back(std::format("!(invalid type: {} {})", type, param));
          }
          switch (who) {
            case 0:
              ret.emplace_back("who=all");
              break;
            case 1:
              ret.emplace_back("who=alive");
              break;
            case 2:
              ret.emplace_back("who=picked");
              break;
            default:
              ret.emplace_back(std::format("who=!(invalid who: {})", who));
              break;
          }
          return ret;
        },
    },
    /* 53 */ {
        "pick_caste",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [caste, caste_type, who, _1, _2] = d.ecodes().data;
          std::vector<std::string> ret;
          if (caste) {
            try {
              ret.emplace_back(std::format("caste={}", d.scen.global.caste_names.at(caste)));
            } catch (const std::out_of_range&) {
              ret.emplace_back(std::format("caste=!(invalid caste: {})", caste));
            }
          }
          switch (caste_type) {
            case 0:
              break;
            case 1:
              ret.emplace_back("caste_class=fighter");
              break;
            case 2:
              ret.emplace_back("caste_class=magical");
              break;
            case 3:
              ret.emplace_back("caste_class=monk/rogue");
              break;
            default:
              ret.emplace_back(std::format("caste_class=!(invalid caste class: {})", caste_type));
          }
          switch (who) {
            case 0:
              ret.emplace_back("who=all");
              break;
            case 1:
              ret.emplace_back("who=alive");
              break;
            case 2:
              ret.emplace_back("who=picked");
              break;
            default:
              ret.emplace_back(std::format("!(invalid who: {})", who));
              break;
          }
          return ret;
        },
    },
    /* 54 */ {
        "change_time_enc",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [tec_num, percent, new_day_incr, reset_to_current, days_to_next_instance] = d.ecodes().data;
          std::vector<std::string> ret;
          ret.emplace_back(std::format("TEC{}", tec_num));
          if (percent != -1) {
            ret.emplace_back(std::format("chance={}%", percent));
          }
          if (new_day_incr != -1) {
            ret.emplace_back(std::format("new_day_incr={}", new_day_incr));
          }
          if (reset_to_current == 0) {
            ret.emplace_back("reset_to_current=no");
          } else if (reset_to_current == 1) {
            ret.emplace_back("reset_to_current=yes");
          } else {
            ret.emplace_back(std::format("reset_to_current=!(invalid reset flag: {})", reset_to_current));
          }
          if (days_to_next_instance != -1) {
            ret.emplace_back(std::format("days_to_next_instance={}", days_to_next_instance));
          }
          return ret;
        },
    },
    /* 55 */ {
        "jmp_picked",
        "jmp_picked_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [pc_id, fail_action, _, success_xap, fail_param] = d.ecodes().data;
          vector<string> ret;
          if (pc_id == 0) {
            ret.emplace_back("any");
          } else if (pc_id > 0) {
            ret.emplace_back(std::format("position {}", pc_id));
          } else {
            ret.emplace_back(std::format("at least {}", -pc_id));
          }
          ret.emplace_back(std::format("on_success=XAP{}", success_xap));
          switch (fail_action) {
            case 0:
              ret.emplace_back("on_failure=exit_ap");
              break;
            case 1:
              ret.emplace_back(std::format("on_failure=XAP{}", fail_param));
              break;
            case 2:
              ret.emplace_back(std::format("on_failure=string_exit:{}",
                  d.render_string_reference(fail_param)));
              break;
            default:
              ret.emplace_back(std::format("on_failure=!(invalid failure action: {} {})", fail_action, fail_param));
          }
          return ret;
        },
    },
    /* 56 */ {
        "jmp_battle",
        "jmp_battle_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [low, high, lose_xap, sound, str_id] = d.ecodes().data;
          vector<string> ret = dasm_battle_list(low, high);
          if (lose_xap < 0) {
            ret.emplace_back("on_failure=back_up");
          } else {
            ret.emplace_back(std::format("on_failure=XAP{}", lose_xap));
          }
          if (sound) {
            ret.emplace_back(std::format("sound=SND{}", sound));
          }
          if (str_id) {
            ret.emplace_back(std::format("string={}", d.render_string_reference(str_id)));
          }
          return ret;
        },
    },
    /* 57 */ {
        "change_tileset",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [new_tileset, dark, level, _1, _2] = d.ecodes().data;
          vector<string> ret{std::format("level={}", level), std::format("new_tileset={}", new_tileset)};
          if (dark == 0) {
            ret.emplace_back("dark=no");
          } else if (dark == 1) {
            ret.emplace_back("dark=yes");
          } else {
            ret.emplace_back(std::format("dark=!(invalid dark flag: {})", dark));
          }
          return ret;
        },
    },
    /* 58 */ {
        "jmp_difficulty",
        "jmp_difficulty_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [difficulty, action, target_type, target, code_index] = d.ecodes().data;
          vector<string> ret;
          switch (difficulty) {
            case 1:
              ret.emplace_back("difficulty >= Novice");
              break;
            case 2:
              ret.emplace_back("difficulty >= Easy");
              break;
            case 3:
              ret.emplace_back("difficulty >= Normal");
              break;
            case 4:
              ret.emplace_back("difficulty >= Hard");
              break;
            case 5:
              ret.emplace_back("difficulty >= Veteran");
              break;
            default:
              ret.emplace_back(std::format("difficulty >= !(invalid difficulty: {})", difficulty));
          }
          ret.emplace_back(dasm_jump_action_target_xb(action, target_type, target, code_index));
          return ret;
        },
    },
    /* 59 */ {
        "jmp_tile",
        "jmp_tile_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [tile_id, action, target_type, target, code_index] = d.ecodes().data;
          return {std::format("{}", tile_id), dasm_jump_action_target_xb(action, target_type, target, code_index)};
        },
    },
    /* 60 */ {
        "drop_money",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& ecodes = d.ecodes();
          vector<string> ret;
          switch (ecodes.data[0]) {
            case 1:
              ret.emplace_back("gold");
              break;
            case 2:
              ret.emplace_back("gems");
              break;
            case 3:
              ret.emplace_back("jewelry");
              break;
            default:
              ret.emplace_back(std::format("!(invalid money type: {})", ecodes.data[0]));
          }
          switch (ecodes.data[1]) {
            case 0:
              ret.emplace_back("who=all");
              break;
            case 1:
              ret.emplace_back("who=picked");
              break;
            default:
              ret.emplace_back(std::format("!(invalid who: {})", ecodes.data[1]));
          }
          return ret;
        },
    },
    /* 61 */ {
        "incr_party_loc",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [_1, x, y, move_type, _2] = d.ecodes().data;
          switch (move_type) {
            case 0:
              return {std::format("x={}", x), std::format("y={}", y)};
            case 1:
              return {std::format("x=rand(1, {})", x), std::format("y=rand(1, {})", y)};
            default:
              return {std::format("x={}", x), std::format("y={}", y), std::format("!(invalid move type: {})", move_type)};
          }
        },
    },
    /* 62 */ {
        "story",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {std::format("{}", d.arg)};
        },
    },
    /* 63 */ {
        "change_time",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [base, days, hours, minutes, _] = d.ecodes().data;
          vector<string> ret;
          switch (base) {
            case 1:
              ret.emplace_back("absolute");
              if (days != -1) {
                ret.emplace_back(std::format("days={}", days));
              }
              if (hours != -1) {
                ret.emplace_back(std::format("hours={}", hours));
              }
              if (minutes != -1) {
                ret.emplace_back(std::format("minutes={}", minutes));
              }
              break;
            case 2:
              ret.emplace_back("relative");
              if (days > 0) {
                ret.emplace_back(std::format("days +{}", days));
              } else if (days < 0) {
                ret.emplace_back(std::format("days -{}", -days));
              }
              if (hours > 0) {
                ret.emplace_back(std::format("hours +{}", hours));
              } else if (hours < 0) {
                ret.emplace_back(std::format("hours -{}", -hours));
              }
              if (minutes > 0) {
                ret.emplace_back(std::format("minutes +{}", minutes));
              } else if (minutes < 0) {
                ret.emplace_back(std::format("minutes -{}", -minutes));
              }
              break;
            default:
              ret.emplace_back(std::format("!(invalid base: {})", base));
          }
          return ret;
        },
    },
    /* 64 */ {
        "jmp_time",
        "jmp_time_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [day, hour, _, before_or_equal_xap, after_xap] = d.ecodes().data;
          return {
              (day == -1) ? "day=any" : std::format("day <= {}", day),
              (hour == -1) ? "hour=any" : std::format("hour <= {}", hour),
              std::format("on_success=XAP{}", before_or_equal_xap),
              std::format("on_failure=XAP{}", after_xap),
          };
        },
    },
    /* 65 */ {
        "give_rand_item",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [count, low, high, _1, _2] = d.ecodes().data;
          vector<string> ret{(count < 0) ? std::format("count=rand(1, {})", -count) : std::format("count={}", count)};
          if (high < low) {
            ret.emplace_back(std::format("!(invalid range: {} {})", low, high));
          } else {
            for (int32_t item_id = low; item_id < high; item_id++) {
              ret.emplace_back(d.scen.desc_for_item(item_id));
            }
          }
          return ret;
        },
    },
    /* 66 */ {
        "allow_camping",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          if (d.arg == 0) {
            return {"enable"};
          } else if (d.arg == 1) {
            return {"disable"};
          } else {
            return {std::format("!(invalid argument: {})", d.arg)};
          }
        },
    },
    /* 67 */ {
        "jmp_item_charge",
        "jmp_item_charge_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [item_id, target_type, min_charges, success_target, failure_target] = d.ecodes().data;
          return {
              d.scen.desc_for_item(item_id),
              std::format("charges >= {}", min_charges),
              std::format("on_success={}", dasm_jump_target_xsc012(target_type, success_target, 0)),
              std::format("on_failure={}", dasm_jump_target_xsc012(target_type, failure_target, 0)),
          };
        },
    },
    /* 68 */ {
        "change_fatigue",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& ecodes = d.ecodes();
          switch (ecodes.data[0]) {
            case 1:
              return {"set_full"};
            case 2:
              return {"set_empty"};
            case 3:
              return {std::format("{}% of current value", ecodes.data[1])};
            default:
              return {std::format("!(invalid change type: {})", ecodes.data[0])};
          }
        },
    },
    /* 69 */ {
        "change_casting_flags",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [enable_char_casting, enable_npc_casting, enable_recharging, _1, _2] = d.ecodes().data;
          return {
              enable_char_casting ? "enable_char_casting" : "disable_char_casting",
              enable_npc_casting ? "enable_npc_casting" : "disable_npc_casting",
              enable_recharging ? "enable_recharging" : "disable_recharging",
          };
        },
    },
    /* 70 */ {
        "save_restore_loc",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& ecodes = d.ecodes();
          switch (ecodes.data[0]) {
            case 1:
              return {"save"};
            case 2:
              return {"restore"};
            default:
              return {std::format("!(invalid action: {})", ecodes.data[0])};
          }
        },
    },
    /* 71 */ {
        "enable_coord_display",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {d.arg ? "disable" : "enable"};
        },
    },
    /* 72 */ {
        "jmp_quest_range_all",
        "jmp_quest_range_all_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [low, high, _, target_type, target] = d.ecodes().data;
          if (low > high) {
            return {std::format("!(invalid range: {} {})", low, high)};
          }
          vector<string> ret;
          for (int32_t z = low; z < high; z++) {
            ret.emplace_back(std::format("{}", z));
          }
          ret.emplace_back(dasm_jump_target_xsc012(target_type, target, 0));
          return ret;
        },
    },
    /* 73 */ {
        "shop_restrict",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [shop_id, low1, high1, low2, high2] = d.ecodes().data;
          vector<string> ret{std::format("SHP{}", abs(shop_id))};
          if (shop_id < 0) {
            ret.emplace_back("auto_enter");
          }
          if (low1 || high1) {
            ret.emplace_back(std::format("accept1=[{}, {}]", low1, high1));
          }
          if (low2 || high2) {
            ret.emplace_back(std::format("accept2=[{}, {}]", low2, high2));
          }
          return ret;
        },
    },
    /* 74 */ {
        "give_spell_pts_picked",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [multiplier, low, high, sound, str_id] = d.ecodes().data;
          vector<string> ret;
          if (low == high) {
            ret.emplace_back(std::format("{}", multiplier * low));
          } else {
            ret.emplace_back(std::format("{} * rand({}, {})", multiplier, low, high));
          }
          if (sound) {
            ret.emplace_back(std::format("sound=SND{}", sound));
          }
          if (str_id) {
            ret.emplace_back(std::format("string={}", d.render_string_reference(str_id)));
          }
          return ret;
        },
    },
    /* 75 */ {
        "jmp_spell_pts",
        "jmp_spell_pts_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [who, min_pts, fail_action, target_type, target] = d.ecodes().data;
          vector<string> ret{
              std::format(">= {}", min_pts), std::format("on_success={}", dasm_jump_target_xsc012(target_type, target, 0))};
          switch (fail_action) {
            case 0:
              ret.emplace_back("on_failure=continue");
              break;
            case 1:
              ret.emplace_back("on_failure=exit_ap");
              break;
            default:
              ret.emplace_back(std::format("on_failure=!(invalid action: {})", fail_action));
          }
          switch (who) {
            case 1:
              ret.emplace_back("who=picked");
              break;
            case 2:
              ret.emplace_back("who=alive");
              break;
            default:
              ret.emplace_back(std::format("who=!(invalid who: {})", who));
          }
          return ret;
        },
    },
    /* 76 */ {
        "incr_quest_value",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [flag_num, delta, target_type, jump_min_value, target] = d.ecodes().data;
          vector<string> ret{std::format("{}", flag_num), std::format("delta={}", delta)};
          if (jump_min_value) {
            ret.emplace_back(std::format("jump if >= {}", jump_min_value));
            ret.emplace_back(std::format("target={}", dasm_jump_target_xsc123(target_type - 1, target, 0)));
          }
          return ret;
        },
    },
    /* 77 */ {
        "jmp_quest_value",
        "jmp_quest_value_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [flag_num, threshold, target_type, target_less, target_equal_greater] = d.ecodes().data;
          return {
              std::format("{}", flag_num),
              std::format("threshold={}", threshold),
              (target_less == 0)
                  ? "if_less=continue"
                  : std::format("if_less={}", dasm_jump_target_xsc012(target_type, target_less, 0)),
              (target_equal_greater == 0)
                  ? "if_equal_or_greater=continue"
                  : std::format("if_equal_or_greater={}", dasm_jump_target_xsc012(target_type, target_equal_greater, 0)),
          };
        },
    },
    /* 78 */ {
        "jmp_tile_params",
        "jmp_tile_params_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [attr, tile_id, target_type, target_false, target_true] = d.ecodes().data;
          vector<string> ret;
          switch (attr) {
            case 1:
              ret.emplace_back("is_shoreline");
              break;
            case 2:
              ret.emplace_back("is_need_boat");
              break;
            case 3:
              ret.emplace_back("is_path");
              break;
            case 4:
              ret.emplace_back("blocks_los");
              break;
            case 5:
              ret.emplace_back("need_fly_float");
              break;
            case 6:
              ret.emplace_back("is_forest");
              break;
            case 7:
              ret.emplace_back(std::format("tile_id={}", tile_id));
              break;
            default:
              ret.emplace_back(std::format("!(invalid attribute: {})", attr));
          }
          ret.emplace_back((target_false == 0)
                  ? "if_false=continue"
                  : std::format("if_false={}", dasm_jump_target_xsc012(target_type, target_false, 0)));
          ret.emplace_back((target_true == 0)
                  ? "if_true=continue"
                  : std::format("if_true={}", dasm_jump_target_xsc012(target_type, target_true, 0)));
          return ret;
        },
    },
    /* 79 */ invalid_opcode_def,
    /* 80 */ invalid_opcode_def,
    /* 81 */ {
        "jmp_char_cond",
        "jmp_char_cond_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [cond, who, _, success_xap, failure_xap] = d.ecodes().data;
          vector<string> ret;
          try {
            ret.emplace_back(RealmzGlobalData::name_for_condition(cond));
          } catch (const out_of_range&) {
            ret.emplace_back(std::format("!(invalid condition: {})", cond));
          }
          if (who == -1) {
            ret.emplace_back("who=picked");
          } else if (who == 0) {
            ret.emplace_back("who=party");
          } else {
            ret.emplace_back(std::format("who=position {}", who));
          }
          ret.emplace_back(success_xap ? std::format("on_success=XAP{}", success_xap) : "on_success=continue");
          ret.emplace_back(failure_xap ? std::format("on_failure=XAP{}", failure_xap) : "on_failure=continue");
          return ret;
        },
    },
    /* 82 */ {"enable_turning", nullptr, dasm_no_args},
    /* 83 */ {"disable_turning", nullptr, dasm_no_args},
    /* 84 */ {"check_scen_registered", nullptr, dasm_no_args},
    /* 85 */ {
        "jmp_random_xap",
        "jmp_random_xap_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [target_type, target_low, target_high, sound, str_id] = d.ecodes().data;
          if (target_low > target_high) {
            return {std::format("!(invalid range: {} {})", target_low, target_high)};
          }
          vector<string> ret;
          for (int32_t z = target_low; z < target_high; z++) {
            ret.emplace_back(dasm_jump_target_xsc012(target_type, z, 0));
          }
          if (sound) {
            ret.emplace_back(std::format("sound=SND{}", sound));
          }
          if (str_id) {
            ret.emplace_back(std::format("string={}", d.render_string_reference(str_id)));
          }
          return ret;
        },
    },
    /* 86 */ {
        "jmp_misc",
        "jmp_misc_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [what, value, target_type, target_true, target_false] = d.ecodes().data;
          bool picked_only = (value < 0);
          bool show_who = true;
          uint16_t used_value = abs(value);
          vector<string> ret;
          switch (what) {
            case 0:
              try {
                ret.emplace_back(std::format("caste_present={}", d.scen.global.caste_names.at(used_value - 1)));
              } catch (const out_of_range&) {
                ret.emplace_back(std::format("caste_present=!(invalid caste: {})", used_value));
              }
              break;
            case 1:
              try {
                ret.emplace_back(std::format("race_present={}", d.scen.global.race_names.at(used_value - 1)));
              } catch (const out_of_range&) {
                ret.emplace_back(std::format("race_present=!(invalid race: {})", used_value));
              }
              break;
            case 2:
              if (used_value == 1) {
                ret.emplace_back("gender_present=male");
              } else if (used_value == 2) {
                ret.emplace_back("gender_present=female");
              } else {
                ret.emplace_back(std::format("gender_present=!(invalid gender: {})", used_value));
              }
              break;
            case 3:
              ret.emplace_back("in_boat");
              show_who = false;
              break;
            case 4:
              ret.emplace_back("camping");
              show_who = false;
              break;
            case 5:
              ret.emplace_back(std::format("caste_class_present={}", name_for_caste_class(used_value)));
              break;
            case 6:
              ret.emplace_back(std::format("race_class_present={}", name_for_race_class(used_value)));
              break;
            case 7:
              ret.emplace_back(std::format("levels > {}", value));
              picked_only = false;
              break;
            case 8:
              ret.emplace_back(std::format("levels > {}", value));
              picked_only = true;
              break;
            default:
              ret.emplace_back(std::format("!(invalid check: {} {})", what, value));
              break;
          }
          if (show_who) {
            ret.emplace_back(picked_only ? "who=picked" : "who=all");
          }
          ret.emplace_back(std::format("on_success={}", dasm_jump_target_xsc012(target_type, target_true, 0)));
          ret.emplace_back(target_false
                  ? std::format("on_failure={}", dasm_jump_target_xsc012(target_type, target_true, 0))
                  : "on_failure=continue");
          return ret;
        },
    },
    /* 87 */ {
        "jmp_npc",
        "jmp_npc_link",
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [npc_id, target_type, fail_action, target, fail_param] = d.ecodes().data;
          vector<string> ret{
              std::format("MST{}", npc_id), std::format("on_success={}", dasm_jump_target_xsc012(target_type, target, 0))};
          switch (fail_action) {
            case 0:
              ret.emplace_back(std::format("on_failure={}", dasm_jump_target_xsc012(target_type, fail_param, 0)));
              break;
            case 1:
              ret.emplace_back("on_failure=continue");
              break;
            case 2:
              ret.emplace_back(std::format("on_failure=string_exit:{}",
                  d.render_string_reference(fail_param)));
              break;
            default:
              ret.emplace_back(std::format("on_failure=!(invalid target: {} {})", fail_action, fail_param));
          }
          return ret;
        },
    },
    /* 88 */ {
        "drop_npc",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {std::format("MST{}", d.arg)};
        },
    },
    /* 89 */ {
        "add_npc",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {std::format("MST{}", d.arg)};
        },
    },
    /* 90 */ {
        "take_exp",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& ecodes = d.ecodes();
          switch (ecodes.data[1]) {
            case 1:
              return {std::format("{}", ecodes.data[0]), "who=picked"};
            case 2:
              return {std::format("{}", ecodes.data[0]), "who=total"};
            default:
              return {std::format("{}", ecodes.data[0]), "who=each"};
          }
        },
    },
    /* 91 */ {"drop_all_items", nullptr, dasm_no_args},
    /* 92 */ {
        "change_rect_ex",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [level, rect, level_type, chance_delta, action] = d.ecodes().data;
          const auto& [param1, param2, param3, param4, _] = d.ecodes(1).data;
          if (level_type < 0 || level_type > 1) {
            return {std::format("!(invalid level type: {})", level_type)};
          }
          vector<string> ret{std::format("{:c}RR{}/{}", level_type ? 'D' : 'L', level, rect)};
          if (chance_delta > 0) {
            ret.emplace_back(std::format("chance += {}/10000", chance_delta));
          } else if (chance_delta < 0) {
            ret.emplace_back(std::format("chance -= {}/10000", -chance_delta));
          }
          switch (action) {
            case -1:
              break;
            case 0:
              ret.emplace_back(std::format("set_rect=(left={}, top={}, right={}, bottom={})", param1, param3, param2, param4));
              break;
            case 1:
              ret.emplace_back(std::format("offset=(x={}, y={})", param1, param2));
              break;
            case 2:
              ret.emplace_back(std::format("delta=(left={}, top={}, right={}, bottom={})", param1, param3, param2, param4));
              break;
            default:
              ret.emplace_back(std::format("!(invalid action: {} {} {} {} {})", action, param1, param2, param3, param4));
          }
          return ret;
        },
    },
    /* 93 */ {"enable_compass", nullptr, dasm_no_args},
    /* 94 */ {"disable_compass", nullptr, dasm_no_args},
    /* 95 */ {
        "change_dir",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          switch (d.arg) {
            case -1:
              return {"random"};
            case 1:
              return {"north"};
            case 2:
              return {"east"};
            case 3:
              return {"south"};
            case 4:
              return {"west"};
            default:
              return {std::format("!(invalid direction: {})", d.arg)};
          }
        },
    },
    /* 96 */ {"disable_dungeon_map", nullptr, dasm_no_args},
    /* 97 */ {"enable_dungeon_map", nullptr, dasm_no_args},
    /* 98 */ {"require_registration", nullptr, dasm_no_args},
    /* 99 */ {"get_registration", nullptr, dasm_no_args},
    /* 100 */ {"end_battle", nullptr, dasm_no_args},
    /* 101 */ {"back_up", nullptr, dasm_no_args},
    /* 102 */ {"level_up_picked", nullptr, dasm_no_args},
    /* 103 */ {
        "cont_boat_camping",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [if_boat, if_camping, set_boat, _1, _2] = d.ecodes().data;
          vector<string> ret;
          if (if_boat == 1) {
            ret.emplace_back("if_not_in_boat=exit_ap");
          } else if (if_boat == 2) {
            ret.emplace_back("if_in_boat=exit_ap");
          }
          if (if_camping == 1) {
            ret.emplace_back("if_not_camping=exit_ap");
          } else if (if_camping == 2) {
            ret.emplace_back("if_camping=exit_ap");
          }
          if (set_boat == 1) {
            ret.emplace_back("set_boat");
          } else if (set_boat == 2) {
            ret.emplace_back("set_not_boat");
          }
          return ret;
        },
    },
    /* 104 */ {
        "enable_random_battles",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {d.arg ? "enable" : "disable"};
        },
    },
    /* 105 */ {
        "enable_allies_in_battle",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {d.arg ? "disable" : "enable"};
        },
    },
    /* 106 */ {
        "set_dark",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& ecodes = d.ecodes();
          vector<string> ret;
          if (ecodes.data[0] == 1) {
            ret.emplace_back("dark");
          } else if (ecodes.data[0] == 2) {
            ret.emplace_back("light");
          }
          if (ecodes.data[1]) {
            ret.emplace_back("skip_if_dark_unchanged");
          }
          // Divinity documentation says this opcode can change LOS status too, but that isn't true
          return ret;
        },
    },
    /* 107 */ {
        "pick_battle_2",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [low, high, sound, str_id, loss_xap] = d.ecodes().data;
          vector<string> ret = dasm_battle_list(low, high);
          if (sound) {
            ret.emplace_back(std::format("sound=SND{}", sound));
          }
          if (str_id) {
            ret.emplace_back(std::format("string={}", d.render_string_reference(str_id)));
          }
          ret.emplace_back(std::format("on_loss=XAP{}", loss_xap));
          return ret;
        },
    },
    /* 108 */ {
        "change_picked_chars",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& ecodes = d.ecodes();
          int16_t delta = ecodes.data[1];
          string delta_str = std::format(" {:c}= {}", (delta < 0) ? '-' : '+', abs(delta));
          switch (ecodes.data[0]) {
            case 1:
              return {"attacks/round" + delta_str};
            case 2:
              return {"spells/round" + delta_str};
            case 3:
              return {"movement" + delta_str};
            case 4:
              return {"damage" + delta_str};
            case 5:
              return {"spell points" + delta_str};
            case 6:
              return {"hand-to-hand damage" + delta_str};
            case 7:
              return {"stamina" + delta_str};
            case 8:
              return {"armor rating" + delta_str};
            case 9:
              return {"chance to hit" + delta_str};
            case 10:
              return {"missile adjust" + delta_str};
            case 11:
              return {"magic resistance" + delta_str};
            case 12:
              return {"prestige" + delta_str};
            default:
              return {std::format("!(invalid action: {}){}", ecodes.data[0], delta_str)};
          }
        },
    },
    /* 109 */ invalid_opcode_def,
    /* 110 */ invalid_opcode_def,
    /* 111 */ {"ret", nullptr, dasm_no_args},
    /* 112 */ {"pop", nullptr, dasm_no_args},
    /* 113 */ invalid_opcode_def,
    /* 114 */ invalid_opcode_def,
    /* 115 */ invalid_opcode_def,
    /* 116 */ invalid_opcode_def,
    /* 117 */ invalid_opcode_def,
    /* 118 */ invalid_opcode_def,
    /* 119 */ {"revive_npc_after", nullptr, dasm_no_args},
    /* 120 */ {
        "change_monster",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [type, mst_id, count, new_icon, new_traitor] = d.ecodes().data;
          if (type < 1 || type > 2) {
            return {std::format("!(invalid type: {})", type)};
          }
          vector<string> ret{
              (type == 2) ? "(monster)" : "(NPC)",
              std::format("MST{}", mst_id),
              std::format("count={}", count),
          };
          if (new_icon != -1) {
            ret.emplace_back(std::format("icon={}", new_icon));
          }
          if (new_traitor != -1) {
            ret.emplace_back(std::format("traitor={}", new_traitor));
          }
          return ret;
        },
    },
    /* 121 */ {"kill_lower_undead", nullptr, dasm_no_args},
    /* 122 */ {
        "fumble_weapon",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& ecodes = d.ecodes();
          vector<string> ret;
          if (ecodes.data[0]) {
            ret.emplace_back(std::format("string={}", d.render_string_reference(ecodes.data[0])));
          }
          if (ecodes.data[1]) {
            ret.emplace_back(std::format("sound=SND{}", ecodes.data[1]));
          }
          return ret;
        },
    },
    /* 123 */ {
        "rout_monsters",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& ecodes = d.ecodes();
          vector<string> ret;
          for (size_t z = 0; z < 5; z++) {
            if (ecodes.data[z]) {
              ret.emplace_back(std::format("MST{}", ecodes.data[z]));
            }
          }
          return ret;
        },
    },
    /* 124 */ {
        "summon_monsters",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [_1, mst_id, count, sound, traitor] = d.ecodes().data;
          vector<string> ret{
              std::format("MST{}", mst_id),
              (count < 0) ? std::format("count=rand(1, {})", -count) : std::format("count={}", count),
          };
          if (sound) {
            ret.emplace_back(std::format("sound=SND{}", sound));
          }
          if (traitor == 0) {
            ret.emplace_back("traitor=default");
          } else {
            ret.emplace_back(std::format("traitor={}", traitor));
          }
          return ret;
        },
    },
    /* 125 */ {
        "destroy_related",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [mst_id, count, _1, _2, ignore_traitor] = d.ecodes().data;
          vector<string> ret{std::format("MST{}", mst_id), std::format("count={}", count)};
          if (ignore_traitor) {
            ret.emplace_back("ignore_traitor");
          }
          return ret;
        },
    },
    /* 126 */ {
        "macro_criteria",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          const auto& [when, param, repeat_mode, low, high] = d.ecodes().data;
          vector<string> ret;
          switch (when) {
            case 0:
              ret.emplace_back(std::format("after round {}", param));
              break;
            case 1:
              ret.emplace_back(std::format("{}% chance", param));
              break;
            case 2:
              ret.emplace_back("on flee or fail");
              break;
            default:
              ret.emplace_back(std::format("!(invalid when: {} {})", when, param));
          }
          switch (repeat_mode) {
            case 0:
              ret.emplace_back("no_repeat");
              break;
            case 1:
              ret.emplace_back("repeat_each_round");
              break;
            case 2:
              ret.emplace_back("jmp_random");
              break;
            default:
              ret.emplace_back(std::format("!(invalid repeat mode: {})", repeat_mode));
          }
          if (repeat_mode == 2) {
            if (low > high) {
              ret.emplace_back(std::format("!(invalid range: {} {})", low, high));
            } else {
              for (int32_t z = low; z < high; z++) {
                ret.emplace_back(std::format("XAP{}", z));
              }
            }
          } else {
            ret.emplace_back(std::format("XAP{}", low));
          }
          return ret;
        },
    },
    /* 127 */ {
        "cont_monster_present",
        nullptr,
        [](const DisassemblyContext& d) -> std::vector<std::string> {
          return {std::format("MST{}", d.arg)};
        },
    },
};

string RealmzScenarioData::disassemble_opcode(int16_t opcode, int16_t arg, const DisassemblyOrigin& origin) const {
  size_t def_index = abs(opcode);
  const OpcodeDefinition* def = (def_index >= opcode_defs.size()) ? &invalid_opcode_def : &opcode_defs[def_index];

  const char* name = (opcode < 0) ? def->negative_name : def->name;
  if (!name) {
    name = ".invalid3";
    def = &invalid_opcode_def;
  }

  string data_str = std::format("{:04X} {:04X}", static_cast<uint16_t>(opcode), static_cast<uint16_t>(arg));
  vector<string> arg_tokens;
  DisassemblyContext d{.scen = *this, .origin = origin, .opcode = opcode, .arg = arg, .used_ecodes = {}};
  try {
    arg_tokens = def->dasm_args(d);
  } catch (const std::exception& e) {
    arg_tokens = {std::format("!(failed: {})", e.what())};
  }
  for (ssize_t index : d.used_ecodes) {
    const auto& ecodes = this->ecodes[index];
    data_str += std::format(" {:04X}:[{:04X} {:04X} {:04X} {:04X} {:04X}]",
        index, static_cast<uint16_t>(ecodes.data[0]), static_cast<uint16_t>(ecodes.data[1]),
        static_cast<uint16_t>(ecodes.data[2]), static_cast<uint16_t>(ecodes.data[3]),
        static_cast<uint16_t>(ecodes.data[4]));
  }

  if (arg_tokens.empty()) {
    return std::format("{:<41} {:<24}", data_str, name);
  } else {
    return std::format("{:<41} {:<24} {}", data_str, name, phosg::join(arg_tokens, ", "));
  }
}

string RealmzScenarioData::disassemble_xap(int16_t ap_num) const {
  const auto& ap = this->xaps.at(ap_num);

  string data = std::format("===== XAP id={} [XAP{}]\n", ap_num, ap_num);

  // TODO: eliminate code duplication here
  for (size_t x = 0; x < land_metadata.size(); x++) {
    for (size_t y = 0; y < land_metadata[x].random_rects.size(); y++) {
      const auto& r = land_metadata[x].random_rects[y];
      for (size_t z = 0; z < 3; z++) {
        if (r.xap_refs[z].xap_num == ap_num) {
          data += std::format("RANDOM RECTANGLE REFERENCE land_level={} rect_num={} start_coord={},{} end_coord={},{} [LRR{}/{} #{} {}%]\n",
              x, y, r.left, r.top, r.right, r.bottom, x, y, z, r.xap_refs[z].chance);
        }
      }
    }
  }
  for (size_t x = 0; x < dungeon_metadata.size(); x++) {
    for (size_t y = 0; y < dungeon_metadata[x].random_rects.size(); y++) {
      const auto& r = dungeon_metadata[x].random_rects[y];
      for (size_t z = 0; z < 3; z++) {
        if (r.xap_refs[z].xap_num == ap_num) {
          data += std::format("RANDOM RECTANGLE REFERENCE dungeon_level={} rect_num={} start_coord={},{} end_coord={},{} [DRR{}/{} #{} {}%]\n",
              x, y, r.left, r.top, r.right, r.bottom, x, y, z, r.xap_refs[z].chance);
        }
      }
    }
  }

  DisassemblyOrigin origin{.type = DisassemblyOrigin::Type::XAP, .level_num = -1, .ap_num = ap_num};
  for (size_t x = 0; x < 8; x++) {
    if (ap.command_codes[x] || ap.argument_codes[x]) {
      string dasm = this->disassemble_opcode(ap.command_codes[x], ap.argument_codes[x], origin);
      data += std::format("  {}\n", dasm);
    }
  }

  return data;
}

string RealmzScenarioData::disassemble_all_xaps() const {
  deque<string> blocks;
  for (size_t x = 0; x < this->xaps.size(); x++) {
    blocks.emplace_back(this->disassemble_xap(x));
  }
  return join(blocks, "");
}

string RealmzScenarioData::disassemble_level_ap(
    const APInfo& ap, int16_t level_num, int16_t ap_num, bool dungeon) const {
  if (ap.get_x() < 0 || ap.get_y() < 0) {
    return "";
  }

  string extra;
  if (ap.to_level != level_num || ap.to_x != ap.get_x() || ap.to_y != ap.get_y()) {
    extra = std::format(" to_level={} to_x={} to_y={}", ap.to_level, ap.to_x, ap.to_y);
  }
  if (ap.percent_chance != 100) {
    extra += std::format(" prob={}", ap.percent_chance);
  }
  string data = std::format("===== {} AP level={} id={} x={} y={}{} [{}AP{}/{}]\n",
      (dungeon ? "DUNGEON" : "LAND"), level_num, ap_num, ap.get_x(), ap.get_y(),
      extra, (dungeon ? 'D' : 'L'), level_num, ap_num);

  DisassemblyOrigin origin{
      .type = dungeon ? DisassemblyOrigin::Type::DUNGEON_AP : DisassemblyOrigin::Type::LAND_AP,
      .level_num = level_num,
      .ap_num = ap_num};
  for (size_t x = 0; x < 8; x++) {
    if (ap.command_codes[x] || ap.argument_codes[x]) {
      string dasm = this->disassemble_opcode(ap.command_codes[x], ap.argument_codes[x], origin);
      data += std::format("  {}\n", dasm);
    }
  }

  return data;
}

string RealmzScenarioData::disassemble_level_rr(
    const RandomRect& rr, int16_t level_num, int16_t rr_num, bool dungeon) const {
  return std::format("\
===== {} RANDOM RECTANGLE level={} id={} x1={} y1={} x2={} y2={} chance={}/10000 [{}RR{}/{}]\n\
  battle_range = [{}, {}], option_chance = {}%, sound = {}, text = {}\n\
  xap1 = XAP{} @ {}% ({})\n\
  xap2 = XAP{} @ {}% ({})\n\
  xap3 = XAP{} @ {}% ({})\n\
",
      (dungeon ? "DUNGEON" : "LAND"), level_num, rr_num, rr.left, rr.top, rr.right, rr.bottom, rr.times_in_10k, (dungeon ? 'D' : 'L'), level_num, rr_num,
      rr.battle_low, rr.battle_high, rr.percent_option, rr.sound, render_string_reference(this->strings, rr.text),
      rr.xap_refs[0].xap_num, abs(rr.xap_refs[0].chance), (rr.xap_refs[0].chance < 0) ? "repeatable" : "one-time",
      rr.xap_refs[1].xap_num, abs(rr.xap_refs[1].chance), (rr.xap_refs[1].chance < 0) ? "repeatable" : "one-time",
      rr.xap_refs[2].xap_num, abs(rr.xap_refs[2].chance), (rr.xap_refs[2].chance < 0) ? "repeatable" : "one-time");
}

string RealmzScenarioData::disassemble_level_aps(int16_t level_num, bool dungeon) const {
  string ret;
  const auto& index = (dungeon ? this->dungeon_aps : this->land_aps);
  size_t count = index.at(level_num).size();
  for (size_t x = 0; x < count; x++) {
    ret += this->disassemble_level_ap(index[level_num][x], level_num, x, dungeon);
  }
  return ret;
}

string RealmzScenarioData::disassemble_level_rrs(int16_t level_num, bool dungeon) const {
  string ret;
  const auto& metadata = (dungeon ? this->dungeon_metadata : this->land_metadata);
  size_t count = metadata.at(level_num).random_rects.size();
  for (size_t x = 0; x < count; x++) {
    ret += this->disassemble_level_rr(metadata[level_num].random_rects[x], level_num, x, dungeon);
  }
  return ret;
}

string RealmzScenarioData::disassemble_all_level_aps_and_rrs(bool dungeon) const {
  deque<string> blocks;
  size_t count = (dungeon ? this->dungeon_aps : this->land_aps).size();
  for (size_t x = 0; x < count; x++) {
    blocks.emplace_back(this->disassemble_level_aps(x, dungeon));
    blocks.emplace_back(this->disassemble_level_rrs(x, dungeon));
  }
  return join(blocks, "");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA DL

static uint16_t location_sig(uint8_t x, uint8_t y) {
  return (static_cast<uint16_t>(x) << 8) | y;
}

void RealmzScenarioData::MapData::transpose() {
  for (size_t y = 0; y < 90; y++) {
    for (size_t x = y + 1; x < 90; x++) {
      int16_t t = this->data[y][x];
      this->data[y][x] = this->data[x][y];
      this->data[x][y] = t;
    }
  }
}

vector<RealmzScenarioData::MapData> RealmzScenarioData::load_dungeon_map_index(const string& filename) {
  return load_vector_file<MapData>(filename);
}

string RealmzScenarioData::generate_dungeon_map_json(int16_t level_num) const {
  const auto& mdata = this->dungeon_maps.at(level_num);
  deque<string> lines;
  lines.emplace_back("[");
  for (ssize_t y = 0; y < 90; y++) {
    string line;
    for (ssize_t x = 0; x < 90; x++) {
      line += std::format("{:4},", mdata.data[y][x]);
    }
    lines.emplace_back(std::move(line));
  }
  lines.emplace_back("]");
  return join(lines, "\n");
}

ImageRGB888 RealmzScenarioData::generate_dungeon_map(
    int16_t level_num, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h, bool show_random_rects) const {
  const auto& mdata = this->dungeon_maps.at(level_num);
  const auto& metadata = this->dungeon_metadata.at(level_num);
  const auto& aps = this->dungeon_aps.at(level_num);

  constexpr uint16_t wall_tile_flag = 0x0001;
  constexpr uint16_t vert_door_tile_flag = 0x0002;
  constexpr uint16_t horiz_door_tile_flag = 0x0004;
  constexpr uint16_t stairs_tile_flag = 0x0008;
  constexpr uint16_t columns_tile_flag = 0x0010;
  // constexpr uint16_t unmapped_tile_flag = 0x0080;
  constexpr uint16_t secret_up_tile_flag = 0x0100;
  constexpr uint16_t secret_right_tile_flag = 0x0200;
  constexpr uint16_t secret_down_tile_flag = 0x0400;
  constexpr uint16_t secret_left_tile_flag = 0x0800;
  constexpr uint16_t has_ap_tile_flag = 0x1000;
  constexpr uint16_t battle_blank_tile_flag = 0x4000;

  if ((x0 >= 90) || (y0 >= 90) || ((x0 + w) > 90) || ((y0 + h) > 90)) {
    throw runtime_error("map bounds out of range");
  }

  ImageRGB888 map(w * 32, h * 32);
  size_t pattern_x = 576, pattern_y = 320;

  unordered_map<uint16_t, vector<int>> loc_to_ap_nums;
  for (size_t x = 0; x < aps.size(); x++) {
    loc_to_ap_nums[location_sig(aps[x].get_x(), aps[x].get_y())].push_back(x);
  }

  ImageRGBA8888N dungeon_pattern = this->global.global_rsf.decode_PICT(302).image;

  for (ssize_t y = y0 + h - 1; y >= y0; y--) {
    for (ssize_t x = x0 + w - 1; x >= x0; x--) {
      int16_t data = mdata.data[y][x];

      size_t xp = (x - x0) * 32;
      size_t yp = (y - y0) * 32;
      map.write_rect(xp, yp, 32, 32, 0x000000FF);
      if (data & wall_tile_flag) {
        map.copy_from_with_source_color_mask(dungeon_pattern, xp, yp, 32, 32, pattern_x + 0, pattern_y + 0, 16, 16, 0xFFFFFFFF, phosg::ResizeMode::NEAREST_NEIGHBOR);
      }
      if (data & vert_door_tile_flag) {
        map.copy_from_with_source_color_mask(dungeon_pattern, xp, yp, 32, 32, pattern_x + 16, pattern_y + 0, 16, 16, 0xFFFFFFFF, phosg::ResizeMode::NEAREST_NEIGHBOR);
      }
      if (data & horiz_door_tile_flag) {
        map.copy_from_with_source_color_mask(dungeon_pattern, xp, yp, 32, 32, pattern_x + 32, pattern_y + 0, 16, 16, 0xFFFFFFFF, phosg::ResizeMode::NEAREST_NEIGHBOR);
      }
      if (data & stairs_tile_flag) {
        map.copy_from_with_source_color_mask(dungeon_pattern, xp, yp, 32, 32, pattern_x + 48, pattern_y + 0, 16, 16, 0xFFFFFFFF, phosg::ResizeMode::NEAREST_NEIGHBOR);
      }
      if (data & columns_tile_flag) {
        map.copy_from_with_source_color_mask(dungeon_pattern, xp, yp, 32, 32, pattern_x + 0, pattern_y + 16, 16, 16, 0xFFFFFFFF, phosg::ResizeMode::NEAREST_NEIGHBOR);
      }
      if (data & secret_up_tile_flag) {
        map.copy_from_with_source_color_mask(dungeon_pattern, xp, yp, 32, 32, pattern_x + 0, pattern_y + 32, 16, 16, 0xFFFFFFFF, phosg::ResizeMode::NEAREST_NEIGHBOR);
      }
      if (data & secret_right_tile_flag) {
        map.copy_from_with_source_color_mask(dungeon_pattern, xp, yp, 32, 32, pattern_x + 16, pattern_y + 32, 16, 16, 0xFFFFFFFF, phosg::ResizeMode::NEAREST_NEIGHBOR);
      }
      if (data & secret_down_tile_flag) {
        map.copy_from_with_source_color_mask(dungeon_pattern, xp, yp, 32, 32, pattern_x + 32, pattern_y + 32, 16, 16, 0xFFFFFFFF, phosg::ResizeMode::NEAREST_NEIGHBOR);
      }
      if (data & secret_left_tile_flag) {
        map.copy_from_with_source_color_mask(dungeon_pattern, xp, yp, 32, 32, pattern_x + 48, pattern_y + 32, 16, 16, 0xFFFFFFFF, phosg::ResizeMode::NEAREST_NEIGHBOR);
      }

      if (data & has_ap_tile_flag) {
        map.draw_horizontal_line(xp, xp + 31, yp, 0, 0xFF0000FF);
        map.draw_horizontal_line(xp, xp + 31, yp + 31, 0, 0xFF0000FF);
        map.draw_vertical_line(xp, yp, yp + 31, 0, 0xFF0000FF);
        map.draw_vertical_line(xp + 31, yp, yp + 31, 0, 0xFF0000FF);
      }
      if (data & battle_blank_tile_flag) {
        map.draw_horizontal_line(xp, xp + 31, yp + 15, 0, 0x00FFFFFF);
        map.draw_horizontal_line(xp, xp + 31, yp + 16, 0, 0x00FFFFFF);
        map.draw_vertical_line(xp + 15, yp, yp + 31, 0, 0x00FFFFFF);
        map.draw_vertical_line(xp + 16, yp, yp + 31, 0, 0x00FFFFFF);
      }

      size_t text_xp = xp + 1;
      size_t text_yp = yp + 1;

      // Draw the coords if both are multiples of 10
      if (y % 10 == 0 && x % 10 == 0) {
        map.draw_text(text_xp, text_yp, 0xFF00FFFF, 0x00000080, "{},{}", x, y);
        text_yp += 8;
      }

      for (const auto& ap_num : loc_to_ap_nums[location_sig(x, y)]) {
        if (aps[ap_num].percent_chance < 100) {
          map.draw_text(text_xp, text_yp, 0xFFFFFFFF, 0x00000080, "{}/{}-{}%", level_num, ap_num, aps[ap_num].percent_chance);
        } else {
          map.draw_text(text_xp, text_yp, 0xFFFFFFFF, 0x00000080, "{}/{}", level_num, ap_num);
        }
        text_yp += 8;
      }
    }
  }

  if (show_random_rects) {
    draw_random_rects(map, metadata.random_rects, 0, 0, true, level_num, x0, y0, w, h);
  }

  return map;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA LD

vector<RealmzScenarioData::MapData> RealmzScenarioData::load_land_map_index(const string& filename) {
  // Format is the same as for dungeons, except it's in column-major order
  vector<MapData> data = load_dungeon_map_index(filename);
  for (auto& m : data) {
    m.transpose();
  }

  return data;
}

unordered_set<string> RealmzScenarioData::all_land_types() const {
  unordered_set<string> all;
  for (const auto& it : this->land_type_to_tileset_definition) {
    all.emplace(it.first);
  }
  for (const auto& it : this->global.land_type_to_tileset_definition) {
    all.emplace(it.first);
  }
  return all;
}

string RealmzScenarioData::generate_land_map_json(int16_t level_num) const {
  const auto& mdata = this->land_maps.at(level_num);
  deque<string> lines;
  lines.emplace_back("[");
  for (ssize_t y = 0; y < 90; y++) {
    string line;
    for (ssize_t x = 0; x < 90; x++) {
      line += std::format("{:4},", mdata.data[y][x]);
    }
    lines.emplace_back(std::move(line));
  }
  lines.emplace_back("]");
  return join(lines, "\n");
}

ImageRGB888 RealmzScenarioData::generate_land_map(
    int16_t level_num,
    uint8_t x0,
    uint8_t y0,
    uint8_t w,
    uint8_t h,
    bool show_random_rects,
    int16_t party_x,
    int16_t party_y,
    const MapData* save_file_map_data,
    const MapMetadata* save_file_map_metadata,
    const APInfo* save_file_aps, // Should be [100]
    const uint8_t* los_revealed, // Should be [90 * 90]
    unordered_set<int16_t>* used_negative_tiles,
    unordered_map<string, unordered_set<uint8_t>>* used_positive_tiles) const {
  const auto& mdata = save_file_map_data ? *save_file_map_data : this->land_maps.at(level_num);
  const auto& metadata = save_file_map_metadata ? *save_file_map_metadata : this->land_metadata.at(level_num);

  struct TileData {
    int16_t field_value;
    bool has_ap = false;
    bool ap_is_secret = false;
    bool ap_is_undiscovered_secret = false;
    bool has_note = false;
    bool path_discovered = false;
    int16_t tile_id;

    TileData(int16_t field_value) : field_value(field_value), tile_id(field_value) {
      if (this->tile_id >= 0) {
        this->has_note = (this->tile_id & 0x4000);
        this->path_discovered = (this->tile_id & 0x2000);
        this->tile_id &= 0x1FFF;
        this->has_ap = (this->tile_id >= 1000);
        this->ap_is_secret = (this->tile_id >= 2000);
        this->ap_is_undiscovered_secret = (this->tile_id >= 3000);
        this->tile_id %= 1000;
      } else {
        this->has_ap = (this->tile_id <= -1000);
        this->ap_is_secret = (this->tile_id <= -2000);
        this->ap_is_undiscovered_secret = (this->tile_id <= -3000);
        this->tile_id %= 1000;
      }
    }
  };

  auto get_ap = [&](size_t z) -> const APInfo* {
    if (save_file_aps) {
      return (z < 100) ? &save_file_aps[z] : nullptr;
    } else {
      const auto& aps = this->land_aps.at(level_num);
      return (z < aps.size()) ? &aps[z] : nullptr;
    }
  };

  unordered_set<uint8_t>* used_positive_tiles_for_land_type = nullptr;
  if (used_positive_tiles) {
    used_positive_tiles_for_land_type = &(*used_positive_tiles)[metadata.land_type];
  }

  LevelNeighbors n;
  if (x0 == 0 && y0 == 0 && w == 90 && h == 90) {
    try {
      n = this->layout.get_level_neighbors(level_num);
    } catch (const runtime_error& e) {
      fwrite_fmt(stderr, "warning: can\'t get neighbors for level ({})\n", e.what());
    }
  }

  int16_t start_x = -1, start_y = -1;
  if (level_num == this->scenario_metadata.start_level) {
    start_x = this->scenario_metadata.start_x;
    start_y = this->scenario_metadata.start_y;
  }

  if ((x0 >= 90) || (y0 >= 90) || ((x0 + w) > 90) || ((y0 + h) > 90)) {
    throw runtime_error("map bounds out of range");
  }

  unordered_map<uint16_t, vector<int>> loc_to_ap_nums;
  for (size_t x = 0; x < 100; x++) {
    const auto* ap = get_ap(x);
    if (ap) {
      loc_to_ap_nums[location_sig(ap->get_x(), ap->get_y())].push_back(x);
    }
  }

  size_t horizontal_neighbors = (n.left != -1 ? 1 : 0) + (n.right != -1 ? 1 : 0);
  size_t vertical_neighbors = (n.top != -1 ? 1 : 0) + (n.bottom != -1 ? 1 : 0);

  const RealmzGlobalData::TileSetDefinition* tileset;
  try {
    tileset = &this->land_type_to_tileset_definition.at(metadata.land_type);
  } catch (const out_of_range&) {
    tileset = &this->global.land_type_to_tileset_definition.at(metadata.land_type);
  }

  ImageRGB888 map(w * 32 + horizontal_neighbors * 9, h * 32 + vertical_neighbors * 9);

  // Write neighbor directory
  if (n.left != -1) {
    string text = std::format("TO LEVEL {}", n.left);
    for (size_t y = (n.top != -1 ? 10 : 1); y < h * 32; y += 10 * 32) {
      for (size_t yy = 0; yy < text.size(); yy++) {
        map.draw_text(2, y + 9 * yy, 0xFFFFFFFF, 0x000000FF, "{}", text[yy]);
      }
    }
  }
  if (n.right != -1) {
    string text = std::format("TO LEVEL {}", n.right);
    size_t x = 32 * 90 + (n.left != -1 ? 11 : 2);
    for (size_t y = (n.top != -1 ? 10 : 1); y < h * 32; y += 10 * 32) {
      for (size_t yy = 0; yy < text.size(); yy++) {
        map.draw_text(x, y + 9 * yy, 0xFFFFFFFF, 0x000000FF, "{}", text[yy]);
      }
    }
  }
  if (n.top != -1) {
    string text = std::format("TO LEVEL {}", n.top);
    for (size_t x = (n.left != -1 ? 10 : 1); x < w * 32; x += 10 * 32) {
      map.draw_text(x, 1, 0xFFFFFFFF, 0x000000FF, "{}", text);
    }
  }
  if (n.bottom != -1) {
    string text = std::format("TO LEVEL {}", n.bottom);
    size_t y = 32 * 90 + (n.top != -1 ? 10 : 1);
    for (size_t x = (n.left != -1 ? 10 : 1); x < w * 32; x += 10 * 32) {
      map.draw_text(x, y, 0xFFFFFFFF, 0x000000FF, "{}", text);
    }
  }

  // Load the positive pattern
  int16_t resource_id = RealmzGlobalData::pict_resource_id_for_land_type(metadata.land_type);
  auto positive_pattern = this->scenario_rsf.resource_exists(RESOURCE_TYPE_PICT, resource_id)
      ? std::move(this->scenario_rsf.decode_PICT(resource_id).image)
      : std::move(this->global.global_rsf.decode_PICT(resource_id).image);

  for (size_t y = y0; y < y0 + h; y++) {
    for (size_t x = x0; x < x0 + w; x++) {
      TileData data{mdata.data[y][x]};
      size_t xp = (x - x0) * 32 + (n.left != -1 ? 9 : 0);
      size_t yp = (y - y0) * 32 + (n.top != -1 ? 9 : 0);

      // Draw the tile itself
      if ((data.tile_id < 0) || (data.tile_id > 200)) { // Masked tile
        if (used_negative_tiles) {
          used_negative_tiles->emplace(data.tile_id);
        }

        ImageRGBA8888N cicn;
        if (this->scenario_rsf.resource_exists(RESOURCE_TYPE_cicn, data.tile_id)) {
          cicn = this->scenario_rsf.decode_cicn(data.tile_id).image;
        } else if (this->global.global_rsf.resource_exists(RESOURCE_TYPE_cicn, data.tile_id)) {
          cicn = this->global.global_rsf.decode_cicn(data.tile_id).image;
        }

        // If neither cicn was valid, draw an error tile
        if (cicn.get_width() == 0 || cicn.get_height() == 0) {
          map.write_rect(xp, yp, 32, 32, 0x000000FF);
          map.draw_text(xp + 2, yp + 30 - 9, 0xFFFFFFFF, 0x000000FF, "{:04X}", data.field_value);

        } else {
          if (tileset->base_tile_id) {
            size_t source_id = tileset->base_tile_id - 1;
            size_t sxp = (source_id % 20) * 32;
            size_t syp = (source_id / 20) * 32;
            map.copy_from(positive_pattern, xp, yp, 32, 32, sxp, syp);
          } else {
            map.write_rect(xp, yp, 32, 32, 0x000000FF);
          }

          // Negative tile images may be >32px in either dimension, and are anchored at the lower-right corner, so we
          // have to adjust the destination x/y appropriately
          map.copy_from_with_blend(
              cicn, xp - (cicn.get_width() - 32), yp - (cicn.get_height() - 32), cicn.get_width(), cicn.get_height(), 0, 0);
        }

      } else if (data.tile_id <= 200) { // Standard tile
        if (used_positive_tiles_for_land_type) {
          used_positive_tiles_for_land_type->emplace(data.tile_id);
        }

        size_t source_id = data.tile_id - 1;
        size_t sxp = (source_id % 20) * 32;
        size_t syp = (source_id / 20) * 32;
        map.copy_from(positive_pattern, xp, yp, 32, 32, sxp, syp);

        // If it's a path, shade it red; if it's a discovered path, shade it yellow
        if (data.path_discovered) {
          map.blend_rect(xp, yp, 32, 32, 0xFFFF0040);
        } else if (tileset->tiles[data.tile_id].is_path) {
          map.blend_rect(xp, yp, 32, 32, 0xFF000040);
        }
      }

      // If there's LOS data, darken the tile if it's not revealed
      if (metadata.use_los && los_revealed && !los_revealed[(y * 90) + x]) {
        map.blend_rect(xp, yp, 32, 32, 0x00000080);
      }
    }
  }

  // This is a separate loop so we can draw APs that are hidden by large negative tile overlays
  for (size_t y = y0; y < y0 + h; y++) {
    for (size_t x = x0; x < x0 + w; x++) {

      size_t xp = (x - x0) * 32 + (n.left != -1 ? 9 : 0);
      size_t yp = (y - y0) * 32 + (n.top != -1 ? 9 : 0);

      TileData data{mdata.data[y][x]};
      size_t text_xp = xp + 2;
      size_t text_yp = yp + 2;

      // Draw a red border if it has an AP, and make it dashed if the AP is secret
      if (data.has_ap && data.ap_is_secret && !data.ap_is_undiscovered_secret) {
        map.draw_horizontal_line(xp, xp + 31, yp, 0, 0xFFFF00FF);
        map.draw_horizontal_line(xp, xp + 31, yp + 31, 0, 0xFFFF00FF);
        map.draw_vertical_line(xp, yp, yp + 31, 0, 0xFFFF00FF);
        map.draw_vertical_line(xp + 31, yp, yp + 31, 0, 0xFFFF00FF);
        map.draw_horizontal_line(xp, xp + 31, yp, 4, 0xFF0000FF);
        map.draw_horizontal_line(xp, xp + 31, yp + 31, 4, 0xFF0000FF);
        map.draw_vertical_line(xp, yp, yp + 31, 4, 0xFF0000FF);
        map.draw_vertical_line(xp + 31, yp, yp + 31, 4, 0xFF0000FF);
      } else if (data.has_ap && data.ap_is_secret) {
        map.draw_horizontal_line(xp, xp + 31, yp, 4, 0xFF0000FF);
        map.draw_horizontal_line(xp, xp + 31, yp + 31, 4, 0xFF0000FF);
        map.draw_vertical_line(xp, yp, yp + 31, 4, 0xFF0000FF);
        map.draw_vertical_line(xp + 31, yp, yp + 31, 4, 0xFF0000FF);
      } else if (data.has_ap) {
        map.draw_horizontal_line(xp, xp + 31, yp, 0, 0xFF0000FF);
        map.draw_horizontal_line(xp, xp + 31, yp + 31, 0, 0xFF0000FF);
        map.draw_vertical_line(xp, yp, yp + 31, 0, 0xFF0000FF);
        map.draw_vertical_line(xp + 31, yp, yp + 31, 0, 0xFF0000FF);
      }

      // Draw the coords if both are multiples of 10
      if (y % 10 == 0 && x % 10 == 0) {
        map.draw_text(text_xp, text_yp, 0xFF00FFFF, 0x00000080, "{},{}", x, y);
        text_yp += 8;
      }

      // Draw "START" if this is the start loc
      if (x == static_cast<size_t>(start_x) && y == static_cast<size_t>(start_y)) {
        map.draw_text(text_xp, text_yp, 0x00FFFFFF, 0x00000080, "START");
        text_yp += 8;
      }
      // Draw "PARTY" if this is the party loc
      if (x == static_cast<size_t>(party_x) && y == static_cast<size_t>(party_y)) {
        map.draw_text(text_xp, text_yp, 0x00FFFFFF, 0x00000080, "PARTY");
        text_yp += 8;
      }

      // Draw APs if present
      for (const auto& ap_num : loc_to_ap_nums[location_sig(x, y)]) {
        const auto* ap = get_ap(ap_num);
        if (!ap) {
          throw std::logic_error("attempted to draw indexed AP but it was not valid");
        }
        if (ap->percent_chance < 100) {
          map.draw_text(text_xp, text_yp, 0xFFFFFFFF, 0x00000080, "{}/{}-{}%", level_num, ap_num, ap->percent_chance);
        } else {
          map.draw_text(text_xp, text_yp, 0xFFFFFFFF, 0x00000080, "{}/{}", level_num, ap_num);
        }
        text_yp += 8;
      }
    }
  }

  if (show_random_rects) {
    draw_random_rects(map, metadata.random_rects, (n.left != -1 ? 9 : 0),
        (n.top != -1 ? 9 : 0), false, level_num, x0, y0, w, h);
  }

  return map;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA SD2

template <size_t FieldSize>
vector<string> load_fixed_size_string_index(const string& filename) {
  try {
    string data = load_file(filename);
    StringReader r(data);

    vector<string> ret;
    while (!r.eof()) {
      size_t size = min<size_t>(r.get_u8(), FieldSize);
      ret.emplace_back(r.read(size));
      if (!r.eof()) {
        r.skip(min<size_t>(r.remaining(), FieldSize - size));
      }
    }
    return ret;
  } catch (const cannot_open_file&) {
    return {};
  }
}

vector<string> RealmzScenarioData::load_string_index(const string& filename) {
  return load_fixed_size_string_index<0xFF>(filename);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA SOLIDS

vector<bool> RealmzScenarioData::load_solids(const string& filename) {
  vector<bool> ret;
  for (uint8_t z : load_vector_file<uint8_t>(filename)) {
    ret.emplace_back(!!z);
  }
  return ret;
}

string RealmzScenarioData::disassemble_solids() const {
  if (this->solids.empty()) {
    return "";
  }

  BlockStringWriter w;
  w.write_fmt("===== NEGATIVE TILE PROPERTIES");
  for (int32_t z = 0; z < static_cast<int32_t>(this->solids.size()); z++) {
    w.write_fmt("  [{}] {}", static_cast<int32_t>(-1 - z), this->solids[z] ? "solid" : "non-solid");
  }
  w.write("");
  return w.close("\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA OD

vector<string> RealmzScenarioData::load_option_string_index(const string& filename) {
  return load_fixed_size_string_index<0x18>(filename);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA MD

vector<RealmzScenarioData::MonsterDefinition> RealmzScenarioData::load_monster_index(const string& filename) {
  return load_vector_file<MonsterDefinition>(filename);
}

string RealmzScenarioData::disassemble_monster(size_t index) const {
  return std::format(
      "===== MONSTER id={} [MST{}]\n{}\n", index, index, this->disassemble_monster(this->monsters.at(index), 1));
}

string RealmzScenarioData::disassemble_monster(const MonsterDefinition& m, size_t indent_level) const {
  BlockStringWriter w;

  string indent_str;
  indent_str.resize(2 * indent_level, ' ');
  w.write_fmt("{}stamina={} bonus={}", indent_str, m.stamina, m.bonus_stamina);
  w.write_fmt("agility={}", m.agility);
  if (m.description_index < this->monster_descriptions.size()) {
    string desc = escape_quotes(this->monster_descriptions[m.description_index]);
    w.write_fmt("description=\"{}\"#{}", escape_quotes(desc), m.description_index);
  } else {
    w.write_fmt("description=#{} (out of range)", m.description_index);
  }
  w.write_fmt("movement={}", m.movement);
  w.write_fmt("armor_rating={}", m.armor_rating);
  w.write_fmt("magic_resistance={}", m.magic_resistance);
  if (m.required_weapon_id == -1) {
    w.write_fmt("required_weapon=BLUNT");
  } else if (m.required_weapon_id == -2) {
    w.write_fmt("required_weapon=SHARP");
  } else if (m.required_weapon_id == 0) {
    w.write_fmt("required_weapon=(any)");
  } else {
    w.write_fmt("required_weapon={}", m.required_weapon_id);
  }
  w.write_fmt("traitor={}", m.traitor);
  w.write_fmt("size={}", m.size);
  w.write_fmt("magic_using={}", m.magic_using);
  w.write_fmt("undead={}", m.undead);
  w.write_fmt("demon_devil={}", m.demon_devil);
  w.write_fmt("reptilian={}", m.reptilian);
  w.write_fmt("very_evil={}", m.very_evil);
  w.write_fmt("intelligent={}", m.intelligent);
  w.write_fmt("giant_size={}", m.giant_size);
  w.write_fmt("non_humanoid={}", m.non_humanoid);
  w.write_fmt("num_physical_attacks={}", m.num_physical_attacks);
  w.write_fmt("num_magic_attacks={}", m.num_magic_attacks);
  for (size_t z = 0; z < 5; z++) {
    static const array<const char*, 0x0B> forms = {
        /* 20 */ "(nothing)",
        /* 21 */ "pummel",
        /* 22 */ "claw",
        /* 23 */ "bite",
        /* 24 */ "(unused-24)",
        /* 25 */ "(unused-25)",
        /* 26 */ "(unused-26)",
        /* 27 */ "punch/kick",
        /* 28 */ "club",
        /* 29 */ "slime",
        /* 2A */ "sting",
    };
    static const array<const char*, 0x14> special_conditions = {
        /* 00 */ "(nothing)",
        /* 01 */ "cause fear",
        /* 02 */ "paralyze",
        /* 03 */ "curse",
        /* 04 */ "stupefy",
        /* 05 */ "entangle",
        /* 06 */ "poison",
        /* 07 */ "confuse",
        /* 08 */ "drain spell points",
        /* 09 */ "drain experience",
        /* 0A */ "charm",
        /* 0B */ "fire damage",
        /* 0C */ "cold damage",
        /* 0D */ "electric damage",
        /* 0E */ "chemical damage",
        /* 0F */ "mental damage",
        /* 10 */ "cause disease",
        /* 11 */ "cause age",
        /* 12 */ "cause blindness",
        /* 13 */ "turn to stone",
    };
    const auto& att = m.attacks[z];
    w.write_fmt("(attack {}) damage_range=[{}, {}]", z, att.min_damage, att.max_damage);
    try {
      w.write_fmt("(attack {}) form={}", z, forms.at(att.form - 0x20));
    } catch (const out_of_range&) {
      w.write_fmt("(attack {}) form=(unknown-{:02X})", z, att.form);
    }
    try {
      w.write_fmt("(attack {}) special_condition={}", z, special_conditions.at(att.special_condition));
    } catch (const out_of_range&) {
      w.write_fmt("(attack {}) special_conditions=(unknown-{:02X})", z, att.special_condition);
    }
  }
  w.write_fmt("damage_plus={}", m.damage_plus);
  w.write_fmt("cast_spell_percent={}", m.cast_spell_percent);
  w.write_fmt("run_away_percent={}", m.run_away_percent);
  w.write_fmt("surrender_percent={}", m.surrender_percent);
  w.write_fmt("use_missile_percent={}", m.use_missile_percent);
  if (m.summon_flag == 0) {
    w.write_fmt("summon_flag=no");
  } else if (m.summon_flag == 1) {
    w.write_fmt("summon_flag=yes");
  } else if (m.summon_flag == -1) {
    w.write_fmt("summon_flag=is_npc");
  } else {
    w.write_fmt("summon_flag={:02X}", m.summon_flag);
  }
  w.write_fmt("drv_adjust_heat={}", m.drv_adjust_heat);
  w.write_fmt("drv_adjust_cold={}", m.drv_adjust_cold);
  w.write_fmt("drv_adjust_electric={}", m.drv_adjust_electric);
  w.write_fmt("drv_adjust_chemical={}", m.drv_adjust_chemical);
  w.write_fmt("drv_adjust_mental={}", m.drv_adjust_mental);
  w.write_fmt("drv_adjust_magic={}", m.drv_adjust_magic);
  w.write_fmt("immune_to_charm={}", m.immune_to_charm);
  w.write_fmt("immune_to_heat={}", m.immune_to_heat);
  w.write_fmt("immune_to_cold={}", m.immune_to_cold);
  w.write_fmt("immune_to_electric={}", m.immune_to_electric);
  w.write_fmt("immune_to_chemical={}", m.immune_to_chemical);
  w.write_fmt("immune_to_mental={}", m.immune_to_mental);
  w.write_fmt("gold={}", m.gold);
  w.write_fmt("gems={}", m.gems);
  w.write_fmt("jewelry={}", m.jewelry);
  for (size_t z = 0; z < 10; z++) {
    uint16_t spell_id = m.spells[z];
    if (spell_id) {
      try {
        w.write_fmt("spells[{}]={} ({})", z, spell_id, this->name_for_spell(spell_id));
      } catch (const out_of_range&) {
        w.write_fmt("spells[{}]={}", z, spell_id);
      }
    }
  }
  for (size_t z = 0; z < 6; z++) {
    if (m.held_items[z]) {
      w.write_fmt("held_items[{}]={}", z, this->desc_for_item(m.held_items[z]));
    }
  }
  if (m.weapon) {
    w.write_fmt("weapon={}", this->desc_for_item(m.weapon));
  } else {
    w.write_fmt("weapon=(none)");
  }
  w.write_fmt("spell_points={}", m.spell_points);
  w.write_fmt("icon={}", m.icon);
  w.write_fmt("spell_points={}", m.spell_points);
  w.write_fmt("experience={}", m.experience);
  w.write_fmt("current_hp={}", m.current_hp);
  w.write_fmt("max_hp={}", m.max_hp);
  w.write_fmt("underneath=[[{}, {}], [{}, {}]]", m.underneath[0][0], m.underneath[0][1], m.underneath[1][0], m.underneath[1][1]);
  w.write_fmt("target={}", m.target);
  w.write_fmt("guarding={}", m.guarding);
  w.write_fmt("hide_in_bestiary_menu={}", m.hide_in_bestiary_menu);
  w.write_fmt("beenattacked={}", m.beenattacked);
  w.write_fmt("remaining_movement={}", m.remaining_movement);
  w.write_fmt("magic_plus_required_to_hit={}", m.magic_plus_required_to_hit);
  for (size_t z = 0; z < sizeof(m.conditions); z++) {
    if (m.conditions[z]) {
      w.write_fmt("condition[{}({})]={}{}",
          z, RealmzGlobalData::name_for_condition(z), m.conditions[z], m.conditions[z] < 0 ? " (permanent)" : "");
    }
  }
  w.write_fmt("lr={}", m.lr);
  w.write_fmt("up={}", m.up);
  w.write_fmt("attacknum={}", m.attacknum);
  w.write_fmt("bonusattack={}", m.bonusattack);
  w.write_fmt("death_xap_num=XAP{}", m.death_xap_num);
  w.write_fmt("max_sp=XAP{}", m.max_sp);
  string name(m.name, sizeof(m.name));
  strip_trailing_zeroes(name);
  w.write_fmt("name=\"{}\"", phosg::escape_quotes(name));

  string separator = "\n" + indent_str;
  return w.close(separator.c_str());
}

string RealmzScenarioData::disassemble_all_monsters() const {
  deque<string> blocks;
  for (size_t z = 0; z < this->monsters.size(); z++) {
    blocks.emplace_back(this->disassemble_monster(z));
  }
  return join(blocks, "");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA BD

vector<RealmzScenarioData::BattleDefinition> RealmzScenarioData::load_battle_index(const string& filename) {
  return load_vector_file<BattleDefinition>(filename);
}

string RealmzScenarioData::disassemble_battle(size_t index) const {
  const auto& b = this->battles.at(index);

  BlockStringWriter w;
  set<int16_t> monster_ids;
  w.write_fmt("===== BATTLE id={} [BTL{}]", index, index);
  for (size_t y = 0; y < 13; y++) {
    string line = std::format("  field[{:X}]:", y);
    for (size_t x = 0; x < 13; x++) {
      int16_t monster_id = b.monster_ids[x][y];
      if (monster_id) {
        monster_ids.emplace(monster_id);
        line += std::format(" {:6}", monster_id);
      } else {
        line += " ------";
      }
    }
    w.write(std::move(line));
  }
  for (int16_t monster_id : monster_ids) {
    uint16_t effective_monster_id = (monster_id < 0) ? -monster_id : monster_id;
    const char* friendly_str = (monster_id < 0) ? "(friendly) " : "";
    try {
      string name(this->monsters.at(effective_monster_id).name, sizeof(MonsterDefinition::name));
      strip_trailing_zeroes(name);
      w.write_fmt("  (reference) {}={}{}", monster_id, friendly_str, phosg::escape_quotes(name));
    } catch (const out_of_range&) {
      w.write_fmt("  (reference) {}={}(missing)", monster_id, friendly_str);
    }
  }
  // TODO: Add monster names here for the monsters referenced in the above lines
  w.write_fmt("  bonus_distance={}", b.bonus_distance);
  w.write_fmt("  a1={:02X}", b.unknown_a1);
  string before = render_string_reference(this->strings, b.before_string);
  w.write_fmt("  before_string={}", before);
  string after = render_string_reference(this->strings, b.after_string);
  w.write_fmt("  after_string={}", after);
  w.write_fmt("  macro_number={}", b.macro_number);
  w.write("", 0);
  return w.close("\n");
}

string RealmzScenarioData::disassemble_all_battles() const {
  deque<string> blocks;
  for (size_t z = 0; z < this->battles.size(); z++) {
    blocks.emplace_back(this->disassemble_battle(z));
  }
  return join(blocks, "");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA NI

string RealmzScenarioData::disassemble_all_custom_item_definitions() const {
  deque<string> blocks;
  for (size_t z = 0; z < this->custom_item_definitions.size(); z++) {
    const RealmzGlobalData::ItemStrings* strings = nullptr;
    try {
      strings = &this->strings_for_item(z + 800);
    } catch (const out_of_range&) {
    }
    blocks.emplace_back(this->global.disassemble_item_definition(this->custom_item_definitions[z], z + 800, strings));
  }
  return join(blocks, "");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DATA SD

vector<RealmzScenarioData::Shop> RealmzScenarioData::load_shop_index(const string& filename) {
  return load_vector_file<Shop>(filename);
}

string RealmzScenarioData::disassemble_shop(const Shop& s, size_t index) const {
  static const array<const char*, 5> category_names = {"weapons", "armor1", "armor2", "magic", "items"};

  BlockStringWriter w;
  w.write_fmt("===== SHOP id={} [SHP{}]", index, index);
  w.write_fmt("  inflation_percent={}", s.inflation_percent);
  for (size_t z = 0; z < 1000; z++) {
    if (s.item_ids[z] || s.item_counts[z]) {
      string desc = this->desc_for_item(s.item_ids[z]);
      w.write_fmt("  {}[{}]={} x{}", category_names[z / 200], z % 200, desc, s.item_counts[z]);
    }
  }
  w.write("", 0);
  return w.close("\n");
}

string RealmzScenarioData::disassemble_all_shops() const {
  deque<string> blocks;
  for (size_t z = 0; z < this->shops.size(); z++) {
    blocks.emplace_back(this->disassemble_shop(this->shops[z], z));
  }
  return join(blocks, "");
}

} // namespace ResourceDASM
