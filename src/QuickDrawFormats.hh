#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <unordered_set>

#include <vector>



enum TransferMode {
  SRC_COPY = 0,
  SRC_OR = 1,
  SRC_XOR = 2,
  SRC_BIC = 3,
  NOT_SRC_COPY = 4,
  NOT_SRC_OR = 5,
  NOT_SRC_XOR = 6,
  NOT_SRC_BIC = 7,
  BLEND = 32,
  ADD_PIN = 33,
  ADD_OVER = 34,
  SUB_PIN = 35,
  TRANSPARENT = 36,
  ADD_MAX = 37,
  SUB_OVER = 38,
  ADD_MIN = 39, // called adMin in QD docs for some reason

  GRAYISH_TEXT_OR = 49,

  HIGHLIGHT = 50,
};



struct Color8 {
  uint8_t r;
  uint8_t g;
  uint8_t b;

  Color8() = default;
  Color8(uint8_t r, uint8_t g, uint8_t b);
} __attribute__((packed));



struct Color {
  be_uint16_t r;
  be_uint16_t g;
  be_uint16_t b;

  Color() = default;
  Color(uint16_t r, uint16_t g, uint16_t b);

  Color8 as8() const;
  uint64_t to_u64() const;
} __attribute__((packed));



struct Point {
  be_int16_t y;
  be_int16_t x;

  Point() = default;
  Point(int16_t y, int16_t x);

  bool operator==(const Point& other) const;
  bool operator!=(const Point& other) const;

  std::string str() const;
}__attribute__((packed));



struct Rect {
  be_int16_t y1;
  be_int16_t x1;
  be_int16_t y2;
  be_int16_t x2;

  Rect() = default;
  Rect(int16_t y1, int16_t x1, int16_t y2, int16_t x2);

  bool operator==(const Rect& other) const;
  bool operator!=(const Rect& other) const;

  bool contains(ssize_t x, ssize_t y) const;
  bool contains(const Rect& other) const;
  ssize_t width() const;
  ssize_t height() const;

  bool is_empty() const;

  std::string str() const;
} __attribute__((packed));



struct Region {
  // Note: unlike most of the others, this struct does not represent the actual
  // structure used in PICT files, but is instead an interpretation thereof. Use
  // the StringReader constructor instead of directly reading these.
  Rect rect;
  std::unordered_set<int32_t> inversions;
  mutable Image rendered;

  Region(StringReader& r);
  Region(const Rect& r);

  std::string serialize() const;

  static int32_t signature_for_inversion_point(int16_t x, int16_t y);
  static Point inversion_point_for_signature(int32_t s);

  bool is_inversion_point(int16_t x, int16_t y) const;

  const Image& render() const;

  bool contains(int16_t x, int16_t y) const;
};



struct Fixed {
  be_int16_t whole;
  be_uint16_t decimal;

  Fixed();
  Fixed(int16_t whole, uint16_t decimal);
} __attribute__ ((packed));



struct Pattern {
  union {
    uint8_t rows[8];
    uint64_t pattern;
  };

  Pattern(uint64_t pattern);

  bool pixel_at(uint8_t x, uint8_t y) const;
} __attribute__ ((packed));



struct Polygon {
  be_uint16_t size;
  Rect bounds;
  Point points[0];
} __attribute__ ((packed));



struct BitMapHeader {
  be_uint16_t flags_row_bytes;
  Rect bounds;
} __attribute__((packed));

struct PixelMapHeader {
  be_uint16_t flags_row_bytes;
  Rect bounds;
  be_uint16_t version;
  be_uint16_t pack_format;
  be_uint32_t pack_size;
  be_uint32_t h_res;
  be_uint32_t v_res;
  be_uint16_t pixel_type;
  be_uint16_t pixel_size; // bits per pixel
  be_uint16_t component_count;
  be_uint16_t component_size;
  be_uint32_t plane_offset;
  be_uint32_t color_table_offset; // when in memory, handle to color table
  be_uint32_t reserved;
} __attribute__((packed));



struct PixelMapData {
  uint8_t data[0];

  uint32_t lookup_entry(uint16_t pixel_size, size_t row_bytes, size_t x, size_t y) const;
  static size_t size(uint16_t row_bytes, size_t h);
} __attribute__((packed));



struct ColorTableEntry {
  be_uint16_t color_num;
  Color c;
} __attribute__((packed));



struct ColorTable {
  be_uint32_t seed;
  be_uint16_t flags;
  be_int16_t num_entries; // actually num_entries - 1
  ColorTableEntry entries[0];

  size_t size() const;
  uint32_t get_num_entries() const;
  const ColorTableEntry* get_entry(int16_t id) const;
} __attribute__((packed));



struct PaletteEntry {
  Color c;
  be_uint16_t usage;
  be_uint16_t tolerance;
  be_uint16_t private_flags;
  be_uint32_t unused;
} __attribute__((packed));



struct PictQuickTimeImageDescription {
  be_uint32_t size; // includes variable-length fields
  be_uint32_t codec;
  be_uint32_t reserved1;
  be_uint16_t reserved2;
  be_uint16_t data_ref_index; // also reserved
  be_uint16_t algorithm_version;
  be_uint16_t revision_level; // version of compression software, essentially
  be_uint32_t vendor;
  be_uint32_t temporal_quality;
  be_uint32_t spatial_quality;
  be_uint16_t width;
  be_uint16_t height;
  Fixed h_res;
  Fixed v_res;
  be_uint32_t data_size;
  be_uint16_t frame_count;
  char name[32];
  be_uint16_t bit_depth;
  be_uint16_t clut_id;
} __attribute__((packed));



struct PictCompressedQuickTimeArgs {
  be_uint32_t size;
  be_uint16_t version;
  be_uint32_t matrix[9];
  be_uint32_t matte_size;
  Rect matte_rect;
  be_uint16_t mode;
  Rect src_rect;
  be_uint32_t accuracy;
  be_uint32_t mask_region_size;
  // variable-length fields:
  // - matte_image_description (determined by matte_size)
  // - matte_data (determined by matte_size)
  // - mask_region (determined by mask_region_size)
  // - image_description (always included; size is self-determined)
  // - data (specified in image_description's data_size field)
} __attribute__((packed));

struct PictUncompressedQuickTimeArgs {
  be_uint32_t size;
  be_uint16_t version;
  be_uint32_t matrix[9];
  be_uint32_t matte_size;
  Rect matte_rect;
  // variable-length fields:
  // - matte_image_description (determined by matte_size)
  // - matte_data (determined by matte_size)
  // - subopcode describing the image and mask (98, 99, 9A, or 9B)
  // - image data
} __attribute__((packed));



struct PictHeader {
  be_uint16_t size; // unused
  Rect bounds;
} __attribute__ ((packed));



Image decode_monochrome_image(const void* vdata, size_t size, size_t w,
    size_t h, size_t row_bytes = 0);
Image decode_monochrome_image_masked(const void* vdata, size_t size,
    size_t w, size_t h);
Image decode_4bit_image(const void* vdata, size_t size, size_t w, size_t h);
Image decode_8bit_image(const void* vdata, size_t size, size_t w, size_t h);
Image decode_color_image(const PixelMapHeader& header,
    const PixelMapData& pixel_map, const ColorTable* ctable,
    const PixelMapData* mask_map = nullptr, size_t mask_row_bytes = 0);
Image apply_alpha_from_mask(const Image& img, const Image& mask);

std::vector<Color8> to_color8(const std::vector<Color>& cs);
std::vector<Color8> to_color8(const std::vector<ColorTableEntry>& cs);
std::vector<Color8> to_color8(const std::vector<PaletteEntry>& cs);
