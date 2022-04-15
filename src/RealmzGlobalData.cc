#include "RealmzGlobalData.hh"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
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
  string the_family_jewels_name = first_file_that_exists({
      (this->dir + "/the_family_jewels.rsf"),
      (this->dir + "/The Family Jewels.rsf"),
      (this->dir + "/THE FAMILY JEWELS.RSF"),
      (this->dir + "/the_family_jewels/rsrc"),
      (this->dir + "/The Family Jewels/rsrc"),
      (this->dir + "/THE FAMILY JEWELS/rsrc"),
      (this->dir + "/the_family_jewels/..namedfork/rsrc"),
      (this->dir + "/The Family Jewels/..namedfork/rsrc"),
      (this->dir + "/THE FAMILY JEWELS/..namedfork/rsrc")});
  string portraits_name = first_file_that_exists({
      (this->dir + "/portraits.rsf"),
      (this->dir + "/Portraits.rsf"),
      (this->dir + "/PORTRAITS.RSF"),
      (this->dir + "/portraits/rsrc"),
      (this->dir + "/Portraits/rsrc"),
      (this->dir + "/PORTRAITS/rsrc"),
      (this->dir + "/portraits/..namedfork/rsrc"),
      (this->dir + "/Portraits/..namedfork/rsrc"),
      (this->dir + "/PORTRAITS/..namedfork/rsrc")});

  this->global_rsf = parse_resource_fork(load_file(the_family_jewels_name));
  this->portraits_rsf = parse_resource_fork(load_file(portraits_name));
  this->load_default_tilesets();
}

void RealmzGlobalData::load_default_tilesets() {
  static const unordered_map<string, vector<string>> land_type_to_filenames({
    {"indoor",  {"data_castle_bd", "Data Castle BD", "DATA CASTLE BD"}},
    {"desert",  {"data_desert_bd", "Data Desert BD", "DATA DESERT BD"}},
    {"outdoor", {"data_p_bd", "Data P BD", "DATA P BD"}},
    {"snow",    {"data_snow_bd", "Data Snow BD", "DATA SNOW BD"}},
    {"cave",    {"data_sub_bd", "Data SUB BD", "DATA SUB BD"}},
    {"abyss",   {"data_swamp_bd", "Data Swamp BD", "DATA SWAMP BD"}},
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

static unordered_map<string, int16_t> land_type_to_resource_id({
  {"outdoor",  300},
  {"dungeon",  302},
  {"cave",     303},
  {"indoor",   304},
  {"desert",   305},
  {"custom_1", 306},
  {"custom_2", 307},
  {"custom_3", 308},
  {"abyss",    309}, // TODO: should this be "swamp" instead?
  {"snow",     310},
});

int16_t resource_id_for_land_type(const string& land_type) {
  return land_type_to_resource_id.at(land_type);
}

Image generate_tileset_definition_legend(const TileSetDefinition& ts,
    const Image& positive_pattern) {

  if (positive_pattern.get_width() != 640 || positive_pattern.get_height() != 320) {
    throw runtime_error("postiive pattern is not 640x320");
  }

  Image result(32 * 13, 97 * 200);
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
    result.draw_text(1, 97 * x + 1, text_color, "%04zX", x);
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

    // Draw the battle expansion
    for (int y = 0; y < 9; y++) {
      int px = 320 + (y % 3) * 32;
      int py = 97 * x + (y / 3) * 32;

      int16_t data = t.battle_expansion[y];
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
