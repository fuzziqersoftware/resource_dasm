#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
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

struct SpritePictDefinition {
  size_t x_segments;
  size_t y_segments;
};

static const SpritePictDefinition default_sprite_pict_def = {1, 1};

static const unordered_map<int16_t, SpritePictDefinition> sprite_pict_defs({
    {328, {1, 6}},
    {428, {4, 2}},
    {528, {7, 1}},
    {650, {15, 2}},
    {652, {15, 2}},
    {658, {15, 2}},
    {700, {27, 1}},
    {701, {27, 2}},
    {702, {27, 1}},
    {703, {27, 2}},
    {711, {1, 3}},
    {1003, {4, 1}},
    {1009, {2, 2}},
    {1010, {10, 1}},
    {1011, {4, 1}},
    {1012, {6, 1}},
    {1013, {4, 1}},
    {1014, {6, 1}},
    {1015, {4, 2}},
    {1016, {7, 1}},
    {1017, {4, 2}},
    {1020, {4, 4}},
    {1021, {5, 1}},
    {1022, {10, 1}},
    {1023, {10, 1}},
    {1024, {4, 3}},
    {1025, {8, 1}},
    {1026, {4, 2}},
    {1027, {6, 1}},
    {1028, {10, 1}},
    {1029, {6, 1}},
    {1030, {3, 1}},
    {1031, {6, 1}},
    {1032, {6, 1}},
    {1033, {6, 1}},
    {1034, {5, 1}},
    {1035, {5, 1}},
    {1036, {6, 1}},
    {1037, {4, 1}},
    {1038, {4, 1}},
    {1039, {9, 1}},
    {1040, {4, 1}},
    {1050, {3, 2}},
    {1051, {4, 1}},
    {1052, {3, 2}},
    {1053, {4, 1}},
    {1054, {4, 1}},
    {1055, {12, 1}},
    {1057, {10, 1}},
    {1058, {10, 1}},
    {1059, {10, 1}},
    {1065, {3, 1}},
    {1080, {1, 6}},
    {1090, {5, 1}},
    {1100, {1, 6}},
    {1101, {1, 6}},
    {1102, {1, 6}},
    {1103, {1, 6}},
    {1104, {1, 6}},
    {1105, {1, 6}},
    {1106, {1, 9}},
    {1107, {1, 9}},
    {1108, {1, 6}},
    {1109, {1, 6}},
    {1110, {1, 6}},
    {1111, {1, 6}},
    {1112, {1, 6}},
    {1113, {6, 1}},
    {1114, {10, 1}},
    {1115, {6, 1}},
    {1116, {8, 1}},
    {1117, {15, 1}},
    {1130, {6, 1}},
    {1131, {6, 1}},
    {1132, {6, 1}},
    {1133, {6, 1}},
    {1135, {6, 1}},
    {1139, {6, 1}},
    {1150, {4, 1}},
    {1151, {4, 1}},
    {1152, {4, 1}},
    {1154, {4, 1}},
    {1200, {13, 1}},
    {1201, {6, 1}},
    {1205, {34, 1}},
    {1206, {34, 1}},
    {1207, {13, 1}},
    {1208, {16, 1}},
    {1209, {16, 1}},
    {1210, {3, 1}},
    {1211, {1, 16}},
    {1212, {16, 1}},
    {1215, {4, 1}},
    {1220, {1, 11}},
    {1250, {7, 3}},
    {1251, {6, 1}},
    {1300, {6, 1}},
    {1301, {6, 1}},
    {1302, {9, 1}},
    {1307, {6, 1}},
    {1308, {4, 1}},
    {1309, {9, 1}},
    {1310, {9, 1}},
    {1320, {6, 1}},
    {1321, {6, 1}},
    {1322, {8, 1}},
    {1340, {6, 1}},
    {1341, {6, 1}},
    {1400, {10, 1}},
    {1410, {1, 7}},
    {1433, {10, 6}},
    {1435, {1, 45}},
    {1440, {4, 3}},
    {1441, {6, 1}},
    {1442, {6, 1}},
    {1450, {4, 1}},
    {1470, {4, 1}},
    {1600, {4, 1}},
    {1701, {4, 2}},
    {1702, {6, 1}},
    {1703, {4, 2}},
    {1704, {1, 8}},
    {1705, {1, 3}},
    {1706, {1, 2}},
    {1707, {1, 9}},
    {1710, {6, 1}},
    {1711, {6, 1}},
    {1713, {6, 1}},
    {1720, {6, 1}},
    {1721, {6, 1}},
    {1730, {16, 1}},
    {1740, {11, 1}},
    {1751, {1, 8}},
    {1752, {1, 6}},
    {1753, {1, 3}},
    {1754, {1, 6}},
    {1760, {9, 1}},
    {1761, {8, 1}},
    {1762, {8, 1}},
    {1766, {8, 1}},
    {1770, {10, 1}},
    {1771, {10, 1}},
    {1772, {4, 1}},
    {1780, {6, 1}},
    {1800, {8, 1}},
    {1810, {9, 1}},
    {1820, {8, 1}},
    {1821, {8, 1}},
    {1822, {6, 1}},
    {1823, {8, 2}},
    {1831, {8, 1}},
    {1832, {8, 1}},
    {1840, {12, 1}},
    {1850, {12, 1}},
    {1851, {12, 1}},
    {1860, {9, 1}},
    {1869, {8, 1}},
    {1870, {6, 1}},
    {1871, {5, 1}},
    {1872, {5, 1}},
    {1873, {2, 1}},
    {1876, {8, 1}},
    {1880, {8, 1}},
    {1881, {7, 1}},
    {1890, {6, 1}},
    {1892, {6, 1}},
    {1900, {6, 1}},
    {1902, {6, 1}},
    {1903, {6, 1}},
    {1911, {4, 1}},
    {1912, {4, 1}},
    {1913, {4, 1}},
    {1914, {4, 1}},
    {1915, {4, 4}},
    {1920, {3, 2}},
    {1921, {3, 2}},
    {1922, {3, 1}},
    {1923, {4, 1}},
    {1924, {2, 1}},
    {1928, {3, 1}},
    {1929, {3, 1}},
    {1970, {2, 2}},
    {1971, {2, 2}},
    {1972, {2, 1}},
    {1973, {2, 2}},
    {1974, {2, 1}},
    {1975, {2, 2}},
    {1976, {2, 2}},
    {1977, {2, 2}},
    {1980, {2, 2}},
    {1981, {2, 2}},
    {1982, {2, 1}},
    {1983, {2, 2}},
    {1984, {2, 1}},
    {1985, {2, 2}},
    {1986, {2, 2}},
    {1987, {2, 2}},
    {1990, {10, 1}},
    {1991, {6, 1}},
    {1992, {11, 1}},
    {1995, {10, 1}},
    {1996, {6, 1}},
    {1997, {11, 1}},
    {2801, {6, 1}},
    {2910, {6, 1}},
    {2915, {6, 1}},
    {2929, {2, 1}},
    {2930, {7, 2}},
    {2931, {7, 3}},
    {2932, {12, 1}},
    {2933, {12, 2}},
    {3099, {8, 2}},
    {8001, {8, 1}},
    {8002, {8, 1}},
    {8004, {8, 1}},
    {10200, {4, 4}},
    {13070, {6, 1}},
});

struct SpriteDefinition {
  int16_t pict_id;
  int16_t segment_number; // Reading order; all y=0 segments before y=1 segments
  bool reverse_horizontal;
  bool is_overlay;

  SpriteDefinition(
      int16_t pict_id = 0,
      int16_t segment_number = 0,
      bool reverse_horizontal = false,
      bool is_overlay = false)
      : pict_id(pict_id),
        segment_number(segment_number),
        reverse_horizontal(reverse_horizontal),
        is_overlay(is_overlay) {}
};

static const unordered_map<int16_t, SpriteDefinition> sprite_defs({
    {1055, {1059, 0, false}}, // gold xichron
    {1056, {1058, 0, false}}, // red xichron
    {1081, {1080, 4, false}}, // already-flying carpet
    {1091, {1090, 1, false}}, // up-right cannon
    {1092, {1090, 2, false}}, // right cannon
    {1093, {1090, 3, false}}, // down-right cannon
    {1094, {1090, 4, false}}, // down cannon
    {1095, {1090, 1, true}}, // down-left cannon
    {1096, {1090, 2, true}}, // left cannon
    {1097, {1090, 3, true}}, // up-left cannon
    {1153, {1152, 3, true}}, // left bouncer
    {1340, {1340, 5, false}}, // health upgrade crystal
    {1341, {1341, 5, false}}, // magic upgrade crystal
    {1401, {1400, 1, false}}, // stone platform
    {1402, {1400, 2, false}}, // dirt platform
    {1403, {1400, 3, false}}, // jeweled platform
    {1404, {1400, 4, false}}, // ice platform
    {1405, {1400, 5, false}}, // spiky platform
    {1406, {1400, 6, false}}, // half-log platform
    {1407, {1400, 7, false}}, // half-log platform
    {1408, {1400, 8, false}}, // half-log platform
    {1409, {1400, 9, false}}, // orange box (unused platform type?)
    {1411, {1410, 0, true}}, // catapult facing left
    {1441, {1440, 4, false}}, // acid geyser
    {1442, {1440, 8, false}}, // lava geyser
    {1451, {1450, 1, false}}, // up pipe
    {1452, {1450, 2, false}}, // left pipe
    {1453, {1450, 3, false}}, // right pipe
    {1462, {1461, 0, true}}, // right wooden halfbridge
    {1465, {1464, 0, true}}, // right bone halfbridge
    {1475, {1487, 0, false}}, // rusted spiked ball (falls)
    {1741, {1740, 8, false}}, // sentry bat
    {1841, {1840, 11, true}}, // left-facing spikes
    {1851, {1850, 0, false}}, // piranha
    {1900, {1900, 4, false}}, // right-facing crossbow
    {1901, {1900, 1, true}}, // left-facing crossbow
    {1902, {1902, 4, false}}, // up-facing crossbow
    {1903, {1903, 4, false}}, // down-facing crossbow
    {2890, {2890, 0, false, true}}, // cloud
    {2891, {2891, 0, false, true}}, // cloud
    {2892, {2892, 0, false, true}}, // cloud
    {2893, {2893, 0, false, true}}, // cloud
    {2911, {2910, 5, true}}, // reversed wooden door

    // These are invisible in-game and have no graphics, but we special-case them
    // to be visible
    {1058, {0, 0, false}}, // perm flag trigger
    {1059, {0, 0, false}}, // secret area
    {3249, {0, 0, false}}, // level exit

    // TODO: These appear to be rendered with a different clut in-game
    {1742, {1740, 8, false}}, // fireball sentry bat
    {1731, {1730, 0, false}}, // blue blob
    {1732, {1730, 0, false}}, // orange blob

    // TODO: These are multiple sprites in-game but defined as only one in the map
    // file (see their PICTs)
    {1425, {1435, 0, false}}, // seesaw platform
    {1860, {1860, 8, false}}, // large fly
    {1920, {1920, 0, false}}, // right fire guardian (probably auto-spawns the left one)
    {3020, {650, 0, false}}, // hangable rope
    {3021, {652, 0, false}}, // hangable rope
    {3022, {658, 0, false}}, // hangable chain
});

static const unordered_set<int16_t> passthrough_sprite_defs({
    1060, // gray/blue teleporter
    1061, // yellow teleporter
    1062, // green teleporter
    1065, // save point
    1070, // rollable rock
    1071, // green rollable rock
    1072, // yellow rollable rock
    1080, // flying carpet
    1090, // up cannon
    1150, // up bouncer
    1151, // down bouncer
    1152, // right bouncer
    1208, // floor fire
    1250, // rock cube
    1290, // big magic crystal
    1291, // big health crystal
    1292, // small money bag
    1293, // large money bag
    1303, // pile of rocks
    1304, // pile of rocks
    1307, // torch
    1308, // treasure chest
    1320, // right-facing wall button
    1321, // left-facing wall button
    1322, // red floor button
    1330, // shadow double powerup
    1331, // walk on water powerup
    1332, // walk on acid powerup
    1333, // walk on lava powerup
    1334, // super jump powerup
    1335, // shield powerup
    1336, // slowfall powerup
    1337, // speed powerup
    1338, // pentashield powerup
    1339, // death powerup
    1350, // bubble
    1400, // limestone platform
    1410, // catapult
    1440, // water geyser
    1450, // down pipe
    1460, // wooden bridge
    1461, // left wooden halfbridge
    1463, // bone bridge
    1464, // bone halfbridge
    1465, // rope or chain (can't tell from disassembly alone)
    1466, // rope bridge
    1470, // bounce mushroom
    1480, // crescent blade
    1480, // orange crescent blade
    1481, // ice crescent blade
    1485, // gray spiked ball
    1486, // white spiked ball
    1487, // rusted spiked ball
    1488, // purple spiked ball
    1490, // floor monster generator
    1491, // ceiling monster generator
    1492, // right-facing monster generator
    1493, // left-facing monster generator
    1700, // knife-throwing goblin
    1705, // sword and shield goblin
    1712, // spider
    1720, // cockroach
    1730, // green blob
    1740, // bat
    1750, // axe goblin
    1760, // rock-throwing goblin
    1780, // habnabit wraith
    1800, // teal frog
    1810, // lava jumper
    1820, // manditraki warrior
    1830, // manditraki wizard
    1840, // right-facing spikes
    1842, // floor spikes
    1843, // ceiling spikes
    1850, // shrieking air piranha
    1870, // danger armadillo
    1871, // danger armadillo
    1892, // orange claw
    1910, // goblin chief
    1990, // xichra
    2000, // scroll
    2700, // plant
    2701, // plant
    2702, // plant
    2703, // plant
    2704, // plant
    2705, // plant
    2706, // plant
    2707, // plant
    2708, // plant
    2709, // plant
    2710, // hanging algae
    2711, // hanging algae
    2712, // hanging algae
    2713, // wall algae
    2714, // wall algae
    2715, // wall algae
    2716, // wall algae
    2717, // wall algae
    2808, // angled bone halfbridge (TODO: should this be reversed?)
    2809, // blue crystals
    2810, // large grass
    2811, // cubic stones
    2812, // stone ruins
    2813, // stone ruins
    2814, // mossy rocks
    2815, // tall bush
    2816, // cattails
    2817, // cattails
    2818, // background ice
    2819, // heads on pikes
    2820, // bones
    2821, // bones
    2822, // bones
    2823, // dead habnabit
    2824, // empty armor
    2825, // empty armor
    2826, // empty armor
    2827, // remains
    2828, // remains
    2829, // remains
    2830, // remains
    2831, // remains
    2832, // scroll altar
    2833, // winged gravestonr
    2834, // gravestone
    2835, // gravestone
    2836, // gravestone
    2837, // caution tape rug
    2838, // purple rug
    2839, // skulls rug
    2840, // large xichra statue
    2841, // small xichra statue
    2842, // stack of books
    2843, // wooden chair with spiderwebs
    2844, // toppled wooden chair with spiderwebs
    2845, // wooden table with spiderwebs
    2846, // scenery rock
    2847, // scenery rock
    2848, // cave weeds
    2849, // cave weeds
    2850, // standable rock
    2852, // standable rock
    2853, // standable rock
    2854, // standable rock
    2856, // standable rock
    2857, // standable rock
    2858, // standable rock
    2859, // standable rock
    2860, // standable rock
    2861, // standable rock
    2862, // standable rock
    2863, // standable rock
    2864, // standable rock
    2865, // standable rock
    2866, // standable rock
    2867, // standable rock
    2868, // standable rock
    2869, // standable rock
    2870, // mushrooms
    2871, // mushrooms
    2872, // mushrooms
    2873, // mushrooms
    2874, // big mushrooms
    2875, // mushrooms
    2876, // mushrooms
    2877, // mushrooms
    2879, // mushrooms
    2880, // mushrooms
    2882, // mushrooms
    2883, // mushrooms
    2884, // mushrooms
    2885, // mushrooms
    2900, // small archway
    2901, // large archway
    2902, // sign
    2903, // book
    2904, // piece of paper
    2905, // map
    2906, // wall plaque
    2907, // start point (checkerboard sign)
    2910, // wooden door
    2921, // large crate
    2922, // barrel
    2923, // small red stool
    2924, // wooden chair
    2925, // metal chair
    2926, // metal table
    2927, // wooden table
    2928, // red-top table
    2932, // stalactite
    2940, // stone door
    2941, // ice wall
    2951, // geroditus
    2952, // rojinko
    2953, // ice cavern guy (rojinko reversed)
    2954, // injured habnabit
    2955, // nimbo
    2956, // dimbo
    2957, // xichra gate guard
    2958, // taryn
    2959, // sara
    2960, // forest nymph matriarch
    2961, // blue robed figure (vion in "an old light")
    2962, // wounded habnabit
    2963, // gray robed figure
    2964, // ben spees
    2965, // ice cavern guy (rojinko reversed) (copy?)
    3001, // horizontal passageway
    3002, // horizontal passageway
    3003, // horizontal passageway
    3004, // horizontal passageway
    3005, // horizontal passageway
    3006, // horizontal passageway
    3007, // horizontal passageway
    3008, // horizontal passageway
    3009, // horizontal passageway
    3050, // hang glider
    3060, // spinning sword
    3070, // snowball
    3080, // tree
    3081, // tree
    3082, // tree
    3083, // tree
    3084, // dead tree
    3085, // dead tree
    3086, // dead tree
    3087, // fallen dead tree
    3090, // box
    3091, // ? box
    3092, // ! box
    3100, // floor chandelier
    3101, // angled floor chandelier
    3102, // tree torch
    3103, // blob tree torch
    3104, // wall chandelier
    3105, // wall chandelier
    3106, // small chandelier
    3107, // beetle torch
    3108, // animal skull torch
    3201, // steel key
    3202, // gold key
    3203, // platinum key
    3204, // magic potion
    3205, // health potion
    3206, // fire seeds
    3207, // locket
    3208, // hammer
    3209, // poppyseed muffin
    3210, // algernon piece
    3211, // algernon frame
    3212, // algernon
    3213, // gwendolyn
    3214, // shield
    3215, // magic shield
    3216, // smite ring
    3217, // escape ring
    3218, // ice pick
    3219, // mult crystal
    3220, // light orb
    3221, // vorpal dirk
    3222, // red xichron (non-spinning)
    3223, // rez necklace
    3224, // fire charm
    3225, // mist potion
    3226, // ziridium seeds

    // TODO: These are multiple sprites in-game but defined as only one in the map
    // file (see their PICTs)
    2930, // mine cart
    1420, // springboard
    1869, // small fly swarm
    1770, // flying monster
});

struct SpriteEntry {
  uint8_t valid;
  uint8_t unused;
  be_int16_t type;
  be_int16_t params[4];
  be_int16_t y;
  be_int16_t x;
} __attribute__((packed));

struct ForegroundLayerTile {
  uint8_t destructibility_type;
  uint8_t type;
} __attribute__((packed));

struct BackgroundLayerTile {
  uint8_t brightness;
  uint8_t type;
} __attribute__((packed));

struct WindTile {
  uint8_t strength;
  uint8_t direction;
} __attribute__((packed));

struct FerazelsWandLevel {
  be_uint32_t signature; // 0x04277DC9
  // 0004
  SpriteEntry sprites[603]; // Probably some space at the end here isn't actually part of the sprite table
  // 25B4
  be_uint32_t unknown1[3];
  // 25C0
  be_uint32_t unknown2;
  char name[0x100]; // p-string, so first byte is the length
  // 26C4
  be_int16_t unknown3;
  uint8_t tint_underwater_ground;
  uint8_t abstract_background; // 1=rain, 2=magic, 3=secret, 4-9=bosses
  uint8_t player_faces_left_at_start;
  uint8_t enlarged_air_currents;
  uint8_t river_motion;
  uint8_t use_192x192_pattern_tileset;
  uint8_t use_entire_clut_for_parallax_background;
  uint8_t is_cold; // causes water to hurt player
  uint8_t disable_music_fade_at_start;
  uint8_t unused_flags[7];
  // 26D6
  uint8_t unknown4[0x30];
  // 2706
  be_int16_t ambient_darkness; // 0=none, 9=max
  be_int16_t unused_info1;
  be_int16_t scroll_center_x;
  be_int16_t scroll_center_y;
  be_int16_t special_tile_damage;
  be_int16_t special_tile_slipperiness;
  be_int16_t unused_info2;
  be_int16_t water_current;
  be_int16_t parallax_sprite_pict_id;
  be_int16_t parallax_sprite_scroll_multiplier;
  be_int16_t parallax_sprite_y;
  be_int16_t alt_clut_id;
  be_int16_t ripple_bg_flag;
  be_int16_t bg_clut_animation; // last 48 entries (cycle, presumably)
  be_int16_t fire_bg_info;
  be_int16_t boss_point_x; // negative if approaching from right
  be_int16_t autoscroll_x_speed; // fixed-point 8.8 in pixels/frame
  be_int16_t autoscroll_y_speed; // fixed-point 8.8 in pixels/frame
  be_int16_t autoscroll_type; // 0=off, 1=fire
  be_int16_t player_air_push_x; // 8.8 in pixels/frame
  be_int16_t secondary_boss_point_x;
  be_int16_t background_clut_animation_type;
  be_int16_t background_clut_animation_range;
  be_int16_t background_clut_animation_speed;
  be_int16_t background_clut_animation_amount_mult;
  be_int16_t top_scroll_range;
  be_int16_t scroll_speed;
  be_int16_t chapter_screen_number;
  be_int16_t chapter_screen_scroll_info;
  be_int16_t unused3[3];
  // 2746
  uint8_t unknown5[0x100];
  // 2846
  be_int16_t player_start_y;
  be_int16_t player_start_x;
  be_int16_t music_id;
  // 0 = default for most of these
  be_int16_t parallax_background_pict_id; // "PxBack"
  be_int16_t parallax_middle_pict_id; // "PxMid"
  be_int16_t foreground_tile_pict_id;
  be_int16_t background_tile_pict_id;
  be_int16_t foreground_overlay_pict_id; // covers the bottom of the level
  be_int16_t wall_tile_pict_id;
  be_int16_t layering_type;
  be_int16_t sprite_clut_id;
  be_int16_t tile_background_clut;
  be_int16_t combo_clut;
  // 2860
  be_int16_t unknown7[0x40];
  // 28E0
  be_int16_t foreground_tile_behaviors[0x60];
  // 29A0
  be_int16_t background_tile_behaviors[0x60];
  // 2A60
  uint8_t unknown8[0x880C];
  // B26C
  be_int16_t unknown9[6];
  // B278
  be_int16_t parallax_background_layer_length;
  be_int16_t parallax_background_layer_count;
  be_int16_t parallax_middle_layer_length;
  be_int16_t parallax_middle_layer_count;
  // B280
  be_int16_t width;
  be_int16_t height;
  be_int16_t unknown11[12];
  // B29C
  uint8_t data[0];

  size_t parallax_background_layers_size() const {
    return (this->parallax_background_layer_length * this->parallax_background_layer_count) * sizeof(int16_t);
  }
  size_t parallax_layers_size() const {
    return (this->parallax_background_layer_length * this->parallax_background_layer_count +
               this->parallax_middle_layer_length * this->parallax_middle_layer_count) *
        sizeof(int16_t);
  }

  const be_int16_t* parallax_background_tiles(uint16_t layer) const {
    return reinterpret_cast<const be_int16_t*>(&this->data[layer * this->parallax_background_layer_length * sizeof(int16_t)]);
  }
  const be_int16_t* parallax_middle_tiles(uint16_t layer) const {
    return reinterpret_cast<const be_int16_t*>(&this->data[this->parallax_background_layers_size() + layer * this->parallax_background_layer_length * sizeof(int16_t)]);
  }
  const ForegroundLayerTile* foreground_tiles() const {
    return reinterpret_cast<const ForegroundLayerTile*>(
        &this->data[this->parallax_layers_size() + this->width * this->height * sizeof(BackgroundLayerTile)]);
  }
  const BackgroundLayerTile* background_tiles() const {
    return reinterpret_cast<const BackgroundLayerTile*>(&this->data[this->parallax_layers_size()]);
  }
  const WindTile* wind_tiles() const {
    return reinterpret_cast<const WindTile*>(&this->data[this->parallax_layers_size() +
        this->width * this->height * sizeof(BackgroundLayerTile) +
        this->width * this->height * sizeof(ForegroundLayerTile) +
        this->width * this->height * sizeof(uint16_t)]);
  }
} __attribute__((packed));

static shared_ptr<ImageRGBA8888> decode_PICT_cached(
    int16_t id, unordered_map<int16_t, shared_ptr<ImageRGBA8888>>& cache, ResourceFile& rf) {
  try {
    return cache.at(id);
  } catch (const out_of_range&) {
    try {
      auto decode_result = rf.decode_PICT(id);
      if (!decode_result.embedded_image_format.empty()) {
        throw runtime_error(std::format("PICT {} is an embedded image", id));
      }
      return cache.emplace(id, make_shared<ImageRGBA8888>(std::move(decode_result.image))).first->second;

    } catch (const out_of_range&) {
      return nullptr;
    }
  }
}

static shared_ptr<ImageRGBA8888> truncate_whitespace(shared_ptr<ImageRGBA8888> img) {
  // Top rows
  size_t x, y;
  for (y = 0; y < img->get_height(); y++) {
    for (x = 0; x < img->get_width(); x++) {
      uint32_t c = img->read(x, y);
      if ((c & 0xFFFFFF00) != 0xFFFFFF00) {
        break;
      }
    }
    if (x != img->get_width()) {
      break;
    }
  }
  size_t top_rows_to_remove = y;
  if (top_rows_to_remove == img->get_height()) {
    // Entire image is white; remove all of it
    return make_shared<ImageRGBA8888>();
  }

  // Left columns
  for (x = 0; x < img->get_width(); x++) {
    for (y = 0; y < img->get_height(); y++) {
      uint32_t c = img->read(x, y);
      if ((c & 0xFFFFFF00) != 0xFFFFFF00) {
        break;
      }
    }
    if (y != img->get_height()) {
      break;
    }
  }
  size_t left_columns_to_remove = y;
  if (left_columns_to_remove == img->get_width()) {
    throw logic_error("entire image is white, but did not catch this already");
  }

  // Bottom rows
  for (y = img->get_height() - 1; y > 0; y--) {
    for (x = 0; x < img->get_width(); x++) {
      uint32_t c = img->read(x, y);
      if ((c & 0xFFFFFF00) != 0xFFFFFF00) {
        break;
      }
    }
    if (x != img->get_width()) {
      break;
    }
  }
  size_t bottom_rows_to_remove = img->get_height() - 1 - y;
  if (bottom_rows_to_remove == img->get_height()) {
    throw logic_error("entire image is white, but did not catch this already");
  }

  // Left columns
  for (x = img->get_width() - 1; x > 0; x--) {
    for (y = 0; y < img->get_height(); y++) {
      uint32_t c = img->read(x, y);
      if ((c & 0xFFFFFF00) != 0xFFFFFF00) {
        break;
      }
    }
    if (y != img->get_height()) {
      break;
    }
  }
  size_t right_columns_to_remove = img->get_width() - 1 - x;
  if (right_columns_to_remove == img->get_width()) {
    throw logic_error("entire image is white, but did not catch this already");
  }

  if (top_rows_to_remove || bottom_rows_to_remove || left_columns_to_remove || right_columns_to_remove) {
    auto new_image = make_shared<ImageRGBA8888>(
        img->get_width() - left_columns_to_remove - right_columns_to_remove,
        img->get_height() - top_rows_to_remove - bottom_rows_to_remove);
    new_image->copy_from(
        *img, 0, 0, new_image->get_width(), new_image->get_height(), left_columns_to_remove, top_rows_to_remove);
    return new_image;
  } else {
    return img;
  }
}

void print_usage() {
  fwrite_fmt(stderr, "\
Usage: ferazel_render [options]\n\
\n\
Options:\n\
  --level=N\n\
      Only render map for this level. Can be given multiple times.\n\
  --levels-file=FILE\n\
      Use this file instead of \"Ferazel\'s Wand World Data\".\n\
  --sprites-file=FILE\n\
      Use this file instead of \"Ferazel\'s Wand Sprites\".\n\
  --backgrounds-file=FILE\n\
      Use this file instead of \"Ferazel\'s Wand Backgrounds\".\n\
  --foreground-opacity=N\n\
      Render the foreground tiles with the given opacity (0-255; default 255).\n\
  --background-opacity=N\n\
      Render the background tiles with the given opacity (0-255; default 255).\n\
  --render-wind\n\
      Render cyan arrows showing wind flow. (default)\n\
  --skip-render-wind\n\
      Don\'t render cyan arrows showing wind flow.\n\
  --render-sprites\n\
      Render sprites. (default)\n\
  --skip-render-sprites\n\
      Don\'t render sprites.\n\
  --render-parallax-background\n\
      Render the parallax background, letterboxed to an appropriate location\n\
      behind the level.\n\
  --skip-render-parallax-background\n\
      Don\'t render the parallax background. (default)\n\
  --parallax-foreground-opacity=N\n\
      Render the parallax foreground at the bottom with the given opacity\n\
      (0-255; default 0).\n\
  --print-unused-pict-ids\n\
      When done, print the IDs of all the PICT resources that were not used.\n\n" IMAGE_SAVER_HELP);
}

int main(int argc, char** argv) {
  unordered_set<int16_t> target_levels;
  bool render_parallax_backgrounds = false;
  uint8_t foreground_opacity = 0xFF;
  uint8_t background_opacity = 0xFF;
  bool render_wind = true;
  bool render_sprites = true;
  uint8_t parallax_foreground_opacity = 0;
  bool print_unused_pict_ids = false;
  ImageSaver image_saver;

  string levels_filename = "Ferazel\'s Wand World Data";
  string sprites_filename = "Ferazel\'s Wand Sprites";
  string backgrounds_filename = "Ferazel\'s Wand Backgrounds";

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
    } else if (!strncmp(argv[z], "--backgrounds-file=", 19)) {
      backgrounds_filename = &argv[z][19];
    } else if (!strncmp(argv[z], "--foreground-opacity=", 21)) {
      foreground_opacity = strtoul(&argv[z][21], nullptr, 0);
    } else if (!strncmp(argv[z], "--background-opacity=", 21)) {
      background_opacity = strtoul(&argv[z][21], nullptr, 0);
    } else if (!strcmp(argv[z], "--render-wind")) {
      render_wind = true;
    } else if (!strcmp(argv[z], "--render-sprites")) {
      render_sprites = true;
    } else if (!strcmp(argv[z], "--render-parallax-background")) {
      render_parallax_backgrounds = true;
    } else if (!strncmp(argv[z], "--parallax-foreground-opacity=", 30)) {
      parallax_foreground_opacity = strtoul(&argv[z][30], nullptr, 0);
    } else if (!strcmp(argv[z], "--skip-render-wind")) {
      render_wind = false;
    } else if (!strcmp(argv[z], "--skip-render-sprites")) {
      render_sprites = false;
    } else if (!strcmp(argv[z], "--skip-render-parallax-background")) {
      render_parallax_backgrounds = false;
    } else if (!strcmp(argv[z], "--print-unused-pict-ids")) {
      print_unused_pict_ids = true;
    } else if (!image_saver.process_cli_arg(argv[z])) {
      fwrite_fmt(stderr, "invalid option: {}\n", argv[z]);
      print_usage();
      return 2;
    }
  }

  const string levels_resource_filename = levels_filename + "/..namedfork/rsrc";
  const string sprites_resource_filename = sprites_filename + "/..namedfork/rsrc";
  const string backgrounds_resource_filename = backgrounds_filename + "/..namedfork/rsrc";

  ResourceFile levels(parse_resource_fork(load_file(levels_resource_filename)));
  ResourceFile sprites(parse_resource_fork(load_file(sprites_resource_filename)));
  ResourceFile backgrounds(parse_resource_fork(load_file(backgrounds_resource_filename)));

  uint32_t level_resource_type = 0x4D6C766C; // Mlvl
  auto level_resources = levels.all_resources_of_type(level_resource_type);
  sort(level_resources.begin(), level_resources.end());

  unordered_map<int16_t, shared_ptr<ImageRGBA8888>> backgrounds_cache;
  unordered_map<int16_t, shared_ptr<ImageRGBA8888>> sprites_cache;
  unordered_map<int16_t, shared_ptr<ImageRGBA8888>> reversed_sprites_cache;

  for (int16_t level_id : level_resources) {
    if (!target_levels.empty() && !target_levels.count(level_id)) {
      continue;
    }

    string level_data = levels.get_resource(level_resource_type, level_id)->data;
    const auto* level = reinterpret_cast<const FerazelsWandLevel*>(level_data.data());

    if (level->signature != 0x04277DC9) {
      fwrite_fmt(stderr, "... {} (incorrect signature: {:08X})\n", level_id, level->signature);
      continue;
    }

    ImageRGB888 result(level->width * 32, level->height * 32);

    if (render_parallax_backgrounds) {
      shared_ptr<ImageRGBA8888> pxback_pict;

      if (level->abstract_background) {
        fwrite_fmt(stderr, "... (Level {}) abstract background\n", level_id);
        if (level->abstract_background == 1) {
          pxback_pict = decode_PICT_cached(6000, sprites_cache, sprites);
        } else if (level->abstract_background == 6) {
          // This one is animated with all frames in one PICT; just pick the
          // first frame
          shared_ptr<ImageRGBA8888> loaded = decode_PICT_cached(357, backgrounds_cache, backgrounds);
          if (loaded.get()) {
            pxback_pict = make_shared<ImageRGBA8888>(128, 128);
            pxback_pict->copy_from_with_blend(*loaded, 0, 0, 128, 128, 0, 0);
          }
        } else if (level->abstract_background != 0) {
          // 2=magic (600? 601?)
          // 3=secret
          // 4-9=bosses
          // the PICTs appear to mostly be around PICT 6000 in the sprites file
          fwrite_fmt(stderr, "error: this level has an abstract background ({}); skipping rendering parallax background\n",
              level->abstract_background);
        }
        if (pxback_pict.get()) {
          // Just tile it over the entire level
          size_t w = pxback_pict->get_width();
          size_t h = pxback_pict->get_height();
          for (ssize_t y = 0; y < level->height * 32; y += h) {
            for (ssize_t x = 0; x < level->width * 32; x += w) {
              result.copy_from_with_blend(*pxback_pict, x, y, w, h, 0, 0);
            }
          }
        }
      } else {
        pxback_pict = decode_PICT_cached(
            level->parallax_background_pict_id,
            backgrounds_cache, backgrounds);

        if (pxback_pict.get()) {
          fwrite_fmt(stderr, "... (Level {}) parallax background\n", level_id);
          // For each row, find the repetition point and truncate the row there
          vector<vector<uint16_t>> parallax_layers;
          for (ssize_t y = 0; y < level->parallax_background_layer_count; y++) {
            const auto* row_tiles = level->parallax_background_tiles(y);
            parallax_layers.emplace_back();
            auto& this_layer = parallax_layers.back();
            for (ssize_t x = 0; x < level->parallax_background_layer_length; x++) {
              if ((row_tiles[x] < 0) ||
                  (find(this_layer.begin(), this_layer.end(), row_tiles[x]) != this_layer.end())) {
                break;
              }
              this_layer.emplace_back(row_tiles[x]);
            }
            // Skip the row entirely if it's only one cell with value 0
            if (this_layer.size() == 1 && this_layer[0] == 0) {
              parallax_layers.pop_back();
            }
          }

          size_t x_segments = pxback_pict->get_width() / 128;
          size_t y_segments = pxback_pict->get_width() / 128;

          ssize_t parallax_height = 128 * parallax_layers.size();
          ssize_t letterbox_height = (level->height * 32 - parallax_height) / 2;
          uint64_t top_r = 0, top_g = 0, top_b = 0, bottom_r = 0, bottom_g = 0, bottom_b = 0;
          if (letterbox_height < 0) {
            fwrite_fmt(stderr, "warning: parallax background height ({}) exceeds level height ({}); background will be truncated\n",
                parallax_height, level->height * 32);
            letterbox_height = 0;
          } else if (letterbox_height > 0 && !parallax_layers.empty()) {
            // Compute the average color of the top and bottom row of the
            // parallax background, and fill the letterbox zone with those colors
            for (int16_t tile_num : parallax_layers[0]) {
              size_t x_segnum = tile_num % x_segments;
              size_t y_segnum = tile_num / x_segments;
              if (y_segnum >= y_segments) {
                continue;
              }
              size_t denominator = 0;
              for (size_t y = 0; y < 128; y++) {
                for (size_t x = 0; x < 128; x++) {
                  try {
                    uint32_t c = pxback_pict->read(x_segnum * 128 + x, y_segnum * 128 + y);
                    top_r += get_r(c);
                    top_g += get_g(c);
                    top_b += get_b(c);
                    denominator++;
                  } catch (const runtime_error&) {
                    continue;
                  }
                }
              }
              top_r /= denominator;
              top_g /= denominator;
              top_b /= denominator;
            }
            for (int16_t tile_num : parallax_layers[parallax_layers.size() - 1]) {
              size_t x_segnum = tile_num % x_segments;
              size_t y_segnum = tile_num / x_segments;
              if (y_segnum >= y_segments) {
                continue;
              }
              size_t denominator = 0;
              for (size_t y = 0; y < 128; y++) {
                for (size_t x = 0; x < 128; x++) {
                  try {
                    uint32_t c = pxback_pict->read(x_segnum * 128 + x, y_segnum * 128 + y);
                    bottom_r += get_r(c);
                    bottom_g += get_g(c);
                    bottom_b += get_b(c);
                    denominator++;
                  } catch (const runtime_error&) {
                    continue;
                  }
                }
              }
              bottom_r /= denominator;
              bottom_g /= denominator;
              bottom_b /= denominator;
            }

            result.write_rect(0, 0, result.get_width(), letterbox_height, rgba8888(top_r, top_g, top_b));
            result.write_rect(0, result.get_height() - letterbox_height, result.get_width(), letterbox_height, rgba8888(bottom_r, bottom_g, bottom_b));
          }

          for (size_t y = 0; y < parallax_layers.size(); y++) {
            const auto& row_tiles = parallax_layers[y];
            for (ssize_t x = 0; x < level->width / 4; x++) {
              int16_t tile_num = row_tiles[x % row_tiles.size()];
              size_t x_segnum = tile_num % x_segments;
              size_t y_segnum = tile_num / x_segments;
              if (y_segnum >= y_segments) {
                result.write_rect(x * 128, y * 128 + letterbox_height, 128, 128, 0xFF0000FF);
              } else {
                result.copy_from_with_blend(*pxback_pict, x * 128, y * 128 + letterbox_height, 128, 128, x_segnum * 128, y_segnum * 128);
              }
            }
          }
        }
      }
    }

    const auto* foreground_tiles = level->foreground_tiles();
    const auto* background_tiles = level->background_tiles();
    if (foreground_opacity || background_opacity) {
      shared_ptr<ImageRGBA8888> foreground_blend_mask_pict = foreground_opacity
          ? decode_PICT_cached(185, sprites_cache, sprites)
          : nullptr;
      // TODO: are these the right defaults?
      shared_ptr<ImageRGBA8888> foreground_pict = decode_PICT_cached(
          level->foreground_tile_pict_id ? level->foreground_tile_pict_id.load() : 200,
          backgrounds_cache, backgrounds);
      shared_ptr<ImageRGBA8888> background_pict = decode_PICT_cached(
          level->background_tile_pict_id ? level->background_tile_pict_id.load() : 203,
          backgrounds_cache, backgrounds);
      shared_ptr<ImageRGBA8888> orig_wall_tile_pict = decode_PICT_cached(
          level->wall_tile_pict_id ? level->wall_tile_pict_id.load() : 206,
          backgrounds_cache, backgrounds);
      shared_ptr<ImageRGBA8888> wall_tile_pict = orig_wall_tile_pict.get() ? truncate_whitespace(orig_wall_tile_pict) : nullptr;

      if (background_opacity) {
        fwrite_fmt(stderr, "... (Level {}) background tiles\n", level_id);
        if (!background_pict.get()) {
          fwrite_fmt(stderr, "warning: background pict {} is missing\n", level->background_tile_pict_id);

        } else {
          auto alpha_blit_pixel_fn = [&](uint32_t d, uint32_t s) -> uint32_t {
            if ((s & 0xFFFFFF00) == 0xFFFFFF00) {
              return d;
            } else if (background_opacity == 0xFF) {
              return s;
            } else {
              return rgba8888(
                  ((background_opacity * get_r(s)) + (0xFF - background_opacity) * get_r(d)) / 0xFF,
                  ((background_opacity * get_g(s)) + (0xFF - background_opacity) * get_g(d)) / 0xFF,
                  ((background_opacity * get_b(s)) + (0xFF - background_opacity) * get_b(d)) / 0xFF,
                  ((background_opacity * get_a(s)) + (0xFF - background_opacity) * get_a(d)) / 0xFF);
            }
          };

          for (ssize_t y = 0; y < level->height; y++) {
            for (ssize_t x = 0; x < level->width; x++) {
              size_t tile_index = y * level->width + x;

              uint8_t bg_tile_type = background_tiles[tile_index].type;
              if (bg_tile_type > 0x61) {
                result.draw_text(x * 32, y * 32, 0x0000FFFF, 0xFFFFFF80, "{:02X}/{:02X}",
                    background_tiles[tile_index].brightness, bg_tile_type);
              } else if (bg_tile_type > 0) {
                uint16_t src_x = ((bg_tile_type - 1) % 8) * 32;
                uint16_t src_y = ((bg_tile_type - 1) / 8) * 32;
                result.copy_from_with_custom(*background_pict, x * 32, y * 32, 32, 32, src_x, src_y, alpha_blit_pixel_fn);
              }
            }
          }
        }
      }

      if (foreground_opacity) {
        fwrite_fmt(stderr, "... (Level {}) foreground tiles\n", level_id);
        if (!foreground_pict.get()) {
          fwrite_fmt(stderr, "warning: background pict {} is missing\n",
              level->background_tile_pict_id);

        } else {
          auto alpha_blit_pixel_fn = [&](uint32_t d, uint32_t s) -> uint32_t {
            if ((s & 0xFFFFFF00) == 0xFFFFFF00) {
              return d;
            } else if (foreground_opacity == 0xFF) {
              return s;
            } else {
              return rgba8888(
                  ((foreground_opacity * get_r(s)) + (0xFF - foreground_opacity) * get_r(d)) / 0xFF,
                  ((foreground_opacity * get_g(s)) + (0xFF - foreground_opacity) * get_g(d)) / 0xFF,
                  ((foreground_opacity * get_b(s)) + (0xFF - foreground_opacity) * get_b(d)) / 0xFF,
                  ((foreground_opacity * get_a(s)) + (0xFF - foreground_opacity) * get_a(d)) / 0xFF);
            }
          };

          for (ssize_t y = 0; y < level->height; y++) {
            for (ssize_t x = 0; x < level->width; x++) {
              size_t tile_index = y * level->width + x;

              uint8_t fg_tile_type = foreground_tiles[tile_index].type;
              if (fg_tile_type > 0x61) {
                result.draw_text(x * 32, y * 32 + 10, 0xFF0000FF, 0xFFFFFF80, "{:02X}/{:02X}",
                    foreground_tiles[tile_index].destructibility_type, fg_tile_type);
              } else if (fg_tile_type == 0x60 && wall_tile_pict.get()) {
                uint16_t wall_src_x = (x * 32) % wall_tile_pict->get_width();
                uint16_t wall_src_y = (y * 32) % wall_tile_pict->get_height();
                result.copy_from_with_custom(
                    *wall_tile_pict, x * 32, y * 32, 32, 32, wall_src_x, wall_src_y, alpha_blit_pixel_fn);
              } else if (fg_tile_type > 0) {
                // The blend mask is indexed by the tile behavior, not by the
                // tile type.
                uint16_t mask_tile_index = level->foreground_tile_behaviors[fg_tile_type - 1];
                uint16_t fore_src_x = ((fg_tile_type - 1) % 8) * 32;
                uint16_t fore_src_y = ((fg_tile_type - 1) / 8) * 32;
                if (!wall_tile_pict.get() || (mask_tile_index >= 0x60)) {
                  result.copy_from_with_custom(
                      *foreground_pict, x * 32, y * 32, 32, 32, fore_src_x, fore_src_y, alpha_blit_pixel_fn);
                } else {
                  uint16_t mask_src_x = (mask_tile_index % 8) * 32;
                  uint16_t mask_src_y = (mask_tile_index / 8) * 32;
                  uint16_t wall_src_x = (x * 32) % wall_tile_pict->get_width();
                  uint16_t wall_src_y = (y * 32) % wall_tile_pict->get_height();
                  for (size_t yy = 0; yy < 32; yy++) {
                    for (size_t xx = 0; xx < 32; xx++) {
                      uint32_t tile_c = foreground_pict->read(fore_src_x + xx, fore_src_y + yy);
                      if ((tile_c & 0xFFFFFF00) == 0xFFFFFF00) {
                        continue;
                      }

                      uint32_t blend_c = foreground_blend_mask_pict->read(mask_src_x + xx, mask_src_y + yy);
                      uint32_t wall_c = wall_tile_pict->read(wall_src_x + xx, wall_src_y + yy);
                      uint8_t r = (get_r(blend_c) * get_r(tile_c) + (0xFF - get_r(blend_c)) * get_r(wall_c)) / 0xFF;
                      uint8_t g = (get_g(blend_c) * get_g(tile_c) + (0xFF - get_g(blend_c)) * get_g(wall_c)) / 0xFF;
                      uint8_t b = (get_b(blend_c) * get_b(tile_c) + (0xFF - get_b(blend_c)) * get_b(wall_c)) / 0xFF;
                      if (foreground_opacity == 0xFF) {
                        result.write(x * 32 + xx, y * 32 + yy, rgba8888(r, g, b));
                      } else {
                        uint32_t d = result.read(x * 32 + xx, y * 32 + yy);
                        uint32_t final_c = rgba8888(
                            ((foreground_opacity * r) + (0xFF - foreground_opacity) * get_r(d)) / 0xFF,
                            ((foreground_opacity * g) + (0xFF - foreground_opacity) * get_g(d)) / 0xFF,
                            ((foreground_opacity * b) + (0xFF - foreground_opacity) * get_b(d)) / 0xFF);
                        result.write(x * 32 + xx, y * 32 + yy, final_c);
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }

    if (render_wind) {
      fwrite_fmt(stderr, "... (Level {}) wind tiles\n", level_id);

      const auto* wind_tiles = level->wind_tiles();
      for (ssize_t y = 0; y < level->height; y++) {
        for (ssize_t x = 0; x < level->width; x++) {
          const auto& tile = wind_tiles[y * level->width + x];
          if (!tile.strength || !tile.direction) {
            continue;
          }
          if (tile.direction == 0x65) { // overlay
            result.draw_text(x * 32, y * 32, 0xFFFFFFFF, 0x00000040, "OVL");
          } else if (tile.direction <= 36) {
            float degrees = (tile.direction - 1) * 10;
            // Zero degrees faces right, 90 degrees faces up
            // TODO: this is ugly; clean it up :(
            float length = (80 * tile.strength) / 255;
            float radians = (degrees * 2 * M_PI) / 360;
            float dy = -sin(radians);
            float dx = cos(radians);
            float arrow_x = (x * 32 + 16) + length * dx;
            float arrow_y = (y * 32 + 16) + length * dy;
            float back_x = (x * 32 + 16) - length * dx;
            float back_y = (y * 32 + 16) - length * dy;
            float arrow_left_radians = radians + (M_PI / 4);
            float arrow_left_dy = sin(arrow_left_radians); // Note: reverse signs from the above
            float arrow_left_dx = -cos(arrow_left_radians);
            float arrow_left_x = arrow_x + 3 * arrow_left_dx;
            float arrow_left_y = arrow_y + 3 * arrow_left_dy;
            float arrow_right_radians = radians - (M_PI / 4);
            float arrow_right_dy = sin(arrow_right_radians);
            float arrow_right_dx = -cos(arrow_right_radians);
            float arrow_right_x = arrow_x + 3 * arrow_right_dx;
            float arrow_right_y = arrow_y + 3 * arrow_right_dy;
            result.draw_line(arrow_x, arrow_y, back_x, back_y, 0x00FFFFFF);
            result.draw_line(arrow_x, arrow_y, arrow_left_x, arrow_left_y, 0x00FFFFFF);
            result.draw_line(arrow_x, arrow_y, arrow_right_x, arrow_right_y, 0x00FFFFFF);
          } else {
            result.draw_text(x * 32, y * 32, 0x000000FF, 0x00FFFFFF, "{:02X}/{:02X}", tile.strength - 1, tile.direction);
          }
        }
      }

      // Render destructible tiles
      if (foreground_opacity) {
        fwrite_fmt(stderr, "... (Level {}) destructible tiles\n", level_id);
        for (ssize_t y = 0; y < level->height; y++) {
          for (ssize_t x = 0; x < level->width; x++) {
            size_t tile_index = y * level->width + x;
            uint8_t destructibility_type = foreground_tiles[tile_index].destructibility_type & 0x0F;
            uint8_t destructibility_dir = foreground_tiles[tile_index].destructibility_type & 0xF0;
            if (!destructibility_type && !destructibility_dir) {
              continue;
            }

            bool render_debug = false;

            bool highlight_left = destructibility_dir == 0x30;
            bool highlight_right = destructibility_dir == 0x40;
            bool highlight_up = destructibility_dir == 0x50;
            if (destructibility_dir == 0x00 || destructibility_dir == 0x20 || destructibility_dir > 0x50) {
              render_debug = true;
            }

            uint32_t stripe_c;
            if (destructibility_type == 0x00) {
              // normal destructible: white
              stripe_c = 0xFFFFFF40;
            } else if (destructibility_type == 0x01) {
              // requires three hits to destroy: yellow
              stripe_c = 0xFFFF0040;
            } else if (destructibility_type == 0x02) {
              // only destructible by explosions: orange
              stripe_c = 0xFF800040;
            } else if (destructibility_type == 0x03) {
              // auto destructible: green
              stripe_c = 0x00FF0040;
            } else if (destructibility_type == 0x04) {
              // destructible by ice pick: blue
              stripe_c = 0x0000FF40;
            } else {
              // unknown: red + black
              stripe_c = 0xFF000080;
              render_debug = true;
            }

            for (ssize_t yy = y * 32 + 16; yy < y * 32 + 48; yy++) {
              for (ssize_t xx = x * 32 + 16; xx < x * 32 + 48; xx++) {
                uint64_t effective_a = get_a(stripe_c);
                if (highlight_up) {
                  effective_a = (effective_a * (32 - ((yy - 16) % 32))) / 0x20;
                } else if (highlight_left) {
                  effective_a = (effective_a * (32 - ((xx - 16) % 32))) / 0x20;
                } else if (highlight_right) {
                  effective_a = (effective_a * ((xx - 16) % 32)) / 0x20;
                }
                try {
                  uint32_t c = result.read(xx, yy);
                  if (((xx + yy) / 8) & 1) {
                    c = rgba8888(
                        ((0xFF - effective_a) * get_r(c)) / 0xFF,
                        ((0xFF - effective_a) * get_g(c)) / 0xFF,
                        ((0xFF - effective_a) * get_b(c)) / 0xFF);
                  } else {
                    c = rgba8888(
                        (effective_a * get_r(stripe_c) + (0xFF - effective_a) * get_r(c)) / 0xFF,
                        (effective_a * get_g(stripe_c) + (0xFF - effective_a) * get_g(c)) / 0xFF,
                        (effective_a * get_b(stripe_c) + (0xFF - effective_a) * get_b(c)) / 0xFF);
                  }
                  result.write(xx, yy, c);
                } catch (const runtime_error&) {
                }
              }
            }

            if (render_debug) {
              result.draw_text(x * 32 + 16, y * 32 + 16, 0x000000FF, 0xFF0000FF,
                  "{:02X}", foreground_tiles[tile_index].destructibility_type);
            }
          }
        }
      }
    }

    if (render_sprites) {
      fwrite_fmt(stderr, "... (Level {}) sprites\n", level_id);

      static const size_t max_sprites = sizeof(level->sprites) / sizeof(level->sprites[0]);
      for (size_t z = 0; z < max_sprites; z++) {
        const auto& sprite = level->sprites[z];
        if (!sprite.valid) {
          continue;
        }

        // Handle invisible sprites that we want to be visible
        bool render_text_as_unknown = true;
        if (sprite.type == 1058) { // Flag trigger
          result.blend_rect(sprite.x, sprite.y, 32 * 3, 32 * 3, 0x00FF0020);
          render_text_as_unknown = false;
        } else if (sprite.type == 1059) { // Secret spot
          result.blend_rect(sprite.x, sprite.y, 32 * 3, 32 * 3, 0xFF00FF20);
          render_text_as_unknown = false;
        } else if (sprite.type == 3249) { // Level exit
          result.blend_rect(sprite.x, sprite.y, 32 * 3, 32 * 3, 0x0000FF20);
          render_text_as_unknown = false;
        } else {
          SpriteDefinition passthrough_sprite_def;
          const SpriteDefinition* sprite_def = nullptr;
          try {
            sprite_def = &sprite_defs.at(sprite.type);
          } catch (const out_of_range&) {
            if (passthrough_sprite_defs.count(sprite.type)) {
              passthrough_sprite_def.pict_id = sprite.type;
              passthrough_sprite_def.segment_number = 0;
              passthrough_sprite_def.reverse_horizontal = false;
              sprite_def = &passthrough_sprite_def;
            }
          }

          const SpritePictDefinition* sprite_pict_def = nullptr;
          if (sprite_def) {
            try {
              sprite_pict_def = &sprite_pict_defs.at(sprite_def->pict_id);
            } catch (const out_of_range&) {
              sprite_pict_def = &default_sprite_pict_def;
            }
          }

          int16_t pict_id = sprite_def ? sprite_def->pict_id : sprite.type.load();
          shared_ptr<ImageRGBA8888> sprite_pict = decode_PICT_cached(pict_id, sprites_cache, sprites);

          if (sprite_pict.get() && sprite_def && sprite_def->reverse_horizontal) {
            try {
              sprite_pict = reversed_sprites_cache.at(pict_id);
            } catch (const out_of_range&) {
              auto reversed_image = make_shared<ImageRGBA8888>(sprite_pict->copy());
              reversed_image->reverse_horizontal();
              reversed_sprites_cache.emplace(pict_id, reversed_image);
              sprite_pict = reversed_image;
            }
          }

          if (sprite_pict.get()) {
            size_t src_x = 0;
            size_t src_y = 0;
            size_t src_w = sprite_pict->get_width();
            size_t src_h = sprite_pict->get_height();
            if (sprite_pict_def) {
              size_t x_segnum = sprite_def->segment_number % sprite_pict_def->x_segments;
              size_t y_segnum = sprite_def->segment_number / sprite_pict_def->x_segments;
              if ((x_segnum < sprite_pict_def->x_segments) && (y_segnum < sprite_pict_def->y_segments)) {
                src_w = sprite_pict->get_width() / sprite_pict_def->x_segments;
                src_h = sprite_pict->get_height() / sprite_pict_def->y_segments;
                src_x = x_segnum * src_w;
                src_y = y_segnum * src_h;
              }
            }

            if (sprite_def && sprite_def->is_overlay) {
              for (size_t yy = 0; yy < src_h; yy++) {
                for (size_t xx = 0; xx < src_w; xx++) {
                  uint32_t sprite_c = sprite_pict->read(src_x + xx, src_y + yy);
                  if ((sprite_c & 0xFFFFFF00) == 0xFFFFFF00) {
                    continue;
                  }
                  try {
                    uint32_t existing_c = result.read(sprite.x + xx, sprite.y + yy);
                    uint8_t sprite_a = (get_r(sprite_c) + get_g(sprite_c) + get_b(sprite_c)) / 3;
                    uint32_t result_c = rgba8888(
                        (sprite_a * 0xFF + (0xFF - sprite_a) * get_r(existing_c)) / 0xFF,
                        (sprite_a * 0xFF + (0xFF - sprite_a) * get_g(existing_c)) / 0xFF,
                        (sprite_a * 0xFF + (0xFF - sprite_a) * get_b(existing_c)) / 0xFF);
                    result.write(sprite.x + xx, sprite.y + yy, result_c);
                  } catch (const runtime_error&) {
                  }
                }
              }
            } else {
              result.copy_from_with_source_color_mask(*sprite_pict, sprite.x, sprite.y, src_w, src_h, src_x, src_y, 0xFFFFFFFF);
            }
          }
          render_text_as_unknown = !sprite_def || !sprite_pict_def;
        }

        if (render_text_as_unknown) {
          result.draw_text(sprite.x, sprite.y, 0x000000FF, 0xFF0000FF, "{}-{:X}", sprite.type, z);
        } else {
          result.draw_text(sprite.x, sprite.y, 0xFFFFFFFF, 0x00000040, "{}-{:X}", sprite.type, z);
        }
      }

      // Render sprite behaviors
      for (size_t z = 0; z < max_sprites; z++) {
        const auto& sprite = level->sprites[z];
        if (!sprite.valid) {
          continue;
        }

        size_t text_y = sprite.y + 10;
        switch (sprite.type) {
          case 2940: // stone door
            if (sprite.params[0] < 0) {
              result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "<BOSS");
            } else {
              result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "<{:X}", sprite.params[0]);
            }
            break;

          case 1308: // treasure chest
            if (sprite.params[2] == 0) {
              result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "empty");
            } else {
              result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "{}x {}", sprite.params[2], sprite.params[1]);
            }
            if (sprite.params[0]) {
              result.draw_text(sprite.x, text_y + 10, 0xFFFFFF80, 0x00000040, "need {}", sprite.params[0]);
            }
            break;

          case 3090: // box
          case 3091: // ? box
          case 3092: // ! box
            if (sprite.params[0] == 2) {
              result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "bomb");
            } else if (sprite.params[2] == 0) {
              result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "empty");
            } else {
              result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "{}x {}", sprite.params[2], sprite.params[1]);
            }
            break;

          case 1060:
          case 1061:
          case 1062:
          case 2900:
          case 2901:
            result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, ">{:X}", sprite.params[0]);
            break;

          case 2910: // door
          case 2911: // door
            if (sprite.params[0]) {
              result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "need {}", sprite.params[0]);
            }
            break;

          case 3070: // snowball
            result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "{}->{}", sprite.params[0], sprite.params[1]);
            break;

          case 2902:
          case 2903:
          case 2904:
          case 2905:
          case 2906:
            result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "STR#500-{}", sprite.params[0] - 1);
            break;

          case 1400:
          case 1401:
          case 1402:
          case 1403:
          case 1404:
          case 1405:
          case 1406:
          case 1407:
          case 1408:
          case 1409: {
            static const unordered_map<int16_t, const char*> motion_type_names({
                {1, "vert"},
                {2, "horiz"},
                {3, "float"},
                {5, "vert/step/stop"},
                {6, "horiz/step/stop"},
                {7, "vert/step"},
                {8, "horiz/step"},
                {10, "circ"},
                {11, "pend"},
                {20, "tricycle"},
                {21, "bicycle"},
                {22, "quadcycle"},
                {30, "seesaw"},
                {50, "fall"},
                {51, "disappear"},
                {52, "disappear/timer"},
            });
            try {
              result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "{}:{}", sprite.params[0], motion_type_names.at(sprite.params[0]));
              if (sprite.params[0] <= 30) {
                result.draw_text(sprite.x, text_y + 10, 0xFFFFFF80, 0x00000040, "range {}px", sprite.params[1]);
                result.draw_text(sprite.x, text_y + 20, 0xFFFFFF80, 0x00000040, "speed {:g}px", static_cast<float>(sprite.params[2]) / 256.0);
                if (sprite.params[0] == 10) {
                  result.draw_text(sprite.x, text_y + 30, 0xFFFFFF80, 0x00000040, "angle {:g}deg", static_cast<float>(sprite.params[3]) / 256.0);
                } else {
                  result.draw_text(sprite.x, text_y + 30, 0xFFFFFF80, 0x00000040, "offset {:g}px", static_cast<float>(sprite.params[3]) / 256.0);
                }

              } else if (sprite.params[0] == 50) {
                result.draw_text(sprite.x, text_y + 10, 0xFFFFFF80, 0x00000040, "wait {}", sprite.params[1]);
                result.draw_text(sprite.x, text_y + 20, 0xFFFFFF80, 0x00000040, "dist {}", sprite.params[2]);

              } else if (sprite.params[0] == 51) {
                result.draw_text(sprite.x, text_y + 10, 0xFFFFFF80, 0x00000040, "wait {}", sprite.params[1]);
                result.draw_text(sprite.x, text_y + 20, 0xFFFFFF80, 0x00000040, "reappear {}", sprite.params[2]);

              } else if (sprite.params[0] == 52) {
                result.draw_text(sprite.x, text_y + 10, 0xFFFFFF80, 0x00000040, "appear {}", sprite.params[1]);
                result.draw_text(sprite.x, text_y + 20, 0xFFFFFF80, 0x00000040, "disappear {}", sprite.params[2]);
                result.draw_text(sprite.x, text_y + 30, 0xFFFFFF80, 0x00000040, "offset {}", sprite.params[3]);
              }

            } catch (const out_of_range&) {
              result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "{}", sprite.params[0]);
            }
            break;
          }

          case 1058:
            result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x0000040, "perm flag trigger");
            break;
          case 1059:
            result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x0000040, "{}secret", sprite.params[0] ? "" : "silent ");
            break;

          case 1090:
          case 1091:
          case 1092:
          case 1093:
          case 1094:
          case 1095:
          case 1096:
          case 1097: {
            static const unordered_map<int16_t, const char*> motion_type_names({
                {101, "spin/cw"},
                {102, "spin/ccw"},
                {103, "spin/cw/fast"},
                {104, "spin/ccw/fast"},
                {105, "rotate/hit"},
            });
            try {
              result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040,
                  "{}:{}", sprite.params[0], motion_type_names.at(sprite.params[0]));
              if (sprite.params[0] != 105) {
                result.draw_text(sprite.x, text_y + 10, 0xFFFFFF80, 0x00000040, "stop {}", sprite.params[1]);
                if (sprite.params[2] == 0) {
                  result.draw_text(sprite.x, text_y + 20, 0xFFFFFF80, 0x00000040, "eighths");
                } else if (sprite.params[2] == 1) {
                  result.draw_text(sprite.x, text_y + 20, 0xFFFFFF80, 0x00000040, "quarters");
                } else if (sprite.params[2] == 2) {
                  result.draw_text(sprite.x, text_y + 20, 0xFFFFFF80, 0x00000040, "halfs");
                } else {
                  result.draw_text(sprite.x, text_y + 20, 0xFFFFFF80, 0x00000040, "each {}", sprite.params[2]);
                }
              }

            } catch (const out_of_range&) {
              if (sprite.params[0] != 0) {
                result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "{}", sprite.params[0]);
              }
            }
            break;
          }
          case 1330: // shadow double powerup
          case 1331: // walk on water powerup
          case 1332: // walk on acid powerup
          case 1333: // walk on lava powerup
          case 1334: // super jump powerup
          case 1335: // shield powerup
          case 1336: // slowfall powerup
          case 1337: // speed powerup
          case 1338: // pentashield powerup
          case 1339: // death powerup
            if (sprite.params[0]) {
              result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "floating");
            }
            break;

          case 3249:
            result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x0000040, "level exit");
            text_y += 10;
            [[fallthrough]];

          default:
            for (size_t z = 0; z < 4; z++) {
              if (sprite.params[z]) {
                result.draw_text(sprite.x, text_y, 0xFFFFFF80, 0x00000040, "{}/{}", z, sprite.params[z]);
                text_y += 10;
              }
            }
        }

        // SCYTHES AND SPIKED BALLS
        // info field [0]: type of motion
        // 1,2: ridable platform
        // 10: circular
        // 11: pendulum
        // 12: 3-D pendulum
        // 13: 3-D circular vertical
        // 14: 3-D circular horizontal

        // info field [1]: range in pixels
        // info field [2]: initial speed in 256ths of pixels per frame
        // info field [3]: starting angle 0-360

        // ENEMIES
        // sentinel enemies: movement like platforms, same info used

        // GROUND FIRE - 1208
        // [0]: flame color. 0=normal, 1=bluish-purple, 2=gray, 3=purple, 4=green

        // SCENERY
        // [0]: 1 for flip
        // [1]: 1 for tint
        // #define kRedTint      1
        // #define kYellowTint     2
        // #define kBlueTint     3
        // #define kWaterTint      4
        // #define kSmokeTint      5
        // #define kDarkTint     6
        // #define kVeryDarkTint   7
        // #define kLightTint      8
        // #define kVeryLightTint    9
        // #define kGreenRotTint   10
        // #define kGrayscaleTint    11
        // #define kFlameTint      12
        // #define kWaterTint2     13
        // #define kColaTint     14
        // #define kPurpleTint     15
        // #define kGobYellowTint    16
        // #define kGobBlueTint    17
        // #define kGobIceTint     18
        // #define kGobPurpleTint    19
        // #define kGobBrownTint   20
        // #define kGobGrayTint    21
        // #define kSemiDarkTint   22
        // [2]: 1 to put in front layer

        // CHARACTERS
        // [0]: Resource ID of Conversation resource to use. (Creatable with Edit Conversation command)
      }

      result.draw_text(level->player_start_x, level->player_start_y, 0xFFFFFF80, 0x00000040,
          "{}", level->player_faces_left_at_start ? "<- START" : "START ->");
    }

    if (parallax_foreground_opacity > 0) {
      shared_ptr<ImageRGBA8888> pxmid_pict = decode_PICT_cached(level->parallax_middle_pict_id, backgrounds_cache, backgrounds);

      if (pxmid_pict.get()) {
        fwrite_fmt(stderr, "... (Level {}) parallax foreground\n", level_id);
        const uint64_t& a = parallax_foreground_opacity;

        ssize_t start_y = level->height * 32 - pxmid_pict->get_height();
        for (ssize_t y = (start_y < 0) ? -start_y : 0; y < static_cast<ssize_t>(pxmid_pict->get_height()); y++) {
          for (ssize_t x = 0; x < level->width * 32; x++) {
            uint32_t p = pxmid_pict->read(x % pxmid_pict->get_height(), y);
            uint32_t r = result.read(x, y + start_y);
            if ((p & 0xFFFFFF00) == 0xFFFFFF00) {
              continue;
            }
            uint32_t c = rgba8888(
                (a * get_r(p) + (0xFF - a) * get_r(r)) / 0xFF,
                (a * get_g(p) + (0xFF - a) * get_g(r)) / 0xFF,
                (a * get_b(p) + (0xFF - a) * get_b(r)) / 0xFF);
            result.write(x, y + start_y, c);
          }
        }
      }
    }

    string sanitized_name;
    for (ssize_t x = 0; x < level->name[0]; x++) {
      char ch = level->name[x + 1];
      if (ch > 0x20 && ch <= 0x7E && ch != '/') {
        sanitized_name.push_back(ch);
      } else {
        sanitized_name.push_back('_');
      }
    }

    string result_filename = std::format("{}_Level_{}_{}", levels_filename, level_id, sanitized_name);
    result_filename = image_saver.save_image(result, result_filename);
    fwrite_fmt(stderr, "... (Level {}) -> {}\n", level_id, result_filename);
  }

  if (print_unused_pict_ids) {
    auto sprite_pict_ids = sprites.all_resources_of_type(RESOURCE_TYPE_PICT);
    sort(sprite_pict_ids.begin(), sprite_pict_ids.end());
    for (int16_t pict_id : sprite_pict_ids) {
      if (!sprites_cache.count(pict_id)) {
        fwrite_fmt(stderr, "sprite pict {} UNUSED\n", pict_id);
      } else {
        fwrite_fmt(stderr, "sprite pict {} used\n", pict_id);
      }
    }
    auto background_pict_ids = backgrounds.all_resources_of_type(RESOURCE_TYPE_PICT);
    sort(background_pict_ids.begin(), background_pict_ids.end());
    for (int16_t pict_id : background_pict_ids) {
      if (!backgrounds_cache.count(pict_id)) {
        fwrite_fmt(stderr, "background pict {} UNUSED\n", pict_id);
      } else {
        fwrite_fmt(stderr, "background pict {} used\n", pict_id);
      }
    }
  }

  return 0;
}
