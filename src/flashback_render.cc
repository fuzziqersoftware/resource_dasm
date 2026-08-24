#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <format>
#include <phosg/Arguments.hh>
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

struct ObjectDefinition {
  // The objects resource (OBJD) is a phosg::be_uint16_t specifying how many objects there are, then that many entries
  // of this format
  // TODO: RE the rest of this format. See notes in notes/flashback-objects.txt
  /* 00 */ phosg::be_int16_t anim_entry_index;
  // x and y are both multiplied by 2 to get the render coordinates. Objects are anchored at the lower left corner
  // (kind of - the actual anchor is (x_div2 * 2 + 16, y_div2 * 2 + 4)).
  /* 02 */ phosg::be_int16_t x_div2;
  /* 04 */ phosg::be_int16_t y_div2;
  /* 06 */ phosg::be_int16_t unknown_a2;
  /* 08 */ phosg::be_int16_t unknown_a3;
  /* 0A */ phosg::be_int16_t unknown_a4;
  /* 0C */ phosg::be_int16_t unknown_a5;
  /* 0E */ phosg::be_int16_t unknown_a6;
  /* 10 */ phosg::be_int16_t unknown_a7;
  /* 12 */ uint8_t unknown_a8;
  /* 13 */ uint8_t room_id;
  /* 14 */ uint8_t unknown_a9;
  /* 15 */ uint8_t flags;
  /* 16 */ uint8_t unknown_a10;
  /* 17 */ uint8_t unknown_a11;
  /* 18 */ uint8_t unknown_a12;
  /* 19 */ uint8_t unknown_a13;
  /* 1A */ uint8_t unknown_a14;
  /* 1B */ uint8_t flags2;
  /* 1C */ uint8_t unknown_a15;
  /* 1D */ uint8_t unknown_a16;
  /* 1E */ phosg::be_int16_t unknown_a17;
  /* 20 */
} __attribute__((packed));

struct AnimationDefinition {
  // Parsed representation of an ANIM entry
  struct Frame {
    uint16_t image_spec;
    uint16_t unknown_a3;
  };
  int16_t unknown_a1;
  int16_t unknown_a2;
  std::vector<Frame> frames;

  static std::vector<AnimationDefinition> parse_anim(const std::string& data) {
    phosg::StringReader r(data);
    size_t num_animations = r.get_u16b();
    std::vector<AnimationDefinition> ret;
    while (ret.size() < num_animations) {
      auto& def = ret.emplace_back();
      size_t offset = r.get_u16b();
      if (offset > 0) {
        auto entry_r = r.sub(offset);
        size_t num_frames = entry_r.get_u16b();
        def.unknown_a1 = entry_r.get_s16b();
        def.unknown_a2 = entry_r.get_s16b();
        while (def.frames.size() < num_frames) {
          auto& frame = def.frames.emplace_back();
          frame.image_spec = entry_r.get_u16b();
          frame.unknown_a3 = entry_r.get_u16b();
        }
      }
    }
    return ret;
  }
};

struct LevelDefinition {
  int16_t lmap_res_id;
  int16_t cond_res_id;
  int16_t objd_res_id;
  int16_t strs_res_id;
  int16_t anim_res_id;
  int16_t base_room_ppss_id;
  int16_t objects_ppss_id;
  std::array<uint8_t, 0x10> clut_segments;
  std::unordered_map<size_t, std::array<uint8_t, 0x10>> clut_segments_overrides;
  std::unordered_set<size_t> break_left_room_links;
  std::unordered_set<size_t> break_right_room_links;
  std::unordered_set<size_t> break_above_room_links;
  std::unordered_set<size_t> break_below_room_links;
};

std::array<LevelDefinition, 7> level_defs{
    LevelDefinition{0x44C, 0x44C, 0x44C, 0x44C, 0x44C, 0x3E8, 0x7D0,
        {0x18, 0x19, 0x1A, 0x1B, 0x30, 0xFF, 0xFF, 0xFF, 0x18, 0x19, 0x1A, 0x1B, 0x37, 0x38, 0xFF, 0xFF}, {}, {}, {}, {}, {}},
    LevelDefinition{0x4B0, 0x4B0, 0x4B0, 0x4B0, 0x4B0, 0x44C, 0x834,
        {0x1C, 0x1D, 0x1E, 0x1F, 0x30, 0xFF, 0xFF, 0xFF, 0x1C, 0x1D, 0x1E, 0x1F, 0x37, 0x38, 0xFF, 0xFF},
        {{0x1B, {0x20, 0x21, 0x22, 0x23, 0x31, 0xFF, 0xFF, 0xFF, 0x20, 0x21, 0x22, 0x23, 0x37, 0x38, 0xFF, 0xFF}}},
        {0x00, 0x0D, 0x26, 0x33}, {0x00, 0x0D, 0x26, 0x33}, {}, {}},
    LevelDefinition{0x514, 0x514, 0x514, 0x514, 0x514, 0x4B0, 0x898,
        {0x24, 0x25, 0x26, 0x27, 0x30, 0xFF, 0xFF, 0xFF, 0x24, 0x25, 0x26, 0x27, 0x37, 0x38, 0xFF, 0xFF}, {}, {}, {}, {}, {}},
    LevelDefinition{0x578, 0x582, 0x582, 0x582, 0x582, 0x514, 0x8FC,
        {0x28, 0x29, 0x2A, 0x2B, 0x30, 0xFF, 0xFF, 0xFF, 0x28, 0x29, 0x2A, 0x2B, 0x37, 0x38, 0xFF, 0xFF}, {}, {}, {}, {}, {}},
    LevelDefinition{0x578, 0x58C, 0x58C, 0x58C, 0x58C, 0x514, 0x8FC,
        {0x28, 0x29, 0x2A, 0x2B, 0x30, 0xFF, 0xFF, 0xFF, 0x28, 0x29, 0x2A, 0x2B, 0x37, 0x38, 0xFF, 0xFF}, {}, {}, {}, {}, {}},
    LevelDefinition{0x5DC, 0x5E6, 0x5E6, 0x5E6, 0x5E6, 0x578, 0x960,
        {0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0xFF, 0xFF, 0xFF, 0x2C, 0x2D, 0x2E, 0x2F, 0x37, 0x38, 0xFF, 0xFF}, {},
        {}, {}, {0x30}, {0x20}},
    LevelDefinition{0x5DC, 0x5F0, 0x5F0, 0x5F0, 0x5F0, 0x578, 0x960,
        {0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0xFF, 0xFF, 0xFF, 0x2C, 0x2D, 0x2E, 0x2F, 0x37, 0x38, 0xFF, 0xFF}, {},
        {}, {}, {0x30}, {0x20}},
};

enum class Direction {
  LEFT = 0,
  RIGHT,
  ABOVE,
  BELOW,
};
std::array<Direction, 4> ALL_DIRECTIONS = {Direction::LEFT, Direction::RIGHT, Direction::ABOVE, Direction::BELOW};

static int8_t x_delta_for_direction(Direction dir) {
  switch (dir) {
    case Direction::LEFT:
      return -1;
    case Direction::RIGHT:
      return 1;
    case Direction::ABOVE:
    case Direction::BELOW:
      return 0;
    default:
      throw std::logic_error("Invalid direction");
  }
}
static int8_t y_delta_for_direction(Direction dir) {
  switch (dir) {
    case Direction::LEFT:
    case Direction::RIGHT:
      return 0;
    case Direction::ABOVE:
      return -1;
    case Direction::BELOW:
      return 1;
    default:
      throw std::logic_error("Invalid direction");
  }
}

int main(int argc, char** argv) {
  phosg::Arguments args(argv + 1, argc - 1);
  if (args.get<bool>("help")) {
    phosg::fwrite_fmt(stderr, "\
Usage: flashback_render FLASHBACK-PATH\n\
\n\
Options:\n\
  --hide-object-annotations\n\
      Remove annotations around animated objects.\n\
  --collision-alpha=ALPHA\n\
      Render collision overlay with this opacity (0-255).\n\
\n");
    return 0;
  }
  const std::string& flashback_path = args.get<std::string>(0);
  bool show_object_annotations = !args.get<bool>("hide-object-annotations");
  uint32_t default_collision_alpha = args.get<uint8_t>("collision-alpha", 0);

  auto rf = ResourceDASM::parse_resource_fork(phosg::load_file(std::format("{}/..namedfork/rsrc", flashback_path)));
  auto base_clut = rf.decode_clut(1000);

  auto build_clut_from_segments = [&](const std::array<uint8_t, 0x10>& segments) -> std::vector<ResourceDASM::ColorTableEntry> {
    std::vector<ResourceDASM::ColorTableEntry> ret;
    ret.reserve(0x100);
    for (size_t z = 0; z < 0x10; z++) {
      if (segments[z] == 0xFF) {
        for (size_t z = 0; z < 0x10; z++) {
          ret.emplace_back(ResourceDASM::ColorTableEntry{0x0000, {0x0000, 0x0000, 0x0000}});
        }
      } else {
        size_t base_clut_index = segments[z] << 4;
        for (size_t z = 0; z < 0x10; z++) {
          ret.emplace_back(base_clut.at(base_clut_index | z));
        }
      }
    }
    return ret;
  };
  auto build_single_segment_clut = [&](uint8_t index, uint8_t segment) -> std::vector<ResourceDASM::ColorTableEntry> {
    std::array<uint8_t, 0x10> segments{
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    segments[index] = segment;
    return build_clut_from_segments(segments);
  };

  constexpr uint32_t lmap_type = 0x4C4D4150; // 'LMAP'
  constexpr uint32_t objd_type = 0x4F424A44; // 'OBJD'
  constexpr uint32_t anim_type = 0x414E494D; // 'ANIM'
  constexpr uint32_t ppss_type = 0x50505353; // 'PPSS'

  // Render titles
  for (size_t title_num = 0; title_num < 6; title_num++) {
    std::vector<ResourceDASM::ColorTableEntry> title_clut;
    if (title_num == 1) {
      title_clut = build_clut_from_segments(
          {0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x37, 0x38, 0xFF, 0xFF});
    } else {
      title_clut = build_clut_from_segments(
          {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x37, 0x38, 0xFF, 0xFF});
    }
    auto ppss_res = rf.get_resource(ppss_type, 5000 + (title_num * 100));
    auto ppss_contents = ResourceDASM::decode_PPSS(ppss_res->data, title_clut);
    if (!ppss_contents.contains(0)) {
      throw std::runtime_error("Room PPSS does not contain image 0");
    }
    auto filename = std::format("flashback_title{}.bmp", title_num);
    phosg::save_file(filename, ppss_contents[0].image.serialize(phosg::ImageFormat::WINDOWS_BITMAP));
    phosg::log_info_f("(Title {}) ... {}", title_num, filename);
  }

  auto apply_clut_to_ppss = [](const std::map<size_t, ResourceDASM::IndexedPPSSEntry>& indexed, const std::vector<ResourceDASM::ColorTableEntry>& clut) -> std::map<size_t, ResourceDASM::ColorPPSSEntry> {
    std::map<size_t, ResourceDASM::ColorPPSSEntry> ret;
    for (const auto& [image_index, entry] : indexed) {
      ret.emplace(image_index, entry.apply_clut(clut));
    }
    return ret;
  };

  phosg::log_info_f("Loading Person PPSS");
  auto person_ppss_res = rf.get_resource(ppss_type, 3000);
  auto person_ppss = ResourceDASM::decode_PPSS(person_ppss_res->data, build_single_segment_clut(4, 0x30));
  phosg::log_info_f("Loading NPC1 PPSS");
  auto npc1_ppss_res = rf.get_resource(ppss_type, 3100);
  auto npc1_ppss_indexed = ResourceDASM::decode_PPSS_indexed(npc1_ppss_res->data);
  auto npc1_ppss_clut32 = apply_clut_to_ppss(npc1_ppss_indexed, build_single_segment_clut(5, 0x32));
  auto npc1_ppss_clut33 = apply_clut_to_ppss(npc1_ppss_indexed, build_single_segment_clut(5, 0x33));
  phosg::log_info_f("Loading NPC2 PPSS");
  auto npc2_ppss_res = rf.get_resource(ppss_type, 3200);
  auto npc2_ppss = ResourceDASM::decode_PPSS(npc2_ppss_res->data, build_single_segment_clut(5, 0x34));
  phosg::log_info_f("Loading NPC3 PPSS");
  auto npc3_ppss_res = rf.get_resource(ppss_type, 3300);
  auto npc3_ppss = ResourceDASM::decode_PPSS(npc3_ppss_res->data, build_single_segment_clut(5, 0x35));
  phosg::log_info_f("Loading NPC4 PPSS");
  auto npc4_ppss_res = rf.get_resource(ppss_type, 3400);
  auto npc4_ppss = ResourceDASM::decode_PPSS(npc4_ppss_res->data, build_single_segment_clut(5, 0x36));

  // Render levels
  for (size_t level_num = 0; level_num < level_defs.size(); level_num++) {
    phosg::log_info_f("(Level {}) Loading definition", level_num + 1);
    const auto& level_def = level_defs[level_num];

    auto lmap_res = rf.get_resource(lmap_type, level_def.lmap_res_id);
    auto lmap_data = ResourceDASM::decompress_presage_lzss(lmap_res->data);
    if (lmap_data.size() != sizeof(LevelMap)) {
      throw std::runtime_error(std::format("LMAP:{} size is incorrect", level_def.lmap_res_id));
    }
    const auto* lmap = reinterpret_cast<const LevelMap*>(lmap_data.data());

    auto objd_res = rf.get_resource(objd_type, level_def.objd_res_id);
    auto objd_data = ResourceDASM::decompress_presage_lzss(objd_res->data);
    phosg::StringReader objd_r(objd_data);
    size_t object_count = objd_r.get_u16b();
    const auto* objects = objd_r.get_array<ObjectDefinition>(object_count);

    auto anim_res = rf.get_resource(anim_type, level_def.anim_res_id);
    auto anim_data = ResourceDASM::decompress_presage_lzss(anim_res->data);
    const auto anim = AnimationDefinition::parse_anim(anim_data);

    auto level_clut = build_clut_from_segments(level_def.clut_segments);

    auto objects_ppss_res = rf.get_resource(ppss_type, level_def.objects_ppss_id);
    auto objects_ppss_indexed = ResourceDASM::decode_PPSS_indexed(objects_ppss_res->data);
    auto objects_ppss_default = apply_clut_to_ppss(objects_ppss_indexed, level_clut);

    phosg::log_info_f("(Level {}) Sorting objects", level_num + 1);
    std::array<std::vector<const ObjectDefinition*>, 0x40> objects_for_room_id;
    for (size_t z = 0; z < object_count; z++) {
      if (objects[z].room_id >= objects_for_room_id.size()) {
        phosg::log_warning_f("Object outside of any room: {}", z);
      } else {
        objects_for_room_id[objects[z].room_id].emplace_back(&objects[z]);
      }
    }

    auto resolve_object_ppss = [&](const std::map<size_t, ResourceDASM::ColorPPSSEntry>& objects_ppss, const ObjectDefinition& obj) -> const ResourceDASM::ColorPPSSEntry* {
      const auto& animation = anim.at(obj.anim_entry_index);
      uint16_t spec = animation.frames.at(0).image_spec & 0x7FFF;
      // The game actually does hardcode this logic, sigh
      if (animation.unknown_a2 != 0) {
        return &objects_ppss.at(spec);
      } else if (spec < 0x022F) {
        return &person_ppss.at(spec);
      } else if (spec < 0x28e) {
        return &((level_num == 0) ? npc1_ppss_clut32 : npc1_ppss_clut33).at(spec - 0x022F);
      } else if (spec < 0x02EA) {
        return &person_ppss.at(spec - 0x005F);
      } else if (spec < 0x0386) {
        return &npc2_ppss.at(spec - 0x02EA);
      } else if (spec < 0x0387) {
        return &person_ppss.at(spec - 0x00FB);
      } else if (spec < 0x0430) {
        return &npc3_ppss.at(spec - 0x0387);
      } else if (spec < 0x04E9) {
        return &npc4_ppss.at(spec - 0x0430);
      } else if (spec < 0x0507) {
        return &person_ppss.at(spec - 0x025D);
      } else {
        return nullptr;
      }
    };

    phosg::log_info_f("(Level {}) Placing rooms", level_num + 1);

    auto get_room_link = [&](uint8_t room_id, Direction direction) -> uint8_t {
      switch (direction) {
        case Direction::LEFT:
          return level_def.break_left_room_links.contains(room_id) ? 0xFF : lmap->left_links.at(room_id);
        case Direction::RIGHT:
          return level_def.break_right_room_links.contains(room_id) ? 0xFF : lmap->right_links.at(room_id);
        case Direction::ABOVE:
          return level_def.break_above_room_links.contains(room_id) ? 0xFF : lmap->above_links.at(room_id);
        case Direction::BELOW:
          return level_def.break_below_room_links.contains(room_id) ? 0xFF : lmap->below_links.at(room_id);
        default:
          throw std::logic_error("Invalid direction");
      }
    };

    auto room_is_empty = [&](uint8_t room_id) -> bool {
      // If there is a PPSS or there is any non-empty collision, then the room is not empty
      // TODO: We should add a no-objects condition here too when objects are implemented
      if (rf.resource_exists(ppss_type, level_def.base_room_ppss_id + room_id)) {
        return false;
      }
      for (Direction dir : ALL_DIRECTIONS) {
        uint8_t link_room_id = get_room_link(room_id, dir);
        if ((link_room_id != 0xFF) && (link_room_id != 0x00)) {
          return false;
        }
      }
      const auto& room = lmap->rooms[room_id];
      for (size_t z = 0; z < 0x10; z++) {
        if (room.ceiling_tiles[z] != LevelMap::RoomCollision::Type::NONE) {
          return false;
        }
        for (size_t y = 0; y < 3; y++) {
          const auto& floor = room.floors[y];
          if ((floor.spaces[z] != LevelMap::RoomCollision::Type::NONE) ||
              (floor.tiles[z] != LevelMap::RoomCollision::Type::NONE)) {
            return false;
          }
        }
      }
      return true;
    };

    // Place all the rooms on a grid by flood-filling from the first one. Then place any remaining rooms in separate
    // components as needed
    struct Placement {
      uint8_t room_id = 0;
      uint8_t component_index = 0; // 0 = unplaced
      int8_t x = 0;
      int8_t y = 0;
      uint32_t key() const {
        return (this->component_index << 16) | (static_cast<uint8_t>(this->x) << 8) | static_cast<uint8_t>(this->y);
      }
      bool operator==(const Placement& other) const = default;
      bool operator!=(const Placement& other) const = default;
    };
    std::array<Placement, 0x40> room_placements;

    for (uint8_t room_id = 0; room_id < 0x40; room_id++) {
      room_placements[room_id].room_id = room_id;
    }

    auto find_placement = [&](uint8_t component_index, int8_t x, int8_t y) -> Placement* {
      for (auto& p : room_placements) {
        if ((p.component_index == component_index) && (p.x == x) && (p.y == y)) {
          return &p;
        }
      }
      return nullptr;
    };

    uint8_t current_component_index = 0;
    for (uint8_t start_room_id = 0; start_room_id < 0x40; start_room_id++) {
      // If the room is empty (no PPSS, no collision) or already placed, don't place it in a new component
      if (room_is_empty(start_room_id) || (room_placements[start_room_id].component_index != 0)) {
        continue;
      }
      room_placements[start_room_id].component_index = ++current_component_index;

      std::deque<uint8_t> flood_queue{start_room_id};
      while (!flood_queue.empty()) {
        const auto& from_placement = room_placements[flood_queue.front()];
        flood_queue.pop_front();

        for (Direction dir : ALL_DIRECTIONS) {
          Placement new_placement{
              get_room_link(from_placement.room_id, dir),
              from_placement.component_index,
              static_cast<int8_t>(from_placement.x + x_delta_for_direction(dir)),
              static_cast<int8_t>(from_placement.y + y_delta_for_direction(dir))};
          if ((new_placement.room_id >= room_placements.size()) || room_is_empty(new_placement.room_id)) {
            continue;
          }
          auto& to_placement = room_placements[new_placement.room_id];

          const auto* existing_placement = find_placement(new_placement.component_index, new_placement.x, new_placement.y);
          if (existing_placement && (*existing_placement != new_placement)) {
            for (auto& p : room_placements) {
              bool should_shift = false;
              switch (dir) {
                case Direction::LEFT:
                  should_shift = (p.x < from_placement.x);
                  break;
                case Direction::RIGHT:
                  should_shift = (p.x > from_placement.x);
                  break;
                case Direction::ABOVE:
                  should_shift = (p.y < from_placement.y);
                  break;
                case Direction::BELOW:
                  should_shift = (p.y > from_placement.y);
                  break;
                default:
                  throw std::logic_error("Invalid direction");
              }
              if (should_shift) {
                p.x += x_delta_for_direction(dir);
                p.y += y_delta_for_direction(dir);
              }
            }
            if (find_placement(new_placement.component_index, new_placement.x, new_placement.y)) {
              throw std::logic_error("Existing placement not vacated after shift");
            }
          }

          if (to_placement.component_index == 0) {
            to_placement = new_placement;
            flood_queue.emplace_back(new_placement.room_id);
          }
        }
      }
    }

    // Compute the dimensions of each component
    struct ComponentState {
      int8_t xmin = 0;
      int8_t xmax = 0;
      int8_t ymin = 0;
      int8_t ymax = 0;
      std::vector<ResourceDASM::ColorTableEntry> override_clut;
      phosg::ImageRGBA8888N map;
      std::map<size_t, ResourceDASM::ColorPPSSEntry> override_objects_ppss;

      void add_room_coord(int8_t x, int8_t y) {
        this->xmin = std::min<int8_t>(this->xmin, x);
        this->xmax = std::max<int8_t>(this->xmax, x);
        this->ymin = std::min<int8_t>(this->ymin, y);
        this->ymax = std::max<int8_t>(this->ymax, y);
      }
    };
    std::vector<ComponentState> components;
    for (auto& p : room_placements) {
      if (p.component_index >= components.size()) {
        components.resize(p.component_index + 1);
      }
      auto& component = components[p.component_index];
      component.add_room_coord(p.x, p.y);
      // If this room has a clut override, apply it to the entire component
      if (auto it = level_def.clut_segments_overrides.find(p.room_id); it != level_def.clut_segments_overrides.end()) {
        component.override_clut = build_clut_from_segments(it->second);
        component.override_objects_ppss = apply_clut_to_ppss(objects_ppss_indexed, component.override_clut);
      }
    }

    // Render all the components
    constexpr size_t room_w = 512;
    constexpr size_t room_h = 448;
    phosg::log_info_f("(Level {}) Creating {} component maps", level_num + 1, components.size());
    for (auto& component : components) {
      component.map = phosg::ImageRGBA8888N(
          (component.xmax - component.xmin + 1) * room_w, (component.ymax - component.ymin + 1) * room_h);
    }
    for (size_t room_id = 0; room_id < 0x40; room_id++) {
      const auto& placement = room_placements[room_id];
      if (placement.component_index == 0) {
        if (!room_is_empty(room_id)) {
          throw std::logic_error("Nonempty room was not placed");
        }
        continue;
      }
      phosg::log_info_f("(Level {}) Rendering room {:02X} (component {})", level_num + 1, room_id, placement.component_index);
      const auto& component = components[placement.component_index];
      auto room_map = component.map.view(
          (placement.x - component.xmin) * room_w, (placement.y - component.ymin) * room_h, room_w, room_h);

      // Draw room background (and foreground, though that will be drawn again later)
      int16_t ppss_id = level_def.base_room_ppss_id + room_id;
      uint32_t collision_alpha = default_collision_alpha;
      phosg::ImageGA88N room_ppss_indexed;
      phosg::ImageRGBA8888N room_ppss_color;
      if (rf.resource_exists(ppss_type, ppss_id)) {
        auto ppss_res = rf.get_resource(ppss_type, ppss_id);
        auto ppss_contents = ResourceDASM::decode_PPSS_indexed(ppss_res->data);
        auto& entry = ppss_contents.at(0);
        room_ppss_color = ResourceDASM::apply_clut(
            entry.image, component.override_clut.empty() ? level_clut : component.override_clut);
        room_ppss_indexed = std::move(entry.image);
        room_map.copy_from(room_ppss_color, 0, 0, room_w, room_h, 0, 0);
      } else {
        room_map.write_rect(0, 0, room_w, room_h, 0x000000FF);
        collision_alpha = 0xFF;
      }

      // Draw object images
      for (const auto* obj : objects_for_room_id.at(room_id)) {
        const auto* entry = resolve_object_ppss(
            component.override_objects_ppss.empty() ? objects_ppss_default : component.override_objects_ppss, *obj);
        if (entry) {
          room_map.copy_from_with_blend(
              entry->image,
              ((obj->x_div2 + 8) * 2) - entry->origin_x,
              ((obj->y_div2 + 2) * 2) - entry->origin_y,
              entry->image.get_width(),
              entry->image.get_height(),
              0,
              0);
        }
      }

      // Draw room foreground again, in case objects rendered over it
      for (size_t y = 0; y < room_ppss_indexed.get_height(); y++) {
        for (size_t x = 0; x < room_ppss_indexed.get_width(); x++) {
          if (phosg::get_r(room_ppss_indexed.read(x, y)) & 0x80) {
            room_map.write(x, y, room_ppss_color.read(x, y));
          }
        }
      }

      // Draw collision overlay
      if (collision_alpha > 0) {
        const auto& coll = lmap->rooms[room_id];
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
      }
    }

    for (size_t room_id = 0; room_id < 0x40; room_id++) {
      // Draw object and room annotations. We don't use room_map here because we want the rectangles to properly show
      // objects that overlap room boundaries
      const auto& placement = room_placements[room_id];
      auto& component = components[placement.component_index];
      size_t room_x = (room_w * (placement.x - component.xmin));
      size_t room_y = (room_h * (placement.y - component.ymin));

      if (show_object_annotations) {
        for (const auto* obj : objects_for_room_id.at(room_id)) {
          const auto* entry = resolve_object_ppss(
              component.override_objects_ppss.empty() ? objects_ppss_default : component.override_objects_ppss, *obj);
          if (entry) {
            size_t obj_x = room_x + ((obj->x_div2 + 8) * 2) - entry->origin_x;
            size_t obj_y = room_y + ((obj->y_div2 + 2) * 2) - entry->origin_y;
            size_t obj_w = entry->image.get_width() - 1;
            size_t obj_h = entry->image.get_height() - 1;
            component.map.draw_horizontal_line(obj_x, obj_x + obj_w, obj_y, 0, 0xFF0000FF);
            component.map.draw_horizontal_line(obj_x, obj_x + obj_w, obj_y + obj_h, 0, 0xFF0000FF);
            component.map.draw_vertical_line(obj_x, obj_y, obj_y + obj_h, 0, 0xFF0000FF);
            component.map.draw_vertical_line(obj_x + obj_w, obj_y, obj_y + obj_h, 0, 0xFF0000FF);
            // TODO: Add more info to these as we discover what the fields do
            component.map.draw_text(obj_x + 2, obj_y + 2, 0xFFFFFFFF, 0x00000040, "{:04X}", obj->anim_entry_index);
          }
        }
      }

      component.map.draw_text(room_x + 1, room_y + 1, 0xFFFFFFFF, 0x00000040, "Room {:02X}", room_id);
    }

    // Draw lines connecting rooms that aren't rendered next to each other
    phosg::log_info_f("(Level {}) Rendering room links", level_num + 1);
    for (size_t room_id = 0; room_id < 0x40; room_id++) {
      const auto& placement = room_placements[room_id];
      auto& component = components.at(placement.component_index);
      for (Direction dir : ALL_DIRECTIONS) {
        uint8_t linked_room_id = get_room_link(placement.room_id, dir);
        if ((linked_room_id == 0xFF) || room_is_empty(placement.room_id) || room_is_empty(linked_room_id)) {
          continue;
        }
        const auto& linked_placement = room_placements.at(linked_room_id);
        if ((linked_placement.x != (placement.x + x_delta_for_direction(dir))) ||
            (linked_placement.y != (placement.y + y_delta_for_direction(dir)))) {
          ssize_t x0 = ((placement.x - component.xmin) * room_w) + ((((1 + x_delta_for_direction(dir)) * room_w) / 2));
          ssize_t y0 = ((placement.y - component.ymin) * room_h) + ((((1 + y_delta_for_direction(dir)) * room_h) / 2));
          ssize_t x1 = ((linked_placement.x - component.xmin) * room_w) + ((((1 - x_delta_for_direction(dir)) * room_w) / 2));
          ssize_t y1 = ((linked_placement.y - component.ymin) * room_h) + ((((1 - y_delta_for_direction(dir)) * room_h) / 2));
          ssize_t x_delta = (x_delta_for_direction(dir) == 0) ? 2 : 0;
          ssize_t y_delta = (y_delta_for_direction(dir) == 0) ? 2 : 0;
          component.map.draw_line(x0, y0, x1, y1, 0xFF0000FF);
          component.map.draw_line(x0 + x_delta, y0 + y_delta, x1 + x_delta, y1 + y_delta, 0xFF0000FF);
          component.map.draw_line(x0 - x_delta, y0 - y_delta, x1 - x_delta, y1 - y_delta, 0xFF0000FF);
        }
      }
    }

    // Save all the components
    for (size_t z = 1; z < components.size(); z++) {
      std::string filename = std::format("flashback_level{}_component{}.bmp", level_num + 1, z);
      phosg::save_file(filename, components[z].map.serialize(phosg::ImageFormat::WINDOWS_BITMAP));
      phosg::log_info_f("(Level {}) ... {}", level_num + 1, filename);
    }
  }

  return 0;
}
