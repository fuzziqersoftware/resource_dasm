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
  size_t x = 0;
  size_t commit_offset = 0;
  size_t commit_x = 0;
  for (size_t offset_chars = 0; offset_chars < text.size(); offset_chars++) {
    char ch = text[offset_chars];
    size_t end_x = (ch == '\n') ? 0 : (x + this->font->glyph_for_char(ch).width);

    // Uncomment fwrite_fmts for debugging
    // fwrite_fmt(stderr, "Wrap: at {} x {} end_x {} commit {} commit_x {} char \'{}\'\n",
    //     offset_chars, x, end_x, commit_offset, commit_x, ch);

    if (ch == '\n' || ch == ' ') {
      // Always commit these
      ret.append(text.data() + commit_offset, offset_chars - commit_offset + 1);
      commit_offset = offset_chars + 1;
      commit_x = end_x;
      // fwrite_fmt(stderr, "Wrap: commit_fixed \'{}\' {} end_x {} commit_x {}\n", ch, commit_offset, end_x, commit_x);

    } else if (end_x <= max_width) {
      // The line does not need to be wrapped yet. Commit if the character is a
      // hyphen
      // fwrite_fmt(stderr, "Wrap: ch ok\n");
      if (ch == '-') {
        ret.append(text.data() + commit_offset, offset_chars - commit_offset + 1);
        commit_offset = offset_chars + 1;
        commit_x = end_x;
        // fwrite_fmt(stderr, "Wrap: commit_hyphen \'{}\' {} end_x {} commit_x {}\n", ch, commit_offset, end_x, commit_x);
      }

    } else {
      // end_x > max_width and commit_offset < offset_chars
      if (commit_x > 0) {
        // Remove any trailing spaces
        while (!ret.empty() && (ret.back() == ' ')) {
          ret.pop_back();
        }
        // The current word should be wrapped and is not the only word on the
        // line (if it were, the last commit would have occurred at x=0).
        // Insert a newline in the wrapped text (which moves commit_x to zero)
        // but don't commit yet
        ret.append(1, '\n');
        x -= commit_x;
        end_x -= commit_x;
        commit_x = 0;
        // fwrite_fmt(stderr, "Wrap: commit not at beginning of line; x {} end_x {} commit_x {}\n", x, end_x, commit_x);
      }

      // If wrapping the line didn't help, then we have to break the current
      // word. Commit everything up to but not including this character, and
      // add a newline
      if (end_x > max_width) {
        ret.append(text.data() + commit_offset, offset_chars - commit_offset);
        ret.append(1, '\n');
        commit_offset = offset_chars;
        commit_x = 0;
        end_x -= x;
        // fwrite_fmt(stderr, "Wrap: end_x too long; commit {} end_x {} commit_x {}\n", commit_offset, end_x, commit_x);
      }

      if (end_x > max_width) {
        throw runtime_error("Maximum width is too small to contain even a single glyph");
      }
    }

    x = end_x;
  }
  // Commit whatever remains, if anything
  if (commit_offset < text.size()) {
    ret.append(text.data() + commit_offset, text.size() - commit_offset);
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

} // namespace ResourceDASM
