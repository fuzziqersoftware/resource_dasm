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

#include "ResourceFormats.hh"

namespace ResourceDASM {

using namespace phosg;

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

} // namespace ResourceDASM
