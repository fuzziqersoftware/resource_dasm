#include "RealmzGlobalData.hh"

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

string first_file_that_exists(const vector<string>& names) {
  for (const auto& it : names) {
    struct stat st;
    if (stat(it.c_str(), &st) == 0) {
      return it;
    }
  }
  return "";
}

TileSetDefinition load_tileset_definition(const string& filename) {
  return load_object_file<TileSetDefinition>(filename, true);
}

RealmzGlobalData::RealmzGlobalData(const string& dir) : dir(dir) {
  string the_family_jewels_name = first_file_that_exists({(this->dir + "/the_family_jewels.rsf"),
      (this->dir + "/The Family Jewels.rsf"),
      (this->dir + "/THE FAMILY JEWELS.RSF"),
      (this->dir + "/the_family_jewels/rsrc"),
      (this->dir + "/The Family Jewels/rsrc"),
      (this->dir + "/THE FAMILY JEWELS/rsrc"),
      (this->dir + "/the_family_jewels/..namedfork/rsrc"),
      (this->dir + "/The Family Jewels/..namedfork/rsrc"),
      (this->dir + "/THE FAMILY JEWELS/..namedfork/rsrc")});
  string portraits_name = first_file_that_exists({(this->dir + "/portraits.rsf"),
      (this->dir + "/Portraits.rsf"),
      (this->dir + "/PORTRAITS.RSF"),
      (this->dir + "/portraits/rsrc"),
      (this->dir + "/Portraits/rsrc"),
      (this->dir + "/PORTRAITS/rsrc"),
      (this->dir + "/portraits/..namedfork/rsrc"),
      (this->dir + "/Portraits/..namedfork/rsrc"),
      (this->dir + "/PORTRAITS/..namedfork/rsrc")});
  string names_name = first_file_that_exists({(this->dir + "/custom names.rsf"),
      (this->dir + "/Custom Names.rsf"),
      (this->dir + "/CUSTOM NAMES.RSF"),
      (this->dir + "/custom names/rsrc"),
      (this->dir + "/Custom Names/rsrc"),
      (this->dir + "/CUSTOM NAMES/rsrc"),
      (this->dir + "/custom names/..namedfork/rsrc"),
      (this->dir + "/Custom Names/..namedfork/rsrc"),
      (this->dir + "/CUSTOM NAMES/..namedfork/rsrc")});
  string data_id_name = first_file_that_exists({(this->dir + "/data id.rsf"),
      (this->dir + "/Data ID.rsf"),
      (this->dir + "/DATA ID.RSF"),
      (this->dir + "/data id/rsrc"),
      (this->dir + "/Data ID/rsrc"),
      (this->dir + "/DATA ID/rsrc"),
      (this->dir + "/data id/..namedfork/rsrc"),
      (this->dir + "/Data ID/..namedfork/rsrc"),
      (this->dir + "/DATA ID/..namedfork/rsrc")});

  this->global_rsf = parse_resource_fork(load_file(the_family_jewels_name));
  this->portraits_rsf = parse_resource_fork(load_file(portraits_name));
  this->names_rsf = parse_resource_fork(load_file(names_name));
  this->data_id_rsf = parse_resource_fork(load_file(data_id_name));

  this->load_default_tilesets();
  this->item_info = this->parse_item_info(this->data_id_rsf);
  this->spell_names = this->parse_spell_names(this->names_rsf);

  auto races_STRN = this->names_rsf.decode_STRN(129);
  this->race_names = std::move(races_STRN.strs);

  auto castes_STRN = this->names_rsf.decode_STRN(131);
  this->caste_names = std::move(castes_STRN.strs);
}

void RealmzGlobalData::load_default_tilesets() {
  static const unordered_map<string, vector<string>> land_type_to_filenames({
      {"indoor", {"data_castle_bd", "Data Castle BD", "DATA CASTLE BD"}},
      {"desert", {"data_desert_bd", "Data Desert BD", "DATA DESERT BD"}},
      {"outdoor", {"data_p_bd", "Data P BD", "DATA P BD"}},
      {"snow", {"data_snow_bd", "Data Snow BD", "DATA SNOW BD"}},
      {"cave", {"data_sub_bd", "Data SUB BD", "DATA SUB BD"}},
      {"abyss", {"data_swamp_bd", "Data Swamp BD", "DATA SWAMP BD"}},
  });
  for (const auto& it : land_type_to_filenames) {
    vector<string> filenames;
    for (const auto& filename : it.second)
      filenames.emplace_back(string_printf("%s/%s", this->dir.c_str(),
          filename.c_str()));

    string filename = first_file_that_exists(filenames);
    if (!filename.empty()) {
      this->land_type_to_tileset_definition.emplace(
          it.first, load_tileset_definition(filename));
    } else {
      fprintf(stderr, "warning: tileset definition for %s is missing\n",
          it.first.c_str());
    }
  }
}

std::unordered_map<uint16_t, ItemInfo> RealmzGlobalData::parse_item_info(ResourceFile& rsf) {
  // Resource IDs:
  // 0 = unidentified name of weapon (e.g. Flail)
  // 1 = identified name of weapon (e.g. Flail of Cat Tails +4)
  // 2 = description of weapon (appears in info window)
  // 200, 201, 202: the above, but for armors
  // 400, 401, 402: the above, but for armors
  // 600, 601, 602: the above, but for magic items
  std::unordered_map<uint16_t, ItemInfo> ret;
  for (size_t base_id = 0; base_id <= 800; base_id += 200) {
    try {
      auto unidentified_STRN = rsf.decode_STRN(base_id + 0);
      auto identified_STRN = rsf.decode_STRN(base_id + 1);
      auto description_STRN = rsf.decode_STRN(base_id + 2);
      size_t count = min<size_t>({unidentified_STRN.strs.size(), identified_STRN.strs.size(), description_STRN.strs.size()});
      for (size_t z = 0; z < count; z++) {
        auto& info = ret[base_id + z];
        info.unidentified_name = std::move(unidentified_STRN.strs[z]);
        info.name = std::move(identified_STRN.strs[z]);
        info.description = std::move(description_STRN.strs[z]);
      }
    } catch (const out_of_range&) {
    }
  }
  return ret;
}

std::unordered_map<uint16_t, std::string> RealmzGlobalData::parse_spell_names(ResourceFile& rsf) {
  static const char* class_names[5] = {
      "Sorcerer", "Priest", "Enchanter", "Special", "Custom"};
  static const char* special_level_names[7] = {
      "ProJo", "ProJo/Breath", "Potion", "Missile", "ImproveMissile", "Misc", "Unnamed"};

  std::unordered_map<uint16_t, std::string> ret;
  for (size_t x = 0; x < 5; x++) {
    for (size_t y = 0; y < 7; y++) {
      try {
        auto decoded = rsf.decode_STRN(((x + 1) * 1000) + y);
        string prefix = (x == 3)
            ? string_printf("(%s/%s) ", class_names[x], special_level_names[y])
            : string_printf("(%s/L%zu) ", class_names[x], y + 1);
        for (size_t z = 0; z < decoded.strs.size(); z++) {
          uint16_t spell_id = ((x + 1) * 1000) + ((y + 1) * 100) + (z + 1);
          ret.emplace(spell_id, prefix + decoded.strs[z]);
        }
      } catch (const out_of_range&) {
      }
    }
  }

  return ret;
}

const ItemInfo& RealmzGlobalData::info_for_item(uint16_t id) const {
  return this->item_info.at(id);
}

const string& RealmzGlobalData::name_for_spell(uint16_t id) const {
  return this->spell_names.at(id);
}

static unordered_map<string, int16_t> land_type_to_resource_id({
    {"outdoor", 300},
    {"dungeon", 302},
    {"cave", 303},
    {"indoor", 304},
    {"desert", 305},
    {"custom_1", 306},
    {"custom_2", 307},
    {"custom_3", 308},
    {"abyss", 309}, // TODO: should this be "swamp" instead?
    {"snow", 310},
});

int16_t resource_id_for_land_type(const string& land_type) {
  return land_type_to_resource_id.at(land_type);
}

Image generate_tileset_definition_legend(const TileSetDefinition& ts,
    const Image& positive_pattern) {

  if (positive_pattern.get_width() != 640 || positive_pattern.get_height() != 320) {
    throw runtime_error("postiive pattern is not 640x320");
  }

  Image result(32 * 15, 97 * 200);
  for (size_t x = 0; x < 200; x++) {

    // Tile 0 is unused apparently? (there are 201 of them)
    const TileDefinition& t = ts.tiles[x + 1];
    uint32_t text_color;
    if (x + 1 == ts.base_tile_id) {
      text_color = 0x000000FF;
      result.fill_rect(0, 97 * x, 32, 96, 0xFFFFFFFF);
    } else {
      text_color = 0xFFFFFFFF;
    }
    result.draw_text(1, 97 * x + 1, text_color, "%04zX", x + 1);
    result.draw_text(1, 97 * x + 17, text_color, "SOUND\n%04X", t.sound_id.load());

    if (x + 1 == ts.base_tile_id) {
      result.draw_text(1, 97 * x + 41, text_color, "BASE");
    }

    // Draw the tile itself
    result.blit(positive_pattern, 32, 97 * x, 32, 32, (x % 20) * 32, (x / 20) * 32);

    // Draw the solid type
    if (t.solid_type == 1) {
      result.fill_rect(64, 97 * x, 32, 96, 0xFF000080);
      result.draw_text(65, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "LARGE\nONLY");
    } else if (t.solid_type == 2) {
      result.fill_rect(64, 97 * x, 32, 96, 0xFF0000FF);
      result.draw_text(65, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "SOLID");
    } else if (t.solid_type == 0) {
      result.draw_text(65, 97 * x + 1, 0xFFFFFFFF, 0x000000FF, "NOT\nSOLID");
    } else {
      result.fill_rect(64, 97 * x, 32, 96, 0xFFFFFFFF);
      result.draw_text(65, 97 * x + 1, 0x000000FF, 0x000000FF, "%04X", t.solid_type.load());
    }

    // Draw its path flag
    if (t.is_path) {
      result.fill_rect(96, 97 * x, 32, 96, 0xFFFFFFFF);
      result.draw_text(97, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "PATH");
    } else {
      result.draw_text(97, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "NOT\nPATH");
    }

    // Draw the shore flag
    if (t.is_shore) {
      result.fill_rect(128, 97 * x, 32, 96, 0xFFFF00FF);
      result.draw_text(129, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "SHORE");
    } else {
      result.draw_text(129, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "NOT\nSHORE");
    }

    // Draw the is/need boat flag
    if (t.is_need_boat == 1) {
      result.fill_rect(160, 97 * x, 32, 96, 0x0080FFFF);
      result.draw_text(161, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "BOAT");
    } else if (t.is_need_boat == 2) {
      result.fill_rect(160, 97 * x, 32, 96, 0x0080FF80);
      result.draw_text(161, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "NEED\nBOAT");
    } else if (t.is_need_boat == 0) {
      result.draw_text(161, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "NO\nBOAT");
    } else {
      result.fill_rect(64, 97 * x, 32, 96, 0xFFFFFFFF);
      result.draw_text(161, 97 * x + 1, 0x000000FF, 0x000000FF, "%04X", t.is_need_boat.load());
    }

    // Draw the fly/float flag
    if (t.need_fly_float) {
      result.fill_rect(192, 97 * x, 32, 96, 0x00FF00FF);
      result.draw_text(193, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "NEED\nFLY\nFLOAT");
    } else {
      result.draw_text(193, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "NO\nFLY\nFLOAT");
    }

    // Draw the blocks LOS flag
    if (t.blocks_los) {
      result.fill_rect(224, 97 * x, 32, 96, 0x808080FF);
      result.draw_text(225, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "BLOCK\nLOS");
    } else {
      result.draw_text(225, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "NO\nBLOCK\nLOS");
    }

    // Draw the special flag (forest type)
    if (t.special_type == 1) {
      result.fill_rect(256, 97 * x, 32, 96, 0x00FF80FF);
      result.draw_text(257, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "TREES");
    } else if (t.special_type == 2) {
      result.fill_rect(256, 97 * x, 32, 96, 0xFF8000FF);
      result.draw_text(257, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "DSRT");
    } else if (t.special_type == 3) {
      result.fill_rect(256, 97 * x, 32, 96, 0xFF0000FF);
      result.draw_text(257, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "SHRMS");
    } else if (t.special_type == 4) {
      result.fill_rect(256, 97 * x, 32, 96, 0x008000FF);
      result.draw_text(257, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "SWAMP");
    } else if (t.special_type == 5) {
      result.fill_rect(256, 97 * x, 32, 96, 0xE0E0E0FF);
      result.draw_text(257, 97 * x + 1, 0xFFFFFFFF, 0x00000080, "SNOW");
    } else if (t.special_type == 0) {
      result.draw_text(257, 97 * x + 1, 0xFFFFFFFF, 0x000000FF, "NO\nTREES");
    } else {
      result.draw_text(257, 97 * x + 1, 0xFFFFFFFF, 0x000000FF, "%04X", t.special_type.load());
    }

    // Draw the time to move
    result.draw_text(288, 97 * x + 1, 0xFFFFFFFF, 0x000000FF, "%hd\nMINS", t.time_per_move.load());

    // Draw unknown fields
    result.draw_text(320, 97 * x + 1, 0xFFFFFFFF, 0x000000FF, "%04hX", t.unknown5.load());
    result.draw_text(352, 97 * x + 1, 0xFFFFFFFF, 0x000000FF, "%04hX", t.unknown6.load());

    // Draw the battle expansion
    for (int y = 0; y < 9; y++) {
      int px = 384 + (y % 3) * 32;
      int py = 97 * x + (y / 3) * 32;

      int16_t data = t.battle_expansion[y / 3][y % 3];
      if (data < 1 || data > 200) {
        result.draw_text(px, py, 0xFFFFFFFF, 0x00000000, "%04X", data);
      } else {
        data--;
        result.blit(positive_pattern, px, py, 32, 32, (data % 20) * 32, (data / 20) * 32);
      }
    }

    // Draw the separator for the next tile
    result.draw_horizontal_line(0, result.get_width(), 97 * x + 96, 4, 0xFFFFFFFF);
  }

  return result;
}

std::string disassemble_tileset_definition(const TileSetDefinition& ts, const char* name) {
  BlockStringWriter w;
  w.write_printf("===== TILESET %s", name);
  w.write("  ID |  ID | BASE |  SOUND | SOLID | PATH | SHORE |  BOAT | FLY | OPAQUE |  FOREST | TM | BATTLE EXPANSION           | BATTLE EXPANSION                   ");

  for (size_t x = 0; x < 201; x++) {
    const TileDefinition& t = ts.tiles[x];

    string solid_type_str;
    if (t.solid_type == 1) {
      solid_type_str = "LARGE";
    } else if (t.solid_type == 2) {
      solid_type_str = "SOLID";
    } else if (t.solid_type == 0) {
      solid_type_str = "     ";
    } else {
      solid_type_str = string_printf(" %04hX", t.solid_type.load());
    }

    string boat_type_str;
    if (t.is_need_boat == 1) {
      boat_type_str = " BOAT";
    } else if (t.is_need_boat == 2) {
      boat_type_str = "WATER";
    } else if (t.is_need_boat == 0) {
      boat_type_str = "     ";
    } else {
      boat_type_str = string_printf(" %04hX", t.is_need_boat.load());
    }

    string forest_type_str;
    if (t.special_type == 1) {
      forest_type_str = "  TREES";
    } else if (t.special_type == 2) {
      forest_type_str = " DESERT";
    } else if (t.special_type == 3) {
      forest_type_str = "SHROOMS";
    } else if (t.special_type == 4) {
      forest_type_str = "  SWAMP";
    } else if (t.special_type == 5) {
      forest_type_str = "   SNOW";
    } else if (t.special_type == 0) {
      forest_type_str = "       ";
    } else {
      forest_type_str = string_printf("   %04hX", t.special_type.load());
    }

    w.write_printf("  %02zX | %3zu | %s | %6d | %s | %s | %s | %s | %s | %s | %s | %02hd | %02hX %02hX %02hX %02hX %02hX %02hX %02hX %02hX %02hX | %3hd %3hd %3hd %3hd %3hd %3hd %3hd %3hd %3hd",
        x,
        x,
        (x == ts.base_tile_id) ? "BASE" : "    ",
        t.sound_id.load(),
        solid_type_str.c_str(),
        t.is_path ? "PATH" : "    ",
        t.is_shore ? "SHORE" : "     ",
        boat_type_str.c_str(),
        t.need_fly_float ? "FLY" : "   ",
        t.blocks_los ? "OPAQUE" : "      ",
        forest_type_str.c_str(),
        t.time_per_move.load(),
        t.battle_expansion[0][0].load(),
        t.battle_expansion[0][1].load(),
        t.battle_expansion[0][2].load(),
        t.battle_expansion[1][0].load(),
        t.battle_expansion[1][1].load(),
        t.battle_expansion[1][2].load(),
        t.battle_expansion[2][0].load(),
        t.battle_expansion[2][1].load(),
        t.battle_expansion[2][2].load(),
        t.battle_expansion[0][0].load(),
        t.battle_expansion[0][1].load(),
        t.battle_expansion[0][2].load(),
        t.battle_expansion[1][0].load(),
        t.battle_expansion[1][1].load(),
        t.battle_expansion[1][2].load(),
        t.battle_expansion[2][0].load(),
        t.battle_expansion[2][1].load(),
        t.battle_expansion[2][2].load());
  }

  return w.close("\n");
}
