#include "RealmzScenarioData.hh"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

RealmzScenarioData::RealmzScenarioData(
    RealmzGlobalData& global, const string& scenario_dir, const string& name)
    : global(global),
      scenario_dir(scenario_dir),
      name(name) {

  string scenario_metadata_name = this->scenario_dir + "/" + this->name;
  string global_metadata_name = first_file_that_exists({(this->scenario_dir + "/global"),
      (this->scenario_dir + "/Global")});
  string dungeon_map_index_name = first_file_that_exists({(this->scenario_dir + "/data_dl"),
      (this->scenario_dir + "/Data DL"),
      (this->scenario_dir + "/DATA DL")});
  string land_map_index_name = first_file_that_exists({(this->scenario_dir + "/data_ld"),
      (this->scenario_dir + "/Data LD"),
      (this->scenario_dir + "/DATA LD")});
  string string_index_name = first_file_that_exists({(this->scenario_dir + "/data_sd2"),
      (this->scenario_dir + "/Data SD2"),
      (this->scenario_dir + "/DATA SD2")});
  string ecodes_index_name = first_file_that_exists({(this->scenario_dir + "/data_edcd"),
      (this->scenario_dir + "/Data EDCD"),
      (this->scenario_dir + "/DATA EDCD")});
  string land_ap_index_name = first_file_that_exists({(this->scenario_dir + "/data_dd"),
      (this->scenario_dir + "/Data DD"),
      (this->scenario_dir + "/DATA DD")});
  string dungeon_ap_index_name = first_file_that_exists({(this->scenario_dir + "/data_ddd"),
      (this->scenario_dir + "/Data DDD"),
      (this->scenario_dir + "/DATA DDD")});
  string extra_ap_index_name = first_file_that_exists({(this->scenario_dir + "/data_ed3"),
      (this->scenario_dir + "/Data ED3"),
      (this->scenario_dir + "/DATA ED3")});
  string land_metadata_index_name = first_file_that_exists({(this->scenario_dir + "/data_rd"),
      (this->scenario_dir + "/Data RD"),
      (this->scenario_dir + "/DATA RD")});
  string dungeon_metadata_index_name = first_file_that_exists({(this->scenario_dir + "/data_rdd"),
      (this->scenario_dir + "/Data RDD"),
      (this->scenario_dir + "/DATA RDD")});
  string simple_encounter_index_name = first_file_that_exists({(this->scenario_dir + "/data_ed"),
      (this->scenario_dir + "/Data ED"),
      (this->scenario_dir + "/DATA ED")});
  string complex_encounter_index_name = first_file_that_exists({(this->scenario_dir + "/data_ed2"),
      (this->scenario_dir + "/Data ED2"),
      (this->scenario_dir + "/DATA ED2")});
  string party_map_index_name = first_file_that_exists({(this->scenario_dir + "/data_md2"),
      (this->scenario_dir + "/Data MD2"),
      (this->scenario_dir + "/DATA MD2")});
  string treasure_index_name = first_file_that_exists({(this->scenario_dir + "/data_td"),
      (this->scenario_dir + "/Data TD"),
      (this->scenario_dir + "/DATA TD")});
  string rogue_encounter_index_name = first_file_that_exists({(this->scenario_dir + "/data_td2"),
      (this->scenario_dir + "/Data TD2"),
      (this->scenario_dir + "/DATA TD2")});
  string time_encounter_index_name = first_file_that_exists({(this->scenario_dir + "/data_td3"),
      (this->scenario_dir + "/Data TD3"),
      (this->scenario_dir + "/DATA TD3")});
  string scenario_resources_name = first_file_that_exists({(this->scenario_dir + "/scenario.rsf"),
      (this->scenario_dir + "/Scenario.rsf"),
      (this->scenario_dir + "/SCENARIO.RSF"),
      (this->scenario_dir + "/scenario/rsrc"),
      (this->scenario_dir + "/Scenario/rsrc"),
      (this->scenario_dir + "/SCENARIO/rsrc"),
      (this->scenario_dir + "/scenario/..namedfork/rsrc"),
      (this->scenario_dir + "/Scenario/..namedfork/rsrc"),
      (this->scenario_dir + "/SCENARIO/..namedfork/rsrc")});

  this->dungeon_maps = this->load_dungeon_map_index(dungeon_map_index_name);
  this->land_maps = this->load_land_map_index(land_map_index_name);
  this->strings = this->load_string_index(string_index_name);
  this->ecodes = this->load_ecodes_index(ecodes_index_name);
  this->dungeon_aps = this->load_ap_index(dungeon_ap_index_name);
  this->land_aps = this->load_ap_index(land_ap_index_name);
  this->xaps = this->load_xap_index(extra_ap_index_name);
  this->dungeon_metadata = this->load_map_metadata_index(dungeon_metadata_index_name);
  this->land_metadata = this->load_map_metadata_index(land_metadata_index_name);
  this->simple_encounters = this->load_simple_encounter_index(simple_encounter_index_name);
  this->complex_encounters = this->load_complex_encounter_index(complex_encounter_index_name);
  this->party_maps = this->load_party_map_index(party_map_index_name);
  this->treasures = this->load_treasure_index(treasure_index_name);
  this->rogue_encounters = this->load_rogue_encounter_index(rogue_encounter_index_name);
  this->time_encounters = this->load_time_encounter_index(time_encounter_index_name);
  // Some scenarios apparently don't have global metadata
  if (!global_metadata_name.empty()) {
    this->global_metadata = this->load_global_metadata(global_metadata_name);
  }
  this->scenario_metadata = this->load_scenario_metadata(scenario_metadata_name);
  this->scenario_rsf = parse_resource_fork(load_file(scenario_resources_name));

  this->item_info = RealmzGlobalData::parse_item_info(this->scenario_rsf);
  this->spell_names = RealmzGlobalData::parse_spell_names(this->scenario_rsf);

  // Load layout separately because it doesn't have to exist
  {
    string fname = first_file_that_exists({(this->scenario_dir + "/layout"),
        (this->scenario_dir + "/Layout")});
    if (!fname.empty()) {
      this->layout = this->load_land_layout(fname);
    } else {
      fprintf(stderr, "note: this scenario has no land layout information\n");
    }
  }

  // Load tilesets
  for (int z = 1; z < 4; z++) {
    string fname = first_file_that_exists({string_printf("%s/data_custom_%d_bd", this->scenario_dir.c_str(), z),
        string_printf("%s/Data Custom %d BD", this->scenario_dir.c_str(), z),
        string_printf("%s/DATA CUSTOM %d BD", this->scenario_dir.c_str(), z)});
    if (!fname.empty()) {
      string land_type = string_printf("custom_%d", z);
      this->land_type_to_tileset_definition.emplace(
          move(land_type), load_tileset_definition(fname));
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

const ItemInfo& RealmzScenarioData::info_for_item(uint16_t id) const {
  try {
    return this->item_info.at(id);
  } catch (const out_of_range&) {
    return this->global.info_for_item(id);
  }
}

static string render_string_reference(const vector<string>& strings,
    int index) {
  if (index == 0) {
    return "0";
  }
  if ((size_t)abs(index) >= strings.size()) {
    return string_printf("%d", index);
  }

  // Strings in Realmz scenarios often end with a bunch of spaces, which looks
  // bad in the disassembly and serves no purpose, so we trim them off here.
  string s = strings[abs(index)];
  strip_trailing_whitespace(s);
  s = escape_quotes(s);
  return string_printf("\"%s\"#%d", s.c_str(), index);
}

////////////////////////////////////////////////////////////////////////////////
// DATA MD2

vector<RealmzScenarioData::PartyMap> RealmzScenarioData::load_party_map_index(
    const string& filename) {
  return load_vector_file<PartyMap>(filename);
}

string RealmzScenarioData::disassemble_party_map(size_t index) {
  const PartyMap& pm = this->party_maps.at(index);

  string ret = string_printf("===== %s MAP id=%zu level=%hd x=%hd y=%hd tile_size=%hd [MAP%zu]\n",
      (pm.is_dungeon ? "DUNGEON" : "LAND"), index, pm.level_num.load(), pm.x.load(), pm.y.load(), pm.tile_size.load(), index);
  if (pm.picture_id) {
    ret += string_printf("  picture id=%hd\n", pm.picture_id.load());
  }
  if (pm.text_id) {
    ret += string_printf("  text id=%hd\n", pm.text_id.load());
  }

  for (int x = 0; x < 10; x++) {
    if (!pm.annotations[x].icon_id && !pm.annotations[x].x && !pm.annotations[x].y) {
      continue;
    }
    ret += string_printf("  annotation icon_id=%d x=%d y=%d\n",
        pm.annotations[x].icon_id.load(), pm.annotations[x].x.load(), pm.annotations[x].y.load());
  }

  string description(pm.description, pm.description_valid_chars);
  ret += string_printf("  description=\"%s\"\n", description.c_str());
  return ret;
}

string RealmzScenarioData::disassemble_all_party_maps() {
  string ret;
  for (size_t z = 0; z < this->party_maps.size(); z++) {
    ret += this->disassemble_party_map(z);
  }
  return ret;
}

Image RealmzScenarioData::render_party_map(size_t index) {
  const auto& pm = this->party_maps.at(index);

  if (!pm.tile_size) {
    throw runtime_error("tile size is zero");
  }
  if (pm.tile_size > (pm.is_dungeon ? 16 : 32)) {
    throw runtime_error("tile size is too large");
  }

  double whf = 320.0 / pm.tile_size;
  size_t wh = static_cast<size_t>(ceil(whf));

  Image ret;
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
    Image cicn;
    try {
      cicn = this->scenario_rsf.decode_cicn(a.icon_id).image;
    } catch (const out_of_range&) {
    }
    try {
      cicn = this->global.global_rsf.decode_cicn(a.icon_id).image;
    } catch (const out_of_range&) {
    }
    if (cicn.get_width() == 0 || cicn.get_height() == 0) {
      fprintf(stderr, "warning: map refers to missing cicn %hd\n", a.icon_id.load());
    } else {
      // It appears that annotations should render centered on the tile on which
      // they are defined, so we may need to adjust dest x/y is the cicn size
      // isn't the same as the tile size.
      ssize_t px = a.x * rendered_tile_size - (cicn.get_width() - rendered_tile_size) / 2;
      ssize_t py = a.y * rendered_tile_size - (cicn.get_height() - rendered_tile_size) / 2;
      ret.blit(cicn, px, py, cicn.get_width(), cicn.get_height(), 0, 0);
    }
  }

  return ret;
}

////////////////////////////////////////////////////////////////////////////////
// LAYOUT

RealmzScenarioData::LevelNeighbors::LevelNeighbors()
    : x(-1),
      y(-1),
      left(-1),
      right(-1),
      top(-1),
      bottom(-1) {}

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

RealmzScenarioData::LandLayout RealmzScenarioData::load_land_layout(
    const string& filename) {
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

RealmzScenarioData::LevelNeighbors
RealmzScenarioData::LandLayout::get_level_neighbors(int16_t id) const {
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

vector<RealmzScenarioData::LandLayout>
RealmzScenarioData::LandLayout::get_connected_components() const {
  LandLayout remaining_components(*this);

  vector<LandLayout> ret;
  for (ssize_t y = 0; y < 8; y++) {
    for (ssize_t x = 0; x < 16; x++) {
      if (remaining_components.layout[y][x] == -1) {
        continue;
      }

      // This cell is the upper-left corner of a connected component; use
      // flood-fill to copy it to this_component
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

Image RealmzScenarioData::generate_layout_map(const LandLayout& l,
    const unordered_map<int16_t, string>& level_id_to_image_name) {

  ssize_t min_x = 16, min_y = 8, max_x = -1, max_y = -1;
  for (ssize_t y = 0; y < 8; y++) {
    for (ssize_t x = 0; x < 16; x++) {
      if (l.layout[y][x] < 0) {
        continue;
      }

      // If the level has no valid neighbors, ignore it
      if (x > 0 && l.layout[y][x - 1] < 0 &&
          x < 15 && l.layout[y][x + 1] < 0 &&
          y > 0 && l.layout[y - 1][x] < 0 &&
          y < 7 && l.layout[y + 1][x] < 0) {
        continue;
      }

      if (x < min_x) {
        min_x = x;
      }
      if (x > max_x) {
        max_x = x;
      }
      if (y < min_y) {
        min_y = y;
      }
      if (y > max_y) {
        max_y = y;
      }
    }
  }

  if (max_x < min_x || max_y < min_y) {
    throw runtime_error("layout has no valid levels");
  }

  max_x++;
  max_y++;

  Image overall_map(90 * 32 * (max_x - min_x), 90 * 32 * (max_y - min_y));
  for (ssize_t y = 0; y < (max_y - min_y); y++) {
    for (ssize_t x = 0; x < (max_x - min_x); x++) {
      int16_t level_id = l.layout[y + min_y][x + min_x];
      if (level_id < 0) {
        continue;
      }

      int xp = 90 * 32 * x;
      int yp = 90 * 32 * y;

      try {
        Image this_level_map(level_id_to_image_name.at(level_id).c_str());

        // If get_level_neighbors fails, then we would not have written any
        // boundary information on the original map, so we can just ignore this
        int sx = 0, sy = 0;
        try {
          LevelNeighbors n = l.get_level_neighbors(level_id);
          sx = (n.left >= 0) ? 9 : 0;
          sy = (n.top >= 0) ? 9 : 0;
        } catch (const runtime_error&) {
        }

        overall_map.blit(this_level_map, xp, yp, 90 * 32, 90 * 32, sx, sy);
      } catch (const exception& e) {
        overall_map.fill_rect(xp, yp, 90 * 32, 90 * 32, 0xFFFFFFFF);
        overall_map.draw_text(xp + 10, yp + 10, 0xFF0000FF, 0x00000000,
            "can\'t read disassembled map %hd", level_id);
        overall_map.draw_text(xp + 10, yp + 20, 0x000000FF, 0x00000000,
            "%s", e.what());
      }
    }
  }

  return overall_map;
}

////////////////////////////////////////////////////////////////////////////////
// GLOBAL

RealmzScenarioData::GlobalMetadata RealmzScenarioData::load_global_metadata(
    const string& filename) {
  return load_object_file<GlobalMetadata>(filename, true);
}

string RealmzScenarioData::disassemble_globals() {
  return string_printf("===== GLOBAL METADATA [GMD]\n"
                       "  start_xap id=XAP%d\n"
                       "  death_xap id=XAP%d\n"
                       "  quit_xap id=XAP%d\n"
                       "  reserved1_xap id=XAP%d\n"
                       "  shop_xap id=XAP%d\n"
                       "  temple_xap id=XAP%d\n"
                       "  reserved2_xap id=XAP%d\n",
      this->global_metadata.start_xap.load(),
      this->global_metadata.death_xap.load(),
      this->global_metadata.quit_xap.load(),
      this->global_metadata.reserved1_xap.load(),
      this->global_metadata.shop_xap.load(),
      this->global_metadata.temple_xap.load(),
      this->global_metadata.reserved2_xap.load());
}

////////////////////////////////////////////////////////////////////////////////
// SCENARIO NAME

RealmzScenarioData::ScenarioMetadata RealmzScenarioData::load_scenario_metadata(
    const string& filename) {
  return load_object_file<ScenarioMetadata>(filename, true);
}

////////////////////////////////////////////////////////////////////////////////
// DATA EDCD

vector<RealmzScenarioData::ECodes> RealmzScenarioData::load_ecodes_index(
    const string& filename) {
  return load_vector_file<ECodes>(filename);
}

////////////////////////////////////////////////////////////////////////////////
// DATA TD

vector<RealmzScenarioData::Treasure> RealmzScenarioData::load_treasure_index(
    const string& filename) {
  return load_vector_file<Treasure>(filename);
}

string RealmzScenarioData::disassemble_treasure(size_t index) {
  const auto& t = this->treasures.at(index);

  string ret = string_printf("===== TREASURE id=%zu [TSR%zu]", index, index);

  if (t.victory_points < 0) {
    ret += string_printf(" victory_points=rand(1,%d)", -t.victory_points);
  } else if (t.victory_points > 0) {
    ret += string_printf(" victory_points=%d", t.victory_points.load());
  }

  if (t.gold < 0) {
    ret += string_printf(" gold=rand(1,%d)", -t.gold.load());
  } else if (t.gold > 0) {
    ret += string_printf(" gold=%d", t.gold.load());
  }

  if (t.gems < 0) {
    ret += string_printf(" gems=rand(1,%d)", -t.gems.load());
  } else if (t.gems > 0) {
    ret += string_printf(" gems=%d", t.gems.load());
  }

  if (t.jewelry < 0) {
    ret += string_printf(" jewelry=rand(1,%d)", -t.jewelry.load());
  } else if (t.jewelry > 0) {
    ret += string_printf(" jewelry=%d", t.jewelry.load());
  }

  ret += '\n';

  for (int x = 0; x < 20; x++) {
    if (t.item_ids[x]) {
      try {
        const auto& info = this->info_for_item(t.item_ids[x]);
        ret += string_printf("  %hd (%s)\n", t.item_ids[x].load(), info.name.c_str());
      } catch (const out_of_range&) {
        ret += string_printf("  %hd\n", t.item_ids[x].load());
      }
    }
  }

  return ret;
}

string RealmzScenarioData::disassemble_all_treasures() {
  string ret;
  for (size_t z = 0; z < this->treasures.size(); z++) {
    ret += this->disassemble_treasure(z);
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
// DATA ED

vector<RealmzScenarioData::SimpleEncounter>
RealmzScenarioData::load_simple_encounter_index(const string& filename) {
  return load_vector_file<SimpleEncounter>(filename);
}

string RealmzScenarioData::disassemble_simple_encounter(size_t index) {
  const auto& e = this->simple_encounters.at(index);

  string prompt = render_string_reference(this->strings, e.prompt);
  string ret = string_printf("===== SIMPLE ENCOUNTER id=%zu can_backout=%hhd max_times=%hhd prompt=%s [SEC%zu]\n",
      index, e.can_backout, e.max_times, prompt.c_str(), index);

  for (size_t x = 0; x < 4; x++) {
    string choice_text(e.choice_text[x].text, min(static_cast<size_t>(e.choice_text[x].valid_chars), static_cast<size_t>(sizeof(e.choice_text[x]) - 1)));
    strip_trailing_whitespace(choice_text);
    if (choice_text.empty()) {
      continue;
    }
    choice_text = escape_quotes(choice_text);
    ret += string_printf("  choice%zu: result=%d text=\"%s\"\n", x,
        e.choice_result_index[x], choice_text.c_str());
  }

  for (size_t x = 0; x < 4; x++) {
    size_t y;
    for (y = 0; y < 8; y++) {
      if (e.choice_codes[x][y] || e.choice_args[x][y]) {
        break;
      }
    }
    if (y == 8) {
      continue; // Option is blank; don't even print it
    }

    ret += string_printf("  result%zu\n", x + 1);
    for (size_t y = 0; y < 8; y++) {
      if (e.choice_codes[x][y] || e.choice_args[x][y]) {
        string dasm = disassemble_opcode(e.choice_codes[x][y], e.choice_args[x][y]);
        ret += string_printf("    %s\n", dasm.c_str());
      }
    }
  }

  return ret;
}

string RealmzScenarioData::disassemble_all_simple_encounters() {
  string ret;
  for (size_t x = 0; x < this->simple_encounters.size(); x++) {
    ret += this->disassemble_simple_encounter(x);
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
// DATA ED2

vector<RealmzScenarioData::ComplexEncounter>
RealmzScenarioData::load_complex_encounter_index(const string& filename) {
  return load_vector_file<ComplexEncounter>(filename);
}

string RealmzScenarioData::disassemble_complex_encounter(size_t index) {
  const auto& e = this->complex_encounters.at(index);

  string prompt = render_string_reference(this->strings, e.prompt);
  string ret = string_printf("===== COMPLEX ENCOUNTER id=%zu can_backout=%hhd max_times=%hhd prompt=%s [CEC%zu]\n",
      index, e.can_backout, e.max_times, prompt.c_str(), index);

  bool wrote_spell_header = false;
  for (size_t x = 0; x < 10; x++) {
    if (!e.spell_codes[x]) {
      continue;
    }
    if (!wrote_spell_header) {
      ret += "  spells\n";
      wrote_spell_header = true;
    }
    try {
      string name = this->global.name_for_spell(e.spell_codes[x].load());
      ret += string_printf("    result=%d id=%d(%s)\n",
          e.spell_result_codes[x], e.spell_codes[x].load(), name.c_str());
    } catch (const out_of_range&) {
      ret += string_printf("    result=%d, id=%d\n",
          e.spell_result_codes[x], e.spell_codes[x].load());
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
    try {
      const auto& info = this->info_for_item(e.item_codes[x]);
      ret += string_printf("    result=%d id=%d(%s)\n",
          e.item_result_codes[x], e.item_codes[x].load(), info.name.c_str());
    } catch (const out_of_range&) {
      ret += string_printf("    result=%d id=%d\n",
          e.item_result_codes[x], e.item_codes[x].load());
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
      ret += string_printf("  actions result=%d\n", e.action_result);
      wrote_action_header = true;
    }
    action_text = escape_quotes(action_text);
    ret += string_printf("    selected=%d text=\"%s\"\n",
        e.actions_selected[x], action_text.c_str());
  }

  if (e.has_rogue_encounter) {
    ret += string_printf("  rogue_encounter id=%d reset=%d\n",
        e.rogue_encounter_id.load(), e.rogue_reset_flag);
  }

  string speak_text(e.speak_text.text, min((int)e.speak_text.valid_chars, (int)sizeof(e.speak_text) - 1));
  strip_trailing_whitespace(speak_text);
  if (!speak_text.empty()) {
    speak_text = escape_quotes(speak_text);
    ret += string_printf("  speak result=%d text=\"%s\"\n", e.speak_result,
        speak_text.c_str());
  }

  for (size_t x = 0; x < 4; x++) {
    size_t y;
    for (y = 0; y < 8; y++) {
      if (e.choice_codes[x][y] || e.choice_args[x][y]) {
        break;
      }
    }
    if (y == 8) {
      continue; // Option is entirely blank; don't even print it
    }

    ret += string_printf("  result%zu\n", x + 1);
    for (size_t y = 0; y < 8; y++) {
      if (e.choice_codes[x][y] || e.choice_args[x][y]) {
        string dasm = disassemble_opcode(e.choice_codes[x][y], e.choice_args[x][y]);
        ret += string_printf("    %s\n", dasm.c_str());
      }
    }
  }

  return ret;
}

string RealmzScenarioData::disassemble_all_complex_encounters() {
  string ret;
  for (size_t x = 0; x < this->complex_encounters.size(); x++) {
    ret += this->disassemble_complex_encounter(x);
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
// DATA TD2

vector<RealmzScenarioData::RogueEncounter>
RealmzScenarioData::load_rogue_encounter_index(const string& filename) {
  return load_vector_file<RogueEncounter>(filename);
}

static const vector<string> rogue_encounter_action_names({
    "acrobatic_act",
    "detect_trap",
    "disable_trap",
    "action3",
    "force_lock",
    "action5",
    "pick_lock",
    "action7",
});

string RealmzScenarioData::disassemble_rogue_encounter(size_t index) {
  const auto& e = this->rogue_encounters.at(index);

  string prompt = render_string_reference(strings, e.prompt_string);
  string ret = string_printf("===== ROGUE ENCOUNTER id=%zu sound=%hd prompt=%s "
                             "pct_per_level_to_open_lock=%hd pct_per_level_to_disable_trap=%hd "
                             "num_lock_tumblers=%hd [REC%zu]\n",
      index, e.prompt_sound.load(), prompt.c_str(), e.percent_per_level_to_open.load(),
      e.percent_per_level_to_disable.load(), e.num_lock_tumblers.load(), index);

  for (size_t x = 0; x < 8; x++) {
    if (!e.actions_available[x]) {
      continue;
    }
    string success_str = render_string_reference(strings, e.success_string_ids[x]);
    string failure_str = render_string_reference(strings, e.failure_string_ids[x]);

    ret += string_printf("  action_%s percent_modify=%d success_result=%d "
                         "failure_result=%d success_str=%s failure_str=%s success_sound=%d "
                         "failure_sound=%d\n",
        rogue_encounter_action_names[x].c_str(),
        e.percent_modify[x], e.success_result_codes[x],
        e.failure_result_codes[x], success_str.c_str(), failure_str.c_str(),
        e.success_sound_ids[x].load(), e.failure_sound_ids[x].load());
  }

  if (e.is_trapped) {
    string spell_desc;
    try {
      const auto& name = this->global.name_for_spell(e.trap_spell);
      spell_desc = string_printf("%d(%s)", e.trap_spell.load(), name.c_str());
    } catch (const out_of_range&) {
      spell_desc = string_printf("%d", e.trap_spell.load());
    }
    ret += string_printf("  trap rogue_only=%d spell=%s spell_power=%d damage_range=[%d,%d] sound=%d\n",
        e.trap_affects_rogue_only, spell_desc.c_str(),
        e.trap_spell_power_level.load(), e.trap_damage_low.load(),
        e.trap_damage_high.load(), e.trap_sound.load());
  }

  return ret;
}

string RealmzScenarioData::disassemble_all_rogue_encounters() {
  string ret;
  for (size_t x = 0; x < this->rogue_encounters.size(); x++) {
    ret += this->disassemble_rogue_encounter(x);
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
// DATA TD3

vector<RealmzScenarioData::TimeEncounter>
RealmzScenarioData::load_time_encounter_index(const string& filename) {
  return load_vector_file<TimeEncounter>(filename);
}

string RealmzScenarioData::disassemble_time_encounter(size_t index) {
  const auto& e = this->time_encounters.at(index);

  string ret = string_printf("===== TIME ENCOUNTER id=%zu", index);

  ret += string_printf(" day=%hd", e.day.load());
  ret += string_printf(" increment=%hd", e.increment.load());
  ret += string_printf(" percent_chance=%hd", e.percent_chance.load());
  ret += string_printf(" xap_id=XAP%hd", e.xap_id.load());
  if (e.required_level != -1) {
    ret += string_printf(" required_level: id=%hd(%s)", e.required_level.load(),
        e.land_or_dungeon == 1 ? "land" : "dungeon");
  }
  if (e.required_rect != -1) {
    ret += string_printf(" required_rect=%hd", e.required_rect.load());
  }
  if (e.required_x != -1 || e.required_y != -1) {
    ret += string_printf(" required_pos=(%hd,%hd)", e.required_x.load(), e.required_y.load());
  }
  if (e.required_item_id != -1) {
    ret += string_printf(" required_item_id=%hd", e.required_item_id.load());
    try {
      const auto& info = this->info_for_item(e.required_item_id);
      ret += string_printf("(%s)", info.name.c_str());
    } catch (const out_of_range&) {
    }
  }
  if (e.required_quest != -1) {
    ret += string_printf(" required_quest=%hd", e.required_quest.load());
  }

  ret += string_printf(" [TEC%zu]\n", index);
  return ret;
}

string RealmzScenarioData::disassemble_all_time_encounters() {
  string ret;
  for (size_t x = 0; x < this->time_encounters.size(); x++) {
    ret += this->disassemble_time_encounter(x);
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
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

vector<RealmzScenarioData::MapMetadata>
RealmzScenarioData::load_map_metadata_index(const string& filename) {
  vector<MapMetadataFile> file_data = load_vector_file<MapMetadataFile>(filename);
  vector<MapMetadata> data(file_data.size());
  for (size_t x = 0; x < file_data.size(); x++) {
    try {
      data[x].land_type = land_type_to_string.at(file_data[x].land_type);
    } catch (const out_of_range& e) {
      data[x].land_type = "unknown";
    }
    for (size_t y = 0; y < 20; y++) {
      RandomRect r;
      r.top = file_data[x].coords[y].top;
      r.left = file_data[x].coords[y].left;
      r.bottom = file_data[x].coords[y].bottom;
      r.right = file_data[x].coords[y].right;
      r.times_in_10k = file_data[x].times_in_10k[y];
      r.battle_low = file_data[x].battle_range[y].low;
      r.battle_high = file_data[x].battle_range[y].high;
      r.xap_num[0] = file_data[x].xap_num[y][0];
      r.xap_num[1] = file_data[x].xap_num[y][1];
      r.xap_num[2] = file_data[x].xap_num[y][2];
      r.xap_chance[0] = file_data[x].xap_chance[y][0];
      r.xap_chance[1] = file_data[x].xap_chance[y][1];
      r.xap_chance[2] = file_data[x].xap_chance[y][2];
      r.percent_option = file_data[x].percent_option[y];
      r.sound = file_data[x].sound[y];
      r.text = file_data[x].text[y];
      data[x].random_rects.push_back(r);
    }
  }

  return data;
}

static void draw_random_rects(Image& map,
    const vector<RealmzScenarioData::RandomRect>& random_rects,
    size_t xpoff,
    size_t ypoff,
    bool is_dungeon,
    int16_t level_num,
    uint8_t x0, uint8_t y0, uint8_t w, uint8_t h) {

  size_t tile_size = is_dungeon ? 16 : 32;
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
        rect.xap_num[0] == 0 && rect.xap_num[1] == 0 && rect.xap_num[2] == 0 &&
        rect.xap_chance[0] == 0 && rect.xap_chance[1] == 0 && rect.xap_chance[2] == 0 &&
        rect.percent_option == 0 && rect.sound == 0 && rect.text == 0) {
      continue;
    }

    // If we get here, then the rect is nontrivial and is at least partially
    // within the render window, so we should draw it.

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

    ssize_t xp_left = (rect.left - x0) * tile_size + xpoff;
    ssize_t xp_right = (rect.right - x0) * tile_size + tile_size - 1 + xpoff;
    ssize_t yp_top = (rect.top - y0) * tile_size + ypoff;
    ssize_t yp_bottom = (rect.bottom - y0) * tile_size + tile_size - 1 + ypoff;

    ssize_t start_xx = (xp_left < 0) ? 0 : xp_left;
    ssize_t end_xx = (xp_right > static_cast<ssize_t>(map.get_width())) ? map.get_width() : xp_right;
    ssize_t start_yy = (yp_top < 0) ? 0 : yp_top;
    ssize_t end_yy = (yp_bottom > static_cast<ssize_t>(map.get_height())) ? map.get_height() : yp_bottom;
    for (ssize_t yy = start_yy; yy < end_yy; yy++) {
      for (ssize_t xx = start_xx; xx < end_xx; xx++) {

        uint64_t r = 0, g = 0, b = 0;
        map.read_pixel(xx, yy, &r, &g, &b);

        if (((xx + yy) / 8) & 1) {
          r = ((0xEF) * (uint32_t)r) / 0xFF;
          g = ((0xEF) * (uint32_t)g) / 0xFF;
          b = ((0xEF) * (uint32_t)b) / 0xFF;
        } else {
          r = (0xFF0 + 0xEF * (uint32_t)r) / 0xFF;
          g = (0xFF0 + 0xEF * (uint32_t)g) / 0xFF;
          b = (0xFF0 + 0xEF * (uint32_t)b) / 0xFF;
        }
        map.write_pixel(xx, yy, r, g, b);
      }
    }

    map.draw_horizontal_line(xp_left, xp_right, yp_top, 0, 0xFFFFFFFF);
    map.draw_horizontal_line(xp_left, xp_right, yp_bottom, 0, 0xFFFFFFFF);
    map.draw_vertical_line(xp_left, yp_top, yp_bottom, 0, 0xFFFFFFFF);
    map.draw_vertical_line(xp_right, yp_top, yp_bottom, 0, 0xFFFFFFFF);

    string rectinfo;
    if (rect.times_in_10k == -1) {
      rectinfo = string_printf("ENC XAP %d", rect.xap_num[0]);

    } else {
      rectinfo = string_printf("%d/10000", rect.times_in_10k);
      if (rect.battle_low || rect.battle_high) {
        rectinfo += string_printf(" b=[%d,%d]", rect.battle_low, rect.battle_high);
      }
      if (rect.percent_option) {
        rectinfo += string_printf(" o=%d%%", rect.percent_option);
      }
      if (rect.sound) {
        rectinfo += string_printf(" s=%d", rect.sound);
      }
      if (rect.text) {
        rectinfo += string_printf(" t=%d", rect.text);
      }
      for (size_t y = 0; y < 3; y++) {
        if (rect.xap_num[y] && rect.xap_chance[y]) {
          rectinfo += string_printf(
              " XAP%hd/%hd%%", rect.xap_num[y], rect.xap_chance[y]);
        }
      }
    }

    map.draw_text(
        xp_left + 2, yp_bottom - 8, NULL, NULL, 0xFFFFFFFF, 0x00000080,
        "%s", rectinfo.c_str());
    map.draw_text(
        xp_left + 2, yp_bottom - 16, NULL, NULL, 0xFFFFFFFF, 0x00000080,
        "%cRR%hd/%zu", is_dungeon ? 'D' : 'L', level_num, z);
  }
}

////////////////////////////////////////////////////////////////////////////////
// DATA DD

int8_t RealmzScenarioData::APInfo::get_x() const {
  if (this->location_code < 0) {
    return -1;
  }
  return this->location_code % 100;
}

int8_t RealmzScenarioData::APInfo::get_y() const {
  if (this->location_code < 0) {
    return -1;
  }
  return (this->location_code / 100) % 100;
}

int8_t RealmzScenarioData::APInfo::get_level_num() const {
  if (this->location_code < 0) {
    return -1;
  }
  return (this->location_code / 10000) % 100;
}

vector<vector<RealmzScenarioData::APInfo>> RealmzScenarioData::load_ap_index(
    const string& filename) {
  vector<APInfo> all_info = this->load_xap_index(filename);

  vector<vector<APInfo>> level_ap_info(all_info.size() / 100);
  for (size_t x = 0; x < all_info.size(); x++) {
    level_ap_info[x / 100].push_back(all_info[x]);
  }

  return level_ap_info;
}

vector<RealmzScenarioData::APInfo> RealmzScenarioData::load_xap_index(
    const string& filename) {
  return load_vector_file<APInfo>(filename);
}

enum class AnnotationType {
  NONE = 0,
  STRING,
  ITEM,
  SPELL,
};

struct OpcodeArgInfo {
  string arg_name;
  unordered_map<int16_t, string> value_names;
  string negative_modifier;
  AnnotationType annotation_type;
};

struct OpcodeInfo {
  string name;
  string negative_name;
  bool always_use_ecodes;
  vector<OpcodeArgInfo> args;
};

// clang-format off
static const unordered_map<int16_t, string> race_names({
  {1, "human"}, {2, "shadow elf"}, {3, "elf"}, {4, "orc"}, {5, "furfoot"},
  {6, "gnome"}, {7, "dwarf"}, {8, "half elf"}, {9, "half orc"}, {10, "goblin"},
  {11, "hobgoblin"}, {12, "kobold"}, {13, "vampire"}, {14, "lizard man"},
  {15, "brownie"}, {16, "pixie"}, {17, "leprechaun"}, {18, "demon"},
  {19, "cathoon"}});

static const unordered_map<int16_t, string> party_condition_names({
  {0, "torch"}, {1, "waterworld"}, {2, "ogre_dragon_hide"},
  {3, "detect_secret"}, {4, "wizard_eye"}, {5, "search"},
  {6, "free_fall_levitate"}, {7, "sentry"}, {8, "charm_resist"}});

static const unordered_map<int16_t, string> char_condition_names({
  {0, "run_away"}, {1, "helpless"}, {2, "tangled"}, {3, "cursed"},
  {4, "magic_aura"}, {5, "stupid"}, {6, "slow"}, {7, "shield_from_hits"},
  {8, "shield_from_proj"}, {9, "poisoned"}, {10, "regenerating"},
  {11, "fire_protection"}, {12, "cold_protection"},
  {13, "electrical_protection"}, {14, "chemical_protection"},
  {15, "mental_protection"}, {16, "1st_level_protection"},
  {17, "2nd_level_protection"}, {18, "3rd_level_protection"},
  {19, "4th_level_protection"}, {20, "5th_level_protection"},
  {21, "strong"}, {22, "protection_from_evil"}, {23, "speedy"},
  {24, "invisible"}, {25, "animated"}, {26, "stoned"}, {27, "blind"},
  {28, "diseased"}, {29, "confused"}, {30, "reflecting_spells"},
  {31, "reflecting_attacks"}, {32, "attack_bonus"}, {33, "absorbing_energy"},
  {34, "energy_drain"}, {35, "absorbing_energy_from_attacks"},
  {36, "hindered_attack"}, {37, "hindered_defense"}, {38, "defense_bonus"},
  {39, "silenced"}});

static const unordered_map<int16_t, string> option_jump_target_value_names({
  {0, "back_up"}, {1, "xap"}, {2, "simple"}, {3, "complex"}, {4, "eliminate"}});

static const unordered_map<int16_t, string> jump_target_value_names({
  {0, "xap"}, {1, "simple"}, {2, "complex"}});

static const unordered_map<int16_t, string> jump_or_exit_actions({
  {1, "jump"}, {2, "exit_ap"}, {-2, "exit_ap_delete"}});

static const unordered_map<int16_t, string> land_dungeon_value_names({
  {0, "land"}, {1, "dungeon"}});

static const unordered_map<int16_t, OpcodeInfo> opcode_definitions({
  {  1, {"string", "", false, {
    {"", {}, "no_wait", AnnotationType::STRING},
  }}},

  {  2, {"battle", "", false, {
    {"low", {}, "surprise", AnnotationType::NONE},
    {"high", {}, "surprise", AnnotationType::NONE},
    {"sound_or_lose_xap", {}, "", AnnotationType::NONE},
    {"string", {}, "", AnnotationType::STRING},
    {"treasure_mode", {{0, "all"}, {5, "no_enemy"}, {10, "xap_on_lose"}}, "", AnnotationType::NONE},
  }}},

  {  3, {"option", "option_link", false, {
    {"continue_option", {{1, "yes"}, {2, "no"}}, "", AnnotationType::NONE},
    {"target_type", option_jump_target_value_names, "", AnnotationType::NONE},
    {"target", {}, "", AnnotationType::NONE},
    {"left_prompt", {}, "", AnnotationType::NONE},
    {"right_prompt", {}, "", AnnotationType::NONE},
  }}},

  {  4, {"simple_enc", "", false, {
    {"", {}, "", AnnotationType::NONE},
  }}},

  {  5, {"complex_enc", "", false, {
    {"", {}, "", AnnotationType::NONE},
  }}},

  {  6, {"shop", "", false, {
    {"", {}, "auto_enter", AnnotationType::NONE},
  }}},

  {  7, {"modify_ap", "", false, {
    {"level", {{-2, "simple"}, {-3, "complex"}}, "", AnnotationType::NONE},
    {"id", {}, "", AnnotationType::NONE},
    {"source_xap", {}, "", AnnotationType::NONE},
    {"level_type", {{0, "same"}, {1, "land"}, {2, "dungeon"}}, "", AnnotationType::NONE},
    {"result_code", {}, "", AnnotationType::NONE},
  }}},

  {  8, {"use_ap", "", false, {
    {"level", {}, "", AnnotationType::NONE},
    {"id", {}, "", AnnotationType::NONE},
  }}},

  {  9, {"sound", "", false, {
    {"", {}, "pause", AnnotationType::NONE},
  }}},

  { 10, {"treasure", "", false, {
    {"", {}, "", AnnotationType::NONE},
  }}},

  { 11, {"victory_points", "", false, {
    {"", {}, "", AnnotationType::NONE},
  }}},

  { 12, {"change_tile", "", false, {
    {"level", {}, "", AnnotationType::NONE},
    {"x", {}, "", AnnotationType::NONE},
    {"y", {}, "", AnnotationType::NONE},
    {"new_tile", {}, "", AnnotationType::NONE},
    {"level_type", {{0, "land"}, {1, "dungeon"}}, "", AnnotationType::NONE},
  }}},

  { 13, {"enable_ap", "", false, {
    {"level", {}, "", AnnotationType::NONE},
    {"id", {}, "", AnnotationType::NONE},
    {"percent_chance", {}, "", AnnotationType::NONE},
    {"low", {}, "dungeon", AnnotationType::NONE},
    {"high", {}, "dungeon", AnnotationType::NONE},
  }}},

  { 14, {"pick_chars", "", false, {
    {"", {}, "only_conscious", AnnotationType::NONE},
  }}},

  { 15, {"heal_picked", "", false, {
    {"mult", {}, "", AnnotationType::NONE},
    {"low_range", {}, "", AnnotationType::NONE},
    {"high_range", {}, "", AnnotationType::NONE},
    {"sound", {}, "", AnnotationType::NONE},
    {"string", {}, "", AnnotationType::STRING},
  }}},

  { 16, {"heal_party", "", false, {
    {"mult", {}, "", AnnotationType::NONE},
    {"low_range", {}, "", AnnotationType::NONE},
    {"high_range", {}, "", AnnotationType::NONE},
    {"sound", {}, "", AnnotationType::NONE},
    {"string", {}, "", AnnotationType::STRING},
  }}},

  { 17, {"spell_picked", "", false, {
    {"spell", {}, "", AnnotationType::NONE},
    {"power", {}, "", AnnotationType::NONE},
    {"drv_modifier", {}, "", AnnotationType::NONE},
    {"can_drv", {{0, "yes"}, {1, "no"}}, "", AnnotationType::NONE},
  }}},

  { 18, {"spell_party", "", false, {
    {"spell", {}, "", AnnotationType::NONE},
    {"power", {}, "", AnnotationType::NONE},
    {"drv_modifier", {}, "", AnnotationType::NONE},
    {"can_drv", {{0, "yes"}, {1, "no"}}, "", AnnotationType::NONE},
  }}},

  { 19, {"rand_string", "", false, {
    {"low", {}, "", AnnotationType::STRING},
    {"high", {}, "", AnnotationType::STRING},
  }}},

  { 20, {"tele_and_run", "", false, {
    {"level", {{-1, "same"}}, "", AnnotationType::NONE},
    {"x", {{-1, "same"}}, "", AnnotationType::NONE},
    {"y", {{-1, "same"}}, "", AnnotationType::NONE},
    {"sound", {}, "", AnnotationType::NONE},
    {"string", {}, "", AnnotationType::STRING},
  }}},

  { 21, {"jmp_if_item", "jmp_if_item_link", false, {
    {"item", {}, "", AnnotationType::ITEM},
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"nonposs_action", {{0, "jump_other"}, {1, "continue"}, {2, "string_exit"}}, "", AnnotationType::NONE},
    {"target", {}, "", AnnotationType::NONE},
    {"other_target", {}, "", AnnotationType::NONE},
  }}},

  { 22, {"change_item", "", false, {
    {"item", {}, "", AnnotationType::ITEM},
    {"num", {}, "", AnnotationType::NONE},
    {"action", {{1, "drop"}, {2, "charge"}, {3, "change_type"}}, "", AnnotationType::NONE},
    {"charges", {}, "", AnnotationType::NONE},
    {"new_item", {}, "", AnnotationType::ITEM},
  }}},

  { 23, {"change_rect", "change_rect_dungeon", false, {
    {"level", {}, "", AnnotationType::NONE},
    {"id", {}, "", AnnotationType::NONE},
    {"times_in_10k", {}, "", AnnotationType::NONE},
    {"new_battle_low", {{-1, "same"}}, "", AnnotationType::NONE},
    {"new_battle_high", {{-1, "same"}}, "", AnnotationType::NONE},
  }}},

  { 24, {"exit_ap", "", false, {}}},

  { 25, {"exit_ap_delete", "", false, {}}},

  { 26, {"mouse_click", "", false, {}}},

  { 27, {"picture", "", false, {
    {"", {}, "", AnnotationType::NONE},
  }}},

  { 28, {"redraw", "", false, {}}},

  { 29, {"give_map", "", false, {
    {"", {}, "auto_show", AnnotationType::NONE},
  }}},

  { 30, {"pick_ability", "", false, {
    {"ability", {}, "choose_failure", AnnotationType::NONE},
    {"success_mod", {}, "", AnnotationType::NONE},
    {"who", {{0, "picked"}, {1, "all"}, {2, "alive"}}, "", AnnotationType::NONE},
    {"what", {{0, "special"}, {1, "attribute"}}, "", AnnotationType::NONE},
  }}},

  { 31, {"jmp_ability", "jmp_ability_link", false, {
    {"ability", {}, "choose_failure", AnnotationType::NONE},
    {"success_mod", {}, "", AnnotationType::NONE},
    {"what", {{0, "special"}, {1, "attribute"}}, "", AnnotationType::NONE},
    {"success_xap", {}, "", AnnotationType::NONE},
    {"failure_xap", {}, "", AnnotationType::NONE},
  }}},

  { 32, {"temple", "", false, {
    {"inflation_percent", {}, "", AnnotationType::NONE},
  }}},

  { 33, {"take_money", "", false, {
    {"", {}, "gems", AnnotationType::NONE},
    {"action", {{0, "cont_if_poss"}, {1, "cont_if_not_poss"}, {2, "force"}, {-1, "jmp_back_if_not_poss"}}, "", AnnotationType::NONE},
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"target", {}, "", AnnotationType::NONE},
    {"code_index", {}, "", AnnotationType::NONE},
  }}},

  { 34, {"break_enc", "", false, {}}},

  { 35, {"simple_enc_del", "", false, {
    {"", {}, "", AnnotationType::NONE},
  }}},

  { 36, {"stash_items", "", false, {
    {"", {{0, "restore"}, {1, "stash"}}, "", AnnotationType::NONE},
  }}},

  { 37, {"set_dungeon", "", false, {
    {"", {{0, "dungeon"}, {1, "land"}}, "", AnnotationType::NONE},
    {"level", {}, "", AnnotationType::NONE},
    {"x", {}, "", AnnotationType::NONE},
    {"y", {}, "", AnnotationType::NONE},
    {"dir", {{1, "north"}, {2, "east"}, {3, "south"}, {4, "west"}}, "", AnnotationType::NONE},
  }}},

  { 38, {"jmp_if_item_enc", "", false, {
    {"item", {}, "", AnnotationType::ITEM},
    {"continue", {{0, "if_poss"}, {1, "if_not_poss"}}, "", AnnotationType::NONE},
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"target", {}, "", AnnotationType::NONE},
    {"code_index", {}, "", AnnotationType::NONE},
  }}},

  { 39, {"jmp_xap", "", false, {
    {"", {}, "", AnnotationType::NONE},
  }}},

  { 40, {"jmp_party_cond", "jmp_party_cond_link", false, {
    {"jmp_cond", {{1, "if_exists"}, {2, "if_not_exists"}}, "", AnnotationType::NONE},
    {"target_type", {{0, "none"}, {1, "xap"}, {1, "simple"}, {1, "complex"}}, "", AnnotationType::NONE},
    {"target", {}, "", AnnotationType::NONE},
    {"condition", party_condition_names, "", AnnotationType::NONE},
  }}},

  { 41, {"simple_enc_del_any", "", false, {
    {"", {}, "", AnnotationType::NONE},
    {"choice", {}, "", AnnotationType::NONE},
  }}},

  { 42, {"jmp_random", "jmp_random_link", false, {
    {"percent_chance", {}, "", AnnotationType::NONE},
    {"action", jump_or_exit_actions, "", AnnotationType::NONE},
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"target", {}, "", AnnotationType::NONE},
    {"code_index", {}, "", AnnotationType::NONE},
  }}},

  { 43, {"give_cond", "", false, {
    {"who", {{0, "all"}, {1, "picked"}, {2, "alive"}}, "", AnnotationType::NONE},
    {"condition", char_condition_names, "", AnnotationType::NONE},
    {"duration", {}, "", AnnotationType::NONE},
    {"sound", {}, "", AnnotationType::NONE},
  }}},

  { 44, {"complex_enc_del", "", false, {
    {"", {}, "", AnnotationType::NONE},
  }}},

  { 45, {"tele", "", false, {
    {"level", {{-1, "same"}}, "", AnnotationType::NONE},
    {"x", {{-1, "same"}}, "", AnnotationType::NONE},
    {"y", {{-1, "same"}}, "", AnnotationType::NONE},
    {"sound", {}, "", AnnotationType::NONE},
  }}},

  { 46, {"jmp_quest", "jmp_quest_link", false, {
    {"", {}, "", AnnotationType::NONE},
    {"check", {{0, "set"}, {1, "not_set"}}, "", AnnotationType::NONE},
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"target", {}, "", AnnotationType::NONE},
    {"code_index", {}, "", AnnotationType::STRING},
  }}},

  { 47, {"set_quest", "", false, {
    {"", {}, "clear", AnnotationType::NONE},
  }}},

  { 48, {"pick_battle", "", false, {
    {"low", {}, "", AnnotationType::NONE},
    {"high", {}, "", AnnotationType::NONE},
    {"sound", {}, "", AnnotationType::NONE},
    {"string", {}, "", AnnotationType::STRING},
    {"treasure", {}, "", AnnotationType::NONE},
  }}},

  { 49, {"bank", "", false, {}}},

  { 50, {"pick_attribute", "", false, {
    {"type", {{0, "race"}, {1, "gender"}, {2, "caste"}, {3, "rase_class"}, {4, "caste_class"}}, "", AnnotationType::NONE},
    {"gender", {{1, "male"}, {2, "female"}}, "", AnnotationType::NONE},
    {"race_caste", {}, "", AnnotationType::NONE},
    {"race_caste_class", {}, "", AnnotationType::NONE},
    {"who", {{0, "all"}, {1, "alive"}}, "", AnnotationType::NONE},
  }}},

  { 51, {"change_shop", "", false, {
    {"", {}, "", AnnotationType::NONE},
    {"inflation_percent_change", {}, "", AnnotationType::NONE},
    {"item_id", {}, "", AnnotationType::ITEM},
    {"item_count", {}, "", AnnotationType::NONE},
  }}},

  { 52, {"pick_misc", "", false, {
    {"type", {{0, "move"}, {1, "position"}, {2, "item_poss"}, {3, "pct_chance"}, {4, "save_vs_attr"}, {5, "save_vs_spell_type"}, {6, "currently_selected"}, {7, "item_equipped"}, {8, "party_position"}}, "", AnnotationType::NONE},
    // TODO: parameter should have AnnotationType::ITEM if type is 2 or 7
    {"parameter", {}, "", AnnotationType::NONE},
    {"who", {{0, "all"}, {1, "alive"}, {2, "picked"}}, "", AnnotationType::NONE},
  }}},

  { 53, {"pick_caste", "", false, {
    {"caste", {}, "", AnnotationType::NONE},
    {"caste_type", {{1, "fighter"}, {2, "magical"}, {3, "monk_rogue"}}, "", AnnotationType::NONE},
    {"who", {{0, "all"}, {1, "alive"}, {2, "picked"}}, "", AnnotationType::NONE},
  }}},

  { 54, {"change_time_enc", "", false, {
    {"", {}, "", AnnotationType::NONE},
    {"percent_chance", {{-1, "same"}}, "", AnnotationType::NONE},
    {"new_day_incr", {{-1, "same"}}, "", AnnotationType::NONE},
    {"reset_to_current", {{0, "no"}, {1, "yes"}}, "", AnnotationType::NONE},
    {"days_to_next_instance", {{-1, "same"}}, "", AnnotationType::NONE},
  }}},

  { 55, {"jmp_picked", "jmp_picked_link", false, {
    {"pc_id", {{0, "any"}}, "", AnnotationType::NONE},
    {"fail_action", {{0, "exit_ap"}, {1, "xap"}, {2, "string_exit"}}, "", AnnotationType::NONE},
    {"unused", {}, "", AnnotationType::NONE},
    {"success_xap", {}, "", AnnotationType::NONE},
    {"failure_parameter", {}, "", AnnotationType::NONE},
  }}},

  { 56, {"jmp_battle", "jmp_battle_link", false, {
    {"battle_low", {}, "", AnnotationType::NONE},
    {"battle_high", {}, "", AnnotationType::NONE},
    {"loss_xap", {{-1, "back_up"}}, "", AnnotationType::NONE},
    {"sound", {}, "", AnnotationType::NONE},
    {"string", {}, "", AnnotationType::STRING},
  }}},

  { 57, {"change_tileset", "", false, {
    {"new_tileset", {}, "", AnnotationType::NONE},
    {"dark", {{0, "no"}, {1, "yes"}}, "", AnnotationType::NONE},
    {"level", {}, "", AnnotationType::NONE},
  }}},

  { 58, {"jmp_difficulty", "jmp_difficulty_link", false, {
    {"difficulty", {{1, "novice"}, {2, "easy"}, {3, "normal"}, {4, "hard"}, {5, "veteran"}}, "", AnnotationType::NONE},
    {"action", jump_or_exit_actions, "", AnnotationType::NONE},
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"target", {}, "", AnnotationType::NONE},
    {"code_index", {}, "", AnnotationType::NONE},
  }}},

  { 59, {"jmp_tile", "jmp_tile_link", false, {
    {"tile", {}, "", AnnotationType::NONE},
    {"action", jump_or_exit_actions, "", AnnotationType::NONE},
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"target", {}, "", AnnotationType::NONE},
    {"code_index", {}, "", AnnotationType::NONE},
  }}},

  { 60, {"drop_all_money", "", false, {
    {"type", {{1, "gold"}, {2, "gems"}, {3, "jewelry"}}, "", AnnotationType::NONE},
    {"who", {{0, "all"}, {1, "picked"}}, "", AnnotationType::NONE},
  }}},

  { 61, {"incr_party_loc", "", false, {
    {"unused", {}, "", AnnotationType::NONE},
    {"x", {}, "", AnnotationType::NONE},
    {"y", {}, "", AnnotationType::NONE},
    {"move_type", {{0, "exact"}, {1, "random"}}, "", AnnotationType::NONE},
  }}},

  { 62, {"story", "", false, {
    {"", {}, "", AnnotationType::NONE},
  }}},

  { 63, {"change_time", "", false, {
    {"base", {{1, "absolute"}, {2, "relative"}}, "", AnnotationType::NONE},
    {"days", {{-1, "same"}}, "", AnnotationType::NONE},
    {"hours", {{-1, "same"}}, "", AnnotationType::NONE},
    {"minutes", {{-1, "same"}}, "", AnnotationType::NONE},
  }}},

  { 64, {"jmp_time", "jmp_time_link", false, {
    {"day", {{-1, "any"}}, "", AnnotationType::NONE},
    {"hour", {{-1, "any"}}, "", AnnotationType::NONE},
    {"unused", {}, "", AnnotationType::NONE},
    {"before_equal_xap", {}, "", AnnotationType::NONE},
    {"after_xap", {}, "", AnnotationType::NONE},
  }}},

  { 65, {"give_rand_item", "", false, {
    {"count", {}, "random", AnnotationType::NONE},
    {"item_low", {}, "", AnnotationType::ITEM},
    {"item_high", {}, "", AnnotationType::ITEM},
  }}},

  { 66, {"allow_camping", "", false, {
    {"", {{0, "enable"}, {1, "disable"}}, "", AnnotationType::NONE},
  }}},

  { 67, {"jmp_item_charge", "jmp_item_charge_link", false, {
    {"", {}, "", AnnotationType::ITEM},
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"min_charges", {}, "", AnnotationType::NONE},
    {"target_if_enough", {{-1, "continue"}}, "", AnnotationType::NONE},
    {"target_if_not_enough", {{-1, "continue"}}, "", AnnotationType::NONE},
  }}},

  { 68, {"change_fatigue", "", false, {
    {"", {{1, "set_full"}, {2, "set_empty"}, {3, "modify"}}, "", AnnotationType::NONE},
    {"factor_percent", {}, "", AnnotationType::NONE},
  }}},

  { 69, {"change_casting_flags", "", false, {
    {"enable_char_casting", {{0, "yes"}, {1, "no"}}, "", AnnotationType::NONE},
    {"enable_npc_casting", {{0, "yes"}, {1, "no"}}, "", AnnotationType::NONE},
    {"enable_recharging", {{0, "yes"}, {1, "no"}}, "", AnnotationType::NONE},
    // Note: apparently e-code 4 isn't used and 5 must always be 1. We don't
    // enforce this for a disassembly though
  }}},

  { 70, {"save_restore_loc", "", true, {
    {"", {{1, "save"}, {2, "restore"}}, "", AnnotationType::NONE},
  }}},

  { 71, {"enable_coord_display", "", false, {
    {"", {{0, "enable"}, {1, "disable"}}, "", AnnotationType::NONE},
  }}},

  { 72, {"jmp_quest_range", "jmp_quest_range_link", false, {
    {"quest_low", {}, "", AnnotationType::NONE},
    {"quest_high", {}, "", AnnotationType::NONE},
    {"unused", {}, "", AnnotationType::NONE},
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"target", {}, "", AnnotationType::NONE},
  }}},

  { 73, {"shop_restrict", "", false, {
    {"", {}, "auto_enter", AnnotationType::NONE},
    {"item_low1", {}, "", AnnotationType::ITEM},
    {"item_high1", {}, "", AnnotationType::ITEM},
    {"item_low2", {}, "", AnnotationType::ITEM},
    {"item_high2", {}, "", AnnotationType::ITEM},
  }}},

  { 74, {"give_spell_pts_picked", "", false, {
    {"mult", {}, "", AnnotationType::NONE},
    {"pts_low", {}, "", AnnotationType::NONE},
    {"pts_high", {}, "", AnnotationType::NONE},
  }}},

  { 75, {"jmp_spell_pts", "jmp_spell_pts_link", false, {
    {"who", {{1, "picked"}, {2, "alive"}}, "", AnnotationType::NONE},
    {"min_pts", {}, "", AnnotationType::NONE},
    {"fail_action", {{0, "continue"}, {1, "exit_ap"}}, "", AnnotationType::NONE},
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"target", {}, "", AnnotationType::NONE},
  }}},

  { 76, {"incr_quest_value", "", false, {
    {"", {}, "", AnnotationType::NONE},
    {"incr", {}, "", AnnotationType::NONE},
    {"target_type", {{0, "none"}, {1, "xap"}, {2, "simple"}, {3, "complex"}}, "", AnnotationType::NONE},
    {"jump_min_value", {}, "", AnnotationType::NONE},
    {"target", {}, "", AnnotationType::NONE},
  }}},

  { 77, {"jmp_quest_value", "jmp_quest_value_link", false, {
    {"", {}, "", AnnotationType::NONE},
    {"value", {}, "", AnnotationType::NONE},
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"target_less", {{0, "continue"}}, "", AnnotationType::NONE},
    {"target_equal_greater", {{0, "continue"}}, "", AnnotationType::NONE},
  }}},

  { 78, {"jmp_tile_params", "jmp_tile_params_link", false, {
    {"attr", {{1, "shoreline"}, {2, "is_needs_boat"}, {3, "path"}, {4, "blocks_los"}, {5, "need_fly_float"}, {6, "special"}, {7, "tile_id"}}, "", AnnotationType::NONE},
    {"tile_id", {}, "", AnnotationType::NONE},
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"target_false", {{0, "continue"}}, "", AnnotationType::NONE},
    {"target_true", {{0, "continue"}}, "", AnnotationType::NONE},
  }}},

  { 81, {"jmp_char_cond", "jmp_char_cond_link", false, {
    {"cond", {}, "", AnnotationType::NONE},
    {"who", {{-1, "picked"}, {0, "party"}}, "", AnnotationType::NONE},
    {"fail_string", {}, "", AnnotationType::STRING},
    {"success_xap", {}, "", AnnotationType::NONE},
    {"failure_xap", {}, "", AnnotationType::NONE},
  }}},

  { 82, {"enable_turning", "", false, {}}},

  { 83, {"disable_turning", "", false, {}}},

  { 84, {"check_scen_registered", "", false, {}}},

  { 85, {"jmp_random_xap", "jmp_random_xap_link", false, {
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"target_low", {}, "", AnnotationType::NONE},
    {"target_high", {}, "", AnnotationType::NONE},
    {"sound", {}, "", AnnotationType::NONE},
    {"string", {}, "", AnnotationType::STRING},
  }}},

  { 86, {"jmp_misc", "jmp_misc_link", false, {
    {"", {{0, "caste_present"}, {1, "race_present"}, {2, "gender_present"}, {3, "in_boat"}, {4, "camping"}, {5, "caste_class_present"}, {6, "race_class_present"}, {7, "total_party_levels"}, {8, "picked_char_levels"}}, "", AnnotationType::NONE},
    {"value", {}, "picked_only", AnnotationType::NONE},
    {"target_type", jump_target_value_names, "", AnnotationType::NONE},
    {"target_true", {{0, "continue"}}, "", AnnotationType::NONE},
    {"target_false", {{0, "continue"}}, "", AnnotationType::NONE},
  }}},

  { 87, {"jmp_npc", "jmp_npc_link", false, {
    {"", {}, "", AnnotationType::NONE},
    {"target_type", jump_target_value_names, "picked_only", AnnotationType::NONE},
    {"fail_action", {{0, "jmp_other"}, {1, "continue"}, {2, "string_exit"}}, "", AnnotationType::NONE},
    {"target", {}, "", AnnotationType::NONE},
    {"other_param", {}, "", AnnotationType::NONE},
  }}},

  { 88, {"drop_npc", "", false, {
    {"", {}, "", AnnotationType::NONE},
  }}},

  { 89, {"add_npc", "", false, {
    {"", {}, "", AnnotationType::NONE},
  }}},

  { 90, {"take_victory_pts", "", false, {
    {"", {}, "", AnnotationType::NONE},
    {"who", {{0, "each"}, {1, "picked"}, {2, "total"}}, "", AnnotationType::NONE},
  }}},

  { 91, {"drop_all_items", "", false, {}}},

  { 92, {"change_rect_size", "", false, {
    {"level", {}, "", AnnotationType::NONE},
    {"rect", {}, "", AnnotationType::NONE},
    {"level_type", {{0, "land"}, {1, "dungeon"}}, "", AnnotationType::NONE},
    {"times_in_10k_mult", {}, "", AnnotationType::NONE},
    {"action", {{-1, "none"}, {0, "set_coords"}, {1, "offset"}, {2, "resize"}, {3, "warp"}}, "", AnnotationType::NONE},
    {"left_h", {}, "", AnnotationType::NONE},
    {"right_v", {}, "", AnnotationType::NONE},
    {"top", {}, "", AnnotationType::NONE},
    {"bottom", {}, "", AnnotationType::NONE},
  }}},

  { 93, {"enable_compass", "", false, {}}},

  { 94, {"disable_compass", "", false, {}}},

  { 95, {"change_dir", "", false, {
    {"", {{-1, "random"}, {1, "north"}, {2, "east"}, {3, "south"}, {4, "west"}}, "", AnnotationType::NONE},
  }}},

  { 96, {"disable_dungeon_map", "", false, {}}},

  { 97, {"enable_dungeon_map", "", false, {}}},

  { 98, {"require_registration", "", false, {}}},

  { 99, {"get_registration", "", false, {}}},

  {100, {"end_battle", "", false, {}}},

  {101, {"back_up", "", false, {}}},

  {102, {"level_up_picked", "", false, {}}},

  {103, {"cont_boat_camping", "", false, {
    {"if_boat", {{1, "true"}, {2, "false"}}, "", AnnotationType::NONE},
    {"if_camping", {{1, "true"}, {2, "false"}}, "", AnnotationType::NONE},
    {"set_boat", {{1, "true"}, {2, "false"}}, "", AnnotationType::NONE},
  }}},

  {104, {"enable_random_battles", "", false, {
    {"", {{0, "false"}, {1, "true"}}, "", AnnotationType::NONE},
  }}},

  {105, {"enable_allies", "", false, {
    {"", {{1, "false"}, {2, "true"}}, "", AnnotationType::NONE},
  }}},

  {106, {"set_dark_los", "", false, {
    {"dark", {{1, "false"}, {2, "true"}}, "", AnnotationType::NONE},
    {"skip_if_dark_same", {{0, "false"}, {1, "true"}}, "", AnnotationType::NONE},
    {"los", {{1, "true"}, {2, "false"}}, "", AnnotationType::NONE},
    {"skip_if_los_same", {{0, "false"}, {1, "true"}}, "", AnnotationType::NONE},
  }}},

  {107, {"pick_battle_2", "", false, {
    {"battle_low", {}, "", AnnotationType::NONE},
    {"battle_high", {}, "", AnnotationType::NONE},
    {"sound", {}, "", AnnotationType::NONE},
    {"loss_xap", {}, "", AnnotationType::NONE},
  }}},

  {108, {"change_picked", "", false, {
    {"what", {{1, "attacks_round"}, {2, "spells_round"}, {3, "movement"}, {4, "damage"}, {5, "spell_pts"}, {6, "hand_to_hand"}, {7, "stamina"}, {8, "armor_rating"}, {9, "to_hit"}, {10, "missile_adjust"}, {11, "magic_resistance"}, {12, "prestige"}}, "", AnnotationType::NONE},
    {"count", {}, "", AnnotationType::NONE},
  }}},

  {111, {"ret", "", false, {}}},

  {112, {"pop", "", false, {}}},

  {119, {"revive_npc_after", "", false, {}}},

  {120, {"change_monster", "", false, {
    {"", {{1, "npc"}, {2, "monster"}}, "", AnnotationType::NONE},
    {"", {}, "", AnnotationType::NONE},
    {"count", {}, "", AnnotationType::NONE},
    {"new_icon", {}, "", AnnotationType::NONE},
    {"new_traitor", {{-1, "same"}}, "", AnnotationType::NONE},
  }}},

  {121, {"kill_lower_undead", "", false, {}}},

  {122, {"fumble_weapon", "", false, {
    {"string", {}, "", AnnotationType::STRING},
    {"sound", {}, "", AnnotationType::NONE},
  }}},

  {123, {"rout_monsters", "", false, {
    {"", {}, "", AnnotationType::NONE},
    {"", {}, "", AnnotationType::NONE},
    {"", {}, "", AnnotationType::NONE},
    {"", {}, "", AnnotationType::NONE},
    {"", {}, "", AnnotationType::NONE},
  }}},

  {124, {"summon_monsters", "", false, {
    {"type", {{0, "individual"}}, "", AnnotationType::NONE},
    {"", {}, "", AnnotationType::NONE},
    {"count", {}, "", AnnotationType::NONE},
    {"sound", {}, "", AnnotationType::NONE},
  }}},

  {125, {"destroy_related", "", false, {
    {"", {}, "", AnnotationType::NONE},
    {"count", {{0, "all"}}, "", AnnotationType::NONE},
    {"unused", {}, "", AnnotationType::NONE},
    {"unused", {}, "", AnnotationType::NONE},
    {"force", {{0, "false"}, {1, "true"}}, "", AnnotationType::NONE},
  }}},

  {126, {"macro_criteria", "", false, {
    {"when", {{0, "round_number"}, {1, "percent_chance"}, {2, "flee_fail"}}, "", AnnotationType::NONE},
    {"round_percent_chance", {}, "", AnnotationType::NONE},
    {"repeat", {{0, "none"}, {1, "each_round"}, {2, "jmp_random"}}, "", AnnotationType::NONE},
    {"xap_low", {}, "", AnnotationType::NONE},
    {"xap_high", {}, "", AnnotationType::NONE},
  }}},

  {127, {"cont_monster_present", "", false, {
    {"", {}, "", AnnotationType::NONE},
  }}},
});
// clang-format on

string RealmzScenarioData::disassemble_opcode(int16_t ap_code, int16_t arg_code) {
  int16_t opcode = abs(ap_code);
  if (opcode_definitions.count(opcode) == 0) {
    size_t ecodes_id = abs(arg_code);
    if (ecodes_id >= this->ecodes.size()) {
      return string_printf("[%hd %hd]", ap_code, arg_code);
    }
    return string_printf("[%hd %hd [%hd %hd %hd %hd %hd]]", ap_code, arg_code,
        this->ecodes[ecodes_id].data[0].load(),
        this->ecodes[ecodes_id].data[1].load(),
        this->ecodes[ecodes_id].data[2].load(),
        this->ecodes[ecodes_id].data[3].load(),
        this->ecodes[ecodes_id].data[4].load());
  }

  OpcodeInfo op = opcode_definitions.at(opcode);
  string op_name = (ap_code < 0 ? op.negative_name : op.name);
  if (op.args.size() == 0) {
    return op_name;
  }

  vector<int16_t> arguments;
  if (op.args.size() == 1 && !op.always_use_ecodes) {
    arguments.push_back(arg_code);

  } else {
    if (arg_code < 0) {
      op_name = op.negative_name;
      arg_code *= -1;
    }

    if ((size_t)arg_code >= ecodes.size()) {
      return string_printf("%-24s [bad ecode id %04X]", op_name.c_str(), arg_code);
    }
    if ((op.args.size() > 5) && ((size_t)arg_code >= ecodes.size() - 1)) {
      return string_printf("%-24s [bad 2-ecode id %04X]", op_name.c_str(), arg_code);
    }

    for (size_t x = 0; x < op.args.size(); x++) {
      arguments.push_back(ecodes[arg_code].data[x]); // Intentional overflow (x)
    }
  }

  string ret = string_printf("%-24s ", op_name.c_str());
  for (size_t x = 0; x < arguments.size(); x++) {
    if (x > 0) {
      ret += ", ";
    }

    string pfx = op.args[x].arg_name.empty() ? "" : (op.args[x].arg_name + "=");

    int16_t value = arguments[x];
    bool use_negative_modifier = false;
    if (value < 0 && !op.args[x].negative_modifier.empty()) {
      use_negative_modifier = true;
      value *= -1;
    }

    if (op.args[x].value_names.count(value)) {
      ret += string_printf("%s%s", pfx.c_str(),
          op.args[x].value_names.at(value).c_str());
    } else if (op.args[x].annotation_type == AnnotationType::STRING) {
      string string_value = render_string_reference(strings, value);
      ret += string_printf("%s%s", pfx.c_str(),
          string_value.c_str());
    } else if (op.args[x].annotation_type == AnnotationType::ITEM) {
      try {
        const auto& info = this->info_for_item(value);
        ret += string_printf("%d(%s)", value, info.name.c_str());
      } catch (const out_of_range&) {
        ret += string_printf("%d", value);
      }
    } else if (op.args[x].annotation_type == AnnotationType::SPELL) {
      try {
        const string& name = this->name_for_spell(value);
        ret += string_printf("%d(%s)", value, name.c_str());
      } catch (const out_of_range&) {
        ret += string_printf("%d", value);
      }
    } else {
      ret += string_printf("%s%hd", pfx.c_str(), value);
    }

    if (use_negative_modifier) {
      ret += (", " + op.args[x].negative_modifier);
    }
  }

  return ret;
}

string RealmzScenarioData::disassemble_xap(int16_t ap_num) {
  const auto& ap = this->xaps.at(ap_num);

  string data = string_printf("===== XAP id=%d [XAP%d]\n", ap_num, ap_num);

  // TODO: eliminate code duplication here
  for (size_t x = 0; x < land_metadata.size(); x++) {
    for (size_t y = 0; y < land_metadata[x].random_rects.size(); y++) {
      const auto& r = land_metadata[x].random_rects[y];
      if (r.xap_num[0] == ap_num ||
          r.xap_num[1] == ap_num ||
          r.xap_num[2] == ap_num) {
        data += string_printf("RANDOM RECTANGLE REFERENCE land_level=%zu rect_num=%zu start_coord=%d,%d end_coord=%d,%d [LRR%zu/%zu]\n",
            x, y, r.left, r.top, r.right, r.bottom, x, y);
      }
    }
  }
  for (size_t x = 0; x < dungeon_metadata.size(); x++) {
    for (size_t y = 0; y < dungeon_metadata[x].random_rects.size(); y++) {
      const auto& r = dungeon_metadata[x].random_rects[y];
      if (r.xap_num[0] == ap_num ||
          r.xap_num[1] == ap_num ||
          r.xap_num[2] == ap_num) {
        data += string_printf("RANDOM RECTANGLE REFERENCE dungeon_level=%zu rect_num=%zu start_coord=%d,%d end_coord=%d,%d [DRR%zu/%zu]\n",
            x, y, r.left, r.top, r.right, r.bottom, x, y);
      }
    }
  }

  for (size_t x = 0; x < 8; x++) {
    if (ap.command_codes[x] || ap.argument_codes[x]) {
      string dasm = disassemble_opcode(ap.command_codes[x], ap.argument_codes[x]);
      data += string_printf("  %s\n", dasm.c_str());
    }
  }

  return data;
}

string RealmzScenarioData::disassemble_all_xaps() {
  string ret;
  for (size_t x = 0; x < this->xaps.size(); x++) {
    ret += this->disassemble_xap(x);
  }
  return ret;
}

string RealmzScenarioData::disassemble_level_ap(
    int16_t level_num, int16_t ap_num, bool dungeon) {
  const auto& ap = (dungeon ? this->dungeon_aps : this->land_aps).at(level_num).at(ap_num);

  if (ap.get_x() < 0 || ap.get_y() < 0) {
    return "";
  }

  string extra;
  if (ap.to_level != level_num || ap.to_x != ap.get_x() || ap.to_y != ap.get_y()) {
    extra = string_printf(" to_level=%d to_x=%d to_y=%d", ap.to_level, ap.to_x,
        ap.to_y);
  }
  if (ap.percent_chance != 100) {
    extra += string_printf(" prob=%d", ap.percent_chance);
  }
  string data = string_printf("===== %s AP level=%d id=%d x=%d y=%d%s [%cAP%d/%d]\n",
      (dungeon ? "DUNGEON" : "LAND"), level_num, ap_num, ap.get_x(), ap.get_y(),
      extra.c_str(), (dungeon ? 'D' : 'L'), level_num, ap_num);

  for (size_t x = 0; x < 8; x++) {
    if (ap.command_codes[x] || ap.argument_codes[x]) {
      string dasm = this->disassemble_opcode(ap.command_codes[x], ap.argument_codes[x]);
      data += string_printf("  %s\n", dasm.c_str());
    }
  }

  return data;
}

string RealmzScenarioData::disassemble_level_aps(int16_t level_num, bool dungeon) {
  string ret;
  size_t count = (dungeon ? this->dungeon_aps : this->land_aps).at(level_num).size();
  for (size_t x = 0; x < count; x++) {
    ret += this->disassemble_level_ap(level_num, x, dungeon);
  }
  return ret;
}

string RealmzScenarioData::disassemble_all_level_aps(bool dungeon) {
  string ret;
  size_t count = (dungeon ? this->dungeon_aps : this->land_aps).size();
  for (size_t x = 0; x < count; x++) {
    ret += this->disassemble_level_aps(x, dungeon);
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
// DATA DL

static uint16_t location_sig(uint8_t x, uint8_t y) {
  return ((uint16_t)x << 8) | y;
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

vector<RealmzScenarioData::MapData> RealmzScenarioData::load_dungeon_map_index(
    const string& filename) {
  return load_vector_file<MapData>(filename);
}

Image RealmzScenarioData::generate_dungeon_map(int16_t level_num, uint8_t x0,
    uint8_t y0, uint8_t w, uint8_t h) {
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
  constexpr uint16_t battle_blank_tile_flag = 0x2000;

  if ((x0 >= 90) || (y0 >= 90) || ((x0 + w) > 90) || ((y0 + h) > 90)) {
    throw runtime_error("map bounds out of range");
  }

  Image map(w * 16, h * 16);
  size_t pattern_x = 576, pattern_y = 320;

  unordered_map<uint16_t, vector<int>> loc_to_ap_nums;
  for (size_t x = 0; x < aps.size(); x++) {
    loc_to_ap_nums[location_sig(aps[x].get_x(), aps[x].get_y())].push_back(x);
  }

  Image dungeon_pattern = this->global.global_rsf.decode_PICT(302).image;

  for (ssize_t y = y0 + h - 1; y >= y0; y--) {
    for (ssize_t x = x0 + w - 1; x >= x0; x--) {
      int16_t data = mdata.data[y][x];

      size_t xp = (x - x0) * 16;
      size_t yp = (y - y0) * 16;
      map.fill_rect(xp, yp, 16, 16, 0x000000FF);
      if (data & wall_tile_flag) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 0,
            pattern_y + 0, 0xFFFFFFFF);
      }
      if (data & vert_door_tile_flag) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 16,
            pattern_y + 0, 0xFFFFFFFF);
      }
      if (data & horiz_door_tile_flag) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 32,
            pattern_y + 0, 0xFFFFFFFF);
      }
      if (data & stairs_tile_flag) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 48,
            pattern_y + 0, 0xFFFFFFFF);
      }
      if (data & columns_tile_flag) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 0,
            pattern_y + 16, 0xFFFFFFFF);
      }
      if (data & secret_up_tile_flag) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 0,
            pattern_y + 32, 0xFFFFFFFF);
      }
      if (data & secret_right_tile_flag) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 16,
            pattern_y + 32, 0xFFFFFFFF);
      }
      if (data & secret_down_tile_flag) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 32,
            pattern_y + 32, 0xFFFFFFFF);
      }
      if (data & secret_left_tile_flag) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 48,
            pattern_y + 32, 0xFFFFFFFF);
      }

      if (data & has_ap_tile_flag) {
        map.draw_horizontal_line(xp, xp + 15, yp, 0, 0xFF0000FF);
        map.draw_horizontal_line(xp, xp + 15, yp + 15, 0, 0xFF0000FF);
        map.draw_vertical_line(xp, yp, yp + 15, 0, 0xFF0000FF);
        map.draw_vertical_line(xp + 15, yp, yp + 15, 0, 0xFF0000FF);
      }
      if (data & battle_blank_tile_flag) {
        map.draw_horizontal_line(xp, xp + 15, yp + 7, 0, 0x00FFFFFF);
        map.draw_horizontal_line(xp, xp + 15, yp + 8, 0, 0x00FFFFFF);
        map.draw_vertical_line(xp + 7, yp, yp + 15, 0, 0x00FFFFFF);
        map.draw_vertical_line(xp + 8, yp, yp + 15, 0, 0x00FFFFFF);
      }

      size_t text_xp = xp + 1;
      size_t text_yp = yp + 1;

      // Draw the coords if both are multiples of 10
      if (y % 10 == 0 && x % 10 == 0) {
        map.draw_text(text_xp, text_yp, 0xFF00FFFF, 0x00000080, "%zu,%zu", x, y);
        text_yp += 8;
      }

      // TODO: we intentionally don't include the DAP%d token here because
      // dungeon tiles are only 16x16, which really only leaves room for two
      // digits. We could fix this by scaling up the tileset to 32x32, but I'm
      // lazy.
      for (const auto& ap_num : loc_to_ap_nums[location_sig(x, y)]) {
        if (aps[ap_num].percent_chance < 100) {
          map.draw_text(text_xp, text_yp, 0xFFFFFFFF, 0x00000080, "%d-%hhu%%",
              ap_num, aps[ap_num].percent_chance);
        } else {
          map.draw_text(text_xp, text_yp, 0xFFFFFFFF, 0x00000080, "%d",
              ap_num);
        }
        text_yp += 8;
      }
    }
  }

  // Finally, draw random rects
  draw_random_rects(map, metadata.random_rects, 0, 0, true, level_num,
      x0, y0, w, h);

  return map;
}

////////////////////////////////////////////////////////////////////////////////
// DATA LD

vector<RealmzScenarioData::MapData> RealmzScenarioData::load_land_map_index(
    const string& filename) {
  // Format is the same as for dungeons, except it's in column-major order
  vector<MapData> data = load_dungeon_map_index(filename);
  for (auto& m : data) {
    m.transpose();
  }

  return data;
}

unordered_set<string> RealmzScenarioData::all_land_types() {
  unordered_set<string> all;
  for (const auto& it : this->land_type_to_tileset_definition) {
    all.emplace(it.first);
  }
  for (const auto& it : this->global.land_type_to_tileset_definition) {
    all.emplace(it.first);
  }
  return all;
}

Image RealmzScenarioData::generate_land_map(
    int16_t level_num,
    uint8_t x0,
    uint8_t y0,
    uint8_t w,
    uint8_t h,
    unordered_set<int16_t>* used_negative_tiles,
    unordered_map<string, unordered_set<uint8_t>>* used_positive_tiles) {
  const auto& mdata = this->land_maps.at(level_num);
  const auto& metadata = this->land_metadata.at(level_num);
  const auto& aps = this->land_aps.at(level_num);

  unordered_set<uint8_t>* used_positive_tiles_for_land_type = nullptr;
  if (used_positive_tiles) {
    used_positive_tiles_for_land_type = &(*used_positive_tiles)[metadata.land_type];
  }

  LevelNeighbors n;
  if (x0 == 0 && y0 == 0 && w == 90 && h == 90) {
    try {
      n = this->layout.get_level_neighbors(level_num);
    } catch (const runtime_error& e) {
      fprintf(stderr, "warning: can\'t get neighbors for level (%s)\n", e.what());
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
  for (size_t x = 0; x < aps.size(); x++) {
    loc_to_ap_nums[location_sig(aps[x].get_x(), aps[x].get_y())].push_back(x);
  }

  size_t horizontal_neighbors = (n.left != -1 ? 1 : 0) + (n.right != -1 ? 1 : 0);
  size_t vertical_neighbors = (n.top != -1 ? 1 : 0) + (n.bottom != -1 ? 1 : 0);

  const TileSetDefinition* tileset;
  try {
    tileset = &this->land_type_to_tileset_definition.at(metadata.land_type);
  } catch (const out_of_range&) {
    tileset = &this->global.land_type_to_tileset_definition.at(metadata.land_type);
  }

  Image map(w * 32 + horizontal_neighbors * 9, h * 32 + vertical_neighbors * 9);

  // Write neighbor directory
  if (n.left != -1) {
    string text = string_printf("TO LEVEL %d", n.left);
    for (size_t y = (n.top != -1 ? 10 : 1); y < h * 32; y += 10 * 32) {
      for (size_t yy = 0; yy < text.size(); yy++) {
        map.draw_text(2, y + 9 * yy, 0xFFFFFFFF, 0x000000FF, "%c", text[yy]);
      }
    }
  }
  if (n.right != -1) {
    string text = string_printf("TO LEVEL %d", n.right);
    size_t x = 32 * 90 + (n.left != -1 ? 11 : 2);
    for (size_t y = (n.top != -1 ? 10 : 1); y < h * 32; y += 10 * 32) {
      for (size_t yy = 0; yy < text.size(); yy++) {
        map.draw_text(x, y + 9 * yy, 0xFFFFFFFF, 0x000000FF, "%c", text[yy]);
      }
    }
  }
  if (n.top != -1) {
    string text = string_printf("TO LEVEL %d", n.top);
    for (size_t x = (n.left != -1 ? 10 : 1); x < w * 32; x += 10 * 32) {
      map.draw_text(x, 1, 0xFFFFFFFF, 0x000000FF, "%s", text.c_str());
    }
  }
  if (n.bottom != -1) {
    string text = string_printf("TO LEVEL %d", n.bottom);
    size_t y = 32 * 90 + (n.top != -1 ? 10 : 1);
    for (size_t x = (n.left != -1 ? 10 : 1); x < w * 32; x += 10 * 32) {
      map.draw_text(x, y, 0xFFFFFFFF, 0x000000FF, "%s", text.c_str());
    }
  }

  // Load the positive pattern
  int16_t resource_id = resource_id_for_land_type(metadata.land_type);
  Image positive_pattern =
      this->scenario_rsf.resource_exists(RESOURCE_TYPE_PICT, resource_id)
      ? this->scenario_rsf.decode_PICT(resource_id).image
      : this->global.global_rsf.decode_PICT(resource_id).image;

  for (size_t y = y0; y < y0 + h; y++) {
    for (size_t x = x0; x < x0 + w; x++) {
      int16_t data = mdata.data[y][x];
      while (data <= -1000) {
        data += 1000;
      }
      while (data > 1000) {
        data -= 1000;
      }

      size_t xp = (x - x0) * 32 + (n.left != -1 ? 9 : 0);
      size_t yp = (y - y0) * 32 + (n.top != -1 ? 9 : 0);

      // Draw the tile itself
      if ((data < 0) || (data > 200)) { // Masked tile
        if (used_negative_tiles) {
          used_negative_tiles->emplace(data);
        }

        Image cicn;
        if (this->scenario_rsf.resource_exists(RESOURCE_TYPE_cicn, data)) {
          cicn = this->scenario_rsf.decode_cicn(data).image;
        } else if (this->global.global_rsf.resource_exists(RESOURCE_TYPE_cicn, data)) {
          cicn = this->global.global_rsf.decode_cicn(data).image;
        }

        // If neither cicn was valid, draw an error tile
        if (cicn.get_width() == 0 || cicn.get_height() == 0) {
          map.fill_rect(xp, yp, 32, 32, 0x000000FF);
          map.draw_text(xp + 2, yp + 30 - 9, 0xFFFFFFFF, 0x000000FF, "%04hX", data);

        } else {
          if (tileset->base_tile_id) {
            size_t source_id = tileset->base_tile_id - 1;
            size_t sxp = (source_id % 20) * 32;
            size_t syp = (source_id / 20) * 32;
            map.blit(positive_pattern, xp, yp, 32, 32, sxp, syp);
          } else {
            map.fill_rect(xp, yp, 32, 32, 0x000000FF);
          }

          // Negative tile images may be >32px in either dimension, and are
          // anchored at the lower-right corner, so we have to adjust the
          // destination x/y appropriately
          map.blit(
              cicn,
              xp - (cicn.get_width() - 32),
              yp - (cicn.get_height() - 32),
              cicn.get_width(),
              cicn.get_height(),
              0,
              0);
        }

      } else if (data <= 200) { // Standard tile
        if (used_positive_tiles_for_land_type) {
          used_positive_tiles_for_land_type->emplace(data);
        }

        size_t source_id = data - 1;
        size_t sxp = (source_id % 20) * 32;
        size_t syp = (source_id / 20) * 32;
        map.blit(positive_pattern, xp, yp, 32, 32, sxp, syp);

        // If it's a path, shade it red
        if (tileset->tiles[data].is_path) {
          map.fill_rect(xp, yp, 32, 32, 0xFF000040);
        }
      }
    }
  }

  // This is a separate loop so we can draw APs that are hidden by large
  // negative tile overlays
  for (size_t y = y0; y < y0 + h; y++) {
    for (size_t x = x0; x < x0 + w; x++) {

      size_t xp = (x - x0) * 32 + (n.left != -1 ? 9 : 0);
      size_t yp = (y - y0) * 32 + (n.top != -1 ? 9 : 0);

      int16_t data = mdata.data[y][x];
      bool has_ap = ((data <= -1000) || (data > 1000));
      bool ap_is_secret = ((data <= -3000) || (data > 3000));
      size_t text_xp = xp + 2;
      size_t text_yp = yp + 2;

      // Draw a red border if it has an AP, and make it dashed if the AP is
      // secret
      if (has_ap && ap_is_secret) {
        map.draw_horizontal_line(xp, xp + 31, yp, 4, 0xFF0000FF);
        map.draw_horizontal_line(xp, xp + 31, yp + 31, 4, 0xFF0000FF);
        map.draw_vertical_line(xp, yp, yp + 31, 4, 0xFF0000FF);
        map.draw_vertical_line(xp + 31, yp, yp + 31, 4, 0xFF0000FF);
      } else if (has_ap) {
        map.draw_horizontal_line(xp, xp + 31, yp, 0, 0xFF0000FF);
        map.draw_horizontal_line(xp, xp + 31, yp + 31, 0, 0xFF0000FF);
        map.draw_vertical_line(xp, yp, yp + 31, 0, 0xFF0000FF);
        map.draw_vertical_line(xp + 31, yp, yp + 31, 0, 0xFF0000FF);
      }

      // Draw the coords if both are multiples of 10
      if (y % 10 == 0 && x % 10 == 0) {
        map.draw_text(text_xp, text_yp, 0xFF00FFFF, 0x00000080, "%zu,%zu", x, y);
        text_yp += 8;
      }

      // Draw "START" if this is the start loc
      if (x == static_cast<size_t>(start_x) && y == static_cast<size_t>(start_y)) {
        map.draw_text(text_xp, text_yp, 0x00FFFFFF, 0x00000080, "START");
        text_yp += 8;
      }

      // Draw APs if present
      for (const auto& ap_num : loc_to_ap_nums[location_sig(x, y)]) {
        if (aps[ap_num].percent_chance < 100) {
          map.draw_text(text_xp, text_yp, 0xFFFFFFFF, 0x00000080, "%d-%d",
              ap_num, aps[ap_num].percent_chance);
        } else {
          map.draw_text(text_xp, text_yp, 0xFFFFFFFF, 0x00000080, "%d", ap_num);
        }
        text_yp += 8;
      }
    }
  }

  // Finally, draw random rects
  draw_random_rects(map, metadata.random_rects, (n.left != -1 ? 9 : 0),
      (n.top != -1 ? 9 : 0), false, level_num, x0, y0, w, h);

  return map;
}

////////////////////////////////////////////////////////////////////////////////
// DATA SD2

vector<string> RealmzScenarioData::load_string_index(const string& filename) {
  auto f = fopen_unique(filename.c_str(), "rb");

  vector<string> all_strings;
  while (!feof(f.get())) {

    string s;
    uint8_t len;
    size_t x;
    len = fgetc(f.get());
    for (x = 0; x < len; x++) {
      s += fgetc(f.get());
    }
    for (; x < 0xFF; x++) {
      fgetc(f.get());
    }

    all_strings.push_back(s);
  }

  return all_strings;
}
