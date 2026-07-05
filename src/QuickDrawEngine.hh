#pragma once

#include <stdint.h>

#include <functional>
#include <utility>
#include <vector>

#include <phosg/Image.hh>
#include <phosg/Strings.hh>

#include "QuickDrawFormats.hh"
#include "ResourceFile.hh"

namespace ResourceDASM {

class QuickDrawPortInterface {
public:
  virtual ~QuickDrawPortInterface();

  // Image data accessors (Image, pixel map, or bitmap)
  virtual size_t width() const = 0;
  virtual size_t height() const = 0;
  virtual void write(ssize_t x, ssize_t y, uint32_t color) = 0;
  virtual void blit(
      const phosg::ImageRGB888& src,
      ssize_t dest_x,
      ssize_t dest_y,
      size_t w,
      size_t h,
      ssize_t src_x = 0,
      ssize_t src_y = 0,
      std::shared_ptr<Region> mask = nullptr,
      ssize_t mask_origin_x = 0,
      ssize_t mask_origin_y = 0) = 0;
  virtual void blit(
      const phosg::ImageRGBA8888N& src,
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

  virtual const phosg::ImageRGB888& get_pen_pixel_pattern() const = 0;
  virtual void set_pen_pixel_pattern(phosg::ImageRGB888&& z) = 0;
  virtual const phosg::ImageRGB888& get_fill_pixel_pattern() const = 0;
  virtual void set_fill_pixel_pattern(phosg::ImageRGB888&& z) = 0;
  virtual const phosg::ImageRGB888& get_background_pixel_pattern() const = 0;
  virtual void set_background_pixel_pattern(phosg::ImageRGB888&& z) = 0;
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

using FontHandler = std::function<std::shared_ptr<ResourceFile::DecodedFontResource>(
    int16_t id, const char* name, int16_t size, uint8_t style)>;

class QuickDrawEngine {
public:
  QuickDrawEngine() = default;
  ~QuickDrawEngine() = default;

  void set_port(QuickDrawPortInterface* port);
  void set_font_handler(FontHandler handler);

  static void set_default_font_handler(FontHandler handler);
  static FontHandler get_default_font_handler();

  void render_pict(const void* data, size_t size);

protected:
  QuickDrawPortInterface* port;
  FontHandler font_handler;
  static FontHandler default_font_handler;
  Color default_highlight_color;

  Rect pict_bounds;
  Rect pict_header_bounds; // Original bounds from header, not modified by SetOrigin
  Point pict_oval_size;
  Point pict_origin;
  Point pict_text_ratio_numerator;
  Point pict_text_ratio_denominator;
  uint8_t pict_version;
  bool pict_highlight_flag;
  Rect pict_last_rect;
  Point pict_text_origin; // Saved text origin for DVText/DHText

  static std::pair<Pattern, phosg::ImageRGB888> pict_read_pixel_pattern(phosg::StringReader& r);
  static std::shared_ptr<Region> pict_read_mask_region(phosg::StringReader& r, const Rect& dest_rect, Rect& mask_rect);

  void pict_skip_0(phosg::StringReader& r, uint16_t opcode);
  void pict_skip_2(phosg::StringReader& r, uint16_t opcode);
  void pict_skip_8(phosg::StringReader& r, uint16_t opcode);
  void pict_skip_12(phosg::StringReader& r, uint16_t opcode);
  void pict_skip_var16(phosg::StringReader& r, uint16_t opcode);
  void pict_skip_var32(phosg::StringReader& r, uint16_t opcode);
  void pict_skip_long_comment(phosg::StringReader& r, uint16_t opcode);
  void pict_unimplemented_opcode(phosg::StringReader& r, uint16_t opcode);

  void pict_set_clipping_region(phosg::StringReader& r, uint16_t opcode);
  void pict_set_font_number(phosg::StringReader& r, uint16_t opcode);
  void pict_set_font_style_flags(phosg::StringReader& r, uint16_t opcode);
  void pict_set_text_source_mode(phosg::StringReader& r, uint16_t opcode);
  void pict_set_text_extra_space(phosg::StringReader& r, uint16_t opcode);
  void pict_set_text_nonspace_extra_width(phosg::StringReader& r, uint16_t opcode);
  void pict_set_font_number_and_name(phosg::StringReader& r, uint16_t opcode);
  void pict_set_pen_size(phosg::StringReader& r, uint16_t opcode);
  void pict_set_pen_mode(phosg::StringReader& r, uint16_t opcode);
  void pict_set_background_pattern(phosg::StringReader& r, uint16_t opcode);
  void pict_set_pen_pattern(phosg::StringReader& r, uint16_t opcode);
  void pict_set_fill_pattern(phosg::StringReader& r, uint16_t opcode);
  void pict_set_background_pixel_pattern(phosg::StringReader& r, uint16_t opcode);
  void pict_set_pen_pixel_pattern(phosg::StringReader& r, uint16_t opcode);
  void pict_set_fill_pixel_pattern(phosg::StringReader& r, uint16_t opcode);
  void pict_set_oval_size(phosg::StringReader& r, uint16_t opcode);
  void pict_set_origin_dh_dv(phosg::StringReader& r, uint16_t opcode);
  void pict_set_text_ratio(phosg::StringReader& r, uint16_t opcode);
  void pict_set_text_size(phosg::StringReader& r, uint16_t opcode);
  void pict_set_foreground_color32(phosg::StringReader& r, uint16_t opcode);
  void pict_set_background_color32(phosg::StringReader& r, uint16_t opcode);
  void pict_set_version(phosg::StringReader& r, uint16_t opcode);
  void pict_set_highlight_mode_flag(phosg::StringReader& r, uint16_t opcode);
  void pict_set_highlight_color(phosg::StringReader& r, uint16_t opcode);
  void pict_set_foreground_color(phosg::StringReader& r, uint16_t opcode);
  void pict_set_background_color(phosg::StringReader& r, uint16_t opcode);
  void pict_set_op_color(phosg::StringReader& r, uint16_t opcode);
  void pict_set_default_highlight_color(phosg::StringReader& r, uint16_t opcode);

  void pict_fill_current_rect_with_pattern(const Pattern& pat, const phosg::ImageRGB888& pixel_pat);
  void pict_erase_last_rect(phosg::StringReader& r, uint16_t opcode);
  void pict_erase_rect(phosg::StringReader& r, uint16_t opcode);
  void pict_fill_last_rect(phosg::StringReader& r, uint16_t opcode);
  void pict_fill_rect(phosg::StringReader& r, uint16_t opcode);
  void pict_fill_last_oval(phosg::StringReader& r, uint16_t opcode);
  void pict_fill_oval(phosg::StringReader& r, uint16_t opcode);

  void pict_draw_line(Point start, Point end);
  void pict_line(phosg::StringReader& r, uint16_t opcode);
  void pict_line_from(phosg::StringReader& r, uint16_t opcode);
  void pict_short_line(phosg::StringReader& r, uint16_t opcode);
  void pict_short_line_from(phosg::StringReader& r, uint16_t opcode);

  void pict_frame_last_rect(phosg::StringReader& r, uint16_t opcode);
  void pict_frame_rect(phosg::StringReader& r, uint16_t opcode);
  void pict_paint_last_rect(phosg::StringReader& r, uint16_t opcode);
  void pict_paint_rect(phosg::StringReader& r, uint16_t opcode);

  void pict_render_text(const std::string& text);
  void pict_long_text(phosg::StringReader& r, uint16_t opcode);
  void pict_dh_text(phosg::StringReader& r, uint16_t opcode);
  void pict_dv_text(phosg::StringReader& r, uint16_t opcode);
  void pict_dh_dv_text(phosg::StringReader& r, uint16_t opcode);

  static std::string unpack_bits(
      phosg::StringReader& r, size_t row_count, uint16_t row_bytes, bool sizes_are_words, bool chunks_are_words);
  static std::string unpack_bits(phosg::StringReader& r, size_t row_count, uint16_t row_bytes, bool chunks_are_words);

  void pict_copy_bits_indexed_color(phosg::StringReader& r, uint16_t opcode);
  void pict_packed_copy_bits_direct_color(phosg::StringReader& r, uint16_t opcode);

  phosg::ImageRGBA8888N pict_decode_smc(
      const PictQuickTimeImageDescription& desc, const std::vector<ColorTableEntry>& clut, const std::string& data);
  phosg::ImageRGBA8888N pict_decode_rpza(const PictQuickTimeImageDescription& desc, const std::string& data);

  void pict_write_quicktime_data(phosg::StringReader& r, uint16_t opcode);

  static const std::vector<void (QuickDrawEngine::*)(phosg::StringReader&, uint16_t)> render_functions;
};

std::vector<ColorTableEntry> create_default_clut();

} // namespace ResourceDASM
