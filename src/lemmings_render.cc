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
#include "ImageSaver.hh"
#include "IndexFormats/Formats.hh"
#include "SpriteDecoders/Decoders.hh"

using namespace std;



struct LemmingsObjectDefinition {
  be_uint16_t flags;
  be_uint16_t seq_frame;
  be_uint16_t seq_length; // Number of frames
  be_uint16_t seq_base; // Index of first animation frame in Objects SHPD
  be_int16_t frame_1;
  be_int16_t sound_1;
  be_int16_t frame_2;
  be_int16_t sound_2;
  uint8_t collision_type;
  uint8_t unused;
  be_int16_t x_offset;
  be_int16_t y_offset;
  be_uint16_t width;
  be_uint16_t height;
} __attribute__((packed));

struct LemmingsLevel {
  be_uint16_t release_rate;
  be_uint16_t lemming_count;
  be_uint16_t goal_count;
  be_uint16_t minutes;
  be_uint16_t climbers;
  be_uint16_t floaters;
  be_uint16_t bombers;
  be_uint16_t blockers;
  be_uint16_t builders;
  be_uint16_t bashers;
  be_uint16_t miners;
  be_uint16_t diggers;
  be_uint16_t x_start;
  be_uint16_t ground_type;
  be_uint16_t iff_number;
  be_uint16_t blank;
  struct ObjectReference {
    be_uint16_t data_x;
    be_uint16_t data_y;
    be_uint16_t data_type;
    be_uint16_t data_flags;

    bool is_blank() const {
      return this->data_x == 0 && this->data_y == 0 &&
             this->data_type == 0 && this->data_flags == 0;
    }
    int16_t x() const {
      return this->data_x - 16;
    }
    int16_t y() const {
      return this->data_y;
    }
    uint16_t type() const {
      return this->data_type;
    }
    bool is_fake() const {
      return this->data_flags & 0x1000;
    }
    bool faces_left() const {
      return this->data_flags & 0x2000;
    }
    bool draw_only_on_tiles() const { // Used for one-way-basher arrows
      return this->data_flags & 0x4000;
    }
    bool background() const {
      return this->data_flags & 0x8000;
    }
  } __attribute__((packed));
  ObjectReference objects[32];
  struct TileReference {
    // Bits:
    // BVE--XXXXXXXXXXX --YYYYYYY-TTTTTT
    // X = x coordinate
    // Y = y coordinate
    // T = type (image index in SHPD list)
    // B = render in background (behind other tiles)
    // V = vertical reverse (and ignore y origin in SHPD image)
    // E = erase this object's shape instead of adding it to the level
    be_uint32_t data;

    bool is_blank() const {
      return this->data == 0xFFFFFFFF;
    }
    bool background() const {
      return this->data & 0x80000000;
    }
    bool vertical_reverse() const {
      return this->data & 0x40000000;
    }
    bool erase() const {
      return this->data & 0x20000000;
    }
    int16_t x() const {
      return ((this->data >> 16) & 0x07FF) - 16;
    }
    int16_t y() const {
      int16_t y = ((this->data >> 7) & 0xFF) - 4;
      if (y > 160) {
        y -= 256;
      }
      return y;
    }
    uint8_t type() const {
      return this->data & 0x3F;
    }
  } __attribute__((packed));
  TileReference tiles[400];
  struct CollisionArea {
    be_uint16_t coords;
    uint8_t size;
    uint8_t offsets;

    bool is_blank() const {
      return (this->coords == 0x0000) && (this->size == 0x00) && (this->offsets == 0x00);
    }
    int16_t x() const {
      return (((this->coords >> 7) & 0x1FF) * 4) - 16 - ((this->offsets >> 6) & 3);
    }
    int16_t y() const {
      return ((this->coords & 0x7F) * 4) - ((this->offsets >> 4) & 3);
    }
    uint16_t width() const {
      return (((this->size >> 4) & 0x0F) * 4) + 4 - ((this->offsets >> 2) & 3);
    }
    uint16_t height() const {
      return ((this->size & 0x0F) * 4) + 4 - (this->offsets & 3);
    }
  } __attribute__((packed));
  CollisionArea collisions[32];
  char name[0x20];
} __attribute__((packed));



uint32_t alpha_blend(uint32_t existing_c, uint32_t incoming_c, uint32_t incoming_alpha) {
  uint32_t er = (existing_c >> 24) & 0xFF;
  uint32_t eg = (existing_c >> 16) & 0xFF;
  uint32_t eb = (existing_c >> 8) & 0xFF;
  uint32_t ir = (incoming_c >> 24) & 0xFF;
  uint32_t ig = (incoming_c >> 16) & 0xFF;
  uint32_t ib = (incoming_c >> 8) & 0xFF;
  uint32_t a = incoming_c & 0xFF;
  uint8_t r = ((er * (0xFF - incoming_alpha)) + (ir * incoming_alpha)) / 0xFF;
  uint8_t g = ((eg * (0xFF - incoming_alpha)) + (ig * incoming_alpha)) / 0xFF;
  uint8_t b = ((eb * (0xFF - incoming_alpha)) + (ib * incoming_alpha)) / 0xFF;
  return (r << 24) | (g << 16) | (b << 8) | a;
}



void print_usage() {
  fprintf(stderr, "\
Usage: lemmings_render [options]\n\
\n\
Options:\n\
  --help\n\
      Show this help text.\n\
  --clut-file=FILE\n\
      Use this color table. You can use a .bin file produced by resource_dasm.\n\
  --levels-file=FILE\n\
      Use this file instead of \"Levels\".\n\
  --graphics-file=FILE\n\
      Use this file instead of \"Graphics\" or \"BW Graphics\".\n\
  --v2\n\
      Use SHPD v2 format (from Oh No! More Lemmings).\n\
  --level=N\n\
      Only render map for this level. Can be given multiple times.\n\
  --show-object-ids\n\
      Annotate objects with their object IDs in the generated map.\n\
  --show-tile-ids\n\
      Annotate tiles with their IDs in the generated map.\n\
  --erase-opacity=N\n\
      Draw erasers with this opacity (0-255; default 255).\n\
  --tile-opacity=N\n\
      Draw normal tiles with this opacity (0-255; default 255).\n\
  --object-opacity=N\n\
      Draw objects with this opacity (0-255; default 255).\n\
\n"
IMAGE_SAVER_HELP);
}

int main(int argc, char** argv) {
  unordered_set<int16_t> target_levels;
  string levels_filename = "Levels";
  string graphics_filename;
  string clut_filename;
  bool show_object_ids = false;
  bool show_tile_ids = false;
  uint8_t erase_opacity = 0xFF;
  uint8_t tile_opacity = 0xFF;
  uint8_t object_opacity = 0xFF;
  bool show_unused_images = false;
  bool use_shpd_v2 = false;
  ImageSaver image_saver;
  for (int z = 1; z < argc; z++) {
    if (!strcmp(argv[z], "--help") || !strcmp(argv[z], "-h")) {
      print_usage();
      return 0;
    } else if (!strcmp(argv[z], "--v2")) {
      use_shpd_v2 = true;
    } else if (!strncmp(argv[z], "--level=", 8)) {
      target_levels.insert(atoi(&argv[z][8]));
    } else if (!strncmp(argv[z], "--levels-file=", 14)) {
      levels_filename = &argv[z][14];
    } else if (!strncmp(argv[z], "--graphics-file=", 16)) {
      graphics_filename = &argv[z][16];
    } else if (!strncmp(argv[z], "--clut-file=", 12)) {
      clut_filename = &argv[z][12];
    } else if (!strcmp(argv[z], "--show-object-ids")) {
      show_object_ids = true;
    } else if (!strcmp(argv[z], "--show-tile-ids")) {
      show_tile_ids = true;
    } else if (!strcmp(argv[z], "--show-unused-images")) {
      show_unused_images = true;
    } else if (!strncmp(argv[z], "--erase-opacity=", 16)) {
      erase_opacity = strtoul(&argv[z][16], nullptr, 0);
    } else if (!strncmp(argv[z], "--tile-opacity=", 15)) {
      tile_opacity = strtoul(&argv[z][15], nullptr, 0);
    } else if (!strncmp(argv[z], "--object-opacity=", 17)) {
      object_opacity = strtoul(&argv[z][17], nullptr, 0);
    } else if (!image_saver.process_cli_arg(argv[z])) {
      fprintf(stderr, "invalid option: %s\n", argv[z]);
      print_usage();
      return 2;
    }
  }

  vector<ColorTableEntry> clut;
  if (!clut_filename.empty()) {
    string data = load_file(clut_filename);
    clut = ResourceFile::decode_clut(data.data(), data.size());
  }

  if (graphics_filename.empty()) {
    graphics_filename = clut.empty() ? "BW Graphics" : "Graphics";
  }

  const string levels_resource_filename = levels_filename + "/..namedfork/rsrc";
  ResourceFile levels(parse_resource_fork(load_file(levels_resource_filename.c_str())));

  auto graphics_rf = parse_resource_fork(load_file(graphics_filename + "/..namedfork/rsrc"));
  string graphics_df_contents = load_file(graphics_filename);
  // TODO: Support LEMMINGS_V2 here too. Does Oh No have the same level format?
  auto shapes = decode_SHPD_collection(
      graphics_rf, graphics_df_contents, clut,
      use_shpd_v2 ? SHPDVersion::LEMMINGS_V2 : SHPDVersion::LEMMINGS_V1);

  constexpr uint32_t level_resource_type = 0x4C45564C; // LEVL
  auto level_resources = levels.all_resources_of_type(level_resource_type);
  sort(level_resources.begin(), level_resources.end());

  vector<vector<LemmingsObjectDefinition>> object_defs_cache;
  unordered_set<string> used_erase_image_names;
  unordered_set<string> used_image_names;

  for (int16_t level_id : level_resources) {
    if (!target_levels.empty() && !target_levels.count(level_id)) {
      continue;
    }

    string level_data = levels.get_resource(level_resource_type, level_id)->data;
    if (level_data.size() != sizeof(LemmingsLevel)) {
      print_data(stderr, level_data);
      throw runtime_error(string_printf(
          "level data size is incorrect: expected %zu bytes, received %zu bytes",
          sizeof(LemmingsLevel), level_data.size()));
    }
    const auto* level = reinterpret_cast<const LemmingsLevel*>(level_data.data());
    if (level->ground_type > 5) {
      throw runtime_error("invalid ground type in level");
    }

    if (object_defs_cache.size() <= level->ground_type) {
      object_defs_cache.resize(level->ground_type + 1);
    }
    if (object_defs_cache[level->ground_type].empty()) {
      constexpr uint32_t object_def_resource_type = 0x4F424A44; // OBJD
      const string& data = levels.get_resource(object_def_resource_type, level->ground_type)->data;
      if (data.size() % sizeof(LemmingsObjectDefinition)) {
        throw runtime_error(string_printf(
            "object definition list size is incorrect: expected a multiple of %zu bytes, received %zu bytes",
            sizeof(LemmingsObjectDefinition), level_data.size()));
      }
      size_t count = data.size() / sizeof(LemmingsObjectDefinition);

      const auto* res_obj_defs = reinterpret_cast<const LemmingsObjectDefinition*>(data.data());
      vector<LemmingsObjectDefinition> obj_defs;
      while (obj_defs.size() < count) {
        obj_defs.emplace_back(res_obj_defs[obj_defs.size()]);
      }
      object_defs_cache[level->ground_type] = move(obj_defs);
    }
    const auto& obj_defs = object_defs_cache.at(level->ground_type);

    // Note: We use the alpha channel to denote what type of pixel each pixel is
    // during rendering (0x00 = nothing, 0xFF = tile, 0xE0 = object,
    // 0xD0 = annotation). Before saving the result, though, we delete the alpha
    // channel entirely.
    Image result(3168, 320, true);

    // Render special image, if one is given
    if (level->iff_number != 0) {
      string img_name = string_printf("%d_Special%d_0",
          1699 + level->iff_number, level->iff_number - 1);
      if (show_unused_images) {
        used_image_names.emplace(img_name);
      }
      auto img = shapes.at(img_name);
      result.blit(img.image, (result.get_width() - img.image.get_width()) / 2 - 16, 0,
          img.image.get_width(), img.image.get_height(), 0, 0);
    }

    // Render land ("tiles", though they're all different sizes/shapes)
    for (size_t z = 0; z < sizeof(level->tiles) / sizeof(level->tiles[0]); z++) {
      const auto& tile = level->tiles[z];
      if (tile.is_blank()) {
        continue;
      }

      try {
        string tile_name = string_printf("%d_Grounds%d_%hhu",
            level->ground_type + 1500, level->ground_type + 1, tile.type());

        ssize_t orig_tile_x = tile.x();
        ssize_t orig_tile_y = tile.y();

        if (show_unused_images) {
          if (tile.erase()) {
            used_erase_image_names.emplace(tile_name);
          } else {
            used_image_names.emplace(tile_name);
          }
        }
        const auto& tile_img = shapes.at(tile_name);
        Image reverse_tile_img;
        const Image* img_to_render = &tile_img.image;
        if (tile.vertical_reverse()) {
          reverse_tile_img = tile_img.image;
          reverse_tile_img.reverse_vertical();
          img_to_render = &reverse_tile_img;
        }

        // After this point, we're working in pixel coordinates, not level
        // coordinates. For the Mac version, this is simply a 2x scaling
        orig_tile_x *= 2;
        orig_tile_y *= 2;

        // It seems the y origin point is ignored if the vertical reverse flag
        // is set, but only in Lemmings (and not in Oh No).
        ssize_t tile_x = orig_tile_x + tile_img.origin_x;
        ssize_t tile_y = orig_tile_y +
            ((!use_shpd_v2 && tile.vertical_reverse()) ? 0 : tile_img.origin_y);

        if (tile.background()) {
          result.custom_blit(*img_to_render, tile_x, tile_y,
              img_to_render->get_width(), img_to_render->get_height(), 0, 0,
              [&](uint32_t& dc, uint32_t sc) -> void {
                if (((dc & 0x000000FF) == 0x00000000) && ((sc & 0x000000FF) != 0x00000000)) {
                  dc = alpha_blend(0x00000000, sc, tile_opacity);
                }
              });
        } else if (tile.erase()) {
          result.custom_blit(*img_to_render, tile_x, tile_y,
              img_to_render->get_width(), img_to_render->get_height(), 0, 0,
              [&](uint32_t& dc, uint32_t sc) -> void {
                if ((sc & 0x000000FF) != 0x00000000) {
                  dc = alpha_blend(dc, 0x00000000, erase_opacity);
                }
              });
        } else {
          result.custom_blit(*img_to_render, tile_x, tile_y,
              img_to_render->get_width(), img_to_render->get_height(), 0, 0,
              [&](uint32_t& dc, uint32_t sc) -> void {
                if ((sc & 0x000000FF) != 0x00000000) {
                  dc = alpha_blend(dc, (sc & 0xFFFFFF00) | 0x000000FF, tile_opacity);
                }
              });
        }

        if (show_tile_ids) {
          result.draw_text(tile_x, tile_y, 0x00FF00FF, 0x40404080,
              "%zu/%c%c%c", z, tile.background() ? 'b' : '-',
              tile.vertical_reverse() ? 'v' : '-', tile.erase() ? 'e' : '-');
        }

      } catch (const exception& e) {
        fprintf(stderr, "warning: cannot render tile %zu: %s\n", z, e.what());
      }
    }

    // Render objects
    for (size_t z = 0; z < sizeof(level->objects) / sizeof(level->objects[0]); z++) {
      const auto& obj = level->objects[z];
      if (obj.is_blank()) {
        continue;
      }

      const auto& def = obj_defs.at(obj.type());

      ssize_t img_x = obj.x() * 2;
      ssize_t img_y = obj.y() * 2;

      string img_name = string_printf("%d_Objects%d_%hu",
          level->ground_type + 1600, level->ground_type + 1, def.seq_base.load());
      bool image_valid = true;
      try {
        if (show_unused_images) {
          used_image_names.emplace(img_name);
        }
        const auto& img = shapes.at(img_name);
        img_x += img.origin_x;
        img_y += img.origin_y;

        auto draw_img_with_flags = [&](const Image& src, ssize_t x, ssize_t y) {
          if (obj.draw_only_on_tiles()) {
            result.custom_blit(src, x, y, src.get_width(), src.get_height(), 0, 0, 
                [&](uint32_t& dc, uint32_t sc) -> void {
                  if (((dc & 0x000000FF) == 0x000000FF) && ((sc & 0x000000FF) != 0x00000000)) {
                    dc = alpha_blend(dc, (sc & 0xFFFFFF00) | 0x000000E0, object_opacity);
                  }
                });
          } else if (obj.background()) {
            result.custom_blit(src, x, y, src.get_width(), src.get_height(), 0, 0,
                [&](uint32_t& dc, uint32_t sc) -> void {
                  if (((dc & 0x000000FF) == 0x00000000) && ((sc & 0x000000FF) != 0x00000000)) {
                    dc = alpha_blend(dc, (sc & 0xFFFFFF00) | 0x000000E0, object_opacity);
                  }
                });
          } else {
            result.custom_blit(src, x, y, src.get_width(), src.get_height(), 0, 0,
                [&](uint32_t& dc, uint32_t sc) -> void {
                  if ((sc & 0x000000FF) != 0x00000000) {
                    dc = alpha_blend(dc, (sc & 0xFFFFFF00) | 0x000000E0, object_opacity);
                  }
                });
          }
        };
        draw_img_with_flags(img.image, img_x, img_y);

        // It looks like this flag causes the deep-water image to render
        // immediately below the image
        if (def.flags & 0x0020) {
          string subimg_name = string_printf("%d_Objects%d_%d",
              level->ground_type + 1600, level->ground_type + 1,
              def.seq_base + def.seq_length);

          try {
            if (show_unused_images) {
              used_image_names.emplace(subimg_name);
            }
            const auto& subimg = shapes.at(subimg_name);
            ssize_t subimg_x = img_x;
            ssize_t subimg_y = img_y + img.image.get_height();
            draw_img_with_flags(subimg.image, subimg_x, subimg_y);
          } catch (const out_of_range&) {
            fprintf(stderr, "warning: missing object subimage %s\n", subimg_name.c_str());
            image_valid = false;
          }
        }

      } catch (const out_of_range&) {
        fprintf(stderr, "warning: missing object image %s\n", img_name.c_str());
        image_valid = false;
      }

      static const vector<uint32_t> collision_type_colors({
        0x00000000, // 0 = no collision
        0x00FF00FF, // 1 = level exit
        0xFF0000FF, // 2 = unused
        0xFF0000FF, // 3 = unused
        0x00FFFFFF, // 4 = trap
        0x00FFFFFF, // 5 = liquid
        0xFFFF00FF, // 6 = fire
        0x00000000, // 7 = left arrows (don't render a box)
        0x00000000, // 8 = right arrows (don't render a box)
        // Everything beyond 8 is unused, except for 11, which is used in one
        // object type in each level set which is never placed.
      });
      uint32_t box_color;
      if (def.collision_type >= collision_type_colors.size()) {
        box_color = 0xFF0000FF;
      } else {
        box_color = collision_type_colors[def.collision_type];
      }
      if (box_color) {
        ssize_t x1 = (obj.x() + def.x_offset * 4) * 2;
        ssize_t y1 = (obj.y() + def.y_offset * 4) * 2 - 16;
        ssize_t x2 = x1 + def.width * 8;
        ssize_t y2 = y1 + def.height * 8;
        result.draw_horizontal_line(x1, x2, y1, 3, box_color);
        result.draw_horizontal_line(x1, x2, y2, 3, box_color);
        result.draw_vertical_line(x1, y1, y2, 3, box_color);
        result.draw_vertical_line(x2, y1, y2, 3, box_color);
      }

      if (show_object_ids) {
        result.draw_text(img_x, img_y,
            image_valid ? 0xFFFF00FF : 0x000000FF,
            image_valid ? 0x40404080 : 0xFF0000FF,
            "%zu: %hd/%04hX/%hd/%hd", z, obj.type(), obj.data_flags.load(),
            def.x_offset.load(), def.y_offset.load());
      }
    }

    // Render collisions (steel) as orange dashed boxes
    for (size_t z = 0; z < sizeof(level->collisions) / sizeof(level->collisions[0]); z++) {
      const auto& coll = level->collisions[z];
      if (coll.is_blank()) {
        continue;
      }
      ssize_t x1 = coll.x() * 2;
      ssize_t y1 = coll.y() * 2;
      ssize_t x2 = x1 + coll.width() * 2;
      ssize_t y2 = y1 + coll.height() * 2;
      result.draw_horizontal_line(x1, x2, y1, 3, 0xFF0000D0);
      result.draw_horizontal_line(x1, x2, y2, 3, 0xFF0000D0);
      result.draw_vertical_line(x1, y1, y2, 3, 0xFF0000D0);
      result.draw_vertical_line(x2, y1, y2, 3, 0xFF0000D0);
    }

    result.set_has_alpha(false);

    string sanitized_name;
    for (ssize_t x = 0; x < level->name[0]; x++) {
      char ch = level->name[x + 1];
      if (ch > 0x20 && ch <= 0x7E) {
        sanitized_name.push_back(ch);
      } else {
        sanitized_name.push_back('_');
      }
    }

    string result_filename = string_printf("Lemmings_Level_%" PRId16 "_%s",
        level_id, sanitized_name.c_str());
    result_filename = image_saver.save_image(result, result_filename);
    fprintf(stderr, "... %s\n", result_filename.c_str());
  }

  if (show_unused_images) {
    for (const auto& it : shapes) {
      if (!used_image_names.count(it.first)) {
        if (used_erase_image_names.count(it.first)) {
          fprintf(stderr, "image used only as eraser: %s\n", it.first.c_str());
        } else {
          fprintf(stderr, "unused image: %s\n", it.first.c_str());
        }
      }
    }
  }

  return 0;
}
