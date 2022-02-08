#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <vector>

#include "ResourceFile.hh"
#include "IndexFormats/ResourceFork.hh"
#include "SpriteDecoders/Decoders.hh"

using namespace std;



struct SpriteEntry {
  uint8_t valid;
  uint8_t unused;
  int16_t type;
  int16_t params[4];
  int16_t y;
  int16_t x;

  void byteswap() {
    this->type = bswap16(type);
    this->x = bswap16(x);
    this->y = bswap16(y);
    for (size_t x = 0; x < 4; x++) {
      this->params[x] = bswap16(this->params[x]);
    }
  }
} __attribute__((packed));

struct ForegroundTile {
  uint8_t unknown;
  uint8_t type;
} __attribute__((packed));

struct BackgroundTile {
  uint8_t unknown;
  uint8_t type;
} __attribute__((packed));

struct HarryLevel {
  // Apparently all levels are 128x128
  // 0000
  BackgroundTile background_tiles[0x4000];
  // 8000
  ForegroundTile foreground_tiles[0x4000];
  // 10000
  SpriteEntry sprites[332]; // Probably some space at the end here isn't actually part of the sprite table
  // 114C0
  char name[0x100]; // p-string, so first byte is the length
  // 115C0
  uint8_t unknown1[0x0AB4];
  // 12074
  int16_t player_tint_index;
  int16_t fall_respawn_x;
  int16_t fall_respawn_y;
  int16_t fall_damage; // can be negative
  int16_t level_tint_index;
  int16_t post_level_scroll_pict_id;
  int16_t pre_level_scroll_pict_id;
  int16_t post_level_pict_id;
  int16_t pre_level_pict_id;
  int16_t scroll_music_id;
  int16_t ripple_length;
  int16_t ripple_width;
  int16_t ripple_speed;
  int16_t unused1;
  int16_t unused2;
  int16_t unused3;
  // 12094
  uint8_t unknown2[0x68];
  // 120FC
  int16_t foreground_pict_id;
  int16_t background_pict_id;
  // 12100
  // There appears to be some unused space here; the levels are larger than this
  // but just have a bunch of 00 bytes

  ForegroundTile foreground_tile_at(size_t x, size_t y) const {
    if (x >= 128 || y >= 128) {
      throw out_of_range("invalid tile coordinates");
    }
    return this->foreground_tiles[x * 128 + y];
  }
  BackgroundTile background_tile_at(size_t x, size_t y) const {
    if (x >= 128 || y >= 128) {
      throw out_of_range("invalid tile coordinates");
    }
    return this->background_tiles[x * 128 + y];
  }

  void byteswap() {
    for (size_t x = 0; x < sizeof(this->sprites) / sizeof(this->sprites[0]); x++) {
      this->sprites[x].byteswap();
    }
    this->player_tint_index = bswap16(this->player_tint_index);
    this->fall_respawn_x = bswap16(this->fall_respawn_x);
    this->fall_respawn_y = bswap16(this->fall_respawn_y);
    this->fall_damage = bswap16(this->fall_damage);
    this->level_tint_index = bswap16(this->level_tint_index);
    this->post_level_scroll_pict_id = bswap16(this->post_level_scroll_pict_id);
    this->pre_level_scroll_pict_id = bswap16(this->pre_level_scroll_pict_id);
    this->post_level_pict_id = bswap16(this->post_level_pict_id);
    this->pre_level_pict_id = bswap16(this->pre_level_pict_id);
    this->scroll_music_id = bswap16(this->scroll_music_id);
    this->ripple_length = bswap16(this->ripple_length);
    this->ripple_width = bswap16(this->ripple_width);
    this->ripple_speed = bswap16(this->ripple_speed);
    this->unused1 = bswap16(this->unused1);
    this->unused2 = bswap16(this->unused2);
    this->unused3 = bswap16(this->unused3);
    this->foreground_pict_id = bswap16(this->foreground_pict_id);
    this->background_pict_id = bswap16(this->background_pict_id);
  }
} __attribute__((packed));

struct HarryWorld {
  // 0000
  char name[0x100]; // p-string
  // 0100
  uint32_t unknown1[17];
  // 0144
  int16_t default_scroll_music_id;
  int16_t win_scroll_pict_id;
  int16_t win_pict_id;
  int16_t win_pict_seconds;
  int16_t win_music_id;
  int16_t unused1[11];
  // 0164
  uint8_t unknown2[613];
  // 03C9
  struct {
    uint8_t length;
    char data[0xFF];
  } strings[0x100];

  void byteswap() {
    this->default_scroll_music_id = bswap16(this->default_scroll_music_id);
    this->win_scroll_pict_id = bswap16(this->win_scroll_pict_id);
    this->win_pict_id = bswap16(this->win_pict_id);
    this->win_pict_seconds = bswap16(this->win_pict_seconds);
    this->win_music_id = bswap16(this->win_music_id);
  }
};



struct SpriteDefinition {
  int16_t hrsp_id;
  const char* overlay_text;
  vector<string> (*get_extra_info)(const SpriteEntry&);

  SpriteDefinition(
    int16_t hrsp_id,
    const char* overlay_text = nullptr,
    vector<string> (*get_extra_info)(const SpriteEntry&) = nullptr)
  : hrsp_id(hrsp_id), overlay_text(overlay_text), get_extra_info(get_extra_info) { }
};

static vector<string> get_default_extra_info(const SpriteEntry& sprite) {
  vector<string> ret;
  for (size_t z = 0; z < 4; z++) {
    if (sprite.params[z]) {
      ret.emplace_back(string_printf("%zu/%hd", z, sprite.params[z]));
    }
  }
  return ret;
} __attribute__((unused))

static vector<string> get_extra_info_debug(const SpriteEntry& sprite) {
  static size_t sprite_index = 0;
  fprintf(stderr, "[sprite debug] type=%05hd [0]=%05hd [1]=%05hd [2]=%05hd [3]=%05hd x=%hd y=%hd idx=%zu\n",
      sprite.type, sprite.params[0], sprite.params[1], sprite.params[2], sprite.params[3], sprite.x, sprite.y, sprite_index);
  vector<string> ret = get_default_extra_info(sprite);
  ret.emplace_back(string_printf("dbg index %zu", sprite_index++));
  return ret;
}

static vector<string> get_locked_door_extra_info(const SpriteEntry& sprite) {
  if (sprite.params[0] == 0) {
    return {"blue key"};
  } else if (sprite.params[0] == 1) {
    return {"yellow key"};
  } else if (sprite.params[0] == 2) {
    return {"green key"};
  } else {
    return {string_printf("key color %hd", sprite.params[0])};
  }
}

static const unordered_map<int16_t, SpriteDefinition> sprite_defs({
  {500, SpriteDefinition(2001)}, // water cooler
  {501, SpriteDefinition(2001)}, // water cooler
  {550, SpriteDefinition(1301)}, // exploding toxic waste barrel
  {560, SpriteDefinition(1801)}, // empty swivel chair (blue); other colors probably done via CLUT
  {601, SpriteDefinition(3901)}, // office plant

  {700, SpriteDefinition(1501)}, // mug of coffee
  {701, SpriteDefinition(1501)}, // mug of coffee
  {702, SpriteDefinition(1503)}, // box of donuts
  {703, SpriteDefinition(1504)}, // single donut
  {711, SpriteDefinition(1505)}, // regular staple ammo
  {712, SpriteDefinition(1506)}, // BADASS ammo
  {713, SpriteDefinition(1507)}, // shrapnel ammo

  {901, SpriteDefinition(2601, "up")}, // door going up
  {911, SpriteDefinition(2610, "down")}, // door going down
  {921, SpriteDefinition(2619, "left")}, // door going left
  {931, SpriteDefinition(2628, "right")}, // door going right

  {903, SpriteDefinition(2601, "up/locked", get_locked_door_extra_info)}, // locked door going up (+CLUT)
  {913, SpriteDefinition(2610, "down/locked", get_locked_door_extra_info)}, // locked door going down (+CLUT)
  {923, SpriteDefinition(2619, "left/locked", get_locked_door_extra_info)}, // locked door going left (+CLUT)
  {933, SpriteDefinition(2628, "right/locked", get_locked_door_extra_info)}, // locked door going right (+CLUT)

  {951, SpriteDefinition(4401)}, // sliding door, h
  {961, SpriteDefinition(4451)}, // sliding door, v

  {1021, SpriteDefinition(2508)}, // dart enemy, facing right
  {1019, SpriteDefinition(6034)}, // dartboard facing left
  {1011, SpriteDefinition(2501)}, // dart enemy, facing left
  {1029, SpriteDefinition(6035)}, // dartboard facing right
  {1031, SpriteDefinition(2515)}, // dart enemy, facing up
  {1039, SpriteDefinition(6036)}, // dartboard facing down
  {1041, SpriteDefinition(2522)}, // dart enemy, facing down
  {1049, SpriteDefinition(6037)}, // dartboard facing down

  {1111, SpriteDefinition(4301)}, // scientist, facing right
  {1121, SpriteDefinition(4309)}, // scientist, facing left
  {1131, SpriteDefinition(4317)}, // scientist, facing up
  {1141, SpriteDefinition(4325)}, // scientist, facing down
  {1211, SpriteDefinition(4301)}, // mad scientist, facing right (+CLUT)
  {1221, SpriteDefinition(4309)}, // mad scientist, facing left (+CLUT)
  {1231, SpriteDefinition(4317)}, // mad scientist, facing up (+CLUT)
  {1241, SpriteDefinition(4325)}, // mad scientist, facing down (+CLUT)

  {1301, SpriteDefinition(2101)}, // benign co-worker #1, facing up
  {1321, SpriteDefinition(2302)}, // benign co-worker #2, facing up
  {1341, SpriteDefinition(2303)}, // benign co-worker #1, facing down
  {1361, SpriteDefinition(2304)}, // benign co-worker #2, facing down
  {1381, SpriteDefinition(2305)}, // benign co-worker #1, facing left
  {1401, SpriteDefinition(2306)}, // benign co-worker #2, facing left
  {1421, SpriteDefinition(2307)}, // benign co-worker #1, facing right
  {1441, SpriteDefinition(2308)}, // benign co-worker #2, facing right

  {1601, SpriteDefinition(3701)}, // mailroom guy, up
  {1609, SpriteDefinition(6051)}, // mailroom awning, up
  {1611, SpriteDefinition(3702)}, // mailroom guy, down
  {1619, SpriteDefinition(6052)}, // mailroom awning, down
  {1621, SpriteDefinition(3703)}, // mailroom guy, right
  {1629, SpriteDefinition(6053)}, // mailroom awning, right
  {1631, SpriteDefinition(3704)}, // mailroom guy, left
  {1639, SpriteDefinition(6054)}, // mailroom awning, left

  {1701, SpriteDefinition(1401)}, // toxic blob
  {1711, SpriteDefinition(1401, "major")}, // major toxic blob
  {1721, SpriteDefinition(1401, "sentient")}, // sentient blob

  {1801, SpriteDefinition(4201)}, // robot
  {1811, SpriteDefinition(4201, "stat")}, // stationary robot (gold) (+CLUT)
  {1821, SpriteDefinition(4201, "major")}, // boss robot (red) (+CLUT)
  {1851, SpriteDefinition(4201, "rev-h")}, // reversor robot, h (+CLUT)
  {1852, SpriteDefinition(4201, "rev-v")}, // reversor robot, v (+CLUT)
  {1861, SpriteDefinition(4201, "maj-rev-h")}, // major reverser robot, h (+CLUT)
  {1862, SpriteDefinition(4201, "maj-rev-v")}, // major reverser robot, v (+CLUT)

  {1901, SpriteDefinition(4604)}, // security gun, up
  {1902, SpriteDefinition(4612)}, // security gun, down
  {1903, SpriteDefinition(4620)}, // security gun, left
  {1904, SpriteDefinition(4628)}, // security gun, right
  {1911, SpriteDefinition(4604, "major")}, // power security gun, up (+CLUT)
  {1912, SpriteDefinition(4612, "major")}, // power security gun, down (+CLUT)
  {1913, SpriteDefinition(4620, "major")}, // power security gun, left (+CLUT)
  {1914, SpriteDefinition(4628, "major")}, // power security gun, right (+CLUT)
  {1921, SpriteDefinition(4604, "missile")}, // missile security gun, up (+CLUT)
  {1922, SpriteDefinition(4612, "missile")}, // missile security gun, down (+CLUT)
  {1923, SpriteDefinition(4620, "missile")}, // missile security gun, left (+CLUT)
  {1924, SpriteDefinition(4628, "missile")}, // missile security gun, right (+CLUT)

  {2001, SpriteDefinition(4701)}, // machine gun guy
  {2011, SpriteDefinition(4701, "major")}, // major machine gun guy (+CLUT)
  {2021, SpriteDefinition(4701, "missile")}, // missile guy (+CLUT)

  // these appear in the editor readme but don't appear to have sprites (?)
  // 2101  tank, horizontal
  // 2102  tank, vertical
  // 2111  major tank, horizontal
  // 2112  major tank, vertical
  // 2501  acid pool, small
  // 2601  acid pool, large
  // 4100  overhead pipe, horizontal
  // 4105  overhead pipe, vertical

  {2201, SpriteDefinition(4902)}, // Dr. Ubermann

  {2301, SpriteDefinition(1921, "up")}, // air conditioner, up
  {2302, SpriteDefinition(1921, "down")}, // air conditioner, down
  {2303, SpriteDefinition(1921, "left")}, // air conditioner, left
  {2304, SpriteDefinition(1921, "right")}, // air conditioner, right

  {3001, SpriteDefinition(6032)}, // bed of tacks
  {3100, SpriteDefinition(1905)}, // telephone

  {3900, SpriteDefinition(3401)}, // grate, h
  {3905, SpriteDefinition(3402)}, // grate, v
  {3910, SpriteDefinition(3401, "reappear")}, // grate, h, red (+CLUT)
  {3915, SpriteDefinition(3402, "reappear")}, // grate, v, red (+CLUT)

  {4201, SpriteDefinition(6041)}, // fire pipe, up
  {4202, SpriteDefinition(6042)}, // fire pipe, down
  {4203, SpriteDefinition(6043)}, // fire pipe, left
  {4204, SpriteDefinition(6044)}, // fire pipe, right

  {5001, SpriteDefinition(3301)}, // Ghost swivel warrior
  {5011, SpriteDefinition(3301, "chieftain")}, // Ghost swivel chieftain (+CLUT)

  {6001, SpriteDefinition(1601, "up/slow")}, // Rolling Boulder, Up
  {6002, SpriteDefinition(1601, "down/slow")}, // Rolling Boulder, Down
  {6003, SpriteDefinition(1601, "left/slow")}, // Rolling Boulder, Left
  {6004, SpriteDefinition(1601, "right/slow")}, // Rolling Boulder, Right
  {6011, SpriteDefinition(1601, "up/fast")}, // Rolling Boulder, Fast, Up
  {6012, SpriteDefinition(1601, "down/fast")}, // Rolling Boulder, Fast, Down
  {6013, SpriteDefinition(1601, "left/fast")}, // Rolling Boulder, Fast, Left
  {6014, SpriteDefinition(1601, "right/fast")}, // Rolling Boulder, Fast, Right

  {9200, SpriteDefinition(1906)}, // copying machine (saved-game location)

  {9300, SpriteDefinition(1907)}, // soul statue

  {9401, SpriteDefinition(1908)}, // cannon up
  {9411, SpriteDefinition(1909)}, // cannon down
  {9421, SpriteDefinition(1910)}, // cannon left
  {9431, SpriteDefinition(1911)}, // cannon right

  {9501, SpriteDefinition(1701)}, // wall button up
  {9511, SpriteDefinition(1711)}, // wall button down
  {9521, SpriteDefinition(1721)}, // wall button left
  {9531, SpriteDefinition(1731)}, // wall button right
  {9541, SpriteDefinition(1741)}, // floor button

  {9600, SpriteDefinition(1902)}, // vending machine

  {9701, SpriteDefinition(4101)}, // ramp, up
  {9702, SpriteDefinition(4102)}, // ramp, down
  {9703, SpriteDefinition(4103)}, // ramp, left
  {9704, SpriteDefinition(4104)}, // ramp, right

  {9800, SpriteDefinition(1901)}, // incinerator

  {9901, SpriteDefinition(4501)}, // stairs, up
  {9902, SpriteDefinition(4502)}, // stairs, down
  {9903, SpriteDefinition(4503)}, // stairs, left
  {9904, SpriteDefinition(4504)}, // stairs, right

  {9991, SpriteDefinition(3201)}, // fade exit, up
  {9992, SpriteDefinition(3202)}, // fade exit, down
  {9993, SpriteDefinition(3203)}, // fade exit, left
  {9994, SpriteDefinition(3204)}, // fade exit, right
  {9995, SpriteDefinition(0, "invis exit")}, // invisible exit - special rendering

  {11199, SpriteDefinition(3601)}, // stack of papers
  {11299, SpriteDefinition(3602)}, // staple gun
  {11397, SpriteDefinition(3652)}, // green key
  {11398, SpriteDefinition(3651)}, // yellow key
  {11399, SpriteDefinition(3603)}, // blue key
  {11499, SpriteDefinition(3604)}, // caffeine pill
  {11599, SpriteDefinition(3605)}, // B.A.D.A.S.S. (Bi-Angular Directional-Accelerated Staple System)
  {11699, SpriteDefinition(3661)}, // mystery vial
  {11799, SpriteDefinition(3607)}, // shrapnel gun
  {11899, SpriteDefinition(3608)}, // soda can
  {11999, SpriteDefinition(3609)}, // power of the swivel

  {21000, SpriteDefinition(3801)}, // note
});



static shared_ptr<Image> decode_PICT_with_transparency_cached(
    int16_t id,
    unordered_map<int16_t, shared_ptr<Image>>& cache,
    ResourceFile& rf) {
  try {
    return cache.at(id);
  } catch (const out_of_range&) {
    try {
      auto decode_result = rf.decode_PICT(id);
      if (!decode_result.embedded_image_format.empty()) {
        throw runtime_error(string_printf("PICT %hd is an embedded image", id));
      }

      // Convert white pixels to transparent pixels
      decode_result.image.set_has_alpha(true);
      decode_result.image.set_alpha_from_mask_color(0xFFFFFFFF);

      auto emplace_ret = cache.emplace(id, new Image(move(decode_result.image)));
      return emplace_ret.first->second;

    } catch (const out_of_range&) {
      return shared_ptr<Image>();
    }
  }
}



void print_usage() {
  fprintf(stderr, "\
Usage: harry_render [options]\n\
\n\
Options:\n\
  --clut-file=FILE: Use this color table (required). You can use a .bin file\n\
      produced by resource_dasm here.\n\
  --levels-file=FILE: Use this file instead of \"Episode 1\".\n\
  --sprites-file=FILE: Use this file instead of \"Harry Graphics\".\n\
  --level=N: Only render map for this level. Can be given multiple times.\n\
  --foreground-opacity=N: Render foreground layer with this opacity\n\
      (0-255; default 255).\n\
  --skip-render-background: Don\'t render background tiles.\n\
  --skip-render-sprites: Don\'t render sprites.\n\
  --print-unused-pict-ids: When done, print the IDs of all the PICT resources\n\
    that were not used.\n");
}

int main(int argc, char** argv) {
  unordered_set<int16_t> target_levels;
  uint8_t foreground_opacity = 0xFF;
  bool render_background_tiles = true;
  bool render_sprites = true;

  string levels_filename = "Episode 1";
  string sprites_filename = "Harry Graphics";
  string clut_filename;

  for (int z = 1; z < argc; z++) {
    if (!strcmp(argv[z], "--help") || !strcmp(argv[z], "-h")) {
      print_usage();
      return 0;
    } else if (!strncmp(argv[z], "--level=", 8)) {
      target_levels.insert(atoi(&argv[z][8]));
    } else if (!strncmp(argv[z], "--levels-file=", 14)) {
      levels_filename = &argv[z][14];
    } else if (!strncmp(argv[z], "--sprites-file=", 15)) {
      sprites_filename = &argv[z][15];
    } else if (!strncmp(argv[z], "--clut-file=", 12)) {
      clut_filename = &argv[z][12];
    } else if (!strncmp(argv[z], "--foreground-opacity=", 21)) {
      foreground_opacity = stoul(&argv[z][21], nullptr, 0);
    } else if (!strcmp(argv[z], "--skip-render-background")) {
      render_background_tiles = false;
    } else if (!strcmp(argv[z], "--skip-render-sprites")) {
      render_sprites = false;
    } else {
      print_usage();
      throw invalid_argument(string_printf("invalid option: %s", argv[z]));
    }
  }

  if (clut_filename.empty()) {
    print_usage();
    throw invalid_argument("--clut-filename is required");
  }

  string clut_data = load_file(clut_filename);
  auto clut = ResourceFile::decode_clut(clut_data.data(), clut_data.size());

  const string levels_resource_filename = levels_filename + "/..namedfork/rsrc";
  const string sprites_resource_filename = sprites_filename + "/..namedfork/rsrc";

  ResourceFile levels(parse_resource_fork(load_file(levels_resource_filename.c_str())));
  ResourceFile sprites(parse_resource_fork(load_file(sprites_resource_filename.c_str())));

  uint32_t level_resource_type = 0x486C766C; // Hlvl
  auto level_resources = levels.all_resources_of_type(level_resource_type);
  sort(level_resources.begin(), level_resources.end());

  unordered_map<int16_t, shared_ptr<Image>> world_pict_cache;
  unordered_map<int16_t, shared_ptr<Image>> sprites_cache;

  for (int16_t level_id : level_resources) {
    if (!target_levels.empty() && !target_levels.count(level_id)) {
      continue;
    }

    string level_data = levels.get_resource(level_resource_type, level_id).data;
    HarryLevel* level = reinterpret_cast<HarryLevel*>(level_data.data());
    level->byteswap();

    Image result(128 * 32, 128 * 32);

    if ((foreground_opacity != 0) || render_background_tiles) {
      shared_ptr<Image> foreground_pict = level->foreground_pict_id ?
          decode_PICT_with_transparency_cached(level->foreground_pict_id, world_pict_cache, levels) :
          decode_PICT_with_transparency_cached(181, sprites_cache, sprites);
      shared_ptr<Image> background_pict = level->background_pict_id ?
          decode_PICT_with_transparency_cached(level->background_pict_id, world_pict_cache, levels) :
          decode_PICT_with_transparency_cached(180, sprites_cache, sprites);
      for (size_t y = 0; y < 128; y++) {
        for (size_t x = 0; x < 128; x++) {
          if (render_background_tiles) {
            auto bg_tile = level->background_tile_at(x, y);
            {
              uint16_t src_x = (bg_tile.type % 8) * 32;
              uint16_t src_y = (bg_tile.type / 8) * 32;
              if (src_y >= background_pict->get_height()) {
                result.draw_text(x * 32, y * 32, 0x000000FF, 0xFF0000FF,
                    "%02hhX/%02hhX", bg_tile.unknown, bg_tile.type);
              } else {
                result.blit(*background_pict, x * 32, y * 32, 32, 32, src_x, src_y);
              }
            }
            if (bg_tile.unknown && bg_tile.unknown != 0xFF) {
              result.draw_text(x * 32, y * 32 + 10, 0x000000FF, 0xFF0000FF,
                  "%02hhX", bg_tile.unknown);
            }
          }

          if (foreground_opacity != 0) {
            auto fg_tile = level->foreground_tile_at(x, y);
            if (fg_tile.type != 0xFF) {
              uint16_t src_x = (fg_tile.type % 8) * 32;
              uint16_t src_y = (fg_tile.type / 8) * 32;
              if (src_y >= foreground_pict->get_height()) {
                result.draw_text(x * 32, y * 32 + 10, 0x000000FF, 0xFF0000FF,
                    "%02hhX/%02hhX", fg_tile.unknown, fg_tile.type);
              } else {
                result.blend_blit(*foreground_pict, x * 32, y * 32, 32, 32,
                    src_x, src_y, foreground_opacity);
              }
            }
            if (fg_tile.unknown && fg_tile.unknown != 0xFF) {
              result.draw_text(x * 32, y * 32 + 10, 0x000000FF, 0xFF0000FF,
                  "%02hhX", fg_tile.unknown);
            }
          }
        }
      }
    }

    if (render_sprites) {
      static const size_t max_sprites = sizeof(level->sprites) / sizeof(level->sprites[0]);
      for (size_t z = 0; z < max_sprites; z++) {
        const auto& sprite = level->sprites[z];
        if (!sprite.valid) {
          continue;
        }

        bool render_text_as_unknown = false;
        const SpriteDefinition* sprite_def = nullptr;
        try {
          sprite_def = &sprite_defs.at(sprite.type);
        } catch (const out_of_range&) {
          render_text_as_unknown = true;
        }

        shared_ptr<Image> sprite_pict;
        if (sprite_def && sprite_def->hrsp_id) {
          try {
            sprite_pict = sprites_cache.at(sprite_def->hrsp_id);
          } catch (const out_of_range&) {
            try {
              const auto& data = sprites.get_resource(0x48725370, sprite_def->hrsp_id).data; // HrSp
              sprite_pict.reset(new Image(decode_HrSp(data, clut)));
              sprites_cache.emplace(sprite_def->hrsp_id, sprite_pict);
            } catch (const out_of_range&) { }
          }
        }

        int16_t sprite_x = sprite.x - 6;
        int16_t sprite_y = sprite.y - 6;

        if (sprite_pict.get()) {
          result.blit(*sprite_pict, sprite_x, sprite_y,
              sprite_pict->get_width(), sprite_pict->get_height(), 0, 0);
        }

        if (render_text_as_unknown) {
          result.draw_text(sprite_x, sprite_y, 0x000000FF, 0xFF0000FF,
              "%hd-%zX", sprite.type, z);
        } else {
          result.draw_text(sprite_x, sprite_y, 0xFFFFFF80, 0x00000040,
              "%hd-%zX", sprite.type, z);
        }

        size_t y_offset = 10;
        if (sprite_def && sprite_def->overlay_text) {
          result.draw_text(sprite_x, sprite_y + y_offset, 0xFFFFFF80, 0x00000040,
              "%s", sprite_def->overlay_text);
          y_offset += 10;
        }

        vector<string> extra_info = (sprite_def && sprite_def->get_extra_info)
            ? sprite_def->get_extra_info(sprite)
            : get_default_extra_info(sprite);
        for (const string& line : extra_info) {
          result.draw_text(sprite_x, sprite_y + y_offset, 0xFFFFFF80, 0x00000040,
              "%s", line.c_str());
          y_offset += 10;
        }
      }

      // TODO
      // result.draw_text(level->player_start_x, level->player_start_y, 0xFFFFFF80, 0x00000040,
      //     level->player_faces_left_at_start ? "<- START" : "START ->");
    }

    string sanitized_name;
    for (ssize_t x = 0; x < level->name[0]; x++) {
      char ch = level->name[x + 1];
      if (ch > 0x20 && ch <= 0x7E) {
        sanitized_name.push_back(ch);
      } else {
        sanitized_name.push_back('_');
      }
    }

    string result_filename = string_printf("Harry_Level_%" PRId16 "_%s.bmp",
        level_id, sanitized_name.c_str());
    result.save(result_filename.c_str(), Image::ImageFormat::WindowsBitmap);
    fprintf(stderr, "... %s\n", result_filename.c_str());
  }

  return 0;
}
