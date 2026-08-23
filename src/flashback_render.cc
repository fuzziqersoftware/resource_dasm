#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <format>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <vector>

#include "DataCodecs/Codecs.hh"
#include "ImageSaver.hh"
#include "IndexFormats/Formats.hh"
#include "ResourceFile.hh"
#include "SpriteDecoders/Decoders.hh"

struct LevelMap {
  struct RoomCollision {
    enum class Type : uint8_t {
      NONE = 0x00,
      SOLID = 0x01,
      TOP_HALF = 0x02,
    };
    std::array<Type, 16> ceiling_tiles;
    struct Floor {
      std::array<Type, 16> spaces;
      std::array<Type, 16> tiles;
    } __attribute__((packed));
    std::array<Floor, 3> floors;
  } __attribute__((packed));

  /* 0000 */ std::array<uint8_t, 0x40> above_links;
  /* 0040 */ std::array<uint8_t, 0x40> below_links;
  /* 0080 */ std::array<uint8_t, 0x40> right_links;
  /* 00C0 */ std::array<uint8_t, 0x40> left_links;
  /* 0100 */ std::array<RoomCollision, 0x40> rooms;
  /* 1D00 */
} __attribute__((packed));

// TODO: RE this format. See notes in notes/flashback-objects.txt
// struct ObjectDefinition {
//   // The objects resource (OBJS) is a phosg::be_uint16_t specifying how many objects there are, then that many entries
//   // of this format
//   /* 20 */
// } __attribute__((packed));

struct LevelDefinition {
  int16_t lmap_res_id;
  int16_t cond_res_id;
  int16_t objd_res_id;
  int16_t strs_res_id;
  int16_t anim_res_id;
  int16_t base_room_ppss_id;
  int16_t objects_ppss_id;
  std::array<int16_t, 0x10> clut_segments;
  std::unordered_map<size_t, std::array<int16_t, 0x10>> clut_segments_overrides;
};

std::array<LevelDefinition, 7> level_defs{
    LevelDefinition{0x44C, 0x44C, 0x44C, 0x44C, 0x44C, 0x3E8, 0x7D0,
        {0x18, 0x19, 0x1A, 0x1B, 0x30, 0x00, 0x00, 0x00, 0x18, 0x19, 0x1A, 0x1B, 0x37, 0x38, 0x00, 0x00},
        {
            {0x1B, {0x20, 0x21, 0x22, 0x23, 0x31, 0x00, 0x00, 0x00, 0x20, 0x21, 0x22, 0x23, 0x37, 0x38, 0x00, 0x00}},
            {0x35, {0x1C, 0x1D, 0x1E, 0x1F, 0x30, 0x00, 0x00, 0x00, 0x1C, 0x1D, 0x1E, 0x1F, 0x37, 0x38, 0x00, 0x00}},
        }},
    LevelDefinition{0x4B0, 0x4B0, 0x4B0, 0x4B0, 0x4B0, 0x44C, 0x834,
        {0x1C, 0x1D, 0x1E, 0x1F, 0x30, 0x00, 0x00, 0x00, 0x1C, 0x1D, 0x1E, 0x1F, 0x37, 0x38, 0x00, 0x00}, {}},
    LevelDefinition{0x514, 0x514, 0x514, 0x514, 0x514, 0x4B0, 0x898,
        {0x24, 0x25, 0x26, 0x27, 0x30, 0x00, 0x00, 0x00, 0x24, 0x25, 0x26, 0x27, 0x37, 0x38, 0x00, 0x00}, {}},
    LevelDefinition{0x578, 0x582, 0x582, 0x582, 0x582, 0x514, 0x8FC,
        {0x28, 0x29, 0x2A, 0x2B, 0x30, 0x00, 0x00, 0x00, 0x28, 0x29, 0x2A, 0x2B, 0x37, 0x38, 0x00, 0x00}, {}},
    LevelDefinition{0x578, 0x58C, 0x58C, 0x58C, 0x58C, 0x514, 0x8FC,
        {0x28, 0x29, 0x2A, 0x2B, 0x30, 0x00, 0x00, 0x00, 0x28, 0x29, 0x2A, 0x2B, 0x37, 0x38, 0x00, 0x00}, {}},
    LevelDefinition{0x5DC, 0x5E6, 0x5E6, 0x5E6, 0x5E6, 0x578, 0x960,
        {0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x00, 0x00, 0x00, 0x2C, 0x2D, 0x2E, 0x2F, 0x37, 0x38, 0x00, 0x00}, {}},
    LevelDefinition{0x5DC, 0x5F0, 0x5F0, 0x5F0, 0x5F0, 0x578, 0x960,
        {0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x00, 0x00, 0x00, 0x2C, 0x2D, 0x2E, 0x2F, 0x37, 0x38, 0x00, 0x00}, {}},
};

void print_usage() {
  phosg::fwrite_fmt(stderr, "\
Usage: flashback_render FLASHBACK-PATH\n\
\n\
Options:\n\
  --clut-file=FILE\n\
      Use this color table (required). You can use a .bin file produced by\n\
      resource_dasm here.\n\n" IMAGE_SAVER_HELP);
}

int main(int argc, char** argv) {
  if (argc != 2) {
    throw std::runtime_error("Path to Flashback program is required");
  }

  auto rf = ResourceDASM::parse_resource_fork(phosg::load_file(std::format("{}/..namedfork/rsrc", argv[1])));
  auto base_clut = rf.decode_clut(1000);

  constexpr uint32_t lmap_type = 0x4C4D4150; // 'LMAP'
  constexpr uint32_t ppss_type = 0x50505353; // 'PPSS'

  for (size_t level_num = 0; level_num < level_defs.size(); level_num++) {
    phosg::log_info_f("(Level {}) Loading definition", level_num);

    const auto& level_def = level_defs[level_num];
    auto lmap_res = rf.get_resource(lmap_type, level_def.lmap_res_id);
    auto lmap_data = ResourceDASM::decompress_presage_lzss(lmap_res->data);
    if (lmap_data.size() != sizeof(LevelMap)) {
      throw std::runtime_error(std::format("LMAP:{} size is incorrect", level_def.lmap_res_id));
    }
    const auto& lmap = *reinterpret_cast<const LevelMap*>(lmap_data.data());

    // Place all the rooms on a grid by flood-filling from the first one. Then place any remaining rooms in separate
    // components as needed
    struct Placement {
      uint8_t component_index = 0; // 0 = unplaced
      int8_t x = 0;
      int8_t y = 0;
    };
    std::array<Placement, 0x40> room_placements;
    struct ComponentDimensions {
      int8_t xmin = 0;
      int8_t xmax = 0; // Inclusive
      int8_t ymin = 0;
      int8_t ymax = 0; // Inclusive

      void add(int8_t x, int8_t y) {
        this->xmin = std::min<int8_t>(this->xmin, x);
        this->xmax = std::max<int8_t>(this->xmax, x);
        this->ymin = std::min<int8_t>(this->ymin, y);
        this->ymax = std::max<int8_t>(this->ymax, y);
      }
    };
    std::vector<ComponentDimensions> component_dimensions{ComponentDimensions{}};

    phosg::log_info_f("(Level {}) Placing rooms", level_num + 1);

    uint8_t current_component_index = 0;
    for (uint8_t start_room_id = 0; start_room_id < 0x40; start_room_id++) {
      if (room_placements[start_room_id].component_index != 0) {
        continue; // Already placed by flood-fill from a previous room
      }
      room_placements[start_room_id].component_index = ++current_component_index;

      auto& current_component_dimensions = component_dimensions.emplace_back();
      std::deque<uint8_t> flood_queue{start_room_id};
      while (!flood_queue.empty()) {
        uint8_t current_room_id = flood_queue.front();
        flood_queue.pop_front();

        const auto& from_placement = room_placements[current_room_id];

        auto place_or_expect_match = [&](uint8_t to_room_id, int8_t x, int8_t y) -> void {
          if (to_room_id != 0xFF) {
            auto& to_placement = room_placements.at(to_room_id);
            if (to_placement.component_index == 0) {
              to_placement.component_index = current_component_index;
              to_placement.x = x;
              to_placement.y = y;
              flood_queue.emplace_back(to_room_id);
              current_component_dimensions.add(x, y);
            }
          }
        };

        place_or_expect_match(lmap.left_links[current_room_id], from_placement.x - 1, from_placement.y);
        place_or_expect_match(lmap.right_links[current_room_id], from_placement.x + 1, from_placement.y);
        place_or_expect_match(lmap.above_links[current_room_id], from_placement.x, from_placement.y - 1);
        place_or_expect_match(lmap.below_links[current_room_id], from_placement.x, from_placement.y + 1);
      }
    }

    // Set up the color table
    std::vector<ResourceDASM::ColorTableEntry> clut;
    clut.resize(0x100);
    for (size_t clut_segment_index = 0; clut_segment_index < 0x10; clut_segment_index++) {
      size_t base_clut_index = level_def.clut_segments[clut_segment_index] << 4;
      size_t dest_clut_index = clut_segment_index << 4;
      for (size_t z = 0; z < 0x10; z++) {
        clut[dest_clut_index | z] = base_clut[base_clut_index | z];
      }
    }

    // Render all the components
    constexpr size_t room_w = 512;
    constexpr size_t room_h = 448;
    std::vector<phosg::ImageRGBA8888N> component_maps;
    component_maps.emplace_back();
    component_maps.reserve(component_dimensions.size());
    phosg::log_info_f("(Level {}) Creating {} component maps", level_num + 1, component_dimensions.size());
    for (size_t component_index = 1; component_index < component_dimensions.size(); component_index++) {
      const auto& dims = component_dimensions[component_index];
      component_maps.emplace_back((dims.xmax - dims.xmin + 1) * room_w, (dims.ymax - dims.ymin + 1) * room_h);
    }
    for (size_t room_id = 0; room_id < 0x40; room_id++) {
      const auto& placement = room_placements[room_id];
      if (placement.component_index == 0) {
        throw std::logic_error("Room was not placed");
      }
      phosg::log_info_f("(Level {}) Rendering room {} (component {})", level_num + 1, room_id, placement.component_index);
      const auto& dims = component_dimensions[placement.component_index];
      auto room_map = component_maps.at(placement.component_index).view((placement.x - dims.xmin) * room_w, (placement.y - dims.ymin) * room_h, room_w, room_h);

      int16_t ppss_id = level_def.base_room_ppss_id + room_id;
      uint32_t collision_alpha = 0x40; // TODO: Make this configurable
      if (rf.resource_exists(ppss_type, ppss_id)) {
        auto ppss_res = rf.get_resource(ppss_type, ppss_id);
        auto ppss_contents = ResourceDASM::decode_PPSS(ppss_res->data, clut);
        if (!ppss_contents.contains(0)) {
          throw std::runtime_error("Room PPSS does not contain image 0");
        }
        room_map.copy_from(ppss_contents[0], 0, 0, room_w, room_h, 0, 0);
      } else {
        room_map.write_rect(0, 0, room_w, room_h, 0x000000FF);
        collision_alpha = 0xFF;
      }

      const auto& coll = lmap.rooms[room_id];
      for (size_t x = 0; x < 16; x++) {
        // Cells are 32px wide and 144px tall; floor is the bottom 16 px of the cell
        if (coll.ceiling_tiles[x] == LevelMap::RoomCollision::Type::SOLID) {
          room_map.blend_rect(x * 32, 0, 32, 16, 0xFFFFFF00 | collision_alpha);
        } else if (coll.ceiling_tiles[x] != LevelMap::RoomCollision::Type::NONE) {
          room_map.blend_rect(x * 32, 0, 32, 16, 0xFF000000 | collision_alpha);
          room_map.draw_text(x * 32, 8, 0xFFFFFFFF, 0xFF000000 | collision_alpha, "CL{:02X}", static_cast<uint8_t>(coll.ceiling_tiles[x]));
        }

        for (size_t floor = 0; floor < 3; floor++) {
          const auto& floor_tiles = coll.floors[floor];
          if (floor_tiles.spaces[x] == LevelMap::RoomCollision::Type::SOLID) {
            room_map.blend_rect(x * 32, 16 + (floor * 144), 32, 128, 0xFFFFFF00 | collision_alpha);
          } else if (floor_tiles.spaces[x] == LevelMap::RoomCollision::Type::TOP_HALF) {
            room_map.blend_rect(x * 32, 16 + (floor * 144), 32, 80, 0xFFFFFF00 | collision_alpha);
          } else if (floor_tiles.spaces[x] != LevelMap::RoomCollision::Type::NONE) {
            room_map.blend_rect(x * 32, 16 + (floor * 144), 32, 128, 0xFF000000 | collision_alpha);
            room_map.draw_text(x * 32, (144 * (floor + 1)) - 72, 0x000000FF, "SP{:02X}", static_cast<uint8_t>(floor_tiles.spaces[x]));
          }
          if (floor_tiles.tiles[x] == LevelMap::RoomCollision::Type::SOLID) {
            room_map.blend_rect(x * 32, 144 + (floor * 144), 32, 16, 0xFFFFFF00 | collision_alpha);
          } else if (floor_tiles.tiles[x] != LevelMap::RoomCollision::Type::NONE) {
            room_map.blend_rect(x * 32, 144 + (floor * 144), 32, 16, 0xFF000000 | collision_alpha);
            room_map.draw_text(x * 32, 144 + (floor * 144) - 16, 0x000000FF, "TL{:02X}", static_cast<uint8_t>(floor_tiles.tiles[x]));
          }
        }
      }

      room_map.draw_text(1, 1, 0xFFFFFFFF, 0x00000040, "Room {}", room_id);
    }

    // Save all the components
    for (size_t z = 1; z < component_maps.size(); z++) {
      std::string filename = std::format("flashback_level{}_component{}.bmp", level_num + 1, z);
      phosg::save_file(filename, component_maps[z].serialize(phosg::ImageFormat::WINDOWS_BITMAP));
      phosg::log_info_f("(Level {}) ... {}", level_num + 1, filename);
    }
  }

  return 0;
}
