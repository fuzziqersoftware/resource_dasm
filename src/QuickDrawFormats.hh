#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <map>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <set>

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
  Color8(uint32_t c);
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
} __attribute__((packed));

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

  Rect anchor(int16_t x = 0, int16_t y = 0) const;

  std::string str() const;
} __attribute__((packed));

struct Region {
  // Note: unlike most of the others, this struct does not represent the actual
  // structure used in PICT files, but is instead an interpretation thereof. Use
  // the StringReader constructor instead of directly reading these.
  Rect rect;
  std::map<int16_t, std::set<int16_t>> inversions;

  Region(StringReader& r);
  Region(const Rect& r);

  std::string serialize() const;

  bool is_inversion_point(int16_t x, int16_t y) const;

  // Renders the region as an image. In the returned image, black pixels are
  // contained in the region and white pixels are not.
  Image render() const;

  class Iterator {
  public:
    Iterator(const Region* region);
    Iterator(const Region* region, const Rect& rect);

    void right();
    void next_line();

    bool check() const;

  private:
    const Region* region;
    Rect target_rect;
    int32_t x;
    int32_t y;
    bool region_is_rect;
    bool current_loc_in_region;

    std::map<int16_t, std::set<int16_t>>::const_iterator inversions_row_it;
    std::set<int16_t> current_row_inversions;
    std::set<int16_t>::const_iterator current_row_it;

    void advance_y();
    void reset_x();
  };

  Iterator iterate() const;
  Iterator iterate(const Rect& rect) const;
};

union Fixed {
  struct {
    be_int16_t whole;
    be_uint16_t decimal;
  } __attribute__((packed)) parts;
  be_int32_t value;

  Fixed();
  Fixed(int16_t whole, uint16_t decimal);

  double as_double() const;
} __attribute__((packed));

struct Pattern {
  union {
    uint8_t rows[8];
    uint64_t pattern;
  };

  Pattern(uint64_t pattern);

  bool pixel_at(uint8_t x, uint8_t y) const;
} __attribute__((packed));

struct Polygon {
  be_uint16_t size;
  Rect bounds;
  Point points[0];
} __attribute__((packed));

struct BitMapHeader {
  be_uint16_t flags_row_bytes;
  Rect bounds;

  inline size_t bytes() const {
    return (this->flags_row_bytes & 0x3FFF) * bounds.height();
  }
} __attribute__((packed));

struct PixelMapHeader {
  // 00
  be_uint16_t flags_row_bytes;
  Rect bounds;
  // 0A
  be_uint16_t version;
  be_uint16_t pack_format;
  // 0E
  be_uint32_t pack_size;
  be_uint32_t h_res;
  be_uint32_t v_res;
  // 1A
  be_uint16_t pixel_type;
  be_uint16_t pixel_size; // bits per pixel
  be_uint16_t component_count;
  // 20
  be_uint16_t component_size;
  be_uint32_t plane_offset;
  // 26
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

  static std::shared_ptr<ColorTable> from_entries(
      const std::vector<ColorTableEntry>& entries);

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
} __attribute__((packed));

extern const std::vector<Color8> default_icon_color_table_4bit;
extern const std::vector<Color8> default_icon_color_table_8bit;

// Decodes a monochrome image. Set bits (ones) in the input data become black
// pixels; unset bits become white pixels. Returns an RGB image containing white
// and black pixels.
Image decode_monochrome_image(
    const void* vdata,
    size_t size,
    size_t w,
    size_t h,
    size_t row_bytes = 0);
// Decodes a monochrome image (as above), but also decodes a second monochrome
// image immediately following the first, and applies the second image as an
// alpha mask over the first. Returns an RGBA image containing white, black, and
// transparent pixels. (The transparent pixels may also have white or black
// color values.)
Image decode_monochrome_image_masked(
    const void* vdata,
    size_t size,
    size_t w,
    size_t h);

// Decodes a 4-bit color image, and applies the given color table to produce a
// full-color RGB image. If null is given for color_table, returns a full-color
// RGB image in which each pixel is one of 000000, 111111, 222222, ... FFFFFF
// (produced by directly placing each 4-bit value in each nybble of each pixel.)
Image decode_4bit_image(
    const void* vdata,
    size_t size,
    size_t w,
    size_t h,
    const std::vector<Color8>* color_table = &default_icon_color_table_4bit);
// Decodes an 8-bit color image, and applies the given color table to produce a
// full-color RGB image. If null is given for color_table, returns a full-color
// RGB image in which all channels of each pixel contain the corresponding value
// from the source pixel.
Image decode_8bit_image(
    const void* vdata,
    size_t size,
    size_t w,
    size_t h,
    const std::vector<Color8>* color_table = &default_icon_color_table_8bit);
// Decodes a color pixel map, optionally with a mask bitmap.
Image decode_color_image(
    const PixelMapHeader& header,
    const PixelMapData& pixel_map,
    const ColorTable* ctable,
    const PixelMapData* mask_map = nullptr,
    size_t mask_row_bytes = 0);

Image replace_image_channel(
    const Image& dest, uint8_t dest_channel, const Image& src, uint8_t src_channel);

std::vector<Color8> to_color8(const std::vector<Color>& cs);
std::vector<Color8> to_color8(const std::vector<ColorTableEntry>& cs);
std::vector<Color8> to_color8(const std::vector<PaletteEntry>& cs);
