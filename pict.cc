#include "pict.hh"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <exception>
#include <phosg/Encoding.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <phosg/Time.hh>
#include <phosg/Filesystem.hh>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <string>

#include "quickdraw_formats.hh"
#include "pict.hh"

using namespace std;



pict_render_result::pict_render_result() : image(0, 0) { }



struct pict_fixed {
  int16_t whole;
  uint16_t decimal;

  pict_fixed(int16_t whole, uint16_t decimal) : whole(whole), decimal(decimal) { }

  void byteswap() {
    this->whole = bswap16(this->whole);
    this->decimal = bswap16(this->decimal);
  }
} __attribute__ ((packed));

struct pict_pattern {
  union {
    uint8_t rows[8];
    uint64_t pattern;
  };

  pict_pattern(uint64_t pattern) : pattern(pattern) { }

  bool pixel_at(uint8_t x, uint8_t y) const {
    return (this->rows[y & 7] >> (7 - (x & 7))) & 1;
  }
} __attribute__ ((packed));

struct pict_point {
  int16_t y;
  int16_t x;

  pict_point(int16_t x, int16_t y) : y(y), x(x) { }
  void byteswap() {
    this->x = bswap16(this->x);
    this->y = bswap16(this->y);
  }
} __attribute__ ((packed));

struct pict_polygon {
  uint16_t size;
  rect bounds;
  pict_point points[0];

  void byteswap() {
    this->size = bswap16(this->size);
    this->bounds.byteswap();
    for (size_t x = 0; x < this->size; x++) {
      this->points[x].byteswap();
    }
  }
} __attribute__ ((packed));

struct pict_color_table {
  string data;
  color_table* table;

  pict_color_table(StringReader& r) {
    size_t size = r.get<color_table>(false).size_swapped();
    this->data = r.read(size);
    this->table = reinterpret_cast<color_table*>(const_cast<char*>(this->data.data()));
    this->table->byteswap();
  }
};

struct pict_region {
  // note: unlike most of the others, this struct does not represent the actual
  // structure used in pict files, but is instead an interpretation thereof. use
  // the StringReader constructor instead of directly reading these.
  rect rect;
  unordered_set<int32_t> inversions;

  pict_region(StringReader& r) {
    size_t start_offset = r.where();

    uint16_t size = r.get_u16r();
    if (size < 0x0A) {
      throw runtime_error("region cannot be smaller than 10 bytes");
    }
    if (size & 1) {
      throw runtime_error("region size is not even");
    }

    this->rect = r.get<struct rect>();
    this->rect.byteswap();

    while (r.where() < start_offset + size) {
      int16_t y = r.get_u16r();
      if (y == 0x7FFF) {
        break;
      }
      while (r.where() < start_offset + size) {
        int16_t x = r.get_u16r();
        if (x == 0x7FFF) {
          break;
        }
        this->inversions.emplace(this->signature_for_inversion_point(x, y));
      }
    }

    if (r.where() != start_offset + size) {
      throw runtime_error("region ends before all data is parsed");
    }
  }

  static int32_t signature_for_inversion_point(int16_t x, int16_t y) {
    return (static_cast<int32_t>(x) << 16) | y;
  }

  bool is_inversion_point(int16_t x, int16_t y) const {
    return this->inversions.count(this->signature_for_inversion_point(x, y));
  }

  Image render() const {
    if (this->inversions.empty()) {
      return Image(0, 0);
    }

    Image ret(this->rect.width(), this->rect.height());
    ret.clear(0xFF, 0xFF, 0xFF);
    // TODO: this works but is quadratic; we can definitely do better. probably
    // something like propagating xors down and to the right as we work would
    // eliminate a lot of extra overwrites
    for (size_t y = 0; y < this->rect.height(); y++) {
      for (size_t x = 0; x < this->rect.width(); x++) {
        if (this->is_inversion_point(x + this->rect.x1, y + this->rect.y1)) {
          for (size_t yy = y; yy < this->rect.height(); yy++) {
            for (size_t xx = x; xx < this->rect.width(); xx++) {
              uint64_t r;
              ret.read_pixel(xx, yy, &r, NULL, NULL);
              ret.write_pixel(xx, yy, r ^ 0xFF, r ^ 0xFF, r ^ 0xFF);
            }
          }
        }
      }
    }

    return ret;
  }
};

struct pict_header {
  uint16_t size; // unused
  rect bounds;

  void byteswap() {
    this->size = bswap16(this->size);
    this->bounds.byteswap();
  }
} __attribute__ ((packed));

struct pict_subheader_v2 {
  int32_t version; // == -1
  pict_fixed bounds_x1;
  pict_fixed bounds_y1;
  pict_fixed bounds_x2;
  pict_fixed bounds_y2;
  uint32_t reserved2;

  void byteswap() {
    this->version = bswap16(this->version);
    this->bounds_y1.byteswap();
    this->bounds_x1.byteswap();
    this->bounds_y2.byteswap();
    this->bounds_x2.byteswap();
  }
} __attribute__ ((packed));

struct pict_subheader_v2_extended {
  int16_t version; // == -2
  uint16_t reserved1;
  pict_fixed horizontal_resolution_dpi;
  pict_fixed vertical_resolution_dpi;
  rect source_rect;
  uint16_t reserved2;

  void byteswap() {
    this->version = bswap16(this->version);
    this->horizontal_resolution_dpi.byteswap();
    this->vertical_resolution_dpi.byteswap();
    this->source_rect.byteswap();
  }
} __attribute__ ((packed));

union pict_subheader {
  pict_subheader_v2 v2;
  pict_subheader_v2_extended v2e;
};



struct pict_render_state {
  pict_header header;

  uint8_t version; // must be 1 or 2

  rect clip_rect;
  Image clip_region_mask;

  pict_point pen_location;
  pict_point pen_size;
  uint16_t pen_mode;

  pict_pattern pen_pattern;
  pict_pattern fill_pattern;
  pict_pattern background_pattern;
  Image pen_pixel_pattern;
  Image fill_pixel_pattern;
  Image background_pixel_pattern;

  color foreground_color;
  color background_color;
  color op_color;
  bool highlight_mode;
  color highlight_color;
  color default_highlight_color;

  rect last_rect;
  pict_point oval_size;
  pict_point origin;

  int16_t text_font_number;
  string text_font_name;
  uint16_t text_size;
  uint8_t text_style_flags;
  uint16_t text_source_mode;
  pict_fixed text_extra_space;
  uint16_t text_nonspace_extra_width;
  pict_point text_ratio_numerator;
  pict_point text_ratio_denominator;

  Image canvas;

  // these are used to handle compressed images. currently we don't decompress
  // them; we only extract them from the PICT and save them as-is. this means we
  // can't do drawing operations on the canvas before or after loading a
  // compressed image!
  bool canvas_modified;
  string embedded_image_format;
  string embedded_image_data;

  pict_render_state(const pict_header& header) :
      header(header),
      version(1),
      clip_rect(this->header.bounds),
      clip_region_mask(0, 0),
      pen_location(0, 0),
      pen_size(1, 1),
      pen_mode(0),
      pen_pattern(0x0000000000000000),
      fill_pattern(0x0000000000000000),
      background_pattern(0xFFFFFFFFFFFFFFFF),
      pen_pixel_pattern(0, 0),
      fill_pixel_pattern(0, 0),
      background_pixel_pattern(0, 0),
      foreground_color(0xFFFF, 0xFFFF, 0xFFFF),
      background_color(0x0000, 0x0000, 0x0000),
      op_color(0xFFFF, 0x0000, 0xFFFF),
      highlight_mode(false),
      highlight_color(0xFFFF, 0x0000, 0x0000),
      default_highlight_color(0xFFFF, 0x0000, 0x0000),
      last_rect(0, 0, 0, 0),
      oval_size(0, 0),
      origin(0, 0),
      text_font_number(-1),
      text_size(12),
      text_style_flags(0),
      text_source_mode(0),
      text_extra_space(0, 0),
      text_nonspace_extra_width(0),
      text_ratio_numerator(0, 0),
      text_ratio_denominator(0, 0),
      canvas(abs(this->header.bounds.x2 - this->header.bounds.x1),
             abs(this->header.bounds.y2 - this->header.bounds.y1), true),
      canvas_modified(false) { }

  void write_canvas_pixel(ssize_t x, ssize_t y, uint64_t r, uint64_t g, uint64_t b, uint64_t a = 0xFF) {
    if (!this->clip_rect.contains(x, y) || !this->header.bounds.contains(x, y)) {
      return;
    }
    if (this->clip_region_mask.get_width()) {
      uint64_t r, g, b;
      this->clip_region_mask.read_pixel(x - this->clip_rect.x1,
          y - this->clip_rect.y1, &r, &g, &b);
      if (!r && !g && !b) {
        return;
      }
    }
    if (!this->embedded_image_format.empty()) {
      throw runtime_error("PICT requires drawing opcodes after QuickTime data");
    }
    this->canvas.write_pixel(x - this->header.bounds.x1,
        y - this->header.bounds.y1, r, g, b, a);
    this->canvas_modified = true;
  }
};

static void skip_0(StringReader& r, pict_render_state& st, uint16_t opcode) { }

static void skip_2(StringReader& r, pict_render_state& st, uint16_t opcode) {
  r.go(r.where() + 2);
}

static void skip_8(StringReader& r, pict_render_state& st, uint16_t opcode) {
  r.go(r.where() + 8);
}

static void skip_12(StringReader& r, pict_render_state& st, uint16_t opcode) {
  r.go(r.where() + 12);
}

static void skip_var16(StringReader& r, pict_render_state& st, uint16_t opcode) {
  uint16_t len = r.get_u16r();
  r.go(r.where() + len);
}

static void skip_var32(StringReader& r, pict_render_state& st, uint16_t opcode) {
  uint32_t len = r.get_u32r();
  r.go(r.where() + len);
}

static void skip_long_comment(StringReader& r, pict_render_state& st, uint16_t opcode) {
  r.go(r.where() + 2); // type (unused)
  uint16_t size = r.get_u16r();
  r.go(r.where() + size);
}

static void unimplemented_opcode(StringReader& r, pict_render_state& st, uint16_t opcode) {
  throw runtime_error(string_printf("unimplemented opcode %04hX at offset %zX",
      opcode, r.where() - st.version));
}



// state modification opcodes

static void set_clipping_region(StringReader& r, pict_render_state& st, uint16_t opcode) {
  pict_region rgn(r);
  st.clip_rect = rgn.rect;
  st.clip_region_mask = rgn.render();
}

static void set_font_number(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.text_font_number = r.get_u16r();
}

static void set_font_style_flags(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.text_style_flags = r.get_u8();
}

static void set_text_source_mode(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.text_source_mode = r.get_u16r();
}

static void set_text_extra_space(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.text_extra_space = r.get<pict_fixed>();
  st.text_extra_space.byteswap();
}

static void set_text_nonspace_extra_width(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.text_nonspace_extra_width = r.get_u16r();
}

static void set_font_number_and_name(StringReader& r, pict_render_state& st, uint16_t opcode) {
  uint16_t data_size = r.get_u16r();
  st.text_font_number = r.get_u16r();
  uint8_t font_name_bytes = r.get_u8();
  if (font_name_bytes != data_size - 3) {
    throw runtime_error("font name length does not align with command data length");
  }
  st.text_font_name = r.read(font_name_bytes);
}

static void set_pen_size(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.pen_size = r.get<pict_point>();
  st.pen_size.byteswap();
}

static void set_pen_mode(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.pen_mode = r.get_u16r();
}

static void set_background_pattern(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.background_pattern = r.get<pict_pattern>();
  st.background_pixel_pattern = Image(0, 0);
}

static void set_pen_pattern(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.pen_pattern = r.get<pict_pattern>();
  st.pen_pixel_pattern = Image(0, 0);
}

static void set_fill_pattern(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.fill_pattern = r.get<pict_pattern>();
  st.fill_pixel_pattern = Image(0, 0);
}

static pair<pict_pattern, Image> read_pixel_pattern(StringReader& r) {
  uint16_t type = r.get_u16r();
  pict_pattern monochrome_pattern = r.get<pict_pattern>();

  if (type == 1) { // normal pattern
    pixel_map_header header = r.get<pixel_map_header>();
    header.byteswap();
    pict_color_table ctable(r);

    uint16_t row_bytes = header.flags_row_bytes & 0x7FFF;
    string data = r.read(header.bounds.height() * row_bytes);
    const pixel_map_data* pixel_map = reinterpret_cast<const pixel_map_data*>(data.data());

    return make_pair(monochrome_pattern, decode_color_image(header, *pixel_map, *ctable.table));

  } else if (type == 2) { // dither pattern
    color c = r.get<color>();
    c.byteswap();
    // TODO: figure out how dither patterns work
    throw runtime_error("dither patterns are not supported");

  } else {
    throw runtime_error("unknown pattern type");
  }
}

static void set_background_pixel_pattern(StringReader& r, pict_render_state& st, uint16_t opcode) {
  auto p = read_pixel_pattern(r);
  st.background_pattern = p.first;
  st.background_pixel_pattern = p.second;
}

static void set_pen_pixel_pattern(StringReader& r, pict_render_state& st, uint16_t opcode) {
  auto p = read_pixel_pattern(r);
  st.pen_pattern = p.first;
  st.pen_pixel_pattern = p.second;
}

static void set_fill_pixel_pattern(StringReader& r, pict_render_state& st, uint16_t opcode) {
  auto p = read_pixel_pattern(r);
  st.fill_pattern = p.first;
  st.fill_pixel_pattern = p.second;
}

static void set_oval_size(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.oval_size = r.get<pict_point>();
  st.oval_size.byteswap();
}

static void set_origin_dh_dv(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.origin = r.get<pict_point>();
  st.origin.byteswap();
}

static void set_text_ratio(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.text_ratio_numerator = r.get<pict_point>();
  st.text_ratio_numerator.byteswap();
  st.text_ratio_denominator = r.get<pict_point>();
  st.text_ratio_denominator.byteswap();
}

static void set_text_size(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.text_size = r.get_u16r();
}

static void set_foreground_color32(StringReader& r, pict_render_state& st, uint16_t opcode) {
  uint32_t color = r.get_u32r();
  st.foreground_color.r = ((color >> 8) & 0xFF00) | ((color >> 16) & 0x00FF);
  st.foreground_color.g = ((color >> 0) & 0xFF00) | ((color >> 8) & 0x00FF);
  st.foreground_color.b = ((color << 8) & 0xFF00) | ((color >> 0) & 0x00FF);
}

static void set_background_color32(StringReader& r, pict_render_state& st, uint16_t opcode) {
  uint32_t color = r.get_u32r();
  st.background_color.r = ((color >> 8) & 0xFF00) | ((color >> 16) & 0x00FF);
  st.background_color.g = ((color >> 0) & 0xFF00) | ((color >> 8) & 0x00FF);
  st.background_color.b = ((color << 8) & 0xFF00) | ((color >> 0) & 0x00FF);
}

static void set_version(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.version = r.get_u8();
  if (st.version != 1 && st.version != 2) {
    throw runtime_error("version is not 1 or 2");
  }
  if ((st.version == 2) && (r.get_u8() != 0xFF)) {
    throw runtime_error("version 2 picture is not version 02FF");
  }
}

static void set_highlight_mode_flag(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.highlight_mode = true;
}

static void set_highlight_color(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.highlight_color = r.get<color>();
  st.highlight_color.byteswap();
}

static void set_foreground_color(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.foreground_color = r.get<color>();
  st.foreground_color.byteswap();
}

static void set_background_color(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.background_color = r.get<color>();
  st.background_color.byteswap();
}

static void set_op_color(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.op_color = r.get<color>();
  st.op_color.byteswap();
}

static void set_default_highlight_color(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.highlight_color = st.default_highlight_color;
}



// simple shape opcodes

static void fill_current_rect_with_pattern(pict_render_state& st,
    const pict_pattern& pat, const Image& pixel_pat) {
  if (pixel_pat.get_width() && pixel_pat.get_height()) {
    for (ssize_t y = st.last_rect.y1; y < st.last_rect.y2; y++) {
      for (ssize_t x = st.last_rect.x1; x < st.last_rect.x2; x++) {
        uint64_t r, g, b;
        pixel_pat.read_pixel(x % pixel_pat.get_width(), y % pixel_pat.get_height(), &r, &g, &b);
        st.write_canvas_pixel(x, y, r, g, b);
      }
    }
  } else {
    for (ssize_t y = st.last_rect.y1; y < st.last_rect.y2; y++) {
      for (ssize_t x = st.last_rect.x1; x < st.last_rect.x2; x++) {
        uint8_t value = pat.pixel_at(x - st.header.bounds.x1, y - st.header.bounds.y1) ? 0x00 : 0xFF;
        st.write_canvas_pixel(x, y, value, value, value);
      }
    }
  }
}

static void erase_last_rect(StringReader& r, pict_render_state& st, uint16_t opcode) {
  fill_current_rect_with_pattern(st, st.background_pattern, st.background_pixel_pattern);
}

static void erase_rect(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.last_rect = r.get<rect>();
  st.last_rect.byteswap();
  fill_current_rect_with_pattern(st, st.background_pattern, st.background_pixel_pattern);
}

static void fill_last_rect(StringReader& r, pict_render_state& st, uint16_t opcode) {
  fill_current_rect_with_pattern(st, st.fill_pattern, st.fill_pixel_pattern);
}

static void fill_rect(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.last_rect = r.get<rect>();
  st.last_rect.byteswap();
  fill_current_rect_with_pattern(st, st.fill_pattern, st.fill_pixel_pattern);
}

static void fill_last_oval(StringReader& r, pict_render_state& st, uint16_t opcode) {
  double x_center = static_cast<double>(st.last_rect.x2 + st.last_rect.x1) / 2.0;
  double y_center = static_cast<double>(st.last_rect.y2 + st.last_rect.y1) / 2.0;
  double width = st.last_rect.x2 - st.last_rect.x1;
  double height = st.last_rect.y2 - st.last_rect.y1;
  for (ssize_t y = st.last_rect.y1; y < st.last_rect.y2; y++) {
    for (ssize_t x = st.last_rect.x1; x < st.last_rect.x2; x++) {
      double x_dist = (static_cast<double>(x) - x_center) / width;
      double y_dist = (static_cast<double>(y) - y_center) / height;
      if (x_dist * x_dist + y_dist * y_dist > 0.25) {
        continue;
      }
      uint8_t value = st.fill_pattern.pixel_at(x - st.header.bounds.x1, y - st.header.bounds.y1) ? 0x00 : 0xFF;
      st.write_canvas_pixel(x, y, value, value, value);
    }
  }
}

static void fill_oval(StringReader& r, pict_render_state& st, uint16_t opcode) {
  st.last_rect = r.get<rect>();
  st.last_rect.byteswap();
  fill_last_oval(r, st, opcode);
}



// bits opcodes

struct pict_copy_bits_monochrome_args {
  bit_map_header header;
  rect source_rect;
  rect dest_rect;
  uint16_t mode;

  void byteswap() {
    this->header.byteswap();
    this->source_rect.byteswap();
    this->dest_rect.byteswap();
    this->mode = bswap16(this->mode);
  }
};

/* there's no struct pict_packed_copy_bits_indexed_color_args because the color
 * table is a variable size and comes early in the format. if there were a
 * struct it would look like this:
 * struct pict_packed_copy_bits_indexed_color_args {
 *   pixel_map_header header;
 *   color_table ctable; // variable size
 *   rect source_rect;
 *   rect dest_rect;
 *   uint16_t mode;
 * };
 */

static string unpack_bits(StringReader& r, size_t w, size_t h,
    uint16_t row_bytes, bool sizes_are_words, bool chunks_are_words) {
  string ret;
  size_t expected_size = row_bytes * h;
  ret.reserve(expected_size);

  for (size_t y = 0; y < h; y++) {
    uint16_t packed_row_bytes = sizes_are_words ? r.get_u16r() : r.get_u8();
    for (size_t row_end_offset = r.where() + packed_row_bytes; r.where() < row_end_offset;) {
      int16_t count = r.get_s8();
      if (count < 0) { // RLE segment
        if (chunks_are_words) {
          uint16_t value = r.get_u16r();
          for (size_t x = 0; x < -(count - 1); x++) {
            ret.push_back((value >> 8) & 0xFF);
            ret.push_back(value & 0xFF);
          }
        } else {
          ret.insert(ret.size(), -(count - 1), r.get_u8());
        }
      } else { // direct segment
        if (chunks_are_words) {
          ret += r.read((count + 1) * 2);
        } else {
          ret += r.read(count + 1);
        }
      }
    }
    if (ret.size() != row_bytes * (y + 1)) {
      throw runtime_error(string_printf("packed data size is incorrect on row %zu at offset %zX (expected %zX, have %zX)",
          y, r.where(), row_bytes * (y + 1), ret.size()));
    }
  }
  if (row_bytes * h != ret.size()) {
    throw runtime_error(string_printf("unpacked data size is incorrect (expected %zX, have %zX)",
        row_bytes * h, ret.size()));
  }
  return ret;
}

static string unpack_bits(StringReader& r, size_t w, size_t h,
    uint16_t row_bytes, bool chunks_are_words) {
  size_t start_offset = r.where();
  string failure_strs[2];
  for (size_t x = 0; x < 2; x++) {
    try {
      // if row_bytes > 250, word sizes are most likely to be correct, so try
      // that first
      return unpack_bits(r, w, h, row_bytes, x ^ (row_bytes > 250), chunks_are_words);
    } catch (const exception& e) {
      failure_strs[x ^ (row_bytes > 250)] = e.what();
      r.go(start_offset);
    }
  }
  throw runtime_error(string_printf("failed to unpack data with either byte sizes (%s) or word sizes (%s)",
      failure_strs[0].c_str(), failure_strs[1].c_str()));
}

static shared_ptr<Image> read_mask_region(StringReader& r, const rect& dest_rect, rect& mask_rect) {
  pict_region rgn(r);
  shared_ptr<Image> mask_region(new Image(rgn.render()));
  if (!mask_region->get_width() && !mask_region->get_height()) {
    // region is empty
    mask_region.reset();
  }
  if (mask_region.get() &&
      ((mask_region->get_width() != dest_rect.width()) ||
       (mask_region->get_height() != dest_rect.height()))) {
    string dest_s = dest_rect.str();
    throw runtime_error(string_printf("mask region dimensions (%zux%zu) do not match dest %s",
        mask_region->get_width(), mask_region->get_height(), dest_s.c_str()));
  }
  mask_rect = rgn.rect;
  return mask_region;
}

static void copy_bits_indexed_color(StringReader& r, pict_render_state& st, uint16_t opcode) {
  bool is_packed = opcode & 0x08;
  bool has_mask_region = opcode & 0x01;

  rect bounds;
  rect source_rect;
  rect dest_rect;
  rect mask_region_rect;
  uint16_t mode;
  shared_ptr<Image> mask_region;
  Image source_image(0, 0);

  // TODO: should we support pixmaps in v1? currently we do, but I don't know if
  // this is technically correct behavior
  bool is_pixmap = r.get_u8(false) & 0x80;
  if (is_pixmap) {
    auto header = r.get<pixel_map_header>();
    header.byteswap();
    bounds = header.bounds;

    pict_color_table ctable(r);

    source_rect = r.get<rect>();
    source_rect.byteswap();
    dest_rect = r.get<rect>();
    dest_rect.byteswap();
    // TODO: figure out where/how to use this
    /* uint16_t mode = */ r.get_u16r();

    if ((source_rect.width() != dest_rect.width()) ||
        (source_rect.height() != dest_rect.height())) {
      throw runtime_error("source and destination rect dimensions do not match");
    }

    if (has_mask_region) {
      mask_region = read_mask_region(r, dest_rect, mask_region_rect);
    }

    uint16_t row_bytes = header.flags_row_bytes & 0x7FFF;
    string data = is_packed ?
        unpack_bits(r, header.bounds.width(), header.bounds.height(), row_bytes, header.pixel_size == 0x10) :
        r.read(header.bounds.height() * row_bytes);
    const pixel_map_data* pixel_map = reinterpret_cast<const pixel_map_data*>(data.data());

    source_image = decode_color_image(header, *pixel_map, *ctable.table);

  } else {
    auto args = r.get<pict_copy_bits_monochrome_args>();
    args.byteswap();

    if (!args.header.bounds.contains(args.source_rect)) {
      string source_s = args.source_rect.str();
      string bounds_s = args.header.bounds.str();
      throw runtime_error(string_printf("source %s is not within bounds %s", source_s.c_str(), bounds_s.c_str()));
    }
    if ((args.source_rect.width() != args.dest_rect.width()) ||
        (args.source_rect.height() != args.dest_rect.height())) {
      throw runtime_error("source and destination rect dimensions do not match");
    }
    bounds = args.header.bounds;
    source_rect = args.source_rect;
    dest_rect = args.dest_rect;
    mode = args.mode;

    if (has_mask_region) {
      mask_region = read_mask_region(r, dest_rect, mask_region_rect);
    }

    string data = is_packed ?
        unpack_bits(r, args.header.bounds.width(), args.header.bounds.height(), args.header.flags_row_bytes, false) :
        r.read(args.header.bounds.height() * args.header.flags_row_bytes);
    source_image = decode_monochrome_image(data.data(), data.size(),
        args.header.bounds.width(), args.header.bounds.height(),
        args.header.flags_row_bytes);
  }

  // TODO: the clipping rect should apply here
  if (mask_region.get()) {
    if (mask_region_rect != source_rect) {
      throw runtime_error("mask region rect is not same as source rect");
    }
    st.canvas.mask_blit(source_image,
        dest_rect.x1 - st.header.bounds.x1,
        dest_rect.y1 - st.header.bounds.y1,
        source_rect.x2 - source_rect.x1,
        source_rect.y2 - source_rect.y1,
        source_rect.x1 - bounds.x1,
        source_rect.y1 - bounds.y1,
        *mask_region);
  } else {
    st.canvas.blit(source_image,
        dest_rect.x1 - st.header.bounds.x1,
        dest_rect.y1 - st.header.bounds.y1,
        source_rect.x2 - source_rect.x1,
        source_rect.y2 - source_rect.y1,
        source_rect.x1 - bounds.x1,
        source_rect.y1 - bounds.y1);
  }
  st.canvas_modified = true;
}

struct pict_packed_copy_bits_direct_color_args {
  uint32_t base_address; // unused
  pixel_map_header header;
  rect source_rect;
  rect dest_rect;
  uint16_t mode;

  void byteswap() {
    this->header.byteswap();
    this->source_rect.byteswap();
    this->dest_rect.byteswap();
    this->mode = bswap16(this->mode);
  }
};

static void packed_copy_bits_direct_color(StringReader& r, pict_render_state& st, uint16_t opcode) {
  bool has_mask_region = opcode & 0x01;

  auto args = r.get<pict_packed_copy_bits_direct_color_args>();
  args.byteswap();

  if (!args.header.bounds.contains(args.source_rect)) {
    string source_s = args.source_rect.str();
    string bounds_s = args.header.bounds.str();
    throw runtime_error(string_printf("source %s is not within bounds %s", source_s.c_str(), bounds_s.c_str()));
  }
  if ((args.source_rect.width() != args.dest_rect.width()) ||
      (args.source_rect.height() != args.dest_rect.height())) {
    throw runtime_error("source and destination rect dimensions do not match");
  }

  shared_ptr<Image> mask_region;
  rect mask_region_rect;
  if (has_mask_region) {
    mask_region = read_mask_region(r, args.dest_rect, mask_region_rect);
  }

  size_t bytes_per_pixel;
  if (args.header.component_size == 8) {
    bytes_per_pixel = args.header.component_count;
    if ((args.header.component_count != 3) && (args.header.component_count != 4)) {
      throw runtime_error("for 5-bit channels, image must have 3 or 4 components");
    }
  } else if (args.header.component_size == 5) {
    // round up to the next byte boundary
    bytes_per_pixel = ((args.header.component_count * 5) + 7) / 8;
    if (args.header.component_count != 3) {
      throw runtime_error("for 5-bit channels, image must have 3 components");
    }
  } else {
    throw runtime_error("only 8-bit and 5-bit channels are supported");
  }
  size_t row_bytes = args.header.bounds.width() * bytes_per_pixel;
  string data = unpack_bits(r, args.header.bounds.width(), args.header.bounds.height(), row_bytes, args.header.pixel_size == 0x10);

  if (mask_region.get() && (mask_region_rect != args.source_rect)) {
    throw runtime_error("mask region rect is not same as source rect");
  }

  for (ssize_t y = 0; y < args.source_rect.height(); y++) {
    size_t row_offset = row_bytes * y;
    for (ssize_t x = 0; x < args.source_rect.width(); x++) {
      if (mask_region.get()) {
        uint64_t r, g, b;
        mask_region->read_pixel(x + args.source_rect.x1 - mask_region_rect.x1,
            y + args.source_rect.y1 - mask_region_rect.y1, &r, &g, &b);
        if (r || g || b) {
          continue;
        }
      }

      uint8_t r_value, g_value, b_value;
      if ((args.header.component_size == 8) && (args.header.component_count == 3)) {
        r_value = data[row_offset + x];
        g_value = data[row_offset + (row_bytes / 3) + x];
        b_value = data[row_offset + (2 * row_bytes / 3) + x];

      } else if ((args.header.component_size == 8) && (args.header.component_count == 4)) {
        // the first component is ignored
        r_value = data[row_offset + (row_bytes / 4) + x];
        g_value = data[row_offset + (2 * row_bytes / 4) + x];
        b_value = data[row_offset + (3 * row_bytes / 4) + x];

      } else if (args.header.component_size == 5) {
        // xrgb1555. see decode_color_image for an explanation of the bit
        // manipulation below
        uint16_t value = bswap16(*reinterpret_cast<const uint16_t*>(&data[row_offset + 2 * x]));
        r_value = ((value >> 7) & 0xF8) | ((value >> 12) & 0x07);
        g_value = ((value >> 2) & 0xF8) | ((value >> 7) & 0x07);
        b_value = ((value << 3) & 0xF8) | ((value >> 2) & 0x07);

      } else {
        throw logic_error("unimplemented channel width");
      }

      st.write_canvas_pixel(x + args.dest_rect.x1, y + args.dest_rect.y1,
          r_value, g_value, b_value);
    }
  }
}



// QuickTime embedded file support

struct pict_quicktime_image_description {
  uint32_t size; // includes variable-length fields
  uint32_t codec;
  uint32_t reserved1;
  uint16_t reserved2;
  uint16_t data_ref_index; // also reserved
  uint16_t algorithm_version;
  uint16_t revision_level; // version of compression software, essentially
  uint32_t vendor;
  uint32_t temporal_quality;
  uint32_t spatial_quality;
  uint16_t width;
  uint16_t height;
  pict_fixed h_res;
  pict_fixed v_res;
  uint32_t data_size;
  uint16_t frame_count;
  char name[32];
  uint16_t bit_depth;
  uint16_t clut_id;

  void byteswap() {
    this->size = bswap32(this->size);
    this->codec = bswap32(this->codec);
    this->reserved1 = bswap32(this->reserved1);
    this->reserved2 = bswap16(this->reserved2);
    this->data_ref_index = bswap16(this->data_ref_index);
    this->algorithm_version = bswap16(this->algorithm_version);
    this->revision_level = bswap16(this->revision_level);
    this->vendor = bswap32(this->vendor);
    this->temporal_quality = bswap32(this->temporal_quality);
    this->spatial_quality = bswap32(this->spatial_quality);
    this->width = bswap16(this->width);
    this->height = bswap16(this->height);
    this->h_res.byteswap();
    this->v_res.byteswap();
    this->data_size = bswap32(this->data_size);
    this->frame_count = bswap16(this->frame_count);
    this->bit_depth = bswap16(this->bit_depth);
    this->clut_id = bswap16(this->clut_id);
  }
} __attribute__((packed));

static Image decode_smc(
    const pict_quicktime_image_description& desc,
    const vector<struct color_table_entry>& clut,
    const string& data) {
  if (data.size() < 4) {
    throw runtime_error("smc-encoded image too small for header");
  }

  uint8_t color_index_cache2[0x100][2];
  uint8_t color_index_cache2_pos = 0;
  uint8_t color_index_cache4[0x100][4];
  uint8_t color_index_cache4_pos = 0;
  uint8_t color_index_cache8[0x100][8];
  uint8_t color_index_cache8_pos = 0;

  StringReader r(data.data(), data.size());
  r.get_u8(); // skip flags byte
  uint32_t encoded_size = r.get_u24r();
  if (encoded_size != data.size()) {
    throw runtime_error("smc-encoded image has incorrect size header");
  }

  Image ret(desc.width, desc.height, true);
  ret.clear(0x00, 0x00, 0x00, 0x00);
  size_t prev_x2, prev_y2;
  size_t prev_x1, prev_y1;
  size_t x = 0, y = 0;
  auto advance_block = [&]() {
    if (y >= ret.get_height()) {
      throw runtime_error("smc decoder advanced beyond end of output image");
    }
    prev_x2 = prev_x1;
    prev_y2 = prev_y1;
    prev_x1 = x;
    prev_y1 = y;
    x += 4;
    if (x >= ret.get_width()) {
      y += 4;
      x = 0;
    }
  };

  auto write_color = [&](size_t x, size_t y, uint8_t color_index) {
    const auto& color_entry = clut.at(color_index);
    try {
      ret.write_pixel(x, y, color_entry.r / 0x101, color_entry.g / 0x101,
          color_entry.b / 0x101, 0xFF);
    } catch (const runtime_error&) { }
  };

  while (!r.eof()) {
    uint8_t opcode = r.get_u8();
    if ((opcode & 0xF0) == 0xF0) {
      throw runtime_error("smc-encoded contains opcode 0xF0");
    }
    switch (opcode & 0xE0) {
      case 0x00: { // skip blocks
        uint8_t num_blocks = ((opcode & 0x10) ? r.get_u8() : (opcode & 0x0F)) + 1;
        for (size_t z = 0; z < num_blocks; z++) {
          advance_block();
        }
        break;
      }

      case 0x20: { // repeat last block
        uint8_t num_blocks = ((opcode & 0x10) ? r.get_u8() : (opcode & 0x0F)) + 1;
        for (size_t z = 0; z < num_blocks; z++) {
          ret.blit(ret, x, y, 4, 4, prev_x1, prev_y1);
          advance_block();
        }
        break;
      }

      case 0x40: { // repeat previous pair of blocks
        uint16_t num_blocks = (((opcode & 0x10) ? r.get_u8() : (opcode & 0x0F)) + 1) * 2;
        for (size_t z = 0; z < num_blocks; z++) {
          ret.blit(ret, x, y, 4, 4, prev_x2, prev_y2);
          advance_block();
        }
        break;
      }

      case 0x60: { // 1-color encoding
        uint8_t num_blocks = ((opcode & 0x10) ? r.get_u8() : (opcode & 0x0F)) + 1;
        uint8_t color_index = r.get_u8();
        const auto& color = clut.at(color_index);
        uint64_t r = color.r / 0x0101;
        uint64_t g = color.g / 0x0101;
        uint64_t b = color.b / 0x0101;
        for (size_t z = 0; z < num_blocks; z++) {
          ret.fill_rect(x, y, 4, 4, r, g, b, 0xFF);
          advance_block();
        }
        break;
      }

      case 0x80: { // 2-color encoding
        uint8_t num_blocks = (opcode & 0x0F) + 1;
        uint8_t color_indexes[2];
        if ((opcode & 0xF0) == 0x80) {
          color_indexes[0] = r.get_u8();
          color_indexes[1] = r.get_u8();
          memcpy(color_index_cache2[color_index_cache2_pos++], color_indexes, sizeof(color_indexes));
        } else { // 0x90
          uint8_t cache_index = r.get_u8();
          memcpy(color_indexes, color_index_cache2[cache_index], sizeof(color_indexes));
        }
        for (size_t z = 0; z < num_blocks; z++) {
          uint8_t top_colors = r.get_u8();
          uint8_t bottom_colors = r.get_u8();
          for (size_t yy = 0; yy < 2; yy++) {
            for (size_t xx = 0; xx < 4; xx++) {
              write_color(x + xx, y + yy,
                  color_indexes[!!(top_colors & (0x80 >> (yy * 4 + xx)))]);
            }
          }
          for (size_t yy = 0; yy < 2; yy++) {
            for (size_t xx = 0; xx < 4; xx++) {
              write_color(x + xx, y + 2 + yy,
                  color_indexes[!!(bottom_colors & (0x80 >> (yy * 4 + xx)))]);
            }
          }
          advance_block();
        }
        break;
      }

      case 0xA0: { // 4-color encoding
        uint8_t num_blocks = (opcode & 0x0F) + 1;
        uint8_t color_indexes[4];
        if ((opcode & 0xF0) == 0xA0) {
          for (size_t z = 0; z < 4; z++) {
            color_indexes[z] = r.get_u8();
          }
          memcpy(color_index_cache4[color_index_cache4_pos++], color_indexes, sizeof(color_indexes));
        } else { // 0xB0
          uint8_t cache_index = r.get_u8();
          memcpy(color_indexes, color_index_cache4[cache_index], sizeof(color_indexes));
        }
        for (size_t z = 0; z < num_blocks; z++) {
          for (size_t yy = 0; yy < 4; yy++) {
            uint8_t row_colors = r.get_u8();
            for (size_t xx = 0; xx < 4; xx++) {
              write_color(x + xx, y + yy,
                  color_indexes[(row_colors >> (6 - (2 * xx))) & 0x03]);
            }
          }
          advance_block();
        }
        break;
      }

      case 0xC0: { // 8-color encoding
        uint8_t num_blocks = (opcode & 0x0F) + 1;
        uint8_t color_indexes[8];
        if ((opcode & 0xF0) == 0xC0) {
          for (size_t z = 0; z < 8; z++) {
            color_indexes[z] = r.get_u8();
          }
          memcpy(color_index_cache8[color_index_cache8_pos++], color_indexes, sizeof(color_indexes));
        } else { // 0xD0
          uint8_t cache_index = r.get_u8();
          memcpy(color_indexes, color_index_cache8[cache_index], sizeof(color_indexes));
        }

        for (size_t z = 0; z < num_blocks; z++) {
          uint64_t block_colors = r.get_u48r();
          // for some reason we have to shuffle the bits around like so:
          // read: 0000 1111 2222 3333 4444 5555 6666 7777 8888 9999 AAAA BBBB
          // used: 0000 1111 2222 4444 5555 6666 8888 9999 AAAA 3333 7777 BBBB
          // what were you thinking, sean callahan?
          block_colors =
              (block_colors         & 0xFFF00000000F) |
              ((block_colors << 4)  & 0x000FFF000000) |
              ((block_colors << 8)  & 0x000000FFF000) |
              ((block_colors >> 24) & 0x000000000F00) |
              ((block_colors >> 12) & 0x0000000000F0);
          for (size_t yy = 0; yy < 4; yy++) {
            for (size_t xx = 0; xx < 4; xx++) {
              write_color(x + xx, y + yy,
                  color_indexes[(block_colors >> (45 - (yy * 12) - (xx * 3))) & 0x07]);
            }
          }
          advance_block();
        }
        break;
      }

      case 0xE0: { // 16-color encoding
        uint8_t num_blocks = (opcode & 0x0F) + 1;
        for (size_t z = 0; z < num_blocks; z++) {
          for (size_t yy = 0; yy < 4; yy++) {
            for (size_t xx = 0; xx < 4; xx++) {
              write_color(x + xx, y + yy, r.get_u8());
            }
          }
          advance_block();
        }
        break;
      }
    }
  }

  return ret;
}

struct pict_compressed_quicktime_args {
  uint32_t size;
  uint16_t version;
  uint32_t matrix[9];
  uint32_t matte_size;
  rect matte_rect;
  uint16_t mode;
  rect src_rect;
  uint32_t accuracy;
  uint32_t mask_region_size;
  // variable-length fields:
  // - matte_image_description (determined by matte_size)
  // - matte_data (determined by matte_size)
  // - mask_region (determined by mask_region_size)
  // - image_description (always included; size is self-determined)
  // - data (specified in image_description's data_size field)

  void byteswap() {
    this->size = bswap32(this->size);
    this->version = bswap16(this->version);
    for (size_t x = 0; x < 9; x++) {
      this->matrix[x] = bswap32(this->matrix[x]);
    }
    this->matte_size = bswap32(this->matte_size);
    this->matte_rect.byteswap();
    this->mode = bswap16(this->mode);
    this->src_rect.byteswap();
    this->accuracy = bswap32(this->accuracy);
    this->mask_region_size = bswap32(this->mask_region_size);
  }
} __attribute__((packed));

struct pict_uncompressed_quicktime_args {
  uint32_t size;
  uint16_t version;
  uint32_t matrix[9];
  uint32_t matte_size;
  rect matte_rect;
  // variable-length fields:
  // - matte_image_description (determined by matte_size)
  // - matte_data (determined by matte_size)
  // - subopcode describing the image and mask (98, 99, 9A, or 9B)
  // - image data

  void byteswap() {
    this->size = bswap32(this->size);
    this->version = bswap16(this->version);
    for (size_t x = 0; x < 9; x++) {
      this->matrix[x] = bswap32(this->matrix[x]);
    }
    this->matte_size = bswap32(this->matte_size);
    this->matte_rect.byteswap();
  }
} __attribute__((packed));

static const unordered_map<uint32_t, string> codec_to_extension({


  {0x67696620, "gif"},
  {0x6A706567, "jpeg"},
  {0x6B706364, "pcd"}, // photo CD
  {0x706E6720, "png"},
  {0x74676120, "tga"},
  {0x74696666, "tiff"},
});

static void write_quicktime_data(StringReader& r, pict_render_state& st,
    uint16_t opcode) {
  bool is_compressed = !(opcode & 0x01);

  if (st.canvas_modified) {
    throw runtime_error("PICT requires QuickTime data after drawing opcodes");
  }
  if (!is_compressed) {
    throw runtime_error("PICT contains uncompressed QuickTime data");
  }

  // get the compressed data header and check for unsupported fancy stuff
  pict_compressed_quicktime_args args = r.get<pict_compressed_quicktime_args>();
  args.byteswap();
  if (args.matte_size) {
    throw runtime_error("compressed QuickTime data includes a matte image");
  }
  if (args.mask_region_size) {
    throw runtime_error("compressed QuickTime data includes a mask region");
  }

  // TODO: in the future if we ever support matte images, we'll have to read the
  // header data for them here

  // get the image description and check for unsupported fancy stuff
  pict_quicktime_image_description desc = r.get<pict_quicktime_image_description>();
  desc.byteswap();
  if (desc.frame_count != 1) {
    throw runtime_error("compressed QuickTime data includes zero or multiple frames");
  }
  if (desc.clut_id != 0xFFFF && desc.clut_id != 0) {
    throw runtime_error("compressed QuickTime data uses external color table");
  }

  // if clut_id == 0, a struct color_table immediately follows the image description
  vector<color_table_entry> clut;
  if (desc.clut_id == 0) {
    color_table clut_header = r.get<color_table>();
    clut_header.byteswap_header();
    clut.resize(clut_header.get_num_entries());
    r.read_into(clut.data(), sizeof(clut[0]) * clut.size());
    for (auto& entry : clut) {
      entry.byteswap();
    }
  }

  // if the image is decodable, decode it
  if (desc.codec == 0x736D6320) { // 'smc '
    string data = r.read(desc.data_size);
    st.canvas = decode_smc(desc, clut, data);

  // if it's not decodable, try to provide a useful result in the original
  // format (GIF, PNG, etc.)
  } else {
    // read the image data
    try {
      st.embedded_image_format = codec_to_extension.at(desc.codec);
    } catch (const out_of_range&) {
      throw runtime_error(string_printf("compressed QuickTime data uses codec %08" PRIX32, desc.codec));
    }
    st.embedded_image_data = r.read(desc.data_size);
  }
}



// opcode index

vector<void(*)(StringReader&, pict_render_state&, uint16_t)> render_functions({
  skip_0,                         // 0000: no operation (args: 0)
  set_clipping_region,            // 0001: clipping region (args: region)
  set_background_pattern,         // 0002: background pattern (args: ?8)
  set_font_number,                // 0003: text font number (args: u16)
  set_font_style_flags,           // 0004: text font style (args: u8)
  set_text_source_mode,           // 0005: text source mode (args: u16)
  set_text_extra_space,           // 0006: extra space (args: u32)
  set_pen_size,                   // 0007: pen size (args: point)
  set_pen_mode,                   // 0008: pen mode (args: u16)
  set_pen_pattern,                // 0009: pen pattern (args: ?8)
  set_fill_pattern,               // 000A: fill pattern (args: ?8)
  set_oval_size,                  // 000B: oval size (args: point)
  set_origin_dh_dv,               // 000C: set origin dh/dv (args: u16, u16)
  set_text_size,                  // 000D: text size (args: u16)
  set_foreground_color32,         // 000E: foreground color (args: u32)
  set_background_color32,         // 000F: background color (args: u32)
  set_text_ratio,                 // 0010: text ratio? (args: point numerator, point denominator)
  set_version,                    // 0011: version (args: u8)
  set_background_pixel_pattern,   // 0012: background pixel pattern (missing in v1) (args: ?)
  set_pen_pixel_pattern,          // 0013: pen pixel pattern (missing in v1) (args: ?)
  set_fill_pixel_pattern,         // 0014: fill pixel pattern (missing in v1) (args: ?)
  unimplemented_opcode,           // 0015: fractional pen position (missing in v1) (args: u16 low word of fixed)
  set_text_nonspace_extra_width,  // 0016: added width for nonspace characters (missing in v1) (args: u16)
  unimplemented_opcode,           // 0017: reserved (args: indeterminate)
  unimplemented_opcode,           // 0018: reserved (args: indeterminate)
  unimplemented_opcode,           // 0019: reserved (args: indeterminate)
  set_foreground_color,           // 001A: foreground color (missing in v1) (args: rgb48)
  set_background_color,           // 001B: background color (missing in v1) (args: rgb48)
  set_highlight_mode_flag,        // 001C: highlight mode flag (missing in v1) (args: 0)
  set_highlight_color,            // 001D: highlight color (missing in v1) (args: rgb48)
  set_default_highlight_color,    // 001E: use default highlight color (missing in v1) (args: 0)
  set_op_color,                   // 001F: color (missing in v1) (args: rgb48)
  unimplemented_opcode,           // 0020: line (args: point, point)
  unimplemented_opcode,           // 0021: line from (args: point)
  unimplemented_opcode,           // 0022: short line (args: point, s8 dh, s8 dv)
  unimplemented_opcode,           // 0023: short line from (args: s8 dh, s8 dv)
  skip_var16,                     // 0024: reserved (args: u16 data length, u8[] data)
  skip_var16,                     // 0025: reserved (args: u16 data length, u8[] data)
  skip_var16,                     // 0026: reserved (args: u16 data length, u8[] data)
  skip_var16,                     // 0027: reserved (args: u16 data length, u8[] data)
  unimplemented_opcode,           // 0028: long text (args: point, u8 count, char[] text)
  unimplemented_opcode,           // 0029: dh text (args: u8 dh, u8 count, char[] text)
  unimplemented_opcode,           // 002A: dv text (args: u8 dv, u8 count, char[] text)
  unimplemented_opcode,           // 002B: dh/dv text (args: u8 dh, u8 dv, u8 count, char[] text)
  set_font_number_and_name,       // 002C: font name (missing in v1) (args: u16 length, u16 old font id, u8 name length, char[] name)
  unimplemented_opcode,           // 002D: line justify (missing in v1) (args: u16 data length, fixed interchar spacing, fixed total extra space)
  unimplemented_opcode,           // 002E: glyph state (missing in v1) (u16 data length, u8 outline, u8 preserve glyph, u8 fractional widths, u8 scaling disabled)
  unimplemented_opcode,           // 002F: reserved (args: u16 data length, u8[] data)
  unimplemented_opcode,           // 0030: frame rect (args: rect)
  unimplemented_opcode,           // 0031: paint rect (args: rect)
  erase_rect,                     // 0032: erase rect (args: rect)
  unimplemented_opcode,           // 0033: invert rect (args: rect)
  fill_rect,                      // 0034: fill rect (args: rect)
  skip_8,                         // 0035: reserved (args: rect)
  skip_8,                         // 0036: reserved (args: rect)
  skip_8,                         // 0037: reserved (args: rect)
  unimplemented_opcode,           // 0038: frame same rect (args: 0)
  unimplemented_opcode,           // 0039: paint same rect (args: 0)
  erase_last_rect,                // 003A: erase same rect (args: 0)
  unimplemented_opcode,           // 003B: invert same rect (args: 0)
  fill_last_rect,                 // 003C: fill same rect (args: 0)
  skip_0,                         // 003D: reserved (args: 0)
  skip_0,                         // 003E: reserved (args: 0)
  skip_0,                         // 003F: reserved (args: 0)
  unimplemented_opcode,           // 0040: frame rrect (args: rect)
  unimplemented_opcode,           // 0041: paint rrect (args: rect)
  unimplemented_opcode,           // 0042: erase rrect (args: rect)
  unimplemented_opcode,           // 0043: invert rrect (args: rect)
  unimplemented_opcode,           // 0044: fill rrect (args: rect)
  skip_8,                         // 0045: reserved (args: rect)
  skip_8,                         // 0046: reserved (args: rect)
  skip_8,                         // 0047: reserved (args: rect)
  unimplemented_opcode,           // 0048: frame same rrect (args: 0)
  unimplemented_opcode,           // 0049: paint same rrect (args: 0)
  unimplemented_opcode,           // 004A: erase same rrect (args: 0)
  unimplemented_opcode,           // 004B: invert same rrect (args: 0)
  unimplemented_opcode,           // 004C: fill same rrect (args: 0)
  skip_0,                         // 004D: reserved (args: 0)
  skip_0,                         // 004E: reserved (args: 0)
  skip_0,                         // 004F: reserved (args: 0)
  unimplemented_opcode,           // 0050: frame oval (args: rect)
  unimplemented_opcode,           // 0051: paint oval (args: rect)
  unimplemented_opcode,           // 0052: erase oval (args: rect)
  unimplemented_opcode,           // 0053: invert oval (args: rect)
  fill_oval,                      // 0054: fill oval (args: rect)
  skip_8,                         // 0055: reserved (args: rect)
  skip_8,                         // 0056: reserved (args: rect)
  skip_8,                         // 0057: reserved (args: rect)
  unimplemented_opcode,           // 0058: frame same oval (args: 0)
  unimplemented_opcode,           // 0059: paint same oval (args: 0)
  unimplemented_opcode,           // 005A: erase same oval (args: 0)
  unimplemented_opcode,           // 005B: invert same oval (args: 0)
  fill_last_oval,                 // 005C: fill same oval (args: 0)
  skip_0,                         // 005D: reserved (args: 0)
  skip_0,                         // 005E: reserved (args: 0)
  skip_0,                         // 005F: reserved (args: 0)
  unimplemented_opcode,           // 0060: frame arc (args: rect, u16 start angle, u16 arc angle)
  unimplemented_opcode,           // 0061: paint arc (args: rect, u16 start angle, u16 arc angle)
  unimplemented_opcode,           // 0062: erase arc (args: rect, u16 start angle, u16 arc angle)
  unimplemented_opcode,           // 0063: invert arc (args: rect, u16 start angle, u16 arc angle)
  unimplemented_opcode,           // 0064: fill arc (args: rect, u16 start angle, u16 arc angle)
  skip_12,                        // 0065: reserved (args: rect, u16 start angle, u16 arc angle)
  skip_12,                        // 0066: reserved (args: rect, u16 start angle, u16 arc angle)
  skip_12,                        // 0067: reserved (args: rect, u16 start angle, u16 arc angle)
  unimplemented_opcode,           // 0068: frame same arc (args: rect)
  unimplemented_opcode,           // 0069: paint same arc (args: rect)
  unimplemented_opcode,           // 006A: erase same arc (args: rect)
  unimplemented_opcode,           // 006B: invert same arc (args: rect)
  unimplemented_opcode,           // 006C: fill same arc (args: rect)
  skip_8,                         // 006D: reserved (args: rect)
  skip_8,                         // 006E: reserved (args: rect)
  skip_8,                         // 006F: reserved (args: rect)
  unimplemented_opcode,           // 0070: frame poly (args: polygon)
  unimplemented_opcode,           // 0071: paint poly (args: polygon)
  unimplemented_opcode,           // 0072: erase poly (args: polygon)
  unimplemented_opcode,           // 0073: invert poly (args: polygon)
  unimplemented_opcode,           // 0074: fill poly (args: polygon)
  skip_var16,                     // 0075: reserved (args: polygon)
  skip_var16,                     // 0076: reserved (args: polygon)
  skip_var16,                     // 0077: reserved (args: polygon)
  unimplemented_opcode,           // 0078: frame same poly (args: 0)
  unimplemented_opcode,           // 0079: paint same poly (args: 0)
  unimplemented_opcode,           // 007A: erase same poly (args: 0)
  unimplemented_opcode,           // 007B: invert same poly (args: 0)
  unimplemented_opcode,           // 007C: fill same poly (args: 0)
  skip_0,                         // 007D: reserved (args: 0)
  skip_0,                         // 007E: reserved (args: 0)
  skip_0,                         // 007F: reserved (args: 0)
  unimplemented_opcode,           // 0080: frame region (args: region)
  unimplemented_opcode,           // 0081: paint region (args: region)
  unimplemented_opcode,           // 0082: erase region (args: region)
  unimplemented_opcode,           // 0083: invert region (args: region)
  unimplemented_opcode,           // 0084: fill region (args: region)
  skip_var16,                     // 0085: reserved (args: region)
  skip_var16,                     // 0086: reserved (args: region)
  skip_var16,                     // 0087: reserved (args: region)
  unimplemented_opcode,           // 0088: frame same region (args: 0)
  unimplemented_opcode,           // 0089: paint same region (args: 0)
  unimplemented_opcode,           // 008A: erase same region (args: 0)
  unimplemented_opcode,           // 008B: invert same region (args: 0)
  unimplemented_opcode,           // 008C: fill same region (args: 0)
  skip_0,                         // 008D: reserved (args: 0)
  skip_0,                         // 008E: reserved (args: 0)
  skip_0,                         // 008F: reserved (args: 0)
  copy_bits_indexed_color,        // 0090: copybits into rect (args: struct)
  copy_bits_indexed_color,        // 0091: copybits into region (args: struct)
  skip_var16,                     // 0092: reserved (args: u16 data length, u8[] data)
  skip_var16,                     // 0093: reserved (args: u16 data length, u8[] data)
  skip_var16,                     // 0094: reserved (args: u16 data length, u8[] data)
  skip_var16,                     // 0095: reserved (args: u16 data length, u8[] data)
  skip_var16,                     // 0096: reserved (args: u16 data length, u8[] data)
  skip_var16,                     // 0097: reserved (args: u16 data length, u8[] data)
  copy_bits_indexed_color,        // 0098: packed indexed color or monochrome copybits into rect (args: struct)
  copy_bits_indexed_color,        // 0099: packed indexed color or monochrome copybits into region (args: struct)
  packed_copy_bits_direct_color,  // 009A: packed direct color copybits into rect (missing in v1) (args: struct)
  packed_copy_bits_direct_color,  // 009B: packed direct color copybits into region (missing in v1) (args: ?)
  skip_var16,                     // 009C: reserved (args: u16 data length, u8[] data)
  skip_var16,                     // 009D: reserved (args: u16 data length, u8[] data)
  skip_var16,                     // 009E: reserved (args: u16 data length, u8[] data)
  skip_var16,                     // 009F: reserved (args: u16 data length, u8[] data)
  skip_2,                         // 00A0: short comment (args: u16 kind)
  skip_long_comment,              // 00A1: long comment (args: u16 kind, u16 length, char[] data)
});

pict_render_result render_quickdraw_picture(const void* vdata, size_t size) {
  if (size < sizeof(pict_header)) {
    throw runtime_error("pict too small for header");
  }

  StringReader r(vdata, size);
  pict_header header = r.get<pict_header>();
  header.byteswap();

  // if the pict header is all zeroes, assume this is a pict file with a
  // 512-byte header that needs to be skipped
  if (header.size == 0 && header.bounds.x1 == 0 && header.bounds.y1 == 0 &&
      header.bounds.x2 == 0 && header.bounds.y2 == 0 && size > 0x200) {
    r.go(0x200);
    header = r.get<pict_header>();
    header.byteswap();
  }

  pict_render_state st(header);
  while (!r.eof()) {
    // in v2 pictures, opcodes are word-aligned
    if ((st.version == 2) && (r.where() & 1)) {
      r.get_u8();
    }

    uint16_t opcode = (st.version == 1) ? r.get_u8() : r.get_u16r();
    if (opcode < render_functions.size()) {
      render_functions[opcode](r, st, opcode);

    } else if (opcode <= 0xAF) { // args: u16 len, u8[] data
      skip_var16(r, st, opcode);

    } else if (opcode <= 0xCF) { // args: 0
      // nop

    } else if (opcode <= 0xFE) { // args: u32 len, u8[] data
      skip_var32(r, st, opcode);

    } else if (opcode == 0xFF) { // args: 0
      break; // end picture

    } else if (opcode <= 0x01FF) { // args: 2
      skip_2(r, st, opcode);

    } else if (opcode <= 0x02FE) { // args: 4
      r.go(r.where() + 4);

    } else if (opcode == 0x02FF) { // args: 2
      // nop (essentially) because we look ahead in the 0011 implementation

    } else if (opcode <= 0x0BFF) { // args: 22
      r.go(r.where() + 22);

    } else if (opcode == 0x0C00) { // args: header
      // currently we don't do anything with tyhe data in this subheader, so
      // just check that its version make sense and ignore it
      pict_subheader h = r.get<pict_subheader>();
      if ((bswap32(h.v2.version) != 0xFFFFFFFF) && (bswap16(h.v2e.version) != 0xFFFE)) {
        throw runtime_error(string_printf("subheader has incorrect version (%08X or %04hX)",
            bswap32(h.v2.version), bswap16(h.v2e.version)));
      }

    } else if (opcode <= 0x7EFF) { // args: 24
      r.go(r.where() + 24);

    } else if (opcode <= 0x7FFF) { // args: 254
      r.go(r.where() + 254);

    } else if (opcode <= 0x80FF) { // args: 0
      // nop

    } else if (opcode <= 0x81FF) { // args: u32 len, u8[] data
      skip_var32(r, st, opcode);

    } else if ((opcode & 0xFFFE) == 0x8200) { // args: pict_compressed_quicktime_args or pict_uncompressed_quicktime_args
      write_quicktime_data(r, st, opcode);
      // TODO: it appears that these opcodes always just end rendering, since
      // some PICTs taht include them have rendering opcodes afterward that
      // appear to do backup things, like render text saying "You need QuickTime
      // to see this picture". So we just end rendering immediately, which seems
      // correct, but I haven't been able to verify this via documentation.
      break;

    } else { // args: u32 len, u8[] data
      skip_var32(r, st, opcode);
    }
  }

  pict_render_result result;
  result.image = move(st.canvas);
  result.embedded_image_format = move(st.embedded_image_format);
  result.embedded_image_data = move(st.embedded_image_data);
  return result;
}
