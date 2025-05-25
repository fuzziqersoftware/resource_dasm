#include "RealmzGlobalData.hh"

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

string first_file_that_exists(const vector<string>& names) {
  for (const auto& it : names) {
    if (std::filesystem::is_regular_file(it)) {
      return it;
    }
  }
  return "";
}

RealmzGlobalData::RealmzGlobalData(const string& dir) : dir(dir) {
  this->global_rsf = parse_resource_fork(load_file(first_file_that_exists({(this->dir + "/the_family_jewels.rsf"),
      (this->dir + "/The Family Jewels.rsf"),
      (this->dir + "/THE FAMILY JEWELS.RSF"),
      (this->dir + "/the_family_jewels/rsrc"),
      (this->dir + "/The Family Jewels/rsrc"),
      (this->dir + "/THE FAMILY JEWELS/rsrc"),
      (this->dir + "/the_family_jewels/..namedfork/rsrc"),
      (this->dir + "/The Family Jewels/..namedfork/rsrc"),
      (this->dir + "/THE FAMILY JEWELS/..namedfork/rsrc")})));
  this->portraits_rsf = parse_resource_fork(load_file(first_file_that_exists({(this->dir + "/portraits.rsf"),
      (this->dir + "/Portraits.rsf"),
      (this->dir + "/PORTRAITS.RSF"),
      (this->dir + "/portraits/rsrc"),
      (this->dir + "/Portraits/rsrc"),
      (this->dir + "/PORTRAITS/rsrc"),
      (this->dir + "/portraits/..namedfork/rsrc"),
      (this->dir + "/Portraits/..namedfork/rsrc"),
      (this->dir + "/PORTRAITS/..namedfork/rsrc")})));
  this->tacticals_rsf = parse_resource_fork(load_file(first_file_that_exists({(this->dir + "/tacticals.rsf"),
      (this->dir + "/Tacticals.rsf"),
      (this->dir + "/TACTICALS.RSF"),
      (this->dir + "/tacticals/rsrc"),
      (this->dir + "/Tacticals/rsrc"),
      (this->dir + "/TACTICALS/rsrc"),
      (this->dir + "/tacticals/..namedfork/rsrc"),
      (this->dir + "/Tacticals/..namedfork/rsrc"),
      (this->dir + "/TACTICALS/..namedfork/rsrc")})));
  this->custom_names_rsf = parse_resource_fork(load_file(first_file_that_exists({(this->dir + "/custom_names.rsf"),
      (this->dir + "/Custom Names.rsf"),
      (this->dir + "/CUSTOM NAMES.RSF"),
      (this->dir + "/custom_names/rsrc"),
      (this->dir + "/Custom Names/rsrc"),
      (this->dir + "/CUSTOM NAMES/rsrc"),
      (this->dir + "/custom_names/..namedfork/rsrc"),
      (this->dir + "/Custom Names/..namedfork/rsrc"),
      (this->dir + "/CUSTOM NAMES/..namedfork/rsrc")})));
  this->scenario_names_rsf = parse_resource_fork(load_file(first_file_that_exists({(this->dir + "/scenario_names.rsf"),
      (this->dir + "/Scenario Names.rsf"),
      (this->dir + "/SCENARIO NAMES.RSF"),
      (this->dir + "/scenario_names/rsrc"),
      (this->dir + "/Scenario Names/rsrc"),
      (this->dir + "/SCENARIO NAMES/rsrc"),
      (this->dir + "/scenario_names/..namedfork/rsrc"),
      (this->dir + "/Scenario Names/..namedfork/rsrc"),
      (this->dir + "/SCENARIO NAMES/..namedfork/rsrc")})));
  this->data_id_rsf = parse_resource_fork(load_file(first_file_that_exists({(this->dir + "/data_id.rsf"),
      (this->dir + "/Data ID.rsf"),
      (this->dir + "/DATA ID.RSF"),
      (this->dir + "/data_id/rsrc"),
      (this->dir + "/Data ID/rsrc"),
      (this->dir + "/DATA ID/rsrc"),
      (this->dir + "/data_id/..namedfork/rsrc"),
      (this->dir + "/Data ID/..namedfork/rsrc"),
      (this->dir + "/DATA ID/..namedfork/rsrc")})));

  this->race_definitions = this->load_race_definitions(first_file_that_exists({(this->dir + "/data_race"),
      (this->dir + "/Data Race"),
      (this->dir + "/DATA RACE")}));
  this->caste_definitions = this->load_caste_definitions(first_file_that_exists({(this->dir + "/data_caste"),
      (this->dir + "/Data Caste"),
      (this->dir + "/DATA CASTE")}));
  this->item_definitions = this->load_item_definitions(first_file_that_exists({(this->dir + "/data_id"),
      (this->dir + "/Data ID"),
      (this->dir + "/DATA ID")}));
  this->spell_definitions = this->load_spell_definitions(first_file_that_exists({(this->dir + "/data_s"),
      (this->dir + "/Data S"),
      (this->dir + "/DATA S")}));

  this->race_names = this->load_race_names(this->custom_names_rsf);
  this->caste_names = this->load_caste_names(this->custom_names_rsf);
  this->spell_names = this->load_spell_names(this->custom_names_rsf);
  this->item_strings = this->load_item_strings(this->data_id_rsf);

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
    for (const auto& filename : it.second) {
      filenames.emplace_back(std::format("{}/{}", this->dir, filename));
    }

    string filename = first_file_that_exists(filenames);
    if (!filename.empty()) {
      this->land_type_to_tileset_definition.emplace(it.first, load_tileset_definition(filename));
    } else {
      fwrite_fmt(stderr, "warning: tileset definition for {} is missing\n", it.first);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// Things that are apparently hardcoded and don't appear in resources

const char* RealmzGlobalData::name_for_condition(size_t condition_id) {
  array<const char*, 40> names = {
      /*  0 */ "Runs away",
      /*  1 */ "Helpless",
      /*  2 */ "Tangled",
      /*  3 */ "Cursed",
      /*  4 */ "Magic aura",
      /*  5 */ "Stupid",
      /*  6 */ "Slow",
      /*  7 */ "Hit shield",
      /*  8 */ "Missile shield",
      /*  9 */ "Poisoned",
      /* 10 */ "Regenerating",
      /* 11 */ "Fire protection",
      /* 12 */ "Cold protection",
      /* 13 */ "Electrical protection",
      /* 14 */ "Chemical protection",
      /* 15 */ "Mental protection",
      /* 16 */ "Magic screen 1",
      /* 17 */ "Magic screen 2",
      /* 18 */ "Magic screen 3",
      /* 19 */ "Magic screen 4",
      /* 20 */ "Magic screen 5",
      /* 21 */ "Strong",
      /* 22 */ "Evil protection",
      /* 23 */ "Speedy",
      /* 24 */ "Invisible",
      /* 25 */ "Animated",
      /* 26 */ "Turned to stone",
      /* 27 */ "Blind",
      /* 28 */ "Diseased",
      /* 29 */ "Confused",
      /* 30 */ "Reflecting spells",
      /* 31 */ "Reflecting attacks",
      /* 32 */ "Attack bonus",
      /* 33 */ "Absorbing energy",
      /* 34 */ "Energy drain",
      /* 35 */ "Absorbing energy from attacks",
      /* 36 */ "Hindered attacks",
      /* 37 */ "Hindered defense",
      /* 38 */ "Defense bonuse",
      /* 39 */ "Silenced",
  };
  return names.at(condition_id);
}

const char* RealmzGlobalData::name_for_age_group(size_t age_group) {
  array<const char*, 5> names = {
      /*  1 */ "Youth",
      /*  2 */ "Young",
      /*  3 */ "Prime",
      /*  4 */ "Adult",
      /*  5 */ "Senior",
  };
  return names.at(age_group - 1);
}

const char* RealmzGlobalData::name_for_item_category_flag(uint8_t flag_index) {
  static const array<const char*, 64> names = {
      /* 8000000000000000 */ "small blunt weapon",
      /* 4000000000000000 */ "medium blunt weapon",
      /* 2000000000000000 */ "large blunt weapon",
      /* 1000000000000000 */ "very small bladed weapon",
      /* 0800000000000000 */ "small bladed weapon",
      /* 0400000000000000 */ "medium bladed weapon",
      /* 0200000000000000 */ "large bladed weapon",
      /* 0100000000000000 */ "very large bladed weapon",
      /* 0080000000000000 */ "staff",
      /* 0040000000000000 */ "spear",
      /* 0020000000000000 */ "pole arm",
      /* 0010000000000000 */ "ninja style weapon",
      /* 0008000000000000 */ "normal bow",
      /* 0004000000000000 */ "crossbow",
      /* 0002000000000000 */ "dart",
      /* 0001000000000000 */ "flask of oil",
      /* 0000800000000000 */ "throwing knife",
      /* 0000400000000000 */ "whip",
      /* 0000200000000000 */ "quiver",
      /* 0000100000000000 */ "belt",
      /* 0000080000000000 */ "necklace",
      /* 0000040000000000 */ "cap",
      /* 0000020000000000 */ "soft helm",
      /* 0000010000000000 */ "small helm",
      /* 0000008000000000 */ "large helm",
      /* 0000004000000000 */ "small shield",
      /* 0000002000000000 */ "medium shield",
      /* 0000001000000000 */ "large shield",
      /* 0000000800000000 */ "bracer",
      /* 0000000400000000 */ "cloth gloves",
      /* 0000000200000000 */ "leather gloves",
      /* 0000000100000000 */ "metal gloves",
      /* 0000000080000000 */ "cloak/cape",
      /* 0000000040000000 */ "robe",
      /* 0000000020000000 */ "padded armor",
      /* 0000000010000000 */ "leather armor",
      /* 0000000008000000 */ "chain armor",
      /* 0000000004000000 */ "banded armor",
      /* 0000000002000000 */ "plate armor",
      /* 0000000001000000 */ "soft boots",
      /* 0000000000800000 */ "hard boots",
      /* 0000000000400000 */ "throwing hammer",
      /* 0000000000200000 */ "throwing stars",
      /* 0000000000100000 */ "misc blunt weapon",
      /* 0000000000080000 */ "misc bladed weapon",
      /* 0000000000040000 */ "misc large weapon",
      /* 0000000000020000 */ "misc missile weapon",
      /* 0000000000010000 */ "misc item",
      /* 0000000000008000 */ "scroll case",
      /* 0000000000004000 */ "brooch/pin",
      /* 0000000000002000 */ "ring",
      /* 0000000000001000 */ "potion",
      /* 0000000000000800 */ "misc magic item",
      /* 0000000000000400 */ "special object",
      /* 0000000000000200 */ "ion stone",
      /* 0000000000000100 */ "book",
      /* 0000000000000080 */ "scroll",
      /* 0000000000000040 */ "unused 0000000000000040",
      /* 0000000000000020 */ "unused 0000000000000020",
      /* 0000000000000010 */ "unused 0000000000000010",
      /* 0000000000000008 */ "unused 0000000000000008",
      /* 0000000000000004 */ "unused 0000000000000004",
      /* 0000000000000002 */ "unused 0000000000000002",
      /* 0000000000000001 */ "unused 0000000000000001",
  };
  return names.at(flag_index);
}

const char* RealmzGlobalData::name_for_race_flag(uint8_t flag_index) {
  static const array<const char*, 16> names = {
      /* 8000 */ "short",
      /* 4000 */ "elvish",
      /* 2000 */ "half",
      /* 1000 */ "goblinoid",
      /* 0800 */ "reptilian",
      /* 0400 */ "nether worldly",
      /* 0200 */ "goodly",
      /* 0100 */ "neutral",
      /* 0080 */ "evil",
      /* 0040 */ "unused 0040",
      /* 0020 */ "unused 0020",
      /* 0010 */ "unused 0010",
      /* 0008 */ "unused 0008",
      /* 0004 */ "unused 0004",
      /* 0002 */ "unused 0002",
      /* 0001 */ "unused 0001",
  };
  return names.at(flag_index);
}

const char* RealmzGlobalData::name_for_caste_flag(uint8_t flag_index) {
  static const array<const char*, 64> names = {
      /* 8000 */ "warrior",
      /* 4000 */ "thief",
      /* 2000 */ "archer",
      /* 1000 */ "sorcerer",
      /* 0800 */ "priest",
      /* 0400 */ "enchanter",
      /* 0200 */ "warrior wizard",
      /* 0100 */ "unused 0100",
      /* 0080 */ "unused 0080",
      /* 0040 */ "unused 0040",
      /* 0020 */ "unused 0020",
      /* 0010 */ "unused 0010",
      /* 0008 */ "unused 0008",
      /* 0004 */ "unused 0004",
      /* 0002 */ "unused 0002",
      /* 0001 */ "unused 0001",
  };
  return names.at(flag_index);
}

////////////////////////////////////////////////////////////////////////////////
// DATA * BD (tileset definitions)

RealmzGlobalData::TileSetDefinition RealmzGlobalData::load_tileset_definition(const string& filename) {
  return load_object_file<TileSetDefinition>(filename, true);
}

int16_t RealmzGlobalData::pict_resource_id_for_land_type(const string& land_type) {
  static const unordered_map<string, int16_t> land_type_to_resource_id({
      {"outdoor", 300},
      {"dungeon", 302},
      {"cave", 303},
      {"indoor", 304},
      {"desert", 305},
      {"custom_1", 306},
      {"custom_2", 307},
      {"custom_3", 308},
      {"abyss", 309}, // "Swamp" in Realmz
      {"snow", 310},
  });
  return land_type_to_resource_id.at(land_type);
}

Image RealmzGlobalData::generate_tileset_definition_legend(
    const TileSetDefinition& ts, const Image& positive_pattern) {

  if (positive_pattern.get_width() != 640 || positive_pattern.get_height() != 320) {
    throw runtime_error("postiive pattern is not 640x320");
  }

  Image result(32 * 15, 97 * 200);
  for (size_t x = 0; x < 200; x++) {

    // Tile 0 is unused apparently? (there are 201 of them)
    const TileDefinition& t = ts.tiles[x + 1];
    uint32_t text_color;
    if (x + 1 == static_cast<size_t>(ts.base_tile_id)) {
      text_color = 0x000000FF;
      result.fill_rect(0, 97 * x, 32, 96, 0xFFFFFFFF);
    } else {
      text_color = 0xFFFFFFFF;
    }
    result.draw_text(1, 97 * x + 1, text_color, "{:04X}", x + 1);
    result.draw_text(1, 97 * x + 17, text_color, "SOUND\n{:04X}", t.sound_id);

    if (x + 1 == static_cast<size_t>(ts.base_tile_id)) {
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
      result.draw_text(65, 97 * x + 1, 0x000000FF, 0x000000FF, "{:04X}", t.solid_type);
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
      result.draw_text(161, 97 * x + 1, 0x000000FF, 0x000000FF, "{:04X}", t.is_need_boat);
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
      result.draw_text(257, 97 * x + 1, 0xFFFFFFFF, 0x000000FF, "{:04X}", t.special_type);
    }

    // Draw the time to move
    result.draw_text(288, 97 * x + 1, 0xFFFFFFFF, 0x000000FF, "{}\nMINS", t.time_per_move);

    // Draw unknown fields
    result.draw_text(320, 97 * x + 1, 0xFFFFFFFF, 0x000000FF, "{:04X}", t.unknown5);
    result.draw_text(352, 97 * x + 1, 0xFFFFFFFF, 0x000000FF, "{:04X}", t.unknown6);

    // Draw the battle expansion
    for (int y = 0; y < 9; y++) {
      int px = 384 + (y % 3) * 32;
      int py = 97 * x + (y / 3) * 32;

      int16_t data = t.battle_expansion[y / 3][y % 3];
      if (data < 1 || data > 200) {
        result.draw_text(px, py, 0xFFFFFFFF, 0x00000000, "{:04X}", data);
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

string RealmzGlobalData::disassemble_tileset_definition(const TileSetDefinition& ts, const char* name) {
  BlockStringWriter w;
  w.write_fmt("===== TILESET {}", name);
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
      solid_type_str = std::format(" {:04X}", t.solid_type);
    }

    string boat_type_str;
    if (t.is_need_boat == 1) {
      boat_type_str = " BOAT";
    } else if (t.is_need_boat == 2) {
      boat_type_str = "WATER";
    } else if (t.is_need_boat == 0) {
      boat_type_str = "     ";
    } else {
      boat_type_str = std::format(" {:04X}", t.is_need_boat);
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
      forest_type_str = std::format("   {:04X}", t.special_type);
    }

    w.write_fmt("  {:02X} | {:3} | {} | {:6} | {} | {} | {} | {} | {} | {} | {} | {:02} | {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} | {:3} {:3} {:3} {:3} {:3} {:3} {:3} {:3} {:3}",
        x,
        x,
        (x == static_cast<size_t>(ts.base_tile_id)) ? "BASE" : "    ",
        t.sound_id,
        solid_type_str,
        t.is_path ? "PATH" : "    ",
        t.is_shore ? "SHORE" : "     ",
        boat_type_str,
        t.need_fly_float ? "FLY" : "   ",
        t.blocks_los ? "OPAQUE" : "      ",
        forest_type_str,
        t.time_per_move,
        t.battle_expansion[0][0],
        t.battle_expansion[0][1],
        t.battle_expansion[0][2],
        t.battle_expansion[1][0],
        t.battle_expansion[1][1],
        t.battle_expansion[1][2],
        t.battle_expansion[2][0],
        t.battle_expansion[2][1],
        t.battle_expansion[2][2],
        t.battle_expansion[0][0],
        t.battle_expansion[0][1],
        t.battle_expansion[0][2],
        t.battle_expansion[1][0],
        t.battle_expansion[1][1],
        t.battle_expansion[1][2],
        t.battle_expansion[2][0],
        t.battle_expansion[2][1],
        t.battle_expansion[2][2]);
  }
  w.write("");
  return w.close("\n");
}

////////////////////////////////////////////////////////////////////////////////
// CUSTOM NAMES.RSF

vector<string> RealmzGlobalData::load_race_names(const ResourceFile& rsf) {
  auto races_STRN = rsf.decode_STRN(129);
  return std::move(races_STRN.strs);
}

vector<string> RealmzGlobalData::load_caste_names(const ResourceFile& rsf) {
  auto castes_STRN = rsf.decode_STRN(131);
  return std::move(castes_STRN.strs);
}

map<uint16_t, string> RealmzGlobalData::load_spell_names(const ResourceFile& rsf) {
  static const char* class_names[5] = {
      "Sorcerer", "Priest", "Enchanter", "Special", "Custom"};
  static const char* special_level_names[7] = {
      "ProJo", "ProJo/Breath", "Potion", "Missile", "ImproveMissile", "Misc", "Unnamed"};

  map<uint16_t, string> ret;
  for (size_t x = 0; x < 5; x++) {
    for (size_t y = 0; y < 7; y++) {
      try {
        auto decoded = rsf.decode_STRN(((x + 1) * 1000) + y);
        string prefix = (x == 3)
            ? std::format("({}/{}) ", class_names[x], special_level_names[y])
            : std::format("({}/L{}) ", class_names[x], y + 1);
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

const string& RealmzGlobalData::name_for_spell(uint16_t id) const {
  return this->spell_names.at(id);
}

////////////////////////////////////////////////////////////////////////////////
// DATA CASTE

static void disassemble_special_abilities(BlockStringWriter& w, const RealmzGlobalData::SpecialAbilities& sa) {
  w.write_fmt("    sneak_attack                        {}", sa.sneak_attack);
  w.write_fmt("    unknown_a1[0]                       {}", sa.unknown_a1[0]);
  w.write_fmt("    unknown_a1[1]                       {}", sa.unknown_a1[1]);
  w.write_fmt("    major_wound                         {}", sa.major_wound);
  w.write_fmt("    detect_secret                       {}", sa.detect_secret);
  w.write_fmt("    acrobatic_act                       {}", sa.acrobatic_act);
  w.write_fmt("    detect_trap                         {}", sa.detect_trap);
  w.write_fmt("    disarm_trap                         {}", sa.disarm_trap);
  w.write_fmt("    unknown_a2                          {}", sa.unknown_a2);
  w.write_fmt("    force_lock                          {}", sa.force_lock);
  w.write_fmt("    unknown_a3                          {}", sa.unknown_a3);
  w.write_fmt("    pick_lock                           {}", sa.pick_lock);
  w.write_fmt("    unknown_a4                          {}", sa.unknown_a4);
  w.write_fmt("    turn_undead                         {}", sa.turn_undead);
}

static void disassemble_drvs_abilities(BlockStringWriter& w, const RealmzGlobalData::DRVsAbilities& drv) {
  w.write_fmt("    charm                               {}", drv.charm);
  w.write_fmt("    heat                                {}", drv.heat);
  w.write_fmt("    cold                                {}", drv.cold);
  w.write_fmt("    electric                            {}", drv.electric);
  w.write_fmt("    chemical                            {}", drv.chemical);
  w.write_fmt("    mental                              {}", drv.mental);
  w.write_fmt("    magical                             {}", drv.magical);
}

vector<RealmzGlobalData::CasteDefinition> RealmzGlobalData::load_caste_definitions(const string& filename) {
  return load_vector_file<CasteDefinition>(filename);
}

string RealmzGlobalData::disassemble_caste_definition(const CasteDefinition& c, size_t index, const char* name) const {
  BlockStringWriter w;
  if (name) {
    w.write_fmt("===== CASTE {} [CST{}] ({})", index, index, name);
  } else {
    w.write_fmt("===== CASTE {} [CST{}]", index, index);
  }
  w.write_fmt("  special_abilities_start");
  disassemble_special_abilities(w, c.special_abilities_start);
  w.write_fmt("  special_abilities_level_up_delta");
  disassemble_special_abilities(w, c.special_abilities_level_up_delta);
  w.write_fmt("  drv_adjust");
  disassemble_drvs_abilities(w, c.drv_adjust);
  w.write_fmt("  a1                                    {}", c.unknown_a1);
  w.write_fmt("  brawn_adjust                          {}", c.brawn_adjust);
  w.write_fmt("  knowledge_adjust                      {}", c.knowledge_adjust);
  w.write_fmt("  judgment_adjust                       {}", c.judgment_adjust);
  w.write_fmt("  agility_adjust                        {}", c.agility_adjust);
  w.write_fmt("  vitality_adjust                       {}", c.vitality_adjust);
  w.write_fmt("  luck_adjust                           {}", c.luck_adjust);
  w.write_fmt("  sorcerer_spells                       {}, start_skill_level={}, max_spell_level={}", c.sorcerer_spell_capability.enabled ? "enabled" : "disabled", c.sorcerer_spell_capability.start_skill_level, c.sorcerer_spell_capability.max_spell_level);
  w.write_fmt("  priest_spells                         {}, start_skill_level={}, max_spell_level={}", c.priest_spell_capability.enabled ? "enabled" : "disabled", c.priest_spell_capability.start_skill_level, c.priest_spell_capability.max_spell_level);
  w.write_fmt("  enchanter_spells                      {}, start_skill_level={}, max_spell_level={}", c.enchanter_spell_capability.enabled ? "enabled" : "disabled", c.enchanter_spell_capability.start_skill_level, c.enchanter_spell_capability.max_spell_level);
  string a2_str = format_data_string(c.unknown_a2, sizeof(c.unknown_a2));
  w.write_fmt("  a2                                    {}", a2_str);
  w.write_fmt("  brawn_range                           [{}, {}]", c.brawn_range.low, c.brawn_range.high);
  w.write_fmt("  knowledge_range                       [{}, {}]", c.knowledge_range.low, c.knowledge_range.high);
  w.write_fmt("  judgment_range                        [{}, {}]", c.judgment_range.low, c.judgment_range.high);
  w.write_fmt("  agility_range                         [{}, {}]", c.agility_range.low, c.agility_range.high);
  w.write_fmt("  vitality_range                        [{}, {}]", c.vitality_range.low, c.vitality_range.high);
  w.write_fmt("  luck_range                            [{}, {}]", c.luck_range.low, c.luck_range.high);
  for (size_t z = 0; z < 40; z++) {
    if (c.condition_levels[z]) {
      w.write_fmt("  condition_levels[{:2}]                  {} // {}", z, c.condition_levels[z], RealmzGlobalData::name_for_condition(z));
    }
  }
  w.write_fmt("  missile_capable                       {}", c.missile_capable);
  w.write_fmt("  missile_bonus_dmg                     {}", c.missile_bonus_damage);
  w.write_fmt("  stamina_start                         {} + {}/level", c.stamina_start, c.stamina_level_up_delta);
  w.write_fmt("  strength_damage_bonus                 {}", c.strength_damage_bonus);
  w.write_fmt("  strength_damage_bonus_max             {}", c.strength_damage_bonus_max);
  w.write_fmt("  dodge_missile_chance                  {} + {}/level", c.dodge_missile_chance_start, c.dodge_missile_chance_level_up_delta);
  w.write_fmt("  melee_hit_chance                      {} + {}/level", c.melee_hit_chance_start, c.melee_hit_chance_level_up_bonus);
  w.write_fmt("  missile_hit_chance                    {} + {}/level", c.missile_hit_chance_start, c.missile_hit_chance_level_up_bonus);
  w.write_fmt("  hand_to_hand_damage                   {} + {}/level", c.hand_to_hand_damage_start, c.hand_to_hand_damage_level_up_bonus);
  string a3_str = format_data_string(c.unknown_a3, sizeof(c.unknown_a3));
  w.write_fmt("  a3                                    {}", a3_str);
  w.write_fmt("  caste_category                        {}", c.caste_category);
  w.write_fmt("  min_age_group                         {} // {}", c.min_age_group, RealmzGlobalData::name_for_age_group(c.min_age_group));
  w.write_fmt("  movement_adj                          {}", c.movement_adjust);
  w.write_fmt("  magic_resistance_mult                 {}", c.magic_resistance_mult);
  w.write_fmt("  two_handed_weapon_adj                 {}", c.two_handed_weapon_adjust);
  w.write_fmt("  max_stamina_bonus                     {}", c.max_stamina_bonus);
  w.write_fmt("  bonus_half_attacks_per_round          {}", c.bonus_half_attacks_per_round);
  w.write_fmt("  max_attacks_per_round                 {}", c.max_attacks_per_round);
  for (size_t z = 0; z < 30; z++) {
    w.write_fmt("  victory_points_until_level_{:<2}         {}", z + 2, c.victory_points_per_level[z]);
  }
  w.write_fmt("  starting_gold                         {}", c.starting_gold);
  for (size_t z = 0; z < 20; z++) {
    if (c.starting_items[z]) {
      try {
        w.write_fmt("  starting_items[{:2}]                    {} ({})", z, c.starting_items[z], this->strings_for_item(c.starting_items[z]).name);
      } catch (const out_of_range&) {
        w.write_fmt("  starting_items[{:2}]                    {}", z, c.starting_items[z]);
      }
    }
  }
  for (size_t z = 0; z < 10; z++) {
    w.write_fmt("  attacks_per_round_levels[{:2}/{}]        {}",
        static_cast<uint8_t>((z & 1) ? ((z >> 1) + 2) : (z + 3)),
        (z & 1) ? '1' : '2',
        c.attacks_per_round_level_thresholds[z]);
  }
  w.write_fmt("  can_use_item_categories               {:016X}", c.can_use_item_categories);
  uint64_t category_flags_remaining = c.can_use_item_categories;
  for (ssize_t z = 63; (z >= 0) && category_flags_remaining; z--) {
    if (category_flags_remaining & 1) {
      w.write_fmt("    {}", RealmzGlobalData::name_for_item_category_flag(z));
    }
    category_flags_remaining >>= 1;
  }
  w.write_fmt("  portrait_id                           {}", c.portrait_id);
  w.write_fmt("  max_spells_per_round                  {}", c.max_spells_per_round);
  string a4_str = format_data_string(c.unknown_a4, sizeof(c.unknown_a4));
  w.write_fmt("  a4                                    {}", a4_str);
  w.write("");
  return w.close("\n");
}

string RealmzGlobalData::disassemble_all_caste_definitions() const {
  BlockStringWriter w;
  for (size_t z = 0; z < this->caste_definitions.size(); z++) {
    const char* name = nullptr;
    try {
      name = this->caste_names.at(z).c_str();
    } catch (const out_of_range&) {
    }
    w.write(this->disassemble_caste_definition(this->caste_definitions[z], z, name));
  }
  return w.close();
}

////////////////////////////////////////////////////////////////////////////////
// DATA ID

vector<RealmzGlobalData::ItemDefinition> RealmzGlobalData::load_item_definitions(const string& filename) {
  return load_vector_file<ItemDefinition>(filename);
}

string RealmzGlobalData::disassemble_item_definition(const ItemDefinition& i, size_t item_id, const ItemStrings* strings) const {
  static const array<const char*, 26> wear_class_names = {
      /* 0 */ "ring",
      /* 1 */ "(unused-1)",
      /* 2 */ "melee weapon",
      /* 3 */ "shield",
      /* 4 */ "armor/robe",
      /* 5 */ "gauntlet/gloves",
      /* 6 */ "cloak/cape",
      /* 7 */ "helmet/cap",
      /* 8 */ "ion stone",
      /* 9 */ "boots",
      /* 10 */ "quiver",
      /* 11 */ "waist/belt",
      /* 12 */ "neck",
      /* 13 */ "scroll case",
      /* 14 */ "misc",
      /* 15 */ "missile weapon",
      /* 16 */ "brooch",
      /* 17 */ "face/mask",
      /* 18 */ "scabbard",
      /* 19 */ "belt loop",
      /* 20 */ "scroll",
      /* 21 */ "magic item",
      /* 22 */ "supply item",
      /* 23 */ "AP item",
      /* 24 */ "identified item",
      /* 25 */ "scenario item",
  };

  BlockStringWriter w;
  w.write_fmt("===== ITEM id={} [ITM{}]", item_id, item_id);

  if (strings) {
    if (!strings->name.empty()) {
      string s = format_data_string(strings->name);
      w.write_fmt("  name                        {}", s);
    }
    if (!strings->unidentified_name.empty()) {
      string s = format_data_string(strings->unidentified_name);
      w.write_fmt("  unidentified_name           {}", s);
    }
    if (!strings->description.empty()) {
      string s = format_data_string(strings->description);
      w.write_fmt("  description                 {}", s);
    }
  }

  w.write_fmt("  strength_bonus              {}", i.strength_bonus);
  w.write_fmt("  item_id                     {}", i.item_id);
  w.write_fmt("  icon_id                     {}", i.icon_id);
  w.write_fmt("  weapon_type                 {}", i.weapon_type);
  w.write_fmt("  blade_type                  {}", i.blade_type);
  w.write_fmt("  required_hands              {}", i.required_hands);
  w.write_fmt("  luck_bonus                  {}", i.luck_bonus);
  w.write_fmt("  movement                    {}", i.movement);
  w.write_fmt("  armor_rating                {}", i.armor_rating);
  w.write_fmt("  magic_resist                {}", i.magic_resist);
  w.write_fmt("  magic_plus                  {}", i.magic_plus);
  w.write_fmt("  spell_points                {}", i.spell_points);
  w.write_fmt("  sound_id                    {}", i.sound_id);
  w.write_fmt("  weight                      {}", i.weight);
  w.write_fmt("  cost                        {}", i.cost);
  w.write_fmt("  charge_count                {}", i.charge_count);
  w.write_fmt("  disguise_item_id            {}", i.disguise_item_id);
  try {
    w.write_fmt("  wear_class                  {} ({})", i.wear_class, wear_class_names.at(i.wear_class));
  } catch (const out_of_range&) {
    w.write_fmt("  wear_class                  {}", i.wear_class);
  }
  w.write_fmt("  category_flags              {:016X}", i.category_flags);
  uint64_t category_flags_remaining = i.category_flags;
  for (ssize_t z = 63; (z >= 0) && category_flags_remaining; z--) {
    if (category_flags_remaining & 1) {
      w.write_fmt("    {}", RealmzGlobalData::name_for_item_category_flag(z));
    }
    category_flags_remaining >>= 1;
  }
  w.write_fmt("  not_usable_by_race_flags    {:04X}", i.not_usable_by_race_flags);
  uint16_t race_flags_remaining = i.not_usable_by_race_flags;
  for (size_t z = 0; (z < 16) && race_flags_remaining; z++) {
    if (race_flags_remaining & 0x8000) {
      w.write_fmt("    {}", RealmzGlobalData::name_for_race_flag(z));
    }
    race_flags_remaining >>= 1;
  }
  w.write_fmt("  usable_by_races             {:04X}", i.usable_by_race_flags);
  race_flags_remaining = i.usable_by_race_flags;
  for (size_t z = 0; (z < 16) && race_flags_remaining; z++) {
    if (race_flags_remaining & 0x8000) {
      w.write_fmt("    {}", RealmzGlobalData::name_for_race_flag(z));
    }
    race_flags_remaining >>= 1;
  }
  w.write_fmt("  not_usable_by_caste_flags   {:04X}", i.not_usable_by_caste_flags);
  uint16_t caste_flags_remaining = i.not_usable_by_caste_flags;
  for (size_t z = 0; (z < 16) && caste_flags_remaining; z++) {
    if (caste_flags_remaining & 0x8000) {
      w.write_fmt("    {}", RealmzGlobalData::name_for_caste_flag(z));
    }
    caste_flags_remaining >>= 1;
  }
  w.write_fmt("  usable_by_castes            {:04X}", i.usable_by_caste_flags);
  caste_flags_remaining = i.usable_by_caste_flags;
  for (size_t z = 0; (z < 16) && caste_flags_remaining; z++) {
    if (caste_flags_remaining & 0x8000) {
      w.write_fmt("    {}", RealmzGlobalData::name_for_caste_flag(z));
    }
    caste_flags_remaining >>= 1;
  }
  try {
    w.write_fmt("  specific_race               RCE{} // {}", i.specific_race, this->race_names.at(i.specific_race));
  } catch (const out_of_range&) {
    w.write_fmt("  specific_race               RCE{}", i.specific_race);
  }
  try {
    w.write_fmt("  specific_caste              CST{} // {}", i.specific_caste, this->caste_names.at(i.specific_caste));
  } catch (const out_of_range&) {
    w.write_fmt("  specific_caste              CST{}", i.specific_caste);
  }
  string a2_str = format_data_string(i.unknown_a2, sizeof(i.unknown_a2));
  w.write_fmt("  a2                          {}", a2_str);
  w.write_fmt("  damage                      {}", i.damage);
  string a3_str = format_data_string(i.unknown_a3, sizeof(i.unknown_a3));
  w.write_fmt("  a3                          {}", a3_str);
  w.write_fmt("  heat_bonus_damage           {}", i.heat_bonus_damage);
  w.write_fmt("  cold_bonus_damage           {}", i.cold_bonus_damage);
  w.write_fmt("  electric_bonus_damage       {}", i.electric_bonus_damage);
  w.write_fmt("  undead_bonus_damage         {}", i.undead_bonus_damage);
  w.write_fmt("  demon_bonus_damage          {}", i.demon_bonus_damage);
  w.write_fmt("  evil_bonus_damage           {}", i.evil_bonus_damage);
  bool special1_is_spell = false;
  bool special1_is_condition = false;
  if (i.specials[0] <= -1 && i.specials[0] >= -7) {
    w.write_fmt("  specials[0]                 power level {}", static_cast<int16_t>(-i.specials[0]));
    special1_is_spell = true;
  } else if (i.specials[0] == 8) {
    w.write_fmt("  specials[0]                 random power level");
    special1_is_spell = true;
  } else if (i.specials[0] >= 20 && i.specials[0] < 60) {
    w.write_fmt("  specials[0]                 add condition {} ({})", static_cast<int16_t>(i.specials[0] - 20), RealmzGlobalData::name_for_condition(i.specials[0] - 20));
  } else if (i.specials[0] >= 60 && i.specials[0] < 100) {
    w.write_fmt("  specials[0]                 remove condition {} ({})", static_cast<int16_t>(i.specials[0] - 60), RealmzGlobalData::name_for_condition(i.specials[0] - 60));
  } else if (i.specials[0] == 120) {
    w.write_fmt("  specials[0]                 auto hit");
  } else if (i.specials[0] == 121) {
    w.write_fmt("  specials[0]                 double to-hit bonus");
  } else if (i.specials[0] == 122) {
    w.write_fmt("  specials[0]                 bonus attack");
  } else {
    w.write_fmt("  specials[0]                 {} (unknown)", i.specials[0]);
  }
  if (special1_is_spell) {
    try {
      const auto& name = this->name_for_spell(i.specials[1]);
      w.write_fmt("  specials[1]                 {} ({})", i.specials[1], name);
    } catch (const out_of_range&) {
      w.write_fmt("  specials[1]                 {} (unknown spell)", i.specials[1]);
    }
  } else if (special1_is_condition) {
    w.write_fmt("  specials[1]                 {} rounds{}", i.specials[1], i.specials[1] < 0 ? " (permanent)" : "");
  } else {
    w.write_fmt("  specials[1]                 {}", i.specials[1]);
  }
  // TODO: These two fields are described as:
  //   - = Special Attributes
  //   + = Special Ability
  //   30 to 40 Party Condition
  // Assign names to values appropriately here.
  if (i.specials[2] < 0) {
    w.write_fmt("  specials[2]                 {} (attribute)", i.specials[2]);
  } else {
    w.write_fmt("  specials[2]                 {} (ability)", i.specials[2]);
  }
  if (i.specials[3] < 0) {
    w.write_fmt("  specials[3]                 {} (attribute)", i.specials[3]);
  } else {
    w.write_fmt("  specials[3]                 {} (ability)", i.specials[3]);
  }
  if (i.wear_class == 23) {
    w.write_fmt("  specials[4]                 {} (AP number)", i.specials[4]);
  } else {
    w.write_fmt("  specials[4]                 {} (attr/ability amount)", i.specials[4]);
  }
  w.write_fmt("  weight_per_charge           {}", i.weight_per_charge);
  w.write_fmt("  drop_on_empty               {}", i.drop_on_empty);
  w.write("");
  return w.close("\n");
}

string RealmzGlobalData::disassemble_all_item_definitions() const {
  deque<string> blocks;
  for (size_t z = 0; z < this->item_definitions.size(); z++) {
    const ItemStrings* strings = nullptr;
    try {
      strings = &this->strings_for_item(z);
    } catch (const out_of_range&) {
    }
    blocks.emplace_back(this->disassemble_item_definition(this->item_definitions[z], z, strings));
  }
  return join(blocks, "");
}

////////////////////////////////////////////////////////////////////////////////
// DATA ID.RSF

unordered_map<uint16_t, RealmzGlobalData::ItemStrings> RealmzGlobalData::load_item_strings(const ResourceFile& rsf) {
  // Resource IDs:
  // 0 = unidentified name of weapon (e.g. Flail)
  // 1 = identified name of weapon (e.g. Flail of Cat Tails +4)
  // 2 = description of weapon (appears in info window)
  // 200, 201, 202: the above, but for armors
  // 400, 401, 402: the above, but for armors
  // 600, 601, 602: the above, but for magic items
  // 800, 801, 802: the above, but for supplies
  unordered_map<uint16_t, ItemStrings> ret;
  for (size_t base_id = 0; base_id <= 800; base_id += 200) {
    ResourceFile::DecodedStringSequence unidentified_STRN;
    ResourceFile::DecodedStringSequence identified_STRN;
    ResourceFile::DecodedStringSequence description_STRN;
    try {
      unidentified_STRN = rsf.decode_STRN(base_id + 0);
    } catch (const out_of_range&) {
    }
    try {
      identified_STRN = rsf.decode_STRN(base_id + 1);
    } catch (const out_of_range&) {
    }
    try {
      description_STRN = rsf.decode_STRN(base_id + 2);
    } catch (const out_of_range&) {
    }

    size_t class_max_id = max<size_t>({unidentified_STRN.strs.size(), identified_STRN.strs.size(), description_STRN.strs.size()});
    for (size_t z = 0; z < class_max_id; z++) {
      auto& info = ret[base_id + z];
      try {
        info.unidentified_name = std::move(unidentified_STRN.strs.at(z));
      } catch (const out_of_range&) {
      }
      try {
        info.name = std::move(identified_STRN.strs.at(z));
      } catch (const out_of_range&) {
      }
      try {
        info.description = std::move(description_STRN.strs.at(z));
      } catch (const out_of_range&) {
      }
      if (info.unidentified_name.empty() && info.name.empty() && info.description.empty()) {
        ret.erase(base_id + z);
      }
    }
  }
  return ret;
}

const RealmzGlobalData::ItemStrings& RealmzGlobalData::strings_for_item(uint16_t id) const {
  return this->item_strings.at(id);
}

////////////////////////////////////////////////////////////////////////////////
// DATA RACE

vector<RealmzGlobalData::RaceDefinition> RealmzGlobalData::load_race_definitions(const string& filename) {
  return load_vector_file<RaceDefinition>(filename);
}

string RealmzGlobalData::disassemble_race_definition(const RaceDefinition& r, size_t index, const char* name) const {
  BlockStringWriter w;
  if (name) {
    w.write_fmt("===== RACE {} [RCE{}] ({})", index, index, name);
  } else {
    w.write_fmt("===== RACE {} [RCE{}]", index, index);
  }
  w.write_fmt("  magic_using_hit_adjust                {}", r.magic_using_hit_chance_adjust);
  w.write_fmt("  undead_hit_adjust                     {}", r.undead_hit_chance_adjust);
  w.write_fmt("  demon_hit_adjust                      {}", r.demon_hit_chance_adjust);
  w.write_fmt("  reptilian_hit_adjust                  {}", r.reptilian_hit_chance_adjust);
  w.write_fmt("  evil_hit_adjust                       {}", r.evil_hit_chance_adjust);
  w.write_fmt("  intelligent_hit_adjust                {}", r.intelligent_hit_chance_adjust);
  w.write_fmt("  giant_hit_adjust                      {}", r.giant_hit_chance_adjust);
  w.write_fmt("  non_humanoid_hit_adjust               {}", r.non_humanoid_hit_chance_adjust);
  w.write_fmt("  special_abilities_adjust");
  disassemble_special_abilities(w, r.special_ability_adjust);
  w.write_fmt("  drvs_adjust");
  disassemble_drvs_abilities(w, r.drv_adjust);
  w.write_fmt("  a1                                    {:02X}{:02X}", r.unknown_a1[0], r.unknown_a1[1]);
  w.write_fmt("  brawn_adjust                          {}", r.brawn_adjust);
  w.write_fmt("  knowledge_adjust                      {}", r.knowledge_adjust);
  w.write_fmt("  judgment_adjust                       {}", r.judgment_adjust);
  w.write_fmt("  agility_adjust                        {}", r.agility_adjust);
  w.write_fmt("  vitality_adjust                       {}", r.vitality_adjust);
  w.write_fmt("  luck_adjust                           {}", r.luck_adjust);
  w.write_fmt("  brawn_range                           [{}, {}]", r.brawn_range.low, r.brawn_range.high);
  w.write_fmt("  knowledge_range                       [{}, {}]", r.knowledge_range.low, r.knowledge_range.high);
  w.write_fmt("  judgment_range                        [{}, {}]", r.judgment_range.low, r.judgment_range.high);
  w.write_fmt("  agility_range                         [{}, {}]", r.agility_range.low, r.agility_range.high);
  w.write_fmt("  vitality_range                        [{}, {}]", r.vitality_range.low, r.vitality_range.high);
  w.write_fmt("  luck_range                            [{}, {}]", r.luck_range.low, r.luck_range.high);
  string a2_str = format_data_string(r.unknown_a2, sizeof(r.unknown_a2));
  w.write_fmt("  a2                                    {}", a2_str);
  for (size_t z = 0; z < 40; z++) {
    if (r.condition_levels[z]) {
      w.write_fmt("  condition_levels[{:2}]                  {} // {}", z, r.condition_levels[z], RealmzGlobalData::name_for_condition(z));
    }
  }
  string a3_str = format_data_string(r.unknown_a3, sizeof(r.unknown_a3));
  w.write_fmt("  a3                                    {}", a3_str);
  w.write_fmt("  base_movement                         {}", r.base_movement);
  w.write_fmt("  magic_resistance_adjust               {}", r.magic_resistance_adjust);
  w.write_fmt("  two_handed_weapon_adjust              {}", r.two_handed_weapon_adjust);
  w.write_fmt("  missile_weapon_adjust                 {}", r.missile_weapon_adjust);
  w.write_fmt("  base_half_attacks                     {}", r.base_half_attacks);
  w.write_fmt("  max_attacks_per_round                 {}", r.max_attacks_per_round);
  w.write_fmt("  possible_castes");
  for (size_t z = 0; z < 30; z++) {
    if (r.possible_castes[z]) {
      try {
        const string& name = this->caste_names.at(z);
        if (name.empty()) {
          w.write_fmt("    CST{}", z);
        } else {
          w.write_fmt("    CST{} ({})", z, name);
        }
      } catch (const out_of_range&) {
        w.write_fmt("    CST{}", z);
      }
    }
  }
  for (size_t z = 0; z < 5; z++) {
    w.write_fmt("  age_ranges[{}]                         [{}, {}]", z, r.age_ranges[z].low, r.age_ranges[z].high);
    w.write_fmt("    brawn                               {}", r.age_adjust[z].brawn);
    w.write_fmt("    knowledge                           {}", r.age_adjust[z].knowledge);
    w.write_fmt("    judgement                           {}", r.age_adjust[z].judgement);
    w.write_fmt("    agility                             {}", r.age_adjust[z].agility);
    w.write_fmt("    vitality                            {}", r.age_adjust[z].vitality);
    w.write_fmt("    luck                                {}", r.age_adjust[z].luck);
    w.write_fmt("    magic_resistance                    {}", r.age_adjust[z].magic_resistance);
    w.write_fmt("    movement                            {}", r.age_adjust[z].movement);
    w.write_fmt("    drv_charm                           {}", r.age_adjust[z].drv_chance_charm);
    w.write_fmt("    drv_heat                            {}", r.age_adjust[z].drv_chance_heat);
    w.write_fmt("    drv_cold                            {}", r.age_adjust[z].drv_chance_cold);
    w.write_fmt("    drv_electric                        {}", r.age_adjust[z].drv_chance_electric);
    w.write_fmt("    drv_chemical                        {}", r.age_adjust[z].drv_chance_chemical);
    w.write_fmt("    drv_mental                          {}", r.age_adjust[z].drv_chance_mental);
    w.write_fmt("    drv_magic                           {}", r.age_adjust[z].drv_chance_magic);
  }
  w.write_fmt("  can_regenerate                        {}", r.can_regenerate ? "true" : "false");
  w.write_fmt("  icon_set_number                       {}", r.icon_set_number);
  w.write_fmt("  can_use_item_categories               {:016X}", r.can_use_item_categories);
  uint64_t category_flags_remaining = r.can_use_item_categories;
  for (ssize_t z = 63; (z >= 0) && category_flags_remaining; z--) {
    if (category_flags_remaining & 1) {
      w.write_fmt("    {}", RealmzGlobalData::name_for_item_category_flag(z));
    }
    category_flags_remaining >>= 1;
  }
  w.write_fmt("  race_flags                            {}", r.race_flags);
  uint16_t race_flags_remaining = r.race_flags;
  for (ssize_t z = 0; (z < 16) && race_flags_remaining; z++) {
    if (race_flags_remaining & 0x8000) {
      w.write_fmt("    {}", RealmzGlobalData::name_for_race_flag(z));
    }
    race_flags_remaining <<= 1;
  }
  string a4_str = format_data_string(r.unknown_a4, sizeof(r.unknown_a4));
  w.write_fmt("  a4                                    {}", a4_str);
  w.write("");
  return w.close("\n");
}

string RealmzGlobalData::disassemble_all_race_definitions() const {
  BlockStringWriter w;
  for (size_t z = 0; z < this->race_definitions.size(); z++) {
    const char* name = nullptr;
    try {
      name = this->race_names.at(z).c_str();
    } catch (const out_of_range&) {
    }
    w.write(this->disassemble_race_definition(this->race_definitions[z], z, name));
  }
  return w.close();
}

////////////////////////////////////////////////////////////////////////////////
// DATA S

map<uint16_t, RealmzGlobalData::SpellDefinition> RealmzGlobalData::load_spell_definitions(const string& filename) {
  auto f = fopen_unique(filename, "rb");

  map<uint16_t, SpellDefinition> ret;
  for (size_t x = 0; x < 5; x++) {
    for (size_t y = 0; y < 7; y++) {
      for (size_t z = 0; z < 15; z++) {
        uint16_t spell_id = ((x + 1) * 1000) + ((y + 1) * 100) + (z + 1);
        auto& def = ret[spell_id];
        freadx(f.get(), &def, sizeof(def));
      }
    }
  }
  return ret;
}

string RealmzGlobalData::disassemble_spell_definition(const SpellDefinition& s, uint16_t spell_id, const char* name) const {
  BlockStringWriter w;
  if (name) {
    w.write_fmt("===== SPELL id={} [SPL{}] ({})", spell_id, spell_id, name);
  } else {
    w.write_fmt("===== SPELL id={} [SPL{}]", spell_id, spell_id);
  }
  w.write_fmt("  range                       {} + {}/level", s.base_range, s.power_range);
  w.write_fmt("  que_icon                    {}", s.que_icon);
  w.write_fmt("  hit_chance_adjust           {}", s.hit_chance_adjust);
  w.write_fmt("  drv_adjust                  {}", s.drv_adjust);
  w.write_fmt("  num_attacks                 {}", s.num_attacks);
  w.write_fmt("  can_rotate                  {}", s.can_rotate);
  w.write_fmt("  drv_adjust                  {}/level", s.drv_adjust_per_level);
  w.write_fmt("  resist_type                 {}", s.resist_type);
  w.write_fmt("  resist_adjust               {}/level", s.resist_adjust_per_level);
  w.write_fmt("  base_cost                   {}", s.base_cost); // TODO: Can be negative; what does that mean?
  w.write_fmt("  damage                      [{}, {}] + [{}, {}]/level", s.damage_base_low, s.damage_base_high, s.damage_per_level_low, s.damage_per_level_high);
  w.write_fmt("  duration                    [{}, {}] + [{}, {}]/level", s.duration_base_low, s.duration_base_high, s.duration_per_level_low, s.duration_per_level_high);
  w.write_fmt("  cast_media                  icon={}, sound={}", s.cast_icon, s.cast_sound);
  w.write_fmt("  resolution_media            icon={}, sound={}", s.resolution_icon, s.resolution_sound);
  w.write_fmt("  target_type                 {}", s.target_type);
  w.write_fmt("  size                        {}", s.size);
  w.write_fmt("  effect                      {}", s.effect);
  w.write_fmt("  spell_class                 {}", s.spell_class);
  w.write_fmt("  damage_type                 {}", s.damage_type);
  w.write_fmt("  usable_in_combat            {}", s.usable_in_combat);
  w.write_fmt("  usable_in_camp              {}", s.usable_in_camp);
  w.write("");
  return w.close("\n");
}

string RealmzGlobalData::disassemble_all_spell_definitions() const {
  BlockStringWriter w;
  for (const auto& it : this->spell_definitions) {
    const char* name = nullptr;
    try {
      name = this->name_for_spell(it.first).c_str();
    } catch (const out_of_range&) {
    }
    w.write(this->disassemble_spell_definition(it.second, it.first, name));
  }
  return w.close();
}

} // namespace ResourceDASM
