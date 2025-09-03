#pragma once

#include <stdint.h>

#include <functional>
#include <utility>
#include <vector>

#include <phosg/Image.hh>
#include <phosg/Strings.hh>

#include "ResourceFile.hh"

namespace ResourceDASM {

void replace_cr_with_lf_inplace(std::string& text);
std::string replace_cr_with_lf(const std::string& text);

class BitmapFontRenderer {
public:
  enum class HorizontalAlignment {
    LEFT = 0,
    CENTER,
    RIGHT,
  };

  BitmapFontRenderer(std::shared_ptr<const ResourceFile::DecodedFontResource> font);
  ~BitmapFontRenderer() = default;

  // Gets the underlying font definition.
  inline std::shared_ptr<const ResourceFile::DecodedFontResource> get_font() const {
    return this->font;
  }

  // Wraps the given text to fit within the given width. When a line exceeds
  // max_width (in pixels), the line is broken at the last space character or
  // after the last hyphen. If there were no spaces or hyphens, the line is
  // broken as close to max_width as possible.
  std::string wrap_text_to_pixel_width(const std::string& text, size_t max_width) const;

  // Computes the width and height of the area required to render all of the
  // given text.
  std::pair<size_t, size_t> pixel_dimensions_for_text(const std::string& text) const;

  // Computes the set of pixels to be written to render a single glyph. Calls
  // write(x, y) once for each pixel to be drawn. Returns the width of the
  // rendered glyph.
  template <typename FnT>
    requires(std::is_invocable_r_v<void, FnT, ssize_t, ssize_t>)
  size_t render_glyph_custom(char ch, ssize_t x, ssize_t y, FnT&& write) const {
    const auto& glyph = this->font->glyph_for_char(ch);
    for (ssize_t py = 0; py < static_cast<ssize_t>(this->font->full_bitmap.get_height()); py++) {
      for (ssize_t px = 0; px < glyph.bitmap_width; px++) {
        if (this->font->full_bitmap.read(glyph.bitmap_offset + px, py) == 0x000000FF) {
          write(x + glyph.offset + px, y + py);
        }
      }
    }
    return glyph.width;
  }

  // Computes the set of pixels to be written to render text. Calls write(x, y)
  // once for each pixel to be drawn. The y value passed to write() is relative
  // to the top of the text. The x value depends on the alignment mode: if it's
  // LEFT, x is nonnegative and relative to the left edge of the text; if it's
  // RIGHT, x is negative and relative to the right edge of the text; if it's
  // CENTER, x may be zero, positive, or negative and is relative to the center
  // line of the text.
  template <typename FnT>
    requires(std::is_invocable_r_v<void, FnT, ssize_t, ssize_t>)
  void render_text_custom(const std::string& text, HorizontalAlignment align, FnT&& write) const {
    if (align == HorizontalAlignment::LEFT) {
      // Left alignment: no need to render entire lines at once; just render
      // char by char (this skips splitting/copying the string)
      ssize_t x = 0, y = 0;
      for (size_t z = 0; z < text.size(); z++) {
        char ch = text[z];
        if (ch == '\n') {
          x = 0;
          y += this->font->full_bitmap.get_height() + this->font->leading;
        } else {
          x += this->render_glyph_custom(ch, x, y, std::move(write));
        }
      }

    } else {
      // Center or right alignment: have to render entire lines, since their x
      // start positions depend on the length of the line
      auto lines = split(text, '\n');

      ssize_t y = 0;
      for (size_t line_num = 0; line_num < lines.size(); line_num++) {
        const auto& line = lines[line_num];
        auto [line_w, line_h] = this->pixel_dimensions_for_text(line);
        if (line_h == 0) {
          line_h = this->font->full_bitmap.get_height();
        }
        line_h += this->font->leading;
        ssize_t x = -static_cast<ssize_t>((align == HorizontalAlignment::RIGHT) ? line_w : (line_w / 2));
        for (size_t z = 0; z < line.size(); z++) {
          x += this->render_glyph_custom(line[z], x, y, std::move(write));
        }
        y += line_h;
      }
    }
  }

  // Renders text to an image, anchored by its upper-left corner at (x, y)
  // within the canvas image. Pixels that would be written outside of the
  // canvas' range are silently skipped. The text color is given as RGBA8888.
  template <PixelFormat Format>
  void render_text(
      Image<Format>& ret,
      const std::string& text,
      ssize_t x1,
      ssize_t y1,
      ssize_t x2,
      ssize_t y2,
      uint32_t color,
      HorizontalAlignment align = HorizontalAlignment::LEFT) const {
    ssize_t x_delta;
    switch (align) {
      case HorizontalAlignment::LEFT:
        x_delta = x1; // px relative to left edge
        break;
      case HorizontalAlignment::CENTER:
        x_delta = (x2 + x1) / 2; // px relative to center line
        break;
      case HorizontalAlignment::RIGHT:
        x_delta = x2; // px relative to right edge (negative)
        break;
      default:
        throw std::logic_error("Unknown horizontal alignment mode");
    }

    this->render_text_custom(text, align, [&](ssize_t px, ssize_t py) -> void {
      px += x_delta;
      py += y1;
      if ((px < x2) && (py < y2) && ret.check(px, py)) {
        ret.write(px, py, color);
      }
    });
  }

  template <PixelFormat Format>
  Image<Format> wrap_and_render_text(
      const std::string& text,
      size_t width, // Required (cannot be zero)
      size_t height, // 0 = as tall as necessary
      uint32_t color,
      HorizontalAlignment align = HorizontalAlignment::LEFT) const {
    std::string wrapped_text = this->wrap_text_to_pixel_width(text, width);
    auto [w, h] = this->pixel_dimensions_for_text(wrapped_text);
    height = (height == 0) ? h : height;
    Image<Format> ret(width, height);
    this->render_text(ret, wrapped_text, 0, 0, width, height, color, align);
    return ret;
  }

protected:
  std::shared_ptr<const ResourceFile::DecodedFontResource> font;
};

} // namespace ResourceDASM
