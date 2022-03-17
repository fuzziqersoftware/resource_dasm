#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/JSON.hh>
#include <phosg/Process.hh>
#include <phosg/Strings.hh>
#include <unordered_map>
#include <vector>

#include "ResourceFile.hh"
#include "SystemTemplates.hh"
#include "M68KEmulator.hh"
#include "PPC32Emulator.hh"
#include "DOLFile.hh"
#include "IndexFormats/ResourceFork.hh"
#include "IndexFormats/Mohawk.hh"
#include "IndexFormats/HIRF.hh"
#include "IndexFormats/DCData.hh"

using namespace std;



static string output_filename(const string& out_dir, const string& base_filename,
    shared_ptr<const ResourceFile::Resource> res, const std::string& after) {
  if (base_filename.empty()) {
    return out_dir;
  }

  // Filter the type so it only contains valid filename characters
  be_uint32_t filtered_type = res->type;
  char* type_str = reinterpret_cast<char*>(&filtered_type);
  for (size_t x = 0; x < 4; x++) {
    if (type_str[x] < 0x20 || type_str[x] > 0x7E || type_str[x] == '/' || type_str[x] == ':') {
      type_str[x] = '_';
    }
  }

  // If the type ends with spaces (e.g. 'snd '), trim them off
  int type_chars = 4;
  for (ssize_t x = 3; x >= 0; x--) {
    if (type_str[x] == ' ') {
      type_chars--;
    } else {
      break;
    }
  }

  string name_token;
  if (!res->name.empty()) {
    name_token = '_';
    for (char ch : res->name) {
      if (ch < 0x20 || ch > 0x7E || ch == '/' || ch == ':') {
        name_token += '_';
      } else {
        name_token += ch;
      }
    }
  }

  if (out_dir.empty()) {
    return string_printf("%s_%.*s_%d%s%s", base_filename.c_str(),
        type_chars, type_str, res->id, name_token.c_str(), after.c_str());
  } else {
    return string_printf("%s/%s_%.*s_%d%s%s", out_dir.c_str(),
        base_filename.c_str(), type_chars, (const char*)&filtered_type, res->id,
        name_token.c_str(), after.c_str());
  }
}

static Image tile_image(const Image& i, size_t tile_x, size_t tile_y) {
  size_t w = i.get_width(), h = i.get_height();
  Image ret(w * tile_x, h * tile_y);
  for (size_t y = 0; y < tile_y; y++) {
    for (size_t x = 0; x < tile_x; x++) {
      ret.blit(i, w * x, h * y, w, h, 0, 0);
    }
  }
  return ret;
}



void write_decoded_file(const string& out_dir, const string& base_filename,
    shared_ptr<const ResourceFile::Resource> res, const string& after, const string& data) {
  string filename = output_filename(out_dir, base_filename, res, after);
  save_file(filename.c_str(), data);
  fprintf(stderr, "... %s\n", filename.c_str());
}

void write_decoded_image(const string& out_dir, const string& base_filename,
    shared_ptr<const ResourceFile::Resource> res, const string& after, const Image& img) {
  string filename = output_filename(out_dir, base_filename, res, after);
  img.save(filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", filename.c_str());
}

void write_decoded_TMPL(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  using Entry = ResourceFile::TemplateEntry;
  using Type = Entry::Type;
  using Format = Entry::Format;

  auto decoded = rf.decode_TMPL(res);

  deque<string> lines;
  auto add_line = [&](string&& line) {
    lines.emplace_back(move(line));
  };
  function<void(const vector<shared_ptr<Entry>>& entries, size_t indent_level)> process_entries = [&](
      const vector<shared_ptr<Entry>>& entries, size_t indent_level) {
    for (const auto& entry : entries) {
      string prefix(indent_level * 2, ' ');

      if (entry->type == Type::VOID) {
        if (entry->name.empty()) {
          add_line(prefix + "# (empty comment)");
        } else {
          add_line(prefix + "# " + entry->name);
        }
        continue;
      }

      if (!entry->name.empty()) {
        prefix += entry->name;
        prefix += ": ";
      }

      switch (entry->type) {
        //Note: Type::VOID is already handled above, before prefix computation
        case Type::INTEGER:
        case Type::FIXED_POINT:
        case Type::POINT_2D: {
          const char* type_str = (entry->type == Type::INTEGER) ? "integer" : "fixed-point";
          uint16_t width = entry->width * (1 + (entry->type == Type::FIXED_POINT));
          const char* format_str;
          switch (entry->format) {
            case Format::DECIMAL:
              format_str = "decimal";
              break;
            case Format::HEX:
              format_str = "hex";
              break;
            case Format::TEXT:
              format_str = "char";
              break;
            case Format::FLAG:
              format_str = "flag";
              break;
            case Format::DATE:
              format_str = "date";
              break;
            default:
              throw logic_error("unknown format");
          }
          string case_names_str;
          if (!entry->case_names.empty()) {
            vector<string> tokens;
            for (const auto& case_name : entry->case_names) {
              tokens.emplace_back(string_printf("%" PRId32 " = %s", case_name.first, case_name.second.c_str()));
            }
            case_names_str = " (" + join(tokens, ", ") + ")";
          }
          add_line(prefix + string_printf("%hu-byte %s (%s)", width, type_str, format_str) + case_names_str);
          break;
        }
        case Type::ALIGNMENT:
          add_line(prefix + string_printf("(align to %hu-byte boundary)", entry->end_alignment));
          break;
        case Type::ZERO_FILL:
          add_line(prefix + string_printf("%hu-byte zero fill", entry->width));
          break;
        case Type::EOF_STRING:
          add_line(prefix + "rest of data in resource");
          break;
        case Type::STRING:
          add_line(prefix + string_printf("%hu data bytes", entry->width));
          break;
        case Type::PSTRING:
        case Type::CSTRING: {
          string line = prefix;
          if (entry->type == Type::PSTRING) {
            line += string_printf("pstring (%hu-byte length)", entry->width);
          } else {
            line += "cstring";
          }
          if (entry->end_alignment) {
            if (entry->align_offset) {
              line += string_printf(" (padded to %hhu-byte alignment with %hhu-byte offset)", entry->end_alignment, entry->align_offset);
            } else {
              line += string_printf(" (padded to %hhu-byte alignment)", entry->end_alignment);
            }
          }
          add_line(move(line));
          break;
        }
        case Type::FIXED_PSTRING:
          // Note: The length byte is NOT included in entry->width, in contrast
          // to FIXED_CSTRING (where the \0 at the end IS included). This is why
          // we +1 here.
          add_line(prefix + string_printf("pstring (1-byte length; %u bytes reserved)", entry->width + 1));
          break;
        case Type::FIXED_CSTRING:
          add_line(prefix + string_printf("cstring (%hu bytes reserved)", entry->width));
          break;
        case Type::BOOL:
          add_line(prefix + "boolean");
          break;
        case Type::BITFIELD:
          add_line(prefix + "(bit field)");
          process_entries(entry->list_entries, indent_level + 1);
          break;
        case Type::RECT:
          add_line(prefix + "rectangle");
          break;
        case Type::COLOR:
          add_line(prefix + "color (48-bit RGB)");
          break;
        case Type::LIST_ZERO_BYTE:
          add_line(prefix + "list (terminated by zero byte)");
          process_entries(entry->list_entries, indent_level + 1);
          break;
        case Type::LIST_ZERO_COUNT:
          add_line(prefix + string_printf("list (%hu-byte zero-based item count)", entry->width));
          process_entries(entry->list_entries, indent_level + 1);
          break;
        case Type::LIST_ONE_COUNT:
          add_line(prefix + string_printf("list (%hu-byte one-based item count)", entry->width));
          process_entries(entry->list_entries, indent_level + 1);
          break;
        case Type::LIST_EOF:
          add_line(prefix + "list (until end of resource)");
          process_entries(entry->list_entries, indent_level + 1);
          break;
        default:
          throw logic_error("unknown entry type in TMPL");
      }
    }
  };
  process_entries(decoded, 0);

  write_decoded_file(out_dir, base_filename, res, ".txt", join(lines, "\n"));
}

void write_decoded_CURS(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_CURS(res);
  string after = string_printf("_%hu_%hu.bmp", decoded.hotspot_x, decoded.hotspot_y);
  write_decoded_image(out_dir, base_filename, res, after, decoded.bitmap);
}

void write_decoded_crsr(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_crsr(res);
  string bitmap_after = string_printf("_%hu_%hu_bitmap.bmp", decoded.hotspot_x, decoded.hotspot_y);
  string after = string_printf("_%hu_%hu.bmp", decoded.hotspot_x, decoded.hotspot_y);
  write_decoded_image(out_dir, base_filename, res, bitmap_after, decoded.bitmap);
  write_decoded_image(out_dir, base_filename, res, after, decoded.image);
}

void write_decoded_ppat(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_ppat(res);

  Image tiled = tile_image(decoded.pattern, 8, 8);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded.pattern);
  write_decoded_image(out_dir, base_filename, res, "_tiled.bmp", tiled);

  tiled = tile_image(decoded.monochrome_pattern, 8, 8);
  write_decoded_image(out_dir, base_filename, res, "_bitmap.bmp", decoded.monochrome_pattern);
  write_decoded_image(out_dir, base_filename, res, "_bitmap_tiled.bmp", tiled);
}

void write_decoded_pptN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_pptN(res);

  for (size_t x = 0; x < decoded.size(); x++) {
    string after = string_printf("_%zu.bmp", x);
    write_decoded_image(out_dir, base_filename, res, after, decoded[x].pattern);

    Image tiled = tile_image(decoded[x].pattern, 8, 8);
    after = string_printf("_%zu_tiled.bmp", x);
    write_decoded_image(out_dir, base_filename, res, after, tiled);

    after = string_printf("_%zu_bitmap.bmp", x);
    write_decoded_image(out_dir, base_filename, res, after, decoded[x].monochrome_pattern);

    tiled = tile_image(decoded[x].monochrome_pattern, 8, 8);
    after = string_printf("_%zu_bitmap_tiled.bmp", x);
    write_decoded_image(out_dir, base_filename, res, after, tiled);
  }
}

void write_decoded_color_table(const string& out_dir,
    const string& base_filename, shared_ptr<const ResourceFile::Resource> res,
    const vector<ColorTableEntry>& decoded,
    const unordered_map<uint16_t, string>* index_names = nullptr) {
  if (decoded.size() == 0) {
    Image img(122, 16, false);
    img.clear(0x00, 0x00, 0x00);
    img.draw_text(4, 4, 0xFFFFFFFF, 0x00000000, "No colors in table");
    write_decoded_image(out_dir, base_filename, res, ".bmp", img);

  } else {
    // Compute the image width based on the maximum length of index names
    size_t max_name_length = 5; // '65535' for unnamed indexes
    if (index_names != nullptr) {
      for (const auto& entry : decoded) {
        try {
          size_t name_length = index_names->at(entry.color_num).size();
          if (name_length > max_name_length) {
            max_name_length = name_length;
          }
        } catch (const out_of_range&) { }
      }
    }

    Image img(122 + 6 * max_name_length, 16 * decoded.size(), false);
    img.clear(0x00, 0x00, 0x00);
    for (size_t z = 0; z < decoded.size(); z++) {
      img.fill_rect(0, 16 * z, 16, 16, decoded[z].c.r / 0x0101,
          decoded[z].c.g / 0x0101, decoded[z].c.b / 0x0101);

      ssize_t x = 20, y = 16 * z + 4, width = 0;
      img.draw_text(x, y, &width, nullptr, 0xFFFFFFFF, 0x00000000, "#");
      x += width;

      img.draw_text(x, y, &width, nullptr, 0xFF0000FF, 0x00000000, "%04hX",
          decoded[z].c.r.load());
      x += width;

      img.draw_text(x, y, &width, nullptr, 0x00FF00FF, 0x00000000, "%04hX",
          decoded[z].c.g.load());
      x += width;

      img.draw_text(x, y, &width, nullptr, 0x0000FFFF, 0x00000000, "%04hX",
          decoded[z].c.b.load());
      x += width;

      const char* name = nullptr;
      if (index_names) {
        try {
          name = index_names->at(decoded[z].color_num).c_str();
        } catch (const out_of_range&) { }
      }

      if (name) {
        img.draw_text(x, y, &width, nullptr, 0xFFFFFFFF, 0x00000000, " (%s)", name);
      } else {
        img.draw_text(x, y, &width, nullptr, 0xFFFFFFFF, 0x00000000, " (%hu)",
            decoded[z].color_num.load());
      }
      x += width;
    }
    write_decoded_image(out_dir, base_filename, res, ".bmp", img);
  }
}

void write_decoded_pltt(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  // Always write the raw for this resource type because the decoded version
  // loses precision
  write_decoded_file(out_dir, base_filename, res, ".bin", res->data);

  auto decoded = rf.decode_pltt(res);
  // Add appropriate color IDs to ths pltt so we can render it as if it were a
  // clut
  vector<ColorTableEntry> entries;
  entries.reserve(decoded.size());
  for (const auto& c : decoded) {
    auto& entry = entries.emplace_back();
    entry.color_num = entries.size() - 1;
    entry.c = c;
  }
  write_decoded_color_table(out_dir, base_filename, res, entries);
}

void write_decoded_clut_actb_cctb_dctb_fctb_wctb(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  // Always write the raw for this resource type because the decoded version
  // loses precision
  write_decoded_file(out_dir, base_filename, res, ".bin", res->data);

  static const unordered_map<uint16_t, string> wctb_index_names({
    {0, "0: wContentColor"},
    {1, "1: wFrameColor"},
    {2, "2: wTextColor"},
    {3, "3: wHiliteColor"},
    {4, "4: wTitleBarColor"},
    {5, "5: wHiliteColorLight"},
    {6, "6: wHiliteColorDark"},
    {7, "7: wTitleBarLight"},
    {8, "8: wTitleBarDark"},
    {9, "9: wDialogLight"},
    {10, "10: wDialogDark"},
    {11, "11: wTingeLight"},
    {12, "12: wTingeDark"},
  });
  static const unordered_map<uint16_t, string> cctb_index_names({
    {0, "0: cFrameColor"},
    {1, "1: cBodyColor"},
    {2, "2: cTextColor"},
    {5, "5: cArrowsColorLight"},
    {6, "6: cArrowsColorDark"},
    {7, "7: cThumbLight"},
    {8, "8: cThumbDark"},
    {9, "9: cHiliteLight"},
    {10, "10: cHiliteDark"},
    {11, "11: cTitleBarLight"},
    {12, "12: cTitleBarDark"},
    {13, "13: cTingeLight"},
    {14, "14: cTingeDark"},
  });

  static const unordered_map<uint32_t, const unordered_map<uint16_t, string>&> index_names_for_type({
    {RESOURCE_TYPE_cctb, cctb_index_names},
    {RESOURCE_TYPE_actb, wctb_index_names},
    {RESOURCE_TYPE_dctb, wctb_index_names},
    {RESOURCE_TYPE_wctb, wctb_index_names},
  });

  const unordered_map<uint16_t, string>* index_names = nullptr;
  try {
    index_names = &index_names_for_type.at(res->type);
  } catch (const out_of_range&) { }

  // These resources are all the same format, so it's ok to call decode_clut
  // here instead of the type-specific functions
  write_decoded_color_table(out_dir, base_filename, res, rf.decode_clut(res),
      index_names);
}

void write_decoded_CTBL(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  // Always write the raw for this resource type because some tools demand it
  write_decoded_file(out_dir, base_filename, res, ".bin", res->data);
  write_decoded_color_table(out_dir, base_filename, res, rf.decode_CTBL(res));
}

void write_decoded_PAT(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  Image decoded = rf.decode_PAT(res);

  Image tiled = tile_image(decoded, 8, 8);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
  write_decoded_image(out_dir, base_filename, res, "_tiled.bmp", tiled);
}

void write_decoded_PATN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_PATN(res);

  for (size_t x = 0; x < decoded.size(); x++) {
    string after = string_printf("_%zu.bmp", x);
    write_decoded_image(out_dir, base_filename, res, after, decoded[x]);

    Image tiled = tile_image(decoded[x], 8, 8);
    after = string_printf("_%zu_tiled.bmp", x);
    write_decoded_image(out_dir, base_filename, res, after, tiled);
  }
}

void write_decoded_SICN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_SICN(res);

  for (size_t x = 0; x < decoded.size(); x++) {
    string after = string_printf("_%zu.bmp", x);
    write_decoded_image(out_dir, base_filename, res, after, decoded[x]);
  }
}

void write_decoded_ICNN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_ICNN(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_icmN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_icmN(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_icsN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_icsN(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_kcsN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_kcsN(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_cicn(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_cicn(res);

  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded.image);

  if (decoded.bitmap.get_width() && decoded.bitmap.get_height()) {
    write_decoded_image(out_dir, base_filename, res, "_bitmap.bmp", decoded.bitmap);
  }
}

void write_decoded_icl8(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_icl8(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_icm8(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_icm8(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_ics8(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_ics8(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_kcs8(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_kcs8(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_icl4(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_icl4(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_icm4(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_icm4(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_ics4(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_ics4(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_kcs4(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_kcs4(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_ICON(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_ICON(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_PICT_internal(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_PICT_internal(res);
  if (!decoded.embedded_image_data.empty()) {
    write_decoded_file(out_dir, base_filename, res, "." + decoded.embedded_image_format, decoded.embedded_image_data);
  } else {
    write_decoded_image(out_dir, base_filename, res, ".bmp", decoded.image);
  }
}

void write_decoded_PICT(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_PICT(res);
  if (!decoded.embedded_image_data.empty()) {
    write_decoded_file(out_dir, base_filename, res, "." + decoded.embedded_image_format, decoded.embedded_image_data);
  } else {
    write_decoded_image(out_dir, base_filename, res, ".bmp", decoded.image);
  }
}

void write_decoded_snd(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_snd(res);
  write_decoded_file(out_dir, base_filename, res,
      decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
}

void write_decoded_csnd(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_csnd(res);
  write_decoded_file(out_dir, base_filename, res,
      decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
}

void write_decoded_esnd(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_esnd(res);
  write_decoded_file(out_dir, base_filename, res,
      decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
}

void write_decoded_ESnd(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_ESnd(res);
  write_decoded_file(out_dir, base_filename, res,
      decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
}

void write_decoded_Ysnd(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_Ysnd(res);
  write_decoded_file(out_dir, base_filename, res,
      decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
}

void write_decoded_SMSD(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  string decoded = rf.decode_SMSD(res);
  write_decoded_file(out_dir, base_filename, res, ".wav", decoded);
}

void write_decoded_SOUN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  string decoded = rf.decode_SOUN(res);
  write_decoded_file(out_dir, base_filename, res, ".wav", decoded);
}

void write_decoded_cmid(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  string decoded = rf.decode_cmid(res);
  write_decoded_file(out_dir, base_filename, res, ".midi", decoded);
}

void write_decoded_emid(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  string decoded = rf.decode_emid(res);
  write_decoded_file(out_dir, base_filename, res, ".midi", decoded);
}

void write_decoded_ecmi(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  string decoded = rf.decode_ecmi(res);
  write_decoded_file(out_dir, base_filename, res, ".midi", decoded);
}

void write_decoded_FONT_NFNT(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_FONT(res);

  {
    string description_filename = output_filename(out_dir, base_filename, res, "_description.txt");
    auto f = fopen_unique(description_filename, "wt");
    fprintf(f.get(), "\
# source_bit_depth = %hhu (%s color table)\n\
# dynamic: %s\n\
# has non-black colors: %s\n\
# fixed-width: %s\n\
# character range: %02hX - %02hX\n\
# maximum width: %hu\n\
# maximum kerning: %hd\n\
# rectangle: %hu x %hu\n\
# maximum ascent: %hd\n\
# maximum descent: %hd\n\
# leading: %hd\n",
        decoded.source_bit_depth,
        decoded.color_table.empty() ? "no" : "has",
        decoded.is_dynamic ? "yes" : "no",
        decoded.has_non_black_colors ? "yes" : "no",
        decoded.fixed_width ? "yes" : "no",
        decoded.first_char,
        decoded.last_char,
        decoded.max_width,
        decoded.max_kerning,
        decoded.rect_width,
        decoded.rect_height,
        decoded.max_ascent,
        decoded.max_descent,
        decoded.leading);

    for (const auto& glyph : decoded.glyphs) {
      if (isprint(glyph.ch)) {
        fprintf(f.get(), "\n# glyph %02hX (%c)\n", glyph.ch, glyph.ch);
      } else {
        fprintf(f.get(), "\n# glyph %02hX\n", glyph.ch);
      }
      fprintf(f.get(), "#   bitmap offset: %hu; width: %hu\n", glyph.bitmap_offset, glyph.bitmap_width);
      fprintf(f.get(), "#   character offset: %hhd; width: %hhu\n", glyph.offset, glyph.width);
    }

    fprintf(f.get(), "\n# missing glyph\n");
    fprintf(f.get(), "#   bitmap offset: %hu; width: %hu\n", decoded.missing_glyph.bitmap_offset, decoded.missing_glyph.bitmap_width);
    fprintf(f.get(), "#   character offset: %hhd; width: %hhu\n", decoded.missing_glyph.offset, decoded.missing_glyph.width);

    fprintf(stderr, "... %s\n", description_filename.c_str());
  }

  if (decoded.missing_glyph.img.get_width()) {
    write_decoded_image(out_dir, base_filename, res, "_glyph_missing.bmp", decoded.missing_glyph.img);
  }

  for (size_t x = 0; x < decoded.glyphs.size(); x++) {
    if (!decoded.glyphs[x].img.get_width()) {
      continue;
    }
    string after = string_printf("_glyph_%02zX.bmp", decoded.first_char + x);
    write_decoded_image(out_dir, base_filename, res, after, decoded.glyphs[x].img);
  }
}

string generate_text_for_cfrg(const vector<ResourceFile::DecodedCodeFragmentEntry>& entries) {
  string ret;
  for (size_t x = 0; x < entries.size(); x++) {
    const auto& entry = entries[x];

    string arch_str = string_for_resource_type(entry.architecture);
    string this_entry_ret;
    if (!entry.name.empty()) {
      this_entry_ret += string_printf("fragment %zu: \"%s\"\n", x, entry.name.c_str());
    } else {
      this_entry_ret += string_printf("fragment %zu: (unnamed)\n", x);
    }
    this_entry_ret += string_printf("  architecture: 0x%08X (%s)\n", entry.architecture, arch_str.c_str());
    this_entry_ret += string_printf("  update_level: 0x%02hhX\n", entry.update_level);
    this_entry_ret += string_printf("  current_version: 0x%08X\n", entry.current_version);
    this_entry_ret += string_printf("  old_def_version: 0x%08X\n", entry.old_def_version);
    this_entry_ret += string_printf("  app_stack_size: 0x%08X\n", entry.app_stack_size);
    this_entry_ret += string_printf("  app_subdir_id/lib_flags: 0x%04hX\n", entry.app_subdir_id);

    uint8_t usage = static_cast<uint8_t>(entry.usage);
    if (usage < 5) {
      static const char* names[5] = {
        "import library",
        "application",
        "drop-in addition",
        "stub library",
        "weak stub library",
      };
      this_entry_ret += string_printf("  usage: 0x%02hhX (%s)\n", usage, names[usage]);
    } else {
      this_entry_ret += string_printf("  usage: 0x%02hhX (invalid)\n", usage);
    }

    uint8_t where = static_cast<uint8_t>(entry.where);
    if (where < 5) {
      static const char* names[5] = {
        "memory",
        "data fork",
        "resource",
        "byte stream",
        "named fragment",
      };
      this_entry_ret += string_printf("  where: 0x%02hhX (%s)\n", where, names[where]);
    } else {
      this_entry_ret += string_printf("  where: 0x%02hhX (invalid)\n", where);
    }

    if (entry.where == ResourceFile::DecodedCodeFragmentEntry::Where::RESOURCE) {
      string type_str = string_for_resource_type(entry.offset);
      this_entry_ret += string_printf("  resource: 0x%08X (%s) #%d\n",
          entry.offset, type_str.c_str(), static_cast<int32_t>(entry.length));
    } else {
      this_entry_ret += string_printf("  offset: 0x%08X\n", entry.offset);
      if (entry.length == 0) {
        this_entry_ret += "  length: (entire contents)\n";
      } else {
        this_entry_ret += string_printf("  length: 0x%08X\n", entry.length);
      }
    }
    this_entry_ret += string_printf("  space_id/fork_kind: 0x%08X\n", entry.space_id);
    this_entry_ret += string_printf("  fork_instance: 0x%04hX\n", entry.fork_instance);
    if (!entry.extension_data.empty()) {
      this_entry_ret += string_printf("  extension_data (%hu): ", entry.extension_count);
      this_entry_ret += format_data_string(entry.extension_data);
      this_entry_ret += '\n';
    }

    ret += this_entry_ret;
  }

  return ret;
}

void write_decoded_cfrg(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  string description = generate_text_for_cfrg(rf.decode_cfrg(res));
  write_decoded_file(out_dir, base_filename, res, ".txt", description);
}

void write_decoded_SIZE(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_SIZE(res);
  string disassembly = string_printf("\
# save_screen = %s\n\
# accept_suspend_events = %s\n\
# disable_option = %s\n\
# can_background = %s\n\
# activate_on_fg_switch = %s\n\
# only_background = %s\n\
# get_front_clicks = %s\n\
# accept_died_events = %s\n\
# clean_addressing = %s\n\
# high_level_event_aware = %s\n\
# local_and_remote_high_level_events = %s\n\
# stationery_aware = %s\n\
# use_text_edit_services = %s\n\
# size = %08" PRIX32 "\n\
# min_size = %08" PRIX32 "\n",
      decoded.save_screen ? "true" : "false",
      decoded.accept_suspend_events ? "true" : "false",
      decoded.disable_option ? "true" : "false",
      decoded.can_background ? "true" : "false",
      decoded.activate_on_fg_switch ? "true" : "false",
      decoded.only_background ? "true" : "false",
      decoded.get_front_clicks ? "true" : "false",
      decoded.accept_died_events ? "true" : "false",
      decoded.clean_addressing ? "true" : "false",
      decoded.high_level_event_aware ? "true" : "false",
      decoded.local_and_remote_high_level_events ? "true" : "false",
      decoded.stationery_aware ? "true" : "false",
      decoded.use_text_edit_services ? "true" : "false",
      decoded.size,
      decoded.min_size);
  write_decoded_file(out_dir, base_filename, res, ".txt", disassembly);
}

void write_decoded_vers(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_vers(res);

  string dev_stage_str = string_printf("0x%02hhX", decoded.development_stage);
  if (decoded.development_stage == 0x20) {
    dev_stage_str += " (development)";
  } else if (decoded.development_stage == 0x40) {
    dev_stage_str += " (alpha)";
  } else if (decoded.development_stage == 0x60) {
    dev_stage_str += " (beta)";
  } else if (decoded.development_stage == 0x80) {
    dev_stage_str += " (release)";
  }

  string region_code_str = string_printf("0x%04hX", decoded.region_code);
  const char* region_name = name_for_region_code(decoded.region_code);
  if (region_name) {
    region_code_str += " (";
    region_code_str += region_name;
    region_code_str += ")";
  }

  string disassembly = string_printf("\
# major_version = %hhu\n\
# minor_version = %hhu\n\
# development_stage = %s\n\
# prerelease_version_level = %hhu\n\
# region_code = %s\n\
# version_number = %s\n\
# version_message = %s\n",
      decoded.major_version,
      decoded.minor_version,
      dev_stage_str.c_str(),
      decoded.prerelease_version_level,
      region_code_str.c_str(),
      decoded.version_number.c_str(),
      decoded.version_message.c_str());
  write_decoded_file(out_dir, base_filename, res, ".txt", disassembly);
}

void write_decoded_finf(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_finf(res);

  string disassembly;
  for (size_t x = 0; x < decoded.size(); x++) {
    const auto& finf = decoded[x];

    string font_id_str = string_printf("%hd", finf.font_id);
    const char* font_name = name_for_font_id(finf.font_id);
    if (font_name) {
      font_id_str += " (";
      font_id_str += font_name;
      font_id_str += ")";
    }

    vector<const char*> style_tokens;
    if (finf.style_flags & ResourceFile::TextStyleFlag::BOLD) {
      style_tokens.emplace_back("bold");
    }
    if (finf.style_flags & ResourceFile::TextStyleFlag::ITALIC) {
      style_tokens.emplace_back("italic");
    }
    if (finf.style_flags & ResourceFile::TextStyleFlag::UNDERLINE) {
      style_tokens.emplace_back("underline");
    }
    if (finf.style_flags & ResourceFile::TextStyleFlag::OUTLINE) {
      style_tokens.emplace_back("outline");
    }
    if (finf.style_flags & ResourceFile::TextStyleFlag::SHADOW) {
      style_tokens.emplace_back("shadow");
    }
    if (finf.style_flags & ResourceFile::TextStyleFlag::CONDENSED) {
      style_tokens.emplace_back("condensed");
    }
    if (finf.style_flags & ResourceFile::TextStyleFlag::EXTENDED) {
      style_tokens.emplace_back("extended");
    }

    string style_str;
    if (style_tokens.empty()) {
      style_str = "normal";
    } else {
      style_str = join(style_tokens, ", ");
    }

    disassembly += string_printf("\
# font info #%zu\n\
# font_id = %s\n\
# style_flags = 0x%04hX (%s)\n\
# size = %hu\n\n",
        x,
        font_id_str.c_str(),
        finf.style_flags,
        style_str.c_str(),
        finf.size);
  }

  write_decoded_file(out_dir, base_filename, res, ".txt", disassembly);
}

void write_decoded_ROvN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_ROvN(res);

  string disassembly = string_printf("# ROM version: 0x%04hX\n", decoded.rom_version);
  for (size_t x = 0; x < decoded.overrides.size(); x++) {
    const auto& override = decoded.overrides[x];
    string type_name = string_for_resource_type(override.type);
    disassembly += string_printf("# override %zu: %08X (%s) #%hd\n",
        x, override.type.load(), type_name.c_str(), override.id.load());
  }

  write_decoded_file(out_dir, base_filename, res, ".txt", disassembly);
}

void write_decoded_CODE(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  string disassembly;
  if (res->id == 0) {
    auto decoded = rf.decode_CODE_0(res);
    disassembly += string_printf("# above A5 size: 0x%08X\n", decoded.above_a5_size);
    disassembly += string_printf("# below A5 size: 0x%08X\n", decoded.below_a5_size);
    for (size_t x = 0; x < decoded.jump_table.size(); x++) {
      const auto& e = decoded.jump_table[x];
      if (e.code_resource_id && e.offset) {
        disassembly += string_printf("# export %zu [A5 + 0x%zX]: CODE %hd offset 0x%hX after header\n",
            x, 0x22 + (x * 8), e.code_resource_id, e.offset);
      }
    }

  } else {
    auto decoded = rf.decode_CODE(res);

    // attempt to decode CODE 0 to get the exported label offsets
    multimap<uint32_t, string> labels;
    try {
      auto code0_data = rf.decode_CODE_0(0, res->type);
      for (size_t x = 0; x < code0_data.jump_table.size(); x++) {
        const auto& e = code0_data.jump_table[x];
        if (e.code_resource_id == res->id) {
          labels.emplace(e.offset, string_printf("export_%zu", x));
        }
      }
    } catch (const exception&) { }

    if (decoded.first_jump_table_entry < 0) {
      disassembly += "# far model CODE resource\n";
      disassembly += string_printf("# near model jump table entries starting at A5 + 0x%08X (%u of them)\n",
          decoded.near_entry_start_a5_offset, decoded.near_entry_count);
      disassembly += string_printf("# far model jump table entries starting at A5 + 0x%08X (%u of them)\n",
          decoded.far_entry_start_a5_offset, decoded.far_entry_count);
      disassembly += string_printf("# A5 relocation data at 0x%08X\n", decoded.a5_relocation_data_offset);
      for (uint32_t addr : decoded.a5_relocation_addresses) {
        disassembly += string_printf("#   A5 relocation at %08X\n", addr);
      }
      disassembly += string_printf("# A5 is 0x%08X\n", decoded.a5);
      disassembly += string_printf("# PC relocation data at 0x%08X\n", decoded.pc_relocation_data_offset);
      for (uint32_t addr : decoded.pc_relocation_addresses) {
        disassembly += string_printf("#   PC relocation at %08X\n", addr);
      }
      disassembly += string_printf("# load address is 0x%08X\n", decoded.load_address);
    } else {
      disassembly += "# near model CODE resource\n";
      if (decoded.num_jump_table_entries == 0) {
        disassembly += string_printf("# this CODE claims to have no jump table entries (but starts at %04X)\n", decoded.first_jump_table_entry);
      } else {
        disassembly += string_printf("# jump table entries: %d-%d (%hu of them)\n",
            decoded.first_jump_table_entry,
            decoded.first_jump_table_entry + decoded.num_jump_table_entries - 1,
            decoded.num_jump_table_entries);
      }
    }

    disassembly += M68KEmulator::disassemble(decoded.code.data(), decoded.code.size(), 0, &labels);
  }

  write_decoded_file(out_dir, base_filename, res, ".txt", disassembly);
}

void write_decoded_DRVR(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_DRVR(res);

  string disassembly;

  vector<const char*> flags_strs;
  if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::EnableRead) {
    flags_strs.emplace_back("EnableRead");
  }
  if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::EnableWrite) {
    flags_strs.emplace_back("EnableWrite");
  }
  if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::EnableControl) {
    flags_strs.emplace_back("EnableControl");
  }
  if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::EnableStatus) {
    flags_strs.emplace_back("EnableStatus");
  }
  if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::NeedGoodbye) {
    flags_strs.emplace_back("NeedGoodbye");
  }
  if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::NeedTime) {
    flags_strs.emplace_back("NeedTime");
  }
  if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::NeedLock) {
    flags_strs.emplace_back("NeedLock");
  }
  string flags_str = join(flags_strs, ",");

  if (decoded.name.empty()) {
    disassembly += "# no name present\n";
  } else {
    disassembly += string_printf("# name: %s\n", decoded.name.c_str());
  }

  if (flags_str.empty()) {
    disassembly += string_printf("# flags: 0x%04hX\n", decoded.flags);
  } else {
    disassembly += string_printf("# flags: 0x%04hX (%s)\n", decoded.flags, flags_str.c_str());
  }

  disassembly += string_printf("# delay: %hu\n", decoded.delay);
  disassembly += string_printf("# event mask: 0x%04hX\n", decoded.event_mask);
  disassembly += string_printf("# menu id: %hd\n", decoded.menu_id);

  multimap<uint32_t, string> labels;

  auto add_label = [&](int32_t label, const char* name) {
    if (label < 0) {
      disassembly += string_printf("# %s label: missing\n", name);
    } else {
      disassembly += string_printf("# %s label: %04X\n", name, label);
      labels.emplace(label, name);
    }
  };
  add_label(decoded.open_label, "open");
  add_label(decoded.prime_label, "prime");
  add_label(decoded.control_label, "control");
  add_label(decoded.status_label, "status");
  add_label(decoded.close_label, "close");

  disassembly += M68KEmulator::disassemble(decoded.code.data(), decoded.code.size(), 0, &labels);

  write_decoded_file(out_dir, base_filename, res, ".txt", disassembly);
}

void write_decoded_dcmp(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_dcmp(res);

  multimap<uint32_t, string> labels;
  if (decoded.init_label >= 0) {
    labels.emplace(decoded.init_label, "init");
  }
  if (decoded.decompress_label >= 0) {
    labels.emplace(decoded.decompress_label, "decompress");
  }
  if (decoded.exit_label >= 0) {
    labels.emplace(decoded.exit_label, "exit");
  }
  string result = M68KEmulator::disassemble(decoded.code.data(),
      decoded.code.size(), decoded.pc_offset, &labels);

  write_decoded_file(out_dir, base_filename, res, ".txt", result);
}

void write_decoded_inline_68k(const string& out_dir, const string& base_filename,
    ResourceFile&, shared_ptr<const ResourceFile::Resource> res) {
  multimap<uint32_t, string> labels;
  labels.emplace(0, "start");
  string result = M68KEmulator::disassemble(res->data.data(), res->data.size(), 0,
      &labels);
  write_decoded_file(out_dir, base_filename, res, ".txt", result);
}

void write_decoded_inline_ppc32(const string& out_dir, const string& base_filename,
    ResourceFile&, shared_ptr<const ResourceFile::Resource> res) {
  multimap<uint32_t, string> labels;
  labels.emplace(0, "start");
  string result = PPC32Emulator::disassemble(res->data.data(), res->data.size(),
      0, &labels);
  write_decoded_file(out_dir, base_filename, res, ".txt", result);
}

void write_decoded_peff(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto peff = rf.decode_peff(res);
  string filename = output_filename(out_dir, base_filename, res, ".txt");
  auto f = fopen_unique(filename, "wt");
  peff.print(f.get());
  fprintf(stderr, "... %s\n", filename.c_str());
}

void write_decoded_expt_nsrd(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = (res->type == RESOURCE_TYPE_expt) ? rf.decode_expt(res) : rf.decode_nsrd(res);
  string filename = output_filename(out_dir, base_filename, res, ".txt");
  auto f = fopen_unique(filename, "wt");
  fputs("Mixed-mode manager header:\n", f.get());
  print_data(f.get(), decoded.header);
  fputc('\n', f.get());
  decoded.peff.print(f.get());
  fprintf(stderr, "... %s\n", filename.c_str());
}

void write_decoded_inline_68k_or_peff(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  if (res->data.size() < 4) {
    throw runtime_error("can\'t determine code type");
  }
  if (*reinterpret_cast<const be_uint32_t*>(res->data.data()) == 0x4A6F7921) { // Joy!
    write_decoded_peff(out_dir, base_filename, rf, res);
  } else {
    write_decoded_inline_68k(out_dir, base_filename, rf, res);
  }
}

void write_decoded_TEXT(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_TEXT(res));
}

void write_decoded_card(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_card(res));
}

void write_decoded_styl(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  write_decoded_file(out_dir, base_filename, res, ".rtf", rf.decode_styl(res));
}

void write_decoded_STR(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_STR(res);

  write_decoded_file(out_dir, base_filename, res, ".txt", decoded.str);
  if (!decoded.after_data.empty()) {
    write_decoded_file(out_dir, base_filename, res, "_data.bin", decoded.after_data);
  }
}

void write_decoded_STRN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto decoded = rf.decode_STRN(res);

  for (size_t x = 0; x < decoded.strs.size(); x++) {
    string after = string_printf("_%lu.txt", x);
    write_decoded_file(out_dir, base_filename, res, after, decoded.strs[x]);
  }
  if (!decoded.after_data.empty()) {
    write_decoded_file(out_dir, base_filename, res, "_excess.bin", decoded.after_data);
  }
}

shared_ptr<JSONObject> generate_json_for_INST(
    const string& base_filename,
    ResourceFile& rf,
    int32_t id,
    const ResourceFile::DecodedInstrumentResource& inst) {
  // SoundMusicSys has a (bug? feature?) where the instrument's base note
  // affects which key region is used, but then the key region's base note
  // determines the played note pitch and the instrument's base note is ignored.
  // To correct for this, we have to shift all the key regions up/down by an
  // appropriate amount, but also use freq_mult to adjust their pitches.
  int8_t key_region_boundary_shift = 0;
  if ((inst.key_regions.size() > 1) && inst.base_note) {
    key_region_boundary_shift = inst.base_note - 0x3C;
  }

  vector<shared_ptr<JSONObject>> key_regions_list;
  for (const auto& rgn : inst.key_regions) {
    const auto& snd_res = rf.get_resource(rgn.snd_type, rgn.snd_id);
    unordered_map<string, shared_ptr<JSONObject>> key_region_dict;
    key_region_dict.emplace("key_low", new JSONObject(static_cast<int64_t>(rgn.key_low + key_region_boundary_shift)));
    key_region_dict.emplace("key_high", new JSONObject(static_cast<int64_t>(rgn.key_high + key_region_boundary_shift)));

    uint8_t snd_base_note = 0x3C;
    uint32_t snd_sample_rate = 22050;
    bool snd_is_mp3 = false;
    try {
      // TODO: This is dumb; we only need the sample rate and base note; find
      // a way to not have to re-decode the sound.
      ResourceFile::DecodedSoundResource decoded_snd;
      if (rgn.snd_type == RESOURCE_TYPE_esnd) {
        decoded_snd = rf.decode_esnd(rgn.snd_id, rgn.snd_type, true);
      } else if (rgn.snd_type == RESOURCE_TYPE_csnd) {
        decoded_snd = rf.decode_csnd(rgn.snd_id, rgn.snd_type, true);
      } else if (rgn.snd_type == RESOURCE_TYPE_snd) {
        decoded_snd = rf.decode_snd(rgn.snd_id, rgn.snd_type, true);
      } else {
        throw logic_error("invalid snd type");
      }
      snd_sample_rate = decoded_snd.sample_rate;
      snd_base_note = decoded_snd.base_note;
      snd_is_mp3 = decoded_snd.is_mp3;

    } catch (const exception& e) {
      fprintf(stderr, "warning: failed to get sound metadata for instrument %" PRId32 " region %hhX-%hhX from snd/csnd/esnd %hu: %s\n",
          id, rgn.key_low, rgn.key_high, rgn.snd_id, e.what());
    }

    string snd_filename = output_filename("", base_filename, snd_res,
        snd_is_mp3 ? ".mp3" : ".wav");
    key_region_dict.emplace("filename", new JSONObject(snd_filename));

    uint8_t base_note;
    if (rgn.base_note && snd_base_note) {
      // TODO: explain this if it works
      base_note = rgn.base_note + snd_base_note - 0x3C;
    } else if (rgn.base_note) {
      base_note = rgn.base_note;
    } else if (snd_base_note) {
      base_note = snd_base_note;
    } else {
      base_note = 0x3C;
    }
    key_region_dict.emplace("base_note", new JSONObject(static_cast<int64_t>(base_note)));

    // if use_sample_rate is NOT set, set a freq_mult to correct for this
    // because smssynth always accounts for different sample rates
    if (!inst.use_sample_rate) {
      key_region_dict.emplace("freq_mult", new JSONObject(22050.0 / static_cast<double>(snd_sample_rate)));
    }

    if (inst.constant_pitch) {
      key_region_dict.emplace("constant_pitch", new JSONObject(static_cast<bool>(true)));
    }

    key_regions_list.emplace_back(new JSONObject(key_region_dict));
  }

  unordered_map<string, shared_ptr<JSONObject>> inst_dict;
  inst_dict.emplace("id", new JSONObject(static_cast<int64_t>(id)));
  inst_dict.emplace("regions", new JSONObject(key_regions_list));
  if (!inst.tremolo_data.empty()) {
    JSONObject::list_type tremolo_json;
    for (uint16_t x : inst.tremolo_data) {
      tremolo_json.emplace_back(make_json_int(x));
    }
    inst_dict.emplace("tremolo_data", new JSONObject(move(tremolo_json)));
  }
  if (!inst.copyright.empty()) {
    inst_dict.emplace("copyright", new JSONObject(inst.copyright));
  }
  if (!inst.author.empty()) {
    inst_dict.emplace("author", new JSONObject(inst.author));
  }
  return shared_ptr<JSONObject>(new JSONObject(move(inst_dict)));
}

shared_ptr<JSONObject> generate_json_for_SONG(
    const string& base_filename,
    ResourceFile& rf,
    const ResourceFile::DecodedSongResource* s) {
  string midi_filename;
  if (s) {
    static const vector<uint32_t> midi_types({
        RESOURCE_TYPE_MIDI, RESOURCE_TYPE_Midi, RESOURCE_TYPE_midi,
        RESOURCE_TYPE_cmid, RESOURCE_TYPE_emid, RESOURCE_TYPE_ecmi});
    for (uint32_t midi_type : midi_types) {
      try {
        const auto& res = rf.get_resource(midi_type, s->midi_id);
        midi_filename = output_filename("", base_filename, res, ".midi");
        break;
      } catch (const exception&) { }
    }
    if (midi_filename.empty()) {
      throw runtime_error("SONG refers to missing MIDI");
    }
  }

  vector<shared_ptr<JSONObject>> instruments;

  // First add the overrides, then add all the other instruments
  if (s) {
    for (const auto& it : s->instrument_overrides) {
      try {
        instruments.emplace_back(generate_json_for_INST(
            base_filename, rf, it.first, rf.decode_INST(it.second)));
      } catch (const exception& e) {
        fprintf(stderr, "warning: failed to add instrument %hu from INST %hu: %s\n",
            it.first, it.second, e.what());
      }
    }
  }
  for (int16_t id : rf.all_resources_of_type(RESOURCE_TYPE_INST)) {
    if (s && s->instrument_overrides.count(id)) {
      continue; // already added this one as a different instrument
    }
    try {
      instruments.emplace_back(generate_json_for_INST(
          base_filename, rf, id, rf.decode_INST(id)));
    } catch (const exception& e) {
      fprintf(stderr, "warning: failed to add instrument %hu: %s\n", id, e.what());
    }
  }

  unordered_map<string, shared_ptr<JSONObject>> base_dict;
  base_dict.emplace("sequence_type", new JSONObject("MIDI"));
  base_dict.emplace("sequence_filename", new JSONObject(midi_filename));
  base_dict.emplace("instruments", new JSONObject(instruments));
  if (s && !s->velocity_override_map.empty()) {
    JSONObject::list_type velocity_override_list;
    for (uint16_t override : s->velocity_override_map) {
      velocity_override_list.emplace_back(make_json_int(override));
    }
    base_dict.emplace("velocity_override_map", new JSONObject(move(
        velocity_override_list)));
  }
  if (s && !s->title.empty()) {
    base_dict.emplace("title", new JSONObject(s->title));
  }
  if (s && !s->performer.empty()) {
    base_dict.emplace("performer", new JSONObject(s->performer));
  }
  if (s && !s->composer.empty()) {
    base_dict.emplace("composer", new JSONObject(s->composer));
  }
  if (s && !s->copyright_date.empty()) {
    base_dict.emplace("copyright_date", new JSONObject(s->copyright_date));
  }
  if (s && !s->copyright_text.empty()) {
    base_dict.emplace("copyright_text", new JSONObject(s->copyright_text));
  }
  if (s && !s->license_contact.empty()) {
    base_dict.emplace("license_contact", new JSONObject(s->license_contact));
  }
  if (s && !s->license_uses.empty()) {
    base_dict.emplace("license_uses", new JSONObject(s->license_uses));
  }
  if (s && !s->license_domain.empty()) {
    base_dict.emplace("license_domain", new JSONObject(s->license_domain));
  }
  if (s && !s->license_term.empty()) {
    base_dict.emplace("license_term", new JSONObject(s->license_term));
  }
  if (s && !s->license_expiration.empty()) {
    base_dict.emplace("license_expiration", new JSONObject(s->license_expiration));
  }
  if (s && !s->note.empty()) {
    base_dict.emplace("note", new JSONObject(s->note));
  }
  if (s && !s->index_number.empty()) {
    base_dict.emplace("index_number", new JSONObject(s->index_number));
  }
  if (s && !s->genre.empty()) {
    base_dict.emplace("genre", new JSONObject(s->genre));
  }
  if (s && !s->subgenre.empty()) {
    base_dict.emplace("subgenre", new JSONObject(s->subgenre));
  }
  if (s && s->tempo_bias && (s->tempo_bias != 16667)) {
    base_dict.emplace("tempo_bias", new JSONObject(static_cast<double>(s->tempo_bias) / 16667.0));
  }
  if (s && s->percussion_instrument) {
    base_dict.emplace("percussion_instrument", new JSONObject(static_cast<int64_t>(s->percussion_instrument)));
  }
  base_dict.emplace("allow_program_change", new JSONObject(static_cast<bool>(s ? s->allow_program_change : true)));

  return shared_ptr<JSONObject>(new JSONObject(base_dict));
}

void write_decoded_INST(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto json = generate_json_for_INST(base_filename, rf, res->id, rf.decode_INST(res));
  write_decoded_file(out_dir, base_filename, res, ".json", json->format());
}

void write_decoded_SONG(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  auto song = rf.decode_SONG(res);
  auto json = generate_json_for_SONG(base_filename, rf, &song);
  write_decoded_file(out_dir, base_filename, res, "_smssynth_env.json", json->format());
}

void write_decoded_Tune(const string& out_dir, const string& base_filename,
    ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {
  string decoded = rf.decode_Tune(res);
  write_decoded_file(out_dir, base_filename, res, ".midi", decoded);
}



typedef void (*resource_decode_fn)(const string& out_dir,
    const string& base_filename, ResourceFile& file,
    shared_ptr<const ResourceFile::Resource> res);

static unordered_map<uint32_t, resource_decode_fn> type_to_decode_fn({
  {RESOURCE_TYPE_actb, write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_ADBS, write_decoded_inline_68k},
  {RESOURCE_TYPE_card, write_decoded_card},
  {RESOURCE_TYPE_cctb, write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_CDEF, write_decoded_inline_68k},
  {RESOURCE_TYPE_cdek, write_decoded_peff},
  {RESOURCE_TYPE_cfrg, write_decoded_cfrg},
  {RESOURCE_TYPE_cicn, write_decoded_cicn},
  {RESOURCE_TYPE_clok, write_decoded_inline_68k},
  {RESOURCE_TYPE_clut, write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_cmid, write_decoded_cmid},
  {RESOURCE_TYPE_CODE, write_decoded_CODE},
  {RESOURCE_TYPE_crsr, write_decoded_crsr},
  {RESOURCE_TYPE_csnd, write_decoded_csnd},
  {RESOURCE_TYPE_CTBL, write_decoded_CTBL},
  {RESOURCE_TYPE_CURS, write_decoded_CURS},
  {RESOURCE_TYPE_dcmp, write_decoded_dcmp},
  {RESOURCE_TYPE_dcod, write_decoded_peff},
  {RESOURCE_TYPE_dctb, write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_DRVR, write_decoded_DRVR},
  {RESOURCE_TYPE_ecmi, write_decoded_ecmi},
  {RESOURCE_TYPE_emid, write_decoded_emid},
  {RESOURCE_TYPE_esnd, write_decoded_esnd},
  {RESOURCE_TYPE_ESnd, write_decoded_ESnd},
  {RESOURCE_TYPE_expt, write_decoded_expt_nsrd},
  {RESOURCE_TYPE_FCMT, write_decoded_STR},
  {RESOURCE_TYPE_fctb, write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_finf, write_decoded_finf},
  {RESOURCE_TYPE_FONT, write_decoded_FONT_NFNT},
  {RESOURCE_TYPE_fovr, write_decoded_peff},
  {RESOURCE_TYPE_icl4, write_decoded_icl4},
  {RESOURCE_TYPE_icl8, write_decoded_icl8},
  {RESOURCE_TYPE_icm4, write_decoded_icm4},
  {RESOURCE_TYPE_icm8, write_decoded_icm8},
  {RESOURCE_TYPE_icmN, write_decoded_icmN},
  {RESOURCE_TYPE_ICNN, write_decoded_ICNN},
  {RESOURCE_TYPE_ICON, write_decoded_ICON},
  {RESOURCE_TYPE_ics4, write_decoded_ics4},
  {RESOURCE_TYPE_ics8, write_decoded_ics8},
  {RESOURCE_TYPE_icsN, write_decoded_icsN},
  {RESOURCE_TYPE_INIT, write_decoded_inline_68k},
  {RESOURCE_TYPE_INST, write_decoded_INST},
  {RESOURCE_TYPE_kcs4, write_decoded_kcs4},
  {RESOURCE_TYPE_kcs8, write_decoded_kcs8},
  {RESOURCE_TYPE_kcsN, write_decoded_kcsN},
  {RESOURCE_TYPE_LDEF, write_decoded_inline_68k},
  {RESOURCE_TYPE_MACS, write_decoded_STR},
  {RESOURCE_TYPE_MBDF, write_decoded_inline_68k},
  {RESOURCE_TYPE_MDEF, write_decoded_inline_68k},
  {RESOURCE_TYPE_minf, write_decoded_TEXT},
  {RESOURCE_TYPE_ncmp, write_decoded_peff},
  {RESOURCE_TYPE_ndmc, write_decoded_peff},
  {RESOURCE_TYPE_ndrv, write_decoded_peff},
  {RESOURCE_TYPE_NFNT, write_decoded_FONT_NFNT},
  {RESOURCE_TYPE_nift, write_decoded_peff},
  {RESOURCE_TYPE_nitt, write_decoded_peff},
  {RESOURCE_TYPE_nlib, write_decoded_peff},
  {RESOURCE_TYPE_nsnd, write_decoded_peff},
  {RESOURCE_TYPE_nsrd, write_decoded_expt_nsrd},
  {RESOURCE_TYPE_ntrb, write_decoded_peff},
  {RESOURCE_TYPE_PACK, write_decoded_inline_68k},
  {RESOURCE_TYPE_PAT , write_decoded_PAT},
  {RESOURCE_TYPE_PATN, write_decoded_PATN},
  {RESOURCE_TYPE_PICT, write_decoded_PICT},
  {RESOURCE_TYPE_pltt, write_decoded_pltt},
  {RESOURCE_TYPE_ppat, write_decoded_ppat},
  {RESOURCE_TYPE_ppct, write_decoded_peff},
  {RESOURCE_TYPE_pptN, write_decoded_pptN},
  {RESOURCE_TYPE_proc, write_decoded_inline_68k},
  {RESOURCE_TYPE_PTCH, write_decoded_inline_68k},
  {RESOURCE_TYPE_ptch, write_decoded_inline_68k},
  {RESOURCE_TYPE_qtcm, write_decoded_peff},
  {RESOURCE_TYPE_ROvN, write_decoded_ROvN},
  {RESOURCE_TYPE_ROvr, write_decoded_inline_68k},
  {RESOURCE_TYPE_scal, write_decoded_peff},
  {RESOURCE_TYPE_SERD, write_decoded_inline_68k},
  {RESOURCE_TYPE_sfvr, write_decoded_peff},
  {RESOURCE_TYPE_SICN, write_decoded_SICN},
  {RESOURCE_TYPE_SIZE, write_decoded_SIZE},
  {RESOURCE_TYPE_SMOD, write_decoded_inline_68k},
  {RESOURCE_TYPE_SMSD, write_decoded_SMSD},
  {RESOURCE_TYPE_snd , write_decoded_snd},
  {RESOURCE_TYPE_snth, write_decoded_inline_68k},
  {RESOURCE_TYPE_SONG, write_decoded_SONG},
  {RESOURCE_TYPE_SOUN, write_decoded_SOUN},
  {RESOURCE_TYPE_STR , write_decoded_STR},
  {RESOURCE_TYPE_STRN, write_decoded_STRN},
  {RESOURCE_TYPE_styl, write_decoded_styl},
  {RESOURCE_TYPE_TEXT, write_decoded_TEXT},
  {RESOURCE_TYPE_TMPL, write_decoded_TMPL},
  {RESOURCE_TYPE_Tune, write_decoded_Tune},
  {RESOURCE_TYPE_vers, write_decoded_vers},
  {RESOURCE_TYPE_wctb, write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_WDEF, write_decoded_inline_68k},
  {RESOURCE_TYPE_XCMD, write_decoded_inline_68k},
  {RESOURCE_TYPE_XFCN, write_decoded_inline_68k},
  {RESOURCE_TYPE_Ysnd, write_decoded_Ysnd},

  // Type aliases (unverified)
  {RESOURCE_TYPE_bstr, write_decoded_STRN},
  {RESOURCE_TYPE_citt, write_decoded_inline_68k},
  {RESOURCE_TYPE_cdev, write_decoded_inline_68k},
  {RESOURCE_TYPE_cmtb, write_decoded_inline_68k},
  {RESOURCE_TYPE_cmuN, write_decoded_inline_68k},
  {RESOURCE_TYPE_code, write_decoded_inline_68k},
  {RESOURCE_TYPE_dem , write_decoded_inline_68k},
  {RESOURCE_TYPE_drvr, write_decoded_DRVR},
  {RESOURCE_TYPE_enet, write_decoded_DRVR},
  {RESOURCE_TYPE_epch, write_decoded_inline_ppc32},
  {RESOURCE_TYPE_gcko, write_decoded_inline_68k},
  {RESOURCE_TYPE_gdef, write_decoded_inline_68k},
  {RESOURCE_TYPE_GDEF, write_decoded_inline_68k},
  {RESOURCE_TYPE_gnld, write_decoded_inline_68k},
  {RESOURCE_TYPE_krnl, write_decoded_inline_ppc32},
  {RESOURCE_TYPE_lmgr, write_decoded_inline_68k},
  {RESOURCE_TYPE_lodr, write_decoded_inline_68k},
  {RESOURCE_TYPE_ltlk, write_decoded_inline_68k},
  {RESOURCE_TYPE_osl , write_decoded_inline_68k},
  {RESOURCE_TYPE_otdr, write_decoded_DRVR},
  {RESOURCE_TYPE_otlm, write_decoded_DRVR},
  {RESOURCE_TYPE_pnll, write_decoded_inline_68k},
  {RESOURCE_TYPE_scod, write_decoded_inline_68k},
  {RESOURCE_TYPE_shal, write_decoded_inline_68k},
  {RESOURCE_TYPE_sift, write_decoded_inline_68k},
  {RESOURCE_TYPE_tdig, write_decoded_inline_68k},
  {RESOURCE_TYPE_tokn, write_decoded_DRVR},
  {RESOURCE_TYPE_wart, write_decoded_inline_68k},
  {RESOURCE_TYPE_vdig, write_decoded_inline_68k_or_peff},
  {RESOURCE_TYPE_pthg, write_decoded_inline_68k_or_peff},
});

static const unordered_map<uint32_t, const char*> type_to_ext({
  {RESOURCE_TYPE_icns, "icns"},
  {RESOURCE_TYPE_MADH, "madh"},
  {RESOURCE_TYPE_MADI, "madi"},
  {RESOURCE_TYPE_MIDI, "midi"},
  {RESOURCE_TYPE_Midi, "midi"},
  {RESOURCE_TYPE_midi, "midi"},
  {RESOURCE_TYPE_PICT, "pict"},
  {RESOURCE_TYPE_sfnt, "ttf"},
});



class ResourceExporter {
public:
  enum class SaveRawBehavior {
    Never = 0,
    IfDecodeFails,
    Always,
  };
  enum class TargetCompressedBehavior {
    Default = 0,
    Target,
    Skip,
  };

  ResourceExporter()
    : use_data_fork(false),
      save_raw(SaveRawBehavior::IfDecodeFails),
      decompress_flags(0),
      target_compressed_behavior(TargetCompressedBehavior::Default),
      skip_templates(false),
      index_format(IndexFormat::ResourceFork),
      parse(parse_resource_fork) { }
  ~ResourceExporter() = default;

  bool use_data_fork;
  SaveRawBehavior save_raw;
  uint64_t decompress_flags;
  unordered_set<uint32_t> target_types;
  unordered_set<uint32_t> skip_types;
  unordered_set<int64_t> target_ids;
  unordered_set<int64_t> skip_ids;
  unordered_set<string> target_names;
  unordered_set<string> skip_names;
  std::vector<std::string> external_preprocessor_command;
  TargetCompressedBehavior target_compressed_behavior;
  bool skip_templates;
private:
  IndexFormat index_format;
  ResourceFile (*parse)(const string&);

public:

  void set_index_format(IndexFormat new_format) {
    this->index_format = new_format;
    if (this->index_format == IndexFormat::ResourceFork) {
      this->parse = parse_resource_fork;
    } else if (this->index_format == IndexFormat::Mohawk) {
      this->parse = parse_mohawk;
    } else if (this->index_format == IndexFormat::HIRF) {
      this->parse = parse_hirf;
    } else if (this->index_format == IndexFormat::DCData) {
      this->parse = parse_dc_data;
    } else {
      throw logic_error("invalid index format");
    }
  }

  bool export_resource(const string& base_filename, const string& out_dir,
      ResourceFile& rf, shared_ptr<const ResourceFile::Resource> res) {

    bool decompression_failed = res->flags & ResourceFlag::FLAG_DECOMPRESSION_FAILED;
    bool is_compressed = res->flags & ResourceFlag::FLAG_COMPRESSED;
    bool was_compressed = res->flags & ResourceFlag::FLAG_DECOMPRESSED;
    if (decompression_failed || is_compressed) {
      auto type_str = string_for_resource_type(res->type);
      fprintf(stderr,
          decompression_failed
            ? "warning: failed to decompress resource %s:%d; saving raw compressed data\n"
            : "note: resource %s:%d is compressed; saving raw compressed data\n",
          type_str.c_str(), res->id);
    }
    if ((this->target_compressed_behavior == TargetCompressedBehavior::Target) &&
        !(is_compressed || was_compressed || decompression_failed)) {
      return false;
    } else if ((this->target_compressed_behavior == TargetCompressedBehavior::Skip) &&
        (is_compressed || was_compressed || decompression_failed)) {
      return false;
    }

    bool write_raw = (this->save_raw == SaveRawBehavior::Always);
    ResourceFile::Resource preprocessed_res;
    shared_ptr<const ResourceFile::Resource> res_to_decode = res;

    // Run external preprocessor if possible. The resource could still be
    // compressed if --skip-decompression was used or if decompression failed;
    // in these cases it doesn't make sense to run the external preprocessor.
    if (!is_compressed && !this->external_preprocessor_command.empty()) {
      auto result = run_process(this->external_preprocessor_command, &res->data, false);
      if (result.exit_status != 0) {
        fprintf(stderr, "\
warning: external preprocessor failed with exit status 0x%x\n\
\n\
stdout (%zu bytes):\n\
%s\n\
\n\
stderr (%zu bytes):\n\
%s\n\
\n", result.exit_status, result.stdout_contents.size(), result.stdout_contents.c_str(), result.stderr_contents.size(), result.stderr_contents.c_str());
      } else {
        fprintf(stderr, "note: external preprocessor succeeded and returned %zu bytes\n",
            result.stdout_contents.size());
        res_to_decode.reset(new ResourceFile::Resource(
            res->type, res->id, res->flags, res->name, move(result.stdout_contents)));
      }
    }

    // Decode if possible. If decompression failed, don't bother trying to
    // decode the resource.
    resource_decode_fn decode_fn = type_to_decode_fn[res_to_decode->type];
    bool decoded = false;
    if (!is_compressed && decode_fn) {
      try {
        decode_fn(out_dir, base_filename, rf, res_to_decode);
        decoded = true;
      } catch (const exception& e) {
        fprintf(stderr, "warning: failed to decode resource: %s\n", e.what());
      }
    }
    // If there's no built-in decoder, try to use a TMPL resource to decode it
    if (!is_compressed && !decoded && !this->skip_templates) {
      // It appears ResEdit looks these up by name
      string tmpl_name = raw_string_for_resource_type(res_to_decode->type);

      // If there's no TMPL, just silently fail this step. If there's a TMPL but
      // it's corrupt or doesn't decode the data correctly, fail with a warning.
      shared_ptr<const ResourceFile::Resource> tmpl_res;
      try {
        tmpl_res = rf.get_resource(RESOURCE_TYPE_TMPL, tmpl_name.c_str());
      } catch (const out_of_range& e) { }

      if (tmpl_res.get()) {
        try {
          string result = string_printf("# (decoded with TMPL %hd)\n", tmpl_res->id);
          result += rf.disassemble_from_template(
              res->data.data(), res->data.size(), rf.decode_TMPL(tmpl_res));
          write_decoded_file(out_dir, base_filename, res_to_decode, ".txt", result);
          decoded = true;
        } catch (const exception& e) {
          fprintf(stderr, "warning: failed to decode resource with template %hd: %s\n", tmpl_res->id, e.what());
        }
      }
    }
    // If there's no built-in decoder and no TMPL in the file, try using a
    // system template
    if (!is_compressed && !decoded && !this->skip_templates) {
      const ResourceFile::TemplateEntryList& tmpl = get_system_template(res_to_decode->type);
      if (!tmpl.empty()) {
        try {
          string result = rf.disassemble_from_template(
              res->data.data(), res->data.size(), tmpl);
          write_decoded_file(out_dir, base_filename, res_to_decode, ".txt", result);
          decoded = true;
        } catch (const exception& e) {
          fprintf(stderr, "warning: failed to decode resource with system template: %s\n", e.what());
        }
      }
    }

    if (!decoded && this->save_raw == SaveRawBehavior::IfDecodeFails) {
      write_raw = true;
    }

    if (write_raw) {
      const char* out_ext = "bin";
      try {
        out_ext = type_to_ext.at(res_to_decode->type);
      } catch (const out_of_range&) { }

      string out_filename_after = string_printf(".%s", out_ext);
      string out_filename = output_filename(out_dir, base_filename, res_to_decode, out_filename_after);

      try {
        // Hack: PICT resources, when saved to disk, should be prepended with a
        // 512-byte unused header
        if (res_to_decode->type == RESOURCE_TYPE_PICT) {
          static const string pict_header(512, 0);
          auto f = fopen_unique(out_filename, "wb");
          fwritex(f.get(), pict_header);
          fwritex(f.get(), res_to_decode->data);
        } else {
          save_file(out_filename, res_to_decode->data);
        }
        fprintf(stderr, "... %s\n", out_filename.c_str());
      } catch (const exception& e) {
        fprintf(stderr, "warning: failed to save raw data: %s\n", e.what());
      }
    }
    return decoded || write_raw;
  }

  bool disassemble_file(const string& filename, const string& out_dir) {
    // open resource fork if present
    string resource_fork_filename;
    if (this->use_data_fork) {
      resource_fork_filename = filename;
    } else if (isfile(filename + "/..namedfork/rsrc")) {
      resource_fork_filename = filename + "/..namedfork/rsrc";
    } else if (isfile(filename + "/rsrc")) {
      resource_fork_filename = filename + "/rsrc";
    } else {
      fprintf(stderr, "failed on %s: no resource fork present\n", filename.c_str());
      return false;
    }

    // compute the base filename
    size_t last_slash_pos = filename.rfind('/');
    string base_filename = (last_slash_pos == string::npos) ? filename :
        filename.substr(last_slash_pos + 1);

    // get the resources from the file
    unique_ptr<ResourceFile> rf;
    try {
      rf.reset(new ResourceFile(this->parse(load_file(resource_fork_filename))));
    } catch (const cannot_open_file&) {
      fprintf(stderr, "failed on %s: cannot open file\n", filename.c_str());
      return false;
    } catch (const io_error& e) {
      fprintf(stderr, "failed on %s: cannot read data\n", filename.c_str());
      return false;
    } catch (const runtime_error& e) {
      fprintf(stderr, "failed on %s: corrupt resource index (%s)\n", filename.c_str(), e.what());
      return false;
    } catch (const out_of_range& e) {
      fprintf(stderr, "failed on %s: corrupt resource index\n", filename.c_str());
      return false;
    }

    bool ret = false;
    try {
      auto resources = rf->all_resources();

      bool has_INST = false;
      for (const auto& it : resources) {
        if ((!this->target_types.empty() && !this->target_types.count(it.first)) ||
            this->skip_types.count(it.first)) {
          continue;
        }
        if ((!this->target_ids.empty() && !this->target_ids.count(it.second)) ||
            this->skip_ids.count(it.second)) {
          continue;
        }
        const auto& res = rf->get_resource(it.first, it.second, this->decompress_flags);
        if ((!this->target_names.empty() && !this->target_names.count(res->name)) ||
            this->skip_names.count(res->name)) {
          continue;
        }
        if (it.first == RESOURCE_TYPE_INST) {
          has_INST = true;
        }
        ret |= this->export_resource(base_filename.c_str(), out_dir.c_str(), *rf, res);
      }

      // special case: if we disassembled any INSTs and the save-raw behavior is
      // not Never, generate an smssynth template file from all the INSTs
      if (has_INST && (this->save_raw != SaveRawBehavior::Never)) {
        string json_filename;
        if (out_dir.empty()) {
          json_filename = string_printf("%s_smssynth_env_template.json", base_filename.c_str());
        } else {
          json_filename = string_printf("%s/%s_smssynth_env_template.json",
              out_dir.c_str(), base_filename.c_str());
        }

        try {
          auto json = generate_json_for_SONG(base_filename, *rf, nullptr);
          save_file(json_filename.c_str(), json->format());
          fprintf(stderr, "... %s\n", json_filename.c_str());

        } catch (const exception& e) {
          fprintf(stderr, "failed to write smssynth env template %s: %s\n",
              json_filename.c_str(), e.what());
        }
      }

    } catch (const exception& e) {
      fprintf(stderr, "failed on %s: %s\n", filename.c_str(), e.what());
    }
    return ret;
  }

  bool disassemble_path(const string& filename, const string& out_dir) {
    if (isdir(filename)) {
      fprintf(stderr, ">>> %s (directory)\n", filename.c_str());

      unordered_set<string> items;
      try {
        items = list_directory(filename);
      } catch (const runtime_error& e) {
        fprintf(stderr, "warning: can\'t list directory: %s\n", e.what());
        return false;
      }

      vector<string> sorted_items;
      sorted_items.insert(sorted_items.end(), items.begin(), items.end());
      sort(sorted_items.begin(), sorted_items.end());

      size_t last_slash_pos = filename.rfind('/');
      string base_filename = (last_slash_pos == string::npos) ? filename :
          filename.substr(last_slash_pos + 1);

      string sub_out_dir = out_dir + "/" + base_filename;
      mkdir(sub_out_dir.c_str(), 0777);

      bool ret = false;
      for (const string& item : sorted_items) {
        ret |= this->disassemble_path(filename + "/" + item, sub_out_dir);
      }
      if (!ret) {
        rmdir(sub_out_dir.c_str());
      }
      return ret;

    } else {
      fprintf(stderr, ">>> %s\n", filename.c_str());
      return this->disassemble_file(filename, out_dir);
    }
  }
};



void print_usage() {
  fprintf(stderr, "\
Fuzziqer Software Classic Mac OS resource fork disassembler\n\
\n\
Usage: resource_dasm [options] input_filename [output_directory]\n\
\n\
If input_filename is a directory, resource_dasm decodes all resources in all\n\
files and subdirectories within that directory, producing a parallel directory\n\
structure in the output directory.\n\
\n\
If output_directory is not given, the directory <input_filename>.out is created\n\
and the output is written there.\n\
\n\
Input options:\n\
  --index-format=FORMAT\n\
      Parse the input as a resource index in this format. Valid FORMATs are:\n\
        resource-fork (default) - Mac OS resource fork\n\
        mohawk - Mohawk archive\n\
        hirf - Beatnik HIRF archive (also known as IREZ, HSB, or RMF)\n\
        dc-data - DC Data file\n\
      If the index format is not resource-fork, --data-fork is implied.\n\
  --target-type=TYPE\n\
      Only extract resources of this type (can be given multiple times).\n\
  --target-id=ID\n\
      Only extract resources with this ID (can be given multiple times).\n\
  --target=TYPE[:ID]\n\
      Short form for --target-type=TYPE --target-id=ID.\n\
  --target-name=NAME\n\
      Only extract resources with this name (can be given multiple times).\n\
  --target-compressed\n\
      Only export resources that are compressed in the source file.\n\
  --skip-type=TYPE\n\
      Don\'t extract resources of this type (can be given multiple times).\n\
  --skip-id=TYPE\n\
      Don\'t extract resources with this ID (can be given multiple times).\n\
  --skip-name=NAME\n\
      Don\'t extract resources with this name (can be given multiple times).\n\
  --skip-compressed\n\
      Don\'t extract resources that are compressed in the source file.\n\
  --data-fork\n\
      Disassemble the file\'s data fork as if it were the resource fork.\n\
  --decode-single-resource=TYPE[:ID[:FLAGS[:NAME]]]\n\
      Decode the input file\'s data fork as if it\'s a single resource of the\n\
      given type. This can be used to decode raw already-exported resources.\n\
      It is usually sufficient to give only a type, as in --decode-type=dcmp.\n\
      Some resources may decode differently depending on their IDs; for these,\n\
      pass an ID as well, as in --decode-type=CODE:0 to decode an import table\n\
      (by default, the resource is treated as if its ID is 1). If the input\n\
      data is compressed, set FLAGS to 1. Currently NAME is unused by any\n\
      decoder, but there may be decoders in the future that depend on the\n\
      resource's name. This option disables all of the above options.\n\
  --disassemble-68k, --disassemble-ppc, --disassemble-pef, --disassemble-dol\n\
      Disassemble the input file as raw 68K code, raw PowerPC code, or a PEFF\n\
      (Preferred Executable Format) or DOL (Nintendo GameCube) executable. If\n\
      no input filename is given in this mode, the data from stdin is\n\
      disassembled instead. If no output filename is given, the disassembly is\n\
      written to stdout. Note that CODE resources have a small header before\n\
      the actual code; to disassemble an exported CODE resource, use\n\
      --decode-single-resource=CODE instead.\n\
  --start-address=ADDR\n\
      When disassembling code with one of the above options, use ADDR as the\n\
      start address (instead of zero).\n\
  --label=ADDR[:NAME]\n\
      Add this label into the disassembly output. If NAME is not given, use\n\
      label<ADDR> as the label name. May be given multiple times.\n\
  --parse-data\n\
      When disassembling code or a single resource with one of the above\n\
      options, treat the input data as a hexadecimal string instead of raw\n\
      (binary) machine code. This is useful when pasting data into a terminal\n\
      from a hex dump or editor.\n\
\n\
Decompression options:\n\
  --skip-decompression\n\
      Don\'t attempt to decompress compressed resources. If decompression fails\n\
      or is disabled via this option, the rest of the decoding steps do not\n\
      run, and the raw compressed data is exported instead.\n\
  --debug-decompression\n\
      Show log output when running resource decompressors.\n\
  --trace-decompression\n\
      Show memory and CPU state when running resource decompressors. This slows\n\
      them down considerably and is generally only used for finding bugs and\n\
      missing features in the emulated CPUs.\n\
  --skip-file-dcmp\n\
      Don\'t attempt to use any 68K decompressors from the input file.\n\
  --skip-file-ncmp\n\
      Don\'t attempt to use any PEFF decompressors from the input file.\n\
  --skip-internal-dcmp\n\
      Don\'t attempt to use any internal default decompressors.\n\
  --skip-system-dcmp\n\
      Don\'t attempt to use the default 68K decompressors.\n\
  --skip-system-ncmp\n\
      Don\'t attempt to use the default PEFF decompressors.\n\
\n\
Decoding options:\n\
  --copy-handler=TYP1,TYP2\n\
      Decode TYP2 resources as if they were TYP1.\n\
  --external-preprocessor=COMMAND\n\
      After decompression, but before decoding resource data, pass it through\n\
      this external program. The resource data will be passed to the specified\n\
      command via stdin, and the command\'s output on stdout will be treated as\n\
      the resource data to decode. This can be used to mostly-transparently\n\
      decompress some custom compression formats.\n\
  --skip-decode\n\
      Don\'t use built-in decoders to convert resources to modern formats.\n\
      Implies --skip-templates.\n\
  --skip-external-decoders\n\
      Only use internal decoders. Currently, this only disables the use of\n\
      picttoppm for decoding PICT resources.\n\
  --skip-templates\n\
      Don\'t attempt to use TMPL resources to convert resources to text files.\n\
\n\
Output options:\n\
  --save-raw=no\n\
      Don\'t save any raw files; only save decoded resources.\n\
  --save-raw=if-decode-fails\n\
      Only save a raw file if the resource can\'t be converted to a modern\n\
      format or a text file (via a template). This is the default behavior.\n\
  --save-raw=yes\n\
      Save raw files even for resources that are successfully decoded.\n\
\n");
}

static uint32_t parse_cli_type(const char* str) {
  size_t type_len = strlen(str);
  if (type_len == 0) {
    return 0x20202020;
  } else if (type_len == 1) {
    return str[0] << 24 | 0x00202020;
  } else if (type_len == 2) {
    return (str[0] << 24) | (str[1] << 16) | 0x00002020;
  } else if (type_len == 3) {
    return (str[0] << 24) | (str[1] << 16) | (str[2] << 8) | 0x00000020;
  } else if (type_len == 4) {
    return (str[0] << 24) | (str[1] << 16) | (str[2] << 8) | str[3];
  } else {
    throw invalid_argument("resource type must be between 0 and 4 bytes long");
  }
}

int main(int argc, char* argv[]) {
  signal(SIGPIPE, SIG_IGN);

  ResourceExporter exporter;
  string filename;
  string out_dir;
  ResourceFile::Resource single_resource;
  bool disassemble_68k = false;
  bool disassemble_ppc = false;
  bool disassemble_pef = false;
  bool disassemble_dol = false;
  bool parse_data = false;
  uint32_t disassembly_start_address = 0;
  multimap<uint32_t, string> disassembly_labels;
  for (int x = 1; x < argc; x++) {
    if (argv[x][0] == '-') {
      if (!strcmp(argv[x], "--index-format=resource-fork")) {
        exporter.set_index_format(IndexFormat::ResourceFork);
      } else if (!strcmp(argv[x], "--index-format=mohawk")) {
        exporter.set_index_format(IndexFormat::Mohawk);
        exporter.use_data_fork = true;
      } else if (!strcmp(argv[x], "--index-format=hirf")) {
        exporter.set_index_format(IndexFormat::HIRF);
        exporter.use_data_fork = true;
      } else if (!strcmp(argv[x], "--index-format=dc-data")) {
        exporter.set_index_format(IndexFormat::DCData);
        exporter.use_data_fork = true;

      } else if (!strncmp(argv[x], "--decode-single-resource=", 25)) {
        auto tokens = split(&argv[x][25], ':');
        if (tokens.size() == 0) {
          throw logic_error("split() returned zero tokens");
        }
        single_resource.type = parse_cli_type(tokens[0].c_str());
        single_resource.id = 1;
        single_resource.flags = 0;
        single_resource.name = "";
        if (tokens.size() > 1) {
          single_resource.id = stol(tokens[1], nullptr, 0);
        }
        if (tokens.size() > 2) {
          single_resource.flags = stoul(tokens[2], nullptr, 0);
        }
        if (tokens.size() > 3) {
          vector<string> name_tokens(make_move_iterator(tokens.begin() + 3), make_move_iterator(tokens.end()));
          single_resource.name = join(name_tokens, ":");
        }

      } else if (!strcmp(argv[x], "--disassemble-68k")) {
        disassemble_68k = true;
      } else if (!strcmp(argv[x], "--disassemble-ppc")) {
        disassemble_ppc = true;
      } else if (!strcmp(argv[x], "--disassemble-pef")) {
        disassemble_pef = true;
      } else if (!strcmp(argv[x], "--disassemble-dol")) {
        disassemble_dol = true;

      } else if (!strncmp(argv[x], "--start-address=", 16)) {
        disassembly_start_address = strtoul(&argv[x][16], nullptr, 16);
      } else if (!strncmp(argv[x], "--label=", 8)) {
        string arg(&argv[x][8]);
        string addr_str, name_str;
        size_t colon_pos = arg.find(':');
        if (colon_pos == string::npos) {
          addr_str = arg;
        } else {
          addr_str = arg.substr(0, colon_pos);
          name_str = arg.substr(colon_pos + 1);
        }
        uint32_t addr = stoul(addr_str, nullptr, 16);
        if (name_str.empty()) {
          name_str = string_printf("label%08" PRIX32, addr);
        }
        disassembly_labels.emplace(addr, name_str);

      } else if (!strcmp(argv[x], "--parse-data")) {
        parse_data = true;

      } else if (!strncmp(argv[x], "--copy-handler=", 15)) {
        if (strlen(argv[x]) != 24 || argv[x][19] != ',') {
          fprintf(stderr, "incorrect format for --copy-handler: %s (types must be 4 bytes each)\n", argv[x]);
          return 1;
        }
        uint32_t from_type = *reinterpret_cast<const be_uint32_t*>(&argv[x][15]);
        uint32_t to_type = *reinterpret_cast<const be_uint32_t*>(&argv[x][20]);
        if (!type_to_decode_fn.count(from_type)) {
          fprintf(stderr, "no handler exists for type %.4s\n", (const char*)&from_type);
          return 1;
        }
        fprintf(stderr, "note: treating %.4s resources as %.4s\n", (const char*)&to_type,
            (const char*)&from_type);
        type_to_decode_fn[to_type] = type_to_decode_fn[from_type];

      } else if (!strcmp(argv[x], "--skip-external-decoders")) {
        type_to_decode_fn[RESOURCE_TYPE_PICT] = write_decoded_PICT_internal;

      } else if (!strncmp(argv[x], "--external-preprocessor=", 24)) {
        exporter.external_preprocessor_command = split(&argv[x][24], ' ');

      } else if (!strncmp(argv[x], "--target-type=", 14)) {
        exporter.target_types.emplace(parse_cli_type(&argv[x][14]));
      } else if (!strncmp(argv[x], "--skip-type=", 12)) {
        exporter.skip_types.emplace(parse_cli_type(&argv[x][12]));

      } else if (!strncmp(argv[x], "--target-id=", 12)) {
        exporter.target_ids.emplace(strtoll(&argv[x][12], nullptr, 0));
      } else if (!strncmp(argv[x], "--skip-id=", 10)) {
        exporter.skip_ids.emplace(strtoll(&argv[x][10], nullptr, 0));

      } else if (!strncmp(argv[x], "--target=", 9)) {
        auto tokens = split(&argv[x][9], ':');
        exporter.target_types.emplace(parse_cli_type(tokens.at(0).c_str()));
        if (tokens.size() == 2) {
          exporter.target_ids.emplace(stoll(tokens.at(1), nullptr, 0));
        } else if (tokens.size() > 2) {
          fprintf(stderr, "invalid target: %s\n", &argv[x][9]);
          return 1;
        }

      } else if (!strncmp(argv[x], "--target-name=", 14)) {
        exporter.target_names.emplace(&argv[x][14]);
      } else if (!strncmp(argv[x], "--skip-name=", 12)) {
        exporter.skip_names.emplace(&argv[x][12]);

      } else if (!strcmp(argv[x], "--skip-decode")) {
        type_to_decode_fn.clear();
        exporter.skip_templates = true;

      } else if (!strcmp(argv[x], "--save-raw=no")) {
        exporter.save_raw = ResourceExporter::SaveRawBehavior::Never;

      } else if (!strcmp(argv[x], "--save-raw=if-decode-fails")) {
        exporter.save_raw = ResourceExporter::SaveRawBehavior::IfDecodeFails;

      } else if (!strcmp(argv[x], "--save-raw=yes")) {
        exporter.save_raw = ResourceExporter::SaveRawBehavior::Always;

      } else if (!strcmp(argv[x], "--data-fork")) {
        exporter.use_data_fork = true;

      } else if (!strcmp(argv[x], "--target-compressed")) {
        exporter.target_compressed_behavior = ResourceExporter::TargetCompressedBehavior::Target;
      } else if (!strcmp(argv[x], "--skip-compressed")) {
        exporter.target_compressed_behavior = ResourceExporter::TargetCompressedBehavior::Skip;

      } else if (!strcmp(argv[x], "--skip-templates")) {
        exporter.skip_templates = true;

      } else if (!strcmp(argv[x], "--skip-decompression")) {
        exporter.decompress_flags |= DecompressionFlag::DISABLED;

      } else if (!strcmp(argv[x], "--debug-decompression")) {
        exporter.decompress_flags |= DecompressionFlag::VERBOSE;
      } else if (!strcmp(argv[x], "--trace-decompression")) {
        exporter.decompress_flags |= DecompressionFlag::TRACE;

      } else if (!strcmp(argv[x], "--skip-file-dcmp")) {
        exporter.decompress_flags |= DecompressionFlag::SKIP_FILE_DCMP;
      } else if (!strcmp(argv[x], "--skip-file-ncmp")) {
        exporter.decompress_flags |= DecompressionFlag::SKIP_FILE_NCMP;
      } else if (!strcmp(argv[x], "--skip-internal-dcmp")) {
        exporter.decompress_flags |= DecompressionFlag::SKIP_INTERNAL;
      } else if (!strcmp(argv[x], "--skip-system-dcmp")) {
        exporter.decompress_flags |= DecompressionFlag::SKIP_SYSTEM_DCMP;
      } else if (!strcmp(argv[x], "--skip-system-ncmp")) {
        exporter.decompress_flags |= DecompressionFlag::SKIP_SYSTEM_NCMP;

      } else {
        fprintf(stderr, "unknown option: %s\n", argv[x]);
        return 1;
      }
    } else {
      if (filename.empty()) {
        filename = argv[x];
      } else if (out_dir.empty()) {
        out_dir = argv[x];
      } else {
        print_usage();
        return 1;
      }
    }
  }

  if (disassemble_ppc || disassemble_68k || disassemble_pef || disassemble_dol) {
    string data;
    if (filename.empty()) {
      data = read_all(stdin);
    } else {
      data = load_file(filename);
    }
    if (parse_data) {
      data = parse_data_string(data);
    }

    if (disassemble_pef) {
      PEFFFile f(filename.c_str(), data);
      if (!out_dir.empty()) {
        auto out = fopen_unique(out_dir, "wt");
        f.print(out.get(), &disassembly_labels);
      } else {
        f.print(stdout, &disassembly_labels);
      }

    } else if (disassemble_dol) {
      DOLFile f(filename.c_str(), data);
      if (!out_dir.empty()) {
        auto out = fopen_unique(out_dir, "wt");
        f.print(out.get(), &disassembly_labels);
      } else {
        f.print(stdout, &disassembly_labels);
      }

    } else {
      auto disassemble = disassemble_68k ? M68KEmulator::disassemble : PPC32Emulator::disassemble;
      string disassembly = disassemble(data.data(), data.size(),
          disassembly_start_address, &disassembly_labels);
      if (!out_dir.empty()) {
        auto out = fopen_unique(out_dir, "wt");
        fwritex(out.get(), disassembly);
      } else {
        fwritex(stdout, disassembly);
      }
    }
    return 0;
  }

  if (filename.empty()) {
    print_usage();
    return 1;
  }

  if (single_resource.type) {
    exporter.save_raw = ResourceExporter::SaveRawBehavior::Never;
    exporter.target_types.clear();
    exporter.target_ids.clear();
    exporter.target_names.clear();
    exporter.target_compressed_behavior = ResourceExporter::TargetCompressedBehavior::Default;
    exporter.use_data_fork = false;

    single_resource.data = load_file(filename);
    if (parse_data) {
      single_resource.data = parse_data_string(single_resource.data);
    }

    uint32_t type = single_resource.type;
    int16_t id = single_resource.id;
    ResourceFile rf;
    rf.add(move(single_resource));

    size_t last_slash_pos = filename.rfind('/');
    string base_filename = (last_slash_pos == string::npos) ? filename :
        filename.substr(last_slash_pos + 1);

    const auto& res = rf.get_resource(type, id, exporter.decompress_flags);
    return exporter.export_resource(filename, "", rf, res) ? 0 : 3;

  } else {
    if (out_dir.empty()) {
      out_dir = filename + ".out";
    }
    mkdir(out_dir.c_str(), 0777);
    if (!exporter.disassemble_path(filename, out_dir)) {
      return 3;
    } else {
      return 0;
    }
  }
}
