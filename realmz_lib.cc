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

#include "realmz_lib.hh"
#include "resource_fork.hh"
#include "util.hh"

using namespace std;



static string render_string_reference(const vector<string>& strings,
    int index) {
  if (index == 0) {
    return "0";
  }
  if ((size_t)abs(index) >= strings.size()) {
    return string_printf("%d", index);
  }
  string escaped = escape_quotes(strings[abs(index)]);
  return string_printf("\"%s\"#%d", escaped.c_str(), index);
}



////////////////////////////////////////////////////////////////////////////////
// DATA MD2

void party_map::byteswap() {
  for (int x = 0; x < 10; x++) {
    this->annotations[x].icon_id = bswap16(this->annotations[x].icon_id);
    this->annotations[x].x = bswap16(this->annotations[x].x);
    this->annotations[x].y = bswap16(this->annotations[x].y);
  }
  this->x = bswap16(this->x);
  this->y = bswap16(this->y);
  this->level_num = bswap16(this->level_num);
  this->picture_id = bswap16(this->picture_id);
  this->tile_size = bswap16(this->tile_size);
  this->text_id = bswap16(this->text_id);
  this->is_dungeon = bswap16(this->is_dungeon);
}

vector<party_map> load_party_map_index(const string& filename) {
  return load_direct_file_data<party_map>(filename);
}

string disassemble_party_map(int index, const party_map& t) {
  string ret = string_printf("===== %s MAP id=%d level=%d x=%d y=%d tile_size=%d\n",
      (t.is_dungeon ? "DUNGEON" : "LAND"), index, t.level_num, t.x, t.y, t.tile_size);
  if (t.picture_id) {
    ret += string_printf("  picture id=%d\n", t.picture_id);
  }
  if (t.text_id) {
    ret += string_printf("  text id=%d\n", t.text_id);
  }

  for (int x = 0; x < 10; x++) {
    if (!t.annotations[x].icon_id && !t.annotations[x].x && !t.annotations[x].y) {
      continue;
    }
    ret += string_printf("  annotation icon_id=%d x=%d y=%d\n",
        t.annotations[x].icon_id, t.annotations[x].x, t.annotations[x].y);
  }

  string description(t.description, t.description_valid_chars);
  ret += string_printf("  description=\"%s\"\n", description.c_str());
  return ret;
}

string disassemble_all_party_maps(const vector<party_map>& t) {
  string ret;
  for (size_t x = 0; x < t.size(); x++) {
    ret += disassemble_party_map(x, t[x]);
  }
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// DATA CUSTOM N BD

void tile_definition::byteswap() {
  this->sound_id = bswap16(this->sound_id);
  this->time_per_move = bswap16(this->time_per_move);
  this->solid_type = bswap16(this->solid_type);
  this->is_shore = bswap16(this->is_shore);
  this->is_need_boat = bswap16(this->is_need_boat);
  this->is_path = bswap16(this->is_path);
  this->blocks_los = bswap16(this->blocks_los);
  this->need_fly_float = bswap16(this->need_fly_float);
  this->special_type = bswap16(this->special_type);
  for (int x = 0; x < 9; x++) {
    this->battle_expansion[x] = bswap16(this->battle_expansion[x]);
  }
}

void tileset_definition::byteswap() {
  this->base_tile_id = bswap16(this->base_tile_id);
  for (int x = 0; x < 201; x++) {
    this->tiles[x].byteswap();
  }
}

tileset_definition load_tileset_definition(const string& filename) {
  return load_direct_file_data_single<tileset_definition>(filename);
}

static const Image& positive_pattern_for_land_type(const string& land_type,
    const string& rsf_file);

Image generate_tileset_definition_legend(const tileset_definition& ts,
    const string& land_type, const string& rsf_name) {

  Image positive_pattern = positive_pattern_for_land_type(land_type, rsf_name);

  Image result(32 * 13, 97 * 200);
  for (size_t x = 0; x < 200; x++) {

    // tile 0 is unused apparently? (there are 201 of them)
    const tile_definition& t = ts.tiles[x + 1];
    uint8_t r, g, b;
    if (x + 1 == ts.base_tile_id) {
      r = g = b = 0x00;
      result.fill_rect(0, 97 * x, 32, 96, 0xFF, 0xFF, 0xFF, 0xFF);
    } else {
      r = g = b = 0xFF;
    }
    result.draw_text(1, 97 * x + 1, NULL, NULL, r, g, b, 0xFF, 0x00, 0x00, 0x00,
        0x00, "%04X", x);
    result.draw_text(1, 97 * x + 17, NULL, NULL, r, g, b, 0xFF, 0x00, 0x00,
        0x00, 0x00, "SOUND\n%04X", t.sound_id);

    if (x + 1 == ts.base_tile_id) {
      result.draw_text(1, 97 * x + 41, NULL, NULL, r, g, b, 0xFF, 0x00, 0x00,
          0x00, 0x00, "BASE");
    }

    // draw the tile itself
    result.blit(positive_pattern, 32, 97 * x, 32, 32, (x % 20) * 32, (x / 20) * 32);

    // draw the solid type
    if (t.solid_type == 1) {
      result.fill_rect(64, 97 * x, 32, 96, 0xFF, 0x00, 0x00, 0x80);
      result.draw_text(65, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "LARGE\nONLY");
    } else if (t.solid_type == 2) {
      result.fill_rect(64, 97 * x, 32, 96, 0xFF, 0x00, 0x00, 0xFF);
      result.draw_text(65, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "SOLID");
    } else if (t.solid_type == 0) {
      result.draw_text(65, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0xFF, "NOT\nSOLID");
    } else {
      result.fill_rect(64, 97 * x, 32, 96, 0xFF, 0xFF, 0xFF, 0xFF);
      result.draw_text(65, 97 * x + 1, NULL, NULL, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, "%04X", t.solid_type);
    }

    // draw its path flag
    if (t.is_path) {
      result.fill_rect(96, 97 * x, 32, 96, 0xFF, 0xFF, 0xFF, 0xFF);
      result.draw_text(97, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "PATH");
    } else {
      result.draw_text(97, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "NOT\nPATH");
    }

    // draw the shore flag
    if (t.is_shore) {
      result.fill_rect(128, 97 * x, 32, 96, 0xFF, 0xFF, 0x00, 0xFF);
      result.draw_text(129, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "SHORE");
    } else {
      result.draw_text(129, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "NOT\nSHORE");
    }

    // draw the is/need boat flag
    if (t.is_need_boat == 1) {
      result.fill_rect(160, 97 * x, 32, 96, 0x00, 0x80, 0xFF, 0xFF);
      result.draw_text(161, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "BOAT");
    } else if (t.is_need_boat == 2) {
      result.fill_rect(160, 97 * x, 32, 96, 0x00, 0x80, 0xFF, 0x80);
      result.draw_text(161, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "NEED\nBOAT");
    } else if (t.is_need_boat == 0) {
      result.draw_text(161, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "NO\nBOAT");
    } else {
      result.fill_rect(64, 97 * x, 32, 96, 0xFF, 0xFF, 0xFF, 0xFF);
      result.draw_text(161, 97 * x + 1, NULL, NULL, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, "%04X", t.is_need_boat);
    }

    // draw the fly/float flag
    if (t.need_fly_float) {
      result.fill_rect(192, 97 * x, 32, 96, 0x00, 0xFF, 0x00, 0xFF);
      result.draw_text(193, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "NEED\nFLY\nFLOAT");
    } else {
      result.draw_text(193, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "NO\nFLY\nFLOAT");
    }

    // draw the blocks LOS flag
    if (t.blocks_los) {
      result.fill_rect(224, 97 * x, 32, 96, 0x80, 0x80, 0x80, 0xFF);
      result.draw_text(225, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "BLOCK\nLOS");
    } else {
      result.draw_text(225, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "NO\nBLOCK\nLOS");
    }

    // draw the special flag (forest type)
    if (t.special_type == 1) {
      result.fill_rect(256, 97 * x, 32, 96, 0x00, 0xFF, 0x80, 0xFF);
      result.draw_text(257, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "TREES");
    } else if (t.special_type == 2) {
      result.fill_rect(256, 97 * x, 32, 96, 0xFF, 0x80, 0x00, 0xFF);
      result.draw_text(257, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "DSRT");
    } else if (t.special_type == 3) {
      result.fill_rect(256, 97 * x, 32, 96, 0xFF, 0x00, 0x00, 0xFF);
      result.draw_text(257, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "SHRMS");
    } else if (t.special_type == 4) {
      result.fill_rect(256, 97 * x, 32, 96, 0x00, 0x80, 0x00, 0xFF);
      result.draw_text(257, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "SWAMP");
    } else if (t.special_type == 5) {
      result.fill_rect(256, 97 * x, 32, 96, 0xE0, 0xE0, 0xE0, 0xFF);
      result.draw_text(257, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80, "SNOW");
    } else if (t.special_type == 0) {
      result.draw_text(257, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0xFF, "NO\nTREES");
    } else {
      result.draw_text(257, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0xFF, "%04X", t.special_type);
    }

    // draw the time to move
    result.draw_text(288, 97 * x + 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0xFF,
        "%hd\nMINS", t.time_per_move);

    // draw the battle expansion
    for (int y = 0; y < 9; y++) {
      int px = 320 + (y % 3) * 32;
      int py = 97 * x + (y / 3) * 32;

      int16_t data = t.battle_expansion[y];
      if (data < 1 || data > 200) {
        result.draw_text(px, py, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, "%04X", data);
      } else {
        data--;
        result.blit(positive_pattern, px, py, 32, 32, (data % 20) * 32, (data / 20) * 32);
      }
    }

    // draw the separator for the next tile
    result.draw_horizontal_line(0, result.get_width(), 97 * x + 96, 4, 0xFF, 0xFF, 0xFF, 0xFF);
  }

  return result;
}



////////////////////////////////////////////////////////////////////////////////
// SCENARIO.RSF

unordered_map<int16_t, Image> get_picts(const string& rsf_name) {
  unordered_map<int16_t, Image> ret;

  ResourceFile rf(rsf_name.c_str());
  for (const auto& it : rf.all_resources()) {
    if (it.first != RESOURCE_TYPE_PICT) {
      continue;
    }

    try {
      ret.emplace(it.second, rf.decode_PICT(it.second));
    } catch (const runtime_error& e) {
      fprintf(stderr, "warning: failed to load resource %08X:%d: %s\n",
          it.first, it.second, e.what());
    }
  }

  return ret;
}

unordered_map<int16_t, ResourceFile::decoded_cicn> get_cicns(const string& rsf_name) {
  unordered_map<int16_t, ResourceFile::decoded_cicn> ret;

  ResourceFile rf(rsf_name.c_str());
  for (const auto& it : rf.all_resources()) {
    if (it.first != RESOURCE_TYPE_cicn) {
      continue;
    }

    try {
      ret.emplace(it.second, rf.decode_cicn(it.second));
    } catch (const runtime_error& e) {
      fprintf(stderr, "warning: failed to load resource %08X:%d: %s\n",
          it.first, it.second, e.what());
    }
  }

  return ret;
}

unordered_map<int16_t, string> get_snds(const string& rsf_name) {
  unordered_map<int16_t, string> ret;

  ResourceFile rf(rsf_name.c_str());
  for (const auto& it : rf.all_resources()) {
    if (it.first != RESOURCE_TYPE_snd) {
      continue;
    }

    try {
      ret.emplace(it.second, rf.decode_snd(it.second));
    } catch (const runtime_error& e) {
      fprintf(stderr, "warning: failed to load resource %08X:%d: %s\n",
          it.first, it.second, e.what());
    }
  }

  return ret;
}

unordered_map<int16_t, pair<string, bool>> get_texts(const string& rsf_name) {
  unordered_map<int16_t, pair<string, bool>> ret;

  ResourceFile rf(rsf_name.c_str());
  for (const auto& it : rf.all_resources()) {
    if (it.first != RESOURCE_TYPE_styl) {
      continue;
    }

    try {
      ret.emplace(it.second, make_pair(rf.decode_styl(it.second), true));
    } catch (const runtime_error& e) {
      fprintf(stderr, "warning: failed to load resource %08X:%d: %s\n",
          it.first, it.second, e.what());
    }
  }

  for (const auto& it : rf.all_resources()) {
    if (it.first != RESOURCE_TYPE_TEXT) {
      continue;
    }

    if (ret.count(it.second)) {
      continue; // already got this one from the styl
    }

    try {
      ret.emplace(it.second, make_pair(rf.decode_TEXT(it.second), false));
    } catch (const runtime_error& e) {
      fprintf(stderr, "warning: failed to load resource %08X:%d: %s\n",
          it.first, it.second, e.what());
    }
  }

  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// LAYOUT

level_neighbors::level_neighbors() : x(-1), y(-1), left(-1), right(-1), top(-1),
    bottom(-1) { }

land_layout::land_layout() {
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 16; x++) {
      this->layout[y][x] = -1;
    }
  }
}

land_layout::land_layout(const land_layout& l) {
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 16; x++) {
      this->layout[y][x] = l.layout[y][x];
    }
  }
}

int land_layout::num_valid_levels() {
  int count = 0;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 16; x++) {
      if (this->layout[y][x] >= 0) {
        count++;
      }
    }
  }
  return count;
}

void land_layout::byteswap() {
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 16; x++) {
      this->layout[y][x] = bswap16(this->layout[y][x]);
    }
  }
}

land_layout load_land_layout(const string& filename) {
  land_layout l = load_direct_file_data_single<land_layout>(filename);
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 16; x++) {
      if (l.layout[y][x] == -1) {
        l.layout[y][x] = 0;
      } else if (l.layout[y][x] == 0) {
        l.layout[y][x] = -1;
      }
    }
  }
  return l;
}

level_neighbors get_level_neighbors(const land_layout& l, int16_t id) {
  level_neighbors n;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 16; x++) {
      if (l.layout[y][x] == id) {
        if (n.x != -1 || n.y != -1) {
          throw runtime_error("multiple entries for level");
        }

        n.x = x;
        n.y = y;
        if (x != 0) {
          n.left = l.layout[y][x - 1];
        }
        if (x != 15) {
          n.right = l.layout[y][x + 1];
        }
        if (y != 0) {
          n.top = l.layout[y - 1][x];
        }
        if (y != 7) {
          n.bottom = l.layout[y + 1][x];
        }
      }
    }
  }

  return n;
}

vector<land_layout> get_connected_components(const land_layout& l) {
  land_layout remaining_components(l);

  vector<land_layout> ret;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 16; x++) {
      if (remaining_components.layout[y][x] == -1) {
        continue;
      }

      // this cell is the upper-left corner of a connected component
      // use flood-fill to copy it to this_component
      land_layout this_component;
      set<pair<int, int>> to_fill;
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

Image generate_layout_map(const land_layout& l,
    const unordered_map<int16_t, string>& level_id_to_image_name) {

  int min_x = 16, min_y = 8, max_x = -1, max_y = -1;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 16; x++) {
      if (l.layout[y][x] < 0) {
        continue;
      }

      // if the level has no valid neighbors, ignore it
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
  for (int y = 0; y < (max_y - min_y); y++) {
    for (int x = 0; x < (max_x - min_x); x++) {
      int16_t level_id = l.layout[y + min_y][x + min_x];
      if (level_id < 0) {
        continue;
      }

      int xp = 90 * 32 * x;
      int yp = 90 * 32 * y;

      try {
        Image this_level_map(level_id_to_image_name.at(level_id).c_str());

        level_neighbors n = get_level_neighbors(l, level_id);
        int sx = (n.left >= 0) ? 9 : 0;
        int sy = (n.top >= 0) ? 9 : 0;

        overall_map.blit(this_level_map, xp, yp, 90 * 32, 90 * 32, sx, sy);

      } catch (const exception& e) {
        overall_map.fill_rect(xp, yp, 90 * 32, 90 * 32, 0xFF, 0xFF, 0xFF, 0xFF);
        overall_map.draw_text(xp + 10, yp + 10, NULL, NULL, 0, 0, 0, 0xFF, 0, 0, 0, 0,
            "can\'t read disassembled map %d (%s)", level_id, e.what());
      }
    }
  }

  return overall_map;
}



////////////////////////////////////////////////////////////////////////////////
// GLOBAL

void global_metadata::byteswap() {
  this->start_xap = bswap16(this->start_xap);
  this->death_xap = bswap16(this->death_xap);
  this->quit_xap = bswap16(this->quit_xap);
  this->reserved1_xap = bswap16(this->reserved1_xap);
  this->shop_xap = bswap16(this->shop_xap);
  this->temple_xap = bswap16(this->temple_xap);
  this->reserved2_xap = bswap16(this->reserved2_xap);
}

global_metadata load_global_metadata(const string& filename) {
  return load_direct_file_data_single<global_metadata>(filename);
}

string disassemble_globals(const global_metadata& g) {
  return string_printf("==== GLOBAL METADATA\n"
      "  start_xap id=%d\n"
      "  death_xap id=%d\n"
      "  quit_xap id=%d\n"
      "  reserved1_xap id=%d\n"
      "  shop_xap id=%d\n"
      "  temple_xap id=%d\n"
      "  reserved2_xap id=%d\n",
      g.start_xap, g.death_xap, g.quit_xap, g.reserved1_xap, g.shop_xap,
      g.temple_xap, g.reserved2_xap);
}



////////////////////////////////////////////////////////////////////////////////
// SCENARIO NAME

void scenario_metadata::byteswap() {
  this->recommended_starting_levels = bswap32(this->recommended_starting_levels);
  this->unknown1 = bswap32(this->unknown1);
  this->start_level = bswap32(this->start_level);
  this->start_x = bswap32(this->start_x);
  this->start_y = bswap32(this->start_y);
}

scenario_metadata load_scenario_metadata(const string& filename) {
  return load_direct_file_data_single<scenario_metadata>(filename);
}



////////////////////////////////////////////////////////////////////////////////
// DATA EDCD

void ecodes::byteswap() {
  for (int x = 0; x < 5; x++) {
    this->data[x] = bswap16(this->data[x]);
  }
}

vector<ecodes> load_ecodes_index(const string& filename) {
  return load_direct_file_data<ecodes>(filename);
}



////////////////////////////////////////////////////////////////////////////////
// DATA TD

void treasure::byteswap() {
  for (int x = 0; x < 20; x++) {
    this->item_ids[x] = bswap16(this->item_ids[x]);
  }
  this->victory_points = bswap16(this->victory_points);
  this->gold = bswap16(this->gold);
  this->gems = bswap16(this->gems);
  this->jewelry = bswap16(this->jewelry);
}

vector<treasure> load_treasure_index(const string& filename) {
  return load_direct_file_data<treasure>(filename);
}

string disassemble_treasure(int index, const treasure& t) {
  string ret = string_printf("===== TREASURE id=%d", index);

  if (t.victory_points < 0) {
    ret += string_printf(" victory_points=rand(1,%d)", -t.victory_points);
  } else if (t.victory_points > 0) {
    ret += string_printf(" victory_points=%d", t.victory_points);
  }

  if (t.gold < 0) {
    ret += string_printf(" gold=rand(1,%d)", -t.gold);
  } else if (t.gold > 0) {
    ret += string_printf(" gold=%d", t.gold);
  }

  if (t.gems < 0) {
    ret += string_printf(" gems=rand(1,%d)", -t.gems);
  } else if (t.gems > 0) {
    ret += string_printf(" gems=%d", t.gems);
  }

  if (t.jewelry < 0) {
    ret += string_printf(" jewelry=rand(1,%d)", -t.jewelry);
  } else if (t.jewelry > 0) {
    ret += string_printf(" jewelry=%d", t.jewelry);
  }

  ret += '\n';

  for (int x = 0; x < 20; x++) {
    if (t.item_ids[x]) {
      ret += string_printf("  %hd\n", t.item_ids[x]);
    }
  }

  return ret;
}

string disassemble_all_treasures(const vector<treasure>& t) {
  string ret;
  for (size_t x = 0; x < t.size(); x++) {
    ret += disassemble_treasure(x, t[x]);
  }
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// DATA ED

void simple_encounter::byteswap() {
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 8; x++) {
      this->choice_args[y][x] = bswap16(this->choice_args[y][x]);
    }
  }
  this->unknown = bswap16(this->unknown);
  this->prompt = bswap16(this->prompt);
}

vector<simple_encounter> load_simple_encounter_index(const string& filename) {
  return load_direct_file_data<simple_encounter>(filename);
}

string disassemble_simple_encounter(int index, const simple_encounter& e,
    const vector<ecodes> ecodes, const vector<string>& strings) {

  string prompt = render_string_reference(strings, e.prompt);
  string ret = string_printf("===== SIMPLE ENCOUNTER id=%d can_backout=%d max_times=%d prompt=%s\n",
      index, e.can_backout, e.max_times, prompt.c_str());

  for (int x = 0; x < 4; x++) {
    string choice_text(e.choice_text[x].text, min(
        (int)e.choice_text[x].valid_chars, (int)(sizeof(e.choice_text[x]) - 1)));
    if (choice_text.empty()) {
      continue;
    }
    ret += string_printf("  choice%d: result=%d text=\"%s\"\n", x,
        e.choice_result_index[x], escape_quotes(choice_text).c_str());
  }

  for (int x = 0; x < 4; x++) {
    int y;
    for (y = 0; y < 8; y++) {
      if (e.choice_codes[x][y] || e.choice_args[x][y]) {
        break;
      }
    }
    if (y == 8) {
      break; // option is blank; don't even print it
    }

    for (int y = 0; y < 8; y++) {
      if (e.choice_codes[x][y] || e.choice_args[x][y]) {
        ret += string_printf("  result%d> %s\n", x + 1, disassemble_opcode(
            e.choice_codes[x][y], e.choice_args[x][y], ecodes, strings).c_str());
      }
    }
  }

  return ret;
}

string disassemble_all_simple_encounters(const vector<simple_encounter>& e,
    const vector<ecodes> ecodes, const vector<string>& strings) {
  string ret;
  for (size_t x = 0; x < e.size(); x++) {
    ret += disassemble_simple_encounter(x, e[x], ecodes, strings);
  }
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// DATA ED2

void complex_encounter::byteswap() {
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 8; x++) {
      this->choice_args[y][x] = bswap16(this->choice_args[y][x]);
    }
  }
  for (int x = 0; x < 10; x++) {
    this->spell_codes[x] = bswap16(this->spell_codes[x]);
  }
  for (int x = 0; x < 10; x++) {
    this->item_codes[x] = bswap16(this->item_codes[x]);
  }
  this->rogue_encounter_id = bswap16(this->rogue_encounter_id);
  this->prompt = bswap16(this->prompt);
}

vector<complex_encounter> load_complex_encounter_index(const string& filename) {
  return load_direct_file_data<complex_encounter>(filename);
}

string disassemble_complex_encounter(int index, const complex_encounter& e,
    const vector<ecodes> ecodes, const vector<string>& strings) {

  string prompt = render_string_reference(strings, e.prompt);
  string ret = string_printf("===== COMPLEX ENCOUNTER id=%d can_backout=%d max_times=%d prompt=%s\n",
      index, e.can_backout, e.max_times, prompt.c_str());

  for (int x = 0; x < 10; x++) {
    if (!e.spell_codes[x]) {
      continue;
    }
    ret += string_printf("  spell id=%d result=%d\n", e.spell_codes[x],
        e.spell_result_codes[x]);
  }

  for (int x = 0; x < 5; x++) {
    if (!e.item_codes[x]) {
      continue;
    }
    ret += string_printf("  item id=%d result=%d\n", e.item_codes[x],
        e.item_result_codes[x]);
  }

  for (int x = 0; x < 5; x++) {
    string action_text(e.action_text[x].text, min(
        (int)e.action_text[x].valid_chars, (int)sizeof(e.action_text[x]) - 1));
    if (action_text.empty()) {
      continue;
    }
    ret += string_printf("  action selected=%d text=\"%s\"\n",
        e.actions_selected[x], escape_quotes(action_text).c_str());
  }
  ret += string_printf("  action_result=%d\n", e.action_result);

  if (e.has_rogue_encounter) {
    ret += string_printf("  rogue_encounter id=%d reset=%d\n",
        e.rogue_encounter_id, e.rogue_reset_flag);
  }

  string speak_text(e.speak_text.text, min((int)e.speak_text.valid_chars,
        (int)sizeof(e.speak_text) - 1));
  if (!speak_text.empty()) {
    ret += string_printf("  speak result=%d text=\"%s\"\n", e.speak_result,
        escape_quotes(speak_text).c_str());
  }

  for (int x = 0; x < 4; x++) {
    int y;
    for (y = 0; y < 8; y++) {
      if (e.choice_codes[x][y] || e.choice_args[x][y]) {
        break;
      }
    }
    if (y == 8) {
      break; // option is blank; don't even print it
    }

    for (int y = 0; y < 8; y++) {
      if (e.choice_codes[x][y] || e.choice_args[x][y]) {
        ret += string_printf("  result%d> %s\n", x + 1, disassemble_opcode(
            e.choice_codes[x][y], e.choice_args[x][y], ecodes, strings).c_str());
      }
    }
  }

  return ret;
}

string disassemble_all_complex_encounters(const vector<complex_encounter>& e,
    const vector<ecodes> ecodes, const vector<string>& strings) {
  string ret;
  for (size_t x = 0; x < e.size(); x++) {
    ret += disassemble_complex_encounter(x, e[x], ecodes, strings);
  }
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// DATA TD2

void rogue_encounter::byteswap() {
  for (int x = 0; x < 8; x++) {
    this->success_string_ids[x] = bswap16(this->success_string_ids[x]);
  }
  for (int x = 0; x < 8; x++) {
    this->failure_string_ids[x] = bswap16(this->failure_string_ids[x]);
  }
  for (int x = 0; x < 8; x++) {
    this->success_sound_ids[x] = bswap16(this->success_sound_ids[x]);
  }
  for (int x = 0; x < 8; x++) {
    this->failure_sound_ids[x] = bswap16(this->failure_sound_ids[x]);
  }

  this->trap_spell = bswap16(this->trap_spell);
  this->trap_damage_low = bswap16(this->trap_damage_low);
  this->trap_damage_high = bswap16(this->trap_damage_high);
  this->num_lock_tumblers = bswap16(this->num_lock_tumblers);
  this->prompt_string = bswap16(this->prompt_string);
  this->trap_sound = bswap16(this->trap_sound);
  this->trap_spell_power_level = bswap16(this->trap_spell_power_level);
  this->prompt_sound = bswap16(this->prompt_sound);
  this->percent_per_level_to_open = bswap16(this->percent_per_level_to_open);
  this->percent_per_level_to_disable = bswap16(this->percent_per_level_to_disable);
};

vector<rogue_encounter> load_rogue_encounter_index(const string& filename) {
  return load_direct_file_data<rogue_encounter>(filename);
}

static const vector<string> rogue_encounter_action_names({
  "acrobatic_act", "detect_trap", "disable_trap", "action3", "force_lock",
  "action5", "pick_lock", "action7",
});

string disassemble_rogue_encounter(int index, const rogue_encounter& e,
    const vector<ecodes> ecodes, const vector<string>& strings) {

  string prompt = render_string_reference(strings, e.prompt_string);
  string ret = string_printf("===== ROGUE ENCOUNTER id=%d sound=%d prompt=%s "
      "pct_per_level_to_open_lock=%d pct_per_level_to_disable_trap=%d "
      "num_lock_tumblers=%d\n",
      index, e.prompt_sound, prompt.c_str(), e.percent_per_level_to_open,
      e.percent_per_level_to_disable, e.num_lock_tumblers);

  for (int x = 0; x < 8; x++) {
    if (!e.actions_available[x]) {
      continue;
    }
    string success_str = render_string_reference(strings, e.success_string_ids[x]);
    string failure_str = render_string_reference(strings, e.failure_string_ids[x]);

    ret += string_printf("  action_%s percent_modify=%d success_result=%d "
        "failure_result=%d success_str=%s failure_str=%s success_sound=%d "
        "failure_sound=%d\n", rogue_encounter_action_names[x].c_str(),
        e.percent_modify[x], e.success_result_codes[x],
        e.failure_result_codes[x], success_str.c_str(), failure_str.c_str(),
        e.success_sound_ids[x], e.failure_sound_ids[x]);
  }

  if (e.is_trapped) {
    ret += string_printf("  trap rogue_only=%d spell=%d spell_power=%d "
        "damage_range=[%d,%d] sound=%d\n", e.trap_affects_rogue_only,
        e.trap_spell, e.trap_spell_power_level, e.trap_damage_low,
        e.trap_damage_high, e.trap_sound);
  }

  return ret;
}

string disassemble_all_rogue_encounters(const vector<rogue_encounter>& e,
    const vector<ecodes> ecodes, const vector<string>& strings) {
  string ret;
  for (size_t x = 0; x < e.size(); x++) {
    ret += disassemble_rogue_encounter(x, e[x], ecodes, strings);
  }
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// DATA TD3

void time_encounter::byteswap() {
  this->day = bswap16(this->day);
  this->increment = bswap16(this->increment);
  this->percent_chance = bswap16(this->percent_chance);
  this->xap_id = bswap16(this->xap_id);
  this->required_level = bswap16(this->required_level);
  this->required_rect = bswap16(this->required_rect);
  this->required_x = bswap16(this->required_x);
  this->required_y = bswap16(this->required_y);
  this->required_item_id = bswap16(this->required_item_id);
  this->required_quest = bswap16(this->required_quest);
  this->land_or_dungeon = bswap16(this->land_or_dungeon);
}

vector<time_encounter> load_time_encounter_index(const string& filename) {
  return load_direct_file_data<time_encounter>(filename);
}

string disassemble_time_encounter(int index, const time_encounter& e) {

  string ret = string_printf("===== TIME ENCOUNTER id=%d ", index);

  ret += string_printf(" day=%d", e.day);
  ret += string_printf(" increment=%d", e.increment);
  ret += string_printf(" percent_chance=%d", e.percent_chance);
  ret += string_printf(" xap_id=%d", e.xap_id);
  if (e.required_level != -1) {
    ret += string_printf(" required_level: id=%d(%s)", e.required_level,
        e.land_or_dungeon == 1 ? "land" : "dungeon");
  }
  if (e.required_rect != -1) {
    ret += string_printf(" required_rect=%d", e.required_rect);
  }
  if (e.required_x != -1 || e.required_y != -1) {
    ret += string_printf(" required_pos=(%d,%d)", e.required_x, e.required_y);
  }
  if (e.required_item_id != -1) {
    ret += string_printf(" required_item_id=%d", e.required_item_id);
  }
  if (e.required_quest != -1) {
    ret += string_printf(" required_quest=%d", e.required_quest);
  }

  ret += '\n';
  return ret;
}

string disassemble_all_time_encounters(const vector<time_encounter>& e) {
  string ret;
  for (size_t x = 0; x < e.size(); x++) {
    ret += disassemble_time_encounter(x, e[x]);
  }
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// DATA RD

struct random_rect_coords {
  int16_t top;
  int16_t left;
  int16_t bottom;
  int16_t right;
};

struct random_rect_battle_range {
  int16_t low;
  int16_t high;
};

struct map_metadata_file {
  random_rect_coords coords[20];
  int16_t times_in_10k[20];
  random_rect_battle_range battle_range[20];
  int16_t xap_num[20][3];
  int16_t xap_chance[20][3];
  int8_t land_type;
  int8_t unknown[0x16]; // seriously wut
  int8_t percent_option[20];
  int8_t unused;
  int16_t sound[20];
  int16_t text[20];
};

static const unordered_map<uint8_t, string> land_type_to_string({
  {0,  "outdoor"},
  {1,  "reserved1"},
  {2,  "reserved2"},
  {3,  "cave"},
  {4,  "indoor"},
  {5,  "desert"},
  {6,  "custom_1"},
  {7,  "custom_2"},
  {8,  "custom_3"},
  {9,  "abyss"},
  {10, "snow"},
});



vector<map_metadata> load_map_metadata_index(const string& filename) {
  auto f = fopen_unique(filename.c_str(), "rb");
  int num = fstat(f.get()).st_size / sizeof(map_metadata_file);

  vector<map_metadata_file> file_data(num);
  fread(file_data.data(), sizeof(map_metadata_file), num, f.get());

  vector<map_metadata> data(num);
  for (int x = 0; x < num; x++) {
    try {
      data[x].land_type = land_type_to_string.at(file_data[x].land_type);
    } catch (const out_of_range& e) {
      data[x].land_type = "unknown";
    }
    for (int y = 0; y < 20; y++) {
      random_rect r;
      r.top            = bswap16(file_data[x].coords[y].top);
      r.left           = bswap16(file_data[x].coords[y].left);
      r.bottom         = bswap16(file_data[x].coords[y].bottom);
      r.right          = bswap16(file_data[x].coords[y].right);
      r.times_in_10k   = bswap16(file_data[x].times_in_10k[y]);
      r.battle_low     = bswap16(file_data[x].battle_range[y].low);
      r.battle_high    = bswap16(file_data[x].battle_range[y].high);
      r.xap_num[0]     = bswap16(file_data[x].xap_num[y][0]);
      r.xap_num[1]     = bswap16(file_data[x].xap_num[y][1]);
      r.xap_num[2]     = bswap16(file_data[x].xap_num[y][2]);
      r.xap_chance[0]  = bswap16(file_data[x].xap_chance[y][0]);
      r.xap_chance[1]  = bswap16(file_data[x].xap_chance[y][1]);
      r.xap_chance[2]  = bswap16(file_data[x].xap_chance[y][2]);
      r.percent_option = file_data[x].percent_option[y];
      r.sound          = bswap16(file_data[x].sound[y]);
      r.text           = bswap16(file_data[x].text[y]);
      data[x].random_rects.push_back(r);
    }
  }

  return data;
}

static void draw_random_rects(Image& map,
    const vector<random_rect>& random_rects, int tile_size, int xpoff,
    int ypoff, uint8_t r, uint8_t g, uint8_t b, uint8_t br, uint8_t bg,
    uint8_t bb, uint8_t ba) {

  for (size_t x = 0; x < random_rects.size(); x++) {

    random_rect rect = random_rects[x];
    if (rect.left < 0) {
      rect.left = 0;
    }
    if (rect.right > 89) {
      rect.right = 89;
    }
    if (rect.top < 0) {
      rect.top = 0;
    }
    if (rect.bottom > 89) {
      rect.bottom = 89;
    }

    // if the rect doesn't cover any tiles, skip it
    if (rect.left > rect.right || rect.top > rect.bottom) {
      continue;
    }

    ssize_t xp_left = rect.left * tile_size + xpoff;
    ssize_t xp_right = rect.right * tile_size + tile_size - 1 + xpoff;
    ssize_t yp_top = rect.top * tile_size + ypoff;
    ssize_t yp_bottom = rect.bottom * tile_size + tile_size - 1 + ypoff;

    ssize_t start_xx = (xp_left < 0) ? 0 : xp_left;
    ssize_t end_xx = (xp_right > map.get_width()) ? map.get_width() : xp_right;
    ssize_t start_yy = (yp_top < 0) ? 0 : yp_top;
    ssize_t end_yy = (yp_bottom > map.get_height()) ? map.get_height() : yp_bottom;
    for (ssize_t yy = start_yy; yy < end_yy; yy++) {
      for (ssize_t xx = start_xx; xx < end_xx; xx++) {

        uint64_t _r = 0, _g = 0, _b = 0;
        map.read_pixel(xx, yy, &_r, &_g, &_b);

        if (((xx + yy) / 8) & 1) {
          _r = ((0xEF) * (uint32_t)_r) / 0xFF;
          _g = ((0xEF) * (uint32_t)_g) / 0xFF;
          _b = ((0xEF) * (uint32_t)_b) / 0xFF;
        } else {
          _r = (0x10 * (uint32_t)r + (0xEF) * (uint32_t)_r) / 0xFF;
          _g = (0x10 * (uint32_t)g + (0xEF) * (uint32_t)_g) / 0xFF;
          _b = (0x10 * (uint32_t)b + (0xEF) * (uint32_t)_b) / 0xFF;
        }
        map.write_pixel(xx, yy, _r, _g, _b);
      }
    }

    map.draw_horizontal_line(xp_left, xp_right, yp_top, 0, r, g, b);
    map.draw_horizontal_line(xp_left, xp_right, yp_bottom, 0, r, g, b);
    map.draw_vertical_line(xp_left, yp_top, yp_bottom, 0, r, g, b);
    map.draw_vertical_line(xp_right, yp_top, yp_bottom, 0, r, g, b);

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
      for (int y = 0; y < 3; y++) {
        if (rect.xap_num[y] && rect.xap_chance[y]) {
          rectinfo += string_printf(" a%d=%d,%d%%", y, rect.xap_num[y], rect.xap_chance[y]);
        }
      }
    }

    map.draw_text(xp_left + 2, yp_bottom - 8, NULL, NULL, r, g, b, 0xFF, br, bg, bb,
        ba, "%s", rectinfo.c_str());
    map.draw_text(xp_left + 2, yp_bottom - 16, NULL, NULL, r, g, b, 0xFF, br, bg, bb,
        ba, "%d", x);
  }
}



////////////////////////////////////////////////////////////////////////////////
// DATA DD

void ap_info::byteswap() {
  this->location_code = bswap32(this->location_code);
  for (int x = 0; x < 8; x++) {
    this->command_codes[x] = bswap16(this->command_codes[x]);
    this->argument_codes[x] = bswap16(this->argument_codes[x]);
  }
}

int8_t ap_info::get_x() const {
  if (this->location_code < 0) {
    return -1;
  }
  return this->location_code % 100;
}

int8_t ap_info::get_y() const {
  if (this->location_code < 0) {
    return -1;
  }
  return (this->location_code / 100) % 100;
}

int8_t ap_info::get_level_num() const {
  if (this->location_code < 0) {
    return -1;
  }
  return (this->location_code / 10000) % 100;
}

vector<vector<ap_info>> load_ap_index(const string& filename) {
  vector<ap_info> all_info = load_xap_index(filename);

  vector<vector<ap_info>> level_ap_info(all_info.size() / 100);
  for (size_t x = 0; x < all_info.size(); x++) {
    level_ap_info[x / 100].push_back(all_info[x]);
  }

  return level_ap_info;
}

vector<ap_info> load_xap_index(const string& filename) {
  return load_direct_file_data<ap_info>(filename);
}

struct opcode_arg_info {
  string arg_name;
  unordered_map<int16_t, string> value_names;
  bool is_string_id;
  string negative_modifier;
};

struct opcode_info {
  string name;
  string negative_name;
  bool always_use_ecodes;
  vector<opcode_arg_info> args;
};

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

static const unordered_map<int16_t, opcode_info> opcode_definitions({
  {  1, {"string", "", false, {
    {"", {}, true, "no_wait"},
  }}},

  {  2, {"battle", "", false, {
    {"low", {}, false, "surprise"},
    {"high", {}, false, "surprise"},
    {"sound_or_lose_xap", {}, false, ""},
    {"string", {}, true, ""},
    {"treasure_mode", {{0, "all"}, {5, "no_enemy"}, {10, "xap_on_lose"}}, false, ""},
  }}},

  {  3, {"option", "option_link", false, {
    {"continue_option", {{1, "yes"}, {2, "no"}}, false, ""},
    {"target_type", option_jump_target_value_names, false, ""},
    {"target", {}, false, ""},
    {"left_prompt", {}, true, ""},
    {"right_prompt", {}, true, ""},
  }}},

  {  4, {"simple_enc", "", false, {
    {"", {}, false, ""},
  }}},

  {  5, {"complex_enc", "", false, {
    {"", {}, false, ""},
  }}},

  {  6, {"shop", "", false, {
    {"", {}, false, "auto_enter"},
  }}},

  {  7, {"modify_ap", "", false, {
    {"level", {{-2, "simple"}, {-3, "complex"}}, false, ""},
    {"id", {}, false, ""},
    {"source_xap", {}, false, ""},
    {"level_type", {{0, "same"}, {1, "land"}, {2, "dungeon"}}, false, ""},
    {"result_code", {}, false, ""},
  }}},

  {  8, {"use_ap", "", false, {
    {"level", {}, false, ""},
    {"id", {}, false, ""},
  }}},

  {  9, {"sound", "", false, {
    {"", {}, false, "pause"},
  }}},

  { 10, {"treasure", "", false, {
    {"", {}, false, ""},
  }}},

  { 11, {"victory_points", "", false, {
    {"", {}, false, ""},
  }}},

  { 12, {"change_tile", "", false, {
    {"level", {}, false, ""},
    {"x", {}, false, ""},
    {"y", {}, false, ""},
    {"new_tile", {}, false, ""},
    {"level_type", {{0, "land"}, {1, "dungeon"}}, false, ""},
  }}},

  { 13, {"enable_ap", "", false, {
    {"level", {}, false, ""},
    {"id", {}, false, ""},
    {"percent_chance", {}, false, ""},
    {"low", {}, false, "dungeon"},
    {"high", {}, false, "dungeon"},
  }}},

  { 14, {"pick_chars", "", false, {
    {"", {}, false, "only_conscious"},
  }}},

  { 15, {"heal_picked", "", false, {
    {"mult", {}, false, ""},
    {"low_range", {}, false, ""},
    {"high_range", {}, false, ""},
    {"sound", {}, false, ""},
    {"string", {}, true, ""},
  }}},

  { 16, {"heal_party", "", false, {
    {"mult", {}, false, ""},
    {"low_range", {}, false, ""},
    {"high_range", {}, false, ""},
    {"sound", {}, false, ""},
    {"string", {}, true, ""},
  }}},

  { 17, {"spell_picked", "", false, {
    {"spell", {}, false, ""},
    {"power", {}, false, ""},
    {"drv_modifier", {}, false, ""},
    {"can_drv", {{0, "yes"}, {1, "no"}}, false, ""},
  }}},

  { 18, {"spell_party", "", false, {
    {"spell", {}, false, ""},
    {"power", {}, false, ""},
    {"drv_modifier", {}, false, ""},
    {"can_drv", {{0, "yes"}, {1, "no"}}, false, ""},
  }}},

  { 19, {"rand_string", "", false, {
    {"low", {}, true, ""},
    {"high", {}, true, ""},
  }}},

  { 20, {"tele_and_run", "", false, {
    {"level", {{-1, "same"}}, false, ""},
    {"x", {{-1, "same"}}, false, ""},
    {"y", {{-1, "same"}}, false, ""},
    {"sound", {}, false, ""},
    {"string", {}, true, ""},
  }}},

  { 21, {"jmp_if_item", "jmp_if_item_link", false, {
    {"item", {}, false, ""},
    {"target_type", jump_target_value_names, false, ""},
    {"nonposs_action", {{0, "jump_other"}, {1, "continue"}, {2, "string_exit"}}, false, ""},
    {"target", {}, false, ""},
    {"other_target", {}, false, ""},
  }}},

  { 22, {"change_item", "", false, {
    {"item", {}, false, ""},
    {"num", {}, false, ""},
    {"action", {{1, "drop"}, {2, "charge"}, {3, "change_type"}}, false, ""},
    {"charges", {}, false, ""},
    {"new_item", {}, false, ""},
  }}},

  { 23, {"change_rect", "change_rect_dungeon", false, {
    {"level", {}, false, ""},
    {"id", {}, false, ""},
    {"times_in_10k", {}, false, ""},
    {"new_battle_low", {{-1, "same"}}, false, ""},
    {"new_battle_high", {{-1, "same"}}, false, ""},
  }}},

  { 24, {"exit_ap", "", false, {}}},

  { 25, {"exit_ap_delete", "", false, {}}},

  { 26, {"mouse_click", "", false, {}}},

  { 27, {"picture", "", false, {
    {"", {}, false, ""},
  }}},

  { 28, {"redraw", "", false, {}}},

  { 29, {"give_map", "", false, {
    {"", {}, false, "auto_show"},
  }}},

  { 30, {"pick_ability", "", false, {
    {"ability", {}, false, "choose_failure"},
    {"success_mod", {}, false, ""},
    {"who", {{0, "picked"}, {1, "all"}, {2, "alive"}}, false, ""},
    {"what", {{0, "special"}, {1, "attribute"}}, false, ""},
  }}},

  { 31, {"jmp_ability", "jmp_ability_link", false, {
    {"ability", {}, false, "choose_failure"},
    {"success_mod", {}, false, ""},
    {"what", {{0, "special"}, {1, "attribute"}}, false, ""},
    {"success_xap", {}, false, ""},
    {"failure_xap", {}, false, ""},
  }}},

  { 32, {"temple", "", false, {
    {"inflation_percent", {}, false, ""},
  }}},

  { 33, {"take_money", "", false, {
    {"", {}, false, "gems"},
    {"action", {{0, "cont_if_poss"}, {1, "cont_if_not_poss"}, {2, "force"}, {-1, "jmp_back_if_not_poss"}}, false, ""},
    {"target_type", jump_target_value_names, false, ""},
    {"target", {}, false, ""},
    {"code_index", {}, false, ""},
  }}},

  { 34, {"break_enc", "", false, {}}},

  { 35, {"simple_enc_del", "", false, {
    {"", {}, false, ""},
  }}},

  { 36, {"stash_items", "", false, {
    {"", {{0, "restore"}, {1, "stash"}}, false, ""},
  }}},

  { 37, {"set_dungeon", "", false, {
    {"", {{0, "dungeon"}, {1, "land"}}, false, ""},
    {"level", {}, false, ""},
    {"x", {}, false, ""},
    {"y", {}, false, ""},
    {"dir", {{1, "north"}, {2, "east"}, {3, "south"}, {4, "west"}}, false, ""},
  }}},

  { 38, {"jmp_if_item_enc", "", false, {
    {"item", {}, false, ""},
    {"continue", {{0, "if_poss"}, {1, "if_not_poss"}}, false, ""},
    {"target_type", jump_target_value_names, false, ""},
    {"target", {}, false, ""},
    {"code_index", {}, false, ""},
  }}},

  { 39, {"jmp_xap", "", false, {
    {"", {}, false, ""},
  }}},

  { 40, {"jmp_party_cond", "jmp_party_cond_link", false, {
    {"jmp_cond", {{1, "if_exists"}, {2, "if_not_exists"}}, false, ""},
    {"target_type", {{0, "none"}, {1, "xap"}, {1, "simple"}, {1, "complex"}}, false, ""},
    {"target", {}, false, ""},
    {"condition", party_condition_names, false, ""},
  }}},

  { 41, {"simple_enc_del_any", "", false, {
    {"", {}, false, ""},
    {"choice", {}, false, ""},
  }}},

  { 42, {"jmp_random", "jmp_random_link", false, {
    {"percent_chance", {}, false, ""},
    {"action", jump_or_exit_actions, false, ""},
    {"target_type", jump_target_value_names, false, ""},
    {"target", {}, false, ""},
    {"code_index", {}, false, ""},
  }}},

  { 43, {"give_cond", "", false, {
    {"who", {{0, "all"}, {1, "picked"}, {2, "alive"}}, false, ""},
    {"condition", char_condition_names, false, ""},
    {"duration", {}, false, ""},
    {"sound", {}, false, ""},
  }}},

  { 44, {"complex_enc_del", "", false, {
    {"", {}, false, ""},
  }}},

  { 45, {"tele", "", false, {
    {"level", {{-1, "same"}}, false, ""},
    {"x", {{-1, "same"}}, false, ""},
    {"y", {{-1, "same"}}, false, ""},
    {"sound", {}, false, ""},
  }}},

  { 46, {"jmp_quest", "jmp_quest_link", false, {
    {"", {}, false, ""},
    {"check", {{0, "set"}, {1, "not_set"}}, false, ""},
    {"target_type", jump_target_value_names, false, ""},
    {"target", {}, false, ""},
    {"code_index", {}, true, ""},
  }}},

  { 47, {"set_quest", "", false, {
    {"", {}, false, "clear"},
  }}},

  { 48, {"pick_battle", "", false, {
    {"low", {}, false, ""},
    {"high", {}, false, ""},
    {"sound", {}, false, ""},
    {"string", {}, true, ""},
    {"treasure", {}, false, ""},
  }}},

  { 49, {"bank", "", false, {}}},

  { 50, {"pick_attribute", "", false, {
    {"type", {{0, "race"}, {1, "gender"}, {2, "caste"}, {3, "rase_class"}, {4, "caste_class"}}, false, ""},
    {"gender", {{1, "male"}, {2, "female"}}, false, ""},
    {"race_caste", {}, false, ""},
    {"race_caste_class", {}, false, ""},
    {"who", {{0, "all"}, {1, "alive"}}, false, ""},
  }}},

  { 51, {"change_shop", "", false, {
    {"", {}, false, ""},
    {"inflation_percent_change", {}, false, ""},
    {"item_id", {}, false, ""},
    {"item_count", {}, false, ""},
  }}},

  { 52, {"pick_misc", "", false, {
    {"type", {{0, "move"}, {1, "position"}, {2, "item_poss"}, {3, "pct_chance"}, {4, "save_vs_attr"}, {5, "save_vs_spell_type"}, {6, "currently_selected"}, {7, "item_equipped"}, {8, "party_position"}}, false, ""},
    {"parameter", {}, false, ""},
    {"who", {{0, "all"}, {1, "alive"}, {2, "picked"}}, false, ""},
  }}},

  { 53, {"pick_caste", "", false, {
    {"caste", {}, false, ""},
    {"caste_type", {{1, "fighter"}, {2, "magical"}, {3, "monk_rogue"}}, false, ""},
    {"who", {{0, "all"}, {1, "alive"}, {2, "picked"}}, false, ""},
  }}},

  { 54, {"change_time_enc", "", false, {
    {"", {}, false, ""},
    {"percent_chance", {{-1, "same"}}, false, ""},
    {"new_day_incr", {{-1, "same"}}, false, ""},
    {"reset_to_current", {{0, "no"}, {1, "yes"}}, false, ""},
    {"days_to_next_instance", {{-1, "same"}}, false, ""},
  }}},

  { 55, {"jmp_picked", "jmp_picked_link", false, {
    {"pc_id", {{0, "any"}}, false, ""},
    {"fail_action", {{0, "exit_ap"}, {1, "xap"}, {2, "string_exit"}}, false, ""},
    {"unused", {}, false, ""},
    {"success_xap", {}, false, ""},
    {"failure_parameter", {}, false, ""},
  }}},

  { 56, {"jmp_battle", "jmp_battle_link", false, {
    {"battle_low", {}, false, ""},
    {"battle_high", {}, false, ""},
    {"loss_xap", {{-1, "back_up"}}, false, ""},
    {"sound", {}, false, ""},
    {"string", {}, true, ""},
  }}},

  { 57, {"change_tileset", "", false, {
    {"new_tileset", {}, false, ""},
    {"dark", {{0, "no"}, {1, "yes"}}, false, ""},
    {"level", {}, false, ""},
  }}},

  { 58, {"jmp_difficulty", "jmp_difficulty_link", false, {
    {"difficulty", {{1, "novice"}, {2, "easy"}, {3, "normal"}, {4, "hard"}, {5, "veteran"}}, false, ""},
    {"action", jump_or_exit_actions, false, ""},
    {"target_type", jump_target_value_names, false, ""},
    {"target", {}, false, ""},
    {"code_index", {}, false, ""},
  }}},

  { 59, {"jmp_tile", "jmp_tile_link", false, {
    {"tile", {}, false, ""},
    {"action", jump_or_exit_actions, false, ""},
    {"target_type", jump_target_value_names, false, ""},
    {"target", {}, false, ""},
    {"code_index", {}, false, ""},
  }}},

  { 60, {"drop_all_money", "", false, {
    {"type", {{1, "gold"}, {2, "gems"}, {3, "jewelry"}}, false, ""},
    {"who", {{0, "all"}, {1, "picked"}}, false, ""},
  }}},

  { 61, {"incr_party_loc", "", false, {
    {"unused", {}, false, ""},
    {"x", {}, false, ""},
    {"y", {}, false, ""},
    {"move_type", {{0, "exact"}, {1, "random"}}, false, ""},
  }}},

  { 62, {"story", "", false, {
    {"", {}, false, ""},
  }}},

  { 63, {"change_time", "", false, {
    {"base", {{1, "absolute"}, {2, "relative"}}, false, ""},
    {"days", {{-1, "same"}}, false, ""},
    {"hours", {{-1, "same"}}, false, ""},
    {"minutes", {{-1, "same"}}, false, ""},
  }}},

  { 64, {"jmp_time", "jmp_time_link", false, {
    {"day", {{-1, "any"}}, false, ""},
    {"hour", {{-1, "any"}}, false, ""},
    {"unused", {}, false, ""},
    {"before_equal_xap", {}, false, ""},
    {"after_xap", {}, false, ""},
  }}},

  { 65, {"give_rand_item", "", false, {
    {"count", {}, false, "random"},
    {"item_low", {}, false, ""},
    {"item_high", {}, false, ""},
  }}},

  { 66, {"allow_camping", "", false, {
    {"", {{0, "enable"}, {1, "disable"}}, false, ""},
  }}},

  { 67, {"jmp_item_charge", "jmp_item_charge_link", false, {
    {"", {}, false, ""},
    {"target_type", jump_target_value_names, false, ""},
    {"min_charges", {}, false, ""},
    {"target_if_enough", {{-1, "continue"}}, false, ""},
    {"target_if_not_enough", {{-1, "continue"}}, false, ""},
  }}},

  { 68, {"change_fatigue", "", false, {
    {"", {{1, "set_full"}, {2, "set_empty"}, {3, "modify"}}, false, ""},
    {"factor_percent", {}, false, ""},
  }}},

  { 69, {"change_casting_flags", "", false, {
    {"enable_char_casting", {{0, "yes"}, {1, "no"}}, false, ""},
    {"enable_npc_casting", {{0, "yes"}, {1, "no"}}, false, ""},
    {"enable_recharging", {{0, "yes"}, {1, "no"}}, false, ""},
    // note: apparently e-code 4 isn't used and 5 must always be 1
    // we don't care about this for a disassembly though
  }}},

  { 70, {"save_restore_loc", "", true, {
    {"", {{1, "save"}, {2, "restore"}}, false, ""},
  }}},

  { 71, {"enable_coord_display", "", false, {
    {"", {{0, "enable"}, {1, "disable"}}, false, ""},
  }}},

  { 72, {"jmp_quest_range", "jmp_quest_range_link", false, {
    {"quest_low", {}, false, ""},
    {"quest_high", {}, false, ""},
    {"unused", {}, false, ""},
    {"target_type", jump_target_value_names, false, ""},
    {"target", {}, false, ""},
  }}},

  { 73, {"shop_restrict", "", false, {
    {"", {}, false, "auto_enter"},
    {"item_low1", {}, false, ""},
    {"item_high1", {}, false, ""},
    {"item_low2", {}, false, ""},
    {"item_high2", {}, false, ""},
  }}},

  { 74, {"give_spell_pts_picked", "", false, {
    {"mult", {}, false, ""},
    {"pts_low", {}, false, ""},
    {"pts_high", {}, false, ""},
  }}},

  { 75, {"jmp_spell_pts", "jmp_spell_pts_link", false, {
    {"who", {{1, "picked"}, {2, "alive"}}, false, ""},
    {"min_pts", {}, false, ""},
    {"fail_action", {{0, "continue"}, {1, "exit_ap"}}, false, ""},
    {"target_type", jump_target_value_names, false, ""},
    {"target", {}, false, ""},
  }}},

  { 76, {"incr_quest_value", "", false, {
    {"", {}, false, ""},
    {"incr", {}, false, ""},
    {"target_type", {{0, "none"}, {1, "xap"}, {2, "simple"}, {3, "complex"}}, false, ""},
    {"jump_min_value", {}, false, ""},
    {"target", {}, false, ""},
  }}},

  { 77, {"jmp_quest_value", "jmp_quest_value_link", false, {
    {"", {}, false, ""},
    {"value", {}, false, ""},
    {"target_type", jump_target_value_names, false, ""},
    {"target_less", {{0, "continue"}}, false, ""},
    {"target_equal_greater", {{0, "continue"}}, false, ""},
  }}},

  { 78, {"jmp_tile_params", "jmp_tile_params_link", false, {
    {"attr", {{1, "shoreline"}, {2, "is_needs_boat"}, {3, "path"}, {4, "blocks_los"}, {5, "need_fly_float"}, {6, "special"}, {7, "tile_id"}}, false, ""},
    {"tile_id", {}, false, ""},
    {"target_type", jump_target_value_names, false, ""},
    {"target_false", {{0, "continue"}}, false, ""},
    {"target_true", {{0, "continue"}}, false, ""},
  }}},

  { 81, {"jmp_char_cond", "jmp_char_cond_link", false, {
    {"cond", {}, false, ""},
    {"who", {{-1, "picked"}, {0, "party"}}, false, ""},
    {"fail_string", {}, true, ""},
    {"success_xap", {}, false, ""},
    {"failure_xap", {}, false, ""},
  }}},

  { 82, {"enable_turning", "", false, {}}},

  { 83, {"disable_turning", "", false, {}}},

  { 84, {"check_scen_registered", "", false, {}}},

  { 85, {"jmp_random_xap", "jmp_random_xap_link", false, {
    {"target_type", jump_target_value_names, false, ""},
    {"target_low", {}, false, ""},
    {"target_high", {}, false, ""},
    {"sound", {}, false, ""},
    {"string", {}, true, ""},
  }}},

  { 86, {"jmp_misc", "jmp_misc_link", false, {
    {"", {{0, "caste_present"}, {1, "race_present"}, {2, "gender_present"}, {3, "in_boat"}, {4, "camping"}, {5, "caste_class_present"}, {6, "race_class_present"}, {7, "total_party_levels"}, {8, "picked_char_levels"}}, false, ""},
    {"value", {}, false, "picked_only"},
    {"target_type", jump_target_value_names, false, ""},
    {"target_true", {{0, "continue"}}, false, ""},
    {"target_false", {{0, "continue"}}, false, ""},
  }}},

  { 87, {"jmp_npc", "jmp_npc_link", false, {
    {"", {}, false, ""},
    {"target_type", jump_target_value_names, false, "picked_only"},
    {"fail_action", {{0, "jmp_other"}, {1, "continue"}, {2, "string_exit"}}, false, ""},
    {"target", {}, false, ""},
    {"other_param", {}, false, ""},
  }}},

  { 88, {"drop_npc", "", false, {
    {"", {}, false, ""},
  }}},

  { 89, {"add_npc", "", false, {
    {"", {}, false, ""},
  }}},

  { 90, {"take_victory_pts", "", false, {
    {"", {}, false, ""},
    {"who", {{0, "each"}, {1, "picked"}, {2, "total"}}, false, ""},
  }}},

  { 91, {"drop_all_items", "", false, {}}},

  { 92, {"change_rect_size", "", false, {
    {"level", {}, false, ""},
    {"rect", {}, false, ""},
    {"level_type", {{0, "land"}, {1, "dungeon"}}, false, ""},
    {"times_in_10k_mult", {}, false, ""},
    {"action", {{-1, "none"}, {0, "set_coords"}, {1, "offset"}, {2, "resize"}, {3, "warp"}}, false, ""},
    {"left_h", {}, false, ""},
    {"right_v", {}, false, ""},
    {"top", {}, false, ""},
    {"bottom", {}, false, ""},
  }}},

  { 93, {"enable_compass", "", false, {}}},

  { 94, {"disable_compass", "", false, {}}},

  { 95, {"change_dir", "", false, {
    {"", {{-1, "random"}, {1, "north"}, {2, "east"}, {3, "south"}, {4, "west"}}, false, ""},
  }}},

  { 96, {"disable_dungeon_map", "", false, {}}},

  { 97, {"enable_dungeon_map", "", false, {}}},

  { 98, {"require_registration", "", false, {}}},

  { 99, {"get_registration", "", false, {}}},

  {100, {"end_battle", "", false, {}}},

  {101, {"back_up", "", false, {}}},

  {102, {"level_up_picked", "", false, {}}},

  {103, {"cont_boat_camping", "", false, {
    {"if_boat", {{1, "true"}, {2, "false"}}, false, ""},
    {"if_camping", {{1, "true"}, {2, "false"}}, false, ""},
    {"set_boat", {{1, "true"}, {2, "false"}}, false, ""},
  }}},

  {104, {"enable_random_battles", "", false, {
    {"", {{0, "false"}, {1, "true"}}, false, ""},
  }}},

  {105, {"enable_allies", "", false, {
    {"", {{1, "false"}, {2, "true"}}, false, ""},
  }}},

  {106, {"set_dark_los", "", false, {
    {"dark", {{1, "false"}, {2, "true"}}, false, ""},
    {"skip_if_dark_same", {{0, "false"}, {1, "true"}}, false, ""},
    {"los", {{1, "true"}, {2, "false"}}, false, ""},
    {"skip_if_los_same", {{0, "false"}, {1, "true"}}, false, ""},
  }}},

  {107, {"pick_battle_2", "", false, {
    {"battle_low", {}, false, ""},
    {"battle_high", {}, false, ""},
    {"sound", {}, false, ""},
    {"loss_xap", {}, false, ""},
  }}},

  {108, {"change_picked", "", false, {
    {"what", {{1, "attacks_round"}, {2, "spells_round"}, {3, "movement"}, {4, "damage"}, {5, "spell_pts"}, {6, "hand_to_hand"}, {7, "stamina"}, {8, "armor_rating"}, {9, "to_hit"}, {10, "missile_adjust"}, {11, "magic_resistance"}, {12, "prestige"}}, false, ""},
    {"count", {}, false, ""},
  }}},

  {111, {"ret", "", false, {}}},

  {112, {"pop", "", false, {}}},

  {119, {"revive_npc_after", "", false, {}}},

  {120, {"change_monster", "", false, {
    {"", {{1, "npc"}, {2, "monster"}}, false, ""},
    {"", {}, false, ""},
    {"count", {}, false, ""},
    {"new_icon", {}, false, ""},
    {"new_traitor", {{-1, "same"}}, false, ""},
  }}},

  {121, {"kill_lower_undead", "", false, {}}},

  {122, {"fumble_weapon", "", false, {
    {"string", {}, true, ""},
    {"sound", {}, false, ""},
  }}},

  {123, {"rout_monsters", "", false, {
    {"", {}, false, ""},
    {"", {}, false, ""},
    {"", {}, false, ""},
    {"", {}, false, ""},
    {"", {}, false, ""},
  }}},

  {124, {"summon_monsters", "", false, {
    {"type", {{0, "individual"}}, false, ""},
    {"", {}, false, ""},
    {"count", {}, false, ""},
    {"sound", {}, false, ""},
  }}},

  {125, {"destroy_related", "", false, {
    {"", {}, false, ""},
    {"count", {{0, "all"}}, false, ""},
    {"unused", {}, false, ""},
    {"unused", {}, false, ""},
    {"force", {{0, "false"}, {1, "true"}}, false, ""},
  }}},

  {126, {"macro_criteria", "", false, {
    {"when", {{0, "round_number"}, {1, "percent_chance"}, {2, "flee_fail"}}, false, ""},
    {"round_percent_chance", {}, false, ""},
    {"repeat", {{0, "none"}, {1, "each_round"}, {2, "jmp_random"}}, false, ""},
    {"xap_low", {}, false, ""},
    {"xap_high", {}, false, ""},
  }}},

  {127, {"cont_monster_present", "", false, {
    {"", {}, false, ""},
  }}},
});

string disassemble_opcode(int16_t ap_code, int16_t arg_code,
    const vector<ecodes>& ecodes, const vector<string>& strings) {

  int16_t opcode = abs(ap_code);
  if (opcode_definitions.count(opcode) == 0) {
    size_t ecodes_id = abs(arg_code);
    if (ecodes_id >= ecodes.size()) {
      return string_printf("[%hd %hd]", ap_code, arg_code);
    }
    return string_printf("[%hd %hd [%hd %hd %hd %hd %hd]]", ap_code, arg_code,
        ecodes[ecodes_id].data[0], ecodes[ecodes_id].data[1],
        ecodes[ecodes_id].data[2], ecodes[ecodes_id].data[3],
        ecodes[ecodes_id].data[4]);
  }

  opcode_info op = opcode_definitions.at(opcode);
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
      arguments.push_back(ecodes[arg_code].data[x]); // intentional overflow (x)
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
    } else if (op.args[x].is_string_id) {
      string string_value = render_string_reference(strings, value);
      ret += string_printf("%s%s", pfx.c_str(),
          string_value.c_str());
    } else {
      ret += string_printf("%s%hd", pfx.c_str(), value);
    }

    if (use_negative_modifier) {
      ret += (", " + op.args[x].negative_modifier);
    }
  }

  return ret;
}

string disassemble_xap(int16_t ap_num, const ap_info& ap,
    const vector<ecodes>& ecodes, const vector<string>& strings,
    const vector<map_metadata>& land_metadata, const vector<map_metadata>& dungeon_metadata) {

  string data = string_printf("==== XAP id=%d\n", ap_num);

  for (size_t x = 0; x < land_metadata.size(); x++) {
    for (size_t y = 0; y < land_metadata[x].random_rects.size(); y++) {
      const auto& r = land_metadata[x].random_rects[y];
      if (r.xap_num[0] == ap_num || r.xap_num[1] == ap_num || r.xap_num[2] == ap_num) {
        data += string_printf("RANDOM RECTANGLE REFERENCE land_level=%lu rect_num=%lu start_coord=%d,%d end_coord=%d,%d\n",
            x, y, r.left, r.top, r.right, r.bottom);
      }
    }
  }

  for (size_t x = 0; x < dungeon_metadata.size(); x++) {
    for (size_t y = 0; y < dungeon_metadata[x].random_rects.size(); y++) {
      const auto& r = dungeon_metadata[x].random_rects[y];
      if (r.xap_num[0] == ap_num || r.xap_num[1] == ap_num || r.xap_num[2] == ap_num) {
        data += string_printf("RANDOM RECTANGLE REFERENCE dungeon_level=%lu rect_num=%lu start_coord=%d,%d end_coord=%d,%d\n",
            x, y, r.left, r.top, r.right, r.bottom);
      }
    }
  }

  for (int x = 0; x < 8; x++) {
    if (ap.command_codes[x] || ap.argument_codes[x]) {
      data += string_printf("  %s\n", disassemble_opcode(ap.command_codes[x],
          ap.argument_codes[x], ecodes, strings).c_str());
    }
  }

  return data;
}

string disassemble_xaps(const vector<ap_info>& aps, const vector<ecodes>& ecodes,
    const vector<string>& strings, const vector<map_metadata>& land_metadata,
    const vector<map_metadata>& dungeon_metadata) {
  string ret;
  for (size_t x = 0; x < aps.size(); x++) {
    ret += disassemble_xap(x, aps[x], ecodes, strings, land_metadata, dungeon_metadata);
  }
  return ret;
}

string disassemble_level_ap(int16_t level_num, int16_t ap_num, const ap_info& ap,
    const vector<ecodes>& ecodes, const vector<string>& strings, int dungeon) {

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
  string data = string_printf("==== %s AP level=%d id=%d x=%d y=%d%s\n",
      (dungeon ? "DUNGEON" : "LAND"), level_num, ap_num, ap.get_x(), ap.get_y(),
      extra.c_str());

  for (int x = 0; x < 8; x++) {
    if (ap.command_codes[x] || ap.argument_codes[x]) {
      data += string_printf("  %s\n", disassemble_opcode(ap.command_codes[x],
          ap.argument_codes[x], ecodes, strings).c_str());
    }
  }

  return data;
}

string disassemble_level_aps(int16_t level_num, const vector<ap_info>& aps,
    const vector<ecodes>& ecodes, const vector<string>& strings, int dungeon) {
  string ret;
  for (size_t x = 0; x < aps.size(); x++) {
    ret += disassemble_level_ap(level_num, x, aps[x], ecodes, strings, dungeon);
  }
  return ret;
}

string disassemble_all_aps(const vector<vector<ap_info>>& aps,
    const vector<ecodes>& ecodes, const vector<string>& strings, int dungeon) {
  string ret;
  for (size_t x = 0; x < aps.size(); x++) {
    ret += disassemble_level_aps(x, aps[x], ecodes, strings, dungeon);
  }
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// DATA DL

static uint16_t location_sig(uint8_t x, uint8_t y) {
  return ((uint16_t)x << 8) | y;
}

void map_data::byteswap() {
  for (int x = 0; x < 90; x++) {
    for (int y = 0; y < 90; y++) {
      this->data[x][y] = bswap16(this->data[x][y]);
    }
  }
}

void map_data::transpose() {
  for (int y = 0; y < 90; y++) {
    for (int x = y + 1; x < 90; x++) {
      int16_t t = this->data[y][x];
      this->data[y][x] = this->data[x][y];
      this->data[x][y] = t;
    }
  }
}

vector<map_data> load_dungeon_map_index(const string& filename) {
  return load_direct_file_data<map_data>(filename);
}

static Image dungeon_pattern(1, 1);

Image generate_dungeon_map(const map_data& mdata, const map_metadata& metadata,
    const vector<ap_info>& aps, int level_num) {

  Image map(90 * 16, 90 * 16);
  int pattern_x = 576, pattern_y = 320;

  unordered_map<uint16_t, vector<int>> loc_to_ap_nums;
  for (size_t x = 0; x < aps.size(); x++) {
    loc_to_ap_nums[location_sig(aps[x].get_x(), aps[x].get_y())].push_back(x);
  }

  for (int y = 89; y >= 0; y--) {
    for (int x = 89; x >= 0; x--) {
      int16_t data = mdata.data[y][x];

      int xp = x * 16;
      int yp = y * 16;
      map.fill_rect(xp, yp, 16, 16, 0, 0, 0, 0xFF);
      if (data & DUNGEON_TILE_WALL) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 0,
            pattern_y + 0, 0xFF, 0xFF, 0xFF);
      }
      if (data & DUNGEON_TILE_VERT_DOOR) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 16,
            pattern_y + 0, 0xFF, 0xFF, 0xFF);
      }
      if (data & DUNGEON_TILE_HORIZ_DOOR) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 32,
            pattern_y + 0, 0xFF, 0xFF, 0xFF);
      }
      if (data & DUNGEON_TILE_STAIRS) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 48,
            pattern_y + 0, 0xFF, 0xFF, 0xFF);
      }
      if (data & DUNGEON_TILE_COLUMNS) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 0,
            pattern_y + 16, 0xFF, 0xFF, 0xFF);
      }
      if (data & DUNGEON_TILE_SECRET_UP) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 0,
            pattern_y + 32, 0xFF, 0xFF, 0xFF);
      }
      if (data & DUNGEON_TILE_SECRET_RIGHT) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 16,
            pattern_y + 32, 0xFF, 0xFF, 0xFF);
      }
      if (data & DUNGEON_TILE_SECRET_DOWN) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 32,
            pattern_y + 32, 0xFF, 0xFF, 0xFF);
      }
      if (data & DUNGEON_TILE_SECRET_LEFT) {
        map.mask_blit(dungeon_pattern, xp, yp, 16, 16, pattern_x + 48,
            pattern_y + 32, 0xFF, 0xFF, 0xFF);
      }

      int text_xp = xp + 1;
      int text_yp = yp + 1;

      // draw the coords if both are multiples of 10
      if (y % 10 == 0 && x % 10 == 0) {
        map.draw_text(text_xp, text_yp, NULL, NULL, 0xFF, 0x00, 0xFF, 0xFF, 0, 0, 0,
            0x80, "%d,%d", x, y);
        text_yp += 8;
      }

      for (const auto& ap_num : loc_to_ap_nums[location_sig(x, y)]) {
        if (aps[ap_num].percent_chance < 100) {
          map.draw_text(text_xp, text_yp, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0,
              0x80, "%d-%d", ap_num, aps[ap_num].percent_chance);
        } else {
          map.draw_text(text_xp, text_yp, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0,
              0x80, "%d", ap_num);
        }
        text_yp += 8;
      }
    }
  }

  // finally, draw random rects
  draw_random_rects(map, metadata.random_rects, 16, 0, 0, 0xFF, 0xFF, 0xFF, 0,
      0, 0, 0x80);

  return map;
}



////////////////////////////////////////////////////////////////////////////////
// DATA LD

vector<map_data> load_land_map_index(const string& filename) {
  // format is the same as for dungeons, except it's in column-major order
  vector<map_data> data = load_dungeon_map_index(filename);
  for (auto& m : data) {
    m.transpose();
  }

  return data;
}

static unordered_map<string, tileset_definition> land_type_to_tileset_definition;

static unordered_map<string, int16_t> land_type_to_resource_id({
  {"custom_1", 306},
  {"custom_2", 307},
  {"custom_3", 308},
});

unordered_set<string> all_land_types() {
  unordered_set<string> all;
  for (const auto& it : land_type_to_tileset_definition) {
    all.insert(it.first);
  }
  return all;
}

static unordered_map<int16_t, ResourceFile::decoded_cicn> default_negative_tile_image_cache;
static unordered_map<int16_t, ResourceFile::decoded_cicn> scenario_negative_tile_image_cache;
static unordered_map<string, Image> positive_pattern_cache;

void populate_custom_tileset_configuration(const string& land_type,
    const tileset_definition& def) {
  land_type_to_tileset_definition[land_type] = def;
}

void populate_image_caches(const string& the_family_jewels_name) {
  ResourceFile rf(the_family_jewels_name.c_str());
  vector<pair<uint32_t, int16_t>> all_resources = rf.all_resources();

  for (const auto& it : all_resources) {
    if (it.first == RESOURCE_TYPE_cicn) {
      try {
        default_negative_tile_image_cache.emplace(it.second,
            rf.decode_cicn(it.second));
      } catch (const runtime_error& e) {
        fprintf(stderr, "warning: failed to load resource %08X:%d: %s\n",
            it.first, it.second, e.what());
      }
    }

    if (it.first == RESOURCE_TYPE_PICT) {
      string land_type;
      if (it.second == 300) {
        land_type = "outdoor";
      } else if (it.second == 302) {
        land_type = "dungeon";
      } else if (it.second == 303) {
        land_type = "cave";
      } else if (it.second == 304) {
        land_type = "indoor";
      } else if (it.second == 305) {
        land_type = "desert";
      } else if (it.second == 309) {
        land_type = "abyss";
      } else if (it.second == 310) {
        land_type = "snow";
      }

      if (land_type.size()) {
        try {
          positive_pattern_cache.emplace(land_type, rf.decode_PICT(it.second));
          if (!land_type.compare("dungeon")) {
            dungeon_pattern = positive_pattern_cache.at(land_type);
          }
        } catch (const runtime_error& e) {
          fprintf(stderr, "warning: failed to load resource %08X:%d: %s\n",
              it.first, it.second, e.what());
        }
      }
    }
  }
}

void add_custom_pattern(const string& land_type, Image& img) {
  positive_pattern_cache.emplace(land_type, img);
}

static const Image& positive_pattern_for_land_type(const string& land_type,
    const string& rsf_file) {

  if (positive_pattern_cache.count(land_type) == 0) { // custom pattern
    if (land_type_to_resource_id.count(land_type) == 0) {
      throw runtime_error("unknown custom land type");
    }

    ResourceFile rf(rsf_file.c_str());
    int16_t resource_id = land_type_to_resource_id.at(land_type);
    positive_pattern_cache.emplace(land_type, rf.decode_PICT(resource_id));
  }

  const Image& ret = positive_pattern_cache.at(land_type);
  if (ret.get_width() != 640 || ret.get_height() != 320) {
    throw runtime_error("positive pattern is the wrong size");
  }
  return ret;
}

Image generate_land_map(const map_data& mdata, const map_metadata& metadata,
    const vector<ap_info>& aps, int level_num, const level_neighbors& n,
    int16_t start_x, int16_t start_y, const string& rsf_file) {

  unordered_map<uint16_t, vector<int>> loc_to_ap_nums;
  for (size_t x = 0; x < aps.size(); x++) {
    loc_to_ap_nums[location_sig(aps[x].get_x(), aps[x].get_y())].push_back(x);
  }

  int horizontal_neighbors = (n.left != -1 ? 1 : 0) + (n.right != -1 ? 1 : 0);
  int vertical_neighbors = (n.top != -1 ? 1 : 0) + (n.bottom != -1 ? 1 : 0);

  const tileset_definition& tileset = land_type_to_tileset_definition.at(metadata.land_type);

  Image map(90 * 32 + horizontal_neighbors * 9, 90 * 32 + vertical_neighbors * 9);

  // write neighbor directory
  if (n.left != -1) {
    string text = string_printf("TO LEVEL %d", n.left);
    for (int y = (n.top != -1 ? 10 : 1); y < 90 * 32; y += 10 * 32) {
      for (size_t yy = 0; yy < text.size(); yy++) {
        map.draw_text(2, y + 9 * yy, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0xFF, "%c", text[yy]);
      }
    }
  }
  if (n.right != -1) {
    string text = string_printf("TO LEVEL %d", n.right);
    int x = 32 * 90 + (n.left != -1 ? 11 : 2);
    for (int y = (n.top != -1 ? 10 : 1); y < 90 * 32; y += 10 * 32) {
      for (size_t yy = 0; yy < text.size(); yy++) {
        map.draw_text(x, y + 9 * yy, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0xFF, "%c", text[yy]);
      }
    }
  }
  if (n.top != -1) {
    string text = string_printf("TO LEVEL %d", n.top);
    for (int x = (n.left != -1 ? 10 : 1); x < 90 * 32; x += 10 * 32) {
      map.draw_text(x, 1, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0xFF, "%s", text.c_str());
    }
  }
  if (n.bottom != -1) {
    string text = string_printf("TO LEVEL %d", n.bottom);
    int y = 32 * 90 + (n.top != -1 ? 10 : 1);
    for (int x = (n.left != -1 ? 10 : 1); x < 90 * 32; x += 10 * 32) {
      map.draw_text(x, y, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0xFF, "%s", text.c_str());
    }
  }

  // load the positive pattern
  Image positive_pattern = positive_pattern_for_land_type(metadata.land_type,
      rsf_file);

  unique_ptr<ResourceFile> rf;
  for (int y = 0; y < 90; y++) {
    for (int x = 0; x < 90; x++) {
      int16_t data = mdata.data[y][x];
      while (data <= -1000) {
        data += 1000;
      }
      while (data > 1000) {
        data -= 1000;
      }

      int xp = x * 32 + (n.left != -1 ? 9 : 0);
      int yp = y * 32 + (n.top != -1 ? 9 : 0);

      // draw the tile itself
      if (data < 0 || data > 200) { // masked tile

        // first try to construct it from the scenario resources
        if (scenario_negative_tile_image_cache.count(data) == 0) {
          try {
            if (!rf.get()) {
              rf.reset(new ResourceFile(rsf_file.c_str()));
            }
            scenario_negative_tile_image_cache.emplace(data, rf->decode_cicn(data));
          } catch (const out_of_range&) {
            // do nothing; we'll fall back to the default resources
          } catch (const runtime_error& e) {
            fprintf(stderr, "warning: failed to decode cicn %d: %s\n", data,
                e.what());
          }
        }

        // then copy it from the default resources if necessary
        if (scenario_negative_tile_image_cache.count(data) == 0 && 
            default_negative_tile_image_cache.count(data) != 0) {
          scenario_negative_tile_image_cache.emplace(data,
              default_negative_tile_image_cache.at(data));
        }

        // if we still don't have a tile, draw an error tile
        if (scenario_negative_tile_image_cache.count(data) == 0) {
          map.fill_rect(xp, yp, 32, 32, 0, 0, 0, 0xFF);
          map.draw_text(xp + 2, yp + 30 - 9, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0,
              0x80, "%04hX", data);

        } else {
          if (tileset.base_tile_id) {
            int source_id = tileset.base_tile_id - 1;
            int sxp = (source_id % 20) * 32;
            int syp = (source_id / 20) * 32;
            map.blit(positive_pattern, xp, yp, 32, 32, sxp, syp);
          } else {
            map.fill_rect(xp, yp, 32, 32, 0, 0, 0, 0xFF);
          }

          // negative tile images may be >32px in either dimension
          const auto& overlay = scenario_negative_tile_image_cache.at(data);
          map.blit(overlay.image, xp - (overlay.image.get_width() - 32),
              yp - (overlay.image.get_height() - 32),
              overlay.image.get_width(), overlay.image.get_height(), 0, 0);
        }

      } else if (data <= 200) { // standard tile
        int source_id = data - 1;
        int sxp = (source_id % 20) * 32;
        int syp = (source_id / 20) * 32;
        map.blit(positive_pattern, xp, yp, 32, 32, sxp, syp);

        // if it's a path, shade it red
        if (tileset.tiles[data].is_path) {
          map.fill_rect(xp, yp, 32, 32, 0xFF, 0x00, 0x00, 0x40);
        }
      }
    }
  }

  // this is a separate loop so we can draw APs that are hidden by large
  // negative tile overlays
  for (int y = 0; y < 90; y++) {
    for (int x = 0; x < 90; x++) {

      int xp = x * 32 + (n.left != -1 ? 9 : 0);
      int yp = y * 32 + (n.top != -1 ? 9 : 0);

      int16_t data = mdata.data[y][x];
      bool has_ap = ((data <= -1000) || (data > 1000));
      bool ap_is_secret = ((data <= -3000) || (data > 3000));
      int text_xp = xp + 2;
      int text_yp = yp + 2;

      // draw a red border if it has an AP
      if (has_ap && ap_is_secret) {
        map.draw_horizontal_line(xp, xp + 31, yp, 4, 0xFF, 0, 0);
        map.draw_horizontal_line(xp, xp + 31, yp + 31, 4, 0xFF, 0, 0);
        map.draw_vertical_line(xp, yp, yp + 31, 4, 0xFF, 0, 0);
        map.draw_vertical_line(xp + 31, yp, yp + 31, 4, 0xFF, 0, 0);
      } else if (has_ap) {
        map.draw_horizontal_line(xp, xp + 31, yp, 0, 0xFF, 0, 0);
        map.draw_horizontal_line(xp, xp + 31, yp + 31, 0, 0xFF, 0, 0);
        map.draw_vertical_line(xp, yp, yp + 31, 0, 0xFF, 0, 0);
        map.draw_vertical_line(xp + 31, yp, yp + 31, 0, 0xFF, 0, 0);
      }

      // draw the coords if both are multiples of 10
      if (y % 10 == 0 && x % 10 == 0) {
        map.draw_text(text_xp, text_yp, NULL, NULL, 0xFF, 0x00, 0xFF, 0xFF, 0, 0, 0,
            0x80, "%d,%d", x, y);
        text_yp += 8;
      }

      // draw "START" if this is the start loc
      if (x == start_x && y == start_y) {
        map.draw_text(text_xp, text_yp, NULL, NULL, 0, 0xFF, 0xFF, 0xFF, 0, 0, 0,
            0x80, "START");
        text_yp += 8;
      }

      // draw APs if present
      for (const auto& ap_num : loc_to_ap_nums[location_sig(x, y)]) {
        if (aps[ap_num].percent_chance < 100) {
          map.draw_text(text_xp, text_yp, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0,
              0x80, "%d-%d", ap_num, aps[ap_num].percent_chance);
        } else {
          map.draw_text(text_xp, text_yp, NULL, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0,
              0x80, "%d", ap_num);
        }
        text_yp += 8;
      }
    }
  }

  // finally, draw random rects
  draw_random_rects(map, metadata.random_rects, 32, (n.left != -1 ? 9 : 0),
      (n.top != -1 ? 9 : 0), 0xFF, 0xFF, 0xFF, 0, 0, 0, 0x80);

  return map;
}



////////////////////////////////////////////////////////////////////////////////
// DATA SD2

vector<string> load_string_index(const string& filename) {
  auto f = fopen_unique(filename.c_str(), "rb");

  vector<string> all_strings;
  while (!feof(f.get())) {

    string s;
    uint8_t len;
    int x;
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
