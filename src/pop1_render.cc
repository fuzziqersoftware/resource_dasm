#include <inttypes.h>

#include <phosg/Arguments.hh>
#include <phosg/Encoding.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <string>
#include <unordered_set>

#include "IndexFormats/Formats.hh"
#include "ResourceFile.hh"
#include "SpriteDecoders/Decoders.hh"

constexpr uint32_t RESOURCE_TYPE_LEVL = 0x4C45564C;
// constexpr uint32_t RESOURCE_TYPE_SHPD = 0x53485044;
// constexpr uint32_t RESOURCE_TYPE_SHPT = 0x53485054;

// TODO: In LC and BW, there's a blank line between tile rows; should the height be 1 smaller?
// Both reserve 6px for the floor; the floor-top anchor is 3 units (6px) above the floor-bottom anchor
constexpr size_t tile_w(bool is_large) {
  return is_large ? 64 : 51;
}
constexpr size_t tile_h(bool is_large) {
  return is_large ? 126 : 104;
}

int16_t resolve_anchor_x(int16_t pos, bool is_large) {
  static constexpr std::array<int16_t, 0x240> small_x_anchor_positions{
      /* 000 */ -204, -202, -201, -199, -198, -196, -194, -193, -191, -190, -188, -186, -185, -183, -182, -180,
      /* 010 */ -179, -177, -175, -174, -172, -171, -169, -167, -166, -164, -163, -161, -159, -158, -156, -155,
      /* 020 */ -153, -151, -150, -148, -147, -145, -143, -142, -140, -139, -137, -135, -134, -132, -131, -129,
      /* 030 */ -128, -126, -124, -123, -121, -120, -118, -116, -115, -113, -112, -110, -108, -107, -105, -104,
      /* 040 */ -102, -100, -99, -97, -96, -94, -92, -91, -89, -88, -86, -84, -83, -81, -80, -78,
      /* 050 */ -77, -75, -73, -72, -70, -69, -67, -65, -64, -62, -61, -59, -57, -56, -54, -53,
      /* 060 */ -51, -49, -48, -46, -45, -43, -41, -40, -38, -37, -35, -33, -32, -30, -29, -27,
      /* 070 */ -26, -24, -22, -21, -19, -18, -16, -14, -13, -11, -10, -8, -6, -5, -3, -2,
      /* 080 */ 0, 2, 3, 5, 6, 8, 10, 11, 13, 14, 16, 18, 19, 21, 22, 24,
      /* 090 */ 26, 27, 29, 30, 32, 33, 35, 37, 38, 40, 41, 43, 45, 46, 48, 49,
      /* 0A0 */ 51, 53, 54, 56, 57, 59, 61, 62, 64, 65, 67, 69, 70, 72, 73, 75,
      /* 0B0 */ 77, 78, 80, 81, 83, 84, 86, 88, 89, 91, 92, 94, 96, 97, 99, 100,
      /* 0C0 */ 102, 104, 105, 107, 108, 110, 112, 113, 115, 116, 118, 120, 121, 123, 124, 126,
      /* 0D0 */ 128, 129, 131, 132, 134, 135, 137, 139, 140, 142, 143, 145, 147, 148, 150, 151,
      /* 0E0 */ 153, 155, 156, 158, 159, 161, 163, 164, 166, 167, 169, 171, 172, 174, 175, 177,
      /* 0F0 */ 179, 180, 182, 183, 185, 186, 188, 190, 191, 193, 194, 196, 198, 199, 201, 202,
      /* 100 */ 204, 206, 207, 209, 210, 212, 214, 215, 217, 218, 220, 222, 223, 225, 226, 228,
      /* 110 */ 230, 231, 233, 234, 236, 237, 239, 241, 242, 244, 245, 247, 249, 250, 252, 253,
      /* 120 */ 255, 257, 258, 260, 261, 263, 265, 266, 268, 269, 271, 273, 274, 276, 277, 279,
      /* 130 */ 281, 282, 284, 285, 287, 288, 290, 292, 293, 295, 296, 298, 300, 301, 303, 304,
      /* 140 */ 306, 308, 309, 311, 312, 314, 316, 317, 319, 320, 322, 324, 325, 327, 328, 330,
      /* 150 */ 332, 333, 335, 336, 338, 339, 341, 343, 344, 346, 347, 349, 351, 352, 354, 355,
      /* 160 */ 357, 359, 360, 362, 363, 365, 367, 368, 370, 371, 373, 375, 376, 378, 379, 381,
      /* 170 */ 383, 384, 386, 387, 389, 390, 392, 394, 395, 397, 398, 400, 402, 403, 405, 406,
      /* 180 */ 408, 410, 411, 413, 414, 416, 418, 419, 421, 422, 424, 426, 427, 429, 430, 432,
      /* 190 */ 434, 435, 437, 438, 440, 441, 443, 445, 446, 448, 449, 451, 453, 454, 456, 457,
      /* 1A0 */ 459, 461, 462, 464, 465, 467, 469, 470, 472, 473, 475, 477, 478, 480, 481, 483,
      /* 1B0 */ 485, 486, 488, 489, 491, 492, 494, 496, 497, 499, 500, 502, 504, 505, 507, 508,
      /* 1C0 */ 510, 512, 513, 515, 516, 518, 520, 521, 523, 524, 526, 528, 529, 531, 532, 534,
      /* 1D0 */ 536, 537, 539, 540, 542, 543, 545, 547, 548, 550, 551, 553, 555, 556, 558, 559,
      /* 1E0 */ 561, 563, 564, 566, 567, 569, 571, 572, 574, 575, 577, 579, 580, 582, 583, 585,
      /* 1F0 */ 587, 588, 590, 591, 593, 594, 596, 598, 599, 601, 602, 604, 606, 607, 609, 610,
      /* 200 */ 612, 614, 615, 617, 618, 620, 622, 623, 625, 626, 628, 630, 631, 633, 634, 636,
      /* 210 */ 638, 639, 641, 642, 644, 645, 647, 649, 650, 652, 653, 655, 657, 658, 660, 661,
      /* 220 */ 663, 665, 666, 668, 669, 671, 673, 674, 676, 677, 679, 681, 682, 684, 685, 687,
      /* 230 */ 689, 690, 692, 693, 695, 696, 698, 700, 701, 703, 704, 706, 708, 709, 711, 712};
  return is_large ? (pos * 2) : small_x_anchor_positions.at(pos + 0x80);
}

int16_t resolve_anchor_y(int16_t pos, bool is_large) {
  std::array<int16_t, 0x13E> small_y_anchor_positions{
      /* 000 */ -104, -102, -100, -98, -96, -95, -93, -91, -90, -88, -87, -85, -83, -82, -80, -78,
      /* 010 */ -77, -75, -74, -72, -70, -69, -67, -65, -64, -62, -60, -59, -57, -56, -54, -52,
      /* 020 */ -51, -49, -47, -46, -44, -42, -41, -39, -38, -36, -34, -33, -31, -29, -28, -26,
      /* 030 */ -25, -23, -21, -20, -18, -16, -15, -13, -11, -10, -8, -7, -5, -3, -2, 0,
      /* 040 */ 2, 4, 6, 8, 9, 11, 13, 14, 16, 17, 19, 21, 22, 24, 26, 27,
      /* 050 */ 29, 31, 32, 34, 35, 37, 39, 40, 42, 44, 45, 47, 48, 50, 52, 53,
      /* 060 */ 55, 57, 58, 60, 62, 63, 65, 66, 68, 70, 71, 73, 75, 76, 78, 80,
      /* 070 */ 81, 83, 84, 86, 88, 89, 91, 93, 94, 96, 97, 99, 101, 102, 104, 106,
      /* 080 */ 108, 110, 112, 113, 115, 117, 118, 120, 121, 123, 125, 126, 128, 130, 131, 133,
      /* 090 */ 135, 136, 138, 139, 141, 143, 144, 146, 148, 149, 151, 152, 154, 156, 157, 159,
      /* 0A0 */ 161, 162, 164, 166, 167, 169, 170, 172, 174, 175, 177, 179, 180, 182, 184, 185,
      /* 0B0 */ 187, 188, 190, 192, 193, 195, 197, 198, 200, 201, 203, 205, 206, 208, 210, 212,
      /* 0C0 */ 214, 216, 217, 219, 221, 222, 224, 225, 227, 229, 230, 232, 234, 235, 237, 239,
      /* 0D0 */ 240, 242, 243, 245, 247, 248, 250, 252, 253, 255, 256, 258, 260, 261, 263, 265,
      /* 0E0 */ 266, 268, 270, 271, 273, 274, 276, 278, 279, 281, 283, 284, 286, 288, 289, 291,
      /* 0F0 */ 292, 294, 296, 297, 299, 301, 302, 304, 305, 307, 309, 310, 312, 314, 316, 318,
      /* 100 */ 320, 321, 323, 325, 326, 328, 329, 331, 333, 334, 336, 338, 339, 341, 343, 344,
      /* 110 */ 346, 347, 349, 351, 352, 354, 356, 357, 359, 360, 362, 364, 365, 367, 369, 370,
      /* 120 */ 372, 374, 375, 377, 378, 380, 382, 383, 385, 387, 388, 390, 392, 393, 395, 396,
      /* 130 */ 398, 400, 401, 403, 405, 406, 408, 409, 411, 413, 414, 416, 418, 420};
  return is_large ? (pos * 2) : small_y_anchor_positions.at(pos + 0x3F);
}

enum TileType : uint8_t {
  EMPTY = 0x00,
  FLOOR = 0x01,
  SPIKES = 0x02,
  SMALL_PILLAR = 0x03,
  GATE = 0x04,
  RAISE_TILE_1 = 0x05,
  DROP_TILE = 0x06,
  TAPESTRY = 0x07,
  LARGE_PILLAR_BOTTOM = 0x08,
  LARGE_PILLAR_TOP = 0x09,
  POTION = 0x0A,
  LOOSE_FLOOR = 0x0B,
  TAPESTRY_TOP = 0x0C,
  MIRROR = 0x0D,
  DEBRIS = 0x0E,
  RAISE_TILE_2 = 0x0F,
  LEVEL_DOOR_LEFT = 0x10,
  LEVEL_DOOR_RIGHT = 0x11,
  CHOMPER = 0x12,
  TORCH = 0x13,
  WALL = 0x14,
  SKELETON = 0x15,
  SWORD = 0x16,
  BALCONY_LEFT = 0x17,
  BALCONY_RIGHT = 0x18,
  LATTICE_PILLAR = 0x19,
  LATTICE_SUPPORT = 0x1A,
  SMALL_LATTICE = 0x1B,
  LATTICE_LEFT = 0x1C,
  LATTICE_RIGHT = 0x1D,
  TORCH_WITH_DEBRIS = 0x1E,
};

struct PieceDefinition {
  uint8_t floor_top_image;
  uint8_t unknown_a2;
  int8_t floor_top_anchor_delta_y;
  uint8_t right_overhang_image;
  uint8_t has_floor;
  int8_t overhang_anchor_delta_y;
  uint8_t bg_overhang_image;
  uint8_t lower_corner_image;
  uint8_t floor_front_image;
  uint8_t fg_image;
  int8_t fg_anchor_delta_x_cells;
  int8_t fg_anchor_delta_y;
};

constexpr static std::array<PieceDefinition, 0x20> piece_defs{
    // GI = floor image index (in SHPD slot 6)
    // GY = floor image Y offset, in anchor points
    // HF = has floor
    // OS = right (overhang/background) floor image index (in SHPD slot 6; res ID 1200 or 2200)
    // LY = right floor Y offset, in anchor points
    // BD = background decoration image index
    // BC = background corner image index
    // FS = floor front image index (in SHPD slot 6); this is just the sliver that renders in the tile's bottom 6 pixels
    // FG = foreground image index (in SHPD slot 6); this is what renders above the FS sliver (the rest of the tile box)
    // FX = X offset for FG image, in X intervals (8 anchor points each)
    // FY = Y offset for FG image, in anchor points
    //                GI          GY    OS    HF    LY    BD    BC    FS    FG    FX    FY
    //                00    01    02    03    04    05    06    07    08    09    0A    0B
    PieceDefinition{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 00 EMPTY
    PieceDefinition{0x29, 0x01, 0x00, 0x2A, 0x01, 0x02, 0x91, 0x00, 0x2B, 0x00, 0x00, 0x00}, // 01 FLOOR
    PieceDefinition{0x7F, 0x01, 0x00, 0x85, 0x01, 0x02, 0x91, 0x00, 0x2B, 0x00, 0x00, 0x00}, // 02 SPIKES
    PieceDefinition{0x5C, 0x01, 0x00, 0x5D, 0x01, 0x02, 0x00, 0x5E, 0x2B, 0x5F, 0x01, 0x00}, // 03 SMALL_PILLAR
    PieceDefinition{0x2E, 0x01, 0x00, 0x2F, 0x01, 0x02, 0x00, 0x30, 0x2B, 0x31, 0x03, 0x00}, // 04 GATE
    PieceDefinition{0x29, 0x01, 0x01, 0x23, 0x01, 0x03, 0x91, 0x00, 0x24, 0x00, 0x00, 0x00}, // 05 RAISE_TILE_1
    PieceDefinition{0x29, 0x01, 0x00, 0x1C, 0x01, 0x02, 0x91, 0x00, 0x60, 0x00, 0x00, 0x00}, // 06 DROP_TILE
    PieceDefinition{0x2E, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x2B, 0x31, 0x03, 0x00}, // 07 TAPESTRY
    PieceDefinition{0x56, 0x01, 0x00, 0x57, 0x01, 0x02, 0x00, 0x00, 0x2B, 0x58, 0x01, 0x00}, // 08 LARGE_PILLAR_BOTTOM
    PieceDefinition{0x00, 0x00, 0x00, 0x59, 0x00, 0x03, 0x00, 0x5A, 0x00, 0x5B, 0x01, 0x03}, // 09 LARGE_PILLAR_TOP
    PieceDefinition{0x29, 0x01, 0x00, 0x2A, 0x01, 0x02, 0x91, 0x00, 0x2B, 0x0C, 0x02, -0x03}, // 0A POTION
    PieceDefinition{0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x91, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0B LOOSE_FLOOR
    PieceDefinition{0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x55, 0x31, 0x03, 0x00}, // 0C TAPESTRY_TOP
    PieceDefinition{0x4B, 0x01, 0x00, 0x2A, 0x01, 0x02, 0x00, 0x00, 0x2B, 0x4D, 0x00, 0x00}, // 0D MIRROR
    PieceDefinition{0x61, 0x01, 0x00, 0x62, 0x01, 0x02, 0x91, 0x00, 0x2B, 0x64, 0x00, 0x00}, // 0E DEBRIS
    PieceDefinition{0x93, 0x01, 0x00, 0x1D, 0x01, 0x01, 0x91, 0x00, 0x95, 0x00, 0x00, 0x00}, // 0F RAISE_TILE_2
    PieceDefinition{0x29, 0x01, 0x00, 0x25, 0x00, 0x00, 0x00, 0x26, 0x2B, 0x00, 0x00, 0x00}, // 10 LEVEL_DOOR_LEFT
    PieceDefinition{0x00, 0x00, 0x00, 0x27, 0x01, 0x02, 0x00, 0x28, 0x2B, 0x00, 0x00, 0x00}, // 11 LEVEL_DOOR_RIGHT
    PieceDefinition{0x00, 0x00, 0x00, 0x2A, 0x01, 0x02, 0x91, 0x00, 0x2B, 0x00, 0x00, 0x00}, // 12 CHOMPER
    PieceDefinition{0x29, 0x01, 0x00, 0x2A, 0x01, 0x02, 0x00, 0x00, 0x2B, 0x00, 0x00, 0x00}, // 13 TORCH
    PieceDefinition{0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00}, // 14 WALL
    PieceDefinition{0x1E, 0x01, 0x00, 0x1F, 0x01, 0x02, 0x00, 0x00, 0x2B, 0x00, 0x00, 0x00}, // 15 SKELETON
    PieceDefinition{0x29, 0x01, 0x00, 0x2A, 0x01, 0x02, 0x91, 0x00, 0x2B, 0x00, 0x00, 0x00}, // 16 SWORD
    PieceDefinition{0x29, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x0B, 0x2B, 0x00, 0x00, 0x00}, // 17 BALCONY_LEFT
    PieceDefinition{0x00, 0x00, 0x00, 0x0C, 0x01, 0x02, 0x00, 0x0D, 0x2B, 0x00, 0x00, 0x00}, // 18 BALCONY_RIGHT
    PieceDefinition{0x5C, 0x01, 0x00, 0x2A, 0x01, 0x02, 0x91, 0x00, 0x2B, 0x5F, 0x01, 0x00}, // 19 LATTICE_PILLAR
    PieceDefinition{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x09, 0x00, -0x35}, // 1A LATTICE_SUPPORT
    PieceDefinition{0x03, 0x00, -0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, -0x35}, // 1B SMALL_LATTICE
    PieceDefinition{0x04, 0x00, -0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, -0x35}, // 1C LATTICE_LEFT
    PieceDefinition{0x05, 0x00, -0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, -0x35}, // 1D LATTICE_RIGHT
    PieceDefinition{0x61, 0x01, 0x00, 0x62, 0x01, 0x02, 0x00, 0x00, 0x2B, 0x64, 0x00, 0x00}, // 1E TORCH_WITH_DEBRIS
    PieceDefinition{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 1F (invalid)
};

struct PrinceOfPersiaLevel {
  struct RoomLinks {
    // All values are in [1,24]
    /* 00 */ uint8_t left;
    /* 01 */ uint8_t right;
    /* 02 */ uint8_t above;
    /* 03 */ uint8_t below;
    /* 04 */
  } __attribute__((packed));

  /* 0000 */ uint8_t tiles_foreground[24][30];
  /* 02D0 */ uint8_t tiles_background[24][30];
  /* 05A0 */ uint8_t events1[0x100]; // Bits: NRRTTTTT (N = trigger next, R = room number (low 2 bits), T = tile ID)
  /* 06A0 */ uint8_t events2[0x100]; // Bits: RRRSSSSS (R = room number (high 3 bits), S = state (unused in file?))
  /* 07A0 */ RoomLinks room_links[24];
  /* 0800 */ uint8_t unknown_a1[0x40];
  /* 0840 */ uint8_t start_room; // 1-24
  /* 0841 */ uint8_t start_tile; // 0-29
  /* 0842 */ int8_t start_direction; // 0 = right, -1 = left
  /* 0843 */ uint8_t unknown_a2[0x15];
  /* 0858 */ uint8_t guard_tile_indexes[0x18]; // 0-29; >= 30 means no guard
  /* 0870 */ int8_t guard_directions[0x18]; // 0 = right, -1 = left
  /* 0888 */ uint8_t unknown_a3[0x18];
  /* 08A0 */ uint8_t unknown_a4[0x18];
  /* 08B8 */ uint8_t guard_skill_level[0x18];
  /* 08D0 */ uint8_t unknown_a5[0x18];
  /* 08E8 */ uint8_t guard_color[0x18];
  /* 0900 */

  struct Event {
    uint8_t room_number;
    uint8_t tile_index;
    bool trigger_next;
  };
  constexpr Event get_event(uint8_t index) const {
    uint8_t high = this->events1[index];
    return Event{
        .room_number = static_cast<uint8_t>(((high >> 5) & 0x03) | ((this->events2[index] >> 3) & 0x1C)),
        .tile_index = static_cast<uint8_t>(high & 0x1F),
        .trigger_next = !(high & 0x80)};
  }

  static inline uint8_t index_for_tile_coords(int8_t x, int8_t y) {
    if (x < 0 || x >= 10 || y < 0 || y >= 3) {
      throw std::logic_error("Invalid tile coordinates");
    }
    return (y * 10) + x;
  }
  std::tuple<TileType, uint8_t, uint8_t> tile_info(
      uint8_t room_id, int8_t x, int8_t y, const std::tuple<TileType, uint8_t, uint8_t>& defaults) const {
    if (room_id >= 0x18) {
      return defaults;
    }
    while (y < 0) {
      room_id = this->room_links[room_id].above - 1;
      if (room_id >= 0x18) {
        return defaults;
      }
      y += 3;
    }
    while (y >= 3) {
      room_id = this->room_links[room_id].below - 1;
      if (room_id >= 0x18) {
        return defaults;
      }
      y -= 3;
    }
    while (x < 0) {
      room_id = this->room_links[room_id].left - 1;
      if (room_id >= 0x18) {
        return defaults;
      }
      x += 10;
    }
    while (x >= 10) {
      room_id = this->room_links[room_id].right - 1;
      if (room_id >= 0x18) {
        return defaults;
      }
      x -= 10;
    }
    uint8_t tile_index = this->index_for_tile_coords(x, y);
    uint8_t fg = this->tiles_foreground[room_id][tile_index];
    return std::make_tuple(static_cast<TileType>(fg & 0x1F), fg, this->tiles_background[room_id][tile_index]);
  }

  void preprocess() {
    for (uint8_t room_id = 0; room_id < 24; room_id++) {
      for (uint8_t tile_index = 0; tile_index < 30; tile_index++) {
        uint8_t& bg = this->tiles_background[room_id][tile_index];
        uint8_t& fg = this->tiles_foreground[room_id][tile_index];
        switch (fg & 0x1F) {
          case TileType::GATE:
            bg = (bg == 1) ? 0xBC : 0x00;
            break;
          case TileType::POTION:
            bg <<= 5;
            // Note: In the original game, this is also where the assignment of the open potion on the copy protection
            // level occurs
            break;
          case TileType::LOOSE_FLOOR:
            bg = 0x00;
            break;
          case TileType::WALL: {
            static constexpr std::tuple<TileType, uint8_t, uint8_t> default_info = std::make_tuple(TileType::WALL, 0, 0);
            bool left_is_wall = std::get<0>(this->tile_info(room_id, (tile_index % 10) - 1, tile_index / 10, default_info)) == TileType::WALL;
            bool right_is_wall = std::get<0>(this->tile_info(room_id, (tile_index % 10) + 1, tile_index / 10, default_info)) == TileType::WALL;
            bg = (bg << 7) | (left_is_wall ? 2 : 0) | (right_is_wall ? 1 : 0);
            break;
          }
        }
      }
    }
  }
} __attribute__((packed));
static_assert(sizeof(PrinceOfPersiaLevel) == 0x900, "PrinceOfPersiaLevel size is incorrect");

struct PoP1RandomGenerator {
  uint32_t state;

  PoP1RandomGenerator(uint32_t state = 0x12345678) : state(state) {}

  uint16_t operator()(size_t max) {
    uint32_t orig_state = state;
    state = ((state >> 0x10) + (state ^ 0x569A)) * 0x6A59;
    return (((orig_state + state) & 0xFFFF) * (max + 1)) >> 0x10;
  };
};

struct Graphics {
  // SHPD slots:
  //   0 = swords (700)
  //   1 = torch, sword (on ground), potion (150)
  //   2 = prince, mouse, life bottles (400)
  //   3 = titles 1 (40)
  //   4 = titles 2 (50)
  //   5 = guards (1000 * guard_type_remapped + 750)
  //   6 = tiles (1200 (dungeon), 2200 (palace))
  //   7 = wall blocks (1360 (dungeon), 2360 (palace))
  std::array<std::unordered_map<size_t, ResourceDASM::DecodedSHPDImage>, 8> slots;

  inline const ResourceDASM::DecodedSHPDImage* get(size_t slot_index, size_t image_index) const {
    const auto& slot = this->slots.at(slot_index);
    auto it = slot.find(image_index);
    return (it == slot.end()) ? nullptr : &it->second;
  }
};

struct Env {
  const std::vector<ResourceDASM::ColorTableEntry>* clut;
  const Graphics* graphics;
  phosg::ImageRGBA8888N map;
  const PrinceOfPersiaLevel* orig_level = nullptr;
  const PrinceOfPersiaLevel* preprocessed_level = nullptr;
  std::array<std::array<std::array<uint8_t, 11>, 4>, 3> current_random_data;
  uint8_t room_id = 0;
  int8_t tile_x = 0;
  int8_t tile_y = 0;
  bool is_color = true;
  bool is_large = true;
  bool is_palace = false;

  static constexpr int16_t CELL_TOP_ANCHOR = -63;
  static constexpr int16_t FLOOR_TOP_ANCHOR = -4;
  static constexpr int16_t FLOOR_BOTTOM_ANCHOR = -1;

  Env(const std::vector<ResourceDASM::ColorTableEntry>* clut,
      const Graphics* graphics,
      const PrinceOfPersiaLevel* orig_level,
      const PrinceOfPersiaLevel* preprocessed_level,
      size_t w_rooms,
      size_t h_rooms,
      bool is_color,
      bool is_large,
      bool is_palace)
      : clut(clut),
        graphics(graphics),
        map(w_rooms * 10 * ::tile_w(is_large), h_rooms * 3 * ::tile_h(is_large), 0x202020FF),
        orig_level(orig_level),
        preprocessed_level(preprocessed_level),
        is_color(is_color),
        is_large(is_large),
        is_palace(is_palace) {}

  Env view(size_t x, size_t y, size_t w, size_t h) {
    Env ret(this->clut, this->graphics, this->orig_level, this->preprocessed_level, 0, 0, this->is_color, this->is_large, this->is_palace);
    ret.map = this->map.view(x, y, w, h);
    ret.room_id = this->room_id;
    ret.tile_x = this->tile_x;
    ret.tile_y = this->tile_y;
    return ret;
  }

  constexpr size_t tile_w() const {
    return ::tile_w(this->is_large);
  }
  constexpr size_t tile_h() const {
    return ::tile_h(this->is_large);
  }
  constexpr size_t resolve_anchor_x(ssize_t x) const {
    return ::resolve_anchor_x(x, this->is_large);
  }
  constexpr size_t resolve_anchor_y(ssize_t x) const {
    return ::resolve_anchor_y(x, this->is_large);
  }

  constexpr std::tuple<TileType, uint8_t, uint8_t> tile_info(
      bool preprocessed, int8_t delta_x = 0, int8_t delta_y = 0) const {
    constexpr static std::tuple<TileType, uint8_t, uint8_t> default_empty = std::make_tuple(TileType::EMPTY, 0, 0);
    constexpr static std::tuple<TileType, uint8_t, uint8_t> default_floor = std::make_tuple(TileType::FLOOR, 0, 0);
    constexpr static std::tuple<TileType, uint8_t, uint8_t> default_wall = std::make_tuple(TileType::WALL, 0, 0);
    int8_t target_tile_x = this->tile_x + delta_x;
    int8_t target_tile_y = this->tile_y + delta_y;
    const std::tuple<TileType, uint8_t, uint8_t>& default_res = (target_tile_y < 0)
        ? default_floor
        : (target_tile_x < 0)
        ? default_wall
        : default_empty;
    const auto* level = preprocessed ? this->preprocessed_level : this->orig_level;
    return level->tile_info(this->room_id, target_tile_x, target_tile_y, default_res);
  }

  void draw_SHPD_image_at_anchor(
      size_t slot_index, size_t image_index, int16_t anchor_delta_x, int16_t anchor_delta_y) {
    const auto* entry = this->graphics->get(slot_index, image_index);
    if (entry) {
      this->map.copy_from_with_blend(
          entry->image,
          this->resolve_anchor_x(this->tile_x * 32 + anchor_delta_x) - entry->origin_x,
          this->resolve_anchor_y((this->tile_y + 1) * 63 + anchor_delta_y) - entry->origin_y,
          entry->image.get_width(),
          entry->image.get_height(),
          0,
          0);
    }
  }

  void draw_rect_at_anchor(int16_t anchor_delta_x, int16_t anchor_delta_y, int16_t w, int16_t h, uint32_t clut_index) {
    ssize_t dest_x1 = this->resolve_anchor_x(this->tile_x * 32 + anchor_delta_x);
    ssize_t dest_y1 = this->resolve_anchor_y((this->tile_y + 1) * 63 + anchor_delta_y - h + 1);
    ssize_t dest_x2 = this->resolve_anchor_x(this->tile_x * 32 + anchor_delta_x + w);
    ssize_t dest_y2 = this->resolve_anchor_y((this->tile_y + 1) * 63 + anchor_delta_y + 1);
    this->map.write_rect(dest_x1, dest_y1, dest_x2 - dest_x1, dest_y2 - dest_y1, this->clut->at(clut_index).c.rgba8888());
  }
};

bool current_tile_is_empty_or_pillar_without_floor(Env& env) {
  auto [type, _1, _2] = env.tile_info(true);
  return (type == TileType::LATTICE_SUPPORT) ||
      (type == TileType::LARGE_PILLAR_TOP) ||
      (type == TileType::EMPTY) ||
      (type == TileType::TAPESTRY_TOP);
}

void draw_left_below_tile_corner_below_floor(Env& env) {
  auto [type, fg, bg] = env.tile_info(true, -1, 1);
  if ((type == TileType::TAPESTRY) || (type == TileType::TAPESTRY_TOP)) {
    if (env.is_palace) {
      std::array<uint8_t, 4> tapestry_image_indexes{0x00, 0x51, 0x53, 0x00};
      env.draw_SHPD_image_at_anchor(6, tapestry_image_indexes.at(bg), 0, Env::FLOOR_BOTTOM_ANCHOR);
    }
  } else if (type == TileType::WALL) { // Wall side top corner
    env.draw_SHPD_image_at_anchor(7, 2, 0, Env::FLOOR_BOTTOM_ANCHOR);
  } else { // Background images for e.g. column, gate, balcony, level door
    env.draw_SHPD_image_at_anchor(6, piece_defs.at(type).lower_corner_image, 0, Env::FLOOR_BOTTOM_ANCHOR);
  }
}

void draw_left_tile_floor_overhang(Env& env) {
  if (current_tile_is_empty_or_pillar_without_floor(env)) {
    draw_left_below_tile_corner_below_floor(env);
    auto [left_type, left_fg, left_bg] = env.tile_info(true, -1, 0);
    if (piece_defs.at(left_type).has_floor) { // Left side of normal floor tile
      env.draw_SHPD_image_at_anchor(6, 0x2A, 0, Env::FLOOR_TOP_ANCHOR + piece_defs[1].overhang_anchor_delta_y);
    }
  }
}

void draw_below_tile_gate_top(Env& env) {
  auto [type, _1, _2] = env.tile_info(true);
  auto [below_type, _3, below_bg] = env.tile_info(true, -1, 1);
  if (((type == TileType::LARGE_PILLAR_TOP) || (type == TileType::EMPTY) || (type == TileType::TAPESTRY_TOP)) &&
      (below_type == TileType::GATE)) {
    uint8_t progress = std::min<uint8_t>(below_bg, 188) / 4;
    if (!env.is_large) {
      std::array<uint8_t, 0x2D> image_indexes{
          0x00, 0x02, 0x04, 0x05, 0x07, 0x08, 0x0A, 0x00, 0x01, 0x03, 0x05, 0x06, 0x08, 0x09, 0x0B, 0x01, 0x02, 0x04,
          0x06, 0x07, 0x09, 0x0B, 0x00, 0x02, 0x03, 0x05, 0x07, 0x08, 0x0A, 0x00, 0x01, 0x03, 0x05, 0x06, 0x08, 0x09,
          0x0B, 0x01, 0x02, 0x04, 0x06, 0x07, 0x09, 0x0A, 0x00};
      env.draw_SHPD_image_at_anchor(6, 0x34, 0, Env::FLOOR_BOTTOM_ANCHOR); // Gate segment
      env.draw_SHPD_image_at_anchor(
          6, image_indexes[std::min<uint8_t>(progress, 0x2D)] + 0x35, 0, Env::FLOOR_BOTTOM_ANCHOR); // Top of gate
    } else {
      env.draw_SHPD_image_at_anchor(6, 0x44, 0, Env::FLOOR_BOTTOM_ANCHOR); // Top of open gate with pole in background
      env.draw_SHPD_image_at_anchor(6, 0x3C + (progress % 8), 0, Env::FLOOR_BOTTOM_ANCHOR); // Bars in various phases
    }
  }
}

void draw_left_tile_other_overhang(Env& env) {
  auto [type, _1, _2] = env.tile_info(true);
  if (type == TileType::WALL) {
    return;
  }

  auto [left_type, left_fg, left_bg] = env.tile_info(true, -1, 0);
  const auto& piece = piece_defs.at(left_type);
  switch (left_type) {
    case TileType::EMPTY:
      if (left_bg < 4) { // Background decorations
        constexpr static std::array<uint8_t, 4> image_indexes{0x00, 0x7C, 0x7D, 0x7E};
        constexpr static std::array<int8_t, 4> image_offsets{0, -20, -20, 0};
        env.draw_SHPD_image_at_anchor(6, image_indexes[left_bg], 0, Env::FLOOR_TOP_ANCHOR + image_offsets[left_bg]);
      }
      break;
    case TileType::FLOOR: {
      env.draw_SHPD_image_at_anchor(6, 0x2A, 0, Env::FLOOR_TOP_ANCHOR + piece.overhang_anchor_delta_y);
      if (left_bg > 3) {
        left_bg = 0;
      }
      // Background decorations (NOT same as in EMPTY case)
      constexpr static std::array<uint8_t, 4> image_indexes{0x2C, 0x7C, 0x2D, 0x2D};
      if (!env.is_palace) {
        if (left_bg != 0) {
          env.draw_SHPD_image_at_anchor(6, image_indexes[left_bg], 0, Env::FLOOR_TOP_ANCHOR - 0x14);
        }
      } else if (left_bg != 1) {
        env.draw_SHPD_image_at_anchor(6, image_indexes[left_bg], 0, Env::FLOOR_TOP_ANCHOR + 2);
      }
      break;
    }
    case TileType::TAPESTRY:
    case TileType::TAPESTRY_TOP:
      if (env.is_palace) { // Different tapestry types
        constexpr static std::array<uint8_t, 4> image_indexes{0x4E, 0x50, 0x52, 0x00};
        env.draw_SHPD_image_at_anchor(6, image_indexes.at(left_bg), 0, Env::FLOOR_TOP_ANCHOR + piece.overhang_anchor_delta_y);
      }
      break;
    case TileType::WALL:
      if (env.is_palace && !(left_bg & 0x80)) { // Blue stripe
        env.draw_SHPD_image_at_anchor(6, 0x54, 0x18, -0x1B);
      }
      // Wall overhang (background); note the different SHPD slot
      env.draw_SHPD_image_at_anchor(7, 1, 0, Env::FLOOR_TOP_ANCHOR + piece.overhang_anchor_delta_y);
      break;
    default:
      if (piece.right_overhang_image != 0) { // Right overhang image
        env.draw_SHPD_image_at_anchor(6, piece.right_overhang_image, 0, Env::FLOOR_TOP_ANCHOR + piece.overhang_anchor_delta_y);
      }
      if (env.is_palace) { // Blue stripe
        env.draw_SHPD_image_at_anchor(6, piece.bg_overhang_image, 0, Env::FLOOR_TOP_ANCHOR - 0x1B);
      }
      if ((left_type == TileType::TORCH) || (left_type == TileType::TORCH_WITH_DEBRIS)) { // Blue stripe with torch
        env.draw_SHPD_image_at_anchor(6, 0x92, 0, Env::FLOOR_BOTTOM_ANCHOR - 0x1C);
      }
  }
}

void draw_left_tile_animated_parts(Env& env) {
  auto [left_type, left_fg, left_bg] = env.tile_info(true, -1, 0);
  switch (left_type) {
    case TileType::SPIKES: // Spikes overhang
      static constexpr std::array<uint8_t, 10> image_indexes{
          0x00, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x89, 0x87, 0x86, 0x00};
      env.draw_SHPD_image_at_anchor(6, image_indexes.at((left_bg & 0x80) ? 5 : left_bg), 0, Env::FLOOR_TOP_ANCHOR - 7);
      break;
    case TileType::GATE: {
      uint8_t progress = std::min<uint8_t>(left_bg, 0xBC) / 4;
      if (!env.is_large) {
        progress = std::min<uint8_t>(progress, 0x2D);
      }
      ssize_t gate_anchor_y = Env::FLOOR_TOP_ANCHOR - (progress + 1);
      if (!env.is_large) {
        env.draw_SHPD_image_at_anchor(6, 0x32, 0, Env::FLOOR_TOP_ANCHOR); // Gate prongs with background bar
        env.draw_SHPD_image_at_anchor(6, 0x33, 0, gate_anchor_y); // Gate prongs; no background bar
      } else {
        if ((gate_anchor_y + 12) < Env::FLOOR_TOP_ANCHOR) {
          env.draw_SHPD_image_at_anchor(6, 0x32, 0, gate_anchor_y);
        } else {
          const auto& piece = piece_defs[4];
          env.draw_SHPD_image_at_anchor(
              6, piece.right_overhang_image, 0, Env::FLOOR_TOP_ANCHOR + piece.overhang_anchor_delta_y);
          env.draw_SHPD_image_at_anchor(6, 0x33, 0, gate_anchor_y - 2);
        }
        ssize_t delta_y = gate_anchor_y - 12;
        for (; delta_y >= Env::CELL_TOP_ANCHOR + 8; delta_y -= 8) {
          env.draw_SHPD_image_at_anchor(6, 0x34, 0, delta_y);
        }
        size_t partial_remaining = delta_y - Env::CELL_TOP_ANCHOR;
        if (partial_remaining < 8) {
          env.draw_SHPD_image_at_anchor(6, 0x3B - partial_remaining, 0, delta_y);
        }
      }
      break;
    }
    case TileType::LOOSE_FLOOR: { // Wobbling loose floor tiles
      static constexpr std::array<uint8_t, 11> image_indexes{
          0x2A, 0x47, 0x2A, 0x48, 0x48, 0x2A, 0x2A, 0x2A, 0x48, 0x48, 0x48};
      uint8_t frame = ((left_bg & 0x80) && ((left_bg & 0x7F) > 10)) ? 1 : (left_bg & 0x7F);
      env.draw_SHPD_image_at_anchor(6, image_indexes.at(frame), 0, Env::FLOOR_BOTTOM_ANCHOR - 1);
      break;
    }
    case TileType::LEVEL_DOOR_LEFT: {
      ssize_t passage_delta_y = Env::FLOOR_TOP_ANCHOR - 0x0D;
      ssize_t door_delta_y = passage_delta_y - left_bg;
      env.draw_SHPD_image_at_anchor(6, 0x63, 8, passage_delta_y); // Passage beyond level door (no stairs)
      env.draw_SHPD_image_at_anchor(6, 0x21, 8, door_delta_y); // Closed level door (clipped?)
      env.draw_SHPD_image_at_anchor(6, 0x63, 8, passage_delta_y);
      if ((env.orig_level->start_room != env.room_id) && (left_bg != 0x00)) {
        env.draw_SHPD_image_at_anchor(6, 0x90, 8, passage_delta_y); // Level exit stairs
      }
      env.draw_SHPD_image_at_anchor(6, 0x21, 8, door_delta_y); // Closed level door
      break;
    }
    case TileType::TORCH:
    case TileType::TORCH_WITH_DEBRIS: // Torch (note different SHPD slot)
      if (left_bg < 9) {
        int16_t anchor_delta_y = Env::FLOOR_TOP_ANCHOR - (env.is_palace ? 0x2A : 0x28);
        env.draw_SHPD_image_at_anchor(1, left_bg + 1, 8 + env.is_palace, anchor_delta_y);
      }
      break;
    default:
      break;
  }
}

void draw_current_tile_floor_front(Env& env) {
  auto [type, fg, bg] = env.tile_info(true);
  uint8_t shpd_slot = 6;
  uint8_t image_index;
  if ((fg & 0x80) || (type != 0x14)) {
    image_index = piece_defs.at(type).floor_front_image;
  } else {
    shpd_slot = 7;
    if (!env.is_palace) { // Bottom of wall segment
      static constexpr std::array<uint8_t, 4> image_indexes{7, 9, 5, 3};
      image_index = image_indexes.at(bg & 0x7F);
    } else {
      image_index = 3;
    }
  }
  env.draw_SHPD_image_at_anchor(shpd_slot, image_index, 0, Env::FLOOR_BOTTOM_ANCHOR);
  if (env.is_palace && (shpd_slot == 7) && env.is_color) {
    env.draw_rect_at_anchor(0, Env::FLOOR_BOTTOM_ANCHOR, 24, 4, env.current_random_data[env.tile_y][3][env.tile_x]);
    if (env.is_large) {
      PoP1RandomGenerator rand(env.room_id + (env.tile_y * 10) + env.tile_x);
      env.draw_SHPD_image_at_anchor(7, rand(2) + 0x0F, 0, Env::FLOOR_BOTTOM_ANCHOR);
    }
  }
}

void draw_current_tile_floor_front_wobble_if_loose(Env& env) {
  auto [type, fg, bg] = env.tile_info(true);
  if (type == TileType::LOOSE_FLOOR) {
    static constexpr std::array<uint8_t, 11> image_indexes{
        0x2B, 0x49, 0x2B, 0x4A, 0x4A, 0x2B, 0x2B, 0x2B, 0x4A, 0x4A, 0x4A};
    uint8_t frame = ((bg & 0x80) && ((bg & 0x7F) > 10)) ? 1 : (bg & 0x7F);
    env.draw_SHPD_image_at_anchor(6, image_indexes.at(frame), 0, Env::FLOOR_BOTTOM_ANCHOR);
  }
}

void draw_current_tile_floor_top(Env& env) {
  auto [type, fg, bg] = env.tile_info(true);
  auto [left_type, left_fg, left_bg] = env.tile_info(true, -1, 0);
  int16_t anchor_delta_y = Env::FLOOR_TOP_ANCHOR;
  uint8_t image_index;
  const auto& piece = piece_defs.at(type);
  if ((left_type == TileType::LATTICE_SUPPORT) && (type == TileType::TAPESTRY_TOP)) {
    image_index = 6;
    anchor_delta_y = Env::FLOOR_TOP_ANCHOR + 3;
  } else if (type == TileType::LOOSE_FLOOR) {
    static constexpr std::array<uint8_t, 11> image_indexes{
        0x29, 0x45, 0x29, 0x46, 0x46, 0x29, 0x29, 0x29, 0x46, 0x46, 0x46};
    uint8_t frame = ((bg & 0x80) && ((bg & 0x7F) > 10)) ? 1 : (bg & 0x7F);
    image_index = image_indexes[frame];
  } else if ((type == TileType::RAISE_TILE_2) && (left_type == TileType::EMPTY)) {
    image_index = 0x94;
  } else {
    image_index = piece.floor_top_image;
  }
  env.draw_SHPD_image_at_anchor(6, image_index, 0, anchor_delta_y + piece.floor_top_anchor_delta_y);
}

uint8_t get_potion_bubble_image_index(const Env& env, uint8_t bg) {
  static constexpr std::array<uint8_t, 8> base_indexes{0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x00};
  static constexpr std::array<uint8_t, 8> offsets{0, 0, 0, 6, 6, 12, 12, 0};

  uint8_t potion_type = bg & 0xE0;
  if ((potion_type == 0) || (potion_type == 0x40)) {
    return 0;
  }

  // Note: The game uses (bg & 0x0F) here; we use 5 so the bubbles will be obvious on the map
  uint8_t ret = base_indexes.at(5);
  return ret + (((ret != 0) && env.is_color) ? offsets[potion_type >> 5] : 0);
}

void draw_current_tile_animated_parts(Env& env) {
  auto [type, fg, bg] = env.tile_info(true);
  switch (type) {
    case TileType::SPIKES: {
      static constexpr std::array<uint8_t, 10> image_indexes{
          0x00, 0x80, 0x81, 0x82, 0x83, 0x84, 0x83, 0x81, 0x80, 0x00};
      env.draw_SHPD_image_at_anchor(6, image_indexes.at((bg & 0x80) ? 5 : bg), 0, Env::FLOOR_TOP_ANCHOR - 2);
      break;
    }
    case TileType::POTION:
      env.draw_SHPD_image_at_anchor(1, get_potion_bubble_image_index(env, bg), 25, Env::FLOOR_TOP_ANCHOR - 14);
      break;
    case TileType::CHOMPER: {
      static constexpr std::array<uint8_t, 7> visual_frame_for_action_frame{0x03, 0x02, 0x00, 0x01, 0x04, 0x03, 0x03};
      static constexpr std::array<uint8_t, 5> bottom_half_image_indexes{0x65, 0x66, 0x67, 0x68, 0x69};
      static constexpr std::array<uint8_t, 5> top_half_image_indexes{0x00, 0x00, 0x6F, 0x70, 0x71};
      static constexpr std::array<uint8_t, 5> top_half_anchor_deltas_y{0x00, 0x00, 0x25, 0x2F, 0x32};
      uint8_t visual_frame = visual_frame_for_action_frame[std::min<uint8_t>(bg & 0x7F, 6)];
      env.draw_SHPD_image_at_anchor(6, bottom_half_image_indexes[visual_frame], 0, Env::FLOOR_TOP_ANCHOR);
      env.draw_SHPD_image_at_anchor(6, top_half_image_indexes[visual_frame], 0, Env::FLOOR_TOP_ANCHOR - top_half_anchor_deltas_y[visual_frame]);
      if (bg & 0x80) {
        env.draw_SHPD_image_at_anchor(6, visual_frame + 0x72, 12, Env::FLOOR_TOP_ANCHOR - 6);
      }
      break;
    }
    case TileType::SWORD:
      env.draw_SHPD_image_at_anchor(1, 10 + (bg == 1), 0, Env::FLOOR_TOP_ANCHOR - 3);
      break;
    default:
      break;
  }
}

std::array<std::array<std::array<uint8_t, 11>, 4>, 3> generate_palace_wall_random_data(uint8_t room_id) {
  PoP1RandomGenerator rand(room_id + 1);
  rand(1);
  std::array<std::array<std::array<uint8_t, 11>, 4>, 3> ret;
  for (uint8_t tile_row = 0; tile_row < 3; tile_row++) {
    auto& tile_row_data = ret[tile_row];
    for (uint8_t block_row = 0; block_row < 4; block_row++) {
      auto& block_row_data = tile_row_data[block_row];
      uint8_t prev = 0xFF;
      for (uint8_t x = 0; x < 11; x++) {
        uint8_t value;
        do {
          value = 0x4C + ((block_row & 1) * 4) + rand(3);
        } while (prev == value);
        block_row_data[x] = value;
        prev = value;
      }
    }
  }

  return ret;
}

void draw_current_tile_foreground_parts(Env& env) {
  auto [type, fg, bg] = env.tile_info(true);

  // Note: The original code re-renders gate bars here if the left tile is a gate and the Prince is standing behind it;
  // presumably this is to make the gate appear in front of (on top of) the Prince in that scenario

  const auto& piece = piece_defs.at(type);
  switch (type) {
    case TileType::SPIKES: {
      static constexpr std::array<uint8_t, 10> image_indexes{
          0x00, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x8E, 0x8C, 0x8B, 0x00};
      env.draw_SHPD_image_at_anchor(6, image_indexes.at((bg & 0x80) ? 5 : bg), 0, Env::FLOOR_TOP_ANCHOR - 2);
      break;
    }
    case TileType::POTION: {
      uint8_t image_index = piece.fg_image + (2 * env.is_palace) + ((bg & 0xE0) == 0x40);
      env.draw_SHPD_image_at_anchor(1, image_index, (piece.fg_anchor_delta_x_cells * 8) + 6, Env::FLOOR_TOP_ANCHOR + piece.fg_anchor_delta_y);
      env.draw_SHPD_image_at_anchor(1, get_potion_bubble_image_index(env, bg), 25, Env::FLOOR_TOP_ANCHOR - 0x0E);
      break;
    }
    // Note: the original code has a case for TileType::DEBRIS here. That case sets a global which is presumably not
    // needed for rendering maps, then does the same thing as the default case.
    case TileType::CHOMPER: {
      static constexpr std::array<uint8_t, 7> visual_frame_for_action_frame{0x03, 0x02, 0x00, 0x01, 0x04, 0x03, 0x03};
      static constexpr std::array<uint8_t, 5> image_index_for_visual_frame{0x6A, 0x6B, 0x6C, 0x6D, 0x6E};
      uint8_t visual_frame = visual_frame_for_action_frame[std::min<uint8_t>(bg & 0x7F, 6)];
      env.draw_SHPD_image_at_anchor(6, image_index_for_visual_frame[visual_frame], 0, Env::FLOOR_TOP_ANCHOR);
      if (bg & 0x80) {
        env.draw_SHPD_image_at_anchor(6, visual_frame + 0x77, 12, Env::FLOOR_TOP_ANCHOR - 6);
      }
      break;
    }
    case TileType::WALL:
      if (env.is_palace) {
        PoP1RandomGenerator rand(env.room_id + 1 + (env.tile_y * 10) + env.tile_x);

        if (env.is_color) {
          env.draw_rect_at_anchor(0, Env::FLOOR_TOP_ANCHOR - 0x28, 32, 20, env.current_random_data[env.tile_y][0][env.tile_x]);
          env.draw_rect_at_anchor(0, Env::FLOOR_TOP_ANCHOR - 0x13, 16, 21, env.current_random_data[env.tile_y][1][env.tile_x]);
          env.draw_rect_at_anchor(16, Env::FLOOR_TOP_ANCHOR - 0x13, 16, 21, env.current_random_data[env.tile_y][1][env.tile_x + 1]);
          env.draw_rect_at_anchor(0, Env::FLOOR_TOP_ANCHOR, 8, 19, env.current_random_data[env.tile_y][2][env.tile_x]);
          env.draw_rect_at_anchor(8, Env::FLOOR_TOP_ANCHOR, 24, 19, env.current_random_data[env.tile_y][2][env.tile_x + 1]);
          env.draw_rect_at_anchor(0, Env::FLOOR_BOTTOM_ANCHOR, 32, 3, env.current_random_data[env.tile_y][3][env.tile_x]);
          if (env.is_large) { // Draw the crack overlays
            env.draw_SHPD_image_at_anchor(7, rand(2) + 0x0F, 0, Env::FLOOR_BOTTOM_ANCHOR);
            env.draw_SHPD_image_at_anchor(7, rand(2) + 0x03, 24, Env::FLOOR_TOP_ANCHOR + -0x35);
            env.draw_SHPD_image_at_anchor(7, rand(2) + 0x06, 0, Env::FLOOR_TOP_ANCHOR + -0x22);
            env.draw_SHPD_image_at_anchor(7, rand(2) + 0x09, 0, Env::FLOOR_TOP_ANCHOR + -0x0D);
            env.draw_SHPD_image_at_anchor(7, rand(2) + 0x0C, 0, Env::FLOOR_TOP_ANCHOR);
          }
        }
        if (!env.is_large) {
          env.draw_SHPD_image_at_anchor(7, 4, 0, Env::FLOOR_TOP_ANCHOR);
        }

      } else { // Dungeon walls
        static constexpr std::array<uint8_t, 4> image_indexes{0x08, 0x0A, 0x06, 0x04};
        env.draw_SHPD_image_at_anchor(7, image_indexes.at(bg & 0x7F), 0, Env::FLOOR_TOP_ANCHOR);
      }
      break;
    default:
      if (piece.fg_image != 0) {
        env.draw_SHPD_image_at_anchor(6, piece.fg_image, piece.fg_anchor_delta_x_cells * 8, Env::FLOOR_TOP_ANCHOR + piece.fg_anchor_delta_y);
      }
  }
}

void draw_tile(Env& env) {
  // This implementation mirrors what the game does internally
  draw_left_tile_floor_overhang(env);
  draw_below_tile_gate_top(env);
  draw_left_tile_other_overhang(env);
  draw_left_tile_animated_parts(env);
  draw_current_tile_floor_front(env);
  draw_current_tile_floor_front_wobble_if_loose(env);
  draw_current_tile_floor_top(env);
  draw_current_tile_animated_parts(env);
  draw_current_tile_foreground_parts(env);
}

void draw_overlay_tile(Env& env, bool is_error, uint8_t overlay_alpha, uint8_t annotation_alpha) {
  if (env.tile_x < 0 || env.tile_x > 9 || env.tile_y < 0 || env.tile_y > 2) {
    return;
  }

  auto tile_view = env.map.view(env.tile_x * env.tile_w(), env.tile_y * env.tile_h(), env.tile_w(), env.tile_h());
  auto [type, fg, bg] = env.tile_info(false);

  if (!is_error && (overlay_alpha > 0)) {
    switch (type) {
      case TileType::EMPTY:
        break;
      case TileType::FLOOR:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        break;
      case TileType::SPIKES:
        tile_view.blend_rect(env.tile_w() / 2 - 2, (env.tile_h() * 5) / 6, 4, env.tile_h() / 6, 0xFF000000 | overlay_alpha);
        tile_view.blend_rect(env.tile_w() / 2 - 10, (env.tile_h() * 5) / 6, 4, env.tile_h() / 6, 0xFF000000 | overlay_alpha);
        tile_view.blend_rect(env.tile_w() / 2 + 6, (env.tile_h() * 5) / 6, 4, env.tile_h() / 6, 0xFF000000 | overlay_alpha);
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        break;
      case TileType::SMALL_PILLAR:
      case TileType::LATTICE_PILLAR:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        tile_view.blend_rect(3 * env.tile_w() / 8, 0, env.tile_w() / 4, env.tile_h() - 6,
            ((type == TileType::LATTICE_PILLAR) ? 0xC0A000000 : 0xC0C0C000) | overlay_alpha);
        break;
      case TileType::GATE:
        tile_view.blend_rect(env.tile_w() - 3, 0, 3, env.tile_h() - 6, 0x0080FF00 | overlay_alpha);
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        break;
      case TileType::RAISE_TILE_1:
      case TileType::RAISE_TILE_2:
        tile_view.blend_rect(0, env.tile_h() - 8, env.tile_w(), 6, 0x00800000 | overlay_alpha);
        break;
      case TileType::DROP_TILE:
        tile_view.blend_rect(0, env.tile_h() - 8, env.tile_w(), 6, 0x80000000 | overlay_alpha);
        break;
      case TileType::TAPESTRY:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        tile_view.blend_rect(
            env.tile_w() - 6, 0, env.tile_w(), env.tile_h() - 6, 0x20202000 | overlay_alpha);
        break;
      case TileType::LARGE_PILLAR_BOTTOM:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        tile_view.blend_rect(env.tile_w() / 3, 0, env.tile_w() / 3, env.tile_h() - 6, 0xC0C0C000 | overlay_alpha);
        break;
      case TileType::LARGE_PILLAR_TOP:
        tile_view.blend_rect(env.tile_w() / 3, 0, env.tile_w() / 3, env.tile_h(), 0xC0C0C000 | overlay_alpha);
        break;
      case TileType::POTION:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        tile_view.blend_rect(0 + 3 * env.tile_w() / 8, env.tile_h() - env.tile_w() / 4 - 8,
            env.tile_w() / 4, env.tile_w() / 4, 0xFF800000 | overlay_alpha);
        break;
      case TileType::LOOSE_FLOOR:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00808000 | overlay_alpha);
        break;
      case TileType::TAPESTRY_TOP:
        tile_view.blend_rect(
            env.tile_w() - 6, 0, env.tile_w(), env.tile_h(), 0x20202000 | overlay_alpha);
        break;
      case TileType::MIRROR:
        tile_view.blend_rect(0, 0, 3, env.tile_h() - 6, 0x00FF4000 | overlay_alpha);
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        break;
      case TileType::DEBRIS:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        tile_view.blend_rect(env.tile_w() / 4, env.tile_h() - 8, env.tile_w() / 2, 2, 0x40404000 | overlay_alpha);
        break;
      case TileType::LEVEL_DOOR_LEFT:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        tile_view.draw_vertical_line(20, 20, env.tile_h() - 6, 0, 0x00800000 | overlay_alpha);
        tile_view.draw_vertical_line(21, 21, env.tile_h() - 6, 0, 0x00800000 | overlay_alpha);
        tile_view.draw_horizontal_line(0 + 20, 0 + env.tile_w(), 20, 0, 0x00800000 | overlay_alpha);
        tile_view.draw_horizontal_line(0 + 20, 0 + env.tile_w(), 21, 0, 0x00800000 | overlay_alpha);
        break;
      case TileType::LEVEL_DOOR_RIGHT:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        tile_view.draw_vertical_line(0 + env.tile_w() - 20, 20, env.tile_h() - 6, 0, 0x00800000 | overlay_alpha);
        tile_view.draw_vertical_line(0 + env.tile_w() - 21, 21, env.tile_h() - 6, 0, 0x00800000 | overlay_alpha);
        tile_view.draw_horizontal_line(0, 0 + env.tile_w() - 20, 20, 0, 0x00800000 | overlay_alpha);
        tile_view.draw_horizontal_line(0, 0 + env.tile_w() - 20, 21, 0, 0x00800000 | overlay_alpha);
        break;
      case TileType::BALCONY_LEFT:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        tile_view.blend_rect(env.tile_w() / 4, 0, (env.tile_w() * 3) / 4, env.tile_h(), 0x00000000 | overlay_alpha);
        break;
      case TileType::BALCONY_RIGHT:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        tile_view.blend_rect(0, 0, (env.tile_w() * 3) / 4, env.tile_h(), 0x00000000 | overlay_alpha);
        break;
      case TileType::CHOMPER:
        tile_view.blend_rect(0, 0, 3, env.tile_h() / 3, 0xFF000000 | overlay_alpha);
        tile_view.blend_rect(0, (env.tile_h() * 2) / 3, 3, env.tile_h() / 3, 0xFF000000 | overlay_alpha);
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        break;
      case TileType::TORCH:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        tile_view.blend_rect(0 + 3 * env.tile_w() / 8, env.tile_h() / 3,
            env.tile_w() / 4, env.tile_h() / 3, 0xFFC08000 | overlay_alpha);
        break;
      case TileType::TORCH_WITH_DEBRIS:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        tile_view.blend_rect(0 + 3 * env.tile_w() / 8, env.tile_h() / 3,
            env.tile_w() / 4, env.tile_h() / 3, 0xFFC08000 | overlay_alpha);
        tile_view.blend_rect(env.tile_w() / 4, env.tile_h() - 8, env.tile_w() / 2, 2, 0x40404000 | overlay_alpha);
        break;
      case TileType::WALL:
        tile_view.blend_rect(0, 0, env.tile_w(), env.tile_h(), 0x80808000 | overlay_alpha);
        break;
      case TileType::SKELETON:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        tile_view.blend_rect(env.tile_w() / 4, env.tile_h() - 8, env.tile_w() / 2, 2, 0x815D1C00 | overlay_alpha);
        break;
      case TileType::SWORD:
        tile_view.blend_rect(0, env.tile_h() - 6, env.tile_w(), 6, 0x00000000 | overlay_alpha);
        tile_view.blend_rect(env.tile_w() / 4, env.tile_h() - 12, env.tile_w() / 2, 2, 0x00008000 | overlay_alpha);
        break;
      case TileType::LATTICE_SUPPORT: {
        phosg::ImageRGBA8888N overlay(tile_view.get_width(), tile_view.get_height(), 0x00000000);
        overlay.draw_line(0, (2 * env.tile_h()) / 3, env.tile_w() / 2, env.tile_h(), 0xC0A00000 | overlay_alpha);
        overlay.draw_line(env.tile_w(), (2 * env.tile_h()) / 3, env.tile_w() / 2, env.tile_h(), 0xC0A00000 | overlay_alpha);
        overlay.flood_fill(1, 1, 0xC0A00000 | overlay_alpha);
        tile_view.copy_from_with_blend(overlay, 0, 0, tile_view.get_width(), tile_view.get_height(), 0, 0);
        break;
      }
      case TileType::SMALL_LATTICE: {
        phosg::ImageRGBA8888N overlay(tile_view.get_width(), tile_view.get_height(), 0x00000000);
        overlay.draw_line(0, (2 * env.tile_h()) / 3, env.tile_w() / 2, env.tile_h() / 2, 0xC0A00000 | overlay_alpha);
        overlay.draw_line(env.tile_w(), (2 * env.tile_h()) / 3, env.tile_w() / 2, env.tile_h() / 2, 0xC0A00000 | overlay_alpha);
        overlay.flood_fill(1, 1, 0xC0A00000 | overlay_alpha);
        tile_view.copy_from_with_blend(overlay, 0, 0, tile_view.get_width(), tile_view.get_height(), 0, 0);
        break;
      }
      case TileType::LATTICE_LEFT: {
        phosg::ImageRGBA8888N overlay(tile_view.get_width(), tile_view.get_height(), 0x00000000);
        overlay.draw_line(0, (2 * env.tile_h()) / 3, env.tile_w(), env.tile_h() / 2, 0xC0A00000 | overlay_alpha);
        overlay.flood_fill(1, 1, 0xC0A00000 | overlay_alpha);
        tile_view.copy_from_with_blend(overlay, 0, 0, tile_view.get_width(), tile_view.get_height(), 0, 0);
        break;
      }
      case TileType::LATTICE_RIGHT: {
        phosg::ImageRGBA8888N overlay(tile_view.get_width(), tile_view.get_height(), 0x00000000);
        overlay.draw_line(0, env.tile_h() / 2, env.tile_w(), (2 * env.tile_h()) / 3, 0xC0A00000 | overlay_alpha);
        overlay.flood_fill(1, 1, 0xC0A00000 | overlay_alpha);
        tile_view.copy_from_with_blend(overlay, 0, 0, tile_view.get_width(), tile_view.get_height(), 0, 0);
        break;
      }
      default:
        is_error = true;
    }
  }
  uint32_t annotation_color = 0xFFFFFF00 | annotation_alpha;
  if (is_error) {
    for (size_t y = 0; y < tile_view.get_height(); y++) {
      for (size_t x = 0; x < tile_view.get_width(); x++) {
        tile_view.write(x, y, phosg::alpha_blend(tile_view.read(x, y), (((x + y) & 8) ? 0x80000000 : 0x00000000) | overlay_alpha));
      }
    }
    annotation_color = 0xFF0000FF;
  }
  tile_view.draw_text(1, 1, annotation_color, overlay_alpha, "{:02X}\n{:02X}", fg, bg);
}

struct RoomGraph {
  struct Component {
    uint8_t component_id;
    std::unordered_map<uint8_t, std::pair<int8_t, int8_t>> placement_map;
  };

  const PrinceOfPersiaLevel* level;
  std::vector<Component> components;
  std::unordered_map<uint32_t, uint8_t> room_id_for_placement;

  uint32_t remaining_room_ids = 0x00FFFFFF;

  static uint32_t placement_key(uint8_t component_id, int8_t x, int8_t y) {
    return (component_id << 16) | (static_cast<uint8_t>(x) << 8) | static_cast<uint8_t>(y);
  }

  bool room_is_remaining(uint8_t room_id) const {
    return this->remaining_room_ids & (1 << room_id);
  }
  void mark_room_done(uint8_t room_id) {
    this->remaining_room_ids &= ~(1 << room_id);
  }

  void place_room(Component& component, uint8_t room_id, int8_t x, int8_t y) {
    uint32_t placement_key = this->placement_key(component.component_id, x, y);

    if (this->room_is_remaining(room_id)) {
      this->mark_room_done(room_id);

      component.placement_map.emplace(room_id, std::make_pair(x, y));
      this->room_id_for_placement.emplace(placement_key, room_id);

      const auto& links = this->level->room_links[room_id];
      if (links.left && (links.left <= 24)) {
        this->place_room(component, links.left - 1, x - 1, y);
      }
      if (links.right && (links.right <= 24)) {
        this->place_room(component, links.right - 1, x + 1, y);
      }
      if (links.above && (links.above <= 24)) {
        this->place_room(component, links.above - 1, x, y - 1);
      }
      if (links.below && (links.below <= 24)) {
        this->place_room(component, links.below - 1, x, y + 1);
      }

    } else {
      // If the room was already placed, ensure the room's existing placement matches the one we're supposed to add
      auto it = this->room_id_for_placement.find(placement_key);
      if (it == this->room_id_for_placement.end() || it->second != room_id) {
        phosg::log_warning_f("Warning: bad backlink at room {}", room_id);
      }
    }
  }

  void place_component(uint8_t start_room_id) {
    auto& component = this->components.emplace_back();
    component.component_id = this->components.size() - 1;
    this->place_room(component, start_room_id, 0, 0);
    if (component.placement_map.empty()) {
      components.pop_back(); // Room was already placed in another component
    } else {
      // Make all offsets nonnegative
      int16_t delta_x = 0, delta_y = 0;
      for (const auto& it : component.placement_map) {
        if (it.second.first < delta_x) {
          delta_x = it.second.first;
        }
        if (it.second.second < delta_y) {
          delta_y = it.second.second;
        }
      }
      for (auto& [room_id, placement] : component.placement_map) {
        this->room_id_for_placement.erase(this->placement_key(component.component_id, placement.first, placement.second));
        placement.first -= delta_x;
        placement.second -= delta_y;
        this->room_id_for_placement.emplace(this->placement_key(component.component_id, placement.first, placement.second), room_id);
      }
    }
  };

  RoomGraph(const PrinceOfPersiaLevel* level, int8_t room_id = -1) : level(level) {
    if (room_id >= 0) {
      auto& component = this->components.emplace_back();
      component.component_id = 0;
      component.placement_map.emplace(room_id, std::make_pair(0, 0));
      uint32_t placement_key = this->placement_key(component.component_id, 0, 0);
      this->room_id_for_placement.emplace(placement_key, room_id);
      this->remaining_room_ids = 0;
    } else {
      this->place_component(level->start_room - 1);
      for (uint8_t room_id = 0; room_id < 24; room_id++) {
        if (this->room_is_remaining(room_id)) {
          this->place_component(room_id);
          if (this->room_is_remaining(room_id)) {
            throw std::logic_error("room ID present in remaining set after processing");
          }
        }
      }
    }
  }
};

int main(int argc, char** argv) {
  phosg::Arguments args(argv + 1, argc - 1);

  const auto& data_dir = args.get<std::string>(0);
  std::string output_dir = args.get<std::string>(1, false);
  if (output_dir.empty()) {
    output_dir = ".";
  }
  uint8_t overlay_alpha = args.get<uint8_t>("overlay-alpha", 0x40);
  uint8_t annotation_alpha = args.get<uint8_t>("annotation-alpha", 0x80);
  bool render_all_components = !args.get<bool>("first-component-only");
  bool use_bw_graphics = args.get<bool>("bw");
  bool use_lc_graphics = args.get<bool>("lc");
  int32_t single_level_id = args.get<int32_t>("level", 0xFFFF);
  int8_t single_room_id = args.get<uint8_t>("room", -1);
  args.assert_none_unused();

  bool is_large = !use_bw_graphics && !use_lc_graphics;
  const char* persia_filename;
  if (use_bw_graphics) {
    persia_filename = "Persia(BW)";
  } else if (use_lc_graphics) {
    persia_filename = "Persia(LC)";
  } else {
    persia_filename = "Persia(COLOR)";
  }
  auto game_rf = ResourceDASM::parse_resource_fork(phosg::load_file(std::format(
      "{}/Prince of Persia/..namedfork/rsrc", data_dir)));
  auto persia_rf = ResourceDASM::parse_resource_fork(phosg::load_file(std::format(
      "{}/{}/..namedfork/rsrc", data_dir, persia_filename)));
  auto data_fork_contents = phosg::load_file(std::format("{}/{}", data_dir, persia_filename));

  std::vector<ResourceDASM::ColorTableEntry> base_clut;
  if (!use_bw_graphics) {
    base_clut = game_rf.decode_clut(2000);
  }

  Graphics graphics;
  graphics.slots[0] = ResourceDASM::decode_SHPD(
      persia_rf, data_fork_contents, 700, base_clut, ResourceDASM::SHPDVersion::PRINCE_OF_PERSIA);
  graphics.slots[1] = ResourceDASM::decode_SHPD(
      persia_rf, data_fork_contents, 150, base_clut, ResourceDASM::SHPDVersion::PRINCE_OF_PERSIA);
  graphics.slots[2] = ResourceDASM::decode_SHPD(
      persia_rf, data_fork_contents, 400, base_clut, ResourceDASM::SHPDVersion::PRINCE_OF_PERSIA);
  // 3 and 4 are titles; not relevant for drawing maps
  // 5 is guards; we should load them with the appropriate clut when guards are implemented here
  graphics.slots[6] = ResourceDASM::decode_SHPD(
      persia_rf, data_fork_contents, 1200, base_clut, ResourceDASM::SHPDVersion::PRINCE_OF_PERSIA);
  graphics.slots[7] = ResourceDASM::decode_SHPD(
      persia_rf, data_fork_contents, 1360, base_clut, ResourceDASM::SHPDVersion::PRINCE_OF_PERSIA);
  auto other_tileset = ResourceDASM::decode_SHPD(
      persia_rf, data_fork_contents, 2200, base_clut, ResourceDASM::SHPDVersion::PRINCE_OF_PERSIA);
  auto other_wall_tileset = ResourceDASM::decode_SHPD(
      persia_rf, data_fork_contents, 2360, base_clut, ResourceDASM::SHPDVersion::PRINCE_OF_PERSIA);
  bool current_tileset_is_palace = false;

  for (const auto& res_id : game_rf.all_resources_of_type(RESOURCE_TYPE_LEVL)) {
    if ((single_level_id >= -0x8000) && (single_level_id < 0x8000) && (res_id != single_level_id)) {
      phosg::log_info_f("Skipping LEVL:{} because it was not specified", res_id);
      continue;
    }

    auto res = game_rf.get_resource(RESOURCE_TYPE_LEVL, res_id);
    if (res->data.size() != sizeof(PrinceOfPersiaLevel)) {
      throw std::runtime_error(std::format("invalid LEVL resource with ID {}", res_id));
    }
    const auto* orig_level = reinterpret_cast<const PrinceOfPersiaLevel*>(res->data.data());
    PrinceOfPersiaLevel preprocessed_level = *orig_level;
    preprocessed_level.preprocess();
    bool is_palace = 0x0E32 & (0x8000 >> res_id);
    if (current_tileset_is_palace != is_palace) {
      graphics.slots[6].swap(other_tileset);
      graphics.slots[7].swap(other_wall_tileset);
      current_tileset_is_palace = is_palace;
    }

    RoomGraph room_graph(orig_level, single_room_id);
    for (const auto& component : room_graph.components) {
      if (component.component_id && !render_all_components) {
        break;
      }

      size_t w_rooms = 0, h_rooms = 0;
      for (const auto& [_, placement] : component.placement_map) {
        w_rooms = std::max<size_t>(placement.first + 1, w_rooms);
        h_rooms = std::max<size_t>(placement.second + 1, h_rooms);
      }

      Env env(&base_clut, &graphics, orig_level, &preprocessed_level, w_rooms, h_rooms, !use_bw_graphics, is_large, is_palace);

      for (const auto& [room_id, placement] : component.placement_map) {
        size_t room_x = placement.first * (env.tile_w() * 10);
        size_t room_y = placement.second * (env.tile_h() * 3);
        auto room_env = env.view(room_x, room_y, env.tile_w() * 10, env.tile_h() * 3);
        room_env.room_id = room_id;
        room_env.current_random_data = generate_palace_wall_random_data(env.room_id);

        // Tiles are drawn left to right, bottom to top; we draw the bottom/left edge tiles first
        std::array<std::array<bool, 10>, 3> tile_errors{};
        room_env.map.clear(0x000000FF);
        for (room_env.tile_y = 2; room_env.tile_y >= 0; room_env.tile_y--) {
          for (room_env.tile_x = 0; room_env.tile_x < 10; room_env.tile_x++) {
            try {
              draw_tile(room_env);
            } catch (const std::invalid_argument&) {
              if (room_env.tile_x >= 0 && room_env.tile_x < 10 && room_env.tile_y >= 0 && room_env.tile_y < 3) {
                tile_errors[room_env.tile_y][room_env.tile_x] = true;
              }
            }
          }
        }

        if (overlay_alpha > 0) {
          for (room_env.tile_y = 0; room_env.tile_y < 3; room_env.tile_y++) {
            for (room_env.tile_x = 0; room_env.tile_x < 10; room_env.tile_x++) {
              draw_overlay_tile(room_env, tile_errors[room_env.tile_y][room_env.tile_x], overlay_alpha, annotation_alpha);
            }
          }
          room_env.map.draw_text(26, 1, 0xFF00FF00 | annotation_alpha, overlay_alpha, "RM{:02X}", room_env.room_id + 1);
        }
      }

      std::string filename = std::format("{}/pop_level{}_part{}.png", output_dir, res_id, component.component_id);
      phosg::save_file(filename, env.map.serialize(phosg::ImageFormat::PNG));
      phosg::log_info_f("... {}", filename);
    }
  }

  return 0;
}

// TODO(DX): Probably we should have a way to render bad backlinks...
// TODO(DX): Some gate tops don't render properly in LC; see most gates in level 4
