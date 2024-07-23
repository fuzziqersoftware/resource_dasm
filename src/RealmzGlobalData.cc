#include "RealmzGlobalData.hh"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
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
    struct stat st;
    if (stat(it.c_str(), &st) == 0) {
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
      filenames.emplace_back(string_printf("%s/%s", this->dir.c_str(), filename.c_str()));
    }

    string filename = first_file_that_exists(filenames);
    if (!filename.empty()) {
      this->land_type_to_tileset_definition.emplace(it.first, load_tileset_definition(filename));
    } else {
      fprintf(stderr, "warning: tileset definition for %s is missing\n", it.first.c_str());
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
    result.draw_text(1, 97 * x + 1, text_color, "%04zX", x + 1);
    result.draw_text(1, 97 * x + 17, text_color, "SOUND\n%04X", t.sound_id.load());

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

string RealmzGlobalData::disassemble_tileset_definition(const TileSetDefinition& ts, const char* name) {
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
        (x == static_cast<size_t>(ts.base_tile_id)) ? "BASE" : "    ",
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

const string& RealmzGlobalData::name_for_spell(uint16_t id) const {
  return this->spell_names.at(id);
}

////////////////////////////////////////////////////////////////////////////////
// DATA CASTE

static void disassemble_special_abilities(BlockStringWriter& w, const RealmzGlobalData::SpecialAbilities& sa) {
  w.write_printf("    sneak_attack                        %hd", sa.sneak_attack.load());
  w.write_printf("    unknown_a1[0]                       %hd", sa.unknown_a1[0].load());
  w.write_printf("    unknown_a1[1]                       %hd", sa.unknown_a1[1].load());
  w.write_printf("    major_wound                         %hd", sa.major_wound.load());
  w.write_printf("    detect_secret                       %hd", sa.detect_secret.load());
  w.write_printf("    acrobatic_act                       %hd", sa.acrobatic_act.load());
  w.write_printf("    detect_trap                         %hd", sa.detect_trap.load());
  w.write_printf("    disarm_trap                         %hd", sa.disarm_trap.load());
  w.write_printf("    unknown_a2                          %hd", sa.unknown_a2.load());
  w.write_printf("    force_lock                          %hd", sa.force_lock.load());
  w.write_printf("    unknown_a3                          %hd", sa.unknown_a3.load());
  w.write_printf("    pick_lock                           %hd", sa.pick_lock.load());
  w.write_printf("    unknown_a4                          %hd", sa.unknown_a4.load());
  w.write_printf("    turn_undead                         %hd", sa.turn_undead.load());
}

static void disassemble_drvs_abilities(BlockStringWriter& w, const RealmzGlobalData::DRVsAbilities& drv) {
  w.write_printf("    charm                               %hd", drv.charm.load());
  w.write_printf("    heat                                %hd", drv.heat.load());
  w.write_printf("    cold                                %hd", drv.cold.load());
  w.write_printf("    electric                            %hd", drv.electric.load());
  w.write_printf("    chemical                            %hd", drv.chemical.load());
  w.write_printf("    mental                              %hd", drv.mental.load());
  w.write_printf("    magical                             %hd", drv.magical.load());
}

vector<RealmzGlobalData::CasteDefinition> RealmzGlobalData::load_caste_definitions(const string& filename) {
  return load_vector_file<CasteDefinition>(filename);
}

string RealmzGlobalData::disassemble_caste_definition(const CasteDefinition& c, size_t index, const char* name) const {
  BlockStringWriter w;
  if (name) {
    w.write_printf("===== CASTE %zu [CST%zu] (%s)", index, index, name);
  } else {
    w.write_printf("===== CASTE %zu [CST%zu]", index, index);
  }
  w.write_printf("  special_abilities_start");
  disassemble_special_abilities(w, c.special_abilities_start);
  w.write_printf("  special_abilities_level_up_delta");
  disassemble_special_abilities(w, c.special_abilities_level_up_delta);
  w.write_printf("  drv_adjust");
  disassemble_drvs_abilities(w, c.drv_adjust);
  w.write_printf("  a1                                    %hd", c.unknown_a1.load());
  w.write_printf("  brawn_adjust                          %hd", c.brawn_adjust.load());
  w.write_printf("  knowledge_adjust                      %hd", c.knowledge_adjust.load());
  w.write_printf("  judgment_adjust                       %hd", c.judgment_adjust.load());
  w.write_printf("  agility_adjust                        %hd", c.agility_adjust.load());
  w.write_printf("  vitality_adjust                       %hd", c.vitality_adjust.load());
  w.write_printf("  luck_adjust                           %hd", c.luck_adjust.load());
  w.write_printf("  sorcerer_spells                       %s, start_skill_level=%hu, max_spell_level=%hu", c.sorcerer_spell_capability.enabled ? "enabled" : "disabled", c.sorcerer_spell_capability.start_skill_level.load(), c.sorcerer_spell_capability.max_spell_level.load());
  w.write_printf("  priest_spells                         %s, start_skill_level=%hu, max_spell_level=%hu", c.priest_spell_capability.enabled ? "enabled" : "disabled", c.priest_spell_capability.start_skill_level.load(), c.priest_spell_capability.max_spell_level.load());
  w.write_printf("  enchanter_spells                      %s, start_skill_level=%hu, max_spell_level=%hu", c.enchanter_spell_capability.enabled ? "enabled" : "disabled", c.enchanter_spell_capability.start_skill_level.load(), c.enchanter_spell_capability.max_spell_level.load());
  string a2_str = format_data_string(c.unknown_a2, sizeof(c.unknown_a2));
  w.write_printf("  a2                                    %s", a2_str.c_str());
  w.write_printf("  brawn_range                           [%hd, %hd]", c.brawn_range.low.load(), c.brawn_range.high.load());
  w.write_printf("  knowledge_range                       [%hd, %hd]", c.knowledge_range.low.load(), c.knowledge_range.high.load());
  w.write_printf("  judgment_range                        [%hd, %hd]", c.judgment_range.low.load(), c.judgment_range.high.load());
  w.write_printf("  agility_range                         [%hd, %hd]", c.agility_range.low.load(), c.agility_range.high.load());
  w.write_printf("  vitality_range                        [%hd, %hd]", c.vitality_range.low.load(), c.vitality_range.high.load());
  w.write_printf("  luck_range                            [%hd, %hd]", c.luck_range.low.load(), c.luck_range.high.load());
  for (size_t z = 0; z < 40; z++) {
    if (c.condition_levels[z]) {
      w.write_printf("  condition_levels[%2zu]                  %hd // %s", z, c.condition_levels[z].load(), RealmzGlobalData::name_for_condition(z));
    }
  }
  w.write_printf("  missile_capable                       %hd", c.missile_capable.load());
  w.write_printf("  missile_bonus_dmg                     %hd", c.missile_bonus_damage.load());
  w.write_printf("  stamina_start                         %hd + %hd/level", c.stamina_start.load(), c.stamina_level_up_delta.load());
  w.write_printf("  strength_damage_bonus                 %hd", c.strength_damage_bonus.load());
  w.write_printf("  strength_damage_bonus_max             %hd", c.strength_damage_bonus_max.load());
  w.write_printf("  dodge_missile_chance                  %hd + %hd/level", c.dodge_missile_chance_start.load(), c.dodge_missile_chance_level_up_delta.load());
  w.write_printf("  melee_hit_chance                      %hd + %hd/level", c.melee_hit_chance_start.load(), c.melee_hit_chance_level_up_bonus.load());
  w.write_printf("  missile_hit_chance                    %hd + %hd/level", c.missile_hit_chance_start.load(), c.missile_hit_chance_level_up_bonus.load());
  w.write_printf("  hand_to_hand_damage                   %hd + %hd/level", c.hand_to_hand_damage_start.load(), c.hand_to_hand_damage_level_up_bonus.load());
  string a3_str = format_data_string(c.unknown_a3, sizeof(c.unknown_a3));
  w.write_printf("  a3                                    %s", a3_str.c_str());
  w.write_printf("  caste_category                        %hd", c.caste_category.load());
  w.write_printf("  min_age_group                         %hd // %s", c.min_age_group.load(), RealmzGlobalData::name_for_age_group(c.min_age_group));
  w.write_printf("  movement_adj                          %hd", c.movement_adjust.load());
  w.write_printf("  magic_resistance_mult                 %hd", c.magic_resistance_mult.load());
  w.write_printf("  two_handed_weapon_adj                 %hd", c.two_handed_weapon_adjust.load());
  w.write_printf("  max_stamina_bonus                     %hd", c.max_stamina_bonus.load());
  w.write_printf("  bonus_half_attacks_per_round          %hd", c.bonus_half_attacks_per_round.load());
  w.write_printf("  max_attacks_per_round                 %hd", c.max_attacks_per_round.load());
  for (size_t z = 0; z < 30; z++) {
    w.write_printf("  victory_points_until_level_%-2zu         %" PRIu32, z + 2, c.victory_points_per_level[z].load());
  }
  w.write_printf("  starting_gold                         %hd", c.starting_gold.load());
  for (size_t z = 0; z < 20; z++) {
    if (c.starting_items[z]) {
      try {
        w.write_printf("  starting_items[%2zu]                    %hd (%s)", z, c.starting_items[z].load(), this->strings_for_item(c.starting_items[z]).name.c_str());
      } catch (const out_of_range&) {
        w.write_printf("  starting_items[%2zu]                    %hd", z, c.starting_items[z].load());
      }
    }
  }
  for (size_t z = 0; z < 10; z++) {
    w.write_printf("  attacks_per_round_levels[%2hhu/%c]        %hhu",
        static_cast<uint8_t>((z & 1) ? ((z >> 1) + 2) : (z + 3)),
        (z & 1) ? '1' : '2',
        c.attacks_per_round_level_thresholds[z]);
  }
  w.write_printf("  can_use_item_categories               %016" PRIX64, c.can_use_item_categories.load());
  uint64_t category_flags_remaining = c.can_use_item_categories;
  for (ssize_t z = 63; (z >= 0) && category_flags_remaining; z--) {
    if (category_flags_remaining & 1) {
      w.write_printf("    %s", RealmzGlobalData::name_for_item_category_flag(z));
    }
    category_flags_remaining >>= 1;
  }
  w.write_printf("  portrait_id                           %hd", c.portrait_id.load());
  w.write_printf("  max_spells_per_round                  %hd", c.max_spells_per_round.load());
  string a4_str = format_data_string(c.unknown_a4, sizeof(c.unknown_a4));
  w.write_printf("  a4                                    %s", a4_str.c_str());
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
  w.write_printf("===== ITEM id=%zu [ITM%zu]", item_id, item_id);

  if (strings) {
    if (!strings->name.empty()) {
      string s = format_data_string(strings->name);
      w.write_printf("  name                        %s", s.c_str());
    }
    if (!strings->unidentified_name.empty()) {
      string s = format_data_string(strings->unidentified_name);
      w.write_printf("  unidentified_name           %s", s.c_str());
    }
    if (!strings->description.empty()) {
      string s = format_data_string(strings->description);
      w.write_printf("  description                 %s", s.c_str());
    }
  }

  w.write_printf("  strength_bonus              %hd", i.strength_bonus.load());
  w.write_printf("  item_id                     %hu", i.item_id.load());
  w.write_printf("  icon_id                     %hd", i.icon_id.load());
  w.write_printf("  weapon_type                 %hu", i.weapon_type.load());
  w.write_printf("  blade_type                  %hd", i.blade_type.load());
  w.write_printf("  required_hands              %hd", i.required_hands.load());
  w.write_printf("  luck_bonus                  %hd", i.luck_bonus.load());
  w.write_printf("  movement                    %hd", i.movement.load());
  w.write_printf("  armor_rating                %hd", i.armor_rating.load());
  w.write_printf("  magic_resist                %hd", i.magic_resist.load());
  w.write_printf("  magic_plus                  %hd", i.magic_plus.load());
  w.write_printf("  spell_points                %hd", i.spell_points.load());
  w.write_printf("  sound_id                    %hd", i.sound_id.load());
  w.write_printf("  weight                      %hd", i.weight.load());
  w.write_printf("  cost                        %hd", i.cost.load());
  w.write_printf("  charge_count                %hd", i.charge_count.load());
  w.write_printf("  disguise_item_id            %hu", i.disguise_item_id.load());
  try {
    w.write_printf("  wear_class                  %hu (%s)", i.wear_class.load(), wear_class_names.at(i.wear_class));
  } catch (const out_of_range&) {
    w.write_printf("  wear_class                  %hu", i.wear_class.load());
  }
  w.write_printf("  category_flags              %016" PRIX64, i.category_flags.load());
  uint64_t category_flags_remaining = i.category_flags;
  for (ssize_t z = 63; (z >= 0) && category_flags_remaining; z--) {
    if (category_flags_remaining & 1) {
      w.write_printf("    %s", RealmzGlobalData::name_for_item_category_flag(z));
    }
    category_flags_remaining >>= 1;
  }
  w.write_printf("  not_usable_by_race_flags    %04hX", i.not_usable_by_race_flags.load());
  uint16_t race_flags_remaining = i.not_usable_by_race_flags;
  for (size_t z = 0; (z < 16) && race_flags_remaining; z++) {
    if (race_flags_remaining & 0x8000) {
      w.write_printf("    %s", RealmzGlobalData::name_for_race_flag(z));
    }
    race_flags_remaining >>= 1;
  }
  w.write_printf("  usable_by_races             %04hX", i.usable_by_race_flags.load());
  race_flags_remaining = i.usable_by_race_flags;
  for (size_t z = 0; (z < 16) && race_flags_remaining; z++) {
    if (race_flags_remaining & 0x8000) {
      w.write_printf("    %s", RealmzGlobalData::name_for_race_flag(z));
    }
    race_flags_remaining >>= 1;
  }
  w.write_printf("  not_usable_by_caste_flags   %04hX", i.not_usable_by_caste_flags.load());
  uint16_t caste_flags_remaining = i.not_usable_by_caste_flags;
  for (size_t z = 0; (z < 16) && caste_flags_remaining; z++) {
    if (caste_flags_remaining & 0x8000) {
      w.write_printf("    %s", RealmzGlobalData::name_for_caste_flag(z));
    }
    caste_flags_remaining >>= 1;
  }
  w.write_printf("  usable_by_castes            %04hX", i.usable_by_caste_flags.load());
  caste_flags_remaining = i.usable_by_caste_flags;
  for (size_t z = 0; (z < 16) && caste_flags_remaining; z++) {
    if (caste_flags_remaining & 0x8000) {
      w.write_printf("    %s", RealmzGlobalData::name_for_caste_flag(z));
    }
    caste_flags_remaining >>= 1;
  }
  try {
    w.write_printf("  specific_race               RCE%hu // %s", i.specific_race.load(), this->race_names.at(i.specific_race).c_str());
  } catch (const out_of_range&) {
    w.write_printf("  specific_race               RCE%hu", i.specific_race.load());
  }
  try {
    w.write_printf("  specific_caste              CST%hu // %s", i.specific_caste.load(), this->caste_names.at(i.specific_caste).c_str());
  } catch (const out_of_range&) {
    w.write_printf("  specific_caste              CST%hu", i.specific_caste.load());
  }
  string a2_str = format_data_string(i.unknown_a2, sizeof(i.unknown_a2));
  w.write_printf("  a2                          %s", a2_str.c_str());
  w.write_printf("  damage                      %hd", i.damage.load());
  string a3_str = format_data_string(i.unknown_a3, sizeof(i.unknown_a3));
  w.write_printf("  a3                          %s", a3_str.c_str());
  w.write_printf("  heat_bonus_damage           %hd", i.heat_bonus_damage.load());
  w.write_printf("  cold_bonus_damage           %hd", i.cold_bonus_damage.load());
  w.write_printf("  electric_bonus_damage       %hd", i.electric_bonus_damage.load());
  w.write_printf("  undead_bonus_damage         %hd", i.undead_bonus_damage.load());
  w.write_printf("  demon_bonus_damage          %hd", i.demon_bonus_damage.load());
  w.write_printf("  evil_bonus_damage           %hd", i.evil_bonus_damage.load());
  bool special1_is_spell = false;
  bool special1_is_condition = false;
  if (i.specials[0] <= -1 && i.specials[0] >= -7) {
    w.write_printf("  specials[0]                 power level %hd", static_cast<int16_t>(-i.specials[0]));
    special1_is_spell = true;
  } else if (i.specials[0] == 8) {
    w.write_printf("  specials[0]                 random power level");
    special1_is_spell = true;
  } else if (i.specials[0] >= 20 && i.specials[0] < 60) {
    w.write_printf("  specials[0]                 add condition %hd (%s)", static_cast<int16_t>(i.specials[0] - 20), RealmzGlobalData::name_for_condition(i.specials[0] - 20));
  } else if (i.specials[0] >= 60 && i.specials[0] < 100) {
    w.write_printf("  specials[0]                 remove condition %hd (%s)", static_cast<int16_t>(i.specials[0] - 60), RealmzGlobalData::name_for_condition(i.specials[0] - 60));
  } else if (i.specials[0] == 120) {
    w.write_printf("  specials[0]                 auto hit");
  } else if (i.specials[0] == 121) {
    w.write_printf("  specials[0]                 double to-hit bonus");
  } else if (i.specials[0] == 122) {
    w.write_printf("  specials[0]                 bonus attack");
  } else {
    w.write_printf("  specials[0]                 %hd (unknown)", i.specials[0].load());
  }
  if (special1_is_spell) {
    try {
      const auto& name = this->name_for_spell(i.specials[1]);
      w.write_printf("  specials[1]                 %hd (%s)", i.specials[1].load(), name.c_str());
    } catch (const out_of_range&) {
      w.write_printf("  specials[1]                 %hd (unknown spell)", i.specials[1].load());
    }
  } else if (special1_is_condition) {
    w.write_printf("  specials[1]                 %hd rounds%s", i.specials[1].load(), i.specials[1] < 0 ? " (permanent)" : "");
  } else {
    w.write_printf("  specials[1]                 %hd", i.specials[1].load());
  }
  // TODO: These two fields are described as:
  //   - = Special Attributes
  //   + = Special Ability
  //   30 to 40 Party Condition
  // Assign names to values appropriately here.
  if (i.specials[2] < 0) {
    w.write_printf("  specials[2]                 %hd (attribute)", i.specials[2].load());
  } else {
    w.write_printf("  specials[2]                 %hd (ability)", i.specials[2].load());
  }
  if (i.specials[3] < 0) {
    w.write_printf("  specials[3]                 %hd (attribute)", i.specials[3].load());
  } else {
    w.write_printf("  specials[3]                 %hd (ability)", i.specials[3].load());
  }
  if (i.wear_class == 23) {
    w.write_printf("  specials[4]                 %hd (AP number)", i.specials[4].load());
  } else {
    w.write_printf("  specials[4]                 %hd (attr/ability amount)", i.specials[4].load());
  }
  w.write_printf("  weight_per_charge           %hd", i.weight_per_charge.load());
  w.write_printf("  drop_on_empty               %hu", i.drop_on_empty.load());
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
    w.write_printf("===== RACE %zu [RCE%zu] (%s)", index, index, name);
  } else {
    w.write_printf("===== RACE %zu [RCE%zu]", index, index);
  }
  w.write_printf("  magic_using_hit_adjust                %hd", r.magic_using_hit_chance_adjust.load());
  w.write_printf("  undead_hit_adjust                     %hd", r.undead_hit_chance_adjust.load());
  w.write_printf("  demon_hit_adjust                      %hd", r.demon_hit_chance_adjust.load());
  w.write_printf("  reptilian_hit_adjust                  %hd", r.reptilian_hit_chance_adjust.load());
  w.write_printf("  evil_hit_adjust                       %hd", r.evil_hit_chance_adjust.load());
  w.write_printf("  intelligent_hit_adjust                %hd", r.intelligent_hit_chance_adjust.load());
  w.write_printf("  giant_hit_adjust                      %hd", r.giant_hit_chance_adjust.load());
  w.write_printf("  non_humanoid_hit_adjust               %hd", r.non_humanoid_hit_chance_adjust.load());
  w.write_printf("  special_abilities_adjust");
  disassemble_special_abilities(w, r.special_ability_adjust);
  w.write_printf("  drvs_adjust");
  disassemble_drvs_abilities(w, r.drv_adjust);
  w.write_printf("  a1                                    %02hhX%02hhX", r.unknown_a1[0], r.unknown_a1[1]);
  w.write_printf("  brawn_adjust                          %hd", r.brawn_adjust.load());
  w.write_printf("  knowledge_adjust                      %hd", r.knowledge_adjust.load());
  w.write_printf("  judgment_adjust                       %hd", r.judgment_adjust.load());
  w.write_printf("  agility_adjust                        %hd", r.agility_adjust.load());
  w.write_printf("  vitality_adjust                       %hd", r.vitality_adjust.load());
  w.write_printf("  luck_adjust                           %hd", r.luck_adjust.load());
  w.write_printf("  brawn_range                           [%hd, %hd]", r.brawn_range.low.load(), r.brawn_range.high.load());
  w.write_printf("  knowledge_range                       [%hd, %hd]", r.knowledge_range.low.load(), r.knowledge_range.high.load());
  w.write_printf("  judgment_range                        [%hd, %hd]", r.judgment_range.low.load(), r.judgment_range.high.load());
  w.write_printf("  agility_range                         [%hd, %hd]", r.agility_range.low.load(), r.agility_range.high.load());
  w.write_printf("  vitality_range                        [%hd, %hd]", r.vitality_range.low.load(), r.vitality_range.high.load());
  w.write_printf("  luck_range                            [%hd, %hd]", r.luck_range.low.load(), r.luck_range.high.load());
  string a2_str = format_data_string(r.unknown_a2, sizeof(r.unknown_a2));
  w.write_printf("  a2                                    %s", a2_str.c_str());
  for (size_t z = 0; z < 40; z++) {
    if (r.condition_levels[z]) {
      w.write_printf("  condition_levels[%2zu]                  %hd // %s", z, r.condition_levels[z].load(), RealmzGlobalData::name_for_condition(z));
    }
  }
  string a3_str = format_data_string(r.unknown_a3, sizeof(r.unknown_a3));
  w.write_printf("  a3                                    %s", a3_str.c_str());
  w.write_printf("  base_movement                         %hd", r.base_movement.load());
  w.write_printf("  magic_resistance_adjust               %hd", r.magic_resistance_adjust.load());
  w.write_printf("  two_handed_weapon_adjust              %hd", r.two_handed_weapon_adjust.load());
  w.write_printf("  missile_weapon_adjust                 %hd", r.missile_weapon_adjust.load());
  w.write_printf("  base_half_attacks                     %hd", r.base_half_attacks.load());
  w.write_printf("  max_attacks_per_round                 %hd", r.max_attacks_per_round.load());
  w.write_printf("  possible_castes");
  for (size_t z = 0; z < 30; z++) {
    if (r.possible_castes[z]) {
      try {
        const string& name = this->caste_names.at(z);
        if (name.empty()) {
          w.write_printf("    CST%zu", z);
        } else {
          w.write_printf("    CST%zu (%s)", z, name.c_str());
        }
      } catch (const out_of_range&) {
        w.write_printf("    CST%zu", z);
      }
    }
  }
  for (size_t z = 0; z < 5; z++) {
    w.write_printf("  age_ranges[%zu]                         [%hd, %hd]", z, r.age_ranges[z].low.load(), r.age_ranges[z].high.load());
    w.write_printf("    brawn                               %hhd", r.age_adjust[z].brawn);
    w.write_printf("    knowledge                           %hhd", r.age_adjust[z].knowledge);
    w.write_printf("    judgement                           %hhd", r.age_adjust[z].judgement);
    w.write_printf("    agility                             %hhd", r.age_adjust[z].agility);
    w.write_printf("    vitality                            %hhd", r.age_adjust[z].vitality);
    w.write_printf("    luck                                %hhd", r.age_adjust[z].luck);
    w.write_printf("    magic_resistance                    %hhd", r.age_adjust[z].magic_resistance);
    w.write_printf("    movement                            %hhd", r.age_adjust[z].movement);
    w.write_printf("    drv_charm                           %hhd", r.age_adjust[z].drv_chance_charm);
    w.write_printf("    drv_heat                            %hhd", r.age_adjust[z].drv_chance_heat);
    w.write_printf("    drv_cold                            %hhd", r.age_adjust[z].drv_chance_cold);
    w.write_printf("    drv_electric                        %hhd", r.age_adjust[z].drv_chance_electric);
    w.write_printf("    drv_chemical                        %hhd", r.age_adjust[z].drv_chance_chemical);
    w.write_printf("    drv_mental                          %hhd", r.age_adjust[z].drv_chance_mental);
    w.write_printf("    drv_magic                           %hhd", r.age_adjust[z].drv_chance_magic);
  }
  w.write_printf("  can_regenerate                        %s", r.can_regenerate ? "true" : "false");
  w.write_printf("  icon_set_number                       %hd", r.icon_set_number.load());
  w.write_printf("  can_use_item_categories               %016" PRIX64, r.can_use_item_categories.load());
  uint64_t category_flags_remaining = r.can_use_item_categories;
  for (ssize_t z = 63; (z >= 0) && category_flags_remaining; z--) {
    if (category_flags_remaining & 1) {
      w.write_printf("    %s", RealmzGlobalData::name_for_item_category_flag(z));
    }
    category_flags_remaining >>= 1;
  }
  w.write_printf("  race_flags                            %hu", r.race_flags.load());
  uint16_t race_flags_remaining = r.race_flags;
  for (ssize_t z = 0; (z < 16) && race_flags_remaining; z++) {
    if (race_flags_remaining & 0x8000) {
      w.write_printf("    %s", RealmzGlobalData::name_for_race_flag(z));
    }
    race_flags_remaining <<= 1;
  }
  string a4_str = format_data_string(r.unknown_a4, sizeof(r.unknown_a4));
  w.write_printf("  a4                                    %s", a4_str.c_str());
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
    w.write_printf("===== SPELL id=%hu [SPL%hu] (%s)", spell_id, spell_id, name);
  } else {
    w.write_printf("===== SPELL id=%hu [SPL%hu]", spell_id, spell_id);
  }
  w.write_printf("  range                       %hhd + %hhd/level", s.base_range, s.power_range);
  w.write_printf("  que_icon                    %hhd", s.que_icon);
  w.write_printf("  hit_chance_adjust           %hhd", s.hit_chance_adjust);
  w.write_printf("  drv_adjust                  %hhd", s.drv_adjust);
  w.write_printf("  num_attacks                 %hhd", s.num_attacks);
  w.write_printf("  can_rotate                  %hhd", s.can_rotate);
  w.write_printf("  drv_adjust                  %hhd/level", s.drv_adjust_per_level);
  w.write_printf("  resist_type                 %hhd", s.resist_type);
  w.write_printf("  resist_adjust               %hhd/level", s.resist_adjust_per_level);
  w.write_printf("  base_cost                   %hhd", s.base_cost); // TODO: Can be negative; what does that mean?
  w.write_printf("  damage                      [%hhd, %hhd] + [%hhd, %hhd]/level", s.damage_base_low, s.damage_base_high, s.damage_per_level_low, s.damage_per_level_high);
  w.write_printf("  duration                    [%hhd, %hhd] + [%hhd, %hhd]/level", s.duration_base_low, s.duration_base_high, s.duration_per_level_low, s.duration_per_level_high);
  w.write_printf("  cast_media                  icon=%hhd, sound=%hhd", s.cast_icon, s.cast_sound);
  w.write_printf("  resolution_media            icon=%hhd, sound=%hhd", s.resolution_icon, s.resolution_sound);
  w.write_printf("  target_type                 %hhd", s.target_type);
  w.write_printf("  size                        %hhd", s.size);
  w.write_printf("  effect                      %hhd", s.effect);
  w.write_printf("  spell_class                 %hhd", s.spell_class);
  w.write_printf("  damage_type                 %hhd", s.damage_type);
  w.write_printf("  usable_in_combat            %hhd", s.usable_in_combat);
  w.write_printf("  usable_in_camp              %hhd", s.usable_in_camp);
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
