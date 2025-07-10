#include <inttypes.h>
#include <stdlib.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <vector>

#include "ImageSaver.hh"
#include "IndexFormats/Formats.hh"
#include "ResourceFile.hh"

using namespace std;
using namespace phosg;
using namespace ResourceDASM;

struct MonkeyShinesRoom {
  struct EnemyEntry {
    be_uint16_t y_pixels;
    be_uint16_t x_pixels;
    be_int16_t y_min;
    be_int16_t x_min;
    be_int16_t y_max;
    be_int16_t x_max;
    be_int16_t y_speed; // in pixels per frame
    be_int16_t x_speed; // in pixels per frame
    be_int16_t type;
    be_uint16_t flags;

    // Sprite flags are:
    // - increasing frames or cycling frames
    // - slow animation
    // - two sets horizontal
    // - two sets vertical
    // - normal sprite, energy drainer, or door
  } __attribute__((packed));

  struct BonusEntry {
    be_uint16_t y_pixels;
    be_uint16_t x_pixels;
    be_int32_t unknown[3]; // these appear to be unused
    be_int16_t type;
    be_uint16_t id;
  } __attribute__((packed));

  be_uint16_t enemy_count;
  be_uint16_t bonus_count;
  EnemyEntry enemies[10];
  BonusEntry bonuses[25];
  be_uint16_t tile_ids[0x20 * 0x14]; // width=32, height=20
  be_uint16_t player_start_y; // unused except in rooms 1000 and 10000
  be_uint16_t player_start_x; // unused except in rooms 1000 and 10000
  be_uint16_t background_ppat_id;
} __attribute__((packed));

struct MonkeyShinesWorld {
  be_uint16_t num_exit_keys;
  be_uint16_t num_bonus_keys;
  be_uint16_t num_bonuses;
  be_int16_t exit_door_room;
  be_int16_t bonus_door_room;

  // Hazard types are:
  // 1 - burned
  // 2 - electrocuted
  // 3 - bee sting
  // 4 - fall
  // 5 - monster
  be_uint16_t hazard_types[16];
  uint8_t hazards_explode[16]; // really just an array of bools
  // Hazard death sounds are:
  // 12 - normal
  // 13 - death from long fall
  // 14 - death from bee sting
  // 15 - death from bomb
  // 16 - death by electrocution
  // 20 - death by lava
  be_uint16_t hazard_death_sounds[16];
  // Explosion sounds can be any of the above or 18 (bomb explosion)
  be_uint16_t hazard_explosion_sounds[16];
} __attribute__((packed));

vector<unordered_map<int16_t, pair<int16_t, int16_t>>> generate_room_placement_maps(
    const vector<int16_t>& room_ids) {
  unordered_set<int16_t> remaining_room_ids(room_ids.begin(), room_ids.end());

  // The basic idea is that when Bonzo moves right or left out of a room, the
  // room number is increased or decreased by 1; when he moves up or down out of
  // a room, it's increased or decreased by 100. There's no notion of rooms
  // linking to each other; links are stored implicitly by the room IDs
  // (resource IDs). To convert this format into something we can actually use,
  // we have to find all the connected components of this graph.

  // It occurs to me that this function might be a good basic algorithms
  // interview question.

  // This recursive lambda adds a single room to a placement map, then uses the
  // flood-fill algorithm to find all the rooms it's connected to. This
  // declaration looks super-gross because lambdas can't be recursive if you
  // declare them with auto. Sigh...
  function<void(unordered_map<int16_t, pair<int16_t, int16_t>>&, int16_t room_id,
      int16_t x_offset, int16_t y_offset)>
      process_room = [&](
                         unordered_map<int16_t, pair<int16_t, int16_t>>& ret, int16_t room_id,
                         int16_t x_offset, int16_t y_offset) {
        if (!remaining_room_ids.erase(room_id)) {
          return;
        }

        ret.emplace(room_id, make_pair(x_offset, y_offset));
        process_room(ret, room_id - 1, x_offset - 1, y_offset);
        process_room(ret, room_id + 1, x_offset + 1, y_offset);
        process_room(ret, room_id - 100, x_offset, y_offset - 1);
        process_room(ret, room_id + 100, x_offset, y_offset + 1);
      };

  // This function generates a placement map with nonnegative offsets that
  // contains the given room
  vector<unordered_map<int16_t, pair<int16_t, int16_t>>> ret;
  auto process_component = [&](int16_t start_room_id) {
    ret.emplace_back();
    auto& placement_map = ret.back();
    process_room(placement_map, start_room_id, 0, 0);
    if (placement_map.empty()) {
      ret.pop_back();
    } else {
      // Make all offsets nonnegative
      int16_t delta_x = 0, delta_y = 0;
      for (const auto& it : placement_map) {
        if (it.second.first < delta_x) {
          delta_x = it.second.first;
        }
        if (it.second.second < delta_y) {
          delta_y = it.second.second;
        }
      }
      for (auto& it : placement_map) {
        it.second.first -= delta_x;
        it.second.second -= delta_y;
      }
    }
  };

  // Start at room 1000 (for main level) and 10000 (for bonus level) and go
  // outward. Both of these start room IDs seem to be hardcoded
  process_component(1000);
  process_component(10000);

  // If there are any rooms left over, process them individually
  while (!remaining_room_ids.empty()) {
    size_t starting_size = remaining_room_ids.size();
    process_component(*remaining_room_ids.begin());
    if (remaining_room_ids.size() >= starting_size) {
      throw logic_error("did not make progress generating room placement maps");
    }
  }

  return ret;
}

void print_usage() {
  fwrite_fmt(stderr, "\
Usage: mshines_render [options] input_filename [output_prefix]\n\
\n" IMAGE_SAVER_HELP);
}

int main(int argc, char** argv) {
  ImageSaver image_saver;
  string filename;
  string out_prefix;

  for (int x = 1; x < argc; x++) {
    if (image_saver.process_cli_arg(argv[x])) {
      // Nothing
    } else if (filename.empty()) {
      filename = argv[x];
    } else if (out_prefix.empty()) {
      out_prefix = argv[x];
    } else {
      fwrite_fmt(stderr, "excess argument: {}\n", argv[x]);
      print_usage();
      return 2;
    }
  }
  if (filename.empty()) {
    print_usage();
    return 2;
  }

  if (out_prefix.empty())
    out_prefix = filename;

  ResourceFile rf(parse_resource_fork(load_file(filename + "/..namedfork/rsrc")));
  const uint32_t room_type = 0x506C766C; // Plvl
  auto room_resource_ids = rf.all_resources_of_type(room_type);
  auto sprites_pict = rf.decode_PICT(130); // hardcoded ID for all worlds
  auto& sprites = sprites_pict.image;

  // Assemble index for animated sprites
  unordered_map<int16_t, pair<shared_ptr<const ImageRGBA8888N>, size_t>> enemy_image_locations;
  {
    size_t next_type_id = 0;
    for (int16_t id = 1000;; id++) {
      if (!rf.resource_exists(RESOURCE_TYPE_PICT, id)) {
        break;
      }
      auto pict = rf.decode_PICT(id);
      auto img = make_shared<ImageRGBA8888N>(std::move(pict.image));
      for (size_t z = 0; z < img->get_height(); z += 80) {
        enemy_image_locations.emplace(next_type_id, make_pair(img, z));
        next_type_id++;
      }
    }
  }

  // Decode the default ppat (we'll use it if a room references a missing ppat,
  // which apparently happens quite a lot - it looks like the ppat id field used
  // to be the room id field and they just never updated it after implementing
  // the custom backgrounds feature)
  unordered_map<int16_t, const ImageRGB888> background_ppat_cache;
  auto emplace_ret = background_ppat_cache.emplace(1000, rf.decode_ppat(1000).pattern);
  const ImageRGB888* default_background_ppat = &emplace_ret.first->second;

  size_t component_number = 0;
  auto placement_maps = generate_room_placement_maps(room_resource_ids);
  for (const auto& placement_map : placement_maps) {
    // First figure out the width and height of this component
    uint16_t w_rooms = 0, h_rooms = 0;
    bool component_contains_start = false, component_contains_bonus_start = false;
    for (const auto& it : placement_map) {
      if (it.second.first >= w_rooms) {
        w_rooms = it.second.first + 1;
      }
      if (it.second.second >= h_rooms) {
        h_rooms = it.second.second + 1;
      }
      if (it.first == 1000) {
        component_contains_start = true;
      } else if (it.first == 10000) {
        component_contains_bonus_start = true;
      }
    }

    // Then render the rooms
    ImageRGB888 result(20 * 32 * w_rooms, 20 * 20 * h_rooms);
    result.clear(0x202020FF);
    for (auto it : placement_map) {
      int16_t room_id = it.first;
      size_t room_x = it.second.first;
      size_t room_y = it.second.second;
      size_t room_px = 20 * 32 * room_x;
      size_t room_py = 20 * 20 * room_y;

      string room_data = rf.get_resource(room_type, room_id)->data;
      if (room_data.size() != sizeof(MonkeyShinesRoom)) {
        fwrite_fmt(stderr, "warning: room 0x{:04X} is not the correct size (expected {} bytes, got {} bytes)\n",
            room_id, sizeof(MonkeyShinesRoom), room_data.size());
        result.write_rect(room_px, room_py, 32 * 20, 20 * 20, 0xFF00FFFF);
        continue;
      }

      const auto* room = reinterpret_cast<const MonkeyShinesRoom*>(room_data.data());

      // Render the appropriate ppat in the background of every room. We don't
      // use ImageWithoutAlpha::copy_from() here just in case the room dimensions
      // aren't a multiple of the ppat dimensions
      const ImageRGB888* background_ppat = nullptr;
      try {
        background_ppat = &background_ppat_cache.at(room->background_ppat_id);
      } catch (const out_of_range&) {
        try {
          auto ppat_id = room->background_ppat_id;
          auto emplace_ret = background_ppat_cache.emplace(
              ppat_id, rf.decode_ppat(room->background_ppat_id).pattern);
          background_ppat = &emplace_ret.first->second;
        } catch (const exception& e) {
          fwrite_fmt(stderr, "warning: room {} uses ppat {} but it can\'t be decoded ({})\n",
              room_id, room->background_ppat_id, e.what());
          background_ppat = default_background_ppat;
        }
      }

      if (background_ppat) {
        for (size_t y = 0; y < 400; y++) {
          for (size_t x = 0; x < 640; x++) {
            uint32_t c = background_ppat->read(x % background_ppat->get_width(), y % background_ppat->get_height());
            result.write(room_px + x, room_py + y, c);
          }
        }

      } else {
        result.write_rect(room_px, room_py, 640, 400, 0xFF00FFFF);
      }

      // Render tiles. Each tile is 20x20
      for (size_t y = 0; y < 20; y++) {
        for (size_t x = 0; x < 32; x++) {

          // Looks like there are 21 rows of sprites in PICT 130, with 16 on
          // each row

          uint16_t tile_id = room->tile_ids[x * 20 + y];
          if (tile_id == 0) {
            continue;
          }
          tile_id--;

          size_t tile_x = 0xFFFFFFFF;
          size_t tile_y = 0xFFFFFFFF;
          if (tile_id < 0x90) { // <0x20: walls, <0x50: jump-through platforms, <0x90: scenery
            tile_x = tile_id % 16;
            tile_y = tile_id / 16;
          } else if (tile_id < 0xA0) { // 2-frame animated tiles
            tile_x = tile_id & 0x0F;
            tile_y = 11;
          } else if (tile_id < 0xB0) { // rollers (usually)
            tile_x = tile_id & 0x0F;
            tile_y = 15;
          } else if (tile_id < 0xB2) { // collapsing floor
            tile_x = 0;
            tile_y = 17 + (tile_id & 1);
          } else if (tile_id < 0xC0) { // 2-frame animated tiles
            tile_x = tile_id & 0x0F;
            tile_y = 11;
          } else if (tile_id < 0xD0) { // 2-frame animated tiles
            tile_x = tile_id & 0x0F;
            tile_y = 13;
          } else if (tile_id < 0xF0) { // more scenery
            tile_x = tile_id & 0x0F;
            tile_y = (tile_id - 0x40) / 16;
          }
          // TODO: there may be more cases than the above; figure them out

          if (tile_x == 0xFFFFFFFF || tile_y == 0xFFFFFFFF) {
            result.write_rect(room_px + x * 20, room_py + y * 20, 20, 20, 0xFF00FFFF);
            fwrite_fmt(stderr, "warning: no known tile for {:02X} (room {}, x={}, y={})\n", tile_id, room_id, x, y);
          } else {
            for (size_t py = 0; py < 20; py++) {
              for (size_t px = 0; px < 20; px++) {
                if (sprites.read(tile_x * 20 + px, tile_y * 40 + py + 20) & 0xFFFFFF00) {
                  result.write(room_px + x * 20 + px, room_py + y * 20 + py, sprites.read(tile_x * 20 + px, tile_y * 40 + py));
                }
              }
            }
          }
        }
      }

      // Render enemies
      for (size_t z = 0; z < room->enemy_count; z++) {
        // It looks like the y coords are off by 80 pixels because of the HUD,
        // which renders at the top. High-quality engineering!
        size_t enemy_px = room_px + room->enemies[z].x_pixels;
        size_t enemy_py = room_py + room->enemies[z].y_pixels - 80;
        try {
          const auto& image_loc = enemy_image_locations.at(room->enemies[z].type);
          const auto& enemy_pict = image_loc.first;
          size_t enemy_pict_py = image_loc.second;
          for (size_t py = 0; py < 40; py++) {
            for (size_t px = 0; px < 40; px++) {
              uint32_t c = enemy_pict->read(px, enemy_pict_py + py);
              uint32_t mc = enemy_pict->read(px, enemy_pict_py + py + 40);
              uint32_t ec = result.read(enemy_px + px, enemy_py + py);
              result.write(enemy_px + px, enemy_py + py, (c & mc) | (ec & ~mc));
            }
          }
        } catch (const out_of_range&) {
          result.write_rect(enemy_px, enemy_px, 20, 20, 0xFF8000FF);
          result.draw_text(enemy_px, enemy_px, 0x000000FF, "{:04X}", room->enemies[z].type);
        }

        // Draw a bounding box to show where its range of motion is
        size_t x_min = room->enemies[z].x_speed ? room->enemies[z].x_min : room->enemies[z].x_pixels;
        size_t x_max = (room->enemies[z].x_speed ? room->enemies[z].x_max : room->enemies[z].x_pixels) + 39;
        size_t y_min = (room->enemies[z].y_speed ? room->enemies[z].y_min : room->enemies[z].y_pixels) - 80;
        size_t y_max = (room->enemies[z].y_speed ? room->enemies[z].y_max : room->enemies[z].y_pixels) + 39 - 80;
        result.draw_horizontal_line(room_px + x_min, room_px + x_max,
            room_py + y_min, 0, 0xFF8000FF);
        result.draw_horizontal_line(room_px + x_min, room_px + x_max,
            room_py + y_max, 0, 0xFF8000FF);
        result.draw_vertical_line(room_px + x_min, room_py + y_min,
            room_py + y_max, 0, 0xFF8000FF);
        result.draw_vertical_line(room_px + x_max, room_py + y_min,
            room_py + y_max, 0, 0xFF8000FF);

        // Draw its initial velocity as a line from the center
        if (room->enemies[z].x_speed || room->enemies[z].y_speed) {
          result.write_rect(enemy_px + 19, enemy_py + 19, 3, 3, 0xFF8000FF);
          result.draw_line(enemy_px + 20, enemy_py + 20,
              enemy_px + 20 + room->enemies[z].x_speed * 10,
              enemy_py + 20 + room->enemies[z].y_speed * 10, 0xFF8000FF);
        }
      }

      // Annotate bonuses with ids
      for (size_t z = 0; z < room->bonus_count; z++) {
        const auto& bonus = room->bonuses[z];
        result.draw_text(room_px + bonus.x_pixels, room_py + bonus.y_pixels - 80, 0xFFFFFFFF, "{:02X}", bonus.id);
      }

      // If this is a starting room, mark the player start location with an
      // arrow and the label "START"
      if (room_id == 1000 || room_id == 10000) {
        size_t x_min = room->player_start_x;
        size_t x_max = room->player_start_x + 39;
        size_t y_min = room->player_start_y - 80;
        size_t y_max = room->player_start_y + 39 - 80;
        result.draw_horizontal_line(room_px + x_min, room_px + x_max, room_py + y_min, 0, 0x00FF80FF);
        result.draw_horizontal_line(room_px + x_min, room_px + x_max, room_py + y_max, 0, 0x00FF80FF);
        result.draw_vertical_line(room_px + x_min, room_py + y_min, room_py + y_max, 0, 0x00FF80FF);
        result.draw_vertical_line(room_px + x_max, room_py + y_min, room_py + y_max, 0, 0x00FF80FF);
        result.draw_text(room_px + x_min + 2, room_py + y_min + 2, 0xFFFFFFFF, 0x00000080, "START");
      }

      result.draw_text(room_px + 2, room_py + 2, 0xFFFFFFFF, 0x00000080, "Room {}", room_id);
    }

    string result_filename;
    if (component_contains_start && component_contains_bonus_start) {
      result_filename = out_prefix + "_world_and_bonus";
    } else if (component_contains_start) {
      result_filename = out_prefix + "_world";
    } else if (component_contains_bonus_start) {
      result_filename = out_prefix + "_bonus";
    } else {
      result_filename = std::format("{}_{}", out_prefix,
          component_number);
      component_number++;
    }
    result_filename = image_saver.save_image(result, result_filename);
    fwrite_fmt(stderr, "... {}\n", result_filename);
  }

  return 0;
}
