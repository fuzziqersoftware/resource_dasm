#include <inttypes.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <exception>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <vector>

#include "IndexFormats/ResourceFork.hh"
#include "ResourceFile.hh"

using namespace std;



void print_extra_data(StringReader& r, size_t end_offset, const char* what) {
  size_t offset = r.where();
  if (offset > end_offset) {
    throw runtime_error(string_printf("%s parsing extended beyond end", what));
  } else if (offset < end_offset) {
    string extra_data = r.read(end_offset - offset);
    if (extra_data.find_first_not_of('\0') != string::npos) {
      fprintf(stderr, "warning: extra data after %s ignored:\n", what);
      print_data(stderr, extra_data, offset);
    }
  }
}

string get_cstr_pad(StringReader& r) {
  bool initial_parity = r.where() & 1;
  string ret = r.get_cstr();
  if (initial_parity != (r.where() & 1)) {
    r.get_u8();
  }
  return ret;
}

string trim_and_decode(const string& src) {
  size_t zero_pos = src.find('\0');
  string ret = (zero_pos != string::npos) ? src.substr(0, zero_pos) : src;
  return decode_mac_roman(ret);
}

bool format_is_v2(uint32_t format) {
  // TODO: When exactly did CARD/BKGD formats change? We assume here that they
  // changed between v1 and v2, which is probably correct, but this is not
  // verified.
  return (format >= 9);
}

string autoformat_hypertalk(const string& src) {
  vector<string> lines = split(src, '\n');

  // First, eliminate all continuation characters by combining lines
  {
    size_t write_index = 0;
    // Note: The seeming mismatch of loop variables here is not a bug. The loop
    // ends when read_index reaches the end of lines, but each iteration of the
    // loop handles a single write_index (and possibly multiple read_indexes).
    for (size_t read_index = 0; read_index < lines.size(); write_index++) {
      string& write_line = lines[write_index];
      if (read_index != write_index) {
        write_line = move(lines[read_index]);
      }
      read_index++;

      // Combine read lines into the write line while the write line still ends
      // with a continuation character. This handles sequences of multiple lines
      // with continuations.
      while ((read_index < lines.size()) &&
             (write_line.size() > 1) &&
             // The return character (C2 in mac roman) decodes to C2 AC, which
             // is the same as (-3E) (-54)
             (write_line[write_line.size() - 2] == -0x3E) &&
             (write_line[write_line.size() - 1] == -0x54)) {
        // Remove the continuation character and preceding whitespace, leaving a
        // single space at the end
        write_line.pop_back();
        write_line.pop_back();
        while (!write_line.empty() && isblank(write_line.back())) {
          write_line.pop_back();
        }
        write_line.push_back(' ');

        // Append the read line, skipping any whitespace
        size_t read_non_whitespace_index = lines[read_index].find_first_not_of(" \t");
        if (read_non_whitespace_index == 0) {
          write_line += lines[read_index];
        } else {
          write_line += lines[read_index].substr(read_non_whitespace_index);
        }

        read_index++;
      }
    }

    lines.resize(write_index);
  }

  // Second, auto-indent lines based on how many blocks they appear in
  {
    size_t indent = 0;
    bool prev_is_if_then = false;
    for (size_t line_num = 0; line_num < lines.size(); line_num++) {
      string& line = lines[line_num];

      // Strip whitespace from the beginning and end; we'll auto-indent later
      size_t line_start_offset = line.find_first_not_of(" \t");
      size_t line_end_offset = line.find_last_not_of(" \t");
      if (line_start_offset == string::npos) {
        line.clear();
      } else {
        line = line.substr(line_start_offset, line_end_offset - line_start_offset + 1);

        // Lowercase the line for pseudo-parsing
        string lowercase_line = line;
        transform(lowercase_line.begin(), lowercase_line.end(), lowercase_line.begin(), ::tolower);
        size_t comment_start = lowercase_line.find("--");
        if (comment_start != string::npos) {
          lowercase_line.resize(comment_start);
          size_t lowercase_line_end_offset = lowercase_line.find_last_not_of(" \t");
          if (lowercase_line_end_offset == string::npos) {
            lowercase_line.clear();
          } else {
            lowercase_line.resize(lowercase_line_end_offset + 1);
          }
        }

        // true if the line is an 'else' or 'else if' statement
        bool is_else = starts_with(lowercase_line, "else");
        // true if the line is an 'if' or 'else if' statement
        bool is_if = is_else ? starts_with(lowercase_line, "else if ") : starts_with(lowercase_line, "if ");
        // true if the line is an 'else' statement with an inline body
        bool is_else_then = is_else && !is_if && !ends_with(lowercase_line, "else");
        // true if the line is an 'if' or 'else if' statement with an inline body
        bool is_if_then = is_if && !ends_with(lowercase_line, " then");
        // true if the line is an 'end' statement
        bool is_end = starts_with(lowercase_line, "end ");
        // true if the line is a 'repeat' statement
        bool is_repeat = starts_with(lowercase_line, "repeat");
        // true if the line is an 'on' statement
        bool is_on = starts_with(lowercase_line, "on ");

        bool should_unindent_here = is_end || (is_else && !prev_is_if_then);
        bool should_indent_after = (is_if && !is_if_then) || (is_else && !is_else_then && !is_if_then) || is_repeat || is_on;

        if (should_unindent_here) {
          if (indent >= 2) {
            indent -= 2;
          } else {
            fprintf(stderr, "warning: autoformatting attempted to unindent past zero on line %zu\n",
                line_num + 1);
          }
        }
        line.insert(0, indent, ' ');
        if (should_indent_after) {
          indent += 2;
        }

        prev_is_if_then = is_if_then;
      }
    }
  }

  size_t script_bytes = lines.size();
  for (const auto& line : lines) {
    script_bytes += line.size();
  }
  string ret;
  ret.reserve(script_bytes);
  for (const auto& line : lines) {
    ret += line;
    ret += '\n';
  }
  return ret;
}

struct OSAScriptData {
  // Format:
  //   uint16_t script_offset; // relative to location of script_size
  //   uint16_t script_size;
  //   uint8_t extra_header_data[...]; // if script_offset != 2 presumably
  //   char script[script_size];
  string extra_header_data;
  string script;

  OSAScriptData() = default;
  OSAScriptData(StringReader& r) {
    if (r.get_u16b(false) == 0) {
      return;
    }
    uint16_t script_offset = r.get_u16b();
    uint16_t script_size = r.get_u16b();
    if (script_offset < 2) {
      throw runtime_error("OSA script overlaps size field");
    }
    if (script_offset > 2) {
      this->extra_header_data = r.read(script_offset - 2);
    }
    this->script = r.read(script_size);
  }
};

void print_formatted_script(FILE* f, const string& script, const OSAScriptData& osa_script_data) {
    string extra_header_data;
  if (script.empty()) {
    if (!osa_script_data.extra_header_data.empty()) {
      fprintf(f, "----- OSA script extra header data -----\n");
      print_data(f, osa_script_data.extra_header_data);
    }
    if (!osa_script_data.script.empty()) {
      fprintf(f, "----- OSA script -----\n");
      string decoded_script = decode_mac_roman(osa_script_data.script);
      bool all_chars_printable = true;
      for (char ch : decoded_script) {
        if (!isprint(ch) && (ch != '\n') && (ch != '\t')) {
          all_chars_printable = false;
          break;
        }
      }
      if (all_chars_printable) {
        fwritex(f, decoded_script);
      } else {
        print_data(f, osa_script_data.script);
      }
    }

  } else {
    fprintf(f, "----- HyperTalk script -----\n");
    string formatted_script = autoformat_hypertalk(script);
    fwritex(f, formatted_script);
  }
}



struct BlockHeader {
  uint32_t size;
  uint32_t type;
  int32_t id;

  void byteswap() {
    this->size = bswap32(this->size);
    this->type = bswap32(this->type);
    this->id = bswap32(this->id);
  }
} __attribute__((packed));

struct StackBlock {
  BlockHeader header; // type 'STAK'
  uint32_t format; // 1-7: pre-release HC 1, 8: HC 1, 9: pre-release HC 2, 10: HC 2
  uint32_t total_size;
  uint32_t stack_block_size;
  uint32_t background_count;
  int32_t first_background_id;
  uint32_t card_count;
  int32_t first_card_id;
  int32_t list_block_id;
  uint32_t free_block_count;
  uint32_t free_size;
  int32_t print_block_id;
  uint32_t protect_password_hash;
  uint16_t max_user_level; // value is 1-5
  uint16_t flags; // 8000 can't modify, 4000 can't delete, 2000 private access, 1000 always set (?), 0800 can't abort, 0400 can't peek
  uint32_t hypercard_create_version;
  uint32_t hypercard_compact_version;
  uint32_t hypercard_modify_version;
  uint32_t hypercard_open_version;
  uint32_t checksum;
  Rect window_rect;
  Rect screen_rect;
  int16_t scroll_y;
  int16_t scroll_x;
  int32_t font_table_block_id;
  int32_t style_table_block_id;
  uint16_t card_height;
  uint16_t card_width;
  uint64_t patterns[0x28];
  string script;
  OSAScriptData osa_script_data;

  StackBlock(StringReader& r) {
    // Format (v2, at least):
    //   BlockHeader header; // type 'STAK'
    //   uint32_t unknown;
    //   uint32_t format; // 0x10; 1-7: pre-release HC 1, 8: HC 1, 9: pre-release HC 2, 10: HC 2
    //   uint32_t total_size;
    //   uint32_t stack_block_size;
    //   uint32_t unknown[2]; // 0x1C
    //   uint32_t background_count;
    //   int32_t first_background_id;
    //   uint32_t card_count;
    //   int32_t first_card_id; // 0x30
    //   int32_t list_block_id;
    //   uint32_t free_block_count;
    //   uint32_t free_size;
    //   int32_t print_block_id; // 0x40
    //   uint32_t protect_password_hash;
    //   uint16_t max_user_level; // value is 1-5
    //   uint16_t unknown;
    //   uint16_t flags; // 8000 can't modify, 4000 can't delete, 2000 private access, 1000 always set (?), 0800 can't abort, 0400 can't peek
    //   uint8_t unknown4[0x12]; // 0x4E
    //   uint32_t hypercard_create_version; // 0x60
    //   uint32_t hypercard_compact_version;
    //   uint32_t hypercard_modify_version;
    //   uint32_t hypercard_open_version;
    //   uint32_t checksum; // 0x70
    //   uint32_t unknown;
    //   Rect window_rect;
    //   Rect screen_rect; // 0x80
    //   int16_t scroll_y;
    //   int16_t scroll_x;
    //   int16_t unknown[2];
    //   uint8_t unknown[0x120]; // 0x90
    //   int32_t font_table_block_id; // 0x1B0
    //   int32_t style_table_block_id;
    //   uint16_t card_height;
    //   uint16_t card_width;
    //   uint16_t unknown[2];
    //   uint8_t unknown[0x100]; // 0x1C0
    //   uint64_t patterns[0x28]; // 0x2C0
    //   uint8_t unknown[0x200]; // 0x400
    //   char script[0]; // 0x600
    this->header = r.get<BlockHeader>();
    this->header.byteswap();
    r.skip(4);
    // 0x10
    this->format = r.get_u32b();
    this->total_size = r.get_u32b();
    this->stack_block_size = r.get_u32b();
    r.skip(8);
    // 0x24
    this->background_count = r.get_u32b();
    this->first_background_id = r.get_s32b();
    this->card_count = r.get_u32b();
    // 0x30
    this->first_card_id = r.get_s32b();
    this->list_block_id = r.get_s32b();
    this->free_block_count = r.get_u32b();
    this->free_size = r.get_u32b();
    // 0x40
    this->print_block_id = r.get_s32b();
    this->protect_password_hash = r.get_u32b();
    this->max_user_level = r.get_u16b();
    r.skip(2);
    this->flags = r.get_u16b();
    r.skip(0x12);
    // 0x60
    this->hypercard_create_version = r.get_u32b();
    this->hypercard_compact_version = r.get_u32b();
    this->hypercard_modify_version = r.get_u32b();
    this->hypercard_open_version = r.get_u32b();
    // 0x70
    this->checksum = r.get_u32b();
    r.skip(4);
    this->window_rect = r.get<Rect>();
    // 0x80
    this->screen_rect = r.get<Rect>();
    this->scroll_y = r.get_s16b();
    this->scroll_x = r.get_s16b();
    r.skip(4);
    // 0x90
    r.skip(0x120);
    // 0x1B0
    this->font_table_block_id = r.get_s32b();
    this->style_table_block_id = r.get_s32b();
    this->card_height = r.get_u16b();
    this->card_width = r.get_u16b();
    r.skip(4);
    // 0x1C0
    r.skip(0x100);
    // 0x2C0
    for (size_t x = 0; x < 0x28; x++) {
      this->patterns[x] = r.get_u64b();
    }
    // 0x400
    r.skip(0x200);
    // 0x600
    this->script = trim_and_decode(r.get_cstr());
    // TODO: parse OSA script if present
  }

  const char* name_for_format(uint32_t format) {
    if (format > 0 && format < 8) {
      return "pre-release HyperCard 1";
    } else if (format == 8) {
      return "HyperCard 1";
    } else if (format == 9) {
      return "pre-release HyperCard 2";
    } else if (format == 10) {
      return "HyperCard 2";
    } else {
      return "unknown";
    }
  }

  const char* name_for_user_level(uint16_t level) {
    if (level == 1) {
      return "browsing";
    } else if (level == 2) {
      return "typing";
    } else if (level == 3) {
      return "painting";
    } else if (level == 4) {
      return "authoring";
    } else if (level == 5) {
      return "scripting";
    } else {
      return "unknown";
    }
  }

  string str_for_flags(uint16_t flags) {
    // 8000 can't modify, 4000 can't delete, 2000 private access, 1000 always set (?), 0800 can't abort, 0400 can't peek
    vector<const char*> tokens;
    if (flags & 0x8000) {
      tokens.emplace_back("can\'t modify");
    }
    if (flags & 0x4000) {
      tokens.emplace_back("can\'t delete");
    }
    if (flags & 0x2000) {
      tokens.emplace_back("private access");
    }
    if (flags & 0x0800) {
      tokens.emplace_back("can\'t abort");
    }
    if (flags & 0x0400) {
      tokens.emplace_back("can\'t peek");
    }
    if (tokens.empty()) {
      return "none";
    }
    return join(tokens, ", ");
  }

};

struct StyleTableBlock {
  BlockHeader header; // type 'STBL'
  uint32_t style_count;

  struct Entry {
    int16_t font_id; // -1 = inherited from field styles
    uint16_t style_flags; // bold, italic, underline, etc. may be 0xFFFF for inherit
    int16_t font_size; // -1 = inherit

    Entry(StringReader& r) {
      // Format:
      //   uint8_t unknown1[0x10];
      //   int16_t font_id;
      //   uint16_t style_flags;
      //   int16_t font_size;
      //   uint16_t unknown2;
      r.skip(0x10);
      this->font_id = r.get_s16b();
      this->style_flags = r.get_u16b();
      this->font_size = r.get_s16b();
      r.skip(2);
    }
  };

  vector<Entry> entries;

  StyleTableBlock(StringReader& r) {
    // Format:
    //   BlockHeader header; // type 'STBL'
    //   uint32_t unknown1;
    //   uint32_t style_count;
    this->header = r.get<BlockHeader>();
    this->header.byteswap();
    r.skip(4);
    this->style_count = r.get_u32b();

    while (this->entries.size() < this->style_count) {
      this->entries.emplace_back(r);
    }
  }
};

struct FontTableBlock {
  BlockHeader header; // type 'FTBL'
  unordered_map<int16_t, string> font_id_to_name;

  FontTableBlock(StringReader& r) {
    // Format:
    //   BlockHeader header; // type 'FTBL'
    //   uint8_t unknown1[6];
    //   uint16_t font_count;
    //   uint32_t unknown2;
    //   For each entry:
    //     int16_t font_id;
    //     uint8_t name_length;
    //     char name[name_length];
    //     char pad; // only if name_length is even
    this->header = r.get<BlockHeader>();
    this->header.byteswap();
    r.skip(6);
    uint16_t font_count = r.get_u16b();
    r.skip(4);
    for (size_t x = 0; x < font_count; x++) {
      int16_t font_id = r.get_s16b();
      uint8_t name_length = r.get_u8();
      string name = r.read(name_length);
      if (!(name_length & 1)) {
        r.get_u8(); // end of entry is always word-aligned
      }
      this->font_id_to_name.emplace(font_id, name);
    }
  }
};

struct PageTableListBlock {
  BlockHeader header; // type 'LIST'
  uint16_t card_blocks_size;
  vector<int32_t> page_block_ids;

  PageTableListBlock(StringReader& r) {
    // Format:
    //   BlockHeader header; // type 'LIST'
    //   uint32_t page_table_count;
    //   uint8_t unknown1[8];
    //   uint16_t card_blocks_size;
    //   uint8_t unknown2[0x10];
    //   For each entry:
    //     uint16_t unknown1;
    //     int32_t page_block_id;
    this->header = r.get<BlockHeader>();
    this->header.byteswap();
    uint32_t page_table_count = r.get_u32b();
    r.skip(8);
    this->card_blocks_size = r.get_u16b();
    r.skip(0x20);
    for (size_t x = 0; x < page_table_count; x++) {
      r.skip(2);
      this->page_block_ids.emplace_back(r.get_s32b());
    }
  }
};

struct PageTableBlock {
  BlockHeader header; // type 'PAGE'
  uint8_t unknown1[0x0C];

  struct Entry {
    int32_t card_id;
    uint8_t card_flags; // 0x20 = marked
    uint8_t extra[0]; // size determined by PageTableListBlock::card_blocks_size
  } __attribute__((packed));
} __attribute__((packed));

struct CardOrBackgroundBlock {
  struct PartEntry {
    uint16_t entry_size;
    int16_t part_id;
    uint8_t type; // 1 = button, 2 = field
    // 0x80 = hidden
    // 0x20 = don't wrap
    // 0x10 = don't search
    // 0x08 = shared text
    // 0x04 = fixed line height
    // 0x02 = auto tab
    // 0x01 = disable / lock text
    uint8_t low_flags;
    int16_t rect_top;
    int16_t rect_left;
    int16_t rect_bottom;
    int16_t rect_right;
    // 0x8000 = show name / auto select
    // 0x4000 = highlight / show lines
    // 0x2000 = wide margins / auto highlight
    // 0x1000 = shared highlight / multiple lines
    // 0x0F00 masks the button family number
    // 0x000F sets style
    //   buttons: 0 = transparent, 1 = opaque, 2 = rectangle, 3 = roundrect, 4 = shadow, 5 = checkbox, 6 = radio, 8 = standard, 9 = default, 10 = oval, 11 = popup
    //   fields: 0 = transparent, 1 = opaque, 2 = rectangle, 4 = shadow, 7 = scrolling
    uint16_t high_flags;
    union {
      uint16_t title_width;
      uint16_t last_selected_line;
    };
    union {
      int16_t icon_id;
      uint16_t first_selected_line;
    };
    uint16_t text_alignment; // 0 = left/default, 1 = center, -1 = right, -2 = force left align?
    int16_t font_id;
    uint16_t font_size;
    // 0x8000 = group
    // 0x4000 = extend
    // 0x2000 = condense
    // 0x1000 = shadow
    // 0x0800 = outline
    // 0x0400 = underline
    // 0x0200 = italic
    // 0x0100 = bold
    uint16_t style_flags;
    uint16_t line_height;
    string name; // c-string
    string script; // c-string
    OSAScriptData osa_script_data;
    // Format ends with a padding byte if needed to make the size even

    PartEntry(StringReader& r) {
      // This format appears to be the same in v1 and v2
      size_t start_offset = r.where();
      // Format exactly matches the struct above
      this->entry_size = r.get_u16b();
      this->part_id = r.get_s16b();
      this->type = r.get_u8();
      this->low_flags = r.get_u8();
      this->rect_top = r.get_s16b();
      this->rect_left = r.get_s16b();
      this->rect_bottom = r.get_s16b();
      this->rect_right = r.get_s16b();
      this->high_flags = r.get_u16b();
      this->title_width = r.get_u16b(); // also sets last_selected_line
      this->icon_id = r.get_s16b(); // also sets first_selected_line
      this->text_alignment = r.get_u16b();
      this->font_id = r.get_s16b();
      this->font_size = r.get_u16b();
      this->style_flags = r.get_u16b();
      this->line_height = r.get_u16b();
      this->name = r.get_cstr();
      // It seems there's always a double zero after the name
      if (r.get_u8() != 0) {
        throw runtime_error("space byte after part name is not zero");
      }
      this->script = trim_and_decode(r.get_cstr());
      if ((r.where() & 1) && (r.get_u8() != 0)) {
        throw runtime_error("alignment byte after part script is not zero");
      }
      // TODO: parse OSA script if present
      print_extra_data(r, start_offset + this->entry_size, "part entry");
    }
  };

  struct PartContentEntry {
    int16_t part_id; // if negative, card part; if positive, background part
    map<uint16_t, uint16_t> offset_to_style_entry_index;
    string text;

    PartContentEntry(StringReader& r, uint32_t stack_format) {
      bool is_v2 = format_is_v2(stack_format);

      // In v1:
      //   int16_t part_id;
      //   char text[...]
      // In v2:
      //   Format if styles_size & 0x8000:
      //     int16_t part_id;
      //     uint16_t entry_size;
      //     uint16_t styles_size;
      //     For each style (styles_length / 4 of them):
      //       uint16_t start_offset;
      //       uint16_t style_entry_index;
      //     char text[...];
      //   Format if !(styles_size & 0x8000):
      //     int16_t part_id;
      //     uint16_t entry_size;
      //     uint8_t zero;
      //     char text[...];

      // size_t start_offset = r.where();
      this->part_id = r.get_s16b();
      if (!is_v2) {
        this->text = decode_mac_roman(r.get_cstr());
      } else { // v2
        uint16_t text_size = r.get_u16b();

        uint8_t has_styles = r.get_u8();
        if (has_styles) {
          if (!(has_styles & 0x80)) {
            throw runtime_error("part content entry style presence flag not set, but marker byte is not zero");
          }
          uint16_t styles_size = ((has_styles << 8) & 0x7F) | r.get_u8();
          if ((styles_size - 2) & 3) {
            throw runtime_error("part content styles length splits style entry");
          }
          uint16_t num_entries = (styles_size - 2) / 4;
          while (this->offset_to_style_entry_index.size() < num_entries) {
            uint16_t start_offset = r.get_u16b();
            uint16_t style_entry_index = r.get_u16b();
            if (!this->offset_to_style_entry_index.emplace(start_offset, style_entry_index).second) {
              throw runtime_error("part content styles entries contain duplicate offset");
            }
          }
        }

        this->text = trim_and_decode(r.read(text_size));
      }
    }
  };

  BlockHeader header; // type 'CARD' or 'BKGD'
  int32_t bmap_block_id; // 0 = transparent
  // 0x4000 = can't delete
  // 0x2000 = hide card picture
  // 0x0800 = don't search
  uint16_t flags;
  int32_t prev_background_id;
  int32_t next_background_id;
  int32_t background_id;
  vector<PartEntry> parts;
  vector<PartContentEntry> part_contents;
  string name;
  string script;
  OSAScriptData osa_script_data;

  CardOrBackgroundBlock(StringReader& r, uint32_t stack_format) {
    bool is_v2 = format_is_v2(stack_format);

    size_t start_offset = r.where();
    this->header = r.get<BlockHeader>();
    this->header.byteswap();

    // Format:
    //   BlockHeader header; // type 'CARD' or 'BKGD' (already read above)
    //   uint32_t unknown; // Not present in v1
    //   int32_t bmap_block_id; // 0 = transparent
    //   uint16_t flags;
    //   uint16_t unknown[3];
    //   int32_t prev_background_id; // Present but ignored in CARD block
    //   int32_t next_background_id; // Present but ignored in CARD block
    //   int32_t background_id; // Not present in BKGD block
    //   uint16_t parts_count;
    //   uint16_t unknown3[3];
    //   uint16_t parts_contents_count;
    //   uint32_t unknown;
    //   PartEntry parts[parts_count];
    //   PartContentEntry part_contents[part_contents_count];
    //   char name[...]; (c-string)
    //   char script[...]; (c-string)
    //   OSAScriptData osa_script_data; (maybe)

    if (is_v2) {
      r.skip(4); // unknown1
    }
    this->bmap_block_id = r.get_s32b();
    this->flags = r.get_u16b();
    r.skip(6);
    if (this->header.type == 0x43415244) { // CARD
      r.skip(0x08);
      this->prev_background_id = 0;
      this->next_background_id = 0;
      this->background_id = r.get_s32b();
    } else { // BKGD
      this->prev_background_id = r.get_s32b();
      this->next_background_id = r.get_s32b();
      this->background_id = 0;
    }

    uint16_t parts_count = r.get_u16b();
    r.skip(6);
    uint16_t parts_contents_count = r.get_u16b();
    r.skip(4);
    for (size_t x = 0; x < parts_count; x++) {
      this->parts.emplace_back(r);
    }
    for (size_t x = 0; x < parts_contents_count; x++) {
      if (is_v2) {
        // Note: it looks like these must always start on aligned boundaries, but
        // they don't necessarily end on aligned boundaries!
        if ((r.where() & 1) && (r.get_u8() != 0)) {
          throw runtime_error(string_printf("part content entry alignment byte at %zX is not zero", r.where() - 1));
        }
      }
      this->part_contents.emplace_back(r, stack_format);
    }
    if (is_v2) {
      if ((r.where() & 1) && (r.get_u8() != 0)) {
        throw runtime_error(string_printf("alignment byte at %zX after part content entries is not zero", r.where()));
      }
    }
    this->name = r.get_cstr();
    // If the script is blank, it looks like the CARD block sometimes just ends
    // early, so we have to check the offset here.
    if (r.where() < start_offset + this->header.size - 1) {
      this->script = trim_and_decode(r.get_cstr());
    }
    // TODO: parse OSA script if present
  }
};

static void operator^=(string& a, const string& b) {
  if (a.size() != b.size()) {
    throw invalid_argument("strings must be the same length");
  }
  for (size_t x = 0; x < b.size(); x++) {
    a[x] ^= b[x];
  }
}

static void operator>>=(string& s, size_t sh) {
  size_t size = s.size();
  if (sh >= size * 8) {
    s.clear();
    s.resize(size, '\0');
    return;
  }

  // TODO: This can probably be done in a faster way than shifting first by
  // bytes, then by bits. In practice, only one of these cases will ever do any
  // real work, since dh can only be 1, 2, 8, or 16.

  // First, shift entire bytes over.
  if (sh >= 8) {
    size_t sh_bytes = sh >> 3;
    for (size_t x = s.size() - 1; x >= sh_bytes; x--) {
      s[x] = s[x - sh_bytes];
    }
    for (size_t x = 0; x < sh_bytes; x++) {
      s[x] = 0;
    }
  }

  // Second, shift by a sub-byte amount.
  if (sh & 7) {
    size_t sh_bits = sh & 7;
    uint8_t upper_mask = 0xFF << (8 - sh_bits);
    uint8_t lower_mask = 0xFF >> sh_bits;
    for (size_t x = s.size() - 1; x >= 1; x--) {
      s[x] = ((s[x] >> sh_bits) & lower_mask) | ((s[x - 1] << (8 - sh_bits)) & upper_mask);
    }
    s[0] = (s[0] >> sh_bits) & lower_mask;
  }
}

struct BitmapBlock {
  BlockHeader header; // type 'BMAP'
  Rect card_rect;
  Rect mask_rect;
  Rect image_rect;
  Image mask;
  Image image;

  enum class MaskMode {
    PRESENT,
    RECT,
    NONE,
  };
  MaskMode mask_mode;

  BitmapBlock(StringReader& r, uint32_t stack_format) {
    bool is_v2 = format_is_v2(stack_format);

    // Format:
    //   BlockHeader header; // type 'BMAP'
    //   uint32_t unknown;
    //   If v2:
    //     uint16_t unknown[2];
    //   uint16_t unknown[2]; // these seem to usually be {1, 0}
    //   Rect card_rect; // {top, left, bottom, right} just like in QuickDraw
    //   Rect mask_rect;
    //   Rect image_rect;
    //   uint32_t unknown[2];
    //   uint32_t mask_size; // compressed data size
    //   uint32_t image_size; // compressed data size
    this->header = r.get<BlockHeader>();
    this->header.byteswap();
    if (is_v2) {
      r.skip(12);
    } else {
      r.skip(8);
    }
    this->card_rect = r.get<Rect>();
    this->mask_rect = r.get<Rect>();
    this->image_rect = r.get<Rect>();
    r.skip(8);
    uint32_t mask_data_size = r.get_u32b();
    uint32_t image_data_size = r.get_u32b();
    string mask_data = r.read(mask_data_size);
    string image_data = r.read(image_data_size);
    if (!mask_data.empty()) {
      this->mask_mode = MaskMode::PRESENT;
      this->mask = this->decode_bitmap(mask_data, this->mask_rect);
    } else if (!this->mask_rect.is_empty()) {
      this->mask_mode = MaskMode::RECT;
    } else {
      this->mask_mode = MaskMode::NONE;
    }
    this->image = this->decode_bitmap(image_data, this->image_rect);
  }

  static Image decode_bitmap(const string& compressed_data, const Rect& bounds) {
    size_t expanded_bounds_left = bounds.x1 & (~31);
    size_t expanded_bounds_right = ((bounds.x2 + 31) & (~31));
    size_t row_length_bits = expanded_bounds_right - expanded_bounds_left;
    size_t row_length_bytes = row_length_bits >> 3;
    string data;


    uint8_t dh = 0, dv = 0;
    auto apply_dh_dv_transform_if_row_end = [&]() {
      // If we aren't at the end of a row or the dh/dv transform would do
      // nothing, then do nothing
      if ((data.size() % row_length_bytes) || ((dh == 0) && (dv == 0))) {
        return;
      }

      string row = data.substr(data.size() - row_length_bytes);
      string xor_row(row_length_bytes, '\0');

      if (dh) {
        string xor_row = data.substr(data.size() - row_length_bytes);
        for (size_t z = row_length_bits / dh; z > 0; z--) {
          xor_row >>= dh;
          row ^= xor_row;
        }
      }
      if (dv) {
        // Some BMAPs set dv to a nonzero value on the very first row. I assume
        // this just means to not do the dv transform for the first row(s)
        if (data.size() >= (1 + dv) * row_length_bytes) {
          row ^= data.substr(data.size() - (1 + dv) * row_length_bytes, row_length_bytes);
        }
      }

      memcpy(data.data() + data.size() - row_length_bytes,
          row.data(),
          row_length_bytes);
    };

    uint8_t row_memo_bytes[8] = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};

    size_t image_w = expanded_bounds_right - expanded_bounds_left;
    size_t image_h = bounds.y2 - bounds.y1;
    size_t image_bits = image_w * image_h;
    if (image_bits & 3) {
      throw logic_error("image bits is not divisible by 8");
    }
    size_t image_bytes = image_bits >> 3;

    StringReader r(compressed_data.data(), compressed_data.size());
    size_t repeat_count = 1;
    size_t next_repeat_count = 1;
    // Note: It looks like sometimes there are extra bytes at the end of a BMAP
    // stream. The actual image should always end on an opcode boundary, so we
    // just stop early if we've produced enough bytes.
    while (!r.eof() && data.size() < image_bytes) {
      uint8_t opcode = r.get_u8();
      for (; repeat_count > 0; repeat_count--) {
        if (opcode < 0x80) { // 00-7F: zero bytes followed by data bytes
          for (size_t z = 0; z < (opcode & 0x0F); z++) {
            data += '\0';
            apply_dh_dv_transform_if_row_end();
          }
          for (size_t z = 0; z < ((opcode >> 4) & 0x07); z++) {
            data += r.get_u8();
            apply_dh_dv_transform_if_row_end();
          }

        } else if (opcode < 0x90) {
          // These opcodes end the row even if the current position isn't at the end
          if (data.size() % row_length_bytes) {
            data.resize(data.size() + (row_length_bytes - (data.size() % row_length_bytes)), '\0');
            apply_dh_dv_transform_if_row_end();
          }
          // Note: The 80-family intentionally do not trigger the dh/dv transform
          switch (opcode) {
            case 0x80: // one uncompressed row
              data += r.read(row_length_bytes);
              break;
            case 0x81: // one white row
              data.resize(data.size() + row_length_bytes, 0x00);
              break;
            case 0x82: // one black row
              data.resize(data.size() + row_length_bytes, 0xFF);
              break;
            case 0x83: { // one row filled with a specific byte
              uint8_t value = r.get_u8();
              row_memo_bytes[(data.size() / row_length_bytes) % 8] = value;
              data.resize(data.size() + row_length_bytes, value);
              break;
            }
            case 0x84: { // like 83, but use a previous value
              uint8_t value = row_memo_bytes[(data.size() / row_length_bytes) % 8];
              data.resize(data.size() + row_length_bytes, value);
              break;
            }
            case 0x85: // copy the row above
            case 0x86: // copy the second row above
            case 0x87: { // copy the third row above
              uint8_t dy = opcode - 0x84;
              if (data.size() < dy * row_length_bytes) {
                throw runtime_error("backreference beyond beginning of output");
              }
              data.append(data.data() + data.size() - dy * row_length_bytes, row_length_bytes);
              break;
            }

            // 88-8F all set dh/dv and don't write any output
            case 0x88:
              dh = 16;
              dv = 0;
              break;
            case 0x89:
              dh = 0;
              dv = 0;
              break;
            case 0x8A:
              dh = 0;
              dv = 1;
              break;
            case 0x8B:
              dh = 0;
              dv = 2;
              break;
            case 0x8C:
              dh = 1;
              dv = 0;
              break;
            case 0x8D:
              dh = 1;
              dv = 1;
              break;
            case 0x8E:
              dh = 2;
              dv = 2;
              break;
            case 0x8F:
              dh = 8;
              dv = 0;
              break;
          }

        } else if (opcode < 0xA0) { // invalid
          throw runtime_error("invalid opcode in compressed bitmap");

        } else if (opcode < 0xC0) { // repeat the next instruction (opcode & 0x1F) times
          next_repeat_count = opcode & 0x1F;
          if (next_repeat_count == 0) {
            throw runtime_error("C-class opcode specified a repeat count of zero");
          } else if (next_repeat_count == 1) {
            throw runtime_error("C-class opcode specified a repeat count of one");
          }

        } else if (opcode < 0xE0) { // (opcode & 0x1F) << 3 data bytes
          size_t count = (opcode & 0x1F) << 3;
          for (size_t z = 0; z < count; z++) {
            data += r.get_u8();
            apply_dh_dv_transform_if_row_end();
          }

        } else { // (opcode & 0x1F) << 4 zero bytes
          size_t count = (opcode & 0x1F) << 4;
          for (size_t z = 0; z < count; z++) {
            data += '\0';
            apply_dh_dv_transform_if_row_end();
          }
        }
      }
      repeat_count = next_repeat_count;
      next_repeat_count = 1;
    }

    if (data.size() != image_bytes) {
      throw runtime_error(string_printf(
          "decompression produced an incorrect amount of data (%zu bytes produced, (%zu * %zu >> 3) = %zu bytes expected)",
          data.size(), image_w, image_h, image_bytes));
    }

    // TODO: We should trim the left/right edges of the image here
    size_t left_pixels_to_skip = bounds.x1 - expanded_bounds_left;
    size_t right_pixels_to_skip = expanded_bounds_right - bounds.x2;
    Image ret(image_w - left_pixels_to_skip - right_pixels_to_skip, image_h);
    for (size_t z = 0; z < data.size(); z++) {
      size_t x = (z % row_length_bytes) << 3;
      size_t y = z / row_length_bytes;
      uint8_t byte = data[z];
      for (size_t bit_x = 0; bit_x < 8; bit_x++) {
        ssize_t pixel_x = x + bit_x - left_pixels_to_skip;
        if (pixel_x >= 0 && static_cast<size_t>(pixel_x) < ret.get_width()) {
          ret.write_pixel(pixel_x, y, (byte & 0x80) ? 0x000000FF : 0xFFFFFFFF);
        }
        byte <<= 1;
      }
    }
    return ret;
  }

  void render_into_card(Image& dest) const {
    Rect effective_mask_rect = this->mask_mode == MaskMode::NONE
        ? this->image_rect : this->mask_rect;
    for (ssize_t y = 0; y < effective_mask_rect.height(); y++) {
      for (ssize_t x = 0; x < effective_mask_rect.width(); x++) {
        ssize_t card_x = effective_mask_rect.x1 + x;
        ssize_t card_y = effective_mask_rect.y1 + y;
        if (!this->image_rect.contains(card_x, card_y)) {
          continue;
        }
        if ((this->mask_mode == MaskMode::PRESENT &&
             this->mask.read_pixel(x, y) == 0xFFFFFFFF) ||
            (this->mask_mode == MaskMode::NONE &&
             this->image.read_pixel(x, y) == 0xFFFFFFFF)) {
          continue;
        }

        dest.write_pixel(card_x, card_y, this->image.read_pixel(
            card_x - this->image_rect.x1, card_y - this->image_rect.y1));
      }
    }
  }
};



void print_usage() {
  fprintf(stderr, "\
Usage: hypercard_dasm [options] <input-filename> [output-dir]\n\
\n\
If output-dir is not given, the directory <input-filename>.out is created and\n\
the output is written there.\n\
\n\
Options:\n\
  --dump-raw-blocks\n\
      Save the raw contents of each block in addition to the disassembly.\n\
  --skip-render-background-parts\n\
      Don\'t draw boxes for background parts in render images.\n\
  --skip-render-card-parts\n\
      Don\'t draw boxes for card parts in render images.\n\
  --skip-bitmap\n\
      Don\'t render the bitmaps behind the parts boxes in render images.\n\
  --manhole-res-directory=DIR\n\
      Enable Manhole mode, using resources from files in the given directory.\n\
      In this mode, bitmaps are skipped, and instead a PICT (from one of the\n\
      resource files) is rendered in each card image. The PICT ID is given by\n\
      a part contents entry in the card.\n\
\n");
}

int main(int argc, char** argv) {
  string filename;
  string out_dir;
  bool dump_raw_blocks = false;
  bool render_background_parts = true;
  bool render_card_parts = true;
  bool render_bitmap = true;
  const char* manhole_res_directory = nullptr;
  for (int x = 1; x < argc; x++) {
    if (!strcmp(argv[x], "--dump-raw-blocks")) {
      dump_raw_blocks = true;
    } else if (!strcmp(argv[x], "--skip-render-background-parts")) {
      render_background_parts = false;
    } else if (!strcmp(argv[x], "--skip-render-card-parts")) {
      render_card_parts = false;
    } else if (!strcmp(argv[x], "--skip-bitmap")) {
      render_bitmap = false;
    } else if (!strncmp(argv[x], "--manhole-res-directory=", 24)) {
      manhole_res_directory = &argv[x][24];
    } else if (filename.empty()) {
      filename = argv[x];
    } else if (out_dir.empty()) {
      out_dir = argv[x];
    } else {
      fprintf(stderr, "excess argument: %s\n", argv[x]);
      print_usage();
      return 2;
    }
  }

  if (filename.empty()) {
    print_usage();
    return 2;
  }

  vector<ResourceFile> manhole_rfs;
  if (manhole_res_directory) {
    unordered_set<string> dirs_to_process({manhole_res_directory});
    while (!dirs_to_process.empty()) {
      auto it = dirs_to_process.begin();
      string dir = move(*it);
      dirs_to_process.erase(it);

      for (const string& filename : list_directory(dir)) {
        string file_path = dir + "/" + filename;
        if (isfile(file_path)) {
          manhole_rfs.emplace_back(parse_resource_fork(load_file(file_path + "/..namedfork/rsrc")));
          fprintf(stderr, "added manhole resource file: %s\n", file_path.c_str());
        } else if (isdir(file_path)) {
          dirs_to_process.emplace(file_path);
        }
      }
    };
  }

  if (out_dir.empty()) {
    out_dir = string_printf("%s.out", filename.c_str());
  }
  mkdir(out_dir.c_str(), 0777);

  string data = load_file(filename);
  StringReader r(data.data(), data.size());
  uint32_t stack_format = 0;

  shared_ptr<StackBlock> stack;
  unordered_map<uint32_t, BitmapBlock> bitmaps;
  unordered_map<uint32_t, CardOrBackgroundBlock> backgrounds;
  unordered_map<uint32_t, CardOrBackgroundBlock> cards;
  while (!r.eof()) {
    size_t block_offset = r.where();
    BlockHeader header = r.get<BlockHeader>(false);
    header.byteswap();
    size_t block_end = block_offset + header.size;

    // See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=36566 for why this is
    // needed.
    int32_t block_id = header.id;

    if (dump_raw_blocks) {
      string type_str = string_for_resource_type(header.type);
      string data = r.read(header.size);
      string output_filename = string_printf("%s/%s_%d_%zX.bin", out_dir.c_str(),
          type_str.c_str(), block_id, block_offset);
      save_file(output_filename, data);
      fprintf(stderr, "... %s\n", output_filename.c_str());
    }

    switch (header.type) {
      case 0x5354414B: // STAK
        stack.reset(new StackBlock(r));
        stack_format = stack->format;
        break;
      case 0x424B4744: // BKGD
        backgrounds.emplace(piecewise_construct, make_tuple(block_id),
            forward_as_tuple(r, stack_format));
        break;
      case 0x43415244: // CARD
        cards.emplace(piecewise_construct, make_tuple(block_id),
            forward_as_tuple(r, stack_format));
        break;
      case 0x424D4150: // BMAP
        bitmaps.emplace(piecewise_construct, make_tuple(block_id),
            forward_as_tuple(r, stack_format));
        break;

      default:
        uint32_t type_swapped = bswap32(header.type);
        fprintf(stderr, "warning: skipping unknown block at %08zX size: %08X type: %08X (%.4s) id: %08X (%d)\n",
            r.where(), header.size, type_swapped, reinterpret_cast<const char*>(&type_swapped), block_id, block_id);

        if (header.size < sizeof(BlockHeader)) {
          throw runtime_error("block is smaller than header");
        }
        r.go(block_end);
    }

    print_extra_data(r, block_end, "block");
  }

  // Disassemble stack block
  if (stack.get()) {
    string disassembly_filename = out_dir + "/stack.txt";
    auto f = fopen_unique(disassembly_filename, "wt");
    fprintf(f.get(), "-- stack: %s\n", filename.c_str());

    fprintf(f.get(), "-- format: %d (%s)\n", stack->format, stack->name_for_format(stack->format));
    string flags_str = stack->str_for_flags(stack->flags);
    fprintf(f.get(), "-- flags: 0x%hX (%s)\n", stack->flags, flags_str.c_str());
    fprintf(f.get(), "-- protect password hash: %u\n", stack->protect_password_hash);
    fprintf(f.get(), "-- maximum user level: %hu (%s)\n", stack->max_user_level, stack->name_for_user_level(stack->max_user_level));

    string window_rect_str = stack->window_rect.str();
    fprintf(f.get(), "-- window: %s\n", window_rect_str.c_str());
    string screen_rect_str = stack->screen_rect.str();
    fprintf(f.get(), "-- screen: %s\n", screen_rect_str.c_str());
    fprintf(f.get(), "-- card dimensions: w=%hu h=%hu\n", stack->card_width, stack->card_height);
    fprintf(f.get(), "-- scroll: x=%hd y=%hd\n", stack->scroll_x, stack->scroll_y);

    fprintf(f.get(), "-- background count: %u\n", stack->background_count);
    fprintf(f.get(), "-- first background id: %d\n", stack->first_background_id);

    fprintf(f.get(), "-- card count: %u\n", stack->card_count);
    fprintf(f.get(), "-- first card id: %d\n", stack->first_card_id);

    fprintf(f.get(), "-- list block id: %d\n", stack->list_block_id);
    fprintf(f.get(), "-- print block id: %d\n", stack->print_block_id);
    fprintf(f.get(), "-- font table block id: %d\n", stack->font_table_block_id);
    fprintf(f.get(), "-- style table block id: %d\n", stack->style_table_block_id);
    fprintf(f.get(), "-- free block count: %u\n", stack->free_block_count);
    fprintf(f.get(), "-- free size: %u bytes\n", stack->free_size);
    fprintf(f.get(), "-- total size: %u bytes\n", stack->total_size);
    fprintf(f.get(), "-- stack block size: %u bytes\n", stack->stack_block_size);

    fprintf(f.get(), "-- created by hypercard version: 0x%08X\n", stack->hypercard_create_version);
    fprintf(f.get(), "-- compacted by hypercard version: 0x%08X\n", stack->hypercard_compact_version);
    fprintf(f.get(), "-- modified by hypercard version: 0x%08X\n", stack->hypercard_modify_version);
    fprintf(f.get(), "-- opened by hypercard version: 0x%08X\n", stack->hypercard_open_version);

    for (size_t x = 0; x < 0x28; x++) {
      fprintf(f.get(), "-- patterns[%zu]: 0x%016" PRIX64 "\n", x, stack->patterns[x]);
    }
    fprintf(f.get(), "-- checksum: 0x%X\n", stack->checksum);
    print_formatted_script(f.get(), stack->script, stack->osa_script_data);
    fprintf(stderr, "... %s\n", disassembly_filename.c_str());
  }

  // Disassemble bitmap blocks
  for (const auto& bitmap_it : bitmaps) {
    int32_t id = bitmap_it.first;
    const auto& bmap = bitmap_it.second;

    string filename = string_printf("%s/bitmap_%d.bmp", out_dir.c_str(), id);
    bmap.image.save(filename, Image::ImageFormat::WindowsBitmap);
    fprintf(stderr, "... %s\n", filename.c_str());

    if (bmap.mask_mode == BitmapBlock::MaskMode::PRESENT) {
      string filename = string_printf("%s/bitmap_%d_mask.bmp", out_dir.c_str(), id);
      bmap.mask.save(filename, Image::ImageFormat::WindowsBitmap);
      fprintf(stderr, "... %s\n", filename.c_str());
    }
  }

  // Disassemble card and background blocks
  {
    auto disassemble_block = [&](const CardOrBackgroundBlock& block) {
      bool is_card = block.header.type == 0x43415244;
      string render_img_filename = string_printf("%s/%s_%d_render.bmp",
          out_dir.c_str(), is_card ? "card" : "background", block.header.id);
      string disassembly_filename = string_printf("%s/%s_%d.txt",
          out_dir.c_str(), is_card ? "card" : "background", block.header.id);

      // Figure out the background and bitmaps, for getting the card size and
      // producing the render image
      const CardOrBackgroundBlock* background = nullptr;
      const BitmapBlock* bmap = nullptr;
      const BitmapBlock* background_bmap = nullptr;
      if (block.bmap_block_id) {
        try {
          bmap = &bitmaps.at(block.bmap_block_id);
        } catch (const out_of_range&) {
          fprintf(stderr, "warning: could not look up bitmap %d\n", block.bmap_block_id);
        }
      }
      if (block.background_id) {
        try {
          background = &backgrounds.at(block.background_id);
        } catch (const out_of_range&) {
          fprintf(stderr, "warning: could not look up background %d\n", block.background_id);
        }
        if (background && background->bmap_block_id) {
          try {
            background_bmap = &bitmaps.at(background->bmap_block_id);
          } catch (const out_of_range&) {
            fprintf(stderr, "warning: could not look up background bitmap %d\n", background->bmap_block_id);
          }
        }
      }

      // If the stack block defines card dimensions, use them. Otherwise, use
      // the card's bitmap dimensions if it exists, or use the background's
      // bitmap dimensions if not. If none of these are defined, give up.
      size_t card_w = 0, card_h = 0;
      if (stack->card_width && stack->card_height) {
        card_w = stack->card_width;
        card_h = stack->card_height;
      }
      if (!card_w && !card_h && bmap) {
        if (!bmap->card_rect.is_empty()) {
          card_w = bmap->card_rect.x2 - bmap->card_rect.x1;
          card_h = bmap->card_rect.y2 - bmap->card_rect.y1;
        }
      }
      if (!card_w && !card_h && background_bmap) {
        if (!background_bmap->card_rect.is_empty()) {
          card_w = background_bmap->card_rect.x2 - background_bmap->card_rect.x1;
          card_h = background_bmap->card_rect.y2 - background_bmap->card_rect.y1;
        }
      }

      Image render_img(card_w, card_h);
      render_img.fill_rect(0, 0, card_w, card_h, 0xFF, 0xFF, 0xFF);

      // For The Manhole, the PICT ID is specified in a part contents entry.
      // This is a hack... we take the first part whose contents are parseable
      // as an integer and refer to a valid PICT.
      if (render_bitmap) {
        if (!manhole_rfs.empty() && card_w == 512 && card_h == 342) {
          const Image* pict = nullptr;
          for (const auto& part_contents : block.part_contents) {
            int16_t pict_id;
            try {
              pict_id = stol(part_contents.text, nullptr, 10);
            } catch (const invalid_argument&) {
              continue;
            }

            static unordered_map<int16_t, Image> picts_cache;
            try {
              pict = &picts_cache.at(pict_id);
            } catch (const out_of_range&) { }

            if (!pict) {
              for (auto& rf : manhole_rfs) {
                if (rf.resource_exists(RESOURCE_TYPE_PICT, pict_id)) {
                  auto decoded = rf.decode_PICT(pict_id);
                  if (!decoded.embedded_image_format.empty()) {
                    throw runtime_error("PICT decoded to an unusable format");
                  }
                  pict = &picts_cache.emplace(pict_id, move(decoded.image)).first->second;
                }
              }
            }

            if (pict) {
              break;
            }
          }

          if (!pict) {
            fprintf(stderr, "warning: no valid PICT found for this card\n");
          } else {
            render_img.blit(*pict, 0, 0, pict->get_width(), pict->get_height(), 0, 0);
          }

        // For regular HyperCard stacks, render the background and card bitmaps.
        } else {
          if (background_bmap) {
            background_bmap->render_into_card(render_img);
          }
          if (bmap) {
            bmap->render_into_card(render_img);
          }
        }
      }

      auto f = fopen_unique(disassembly_filename, "wt");
      fprintf(f.get(), "-- %s: %d from stack: %s\n",
          is_card ? "card" : "background", block.header.id, filename.c_str());
      fprintf(f.get(), "-- bmap block id: %d\n", block.bmap_block_id);
      fprintf(f.get(), "-- flags: %04hX\n", block.flags);
      fprintf(f.get(), "-- background id: %d\n", block.background_id);
      fprintf(f.get(), "-- name: %s\n", block.name.c_str());
      print_formatted_script(f.get(), block.script, block.osa_script_data);

      const uint32_t background_parts_render_color = 0x00FF00FF;
      const uint32_t card_parts_render_color = 0xFF0000FF;
      if (background && render_background_parts) {
        for (const auto& part : background->parts) {
          render_img.draw_horizontal_line(part.rect_left, part.rect_right, part.rect_top, 0, background_parts_render_color);
          render_img.draw_horizontal_line(part.rect_left, part.rect_right, part.rect_bottom, 0, background_parts_render_color);
          render_img.draw_vertical_line(part.rect_left, part.rect_top, part.rect_bottom, 0, background_parts_render_color);
          render_img.draw_vertical_line(part.rect_right, part.rect_top, part.rect_bottom, 0, background_parts_render_color);
          render_img.draw_text(part.rect_left + 1, part.rect_top + 1, background_parts_render_color, 0x00000000, "%hd", part.part_id);
        }
      }
      for (const auto& part : block.parts) {
        if (render_card_parts) {
          render_img.draw_horizontal_line(part.rect_left, part.rect_right, part.rect_top, 0, card_parts_render_color);
          render_img.draw_horizontal_line(part.rect_left, part.rect_right, part.rect_bottom, 0, card_parts_render_color);
          render_img.draw_vertical_line(part.rect_left, part.rect_top, part.rect_bottom, 0, card_parts_render_color);
          render_img.draw_vertical_line(part.rect_right, part.rect_top, part.rect_bottom, 0, card_parts_render_color);
          render_img.draw_text(part.rect_left + 1, part.rect_top + 1, card_parts_render_color, 0x00000000, "%hd", part.part_id);
        }
        fprintf(f.get(), "\n\n");
        if (part.type == 0 || part.type > 2) {
          fprintf(f.get(), "-- part %hd (type %hhu)\n", part.part_id, part.type);
        } else {
          fprintf(f.get(), "-- part %hd (%s)\n", part.part_id, (part.type == 1) ? "button" : "field");
        }
        fprintf(f.get(), "-- low flags: %02hhX\n", part.low_flags);
        fprintf(f.get(), "-- high flags: %04hX\n", part.high_flags);
        fprintf(f.get(), "-- rect: left=%hd top=%hd right=%hd bottom=%hd\n",
            part.rect_left, part.rect_top, part.rect_bottom, part.rect_right);
        fprintf(f.get(), "-- title width / last selected line: %hu\n", part.title_width);
        fprintf(f.get(), "-- icon id / first selected line: %hd / %hu\n", part.icon_id, part.first_selected_line);
        fprintf(f.get(), "-- text alignment: %hu\n", part.text_alignment);
        fprintf(f.get(), "-- font id: %hd\n", part.font_id);
        fprintf(f.get(), "-- text size: %hu\n", part.font_size);
        fprintf(f.get(), "-- style flags: %hu\n", part.style_flags);
        fprintf(f.get(), "-- line height: %hu\n", part.line_height);
        fprintf(f.get(), "-- part name: %s\n", part.name.c_str());
        print_formatted_script(f.get(), part.script, part.osa_script_data);
      }

      for (const auto& part_contents : block.part_contents) {
        fprintf(f.get(), "\n\n");
        fprintf(f.get(), "-- part contents for %s part %d\n",
            (part_contents.part_id < 0) ? "card" : "background",
            (part_contents.part_id < 0) ? -part_contents.part_id : part_contents.part_id);
        if (!part_contents.offset_to_style_entry_index.empty()) {
          fprintf(f.get(), "-- note: style data is present\n");
        }
        fprintf(f.get(), "----- text -----\n");
        fwritex(f.get(), part_contents.text);
      }

      fprintf(stderr, "... %s\n", disassembly_filename.c_str());

      // TODO: do something with OSA script data
      if (!card_w || !card_h) {
        fprintf(stderr, "warning: could not determine card dimensions\n");
      } else if (render_bitmap || render_background_parts || render_card_parts) {
        render_img.save(render_img_filename, Image::ImageFormat::WindowsBitmap);
        fprintf(stderr, "... %s\n", render_img_filename.c_str());
      }
    };

    for (const auto& background_it : backgrounds) {
      disassemble_block(background_it.second);
    }
    for (const auto& card_it : cards) {
      disassemble_block(card_it.second);
    }
  }

  return 0;
}
