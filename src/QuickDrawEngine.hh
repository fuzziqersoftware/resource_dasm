#pragma once

#include <stdint.h>

#include <functional>
#include <utility>
#include <vector>

#include <phosg/Image.hh>
#include <phosg/Strings.hh>

#include "QuickDrawFormats.hh"

namespace ResourceDASM {

using namespace phosg;

class QuickDrawPortInterface {
public:
  virtual ~QuickDrawPortInterface();

  // Image data accessors (Image, pixel map, or bitmap)
  virtual size_t width() const = 0;
  virtual size_t height() const = 0;
  virtual void write_pixel(ssize_t x, ssize_t y, uint8_t r, uint8_t g, uint8_t b) = 0;
  virtual void blit(
      const Image& src,
      ssize_t dest_x,
      ssize_t dest_y,
      size_t w,
      size_t h,
      ssize_t src_x = 0,
      ssize_t src_y = 0,
      std::shared_ptr<Region> mask = nullptr,
      ssize_t mask_origin_x = 0,
      ssize_t mask_origin_y = 0) = 0;

  // External resource data accessors
  virtual std::vector<ColorTableEntry> read_clut(int16_t id) = 0;

  // QuickDraw state accessors
  virtual const Rect& get_bounds() const = 0;
  virtual void set_bounds(Rect z) = 0;
  virtual const Region& get_clip_region() const = 0;
  virtual void set_clip_region(Region&& z) = 0;

  virtual Color get_foreground_color() const = 0;
  virtual void set_foreground_color(Color z) = 0;
  virtual Color get_background_color() const = 0;
  virtual void set_background_color(Color z) = 0;
  virtual Color get_highlight_color() const = 0;
  virtual void set_highlight_color(Color z) = 0;
  virtual Color get_op_color() const = 0;
  virtual void set_op_color(Color z) = 0;

  virtual int16_t get_extra_space_nonspace() const = 0;
  virtual void set_extra_space_nonspace(int16_t z) = 0;
  virtual Fixed get_extra_space_space() const = 0;
  virtual void set_extra_space_space(Fixed z) = 0;

  virtual Point get_pen_loc() const = 0;
  virtual void set_pen_loc(Point z) = 0;
  virtual int16_t get_pen_loc_frac() const = 0;
  virtual void set_pen_loc_frac(int16_t z) = 0;
  virtual Point get_pen_size() const = 0;
  virtual void set_pen_size(Point z) = 0;
  virtual int16_t get_pen_mode() const = 0;
  virtual void set_pen_mode(int16_t z) = 0;
  virtual int16_t get_pen_visibility() const = 0;
  virtual void set_pen_visibility(int16_t z) = 0;

  virtual int16_t get_text_font() const = 0;
  virtual void set_text_font(int16_t z) = 0;
  virtual int16_t get_text_mode() const = 0;
  virtual void set_text_mode(int16_t z) = 0;
  virtual int16_t get_text_size() const = 0;
  virtual void set_text_size(int16_t z) = 0;
  virtual uint8_t get_text_style() const = 0;
  virtual void set_text_style(uint8_t z) = 0;

  virtual int16_t get_foreground_color_index() const = 0;
  virtual void set_foreground_color_index(int16_t z) = 0;
  virtual int16_t get_background_color_index() const = 0;
  virtual void set_background_color_index(int16_t z) = 0;

  virtual const Image& get_pen_pixel_pattern() const = 0;
  virtual void set_pen_pixel_pattern(Image&& z) = 0;
  virtual const Image& get_fill_pixel_pattern() const = 0;
  virtual void set_fill_pixel_pattern(Image&& z) = 0;
  virtual const Image& get_background_pixel_pattern() const = 0;
  virtual void set_background_pixel_pattern(Image&& z) = 0;
  virtual Pattern get_pen_mono_pattern() const = 0;
  virtual void set_pen_mono_pattern(Pattern z) = 0;
  virtual Pattern get_fill_mono_pattern() const = 0;
  virtual void set_fill_mono_pattern(Pattern z) = 0;
  virtual Pattern get_background_mono_pattern() const = 0;
  virtual void set_background_mono_pattern(Pattern z) = 0;

protected:
  QuickDrawPortInterface() = default;
};

class pict_contains_undecodable_quicktime : public std::exception {
public:
  pict_contains_undecodable_quicktime(std::string&& ext, std::string&& data);
  ~pict_contains_undecodable_quicktime() = default;

  std::string extension;
  std::string data;
};

class QuickDrawEngine {
public:
  QuickDrawEngine() = default;
  ~QuickDrawEngine() = default;

  void set_port(QuickDrawPortInterface* port);

  void render_pict(const void* data, size_t size);

protected:
  QuickDrawPortInterface* port;
  Color default_highlight_color;

  Rect pict_bounds;
  Point pict_oval_size;
  Point pict_origin;
  Point pict_text_ratio_numerator;
  Point pict_text_ratio_denominator;
  uint8_t pict_version;
  bool pict_highlight_flag;
  Rect pict_last_rect;

  static std::pair<Pattern, Image> pict_read_pixel_pattern(StringReader& r);
  static std::shared_ptr<Region> pict_read_mask_region(StringReader& r,
      const Rect& dest_rect, Rect& mask_rect);

  void pict_skip_0(StringReader& r, uint16_t opcode);
  void pict_skip_2(StringReader& r, uint16_t opcode);
  void pict_skip_8(StringReader& r, uint16_t opcode);
  void pict_skip_12(StringReader& r, uint16_t opcode);
  void pict_skip_var16(StringReader& r, uint16_t opcode);
  void pict_skip_var32(StringReader& r, uint16_t opcode);
  void pict_skip_long_comment(StringReader& r, uint16_t opcode);
  void pict_unimplemented_opcode(StringReader& r, uint16_t opcode);

  void pict_set_clipping_region(StringReader& r, uint16_t opcode);
  void pict_set_font_number(StringReader& r, uint16_t opcode);
  void pict_set_font_style_flags(StringReader& r, uint16_t opcode);
  void pict_set_text_source_mode(StringReader& r, uint16_t opcode);
  void pict_set_text_extra_space(StringReader& r, uint16_t opcode);
  void pict_set_text_nonspace_extra_width(StringReader& r, uint16_t opcode);
  void pict_set_font_number_and_name(StringReader& r, uint16_t opcode);
  void pict_set_pen_size(StringReader& r, uint16_t opcode);
  void pict_set_pen_mode(StringReader& r, uint16_t opcode);
  void pict_set_background_pattern(StringReader& r, uint16_t opcode);
  void pict_set_pen_pattern(StringReader& r, uint16_t opcode);
  void pict_set_fill_pattern(StringReader& r, uint16_t opcode);
  void pict_set_background_pixel_pattern(StringReader& r, uint16_t opcode);
  void pict_set_pen_pixel_pattern(StringReader& r, uint16_t opcode);
  void pict_set_fill_pixel_pattern(StringReader& r, uint16_t opcode);
  void pict_set_oval_size(StringReader& r, uint16_t opcode);
  void pict_set_origin_dh_dv(StringReader& r, uint16_t opcode);
  void pict_set_text_ratio(StringReader& r, uint16_t opcode);
  void pict_set_text_size(StringReader& r, uint16_t opcode);
  void pict_set_foreground_color32(StringReader& r, uint16_t opcode);
  void pict_set_background_color32(StringReader& r, uint16_t opcode);
  void pict_set_version(StringReader& r, uint16_t opcode);
  void pict_set_highlight_mode_flag(StringReader& r, uint16_t opcode);
  void pict_set_highlight_color(StringReader& r, uint16_t opcode);
  void pict_set_foreground_color(StringReader& r, uint16_t opcode);
  void pict_set_background_color(StringReader& r, uint16_t opcode);
  void pict_set_op_color(StringReader& r, uint16_t opcode);
  void pict_set_default_highlight_color(StringReader& r, uint16_t opcode);

  void pict_fill_current_rect_with_pattern(const Pattern& pat, const Image& pixel_pat);
  void pict_erase_last_rect(StringReader& r, uint16_t opcode);
  void pict_erase_rect(StringReader& r, uint16_t opcode);
  void pict_fill_last_rect(StringReader& r, uint16_t opcode);
  void pict_fill_rect(StringReader& r, uint16_t opcode);
  void pict_fill_last_oval(StringReader& r, uint16_t opcode);
  void pict_fill_oval(StringReader& r, uint16_t opcode);

  static std::string unpack_bits(StringReader& r, size_t row_count,
      uint16_t row_bytes, bool sizes_are_words, bool chunks_are_words);
  static std::string unpack_bits(StringReader& r, size_t row_count,
      uint16_t row_bytes, bool chunks_are_words);

  void pict_copy_bits_indexed_color(StringReader& r, uint16_t opcode);
  void pict_packed_copy_bits_direct_color(StringReader& r, uint16_t opcode);

  static Color8 decode_rgb555(uint16_t color);

  Image pict_decode_smc(
      const PictQuickTimeImageDescription& desc,
      const std::vector<ColorTableEntry>& clut,
      const std::string& data);
  Image pict_decode_rpza(
      const PictQuickTimeImageDescription& desc,
      const std::string& data);

  void pict_write_quicktime_data(StringReader& r, uint16_t opcode);

  static const std::vector<void (QuickDrawEngine::*)(StringReader&, uint16_t)> render_functions;
};

std::vector<ColorTableEntry> create_default_clut();

} // namespace ResourceDASM
