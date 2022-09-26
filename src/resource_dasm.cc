#include <inttypes.h>
#include <math.h>
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
#include <unordered_set>
#include <vector>

#include "TextCodecs.hh"
#include "Emulators/M68KEmulator.hh"
#include "Emulators/PPC32Emulator.hh"
#include "Emulators/X86Emulator.hh"
#include "ExecutableFormats/DOLFile.hh"
#include "ExecutableFormats/ELFFile.hh"
#include "ExecutableFormats/PEFFile.hh"
#include "ExecutableFormats/PEFile.hh"
#include "ExecutableFormats/RELFile.hh"
#include "IndexFormats/Formats.hh"
#include "Lookups.hh"
#include "ResourceCompression.hh"
#include "ResourceFile.hh"
#include "SystemTemplates.hh"

using namespace std;



static const string RESOURCE_FORK_FILENAME_SUFFIX = "/..namedfork/rsrc";
static const string RESOURCE_FORK_FILENAME_SHORT_SUFFIX = "/rsrc";

static constexpr char FILENAME_FORMAT_STANDARD[] = "%f_%t_%i%n";
static constexpr char FILENAME_FORMAT_STANDARD_DIRS[] = "%f/%t_%i%n";
static constexpr char FILENAME_FORMAT_STANDARD_TYPE_DIRS[] = "%f/%t/%i%ns";
static constexpr char FILENAME_FORMAT_TYPE_FIRST[] = "%t/%f_%i%n";
static constexpr char FILENAME_FORMAT_TYPE_FIRST_DIRS[] = "%t/%f/%i%n";



class ResourceExporter {
private:
  void ensure_directories_exist(const string& filename) {
    vector<string> tokens = split(filename, '/');
    if (tokens.empty()) {
      return;
    }
    tokens.pop_back();

    if (tokens.empty()) {
      return;
    }

    string dir;
    bool first_token = true;
    for (const string& token : tokens) {
      if (!first_token) {
        dir.push_back('/');
      } else {
        first_token = false;
      }
      dir += token;
      // dir can be / if filename is an absolute path; just skip it
      if (dir != "/" && !isdir(dir)) {
        mkdir(dir.c_str(), 0777);
      }
    }
  }
  
  string output_filename(
      const string& base_filename,
      const int16_t* res_id,
      const string& res_type,
      const string& res_name,
      uint16_t res_flags,
      const std::string& after) {
    if (base_filename.empty()) {
      return out_dir;
    }

    string base_out_dir_str = this->base_out_dir;
    if (!base_out_dir_str.empty()) {
      base_out_dir_str += '/';
    }
    string filename_str;
    if (!this->out_dir.empty()) {
      filename_str += this->out_dir;
      filename_str += '/';
    }
    filename_str += base_filename;
    
    string  result = base_out_dir_str;
    bool    saw_percent = false;
    for (char ch : this->filename_format) {
      if (ch == '%' && !saw_percent) {
        saw_percent = true;
      } else if (saw_percent) {
        saw_percent = false;
        switch (ch) {
          case '%':
            result += '%';
            break;
          
          case 'a':
            result += res_flags & FLAG_COMPRESSED ? 'c' : '-';
            result += res_flags & FLAG_PRELOAD ? 'p' : '-';
            result += res_flags & FLAG_PROTECTED ? 'r' : '-';
            result += res_flags & FLAG_LOCKED ? 'l' : '-';
            result += res_flags & FLAG_PURGEABLE ? 'u' : '-';
            result += res_flags & FLAG_LOAD_IN_SYSTEM_HEAP ? 's' : '-';
            break;
          
          case 'f':
            result += filename_str;
            break;
          
          case 'i':
            if (res_id) {
              result += string_printf("%d", *res_id);
            }
            break;
          
          case 'n':
            result += res_name;
            break;
          
          case 't':
            result += res_type;
            break;
          
          default:
            throw runtime_error(string_printf("unimplemented escape '%c' in filename format", ch));
        }
      } else {
        result += ch;
      }
    }
    result += after;
    
    return result;
  }

  string output_filename(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res,
      const std::string& after) {
    if (base_filename.empty()) {
      return out_dir;
    }
    
    string type_str = string_for_resource_type(res->type, /*for_filename=*/ true);

    // If the type ends with spaces (e.g. 'snd '), trim them off
    strip_trailing_whitespace(type_str);

    string name_token;
    if (!res->name.empty()) {
      name_token = '_' + decode_mac_roman(res->name, /*for_filename=*/ true);
    }
    
    return output_filename(base_filename, &res->id, type_str, name_token, res->flags, after);
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



  void write_decoded_data(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res,
      const string& after,
      const string& data) {
    string filename = this->output_filename(base_filename, res, after);
    this->ensure_directories_exist(filename);
    save_file(filename, data);
    fprintf(stderr, "... %s\n", filename.c_str());
  }

  void write_decoded_data(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res,
      const string& after,
      const Image& img) {
    string filename = this->output_filename(base_filename, res, after);
    this->ensure_directories_exist(filename);
    img.save(filename.c_str(), Image::Format::WINDOWS_BITMAP);
    fprintf(stderr, "... %s\n", filename.c_str());
  }

  void write_decoded_TMPL(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    using Entry = ResourceFile::TemplateEntry;
    using Type = Entry::Type;
    using Format = Entry::Format;

    auto decoded = this->current_rf->decode_TMPL(res);

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
                tokens.emplace_back(string_printf("%" PRId64 " = %s", case_name.first, case_name.second.c_str()));
              }
              case_names_str = " (" + join(tokens, ", ") + ")";
            }
            add_line(prefix + string_printf("%hu-byte %s (%s)", width, type_str, format_str) + case_names_str);
            break;
          }
          case Type::ALIGNMENT:
            add_line(prefix + string_printf("(align to %hhu-byte boundary)", entry->end_alignment));
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

    this->write_decoded_data(base_filename, res, ".txt", join(lines, "\n"));
  }

  void write_decoded_CURS(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_CURS(res);
    string after = string_printf("_%hu_%hu.bmp", decoded.hotspot_x, decoded.hotspot_y);
    this->write_decoded_data(base_filename, res, after, decoded.bitmap);
  }

  void write_decoded_crsr(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_crsr(res);
    string bitmap_after = string_printf("_%hu_%hu_bitmap.bmp", decoded.hotspot_x, decoded.hotspot_y);
    string after = string_printf("_%hu_%hu.bmp", decoded.hotspot_x, decoded.hotspot_y);
    this->write_decoded_data(base_filename, res, bitmap_after, decoded.bitmap);
    this->write_decoded_data(base_filename, res, after, decoded.image);
  }

  void write_decoded_ppat(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_ppat(res);

    Image tiled = tile_image(decoded.pattern, 8, 8);
    this->write_decoded_data(base_filename, res, ".bmp", decoded.pattern);
    this->write_decoded_data(base_filename, res, "_tiled.bmp", tiled);

    tiled = tile_image(decoded.monochrome_pattern, 8, 8);
    this->write_decoded_data(base_filename, res, "_bitmap.bmp", decoded.monochrome_pattern);
    this->write_decoded_data(base_filename, res, "_bitmap_tiled.bmp", tiled);
  }

  void write_decoded_pptN(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_pptN(res);

    for (size_t x = 0; x < decoded.size(); x++) {
      string after = string_printf("_%zu.bmp", x);
      this->write_decoded_data(base_filename, res, after, decoded[x].pattern);

      Image tiled = tile_image(decoded[x].pattern, 8, 8);
      after = string_printf("_%zu_tiled.bmp", x);
      this->write_decoded_data(base_filename, res, after, tiled);

      after = string_printf("_%zu_bitmap.bmp", x);
      this->write_decoded_data(base_filename, res, after, decoded[x].monochrome_pattern);

      tiled = tile_image(decoded[x].monochrome_pattern, 8, 8);
      after = string_printf("_%zu_bitmap_tiled.bmp", x);
      this->write_decoded_data(base_filename, res, after, tiled);
    }
  }

  void write_decoded_data(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res,
      const vector<ColorTableEntry>& decoded,
      const unordered_map<uint16_t, string>* index_names = nullptr) {
    if (decoded.size() == 0) {
      Image img(122, 16, false);
      img.clear(0x00, 0x00, 0x00);
      img.draw_text(4, 4, 0xFFFFFFFF, 0x00000000, "No colors in table");
      this->write_decoded_data(base_filename, res, ".bmp", img);

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
      this->write_decoded_data(base_filename, res, ".bmp", img);
    }
    
    // Also store the colors as a Photoshop .act file
    {
      // https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/#50577411_pgfId-1070626
      //
      //    There is no version number written in the file. The file is 768 or 772
      //    bytes long and contains 256 RGB colors. The first color in the table
      //    is index zero. There are three bytes per color in the order red, green,
      //    blue. If the file is 772 bytes long there are 4 additional bytes remaining.
      //    Two bytes for the number of colors to use. Two bytes for the color index
      //    with the transparency color to use. If loaded into the Colors palette,
      //    the colors will be installed in the color swatch list as RGB colors.
      //
      StringWriter  data;
      for (size_t z = 0; z < decoded.size(); z++) {
        data.put_u8(decoded[z].c.r / 0x0101);
        data.put_u8(decoded[z].c.g / 0x0101);
        data.put_u8(decoded[z].c.b / 0x0101);
      }
      data.extend_to(768, '\0');
      data.put_u16b(decoded.size());
      // No transparent color
      data.put_u16b(0xFFFFu);
      
      this->write_decoded_data(base_filename, res, ".act", data.str());
    }
  }

  void write_decoded_pltt(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    // Always write the raw for this resource type because the decoded version
    // loses precision
    this->write_decoded_data(base_filename, res, ".bin", res->data);

    auto decoded = this->current_rf->decode_pltt(res);
    // Add appropriate color IDs to ths pltt so we can render it as if it were a
    // clut
    vector<ColorTableEntry> entries;
    entries.reserve(decoded.size());
    for (const auto& c : decoded) {
      auto& entry = entries.emplace_back();
      entry.color_num = entries.size() - 1;
      entry.c = c;
    }
    this->write_decoded_data(base_filename, res, entries);
  }

  void write_decoded_clut_actb_cctb_dctb_fctb_wctb(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    // Always write the raw for this resource type because the decoded version
    // loses precision
    this->write_decoded_data(base_filename, res, ".bin", res->data);

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
    this->write_decoded_data(base_filename, res, this->current_rf->decode_clut(res),
        index_names);
  }

  void write_decoded_CTBL(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    // Always write the raw for this resource type because some tools demand it
    this->write_decoded_data(base_filename, res, ".bin", res->data);
    this->write_decoded_data(base_filename, res, this->current_rf->decode_CTBL(res));
  }

  void write_decoded_PAT(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    Image decoded = this->current_rf->decode_PAT(res);

    Image tiled = tile_image(decoded, 8, 8);
    this->write_decoded_data(base_filename, res, ".bmp", decoded);
    this->write_decoded_data(base_filename, res, "_tiled.bmp", tiled);
  }

  void write_decoded_PATN(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_PATN(res);

    for (size_t x = 0; x < decoded.size(); x++) {
      string after = string_printf("_%zu.bmp", x);
      this->write_decoded_data(base_filename, res, after, decoded[x]);

      Image tiled = tile_image(decoded[x], 8, 8);
      after = string_printf("_%zu_tiled.bmp", x);
      this->write_decoded_data(base_filename, res, after, tiled);
    }
  }

  void write_decoded_SICN(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_SICN(res);

    for (size_t x = 0; x < decoded.size(); x++) {
      string after = string_printf("_%zu.bmp", x);
      this->write_decoded_data(base_filename, res, after, decoded[x]);
    }
  }
  
  void write_decoded_data(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res,
      const ResourceFile::DecodedIconListResource& decoded) {
    if (!decoded.composite.empty()) {
      this->write_decoded_data(base_filename, res, ".bmp", decoded.composite);
    } else if (!decoded.images.empty()) {
      for (size_t x = 0; x < decoded.images.size(); x++) {
        this->write_decoded_data(base_filename, res, string_printf("_%zu.bmp", x), decoded.images[x]);
      }
    } else {
      throw logic_error("decoded icon list contains neither composite nor images");
    }
  }

  shared_ptr<const ResourceFile::Resource> load_family_icon(const std::shared_ptr<const ResourceFile::Resource>& icon, std::uint32_t type) {
    if (icon->type == type) {
      return icon;
    }

    if (this->current_rf->resource_exists(type, icon->id)) {
      return this->current_rf->get_resource(type, icon->id);
    }

    return nullptr;
  }

  static void put_icns_data(
      StringWriter& data,
      const shared_ptr<const ResourceFile::Resource>& icon,
      uint32_t type,
      uint32_t num_pixels,
      uint8_t bit_depth) {

    uint32_t size = (num_pixels * bit_depth) / 8;

    // Skip incomplete resources
    if (icon->data.size() >= size) {
      data.put_u32b(type);
      data.put_u32b(8 + size);
      data.write(icon->data.data(), size);
    }
  }

  static void put_dummy_icns_data(
      StringWriter& data,
      uint32_t type,
      uint32_t num_pixels) {
    
    data.put_u32b(type);
    data.put_u32b(8 + num_pixels / 4);
    data.extend_by(num_pixels / 8, 0x00u);
    data.extend_by(num_pixels / 8, 0xFFu);
  }

  void write_icns(
      const string& base_filename,
      const shared_ptr<const ResourceFile::Resource>& icon) {

    // Already exported? Save time and don't export it again
    if (exported_family_icns.find(icon->id) != exported_family_icns.end()) {
      return;
    }

    // Load all of the family's icons
    shared_ptr<const ResourceFile::Resource> icsN = load_family_icon(icon, RESOURCE_TYPE_icsN);
    shared_ptr<const ResourceFile::Resource> ics4 = load_family_icon(icon, RESOURCE_TYPE_ics4);
    shared_ptr<const ResourceFile::Resource> ics8 = load_family_icon(icon, RESOURCE_TYPE_ics8);
    shared_ptr<const ResourceFile::Resource> icnN = load_family_icon(icon, RESOURCE_TYPE_ICNN);
    shared_ptr<const ResourceFile::Resource> icl4 = load_family_icon(icon, RESOURCE_TYPE_icl4);
    shared_ptr<const ResourceFile::Resource> icl8 = load_family_icon(icon, RESOURCE_TYPE_icl8);

    // Start .icns file
    StringWriter data;
    data.put_u32b(0x69636E73);
    data.put_u32b(0);

    // Write color icons (first, or they won't show in Finder)
    if (ics4) {
      this->put_icns_data(data, ics4, RESOURCE_TYPE_ics4, 16 * 16, 4);
    }
    if (ics8) {
      this->put_icns_data(data, ics8, RESOURCE_TYPE_ics8, 16 * 16, 8);
    }
    if (icl4) {
      this->put_icns_data(data, icl4, RESOURCE_TYPE_icl4, 32 * 32, 4);
    }
    if (icl8) {
      this->put_icns_data(data, icl8, RESOURCE_TYPE_icl8, 32 * 32, 8);
    }

    // Write b/w icons. If they're missing, write a black square as icon, and all pixels set as mask: color icons don't
    // display correctly without b/w icon+mask
    if (icsN) {
      this->put_icns_data(data, icsN, RESOURCE_TYPE_icsN, 16 * 16, 2);
    } else if (ics4 || ics8) {
      this->put_dummy_icns_data(data, RESOURCE_TYPE_icsN, 16 * 16);
    }
    if (icnN) {
      this->put_icns_data(data, icnN, RESOURCE_TYPE_ICNN, 32 * 32, 2);
    } else if (icl4 || icl8) {
      this->put_dummy_icns_data(data, RESOURCE_TYPE_ICNN, 32 * 32);
    }

    // Adjust .icns size
    data.pput_u32b(4, data.size());

    this->write_decoded_data(base_filename, icon, ".icns", data.str());

    exported_family_icns.insert(icon->id);
  }

  void write_decoded_ICNN(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    if (export_icon_family_as_bmp) {
      auto decoded = this->current_rf->decode_ICNN(res);
      this->write_decoded_data(base_filename, res, decoded);
    }
    if (export_icon_family_as_icns) {
      this->write_icns(base_filename, res);
    }
  }

  void write_decoded_icmN(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_icmN(res);
    this->write_decoded_data(base_filename, res, decoded);
  }

  void write_decoded_icsN(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    if (export_icon_family_as_bmp) {
      auto decoded = this->current_rf->decode_icsN(res);
      this->write_decoded_data(base_filename, res, decoded);
    }
    if (export_icon_family_as_icns) {
      this->write_icns(base_filename, res);
    }
  }

  void write_decoded_kcsN(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_kcsN(res);
    this->write_decoded_data(base_filename, res, decoded);
  }

  void write_decoded_cicn(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_cicn(res);

    this->write_decoded_data(base_filename, res, ".bmp", decoded.image);

    if (decoded.bitmap.get_width() && decoded.bitmap.get_height()) {
      this->write_decoded_data(base_filename, res, "_bitmap.bmp", decoded.bitmap);
    }
  }

  void write_decoded_icl8(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    if (export_icon_family_as_bmp) {
      auto decoded = this->current_rf->decode_icl8(res);
      this->write_decoded_data(base_filename, res, ".bmp", decoded);
    }
    if (export_icon_family_as_icns) {
      this->write_icns(base_filename, res);
    }
  }

  void write_decoded_icm8(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_icm8(res);
    this->write_decoded_data(base_filename, res, ".bmp", decoded);
  }

  void write_decoded_ics8(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    if (export_icon_family_as_bmp) {
      auto decoded = this->current_rf->decode_ics8(res);
      this->write_decoded_data(base_filename, res, ".bmp", decoded);
    }
    if (export_icon_family_as_icns) {
      this->write_icns(base_filename, res);
    }
  }

  void write_decoded_kcs8(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_kcs8(res);
    this->write_decoded_data(base_filename, res, ".bmp", decoded);
  }

  void write_decoded_icl4(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    if (export_icon_family_as_bmp) {
      auto decoded = this->current_rf->decode_icl4(res);
      this->write_decoded_data(base_filename, res, ".bmp", decoded);
    }
    if (export_icon_family_as_icns) {
      this->write_icns(base_filename, res);
    }
  }

  void write_decoded_icm4(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_icm4(res);
    this->write_decoded_data(base_filename, res, ".bmp", decoded);
  }

  void write_decoded_ics4(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    if (export_icon_family_as_bmp) {
      auto decoded = this->current_rf->decode_ics4(res);
      this->write_decoded_data(base_filename, res, ".bmp", decoded);
    }
    if (export_icon_family_as_icns) {
      this->write_icns(base_filename, res);
    }
  }

  void write_decoded_kcs4(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_kcs4(res);
    this->write_decoded_data(base_filename, res, ".bmp", decoded);
  }

  void write_decoded_ICON(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_ICON(res);
    this->write_decoded_data(base_filename, res, ".bmp", decoded);
  }

  // Note: This function is used for decoding an icns resource, not for creating
  // an icns file from the ICN#, icl4, icl8, etc. resources from the source file
  // (for that, see write_icns).
  void write_decoded_icns(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res,
      const ResourceFile::DecodedIconImagesResource& decoded,
      const string& filename_suffix = "") {
    size_t file_index = 0;
    for (const auto& it : decoded.type_to_image) {
      string type_str = string_for_resource_type(it.first);
      string after = string_printf("%s_%s.%zu.bmp", filename_suffix.c_str(), type_str.c_str(), file_index++);
      this->write_decoded_data(base_filename, res, after, it.second);
    }
    for (const auto& it : decoded.type_to_composite_image) {
      string type_str = string_for_resource_type(it.first);
      string after = string_printf("%s_%s_composite.%zu.bmp", filename_suffix.c_str(), type_str.c_str(), file_index++);
      this->write_decoded_data(base_filename, res, after, it.second);
    }
    for (const auto& it : decoded.type_to_jpeg2000_data) {
      string type_str = string_for_resource_type(it.first);
      string after = string_printf("%s_%s.%zu.jp2", filename_suffix.c_str(), type_str.c_str(), file_index++);
      this->write_decoded_data(base_filename, res, after, it.second);
    }
    for (const auto& it : decoded.type_to_png_data) {
      string type_str = string_for_resource_type(it.first);
      string after = string_printf("%s_%s.%zu.png", filename_suffix.c_str(), type_str.c_str(), file_index++);
      this->write_decoded_data(base_filename, res, after, it.second);
    }
    if (!decoded.toc_data.empty()) {
      string after = string_printf("%s.toc.bin", filename_suffix.c_str());
      this->write_decoded_data(base_filename, res, after, decoded.toc_data);
    }
    if (!decoded.name.empty()) {
      string after = string_printf("%s.name.txt", filename_suffix.c_str());
      this->write_decoded_data(base_filename, res, after, decoded.name);
    }
    if (!decoded.info_plist.empty()) {
      string after = string_printf("%s.info.plist", filename_suffix.c_str());
      this->write_decoded_data(base_filename, res, after, decoded.info_plist);
    }
    if (decoded.template_icns) {
      this->write_decoded_icns(base_filename, res, *decoded.template_icns,
          filename_suffix + "_template");
    }
    if (decoded.selected_icns) {
      this->write_decoded_icns(base_filename, res, *decoded.selected_icns,
          filename_suffix + "_selected");
    }
    if (decoded.dark_icns) {
      this->write_decoded_icns(base_filename, res, *decoded.dark_icns,
          filename_suffix + "_dark");
    }
  }

  void write_decoded_icns(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_icns(res);
    this->write_decoded_icns(base_filename, res, decoded);
  }

  void write_decoded_PICT_internal(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_PICT_internal(res);
    if (!decoded.embedded_image_data.empty()) {
      this->write_decoded_data(base_filename, res, "." + decoded.embedded_image_format, decoded.embedded_image_data);
    } else {
      this->write_decoded_data(base_filename, res, ".bmp", decoded.image);
    }
  }

  void write_decoded_PICT(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_PICT(res);
    if (!decoded.embedded_image_data.empty()) {
      this->write_decoded_data(base_filename, res, "." + decoded.embedded_image_format, decoded.embedded_image_data);
    } else {
      this->write_decoded_data(base_filename, res, ".bmp", decoded.image);
    }
  }

  void write_decoded_snd(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_snd(res);
    this->write_decoded_data(base_filename, res,
        decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
  }

  void write_decoded_csnd(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_csnd(res);
    this->write_decoded_data(base_filename, res,
        decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
  }

  void write_decoded_esnd(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_esnd(res);
    this->write_decoded_data(base_filename, res,
        decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
  }

  void write_decoded_ESnd(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_ESnd(res);
    this->write_decoded_data(base_filename, res,
        decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
  }

  void write_decoded_Ysnd(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_Ysnd(res);
    this->write_decoded_data(base_filename, res,
        decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
  }

  void write_decoded_SMSD(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    string decoded = this->current_rf->decode_SMSD(res);
    this->write_decoded_data(base_filename, res, ".wav", decoded);
  }

  void write_decoded_SOUN(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    string decoded = this->current_rf->decode_SOUN(res);
    this->write_decoded_data(base_filename, res, ".wav", decoded);
  }

  void write_decoded_cmid(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    string decoded = this->current_rf->decode_cmid(res);
    this->write_decoded_data(base_filename, res, ".midi", decoded);
  }

  void write_decoded_emid(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    string decoded = this->current_rf->decode_emid(res);
    this->write_decoded_data(base_filename, res, ".midi", decoded);
  }

  void write_decoded_ecmi(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    string decoded = this->current_rf->decode_ecmi(res);
    this->write_decoded_data(base_filename, res, ".midi", decoded);
  }

  void write_decoded_FONT_NFNT(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_FONT(res);

    {
      string description_filename = this->output_filename(base_filename, res, "_description.txt");
      this->ensure_directories_exist(description_filename);
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
      this->write_decoded_data(base_filename, res, "_glyph_missing.bmp", decoded.missing_glyph.img);
    }

    for (size_t x = 0; x < decoded.glyphs.size(); x++) {
      if (!decoded.glyphs[x].img.get_width()) {
        continue;
      }
      string after = string_printf("_glyph_%02zX.bmp", decoded.first_char + x);
      this->write_decoded_data(base_filename, res, after, decoded.glyphs[x].img);
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

  void write_decoded_cfrg(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    string description = generate_text_for_cfrg(this->current_rf->decode_cfrg(res));
    this->write_decoded_data(base_filename, res, ".txt", description);
  }

  void write_decoded_SIZE(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_SIZE(res);
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
    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_vers(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_vers(res);

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
    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_finf(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_finf(res);

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

    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_ROvN(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_ROvN(res);

    string disassembly = string_printf("# ROM version: 0x%04hX\n", decoded.rom_version);
    for (size_t x = 0; x < decoded.overrides.size(); x++) {
      const auto& override = decoded.overrides[x];
      string type_name = string_for_resource_type(override.type);
      disassembly += string_printf("# override %zu: %08X (%s) #%hd\n",
          x, override.type.load(), type_name.c_str(), override.id.load());
    }

    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_CODE(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    string disassembly;
    if (res->id == 0) {
      auto decoded = this->current_rf->decode_CODE_0(res);
      disassembly += string_printf("# above A5 size: 0x%08X\n", decoded.above_a5_size);
      disassembly += string_printf("# below A5 size: 0x%08X\n", decoded.below_a5_size);
      for (size_t x = 0; x < decoded.jump_table.size(); x++) {
        const auto& e = decoded.jump_table[x];
        if (e.code_resource_id || e.offset) {
          disassembly += string_printf("# export %zu [A5 + 0x%zX]: CODE %hd offset 0x%hX after header\n",
              x, 0x22 + (x * 8), e.code_resource_id, e.offset);
        }
      }

    } else {
      auto decoded = this->current_rf->decode_CODE(res);

      // attempt to decode CODE 0 to get the exported label offsets
      multimap<uint32_t, string> labels;
      try {
        auto code0_data = this->current_rf->decode_CODE_0(0, res->type);
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

    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_DRVR(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_DRVR(res);

    string disassembly;

    vector<const char*> flags_strs;
    if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::ENABLE_READ) {
      flags_strs.emplace_back("ENABLE_READ");
    }
    if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::ENABLE_WRITE) {
      flags_strs.emplace_back("ENABLE_WRITE");
    }
    if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::ENABLE_CONTROL) {
      flags_strs.emplace_back("ENABLE_CONTROL");
    }
    if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::ENABLE_STATUS) {
      flags_strs.emplace_back("ENABLE_STATUS");
    }
    if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::NEED_GOODBYE) {
      flags_strs.emplace_back("NEED_GOODBYE");
    }
    if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::NEED_TIME) {
      flags_strs.emplace_back("NEED_TIME");
    }
    if (decoded.flags & ResourceFile::DecodedDriverResource::Flag::NEED_LOCK) {
      flags_strs.emplace_back("NEED_LOCK");
    }
    string flags_str = join(flags_strs, ", ");

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

    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_dcmp(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_dcmp(res);

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

    this->write_decoded_data(base_filename, res, ".txt", result);
  }

  void write_decoded_inline_68k(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    multimap<uint32_t, string> labels;
    labels.emplace(0, "start");
    string result = M68KEmulator::disassemble(res->data.data(), res->data.size(), 0,
        &labels);
    this->write_decoded_data(base_filename, res, ".txt", result);
  }

  void write_decoded_inline_ppc32(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    multimap<uint32_t, string> labels;
    labels.emplace(0, "start");
    string result = PPC32Emulator::disassemble(res->data.data(), res->data.size(),
        0, &labels);
    this->write_decoded_data(base_filename, res, ".txt", result);
  }

  void write_decoded_pef(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto pef = this->current_rf->decode_pef(res);
    string filename = this->output_filename(base_filename, res, ".txt");
    this->ensure_directories_exist(filename);
    auto f = fopen_unique(filename, "wt");
    pef.print(f.get());
    fprintf(stderr, "... %s\n", filename.c_str());
  }

  void write_decoded_expt_nsrd(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = (res->type == RESOURCE_TYPE_expt) ? this->current_rf->decode_expt(res) : this->current_rf->decode_nsrd(res);
    string filename = this->output_filename(base_filename, res, ".txt");
    this->ensure_directories_exist(filename);
    auto f = fopen_unique(filename, "wt");
    fputs("Mixed-mode manager header:\n", f.get());
    print_data(f.get(), decoded.header);
    fputc('\n', f.get());
    decoded.pef.print(f.get());
    fprintf(stderr, "... %s\n", filename.c_str());
  }

  void write_decoded_inline_68k_or_pef(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    if (res->data.size() < 4) {
      throw runtime_error("can\'t determine code type");
    }
    if (*reinterpret_cast<const be_uint32_t*>(res->data.data()) == 0x4A6F7921) { // Joy!
      write_decoded_pef(base_filename, res);
    } else {
      write_decoded_inline_68k(base_filename, res);
    }
  }

  void write_decoded_TEXT(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    this->write_decoded_data(base_filename, res, ".txt", this->current_rf->decode_TEXT(res));
  }

  void write_decoded_card(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    this->write_decoded_data(base_filename, res, ".txt", this->current_rf->decode_card(res));
  }

  void write_decoded_styl(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    this->write_decoded_data(base_filename, res, ".rtf", this->current_rf->decode_styl(res));
  }

  void write_decoded_STR(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_STR(res);

    this->write_decoded_data(base_filename, res, ".txt", decoded.str);
    if (!decoded.after_data.empty()) {
      this->write_decoded_data(base_filename, res, "_data.bin", decoded.after_data);
    }
  }

  void write_decoded_STRN(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_STRN(res);

    for (size_t x = 0; x < decoded.strs.size(); x++) {
      string after = string_printf("_%lu.txt", x);
      this->write_decoded_data(base_filename, res, after, decoded.strs[x]);
    }
    if (!decoded.after_data.empty()) {
      this->write_decoded_data(base_filename, res, "_excess.bin", decoded.after_data);
    }
  }

  shared_ptr<JSONObject> generate_json_for_INST(
      const string& base_filename,
      int32_t id,
      const ResourceFile::DecodedInstrumentResource& inst,
      int8_t song_semitone_shift) {
    // SoundMusicSys has a (bug? feature?) where the instrument's base note
    // affects which key region is used, but then the key region's base note
    // determines the played note pitch and the instrument's base note is ignored.
    // To correct for this, we have to shift all the key regions up/down by an
    // appropriate amount, but also use freq_mult to adjust their pitches.
    int8_t key_region_boundary_shift = 0;
    if ((inst.key_regions.size() > 1) && inst.base_note) {
      key_region_boundary_shift += inst.base_note - 0x3C;
    }

    vector<shared_ptr<JSONObject>> key_regions_list;
    for (const auto& rgn : inst.key_regions) {
      const auto& snd_res = this->current_rf->get_resource(rgn.snd_type, rgn.snd_id);
      unordered_map<string, shared_ptr<JSONObject>> key_region_dict;
      key_region_dict.emplace("key_low", new JSONObject(static_cast<int64_t>(rgn.key_low) + key_region_boundary_shift));
      key_region_dict.emplace("key_high", new JSONObject(static_cast<int64_t>(rgn.key_high) + key_region_boundary_shift));

      uint8_t snd_base_note = 0x3C;
      uint32_t snd_sample_rate = 22050;
      bool snd_is_mp3 = false;
      try {
        // TODO: This is dumb; we only need the sample rate and base note; find
        // a way to not have to re-decode the sound.
        ResourceFile::DecodedSoundResource decoded_snd;
        if (rgn.snd_type == RESOURCE_TYPE_esnd) {
          decoded_snd = this->current_rf->decode_esnd(rgn.snd_id, rgn.snd_type, true);
        } else if (rgn.snd_type == RESOURCE_TYPE_csnd) {
          decoded_snd = this->current_rf->decode_csnd(rgn.snd_id, rgn.snd_type, true);
        } else if (rgn.snd_type == RESOURCE_TYPE_snd) {
          decoded_snd = this->current_rf->decode_snd(rgn.snd_id, rgn.snd_type, true);
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

      string snd_filename = basename(this->output_filename(base_filename, snd_res,
          snd_is_mp3 ? ".mp3" : ".wav"));
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
      key_region_dict.emplace("base_note", new JSONObject(
          static_cast<int64_t>(base_note)));

      // if use_sample_rate is NOT set, set a freq_mult to correct for this
      // because smssynth always accounts for different sample rates
      double freq_mult = 1.0f;
      if (!inst.use_sample_rate) {
        freq_mult *= 22050.0 / static_cast<double>(snd_sample_rate);
      }
      if (song_semitone_shift) {
        // TODO: Does this implementation suffice? Should we be shifting
        // base_notes and key_lows/key_highs instead, to account for region choice
        // when playing notes?
        freq_mult *= pow(2, static_cast<double>(song_semitone_shift) / 12.0);
      }
      if (freq_mult != 1.0f) {
        key_region_dict.emplace("freq_mult", new JSONObject(freq_mult));
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
      const ResourceFile::DecodedSongResource* s) {
    string midi_filename;
    if (s) {
      static const vector<uint32_t> midi_types({
          RESOURCE_TYPE_MIDI, RESOURCE_TYPE_Midi, RESOURCE_TYPE_midi,
          RESOURCE_TYPE_cmid, RESOURCE_TYPE_emid, RESOURCE_TYPE_ecmi});
      for (uint32_t midi_type : midi_types) {
        try {
          const auto& res = this->current_rf->get_resource(midi_type, s->midi_id);
          midi_filename = basename(this->output_filename(base_filename, res, ".midi"));
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
              base_filename, it.first, this->current_rf->decode_INST(it.second), s->semitone_shift));
        } catch (const exception& e) {
          fprintf(stderr, "warning: failed to add instrument %hu from INST %hu: %s\n",
              it.first, it.second, e.what());
        }
      }
    }
    for (int16_t id : this->current_rf->all_resources_of_type(RESOURCE_TYPE_INST)) {
      if (s && s->instrument_overrides.count(id)) {
        continue; // already added this one as a different instrument
      }
      try {
        instruments.emplace_back(generate_json_for_INST(
            base_filename, id, this->current_rf->decode_INST(id), s ? s->semitone_shift : 0));
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

  void write_decoded_INST(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto json = generate_json_for_INST(base_filename, res->id, this->current_rf->decode_INST(res), 0);
    this->write_decoded_data(base_filename, res, ".json", json->format());
  }

  void write_decoded_SONG(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    auto song = this->current_rf->decode_SONG(res);
    auto json = generate_json_for_SONG(base_filename, &song);
    this->write_decoded_data(base_filename, res, "_smssynth_env.json", json->format());
  }

  void write_decoded_Tune(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {
    string decoded = this->current_rf->decode_Tune(res);
    this->write_decoded_data(base_filename, res, ".midi", decoded);
  }



  typedef void (ResourceExporter::*resource_decode_fn)(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res);

  static const unordered_map<uint32_t, resource_decode_fn> default_type_to_decode_fn;
  
  unordered_map<uint32_t, resource_decode_fn> type_to_decode_fn;
  static const unordered_map<uint32_t, const char*> type_to_ext;
  
  // Maps (type, ID) pairs to a type to remap special resources
  // (e.g. INTL 0 which is remapped to itl0)
  //
  // not `unordered_map` because `pair` doesn't support hashing
  static const map<pair<uint32_t, int16_t>, uint32_t> remap_resource_type;
  

  bool disassemble_file(const string& filename) {
    string resource_fork_filename = filename;
    if (!this->use_data_fork) {
      resource_fork_filename += RESOURCE_FORK_FILENAME_SUFFIX;
    }

    // On HFS+, the resource fork always exists, but might be empty. On APFS,
    // the resource fork is optional.
    if (!isfile(resource_fork_filename) || stat(resource_fork_filename).st_size == 0) {
      fprintf(stderr, ">>> %s (%s)\n", filename.c_str(),
          this->use_data_fork ? "file is empty" : "resource fork missing or empty");
      return false;

    } else {
      fprintf(stderr, ">>> %s\n", filename.c_str());
    }

    // compute the base filename
    size_t last_slash_pos = filename.rfind('/');
    string base_filename = (last_slash_pos == string::npos) ? filename :
        filename.substr(last_slash_pos + 1);

    // get the resources from the file
    try {
      this->current_rf.reset(new ResourceFile(this->parse(load_file(resource_fork_filename))));
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
      auto resources = this->current_rf->all_resources();

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
        const auto& res = this->current_rf->get_resource(
            it.first, it.second, this->decompress_flags);
        if ((!this->target_names.empty() && !this->target_names.count(res->name)) ||
            this->skip_names.count(res->name)) {
          continue;
        }
        if (it.first == RESOURCE_TYPE_INST) {
          has_INST = true;
        }
        ret |= this->export_resource(base_filename.c_str(), res);
      }

      // special case: if we disassembled any INSTs and the save-raw behavior is
      // not Never, generate an smssynth template file from all the INSTs
      if (has_INST && (this->save_raw != SaveRawBehavior::NEVER)) {
        string json_filename = output_filename(base_filename, nullptr, "generated", "", 0, "smssynth_env_template.json");

        try {
          auto json = generate_json_for_SONG(base_filename, nullptr);
          save_file(json_filename, json->format());
          fprintf(stderr, "... %s\n", json_filename.c_str());

        } catch (const exception& e) {
          fprintf(stderr, "failed to write smssynth env template %s: %s\n",
              json_filename.c_str(), e.what());
        }
      }

    } catch (const exception& e) {
      fprintf(stderr, "failed on %s: %s\n", filename.c_str(), e.what());
    }

    this->current_rf.reset();
    return ret;
  }

  bool disassemble_path(const string& filename) {
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

      string sub_out_dir = this->out_dir.empty()
          ? base_filename : (this->out_dir + "/" + base_filename);
      bool ret = false;
      for (const string& item : sorted_items) {
        sub_out_dir.swap(this->out_dir);
        ret |= this->disassemble_path(filename + "/" + item);
        sub_out_dir.swap(this->out_dir);
      }
      return ret;

    } else {
      return this->disassemble_file(filename);
    }
  }

public:

  enum class SaveRawBehavior {
    NEVER = 0,
    IF_DECODE_FAILS,
    ALWAYS,
  };
  enum class TargetCompressedBehavior {
    DEFAULT = 0,
    TARGET,
    SKIP,
  };

  ResourceExporter()
    : type_to_decode_fn(default_type_to_decode_fn),
      use_data_fork(false),
      filename_format(FILENAME_FORMAT_STANDARD),
      save_raw(SaveRawBehavior::IF_DECODE_FAILS),
      decompress_flags(0),
      target_compressed_behavior(TargetCompressedBehavior::DEFAULT),
      skip_templates(false),
      export_icon_family_as_bmp(true),
      export_icon_family_as_icns(true),
      index_format(IndexFormat::RESOURCE_FORK),
      parse(parse_resource_fork) { }
  ~ResourceExporter() = default;

  bool use_data_fork;
  string filename_format;
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
  bool export_icon_family_as_bmp;
  bool export_icon_family_as_icns;
private:
  string base_out_dir; // Fixed part of filename (e.g. <file>.out)
  string out_dir; // Recursive part of filename (dirs after <file>.out)
  IndexFormat index_format;
  unique_ptr<ResourceFile> current_rf;
  unordered_set<int32_t> exported_family_icns;
  ResourceFile (*parse)(const string&);

public:

  void set_index_format(IndexFormat new_format) {
    this->index_format = new_format;
    if (this->index_format == IndexFormat::RESOURCE_FORK) {
      this->parse = parse_resource_fork;
    } else if (this->index_format == IndexFormat::MOHAWK) {
      this->parse = parse_mohawk;
    } else if (this->index_format == IndexFormat::HIRF) {
      this->parse = parse_hirf;
    } else if (this->index_format == IndexFormat::DC_DATA) {
      this->parse = parse_dc_data;
    } else {
      throw logic_error("invalid index format");
    }
  }

  void set_decoder_alias(uint32_t from_type, uint32_t to_type) {
    try {
      this->type_to_decode_fn[to_type] = this->type_to_decode_fn.at(from_type);
    } catch (const out_of_range&) { }
  }

  void disable_external_decoders() {
    this->type_to_decode_fn[RESOURCE_TYPE_PICT] = &ResourceExporter::write_decoded_PICT_internal;
  }

  void disable_all_decoders() {
    this->type_to_decode_fn.clear();
  }

  bool export_resource(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res) {

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
    if ((this->target_compressed_behavior == TargetCompressedBehavior::TARGET) &&
        !(is_compressed || was_compressed || decompression_failed)) {
      return false;
    } else if ((this->target_compressed_behavior == TargetCompressedBehavior::SKIP) &&
        (is_compressed || was_compressed || decompression_failed)) {
      return false;
    }

    bool write_raw = (this->save_raw == SaveRawBehavior::ALWAYS);
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
    uint32_t  remapped_type = res->type;
    try {
      remapped_type = remap_resource_type.at({ res_to_decode->type, res_to_decode->id });
    } catch (const out_of_range&) { }
    
    resource_decode_fn  decode_fn = nullptr;
    try {
      decode_fn = type_to_decode_fn.at(remapped_type);
    } catch (const out_of_range&) { }

    bool decoded = false;
    if (!is_compressed && decode_fn) {
      try {
        (this->*decode_fn)(base_filename, res_to_decode);
        decoded = true;
      } catch (const exception& e) {
        auto type_str = string_for_resource_type(res->type);
        if (remapped_type != res->type) {
          auto remapped_type_str = string_for_resource_type(remapped_type);
          fprintf(stderr, "warning: failed to decode resource %s:%d (remapped to %s): %s\n", type_str.c_str(), res->id, remapped_type_str.c_str(), e.what());
        } else {
          fprintf(stderr, "warning: failed to decode resource %s:%d: %s\n", type_str.c_str(), res->id, e.what());
        }
      }
    }
    // If there's no built-in decoder and there's a context ResourceFile, try to
    // use a TMPL resource to decode it
    if (!is_compressed && !decoded && !this->skip_templates && this->current_rf.get()) {
      // It appears ResEdit looks these up by name
      string tmpl_name = raw_string_for_resource_type(remapped_type);

      // If there's no TMPL, just silently fail this step. If there's a TMPL but
      // it's corrupt or doesn't decode the data correctly, fail with a warning.
      shared_ptr<const ResourceFile::Resource> tmpl_res;
      try {
        tmpl_res = this->current_rf->get_resource(RESOURCE_TYPE_TMPL, tmpl_name.c_str());
      } catch (const out_of_range& e) { }

      if (tmpl_res.get()) {
        try {
          string result = string_printf("# (decoded with TMPL %hd)\n", tmpl_res->id);
          result += this->current_rf->disassemble_from_template(
              res->data.data(), res->data.size(), this->current_rf->decode_TMPL(tmpl_res));
          this->write_decoded_data(base_filename, res_to_decode, ".txt", result);
          decoded = true;
        } catch (const exception& e) {
          auto type_str = string_for_resource_type(res->type);
          if (remapped_type != res->type) {
            auto remapped_type_str = string_for_resource_type(remapped_type);
            fprintf(stderr, "warning: failed to decode resource %s:%d (remapped to %s) with template %hd: %s\n", type_str.c_str(), res->id, remapped_type_str.c_str(), tmpl_res->id, e.what());
          } else {
            fprintf(stderr, "warning: failed to decode resource %s:%d with template %hd: %s\n", type_str.c_str(), res->id, tmpl_res->id, e.what());
          }
        }
      }
    }
    // If there's no built-in decoder and no TMPL in the file, try using a
    // system template
    if (!is_compressed && !decoded && !this->skip_templates) {
      const ResourceFile::TemplateEntryList& tmpl = get_system_template(remapped_type);
      if (!tmpl.empty()) {
        try {
          string result = ResourceFile::disassemble_from_template(
              res->data.data(), res->data.size(), tmpl);
          this->write_decoded_data(base_filename, res_to_decode, ".txt", result);
          decoded = true;
        } catch (const exception& e) {
          auto type_str = string_for_resource_type(res->type);
          if (remapped_type != res->type) {
            auto remapped_type_str = string_for_resource_type(remapped_type);
            fprintf(stderr, "warning: failed to decode resource %s:%d (remapped to %s) with system template: %s\n", type_str.c_str(), res->id, remapped_type_str.c_str(), e.what());
          } else {
            fprintf(stderr, "warning: failed to decode resource %s:%d with system template: %s\n", type_str.c_str(), res->id, e.what());
          }
        }
      }
    }

    if (!decoded && this->save_raw == SaveRawBehavior::IF_DECODE_FAILS) {
      write_raw = true;
    }

    if (write_raw) {
      const char* out_ext = "bin";
      try {
        out_ext = type_to_ext.at(res_to_decode->type);
      } catch (const out_of_range&) { }

      string out_filename_after = string_printf(".%s", out_ext);
      string out_filename = this->output_filename(base_filename, res_to_decode, out_filename_after);
      this->ensure_directories_exist(out_filename);

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

  bool disassemble(const string& filename, const string& base_out_dir) {
    this->base_out_dir = base_out_dir;
    return this->disassemble_path(filename);
  }
};

// Annoyingly, these have to be initialized out of line
const unordered_map<uint32_t, ResourceExporter::resource_decode_fn> ResourceExporter::default_type_to_decode_fn({
  {RESOURCE_TYPE_actb, &ResourceExporter::write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_ADBS, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_card, &ResourceExporter::write_decoded_card},
  {RESOURCE_TYPE_cctb, &ResourceExporter::write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_CDEF, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_cdek, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_cfrg, &ResourceExporter::write_decoded_cfrg},
  {RESOURCE_TYPE_cicn, &ResourceExporter::write_decoded_cicn},
  {RESOURCE_TYPE_clok, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_clut, &ResourceExporter::write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_cmid, &ResourceExporter::write_decoded_cmid},
  {RESOURCE_TYPE_CODE, &ResourceExporter::write_decoded_CODE},
  {RESOURCE_TYPE_crsr, &ResourceExporter::write_decoded_crsr},
  {RESOURCE_TYPE_csnd, &ResourceExporter::write_decoded_csnd},
  {RESOURCE_TYPE_CTBL, &ResourceExporter::write_decoded_CTBL},
  {RESOURCE_TYPE_CURS, &ResourceExporter::write_decoded_CURS},
  {RESOURCE_TYPE_dcmp, &ResourceExporter::write_decoded_dcmp},
  {RESOURCE_TYPE_dcod, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_dctb, &ResourceExporter::write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_DRVR, &ResourceExporter::write_decoded_DRVR},
  {RESOURCE_TYPE_ecmi, &ResourceExporter::write_decoded_ecmi},
  {RESOURCE_TYPE_emid, &ResourceExporter::write_decoded_emid},
  {RESOURCE_TYPE_esnd, &ResourceExporter::write_decoded_esnd},
  {RESOURCE_TYPE_ESnd, &ResourceExporter::write_decoded_ESnd},
  {RESOURCE_TYPE_expt, &ResourceExporter::write_decoded_expt_nsrd},
  {RESOURCE_TYPE_FCMT, &ResourceExporter::write_decoded_STR},
  {RESOURCE_TYPE_fctb, &ResourceExporter::write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_finf, &ResourceExporter::write_decoded_finf},
  {RESOURCE_TYPE_FONT, &ResourceExporter::write_decoded_FONT_NFNT},
  {RESOURCE_TYPE_fovr, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_icl4, &ResourceExporter::write_decoded_icl4},
  {RESOURCE_TYPE_icl8, &ResourceExporter::write_decoded_icl8},
  {RESOURCE_TYPE_icm4, &ResourceExporter::write_decoded_icm4},
  {RESOURCE_TYPE_icm8, &ResourceExporter::write_decoded_icm8},
  {RESOURCE_TYPE_icmN, &ResourceExporter::write_decoded_icmN},
  {RESOURCE_TYPE_ICNN, &ResourceExporter::write_decoded_ICNN},
  {RESOURCE_TYPE_ICON, &ResourceExporter::write_decoded_ICON},
  {RESOURCE_TYPE_icns, &ResourceExporter::write_decoded_icns},
  {RESOURCE_TYPE_ics4, &ResourceExporter::write_decoded_ics4},
  {RESOURCE_TYPE_ics8, &ResourceExporter::write_decoded_ics8},
  {RESOURCE_TYPE_icsN, &ResourceExporter::write_decoded_icsN},
  {RESOURCE_TYPE_INIT, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_INST, &ResourceExporter::write_decoded_INST},
  {RESOURCE_TYPE_kcs4, &ResourceExporter::write_decoded_kcs4},
  {RESOURCE_TYPE_kcs8, &ResourceExporter::write_decoded_kcs8},
  {RESOURCE_TYPE_kcsN, &ResourceExporter::write_decoded_kcsN},
  {RESOURCE_TYPE_LDEF, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_MACS, &ResourceExporter::write_decoded_STR},
  {RESOURCE_TYPE_MBDF, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_MDEF, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_minf, &ResourceExporter::write_decoded_TEXT},
  {RESOURCE_TYPE_ncmp, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_ndmc, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_ndrv, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_NFNT, &ResourceExporter::write_decoded_FONT_NFNT},
  {RESOURCE_TYPE_nift, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_nitt, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_nlib, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_nsnd, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_nsrd, &ResourceExporter::write_decoded_expt_nsrd},
  {RESOURCE_TYPE_ntrb, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_PACK, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_PAT , &ResourceExporter::write_decoded_PAT},
  {RESOURCE_TYPE_PATN, &ResourceExporter::write_decoded_PATN},
  {RESOURCE_TYPE_PICT, &ResourceExporter::write_decoded_PICT},
  {RESOURCE_TYPE_pltt, &ResourceExporter::write_decoded_pltt},
  {RESOURCE_TYPE_ppat, &ResourceExporter::write_decoded_ppat},
  {RESOURCE_TYPE_ppct, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_pptN, &ResourceExporter::write_decoded_pptN},
  {RESOURCE_TYPE_proc, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_PTCH, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_ptch, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_qtcm, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_ROvN, &ResourceExporter::write_decoded_ROvN},
  {RESOURCE_TYPE_ROvr, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_scal, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_SERD, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_sfvr, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_SICN, &ResourceExporter::write_decoded_SICN},
  {RESOURCE_TYPE_SIZE, &ResourceExporter::write_decoded_SIZE},
  {RESOURCE_TYPE_SMOD, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_SMSD, &ResourceExporter::write_decoded_SMSD},
  {RESOURCE_TYPE_snd , &ResourceExporter::write_decoded_snd},
  {RESOURCE_TYPE_snth, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_SONG, &ResourceExporter::write_decoded_SONG},
  {RESOURCE_TYPE_SOUN, &ResourceExporter::write_decoded_SOUN},
  {RESOURCE_TYPE_STR , &ResourceExporter::write_decoded_STR},
  {RESOURCE_TYPE_STRN, &ResourceExporter::write_decoded_STRN},
  {RESOURCE_TYPE_styl, &ResourceExporter::write_decoded_styl},
  {RESOURCE_TYPE_TEXT, &ResourceExporter::write_decoded_TEXT},
  {RESOURCE_TYPE_TMPL, &ResourceExporter::write_decoded_TMPL},
  {RESOURCE_TYPE_Tune, &ResourceExporter::write_decoded_Tune},
  {RESOURCE_TYPE_vers, &ResourceExporter::write_decoded_vers},
  {RESOURCE_TYPE_wctb, &ResourceExporter::write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_WDEF, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_XCMD, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_XFCN, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_Ysnd, &ResourceExporter::write_decoded_Ysnd},

  // Type aliases (unverified)
  {RESOURCE_TYPE_adio, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_AINI, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_atlk, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_boot, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_bstr, &ResourceExporter::write_decoded_STRN},
  {RESOURCE_TYPE_citt, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_cdev, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_cmtb, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_cmuN, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_code, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_dem , &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_dimg, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_drvr, &ResourceExporter::write_decoded_DRVR},
  {RESOURCE_TYPE_enet, &ResourceExporter::write_decoded_DRVR},
  {RESOURCE_TYPE_epch, &ResourceExporter::write_decoded_inline_ppc32},
  {RESOURCE_TYPE_FKEY, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_gcko, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_gdef, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_GDEF, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_gnld, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_krnl, &ResourceExporter::write_decoded_inline_ppc32},
  {RESOURCE_TYPE_lmgr, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_lodr, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_ltlk, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_mntr, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_mstr, &ResourceExporter::write_decoded_STR},
  {RESOURCE_TYPE_mstN, &ResourceExporter::write_decoded_STRN},
  {RESOURCE_TYPE_ndlc, &ResourceExporter::write_decoded_pef},
  {RESOURCE_TYPE_osl , &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_otdr, &ResourceExporter::write_decoded_DRVR},
  {RESOURCE_TYPE_otlm, &ResourceExporter::write_decoded_DRVR},
  {RESOURCE_TYPE_pnll, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_scod, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_shal, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_sift, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_tdig, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_tokn, &ResourceExporter::write_decoded_DRVR},
  {RESOURCE_TYPE_wart, &ResourceExporter::write_decoded_inline_68k},
  {RESOURCE_TYPE_vdig, &ResourceExporter::write_decoded_inline_68k_or_pef},
  {RESOURCE_TYPE_pthg, &ResourceExporter::write_decoded_inline_68k_or_pef},
});

const unordered_map<uint32_t, const char*> ResourceExporter::type_to_ext({
  {RESOURCE_TYPE_icns, "icns"},
  {RESOURCE_TYPE_MADH, "madh"},
  {RESOURCE_TYPE_MADI, "madi"},
  {RESOURCE_TYPE_MIDI, "midi"},
  {RESOURCE_TYPE_Midi, "midi"},
  {RESOURCE_TYPE_midi, "midi"},
  {RESOURCE_TYPE_PICT, "pict"},
  {RESOURCE_TYPE_sfnt, "ttf"},
});

const map<std::pair<uint32_t, int16_t>, uint32_t> ResourceExporter::remap_resource_type = {
  { { RESOURCE_TYPE_PREC, 0 }, RESOURCE_TYPE_PRC0 },
  { { RESOURCE_TYPE_PREC, 1 }, RESOURCE_TYPE_PRC0 },
  { { RESOURCE_TYPE_PREC, 3 }, RESOURCE_TYPE_PRC3 },
  { { RESOURCE_TYPE_PREC, 4 }, RESOURCE_TYPE_PRC3 },
  { { RESOURCE_TYPE_INTL, 0 }, RESOURCE_TYPE_itl0 },
  { { RESOURCE_TYPE_INTL, 1 }, RESOURCE_TYPE_itl1 },
};



void print_usage() {
  fputs("\
Usage: resource_dasm [options] input_filename [output_directory]\n\
\n\
If input_filename is a directory, resource_dasm decodes all resources in all\n\
files and subdirectories within that directory, producing a parallel directory\n\
structure in the output directory.\n\
\n\
If output_directory is not given, the directory <input_filename>.out is created\n\
and the output is written there.\n\
\n\
By default, resource_dasm exports resources from the input file and converts\n\
them into modern formats when possible. resource_dasm can also create new\n\
resource files or modify existing resource files; these modes of action are\n\
described at the end.\n\
\n\
Resource disassembly input options:\n\
  --index-format=FORMAT\n\
      Parse the input as a resource index in this format. Valid FORMATs are:\n\
        resource-fork (default): Mac OS resource fork\n\
        mohawk: Mohawk archive\n\
        hirf: Beatnik HIRF archive (also known as IREZ, HSB, or RMF)\n\
        dc-data: DC Data file\n\
      If the index format is not resource-fork, --data-fork is implied.\n\
  --data-fork\n\
      Disassemble the file\'s data fork as if it were the resource fork.\n\
  --target-type=TYPE\n\
      Only extract resources of this type (can be given multiple times). To\n\
      specify characters with special meanings or non-ASCII characters in\n\
      either type, escape them as %<hex>. For example, to specify the $\n\
      character in the type, escape it as %24. The % character itself can be\n\
      written as %25.\n\
  --target-id=ID\n\
      Only extract resources with this ID (can be given multiple times).\n\
  --target=TYPE[:ID]\n\
      Short form for --target-type=TYPE --target-id=ID.\n\
  --target-name=NAME\n\
      Only extract resources with this name (can be given multiple times).\n\
  --target-compressed\n\
      Only extract resources that are compressed in the source file.\n\
  --skip-type=TYPE\n\
      Don\'t extract resources of this type (can be given multiple times). TYPE\n\
      is escaped the same way as for the --target-type option.\n\
  --skip-id=ID\n\
      Don\'t extract resources with this ID (can be given multiple times).\n\
  --skip-name=NAME\n\
      Don\'t extract resources with this name (can be given multiple times).\n\
  --skip-compressed\n\
      Don\'t extract resources that are compressed in the source file.\n\
  --decode-single-resource=TYPE[:ID[:FLAGS[:NAME]]]\n\
      Decode the input file\'s data fork as if it\'s a single resource of the\n\
      given type. This can be used to decode raw already-exported resources.\n\
      It is usually sufficient to give only a type, as in --decode-type=cicn.\n\
      Some resources may decode differently depending on their IDs; for these,\n\
      pass an ID as well, as in --decode-type=CODE:0 to decode an import table\n\
      (by default, the resource is treated as if its ID is 1). If the input\n\
      data is compressed, set FLAGS to 1, like --decode-type=snd:1:1. Currently\n\
      NAME is not used by any decoder, but there may be decoders in the future\n\
      that depend on the resource\'s name. Using the --decode-single-resource\n\
      option disables all of the above options.\n\
  --decode-pict-file\n\
      Decode the input as a PICT file. When using this option, resource_dasm\n\
      expects an unused 512-byte header before the data, then decodes the rest\n\
      of the data as a PICT resource. This option is not the same as using\n\
      --decode-single-resource=PICT, because PICT resources do not have an\n\
      unused 512-byte header (unlike PICT files).\n\
\n\
Resource decompression options:\n\
  --skip-decompression\n\
      Don\'t attempt to decompress compressed resources. If a resource is\n\
      compressed and decompression fails or is disabled via this option, the\n\
      rest of the decoding steps do not run and the raw compressed data is\n\
      exported instead.\n\
  --skip-file-dcmp\n\
      Don\'t attempt to use any 68K decompressors from the input file.\n\
  --skip-file-ncmp\n\
      Don\'t attempt to use any PEF decompressors from the input file.\n\
  --skip-native-dcmp\n\
      Don\'t attempt to use any native decompressors.\n\
  --skip-system-dcmp\n\
      Don\'t attempt to use the default 68K decompressors.\n\
  --skip-system-ncmp\n\
      Don\'t attempt to use the default PEF decompressors.\n\
  --verbose-decompression\n\
      Show log output when running resource decompressors.\n\
  --strict-decompression\n\
      When running emulated decompressors, some allocated memory regions may be\n\
      larger than strictly necessary for decompression. This option prevents\n\
      emulated decompressors from using the extra allocated space. This option\n\
      does nothing for native decompressors.\n\
  --trace-decompression\n\
      Show memory and CPU state when running resource decompressors under\n\
      emulation. This option does nothing for native decompressors. This slows\n\
      down emulation considerably and is generally only used for finding bugs\n\
      and missing features in the emulated CPUs.\n\
  --debug-decompression\n\
      Start emulated decompressors in single-step mode. This option does\n\
      nothing for native decompressors. When using this option, emulated\n\
      decompressors are stopped immediately before the first opcode is run, and\n\
      you get an m68kexec-style debugger shell to control emulation and inspect\n\
      its state. Run `help` in the shell to see the available commands.\n\
\n\
Resource decoding options:\n\
  --copy-handler=TYPE1:TYPE2\n\
      Decode TYPE1 resources as if they were TYPE2. Non-ASCII bytes in the\n\
      types can be escaped the same way as for the --target-type option. (The\n\
      character \':\' can be escaped as %3A, for example.)\n\
  --external-preprocessor=COMMAND\n\
      After decompression, but before decoding resource data, pass it through\n\
      this external program. The resource data will be passed to the specified\n\
      command via stdin, and the command\'s output on stdout will be treated as\n\
      the resource data to decode. This can be used to transparently decompress\n\
      some custom compression formats.\n\
  --skip-decode\n\
      Don\'t use any decoders to convert resources to modern formats. This\n\
      option implies --skip-templates as well.\n\
  --skip-external-decoders\n\
      Only use internal decoders. Currently, this only disables the use of\n\
      picttoppm for decoding PICT resources.\n\
  --skip-templates\n\
      Don\'t attempt to use TMPL resources to convert resources to text files.\n\
\n\
Resource disassembly output options:\n\
  --save-raw=no\n\
      Don\'t save any raw files; only save decoded resources. For resources that\n\
      can\'t be decoded, no output file is created.\n\
  --save-raw=if-decode-fails\n\
      Only save a raw file if the resource can\'t be converted to a modern\n\
      format or a text file (via a template). This is the default behavior.\n\
  --save-raw=yes (or just --save-raw)\n\
      Save raw files even for resources that are successfully decoded.\n\
  --filename-format=FORMAT\n\
      Specify the directory structure of the output. FORMAT is a printf-like\n\
      string with the following format specifications:\n\
        %a:     the resource's attributes as a string of six characters, where\n\
                each character represents one of the attributes:\n\
                  'c-----': Compressed?\n\
                  '-p----': Preload?\n\
                  '--r---': pRotected [Read-only]?\n\
                  '---l--': Locked?\n\
                  '----u-': pUrgeable [Unloadable]?\n\
                  '-----s': load into System heap?\n\
        %f:     the name of the file containing the resource\n\
        %i:     the resource's ID\n\
        %n:     the resource's name\n\
        %t:     the resource's type\n\
        %%:     a percent sign\n\
      FORMAT can also be one of the following values, which produce output\n\
      filenames like these examples:\n\
        std:    OutDir/Folder1/FileA_snd_128_Name.wav (this is the default)\n\
        dirs:   OutDir/Folder1/FileA/snd_128_Name.wav\n\
        tdirs:  OutDir/Folder1/FileA/snd/128_Name.wav\n\
        t1:     OutDir/snd/Folder1/FileA_128_Name.wav\n\
        t1dirs: OutDir/snd/Folder1/FileA/128_Name.wav\n\
      When using the tdirs, t1dirs or similar custom formats, any generated JSON\n\
      files from SONG resources will not play with smssynth unless you manually put\n\
      the required sound and MIDI resources in the same directory as the SONG JSON\n\
      after decoding.\n\
\n\
Resource-type specific options:\n\
  --icon-family-format=bmp,icns\n\
      Export icon families (icl8, ICN# etc) in one or several formats. A comma-\n\
      separated list of one or more of:\n\
        bmp:    Save each icon of the family as a separate .bmp file\n\
        icns:   Save all icons of the family together in a single .icns file\n\
\n\
Resource file modification options:\n\
  --create\n\
      Create a new resource map instead of modifying or disassembling an\n\
      existing one. When this option is given, no input filename is required,\n\
      but an output filename is required.\n\
  --add-resource=TYPE:ID[+FLAGS[/NAME]]@FILENAME\n\
      Add this resource to the input file, and save the resulting resource map\n\
      in the output file. If a resource with the given type and ID already\n\
      exists, it is replaced with the new resource.\n\
  --delete-resource=TYPE:ID\n\
      Delete this resource in the output file.\n\
  --data-fork\n\
      Read the input file\'s data fork as if it were the resource fork.\n\
  --output-data-fork\n\
      Write the output file\'s data fork as if it were the resource fork.\n\
\n", stderr);
}

static uint32_t parse_cli_type(const char* str, char end_char = '\0', size_t* num_chars_consumed = nullptr) {
  union {
    uint8_t bytes[4];
    be_uint32_t type;
  } dest;
  dest.type = 0x20202020;

  size_t src_offset = 0;
  size_t dest_offset = 0;
  while ((dest_offset < 4) && str[src_offset] && (str[src_offset] != end_char)) {
    if (str[src_offset] == '%') {
      src_offset++;
      uint8_t value = value_for_hex_char(str[src_offset++]) << 4;
      value |= value_for_hex_char(str[src_offset++]);
      dest.bytes[dest_offset++] = value;
    } else {
      dest.bytes[dest_offset++] = str[src_offset++];
    }
  }

  if (num_chars_consumed) {
    *num_chars_consumed = src_offset;
  }

  return dest.type;
}

int main(int argc, char* argv[]) {
  signal(SIGPIPE, SIG_IGN);

  try {
    struct ModificationOperation {
      enum class Type {
        ADD = 0,
        DELETE,
        CHANGE_ID,
        RENAME,
      };
      Type op_type;
      uint32_t res_type;
      int16_t res_id;
      int16_t new_res_id; // Only used for CHANGE_ID
      uint8_t res_flags; // Only used for ADD
      std::string res_name; // Only used for ADD and RENAME
      std::string filename; // Only used for ADD

      ModificationOperation()
        : op_type(Type::ADD),
          res_type(0),
          res_id(0),
          new_res_id(0),
          res_flags(0) { }
    };

    ResourceExporter exporter;
    string filename;
    string out_dir;
    vector<ModificationOperation> modifications;
    ResourceFile::Resource single_resource;
    bool decode_pict_file = false;
    bool modify_resource_map = false;
    bool parse_data = false;
    bool create_resource_map = false;
    bool use_output_data_fork = false; // Only used if modify_resource_map == true
    for (int x = 1; x < argc; x++) {
      if (argv[x][0] == '-') {
        if (!strcmp(argv[x], "--index-format=resource-fork")) {
          exporter.set_index_format(IndexFormat::RESOURCE_FORK);
        } else if (!strcmp(argv[x], "--index-format=mohawk")) {
          exporter.set_index_format(IndexFormat::MOHAWK);
          exporter.use_data_fork = true;
        } else if (!strcmp(argv[x], "--index-format=hirf")) {
          exporter.set_index_format(IndexFormat::HIRF);
          exporter.use_data_fork = true;
        } else if (!strcmp(argv[x], "--index-format=dc-data")) {
          exporter.set_index_format(IndexFormat::DC_DATA);
          exporter.use_data_fork = true;

        } else if (!strcmp(argv[x], "--decode-pict-file")) {
          decode_pict_file = true;
          single_resource.type = RESOURCE_TYPE_PICT;
          single_resource.id = 1;
          single_resource.flags = 0;
          single_resource.name = "";

        } else if (!strncmp(argv[x], "--decode-single-resource=", 25)) {
          auto tokens = split(&argv[x][25], ':');
          if (tokens.size() == 0) {
            throw invalid_argument("--decode-single-resource needs a value");
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

        } else if (!strcmp(argv[x], "--create")) {
          modify_resource_map = true;
          create_resource_map = true;

        } else if (!strncmp(argv[x], "--add-resource=", 15)) {
          modify_resource_map = true;
          char* input = &argv[x][15];
          size_t type_chars;
          ModificationOperation op;
          op.op_type = ModificationOperation::Type::ADD;
          op.res_type = parse_cli_type(input, ':', &type_chars);
          if (input[type_chars] != ':') {
            throw invalid_argument("invalid argument to --add-resource");
          }
          input += (type_chars + 1);
          op.res_id = strtol(input, &input, 0);
          if (*input == '+') {
            op.res_flags = strtol(input, &input, 16);
          }
          if (*input == '/') {
            char* name_end = strchr(input, '@');
            if (name_end) {
              op.res_name.assign(input + 1, (name_end - input) - 1);
              input = name_end;
            } else {
              op.res_name = input;
              input += op.res_name.size();
            }
          }
          if (*input == '@') {
            op.filename = input + 1;
            input += op.filename.size() + 1;
          }
          if (*input) {
            throw invalid_argument("unparsed data in --add-resource command");
          }
          modifications.emplace_back(move(op));

        } else if (!strncmp(argv[x], "--delete-resource=", 18)) {
          modify_resource_map = true;
          auto tokens = split(&argv[x][18], ':');
          if (tokens.size() != 2) {
            throw invalid_argument("--delete-resource argument must be TYPE:ID");
          }
          ModificationOperation op;
          op.op_type = ModificationOperation::Type::DELETE;
          op.res_type = parse_cli_type(tokens[0].c_str());
          op.res_id = stol(tokens[1]);
          modifications.emplace_back(move(op));

        // TODO: Implement some more modification options. Specifically:
        // --change-resource-id=TYPE:OLDID:NEWID
        // --rename-resource=TYPE:ID:NAME
        // --rename-resource=TYPE:ID
        // The implementations should already be correct; we just need the CLI
        // option parsers here.

        } else if (!strcmp(argv[x], "--parse-data")) {
          parse_data = true;

        } else if (!strncmp(argv[x], "--copy-handler=", 15)) {
          const char* input = &argv[x][15];
          size_t type_chars;
          uint32_t from_type = parse_cli_type(input, ':', &type_chars);
          if (input[type_chars] != ':') {
            throw invalid_argument("invalid argument to --copy-handler");
          }
          uint32_t to_type = parse_cli_type(&input[type_chars + 1]);
          exporter.set_decoder_alias(from_type, to_type);

        } else if (!strcmp(argv[x], "--skip-external-decoders")) {
          exporter.disable_external_decoders();

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
            throw invalid_argument(string_printf("invalid target: %s", &argv[x][9]));
          }

        } else if (!strncmp(argv[x], "--target-name=", 14)) {
          exporter.target_names.emplace(&argv[x][14]);
        } else if (!strncmp(argv[x], "--skip-name=", 12)) {
          exporter.skip_names.emplace(&argv[x][12]);

        } else if (!strcmp(argv[x], "--skip-decode")) {
          exporter.disable_all_decoders();
          exporter.skip_templates = true;

        } else if (!strcmp(argv[x], "--save-raw=no")) {
          exporter.save_raw = ResourceExporter::SaveRawBehavior::NEVER;
        } else if (!strcmp(argv[x], "--save-raw=if-decode-fails")) {
          exporter.save_raw = ResourceExporter::SaveRawBehavior::IF_DECODE_FAILS;
        } else if (!strcmp(argv[x], "--save-raw=yes") || !strcmp(argv[x], "--save-raw")) {
          exporter.save_raw = ResourceExporter::SaveRawBehavior::ALWAYS;

        } else if (!strcmp(argv[x], "--filename-format=std")) {
          exporter.filename_format = FILENAME_FORMAT_STANDARD;
        } else if (!strcmp(argv[x], "--filename-format=dirs")) {
          exporter.filename_format = FILENAME_FORMAT_STANDARD_DIRS;
        } else if (!strcmp(argv[x], "--filename-format=tdirs")) {
          exporter.filename_format = FILENAME_FORMAT_STANDARD_TYPE_DIRS;
        } else if (!strcmp(argv[x], "--filename-format=t1")) {
          exporter.filename_format = FILENAME_FORMAT_TYPE_FIRST;
        } else if (!strcmp(argv[x], "--filename-format=t1dirs")) {
          exporter.filename_format = FILENAME_FORMAT_TYPE_FIRST_DIRS;
        } else if (!strncmp(argv[x], "--filename-format=", 18)) {
          exporter.filename_format = &argv[x][18];
      
        } else if (!strncmp(argv[x], "--icon-family-format=", 21)) {
          auto formats = split(&argv[x][21], ',');
          if (formats.size() == 0) {
            throw invalid_argument("--icon-family-format needs a value");
          }
          exporter.export_icon_family_as_bmp = false;
          exporter.export_icon_family_as_icns = false;
          for (const auto& format : formats) {
            if (format == "bmp") {
              exporter.export_icon_family_as_bmp = true;
            }
            else if (format == "icns") {
              exporter.export_icon_family_as_icns = true;
            }
            else {
              throw invalid_argument("invalid value for --icon-family-format");
            }
          }
      
        } else if (!strcmp(argv[x], "--data-fork")) {
          exporter.use_data_fork = true;
        } else if (!strcmp(argv[x], "--output-data-fork")) {
          use_output_data_fork = true;

        } else if (!strcmp(argv[x], "--target-compressed")) {
          exporter.target_compressed_behavior = ResourceExporter::TargetCompressedBehavior::TARGET;
        } else if (!strcmp(argv[x], "--skip-compressed")) {
          exporter.target_compressed_behavior = ResourceExporter::TargetCompressedBehavior::SKIP;

        } else if (!strcmp(argv[x], "--skip-templates")) {
          exporter.skip_templates = true;

        } else if (!strcmp(argv[x], "--skip-decompression")) {
          exporter.decompress_flags |= DecompressionFlag::DISABLED;

        } else if (!strcmp(argv[x], "--verbose-decompression")) {
          exporter.decompress_flags |= DecompressionFlag::VERBOSE;
        } else if (!strcmp(argv[x], "--trace-decompression")) {
          exporter.decompress_flags |= DecompressionFlag::TRACE_EXECUTION;
        } else if (!strcmp(argv[x], "--debug-decompression")) {
          exporter.decompress_flags |= DecompressionFlag::DEBUG_EXECUTION;

        } else if (!strcmp(argv[x], "--skip-file-dcmp")) {
          exporter.decompress_flags |= DecompressionFlag::SKIP_FILE_DCMP;
        } else if (!strcmp(argv[x], "--skip-file-ncmp")) {
          exporter.decompress_flags |= DecompressionFlag::SKIP_FILE_NCMP;
        } else if (!strcmp(argv[x], "--skip-native-dcmp")) {
          exporter.decompress_flags |= DecompressionFlag::SKIP_NATIVE;
        } else if (!strcmp(argv[x], "--skip-system-dcmp")) {
          exporter.decompress_flags |= DecompressionFlag::SKIP_SYSTEM_DCMP;
        } else if (!strcmp(argv[x], "--skip-system-ncmp")) {
          exporter.decompress_flags |= DecompressionFlag::SKIP_SYSTEM_NCMP;

        } else {
          throw invalid_argument(string_printf("unknown option: %s", argv[x]));
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

    if (modify_resource_map && modifications.empty() && !create_resource_map) {
      throw runtime_error("multiple incompatible modes were specified");
    }

    if (!modify_resource_map) {
      if (filename.empty()) {
        print_usage();
        return 1;
      }

      if (single_resource.type) {
        exporter.save_raw = ResourceExporter::SaveRawBehavior::NEVER;
        exporter.target_types.clear();
        exporter.target_ids.clear();
        exporter.target_names.clear();
        exporter.target_compressed_behavior = ResourceExporter::TargetCompressedBehavior::DEFAULT;
        exporter.use_data_fork = false;

        single_resource.data = load_file(filename);
        if (parse_data) {
          single_resource.data = parse_data_string(single_resource.data);
        }
        if (decode_pict_file) {
          single_resource.data = single_resource.data.substr(0x200);
        }

        uint32_t type = single_resource.type;
        int16_t id = single_resource.id;
        ResourceFile rf;
        rf.add(move(single_resource));

        size_t last_slash_pos = filename.rfind('/');
        string base_filename = (last_slash_pos == string::npos) ? filename :
            filename.substr(last_slash_pos + 1);

        const auto& res = rf.get_resource(type, id, exporter.decompress_flags);
        return exporter.export_resource(filename, res) ? 0 : 3;

      } else {
        if (out_dir.empty()) {
          out_dir = filename + ".out";
        }
        mkdir(out_dir.c_str(), 0777);
        if (!exporter.disassemble(filename, out_dir)) {
          return 3;
        } else {
          return 0;
        }
      }

    } else { // modify_resource_map == true
      if (filename.empty()) {
        print_usage();
        return 1;
      }

      string input_data;
      if (!create_resource_map) {
        string input_filename;
        if (exporter.use_data_fork) {
          input_filename = filename;
        } else if (isfile(filename + RESOURCE_FORK_FILENAME_SUFFIX)) {
          input_filename = filename + RESOURCE_FORK_FILENAME_SUFFIX;
        } else if (isfile(filename + RESOURCE_FORK_FILENAME_SHORT_SUFFIX)) {
          input_filename = filename + RESOURCE_FORK_FILENAME_SHORT_SUFFIX;
        }
        input_data = load_file(input_filename);

        if (out_dir.empty()) {
          out_dir = filename + ".out";
        }

      } else {
        if (!out_dir.empty()) {
          throw invalid_argument("only an output filename should be given if creating a resource map");
        }
        out_dir = filename;
      }

      fprintf(stderr, "... (load input) %zu bytes\n", input_data.size());

      ResourceFile rf = parse_resource_fork(input_data);
      for (const auto& op : modifications) {
        string type_str = string_for_resource_type(op.res_type);
        switch (op.op_type) {
          case ModificationOperation::Type::ADD: {
            ResourceFile::Resource res;
            res.type = op.res_type;
            res.id = op.res_id;
            res.flags = op.res_flags;
            res.name = op.res_name;
            res.data = load_file(op.filename);
            size_t data_bytes = res.data.size();
            if (!rf.add(move(res))) {
              throw runtime_error("cannot add resource");
            }
            fprintf(stderr, "... (add) %s:%hd flags=%02hhX name=\"%s\" data=\"%s\" (%zu bytes) OK\n",
                type_str.c_str(), op.res_id, op.res_flags, op.res_name.c_str(), op.filename.c_str(), data_bytes);
            break;
          }
          case ModificationOperation::Type::DELETE:
            if (!rf.remove(op.res_type, op.res_id)) {
              throw runtime_error("cannot delete resource");
            }
            fprintf(stderr, "... (delete) %s:%hd OK\n", type_str.c_str(), op.res_id);
            break;
          case ModificationOperation::Type::CHANGE_ID:
            if (!rf.change_id(op.res_type, op.res_id, op.new_res_id)) {
              throw runtime_error("cannot change resource id");
            }
            fprintf(stderr, "... (change id) %s:%hd=>%hd OK\n", type_str.c_str(),
                op.res_id, op.new_res_id);
            break;
          case ModificationOperation::Type::RENAME:
            if (!rf.rename(op.res_type, op.res_id, op.res_name)) {
              throw runtime_error("cannot rename resource");
            }
            fprintf(stderr, "... (rename) %s:%hd=>\"%s\" OK\n", type_str.c_str(),
                op.res_id, op.res_name.c_str());
            break;
          default:
            throw logic_error("invalid modification operation");
        }
      }

      if (!use_output_data_fork) {
        out_dir += RESOURCE_FORK_FILENAME_SUFFIX;
      }

      string output_data = serialize_resource_fork(rf);
      fprintf(stderr, "... (serialize output) %zu bytes\n", output_data.size());

      // Attempting to open the resource fork of a nonexistent file will fail
      // without creating the file, so if we're writing to a resource fork, we
      // touch the file first to make sure it will exist when we write the output.
      if (ends_with(out_dir, RESOURCE_FORK_FILENAME_SUFFIX)) {
        fopen_unique(out_dir.substr(0, out_dir.size() - RESOURCE_FORK_FILENAME_SUFFIX.size()), "a+");
      } else if (ends_with(out_dir, RESOURCE_FORK_FILENAME_SHORT_SUFFIX)) {
        fopen_unique(out_dir.substr(0, out_dir.size() - RESOURCE_FORK_FILENAME_SHORT_SUFFIX.size()), "a+");
      }
      save_file(out_dir, output_data);

      return 0;
    }
  }
  catch (const std::exception& e) {
    fprintf(stderr, "Error: %s\n", e.what());
    return 1;
  }
}
