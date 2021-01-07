#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <unordered_set>

#include <vector>



enum QuickDrawTransferMode {
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
  AD_MIN = 39,

  GRAYISH_TEXT_OR = 49,

  HIGHLIGHT = 50,
};



struct Color8 {
  uint8_t r;
  uint8_t g;
  uint8_t b;

  Color8() = default;
  Color8(uint16_t r, uint16_t g, uint16_t b);
} __attribute__((packed));



struct Color {
  uint16_t r;
  uint16_t g;
  uint16_t b;

  Color() = default;
  Color(uint16_t r, uint16_t g, uint16_t b);
  void byteswap();

  uint64_t to_u64() const;
} __attribute__((packed));



struct Point {
  int16_t y;
  int16_t x;

  Point() = default;
  Point(int16_t y, int16_t x);
  void byteswap();

  bool operator==(const Point& other) const;
  bool operator!=(const Point& other) const;

  std::string str() const;
}__attribute__((packed));



struct Rect {
  int16_t y1;
  int16_t x1;
  int16_t y2;
  int16_t x2;

  Rect() = default;
  Rect(int16_t y1, int16_t x1, int16_t y2, int16_t x2);
  void byteswap();

  bool operator==(const Rect& other) const;
  bool operator!=(const Rect& other) const;

  bool contains(ssize_t x, ssize_t y) const;
  bool contains_swapped(ssize_t x, ssize_t y) const;
  bool contains(const Rect& other) const;
  ssize_t width() const;
  ssize_t width_swapped() const;
  ssize_t height() const;
  ssize_t height_swapped() const;

  bool is_empty() const;

  std::string str() const;
} __attribute__((packed));



struct Region {
  // note: unlike most of the others, this struct does not represent the actual
  // structure used in pict files, but is instead an interpretation thereof. use
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
  int16_t whole;
  uint16_t decimal;

  Fixed();
  Fixed(int16_t whole, uint16_t decimal);
  void byteswap();
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
  uint16_t size;
  Rect bounds;
  Point points[0];

  void byteswap();
} __attribute__ ((packed));



struct BitMapHeader {
  uint16_t flags_row_bytes;
  Rect bounds;
  uint8_t data[0]; // not affected by byteswap()

  void byteswap();
} __attribute__((packed));



struct PixelMapHeader {
  uint16_t flags_row_bytes;
  Rect bounds;
  uint16_t version;
  uint16_t pack_format;
  uint32_t pack_size;
  uint32_t h_res;
  uint32_t v_res;
  uint16_t pixel_type;
  uint16_t pixel_size; // bits per pixel
  uint16_t component_count;
  uint16_t component_size;
  uint32_t plane_offset;
  uint32_t color_table_offset;
  uint32_t reserved;
  uint8_t data[0]; // not affected by byteswap()

  void byteswap();
} __attribute__((packed));



struct PixelMapData {
  uint8_t data[0];

  uint32_t lookup_entry(uint16_t pixel_size, size_t row_bytes, size_t x, size_t y) const;
  static size_t size(uint16_t row_bytes, size_t h);
} __attribute__((packed));



struct ColorTableEntry {
  uint16_t color_num;
  Color c;

  void byteswap();
} __attribute__((packed));



struct ColorTable {
  uint32_t seed;
  uint16_t flags;
  int16_t num_entries; // actually num_entries - 1
  ColorTableEntry entries[0];

  size_t size() const;
  size_t size_swapped() const;
  void byteswap_header();
  void byteswap();
  uint32_t get_num_entries() const;
  const ColorTableEntry* get_entry(int16_t id) const;
} __attribute__((packed));



struct PaletteEntry {
  Color c;
  uint16_t unknown[5];
} __attribute__((packed));



struct PictQuickTimeImageDescription {
  uint32_t size; // includes variable-length fields
  uint32_t codec;
  uint32_t reserved1;
  uint16_t reserved2;
  uint16_t data_ref_index; // also reserved
  uint16_t algorithm_version;
  uint16_t revision_level; // version of compression software, essentially
  uint32_t vendor;
  uint32_t temporal_quality;
  uint32_t spatial_quality;
  uint16_t width;
  uint16_t height;
  Fixed h_res;
  Fixed v_res;
  uint32_t data_size;
  uint16_t frame_count;
  char name[32];
  uint16_t bit_depth;
  uint16_t clut_id;

  void byteswap();
} __attribute__((packed));



struct PictCompressedQuickTimeArgs {
  uint32_t size;
  uint16_t version;
  uint32_t matrix[9];
  uint32_t matte_size;
  Rect matte_rect;
  uint16_t mode;
  Rect src_rect;
  uint32_t accuracy;
  uint32_t mask_region_size;
  // variable-length fields:
  // - matte_image_description (determined by matte_size)
  // - matte_data (determined by matte_size)
  // - mask_region (determined by mask_region_size)
  // - image_description (always included; size is self-determined)
  // - data (specified in image_description's data_size field)

  void byteswap();
} __attribute__((packed));

struct PictUncompressedQuickTimeArgs {
  uint32_t size;
  uint16_t version;
  uint32_t matrix[9];
  uint32_t matte_size;
  Rect matte_rect;
  // variable-length fields:
  // - matte_image_description (determined by matte_size)
  // - matte_data (determined by matte_size)
  // - subopcode describing the image and mask (98, 99, 9A, or 9B)
  // - image data

  void byteswap();
} __attribute__((packed));



struct PictHeader {
  uint16_t size; // unused
  Rect bounds;

  void byteswap();
} __attribute__ ((packed));



Image decode_monochrome_image(const void* vdata, size_t size, size_t w,
    size_t h, size_t row_bytes = 0);
Image decode_monochrome_image_masked(const void* vdata, size_t size,
    size_t w, size_t h);
Image decode_4bit_image(const void* vdata, size_t size, size_t w, size_t h);
Image decode_8bit_image(const void* vdata, size_t size, size_t w, size_t h);
Image decode_color_image(const PixelMapHeader& header,
    const PixelMapData& pixel_map, const ColorTable& ctable,
    const PixelMapData* mask_map = NULL, size_t mask_row_bytes = 0);
Image apply_alpha_from_mask(const Image& img, const Image& mask);
