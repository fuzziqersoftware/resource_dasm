#include <stdio.h>

#include "DataCodecs/Codecs.hh"
#include "ResourceFile.hh"
#include "TextCodecs.hh"
#include <cstring>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <zlib.h>

using namespace std;

void print_usage() {
  fprintf(stderr, "\
Usage: icon_dearchiver <input-filename> [output-dir]\n\
\n\
If output-dir is not given, the directory <input-filename>.out is created and\n\
the output is written there.\n\
\n");
}

static constexpr uint8_t ICON_TYPE_COUNT = 15;

static constexpr struct {
  uint32_t icns_type;
  // The type's size in Icon Archiver archive. Identical to type's size in .icns
  // file if and only if the type is not 24 bit RGB
  uint32_t size_in_archive;
  // 24 bit RGB type instead of indexed, b/w or alpha?
  bool is_24_bits;
  // The bit in the bitfield of an icon that indicates which icon types exist
  // (the bits aren't in the same order as the icon data)
  uint8_t type_bit;
} ICON_TYPES[] = {
    // These are in the order the icon types are stored in an Icon Archiver 4 file's
    // icon data
    {RESOURCE_TYPE_ICNN, 256, false, 5},
    {RESOURCE_TYPE_icl4, 512, false, 6},
    {RESOURCE_TYPE_icl8, 1024, false, 7},
    {RESOURCE_TYPE_il32, 4096, true, 8},
    {RESOURCE_TYPE_l8mk, 1024, false, 9},

    {RESOURCE_TYPE_icsN, 64, false, 0},
    {RESOURCE_TYPE_ics4, 128, false, 1},
    {RESOURCE_TYPE_ics8, 256, false, 2},
    {RESOURCE_TYPE_is32, 1024, true, 3},
    {RESOURCE_TYPE_s8mk, 256, false, 4},

    {RESOURCE_TYPE_ichN, 576, false, 10},
    {RESOURCE_TYPE_ich4, 1152, false, 11},
    {RESOURCE_TYPE_ich8, 2304, false, 12},
    {RESOURCE_TYPE_ih32, 9216, true, 13},
    {RESOURCE_TYPE_h8mk, 2304, false, 14},
};
static_assert(sizeof(ICON_TYPES) / sizeof(ICON_TYPES[0]) == ICON_TYPE_COUNT);

static constexpr uint8_t icns_type_to_icns_idx(uint32_t icns_type) {
  for (uint8_t i = 0; i < ICON_TYPE_COUNT; ++i) {
    if (ICON_TYPES[i].icns_type == icns_type) {
      return i;
    }
  }
  throw logic_error("Unsupported icns icon type");
}

// Order must match the one in `ICON_TYPES` above, use helper function to
// guarantee this
static constexpr uint8_t ICON_TYPE_ICNN = icns_type_to_icns_idx(RESOURCE_TYPE_ICNN);
static constexpr uint8_t ICON_TYPE_icl4 = icns_type_to_icns_idx(RESOURCE_TYPE_icl4);
static constexpr uint8_t ICON_TYPE_icl8 = icns_type_to_icns_idx(RESOURCE_TYPE_icl8);
static constexpr uint8_t ICON_TYPE_il32 = icns_type_to_icns_idx(RESOURCE_TYPE_il32);
static constexpr uint8_t ICON_TYPE_l8mk = icns_type_to_icns_idx(RESOURCE_TYPE_l8mk);

static constexpr uint8_t ICON_TYPE_icsN = icns_type_to_icns_idx(RESOURCE_TYPE_icsN);
static constexpr uint8_t ICON_TYPE_ics4 = icns_type_to_icns_idx(RESOURCE_TYPE_ics4);
static constexpr uint8_t ICON_TYPE_ics8 = icns_type_to_icns_idx(RESOURCE_TYPE_ics8);
static constexpr uint8_t ICON_TYPE_is32 = icns_type_to_icns_idx(RESOURCE_TYPE_is32);
static constexpr uint8_t ICON_TYPE_s8mk = icns_type_to_icns_idx(RESOURCE_TYPE_s8mk);

static constexpr uint8_t ICON_TYPE_ichN = icns_type_to_icns_idx(RESOURCE_TYPE_ichN);
static constexpr uint8_t ICON_TYPE_ich4 = icns_type_to_icns_idx(RESOURCE_TYPE_ich4);
static constexpr uint8_t ICON_TYPE_ich8 = icns_type_to_icns_idx(RESOURCE_TYPE_ich8);
static constexpr uint8_t ICON_TYPE_ih32 = icns_type_to_icns_idx(RESOURCE_TYPE_ih32);
static constexpr uint8_t ICON_TYPE_h8mk = icns_type_to_icns_idx(RESOURCE_TYPE_h8mk);

// .icns files must contain the icons in a specific order, namely b/w icons
// last, or they don't show up correctly in Finder
// TODO: system-made .icns don't do this?
static constexpr uint8_t ICON_ICNS_ORDER[] = {
    ICON_TYPE_ics4,
    ICON_TYPE_ics8,
    ICON_TYPE_is32,
    ICON_TYPE_s8mk,
    ICON_TYPE_icl4,
    ICON_TYPE_icl8,
    ICON_TYPE_il32,
    ICON_TYPE_l8mk,
    ICON_TYPE_ich4,
    ICON_TYPE_ich8,
    ICON_TYPE_ih32,
    ICON_TYPE_h8mk,
    ICON_TYPE_icsN,
    ICON_TYPE_ICNN,
    ICON_TYPE_ichN,
};
static_assert(sizeof(ICON_ICNS_ORDER) == ICON_TYPE_COUNT * sizeof(ICON_ICNS_ORDER[0]));

static bool need_bw_icon(uint8_t bw_icon_type, const int32_t (&uncompressed_offsets)[ICON_TYPE_COUNT]) {
  switch (bw_icon_type) {
    case ICON_TYPE_icsN:
      return uncompressed_offsets[ICON_TYPE_ics4] >= 0 || uncompressed_offsets[ICON_TYPE_ics8] >= 0;

    case ICON_TYPE_ICNN:
      return uncompressed_offsets[ICON_TYPE_icl4] >= 0 || uncompressed_offsets[ICON_TYPE_icl8] >= 0;

    case ICON_TYPE_ichN:
      return uncompressed_offsets[ICON_TYPE_ich4] >= 0 || uncompressed_offsets[ICON_TYPE_ich8] >= 0;
  }
  return false;
}

struct DearchiverContext {
  StringReader in;
  string base_name;
  string out_dir;
};

static void write_icns(
    const DearchiverContext& context,
    uint32_t icon_number, const string& icon_name,
    const char* uncompressed_data, const int32_t (&uncompressed_offsets)[ICON_TYPE_COUNT]) {
  // TODO: custom format string, padding for icon number
  string filename = string_printf("%s/%s_%u", context.out_dir.c_str(), context.base_name.c_str(), icon_number);
  if (!icon_name.empty()) {
    filename += "_";
    // TODO: sanitize name
    filename += icon_name;
  }
  // TODO: write icns, icl8 etc resources into single rsrc file, use filename as rsrc name
  filename += ".icns";

  // Start .icns file
  StringWriter data;
  data.put_u32b(0x69636E73);
  data.put_u32b(0);

  for (unsigned int t = 0; t < ICON_TYPE_COUNT; ++t) {
    uint32_t type = ICON_ICNS_ORDER[t];
    if (uncompressed_offsets[type] >= 0) {
      data.put_u32b(ICON_TYPES[type].icns_type);
      uint32_t size_pos = data.size();
      data.put_u32b(0);
      uint32_t size;
      if (ICON_TYPES[type].is_24_bits) {
        // Icon Archiver stores 24 bit icons as ARGB. The .icns format requires them to
        // be compressed one channel after the other with a PackBits-like algorithm
        size = compress_strided_icns_data(data, uncompressed_data + uncompressed_offsets[type] + 1, ICON_TYPES[type].size_in_archive, 4) +
            compress_strided_icns_data(data, uncompressed_data + uncompressed_offsets[type] + 2, ICON_TYPES[type].size_in_archive, 4) +
            compress_strided_icns_data(data, uncompressed_data + uncompressed_offsets[type] + 3, ICON_TYPES[type].size_in_archive, 4);
      } else {
        data.write(uncompressed_data + uncompressed_offsets[type], ICON_TYPES[type].size_in_archive);
        size = ICON_TYPES[type].size_in_archive;
      }
      data.pput_u32b(size_pos, 8 + size);
    } else if (need_bw_icon(type, uncompressed_offsets)) {
      // If b/w icons are missing, write a black square as icon, and all pixels set as mask:
      // color icons don't display correctly without b/w icon+mask(?)
      data.put_u32b(ICON_TYPES[type].icns_type);
      data.put_u32b(8 + ICON_TYPES[type].size_in_archive);
      data.extend_by(ICON_TYPES[type].size_in_archive / 2, 0x00u);
      data.extend_by(ICON_TYPES[type].size_in_archive / 2, 0xFFu);
    }
  }

  // Adjust .icns size
  data.pput_u32b(4, data.size());

  save_file(filename, data.str());
  fprintf(stderr, "... %s\n", filename.c_str());
}

static void dearchive_icon(DearchiverContext& context, uint16_t version, uint32_t icon_number) {
  StringReader& r = context.in;
  uint32_t const r_where = r.where();

  // This includes all the icon's data, including this very uint32_t
  uint32_t icon_size = r.get_u32b();

  // always 0?
  r.get_u16b();

  // Seems related to icon_size, seems to be always 11 bytes
  // (version 1) / 10 bytes (version 2) less
  r.get_u16b();

  // Is the icon selected in Icon Archiver? (doesn't seem to be actually used by
  // application)
  r.get_u16b();

  // More icon_size relatives
  r.get_u16b();

  uint16_t uncompressed_icon_size = r.get_u16b();
  string uncompressed_data(uncompressed_icon_size, '\0');
  int32_t uncompressed_offsets[ICON_TYPE_COUNT];
  std::memset(uncompressed_offsets, -1, sizeof(uncompressed_offsets));

  string icon_name;
  if (version > 1) {
    // Version 2 has a bitfield of 15 bits (3 sizes, 5 color depths including mask)
    // for each icon that specifies which types of an icon family there are
    uint16_t icon_types = r.get_u16b();
    uint32_t offset = 0;
    for (uint32_t type = 0; type < ICON_TYPE_COUNT; ++type) {
      if (icon_types & (1 << ICON_TYPES[type].type_bit)) {
        uncompressed_offsets[type] = offset;

        offset += ICON_TYPES[type].size_in_archive;
      }
      if (offset > uncompressed_icon_size) {
        fprintf(stderr, "Warning: buffer overflow while decoding icon %u: %u > %u. Skipping...\n", icon_number, offset, uncompressed_icon_size);
        r.go(r_where + icon_size);
        return;
      }
    }
    if (offset == 0) {
      fprintf(stderr, "Warning: icon %u contains no supported icon types. Skipping...\n", icon_number);
      r.go(r_where + icon_size);
      return;
    }

    // ???
    r.get_u16b();

    icon_name = r.readx(r.get_u8());

    // Icon name seems to be both a Pascal and a C string, skip the NUL terminator
    r.get_u8();

    // All icons are compressed as a single blob with zlib
    uint32_t compressed_size_zlib = r_where + icon_size - r.where();
    uLongf uncompressed_size_zlib = uncompressed_icon_size;
    int zlib_result = uncompress(reinterpret_cast<Bytef*>(uncompressed_data.data()), &uncompressed_size_zlib, reinterpret_cast<const Bytef*>(r.getv(compressed_size_zlib)), compressed_size_zlib);
    if (zlib_result != 0) {
      fprintf(stderr, "Warning: zlib error decompressing icon %u: %d\n. Skipping...", icon_number, zlib_result);
      r.go(r_where + icon_size);
      return;
    }
    if (uncompressed_size_zlib != uncompressed_icon_size) {
      fprintf(stderr, "Warning: decompressed icon %u is of size %lu instead of %u as expected\n. Skipping...", icon_number, uncompressed_size_zlib, uncompressed_icon_size);
      r.go(r_where + icon_size);
      return;
    }
  } else {
    // Version 1 uses an array of offsets from a position before the icon's name.
    // Before System 8.5 there were only 6 icon types:
    //
    //  ICN#    32x32x1 with mask
    //  icl4    32x32x4
    //  icl8    32x32x8
    //  ics#    16x16x1 with mask
    //  ics4    16x16x4
    //  ics8    16x16x8
    //
    // An offset of 0 means that the icon type doesn't exist. The offsets aren't
    // always in ascending order. They are into the *uncompressed* data.
    uint16_t icon_offsets[6] = {
        r.get_u16b(),
        r.get_u16b(),
        r.get_u16b(),
        r.get_u16b(),
        r.get_u16b(),
        r.get_u16b(),
    };

    icon_name = r.readx(r.get_u8());

    // The offsets don't start at 0, i.e. they aren't relative to the beginning of
    // the compressed icon data, but relative to somewhere before the icon's name
    uint16_t offset_base = icon_name.size() + 17;

    // All icons are compressed as a single blob with PackBits
    unpack_bits(r, uncompressed_data.data(), uncompressed_icon_size);

    uncompressed_offsets[ICON_TYPE_ICNN] = icon_offsets[0] - offset_base;
    uncompressed_offsets[ICON_TYPE_icl4] = icon_offsets[1] - offset_base;
    uncompressed_offsets[ICON_TYPE_icl8] = icon_offsets[2] - offset_base;
    uncompressed_offsets[ICON_TYPE_icsN] = icon_offsets[3] - offset_base;
    uncompressed_offsets[ICON_TYPE_ics4] = icon_offsets[4] - offset_base;
    uncompressed_offsets[ICON_TYPE_ics8] = icon_offsets[5] - offset_base;
  }

  strip_trailing_whitespace(icon_name);
  icon_name = decode_mac_roman(icon_name, /*for filename=*/true);

  write_icns(context, icon_number, icon_name, uncompressed_data.data(), uncompressed_offsets);

  // Done: continue right after the icon, skipping any possible padding after
  // the icon's data
  r.go(r_where + icon_size);
}

int main(int argc, const char** argv) {
  try {
    if (argc < 2) {
      print_usage();
      return 2;
    }

    DearchiverContext context;

    for (int x = 1; x < argc; x++) {
      if (context.base_name.empty()) {
        context.base_name = argv[x];
      } else if (context.out_dir.empty()) {
        context.out_dir = argv[x];
      } else {
        fprintf(stderr, "excess argument: %s\n", argv[x]);
        print_usage();
        return 2;
      }
    }

    if (context.base_name.empty()) {
      print_usage();
      return 2;
    }
    if (context.out_dir.empty()) {
      context.out_dir = string_printf("%s.out", context.base_name.c_str());
    }
    mkdir(context.out_dir.c_str(), 0777);

    string content = load_file(context.base_name);
    StringReader& r = context.in = StringReader(content);

    // Check signature ('QBSE' 'PACK')
    if (r.get_u32b() != 0x51425345 || r.get_u32b() != 0x5041434B) {
      fprintf(stderr, "File '%s' isn't an Icon Archiver file\n", context.base_name.c_str());
      return 2;
    }

    // ???
    r.skip(2);

    // Version: 1 = Icon Archiver 2; 2 = Icon Archiver 4
    uint16_t version = r.get_u16b();
    if (version != 1 && version != 2) {
      fprintf(stderr, "File '%s' has unsupported version %hu\n", context.base_name.c_str(), version);
      return 2;
    }

    uint32_t icon_count = r.get_u32b();

    // ???
    // Offset 0x30: Window left-top (16 bit y, 16 bit x)
    // Offset 0x34: Window right-bottom (16 bit y, 16 bit x)
    r.skip(32);

    // Seems to be some kind of date
    r.get_u64b();

    // ???
    r.skip(8);

    if (version > 1) {
      // Another signature? ('IAUB')
      if (r.get_u32b() != 0x49415542) {
        fprintf(stderr, "File '%s' isn't an Icon Archiver version 2 file\n", context.base_name.c_str());
        return 2;
      }

      // ???
      r.skip(57);

      // Are the copyright and comment string locked, i.e. can't be changed
      // anymore in Icon Archiver
      r.get_u8();

      // ???
      r.skip(2);

      // Copyright and comment strings are Pascal strings padded to a fixed length
      r.get_u8();
      string copyright = decode_mac_roman(r.readx(63));
      strip_trailing_zeroes(copyright);

      r.get_u8();
      string comment = decode_mac_roman(r.readx(255));
      strip_trailing_zeroes(comment);

      if (!copyright.empty() || !comment.empty()) {
        // TODO: output archive comments?
      }

      // After the comments there's additional ??? data and then an array of
      // uint32_t with one element for each icon in the file. All elements
      // are zero. Could be an array of offsets to the icon data, initialized
      // when loading the archive
      r.go(0x440 + 4 * icon_count);
    } else {
      // Same, but for Icon Archiver 2
      r.go(0x40 + 4 * icon_count);
    }

    for (std::uint32_t icon_no = 0; icon_no < icon_count; ++icon_no) {
      dearchive_icon(context, version, icon_no);
    }
  } catch (const std::exception& e) {
    fprintf(stderr, "Error: %s\n", e.what());
    return 1;
  }
}
