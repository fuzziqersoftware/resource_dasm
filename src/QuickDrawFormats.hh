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

  ImageG1 render() const;

  class Iterator {
  public:
    Iterator(const Region* region);
    Iterator(const Region* region, const Rect& rect);
    Iterator(const Iterator& other) = delete;
    Iterator(Iterator&& other) = delete;
    Iterator& operator=(const Iterator& other) = delete;
    Iterator& operator=(Iterator&& other) = delete;

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

// Decodes a monochrome image
ImageG1 decode_monochrome_image(const void* vdata, size_t size, size_t w, size_t h, size_t row_bytes = 0);

// Decodes a monochrome image (as above), but also decodes a second monochrome
// image immediately following the first, and applies the second image as an
// alpha mask over the first. Returns an RGBA image containing white, black, and
// transparent pixels. (The transparent pixels may also have white or black
// color values.)
ImageGA11 decode_monochrome_image_masked(const void* vdata, size_t size, size_t w, size_t h);

// Decodes a 4-bit color image, and applies the given color table to produce a
// full-color RGB image. If null is given for color_table, returns a full-color
// RGB image in which each pixel is one of 000000, 111111, 222222, ... FFFFFF
// (produced by directly placing each 4-bit value in each nybble of each pixel.)
ImageRGB888 decode_4bit_image(
    const void* vdata,
    size_t size,
    size_t w,
    size_t h,
    const std::vector<Color8>* color_table = &default_icon_color_table_4bit);

// Decodes an 8-bit color image, and applies the given color table to produce a
// full-color RGB image. If null is given for color_table, returns a full-color
// RGB image in which all channels of each pixel contain the corresponding value
// from the source pixel.
ImageRGB888 decode_8bit_image(
    const void* vdata,
    size_t size,
    size_t w,
    size_t h,
    const std::vector<Color8>* color_table = &default_icon_color_table_8bit);

// Decodes a color pixel map, optionally with a mask bitmap.
ImageRGB888 decode_color_image(const PixelMapHeader& header, const PixelMapData& pixel_map, const ColorTable* ctable);
ImageRGBA8888N decode_color_image_masked(
    const PixelMapHeader& header,
    const PixelMapData& pixel_map,
    const ColorTable* ctable,
    const PixelMapData& mask_map,
    size_t mask_row_bytes);

template <PixelFormat SourceFormat, PixelFormat MaskFormat>
  requires(Image<MaskFormat>::HAS_ALPHA)
ImageRGBA8888N apply_alpha_from_mask(const Image<SourceFormat>& image, const Image<MaskFormat>& mask) {
  if ((image.get_width() != mask.get_width()) || (image.get_height() != mask.get_height())) {
    throw std::runtime_error("dest and src dimensions are not equal");
  }
  ImageRGBA8888N ret(image.get_width(), image.get_height());
  for (size_t y = 0; y < image.get_height(); y++) {
    for (size_t x = 0; x < image.get_width(); x++) {
      ret.write(x, y, (image.read(x, y) & 0xFFFFFF00) | (mask.read(x, y) & 0x000000FF));
    }
  }
  return ret;
}

std::vector<Color8> to_color8(const std::vector<Color>& cs);
std::vector<Color8> to_color8(const std::vector<ColorTableEntry>& cs);
std::vector<Color8> to_color8(const std::vector<PaletteEntry>& cs);

} // namespace ResourceDASM
