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

  // Computes the set of pixels to be written to render text. Calls write(x, y)
  // once for each pixel to be drawn. The (x, y) passed to write() are relative
  // to the upper-left corner of the text.
  template <typename FnT>
    requires(std::is_invocable_r_v<void, FnT, size_t, size_t>)
  void render_text_custom(const std::string& text, FnT&& write) const {
    size_t x = 0, y = 0;
    for (size_t z = 0; z < text.size(); z++) {
      char ch = text[z];
      if (ch == '\n') {
        x = 0;
        y += this->font->full_bitmap.get_height() + this->font->leading;
      } else {
        const auto& glyph = this->font->glyph_for_char(ch);
        // Uncomment for debugging
        // fwrite_fmt(stderr, "Render: at {} x {} y {} glyph [width {} offset {}] char \'{}\'\n",
        //     z, x, y, glyph.width, glyph.offset, ch);
        for (ssize_t py = 0; py < static_cast<ssize_t>(this->font->full_bitmap.get_height()); py++) {
          for (ssize_t px = 0; px < glyph.bitmap_width; px++) {
            if (this->font->full_bitmap.read(glyph.bitmap_offset + px, py) == 0x000000FF) {
              write(x + glyph.offset + px, y + py);
            }
          }
        }
        x += glyph.width;
      }
    }
  }

  // Renders text to an image, anchored by its upper-left corner at (x, y)
  // within the canvas image. Pixels that would be written outside of the
  // canvas' range are silently skipped. The text color is given as RGBA8888.
  template <PixelFormat Format>
  void render_text(
      Image<Format>& ret, const std::string& text, ssize_t x1, ssize_t y1, ssize_t x2, ssize_t y2, uint32_t color) const {
    this->render_text_custom(text, [&](size_t px, size_t py) -> void {
      px += x1;
      py += y1;
      if (static_cast<ssize_t>(px) < x2 && static_cast<ssize_t>(py) < y2 && ret.check(px, py)) {
        ret.write(px, py, color);
      }
    });
  }

protected:
  std::shared_ptr<const ResourceFile::DecodedFontResource> font;
};

} // namespace ResourceDASM
