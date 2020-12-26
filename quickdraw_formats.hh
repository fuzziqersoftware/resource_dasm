#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Image.hh>

#include <vector>



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
  bool contains(const Rect& other) const;
  ssize_t width() const;
  ssize_t height() const;

  std::string str() const;
} __attribute__((packed));

struct BitMapHeader {
  uint16_t flags_row_bytes;
  Rect bounds;

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
