#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Image.hh>

#include <vector>



struct color {
  uint16_t r;
  uint16_t g;
  uint16_t b;

  color(uint16_t r, uint16_t g, uint16_t b);
  void byteswap();

  uint64_t to_u64() const;
} __attribute__((packed));

struct rect {
  int16_t y1;
  int16_t x1;
  int16_t y2;
  int16_t x2;

  rect() = default;
  rect(int16_t y1, int16_t x1, int16_t y2, int16_t x2);
  void byteswap();

  bool operator==(const rect& other) const;
  bool operator!=(const rect& other) const;

  bool contains(ssize_t x, ssize_t y) const;
  bool contains(const rect& other) const;
  ssize_t width() const;
  ssize_t height() const;

  std::string str() const;
};

struct bit_map_header {
  uint16_t flags_row_bytes;
  rect bounds;

  void byteswap();
} __attribute__((packed));

struct pixel_map_header {
  uint16_t flags_row_bytes;
  rect bounds;
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

struct pixel_map_data {
  uint8_t data[0];

  uint32_t lookup_entry(uint16_t pixel_size, size_t row_bytes, size_t x, size_t y) const;
  static size_t size(uint16_t row_bytes, size_t h);
} __attribute__((packed));

struct color_table_entry {
  uint16_t color_num;
  uint16_t r;
  uint16_t g;
  uint16_t b;

  void byteswap();
} __attribute__((packed));

struct color_table {
  uint32_t seed;
  uint16_t flags;
  int16_t num_entries; // actually num_entries - 1
  color_table_entry entries[0];

  size_t size() const;
  size_t size_swapped() const;
  void byteswap_header();
  void byteswap();
  uint32_t get_num_entries() const;
  const color_table_entry* get_entry(int16_t id) const;
} __attribute__((packed));

struct pltt_entry {
  uint16_t r;
  uint16_t g;
  uint16_t b;
  uint16_t unknown[5];
} __attribute__((packed));

struct clut_entry {
  uint16_t index;
  uint16_t r;
  uint16_t g;
  uint16_t b;
} __attribute__((packed));



Image decode_monochrome_image(const void* vdata, size_t size, size_t w,
    size_t h, size_t row_bytes = 0);
Image decode_monochrome_image_masked(const void* vdata, size_t size,
    size_t w, size_t h);
Image decode_4bit_image(const void* vdata, size_t size, size_t w, size_t h);
Image decode_8bit_image(const void* vdata, size_t size, size_t w, size_t h);
Image decode_color_image(const pixel_map_header& header,
    const pixel_map_data& pixel_map, const color_table& ctable,
    const pixel_map_data* mask_map = NULL, size_t mask_row_bytes = 0);
Image apply_alpha_from_mask(const Image& img, const Image& mask);
