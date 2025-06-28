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

  // Renders text to an image, anchored by its upper-left corner at (x, y)
  // within the canvas image. Pixels that would be written outside of the
  // canvas' range are silently skipped. The text color is given as RGBA8888.
  void render_text(Image& canvas, const std::string& text, ssize_t x1, ssize_t y1, ssize_t x2, ssize_t y2, uint32_t color) const;

  // Computes the set of pixels to be written to render text. Calls
  // write_pixel(x, y) once for each pixel to be drawn. The (x, y) passed to
  // write_pixel are relative to the upper-left corner of the text.
  void render_text_custom(const std::string& text, std::function<void(size_t, size_t)> write_pixel) const;

protected:
  std::shared_ptr<const ResourceFile::DecodedFontResource> font;
};

} // namespace ResourceDASM
