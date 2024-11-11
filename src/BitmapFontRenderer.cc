#include "BitmapFontRenderer.hh"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

using namespace std;
using namespace phosg;

namespace ResourceDASM {

void replace_cr_with_lf_inplace(std::string& text) {
  for (char& ch : text) {
    if (ch == '\r') {
      ch = '\n';
    }
  }
}

std::string replace_cr_with_lf(const std::string& text) {
  std::string ret;
  ret.reserve(text.size());
  for (char ch : text) {
    ret.push_back((ch == '\r') ? '\n' : ch);
  }
  return ret;
}

BitmapFontRenderer::BitmapFontRenderer(std::shared_ptr<const ResourceFile::DecodedFontResource> font)
    : font(font) {}

std::string BitmapFontRenderer::wrap_text_to_pixel_width(const std::string& text, size_t max_width) const {
  // We only wrap at spaces and after hyphens
  string ret;
  size_t line_width_px = 0;
  size_t line_start_chars = 0;
  size_t last_valid_wrap_offset = 0;
  for (size_t offset_chars = 0; offset_chars < text.size(); offset_chars++) {
    // Uncomment for debugging
    // fprintf(stderr, "Wrap: at %zu line %zu valid %zu px %zu char \'%c\'\n",
    //     offset_chars, line_start_chars, last_valid_wrap_offset, line_width_px, text[offset_chars]);

    // If this character is a space, it's acceptable to wrap the line here. If
    // this character is a hyphen, it's acceptable to wrap the line immediately
    // after here.
    char ch = text[offset_chars];
    if ((ch == ' ') || (offset_chars > 0 && text[offset_chars - 1] == '-')) {
      last_valid_wrap_offset = offset_chars;
    }

    if (ch == '\n') {
      // The line ends before max_width; commit the entire line and the \n
      ret.append(text.data() + line_start_chars, offset_chars - line_start_chars + 1);
      line_width_px = 0;
      line_start_chars = offset_chars + 1;
      last_valid_wrap_offset = line_start_chars;

    } else {
      // If this character would put us past the limit, wrap the line
      const auto& glyph = this->font->glyph_for_char(ch);
      line_width_px += glyph.width;
      if (line_width_px > max_width) {
        if (line_start_chars == offset_chars) {
          throw std::runtime_error("Maximum width is too narrow; cannot commit zero-character line");
        } else if (last_valid_wrap_offset > line_start_chars && last_valid_wrap_offset <= offset_chars) {
          // The line can be wrapped partway through; commit the text up to
          // that point. If the wrap is a space, skip it (the newline will
          // replace it); otherwise, don't skip it
          ret.append(text.data() + line_start_chars, last_valid_wrap_offset - line_start_chars);
          ret.append(1, '\n');
          line_start_chars = last_valid_wrap_offset + ((text[last_valid_wrap_offset] == ' ') ? 1 : 0);
          line_width_px = 0;
        } else {
          // The line cannot be wrapped partway through; we'll have to break
          // the current word.
          ret.append(text.data() + line_start_chars, offset_chars - line_start_chars);
          ret.append(1, '\n');
          line_start_chars = offset_chars;
          last_valid_wrap_offset = offset_chars;
          line_width_px = glyph.width;
          if (line_width_px > max_width) {
            throw runtime_error("Maximum width is too small to contain even a single glyph");
          }
        }
      }
    }
  }
  // Commit the last line of text, if any
  if (line_start_chars < text.size()) {
    ret.append(text.data() + line_start_chars, text.size() - line_start_chars);
  }

  return ret;
}

std::pair<size_t, size_t> BitmapFontRenderer::pixel_dimensions_for_text(const std::string& text) const {
  if (text.empty()) {
    return make_pair(0, 0);
  }

  size_t max_width = 0;
  size_t num_lines = 1;
  size_t line_width = 0;
  for (char ch : text) {
    if (ch == '\n') {
      max_width = std::max<size_t>(max_width, line_width);
      line_width = 0;
      num_lines++;
    } else {
      line_width += this->font->glyph_for_char(ch).width;
    }
  }
  max_width = std::max<size_t>(max_width, line_width);

  // The height is the sum of all the line heights, plus the leadings between
  // the lines (num_lines - 1 of them)
  size_t overall_height = num_lines * (this->font->full_bitmap.get_height() + this->font->leading) - this->font->leading;
  return make_pair(max_width, overall_height);
}

void BitmapFontRenderer::render_text(Image& ret, const std::string& text, ssize_t x, ssize_t y, uint32_t color) const {
  auto write_pixel = [&](size_t px, size_t py) -> void {
    try {
      ret.write_pixel(x + px, y + py, color);
    } catch (const out_of_range&) {
    }
  };
  this->render_text_custom(text, write_pixel);
}

void BitmapFontRenderer::render_text_custom(const std::string& text, std::function<void(size_t, size_t)> write_pixel) const {
  size_t x = 0, y = 0;
  for (char ch : text) {
    if (ch == '\n') {
      x = 0;
      y += this->font->full_bitmap.get_height() + this->font->leading;
    } else {
      const auto& glyph = this->font->glyph_for_char(ch);
      for (ssize_t py = 0; py < static_cast<ssize_t>(this->font->full_bitmap.get_height()); py++) {
        for (ssize_t px = 0; px < glyph.bitmap_width; px++) {
          if (this->font->full_bitmap.read_pixel(glyph.bitmap_offset + px, py)) {
            write_pixel(x + glyph.offset + px, y + py);
          }
        }
      }
      x += glyph.width;
    }
  }
}

} // namespace ResourceDASM
