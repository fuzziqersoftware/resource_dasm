#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/JSON.hh>
#include <phosg/Platform.hh>
#include <phosg/Process.hh>
#include <phosg/Strings.hh>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Cli.hh"
#include "Emulators/M68KEmulator.hh"
#include "Emulators/PPC32Emulator.hh"
#include "Emulators/X86Emulator.hh"
#include "ExecutableFormats/DOLFile.hh"
#include "ExecutableFormats/ELFFile.hh"
#include "ExecutableFormats/PEFFile.hh"
#include "ExecutableFormats/PEFile.hh"
#include "ExecutableFormats/RELFile.hh"
#include "ImageSaver.hh"
#include "IndexFormats/Formats.hh"
#include "Lookups.hh"
#include "ResourceCompression.hh"
#include "ResourceFile.hh"
#include "ResourceIDs.hh"
#include "SystemDecompressors.hh"
#include "SystemTemplates.hh"
#include "TextCodecs.hh"

using namespace std;
using namespace phosg;
using namespace ResourceDASM;

static const string RESOURCE_FORK_FILENAME_SUFFIX = "/..namedfork/rsrc";
static const string RESOURCE_FORK_FILENAME_SHORT_SUFFIX = "/rsrc";

static constexpr char FILENAME_FORMAT_STANDARD[] = "%f_%t_%i%n";
static constexpr char FILENAME_FORMAT_STANDARD_HEX[] = "%f_%T_%i%n";
static constexpr char FILENAME_FORMAT_STANDARD_DIRS[] = "%f/%t_%i%n";
static constexpr char FILENAME_FORMAT_STANDARD_TYPE_DIRS[] = "%f/%t/%i%ns";
static constexpr char FILENAME_FORMAT_TYPE_FIRST[] = "%t/%f_%i%n";
static constexpr char FILENAME_FORMAT_TYPE_FIRST_DIRS[] = "%t/%f/%i%n";

static string disassembly_for_dcmp(
    const ResourceFile::DecodedDecompressorResource& dcmp) {
  multimap<uint32_t, string> labels;
  if (dcmp.init_label >= 0) {
    labels.emplace(dcmp.init_label, "init");
  }
  if (dcmp.decompress_label >= 0) {
    labels.emplace(dcmp.decompress_label, "decompress");
  }
  if (dcmp.exit_label >= 0) {
    labels.emplace(dcmp.exit_label, "exit");
  }
  return M68KEmulator::disassemble(
      dcmp.code.data(), dcmp.code.size(), dcmp.pc_offset, &labels);
}

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
      if (dir != "/" && !std::filesystem::is_directory(dir)) {
        std::filesystem::create_directories(dir);
      }
    }
  }

  string output_filename(
      const string& base_filename,
      const uint32_t* res_type,
      const int16_t* res_id,
      const string& res_type_str,
      const string& res_name,
      uint16_t res_flags,
      const string& after) {
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

    string result = base_out_dir_str;
    bool saw_percent = false;
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
              result += std::format("{}", *res_id);
            }
            break;

          case 'n':
            result += res_name;
            break;

          case 't':
            result += res_type_str;
            break;

          case 'T':
            if (res_type) {
              result += std::format("{:08X}", *res_type);
            }
            break;

          default:
            throw runtime_error(std::format("unimplemented escape '{}' in filename format", ch));
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
      const string& after) {
    if (base_filename.empty()) {
      return out_dir;
    }

    string type_str = string_for_resource_type(res->type, /*for_filename=*/true);

    // If the type ends with spaces (e.g. 'snd '), trim them off
    strip_trailing_whitespace(type_str);

    string name_token;
    if (!res->name.empty()) {
      name_token = '_' + decode_mac_roman(res->name, /*for_filename=*/true);
    }

    return output_filename(
        base_filename,
        &res->type,
        &res->id,
        type_str,
        name_token,
        res->flags,
        after);
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
    fwrite_fmt(stderr, "... {}\n", filename);
  }

  void write_decoded_image(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res,
      const string& after,
      const Image& img) {
    string filename = this->output_filename(base_filename, res, after);
    this->ensure_directories_exist(filename);
    filename = this->image_saver.save_image(img, filename);
    fwrite_fmt(stderr, "... {}\n", filename);
  }

  void write_decoded_TMPL(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_TMPL(res);
    string data = ResourceFile::describe_template(decoded);
    this->write_decoded_data(base_filename, res, ".txt", data);
  }

  void write_decoded_CURS(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_CURS(res);
    string after = std::format("_{}_{}", decoded.hotspot_x, decoded.hotspot_y);
    this->write_decoded_image(base_filename, res, after, decoded.bitmap);
  }

  void write_decoded_crsr(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_crsr(res);
    string bitmap_after = std::format("_{}_{}_bitmap", decoded.hotspot_x, decoded.hotspot_y);
    string after = std::format("_{}_{}", decoded.hotspot_x, decoded.hotspot_y);
    this->write_decoded_image(base_filename, res, bitmap_after, decoded.bitmap);
    this->write_decoded_image(base_filename, res, after, decoded.image);
  }

  void write_decoded_ppat(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_ppat(res);

    Image tiled = tile_image(decoded.pattern, 8, 8);
    this->write_decoded_image(base_filename, res, "", decoded.pattern);
    this->write_decoded_image(base_filename, res, "_tiled", tiled);

    tiled = tile_image(decoded.monochrome_pattern, 8, 8);
    this->write_decoded_image(base_filename, res, "_bitmap", decoded.monochrome_pattern);
    this->write_decoded_image(base_filename, res, "_bitmap_tiled", tiled);
  }

  void write_decoded_pptN(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_pptN(res);

    for (size_t x = 0; x < decoded.size(); x++) {
      string after = std::format("_{}", x);
      this->write_decoded_image(base_filename, res, after, decoded[x].pattern);

      Image tiled = tile_image(decoded[x].pattern, 8, 8);
      after = std::format("_{}_tiled", x);
      this->write_decoded_image(base_filename, res, after, tiled);

      after = std::format("_{}_bitmap", x);
      this->write_decoded_image(base_filename, res, after, decoded[x].monochrome_pattern);

      tiled = tile_image(decoded[x].monochrome_pattern, 8, 8);
      after = std::format("_{}_bitmap_tiled", x);
      this->write_decoded_image(base_filename, res, after, tiled);
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
      this->write_decoded_image(base_filename, res, "", img);

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
          } catch (const out_of_range&) {
          }
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

        img.draw_text(x, y, &width, nullptr, 0xFF0000FF, 0x00000000, "{:04X}",
            decoded[z].c.r);
        x += width;

        img.draw_text(x, y, &width, nullptr, 0x00FF00FF, 0x00000000, "{:04X}", decoded[z].c.g);
        x += width;

        img.draw_text(x, y, &width, nullptr, 0x0000FFFF, 0x00000000, "{:04X}", decoded[z].c.b);
        x += width;

        const char* name = nullptr;
        if (index_names) {
          try {
            name = index_names->at(decoded[z].color_num).c_str();
          } catch (const out_of_range&) {
          }
        }

        if (name) {
          img.draw_text(x, y, &width, nullptr, 0xFFFFFFFF, 0x00000000, " ({})", name);
        } else {
          img.draw_text(x, y, &width, nullptr, 0xFFFFFFFF, 0x00000000, " ({})", decoded[z].color_num);
        }
        x += width;
      }
      this->write_decoded_image(base_filename, res, "", img);
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
      StringWriter data;
      for (size_t z = 0; z < decoded.size(); z++) {
        data.put_u8(decoded[z].c.r / 0x0101);
        data.put_u8(decoded[z].c.g / 0x0101);
        data.put_u8(decoded[z].c.b / 0x0101);
      }
      data.extend_to(768, '\0');
      data.put_u16b(decoded.size());
      // No transparent color
      data.put_u16b(0xFFFF);

      this->write_decoded_data(base_filename, res, ".act", data.str());
    }
  }

  void write_decoded_pltt(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
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
    } catch (const out_of_range&) {
    }

    // These resources are all the same format, so it's ok to call decode_clut
    // here instead of the type-specific functions
    this->write_decoded_data(base_filename, res, this->current_rf->decode_clut(res),
        index_names);
  }

  void write_decoded_CTBL(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    // Always write the raw for this resource type because some tools demand it
    this->write_decoded_data(base_filename, res, ".bin", res->data);
    this->write_decoded_data(base_filename, res, this->current_rf->decode_CTBL(res));
  }

  void write_decoded_PAT(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    Image decoded = this->current_rf->decode_PAT(res);

    Image tiled = tile_image(decoded, 8, 8);
    this->write_decoded_image(base_filename, res, "", decoded);
    this->write_decoded_image(base_filename, res, "_tiled", tiled);
  }

  void write_decoded_PATN(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_PATN(res);

    for (size_t x = 0; x < decoded.size(); x++) {
      string after = std::format("_{}", x);
      this->write_decoded_image(base_filename, res, after, decoded[x]);

      Image tiled = tile_image(decoded[x], 8, 8);
      after = std::format("_{}_tiled", x);
      this->write_decoded_image(base_filename, res, after, tiled);
    }
  }

  void write_decoded_SICN(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_SICN(res);

    for (size_t x = 0; x < decoded.size(); x++) {
      string after = std::format("_{}", x);
      this->write_decoded_image(base_filename, res, after, decoded[x]);
    }
  }

  void write_decoded_data(
      const string& base_filename,
      shared_ptr<const ResourceFile::Resource> res,
      const ResourceFile::DecodedIconListResource& decoded) {
    if (!decoded.composite.empty()) {
      this->write_decoded_image(base_filename, res, "", decoded.composite);
    } else if (!decoded.images.empty()) {
      for (size_t x = 0; x < decoded.images.size(); x++) {
        this->write_decoded_image(base_filename, res, std::format("_{}", x), decoded.images[x]);
      }
    } else {
      throw logic_error("decoded icon list contains neither composite nor images");
    }
  }

  shared_ptr<const ResourceFile::Resource> load_family_icon(const shared_ptr<const ResourceFile::Resource>& icon, uint32_t type) {
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

  void write_icns(const string& base_filename, const shared_ptr<const ResourceFile::Resource>& icon) {
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

  void write_decoded_ICNN(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    if (export_icon_family_as_image) {
      auto decoded = this->current_rf->decode_ICNN(res);
      this->write_decoded_data(base_filename, res, decoded);
    }
    if (export_icon_family_as_icns) {
      this->write_icns(base_filename, res);
    }
  }

  void write_decoded_icmN(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_icmN(res);
    this->write_decoded_data(base_filename, res, decoded);
  }

  void write_decoded_icsN(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    if (export_icon_family_as_image) {
      auto decoded = this->current_rf->decode_icsN(res);
      this->write_decoded_data(base_filename, res, decoded);
    }
    if (export_icon_family_as_icns) {
      this->write_icns(base_filename, res);
    }
  }

  void write_decoded_kcsN(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_kcsN(res);
    this->write_decoded_data(base_filename, res, decoded);
  }

  void write_decoded_cicn(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_cicn(res);

    this->write_decoded_image(base_filename, res, "", decoded.image);

    if (decoded.bitmap.get_width() && decoded.bitmap.get_height()) {
      this->write_decoded_image(base_filename, res, "_bitmap", decoded.bitmap);
    }
  }

  void write_decoded_icl8(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    if (export_icon_family_as_image) {
      auto decoded = this->current_rf->decode_icl8(res);
      this->write_decoded_image(base_filename, res, "", decoded);
    }
    if (export_icon_family_as_icns) {
      this->write_icns(base_filename, res);
    }
  }

  void write_decoded_icm8(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_icm8(res);
    this->write_decoded_image(base_filename, res, "", decoded);
  }

  void write_decoded_ics8(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    if (export_icon_family_as_image) {
      auto decoded = this->current_rf->decode_ics8(res);
      this->write_decoded_image(base_filename, res, "", decoded);
    }
    if (export_icon_family_as_icns) {
      this->write_icns(base_filename, res);
    }
  }

  void write_decoded_kcs8(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_kcs8(res);
    this->write_decoded_image(base_filename, res, "", decoded);
  }

  void write_decoded_icl4(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    if (export_icon_family_as_image) {
      auto decoded = this->current_rf->decode_icl4(res);
      this->write_decoded_image(base_filename, res, "", decoded);
    }
    if (export_icon_family_as_icns) {
      this->write_icns(base_filename, res);
    }
  }

  void write_decoded_icm4(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_icm4(res);
    this->write_decoded_image(base_filename, res, "", decoded);
  }

  void write_decoded_ics4(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    if (export_icon_family_as_image) {
      auto decoded = this->current_rf->decode_ics4(res);
      this->write_decoded_image(base_filename, res, "", decoded);
    }
    if (export_icon_family_as_icns) {
      this->write_icns(base_filename, res);
    }
  }

  void write_decoded_kcs4(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_kcs4(res);
    this->write_decoded_image(base_filename, res, "", decoded);
  }

  void write_decoded_ICON(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_ICON(res);
    this->write_decoded_image(base_filename, res, "", decoded);
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
      string after = std::format("{}_{}.{}", filename_suffix, type_str, file_index++);
      this->write_decoded_image(base_filename, res, after, it.second);
    }
    for (const auto& it : decoded.type_to_composite_image) {
      string type_str = string_for_resource_type(it.first);
      string after = std::format("{}_{}_composite.{}", filename_suffix, type_str, file_index++);
      this->write_decoded_image(base_filename, res, after, it.second);
    }
    for (const auto& it : decoded.type_to_jpeg2000_data) {
      string type_str = string_for_resource_type(it.first);
      string after = std::format("{}_{}.{}.jp2", filename_suffix, type_str, file_index++);
      this->write_decoded_data(base_filename, res, after, it.second);
    }
    for (const auto& it : decoded.type_to_png_data) {
      string type_str = string_for_resource_type(it.first);
      string after = std::format("{}_{}.{}.png", filename_suffix, type_str, file_index++);
      this->write_decoded_data(base_filename, res, after, it.second);
    }
    if (!decoded.toc_data.empty()) {
      string after = std::format("{}.toc.bin", filename_suffix);
      this->write_decoded_data(base_filename, res, after, decoded.toc_data);
    }
    if (!decoded.name.empty()) {
      string after = std::format("{}.name.txt", filename_suffix);
      this->write_decoded_data(base_filename, res, after, decoded.name);
    }
    if (!decoded.info_plist.empty()) {
      string after = std::format("{}.info.plist", filename_suffix);
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

  void write_decoded_icns(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_icns(res);
    this->write_decoded_icns(base_filename, res, decoded);
  }

  void write_decoded_PICT_internal(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_PICT(res, false);
    if (!decoded.embedded_image_data.empty()) {
      this->write_decoded_data(base_filename, res, "." + decoded.embedded_image_format, decoded.embedded_image_data);
    } else {
      this->write_decoded_image(base_filename, res, "", decoded.image);
    }
  }

  void write_decoded_PICT(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_PICT(res);
    if (!decoded.embedded_image_data.empty()) {
      this->write_decoded_data(base_filename, res, "." + decoded.embedded_image_format, decoded.embedded_image_data);
    } else {
      this->write_decoded_image(base_filename, res, "", decoded.image);
    }
  }

  void write_decoded_snd(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_snd(res);
    this->write_decoded_data(base_filename, res,
        decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
  }

  void write_decoded_csnd(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_csnd(res);
    this->write_decoded_data(base_filename, res,
        decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
  }

  void write_decoded_esnd(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_esnd(res);
    this->write_decoded_data(base_filename, res,
        decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
  }

  void write_decoded_ESnd(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_ESnd(res);
    this->write_decoded_data(base_filename, res,
        decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
  }

  void write_decoded_Ysnd(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_Ysnd(res);
    this->write_decoded_data(base_filename, res,
        decoded.is_mp3 ? ".mp3" : ".wav", decoded.data);
  }

  void write_decoded_SMSD(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    string decoded = this->current_rf->decode_SMSD(res);
    this->write_decoded_data(base_filename, res, ".wav", decoded);
  }

  void write_decoded_SOUN(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    string decoded = this->current_rf->decode_SOUN(res);
    this->write_decoded_data(base_filename, res, ".wav", decoded);
  }

  void write_decoded_cmid(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    string decoded = this->current_rf->decode_cmid(res);
    this->write_decoded_data(base_filename, res, ".midi", decoded);
  }

  void write_decoded_emid(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    string decoded = this->current_rf->decode_emid(res);
    this->write_decoded_data(base_filename, res, ".midi", decoded);
  }

  void write_decoded_ecmi(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    string decoded = this->current_rf->decode_ecmi(res);
    this->write_decoded_data(base_filename, res, ".midi", decoded);
  }

  void write_decoded_FONT_NFNT(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_FONT(res);

    {
      string description_filename = this->output_filename(base_filename, res, "_description.txt");
      this->ensure_directories_exist(description_filename);
      auto f = fopen_unique(description_filename, "wt");
      fwrite_fmt(f.get(), "\
# source_bit_depth = {} ({} color table)\n\
# dynamic: {}\n\
# has non-black colors: {}\n\
# fixed-width: {}\n\
# character range: {:02X} - {:02X}\n\
# maximum width: {}\n\
# maximum kerning: {}\n\
# rectangle: {} x {}\n\
# maximum ascent: {}\n\
# maximum descent: {}\n\
# leading: {}\n",
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
          fwrite_fmt(f.get(), "\n# glyph {:02X} ({})\n", glyph.ch, glyph.ch);
        } else {
          fwrite_fmt(f.get(), "\n# glyph {:02X}\n", glyph.ch);
        }
        fwrite_fmt(f.get(), "#   bitmap offset: {}; width: {}\n", glyph.bitmap_offset, glyph.bitmap_width);
        fwrite_fmt(f.get(), "#   character offset: {}; width: {}\n", glyph.offset, glyph.width);
      }

      fwrite_fmt(f.get(), "\n# missing glyph\n");
      fwrite_fmt(f.get(), "#   bitmap offset: {}; width: {}\n", decoded.missing_glyph.bitmap_offset, decoded.missing_glyph.bitmap_width);
      fwrite_fmt(f.get(), "#   character offset: {}; width: {}\n", decoded.missing_glyph.offset, decoded.missing_glyph.width);

      fwrite_fmt(stderr, "... {}\n", description_filename);
    }

    this->write_decoded_image(base_filename, res, "_all_glyphs", decoded.full_bitmap.to_color(0xFFFFFFFF, 0x000000FF, false));

    if (decoded.missing_glyph.img.get_width()) {
      this->write_decoded_image(base_filename, res, "_glyph_missing", decoded.missing_glyph.img);
    }
    for (size_t x = 0; x < decoded.glyphs.size(); x++) {
      if (!decoded.glyphs[x].img.get_width()) {
        continue;
      }
      string after = std::format("_glyph_{:02X}", decoded.first_char + x);
      this->write_decoded_image(base_filename, res, after, decoded.glyphs[x].img);
    }
  }

  string generate_text_for_cfrg(const vector<ResourceFile::DecodedCodeFragmentEntry>& entries) {
    string ret;
    for (size_t x = 0; x < entries.size(); x++) {
      const auto& entry = entries[x];

      string arch_str = string_for_resource_type(entry.architecture);
      string this_entry_ret;
      if (!entry.name.empty()) {
        this_entry_ret += std::format("fragment {}: \"{}\"\n", x, entry.name);
      } else {
        this_entry_ret += std::format("fragment {}: (unnamed)\n", x);
      }
      this_entry_ret += std::format("  architecture: 0x{:08X} ({})\n", entry.architecture, arch_str);
      this_entry_ret += std::format("  update_level: 0x{:02X}\n", entry.update_level);
      this_entry_ret += std::format("  current_version: 0x{:08X}\n", entry.current_version);
      this_entry_ret += std::format("  old_def_version: 0x{:08X}\n", entry.old_def_version);
      this_entry_ret += std::format("  app_stack_size: 0x{:08X}\n", entry.app_stack_size);
      this_entry_ret += std::format("  app_subdir_id/lib_flags: 0x{:04X}\n", entry.app_subdir_id);

      uint8_t usage = static_cast<uint8_t>(entry.usage);
      if (usage < 5) {
        static const char* names[5] = {
            "import library",
            "application",
            "drop-in addition",
            "stub library",
            "weak stub library",
        };
        this_entry_ret += std::format("  usage: 0x{:02X} ({})\n", usage, names[usage]);
      } else {
        this_entry_ret += std::format("  usage: 0x{:02X} (invalid)\n", usage);
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
        this_entry_ret += std::format("  where: 0x{:02X} ({})\n", where, names[where]);
      } else {
        this_entry_ret += std::format("  where: 0x{:02X} (invalid)\n", where);
      }

      if (entry.where == ResourceFile::DecodedCodeFragmentEntry::Where::RESOURCE) {
        string type_str = string_for_resource_type(entry.offset);
        this_entry_ret += std::format("  resource: 0x{:08X} ({}) #{}\n",
            entry.offset, type_str, static_cast<int32_t>(entry.length));
      } else {
        this_entry_ret += std::format("  offset: 0x{:08X}\n", entry.offset);
        if (entry.length == 0) {
          this_entry_ret += "  length: (entire contents)\n";
        } else {
          this_entry_ret += std::format("  length: 0x{:08X}\n", entry.length);
        }
      }
      this_entry_ret += std::format("  space_id/fork_kind: 0x{:08X}\n", entry.space_id);
      this_entry_ret += std::format("  fork_instance: 0x{:04X}\n", entry.fork_instance);
      if (!entry.extension_data.empty()) {
        this_entry_ret += std::format("  extension_data ({}): ", entry.extension_count);
        this_entry_ret += format_data_string(entry.extension_data);
        this_entry_ret += '\n';
      }

      ret += this_entry_ret;
    }

    return ret;
  }

  void write_decoded_cfrg(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    string description = generate_text_for_cfrg(this->current_rf->decode_cfrg(res));
    this->write_decoded_data(base_filename, res, ".txt", description);
  }

  void write_decoded_SIZE(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_SIZE(res);
    string disassembly = std::format("\
  # save_screen = {}\n\
  # accept_suspend_events = {}\n\
  # disable_option = {}\n\
  # can_background = {}\n\
  # activate_on_fg_switch = {}\n\
  # only_background = {}\n\
  # get_front_clicks = {}\n\
  # accept_died_events = {}\n\
  # clean_addressing = {}\n\
  # high_level_event_aware = {}\n\
  # local_and_remote_high_level_events = {}\n\
  # stationery_aware = {}\n\
  # use_text_edit_services = {}\n\
  # size = {:08X}\n\
  # min_size = {:08X}\n",
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

  void write_decoded_vers(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_vers(res);

    string dev_stage_str = std::format("0x{:02X}", decoded.development_stage);
    if (decoded.development_stage == 0x20) {
      dev_stage_str += " (development)";
    } else if (decoded.development_stage == 0x40) {
      dev_stage_str += " (alpha)";
    } else if (decoded.development_stage == 0x60) {
      dev_stage_str += " (beta)";
    } else if (decoded.development_stage == 0x80) {
      dev_stage_str += " (release)";
    }

    string region_code_str = std::format("0x{:04X}", decoded.region_code);
    const char* region_name = name_for_region_code(decoded.region_code);
    if (region_name) {
      region_code_str += " (";
      region_code_str += region_name;
      region_code_str += ")";
    }

    string version_str = std::format("{:c}{:c}.{:c}.{:c}",
        '0' + ((decoded.major_version >> 4) & 0x0F),
        '0' + (decoded.major_version & 0x0F),
        '0' + ((decoded.minor_version >> 4) & 0x0F),
        '0' + (decoded.minor_version & 0x0F));
    if (version_str.starts_with("0") && !version_str.starts_with("0.")) {
      version_str.erase(version_str.begin());
    }
    if (version_str.ends_with(".0")) {
      version_str.resize(version_str.size() - 2);
    }

    string disassembly = std::format("\
# major_version = {} (major=0x{:02X}, minor=0x{:02X})\n\
# development_stage = {}\n\
# prerelease_version_level = {}\n\
# region_code = {}\n\
# version_number = {}\n\
# version_message = {}\n",
        version_str,
        decoded.major_version,
        decoded.minor_version,
        dev_stage_str,
        decoded.prerelease_version_level,
        region_code_str,
        decoded.version_number,
        decoded.version_message);
    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_finf(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_finf(res);

    string disassembly;
    for (size_t x = 0; x < decoded.size(); x++) {
      const auto& finf = decoded[x];

      string font_id_str = std::format("{}", finf.font_id);
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

      disassembly += std::format("\
  # font info #{}\n\
  # font_id = {}\n\
  # style_flags = 0x{:04X} ({})\n\
  # size = {}\n\n",
          x,
          font_id_str,
          finf.style_flags,
          style_str,
          finf.size);
    }

    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_ROvN(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_ROvN(res);

    string disassembly = std::format("# ROM version: 0x{:04X}\n", decoded.rom_version);
    for (size_t x = 0; x < decoded.overrides.size(); x++) {
      const auto& override = decoded.overrides[x];
      string type_name = string_for_resource_type(override.type);
      disassembly += std::format("# override {}: {:08X} ({}) #{}\n",
          x, override.type, type_name, override.id);
    }

    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_CODE(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    string disassembly;
    if (res->id == 0) {
      auto decoded = this->current_rf->decode_CODE_0(res);
      disassembly += std::format("# above A5 size: 0x{:08X}\n", decoded.above_a5_size);
      disassembly += std::format("# below A5 size: 0x{:08X}\n", decoded.below_a5_size);
      for (size_t x = 0; x < decoded.jump_table.size(); x++) {
        const auto& e = decoded.jump_table[x];
        if (e.code_resource_id || e.offset) {
          disassembly += std::format("# export {} [A5 + 0x{:X}]: CODE {} offset 0x{:X} after header\n",
              x, 0x22 + (x * 8), e.code_resource_id, e.offset);
        }
      }

    } else {
      auto decoded = this->current_rf->decode_CODE(res);

      // Attempt to decode CODE 0 to get the jump table
      multimap<uint32_t, string> labels;
      vector<JumpTableEntry> jump_table;
      try {
        auto code0_data = this->current_rf->decode_CODE_0(static_cast<int16_t>(0), res->type);
        for (size_t x = 0; x < code0_data.jump_table.size(); x++) {
          const auto& e = code0_data.jump_table[x];
          if (e.code_resource_id == res->id) {
            labels.emplace(e.offset, std::format("export_{}", x));
          }
        }
        jump_table = std::move(code0_data.jump_table);
      } catch (const exception&) {
      }

      if (decoded.first_jump_table_entry < 0) {
        disassembly += "# far model CODE resource\n";
        disassembly += std::format("# near model jump table entries starting at A5 + 0x{:08X} ({} of them)\n",
            decoded.near_entry_start_a5_offset, decoded.near_entry_count);
        disassembly += std::format("# far model jump table entries starting at A5 + 0x{:08X} ({} of them)\n",
            decoded.far_entry_start_a5_offset, decoded.far_entry_count);
        disassembly += std::format("# A5 relocation data at 0x{:08X}\n", decoded.a5_relocation_data_offset);
        for (uint32_t addr : decoded.a5_relocation_addresses) {
          disassembly += std::format("#   A5 relocation at {:08X}\n", addr);
        }
        disassembly += std::format("# A5 is 0x{:08X}\n", decoded.a5);
        disassembly += std::format("# PC relocation data at 0x{:08X}\n", decoded.pc_relocation_data_offset);
        for (uint32_t addr : decoded.pc_relocation_addresses) {
          disassembly += std::format("#   PC relocation at {:08X}\n", addr);
        }
        disassembly += std::format("# load address is 0x{:08X}\n", decoded.load_address);
      } else {
        disassembly += "# near model CODE resource\n";
        if (decoded.num_jump_table_entries == 0) {
          disassembly += std::format("# this CODE claims to have no jump table entries (but starts at {:04X})\n", decoded.first_jump_table_entry);
        } else {
          disassembly += std::format("# jump table entries: {}-{} ({} of them)\n",
              decoded.first_jump_table_entry,
              decoded.first_jump_table_entry + decoded.num_jump_table_entries - 1,
              decoded.num_jump_table_entries);
        }
      }

      disassembly += M68KEmulator::disassemble(
          decoded.code.data(), decoded.code.size(), 0, &labels, true, &jump_table);
    }

    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_DRVR(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
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
      disassembly += std::format("# name: {}\n", decoded.name);
    }

    if (flags_str.empty()) {
      disassembly += std::format("# flags: 0x{:04X}\n", decoded.flags);
    } else {
      disassembly += std::format("# flags: 0x{:04X} ({})\n", decoded.flags, flags_str);
    }

    disassembly += std::format("# delay: {}\n", decoded.delay);
    disassembly += std::format("# event mask: 0x{:04X}\n", decoded.event_mask);
    disassembly += std::format("# menu id: {}\n", decoded.menu_id);

    multimap<uint32_t, string> labels;

    auto add_label = [&](uint16_t label, const char* name) {
      if (label == 0) {
        disassembly += std::format("# {} label: not set\n", name);
      } else {
        disassembly += std::format("# {} label: {:04X}\n", name, label);
        labels.emplace(label, name);
      }
    };
    add_label(decoded.open_label, "open");
    add_label(decoded.prime_label, "prime");
    add_label(decoded.control_label, "control");
    add_label(decoded.status_label, "status");
    add_label(decoded.close_label, "close");

    disassembly += M68KEmulator::disassemble(
        decoded.code.data(), decoded.code.size(), decoded.code_start_offset, &labels);

    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_RSSC(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_RSSC(res);

    multimap<uint32_t, string> labels;
    string disassembly;
    size_t function_count = sizeof(decoded.function_offsets) / sizeof(decoded.function_offsets[0]);
    for (size_t z = 0; z < function_count; z++) {
      if (decoded.function_offsets[z] == 0) {
        disassembly += std::format("# export_{} => (not set)\n", z);
      } else {
        disassembly += std::format("# export_{} => {:08X}\n", z, decoded.function_offsets[z]);
      }
      labels.emplace(decoded.function_offsets[z], std::format("export_{}", z));
    }
    disassembly += M68KEmulator::disassemble(
        decoded.code.data(), decoded.code.size(), 0x16, &labels);

    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_dcmp(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_dcmp(res);
    string disassembly = disassembly_for_dcmp(decoded);
    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_inline_68k(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    multimap<uint32_t, string> labels;
    labels.emplace(0, "start");
    string result = M68KEmulator::disassemble(res->data.data(), res->data.size(), 0,
        &labels);
    this->write_decoded_data(base_filename, res, ".txt", result);
  }

  void write_decoded_inline_ppc32(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    multimap<uint32_t, string> labels;
    labels.emplace(0, "start");
    string result = PPC32Emulator::disassemble(res->data.data(), res->data.size(),
        0, &labels);
    this->write_decoded_data(base_filename, res, ".txt", result);
  }

  void write_decoded_pef(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto pef = this->current_rf->decode_pef(res);
    string filename = this->output_filename(base_filename, res, ".txt");
    this->ensure_directories_exist(filename);
    auto f = fopen_unique(filename, "wt");
    pef.print(f.get());
    fwrite_fmt(stderr, "... {}\n", filename);
  }

  void write_decoded_expt_nsrd(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = (res->type == RESOURCE_TYPE_expt) ? this->current_rf->decode_expt(res) : this->current_rf->decode_nsrd(res);
    string filename = this->output_filename(base_filename, res, ".txt");
    this->ensure_directories_exist(filename);
    auto f = fopen_unique(filename, "wt");
    fputs("Mixed-mode manager header:\n", f.get());
    print_data(f.get(), decoded.header);
    fputc('\n', f.get());
    decoded.pef.print(f.get());
    fwrite_fmt(stderr, "... {}\n", filename);
  }

  void write_decoded_inline_68k_or_pef(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    if (res->data.size() < 4) {
      throw runtime_error("can\'t determine code type");
    }
    if (*reinterpret_cast<const be_uint32_t*>(res->data.data()) == 0x4A6F7921) { // Joy!
      write_decoded_pef(base_filename, res);
    } else {
      write_decoded_inline_68k(base_filename, res);
    }
  }

  void write_decoded_TEXT(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    this->write_decoded_data(base_filename, res, ".txt", this->current_rf->decode_TEXT(res));
  }

  void write_decoded_card(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    this->write_decoded_data(base_filename, res, ".txt", this->current_rf->decode_card(res));
  }

  void write_decoded_styl(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    this->write_decoded_data(base_filename, res, ".rtf", this->current_rf->decode_styl(res));
  }

  void write_decoded_STR(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_STR(res);

    this->write_decoded_data(base_filename, res, ".txt", decoded.str);
    if (!decoded.after_data.empty()) {
      this->write_decoded_data(base_filename, res, "_data.bin", decoded.after_data);
    }
  }

  void write_decoded_STRN(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_STRN(res);

    for (size_t x = 0; x < decoded.strs.size(); x++) {
      string after = std::format("_{}.txt", x);
      this->write_decoded_data(base_filename, res, after, decoded.strs[x]);
    }
    if (!decoded.after_data.empty()) {
      this->write_decoded_data(base_filename, res, "_excess.bin", decoded.after_data);
    }
  }

  void write_decoded_TwCS(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_TwCS(res);
    for (size_t x = 0; x < decoded.size(); x++) {
      string after = std::format("_{}.txt", x);
      this->write_decoded_data(base_filename, res, after, decoded[x]);
    }
  }

  void write_decoded_KCHR(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto decoded = this->current_rf->decode_KCHR(res);

    string disassembly = "# Modifiers table:\n";
    disassembly += format_data(decoded.table_index_for_modifiers.data(), decoded.table_index_for_modifiers.size());

    for (size_t z = 0; z < decoded.tables.size(); z++) {
      const auto& table = decoded.tables[z];
      disassembly += std::format("# Character table {}:\n", z);
      disassembly += format_data(table.data(), table.size());
    }

    for (size_t z = 0; z < decoded.dead_keys.size(); z++) {
      const auto& dead_key = decoded.dead_keys[z];
      disassembly += std::format("# Dead key {} (table {:02X} vkcode {:02X}):\n",
          z, dead_key.table_index, dead_key.virtual_key_code);
      for (const auto& completion : dead_key.completions) {
        disassembly += std::format("#   Completion {:02X} => {:02X}\n",
            completion.completion_char, completion.substitute_char);
      }
      disassembly += std::format("#   Completion {:02X} => {:02X} (no match)\n",
          dead_key.no_match_completion.completion_char, dead_key.no_match_completion.substitute_char);
    }

    this->write_decoded_data(base_filename, res, ".txt", disassembly);
  }

  void write_decoded_DITL(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    using T = ResourceFile::DecodedDialogItem::Type;

    auto decoded = this->current_rf->decode_DITL(res);

    string filename = this->output_filename(base_filename, res, ".txt");
    this->ensure_directories_exist(filename);
    auto f = fopen_unique(filename, "wt");
    fwrite_fmt(f.get(), "# {} entries\n", decoded.size());

    for (size_t z = 0; z < decoded.size(); z++) {
      const auto& item = decoded[z];

      bool text_is_data = false;
      uint32_t external_res_type = 0;
      const char* type_name = nullptr;
      switch (item.type) {
        case T::BUTTON:
          type_name = "BUTTON";
          break;
        case T::CHECKBOX:
          type_name = "CHECKBOX";
          break;
        case T::RADIO_BUTTON:
          type_name = "RADIO_BUTTON";
          break;
        case T::RESOURCE_CONTROL:
          type_name = "RESOURCE_CONTROL";
          external_res_type = RESOURCE_TYPE_CNTL;
          break;
        case T::HELP_BALLOON:
          type_name = "HELP_BALLOON";
          // TODO: Are either of resource_type or text valid here?
          break;
        case T::TEXT: // text valid
          type_name = "TEXT";
          break;
        case T::EDIT_TEXT: // text valid
          type_name = "EDIT_TEXT";
          break;
        case T::ICON: // resource_id valid
          type_name = "ICON";
          external_res_type = RESOURCE_TYPE_ICON;
          break;
        case T::PICTURE: // resource_id valid
          type_name = "PICTURE";
          external_res_type = RESOURCE_TYPE_PICT;
          break;
        case T::CUSTOM: // neither resource_id nor text valid
          type_name = "CUSTOM";
          text_is_data = true;
          break;
        case T::UNKNOWN: // text contains raw info string (may be binary data!)
          type_name = "UNKNOWN";
          text_is_data = true;
          break;
      };

      fwrite_fmt(f.get(), "# item {}: {} (0x{:02X}) {}\n", z, type_name, item.raw_type, item.enabled ? "enabled" : "disabled");
      fwrite_fmt(f.get(), "#   bounds: x1={} y1={} x2={} y2={}\n", item.bounds.x1, item.bounds.y1, item.bounds.x2, item.bounds.y2);
      if (external_res_type) {
        string res_type_name = string_for_resource_type(external_res_type);
        fwrite_fmt(f.get(), "#   {} resource ID: {}\n", res_type_name, item.resource_id);
      } else if (text_is_data) {
        string text = format_data_string(item.text);
        fwrite_fmt(f.get(), "#   data: {}\n", text);
      } else {
        string text = escape_controls_utf8(decode_mac_roman(item.text));
        fwrite_fmt(f.get(), "#   text: \"{}\"\n", text);
      }
    }
  }

  JSON generate_json_for_INST(
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

    auto key_regions_list = JSON::list();
    for (const auto& rgn : inst.key_regions) {
      const auto& snd_res = this->current_rf->get_resource(rgn.snd_type, rgn.snd_id);
      auto key_region_dict = JSON::dict();
      key_region_dict.emplace("key_low", rgn.key_low + key_region_boundary_shift);
      key_region_dict.emplace("key_high", rgn.key_high + key_region_boundary_shift);

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
        fwrite_fmt(stderr, "warning: failed to get sound metadata for instrument {} region {:X}-{:X} from snd/csnd/esnd {}: {}\n",
            id, rgn.key_low, rgn.key_high, rgn.snd_id, e.what());
      }

      key_region_dict.emplace("filename", basename(this->output_filename(base_filename, snd_res, snd_is_mp3 ? ".mp3" : ".wav")));

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
      key_region_dict.emplace("base_note", base_note);

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
        key_region_dict.emplace("freq_mult", freq_mult);
      }

      if (inst.constant_pitch) {
        key_region_dict.emplace("constant_pitch", true);
      }

      key_regions_list.emplace_back(std::move(key_region_dict));
    }

    auto inst_dict = JSON::dict();
    inst_dict.emplace("id", id);
    inst_dict.emplace("regions", std::move(key_regions_list));
    if (!inst.tremolo_data.empty()) {
      auto tremolo_json = JSON::list();
      for (uint16_t x : inst.tremolo_data) {
        tremolo_json.emplace_back(x);
      }
      inst_dict.emplace("tremolo_data", std::move(tremolo_json));
    }
    if (!inst.copyright.empty()) {
      inst_dict.emplace("copyright", inst.copyright);
    }
    if (!inst.author.empty()) {
      inst_dict.emplace("author", inst.author);
    }
    return inst_dict;
  }

  JSON generate_json_for_SONG(
      const string& base_filename,
      const ResourceFile::DecodedSongResource* s) {
    string midi_filename;
    if (s) {
      static const vector<uint32_t> midi_types({RESOURCE_TYPE_MIDI, RESOURCE_TYPE_Midi, RESOURCE_TYPE_midi,
          RESOURCE_TYPE_cmid, RESOURCE_TYPE_emid, RESOURCE_TYPE_ecmi});
      for (uint32_t midi_type : midi_types) {
        try {
          const auto& res = this->current_rf->get_resource(midi_type, s->midi_id);
          midi_filename = basename(this->output_filename(base_filename, res, ".midi"));
          break;
        } catch (const exception&) {
        }
      }
      if (midi_filename.empty()) {
        throw runtime_error("SONG refers to missing MIDI");
      }
    }

    // First add the overrides, then add all the other instruments
    auto instruments = JSON::list();
    if (s) {
      for (const auto& it : s->instrument_overrides) {
        try {
          instruments.emplace_back(generate_json_for_INST(
              base_filename, it.first, this->current_rf->decode_INST(it.second), s->semitone_shift));
        } catch (const exception& e) {
          fwrite_fmt(stderr, "warning: failed to add instrument {} from INST {}: {}\n",
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
        fwrite_fmt(stderr, "warning: failed to add instrument {}: {}\n", id, e.what());
      }
    }

    auto base_dict = JSON::dict({{"sequence_type", "MIDI"}, {"sequence_filename", midi_filename}, {"instruments", instruments}});
    if (s && !s->velocity_override_map.empty()) {
      auto velocity_override_list = JSON::list();
      for (uint16_t override : s->velocity_override_map) {
        velocity_override_list.emplace_back(override);
      }
      base_dict.emplace("velocity_override_map", std::move(velocity_override_list));
    }
    if (s && !s->title.empty()) {
      base_dict.emplace("title", s->title);
    }
    if (s && !s->performer.empty()) {
      base_dict.emplace("performer", s->performer);
    }
    if (s && !s->composer.empty()) {
      base_dict.emplace("composer", s->composer);
    }
    if (s && !s->copyright_date.empty()) {
      base_dict.emplace("copyright_date", s->copyright_date);
    }
    if (s && !s->copyright_text.empty()) {
      base_dict.emplace("copyright_text", s->copyright_text);
    }
    if (s && !s->license_contact.empty()) {
      base_dict.emplace("license_contact", s->license_contact);
    }
    if (s && !s->license_uses.empty()) {
      base_dict.emplace("license_uses", s->license_uses);
    }
    if (s && !s->license_domain.empty()) {
      base_dict.emplace("license_domain", s->license_domain);
    }
    if (s && !s->license_term.empty()) {
      base_dict.emplace("license_term", s->license_term);
    }
    if (s && !s->license_expiration.empty()) {
      base_dict.emplace("license_expiration", s->license_expiration);
    }
    if (s && !s->note.empty()) {
      base_dict.emplace("note", s->note);
    }
    if (s && !s->index_number.empty()) {
      base_dict.emplace("index_number", s->index_number);
    }
    if (s && !s->genre.empty()) {
      base_dict.emplace("genre", s->genre);
    }
    if (s && !s->subgenre.empty()) {
      base_dict.emplace("subgenre", s->subgenre);
    }
    if (s && s->tempo_bias && (s->tempo_bias != 16667)) {
      base_dict.emplace("tempo_bias", static_cast<double>(s->tempo_bias) / 16667.0);
    }
    if (s && s->note_decay && (s->note_decay != -1)) {
      base_dict.emplace("note_decay", s->note_decay);
    }
    if (s && s->percussion_instrument) {
      base_dict.emplace("percussion_instrument", s->percussion_instrument);
    }
    base_dict.emplace("allow_program_change", s ? s->allow_program_change : true);
    return base_dict;
  }

  void write_decoded_INST(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto json = generate_json_for_INST(base_filename, res->id, this->current_rf->decode_INST(res), 0);
    this->write_decoded_data(base_filename, res, ".json", json.serialize(JSON::SerializeOption::FORMAT));
  }

  void write_decoded_SONG(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
    auto song = this->current_rf->decode_SONG(res);
    auto json = generate_json_for_SONG(base_filename, &song);
    this->write_decoded_data(base_filename, res, "_smssynth_env.json", json.serialize(JSON::SerializeOption::FORMAT));
  }

  void write_decoded_Tune(const string& base_filename, shared_ptr<const ResourceFile::Resource> res) {
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
  static const map<pair<uint32_t, int16_t>, uint32_t> remap_resource_type_id;

  // Maps resource type aliases to the original type
  static const unordered_map<uint32_t, uint32_t> remap_resource_type;

  bool is_included(uint32_t type, int16_t id) const {
    // Included because all types, IDs and names are implicitly included?
    if (this->target_types_ids.empty() && !this->target_ids && this->target_names.empty()) {
      return true;
    }

    // Included by ID regardless of type?
    if (this->target_ids && (*this->target_ids)[id]) {
      return true;
    }

    // Included by type and ID?
    if (auto it = this->target_types_ids.find(type); it != this->target_types_ids.end() && it->second[id]) {
      return true;
    }

    // Included by name?
    if (!this->target_names.empty()) {
      if (auto it = this->target_names.find(this->current_rf->get_resource_name(type, id));
          it != this->target_names.end()) {
        return true;
      }
    }

    // Not included
    return false;
  }

  bool is_excluded(uint32_t type, int16_t id) const {
    // Excluded by ID?
    if (this->skip_ids && (*this->skip_ids)[id]) {
      return true;
    }

    // Excluded by type and ID?
    if (auto it = this->skip_types_ids.find(type); it != this->skip_types_ids.end() && !it->second[id]) {
      return true;
    }

    // Excluded by name?
    if (!this->skip_names.empty()) {
      if (auto it = this->skip_names.find(this->current_rf->get_resource_name(type, id));
          it != this->skip_names.end()) {
        return true;
      }
    }

    // Not explicitly included
    return false;
  }

  bool disassemble_file(const string& filename) {
    string resource_fork_filename = filename;
    if (!this->use_data_fork) {
      resource_fork_filename += RESOURCE_FORK_FILENAME_SUFFIX;
    }

    // On HFS+, the resource fork always exists, but might be empty. On APFS,
    // the resource fork is optional.
    if (!std::filesystem::is_regular_file(resource_fork_filename) || std::filesystem::file_size(resource_fork_filename) == 0) {
      fwrite_fmt(stderr, ">>> {} ({})\n", filename,
          this->use_data_fork ? "file is empty" : "resource fork missing or empty");
      return false;

    } else {
      fwrite_fmt(stderr, ">>> {}\n", filename);
    }

    // compute the base filename
    size_t last_slash_pos = filename.rfind('/');
    string base_filename = (last_slash_pos == string::npos) ? filename : filename.substr(last_slash_pos + 1);

    // get the resources from the file
    try {
      this->current_rf = make_unique<ResourceFile>(this->parse(load_file(resource_fork_filename)));
    } catch (const cannot_open_file&) {
      fwrite_fmt(stderr, "failed on {}: cannot open file\n", filename);
      return false;
    } catch (const io_error& e) {
      fwrite_fmt(stderr, "failed on {}: cannot read data\n", filename);
      return false;
    } catch (const runtime_error& e) {
      fwrite_fmt(stderr, "failed on {}: corrupt resource index ({})\n", filename, e.what());
      return false;
    } catch (const out_of_range& e) {
      fwrite_fmt(stderr, "failed on {}: corrupt resource index\n", filename);
      return false;
    }

    bool ret = false;
    try {
      auto resources = this->current_rf->all_resources();

      bool has_INST = false;
      for (const auto& it : resources) {
        if (!is_included(it.first, it.second) || is_excluded(it.first, it.second)) {
          continue;
        }

        const auto& res = this->current_rf->get_resource(it.first, it.second, this->decompress_flags);

        if (it.first == RESOURCE_TYPE_INST) {
          has_INST = true;
        }
        ret |= this->export_resource(base_filename, res);
      }

      // Special case: if we disassembled any INSTs and the save-raw behavior is
      // not Never, generate an smssynth template file from all the INSTs
      if (has_INST && (this->save_raw != SaveRawBehavior::NEVER)) {
        string json_filename = output_filename(base_filename, nullptr, nullptr, "generated", "", 0, "smssynth_env_template.json");

        try {
          auto json = generate_json_for_SONG(base_filename, nullptr);
          save_file(json_filename, json.serialize(JSON::SerializeOption::FORMAT));
          fwrite_fmt(stderr, "... {}\n", json_filename);

        } catch (const exception& e) {
          fwrite_fmt(stderr, "failed to write smssynth env template {}: {}\n",
              json_filename, e.what());
        }
      }

    } catch (const exception& e) {
      fwrite_fmt(stderr, "failed on {}: {}\n", filename, e.what());
    }

    this->current_rf.reset();
    return ret;
  }

  bool disassemble_path(const string& filename) {
    if (std::filesystem::is_directory(filename)) {
      fwrite_fmt(stderr, ">>> {} (directory)\n", filename);

      unordered_set<string> items;
      try {
        for (const auto& item : std::filesystem::directory_iterator(filename)) {
          items.emplace(item.path().filename().string());
        }
      } catch (const runtime_error& e) {
        fwrite_fmt(stderr, "warning: can\'t list directory: {}\n", e.what());
        return false;
      }

      vector<string> sorted_items;
      sorted_items.insert(sorted_items.end(), items.begin(), items.end());
      sort(sorted_items.begin(), sorted_items.end());

      size_t last_slash_pos = filename.rfind('/');
      string base_filename = (last_slash_pos == string::npos) ? filename : filename.substr(last_slash_pos + 1);

      string sub_out_dir = this->out_dir.empty()
          ? base_filename
          : (this->out_dir + "/" + base_filename);
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
        export_icon_family_as_image(true),
        export_icon_family_as_icns(true),
        image_saver(),
        index_format(IndexFormat::RESOURCE_FORK),
        parse(parse_resource_fork) {}
  ~ResourceExporter() = default;

  bool use_data_fork;
  string filename_format;
  SaveRawBehavior save_raw;
  uint64_t decompress_flags;
  unordered_map<uint32_t, ResourceIDs> target_types_ids;
  unordered_map<uint32_t, ResourceIDs> skip_types_ids;
  optional<ResourceIDs> target_ids;
  unordered_set<string> target_names;
  optional<ResourceIDs> skip_ids;
  unordered_set<string> skip_names;
  vector<string> external_preprocessor_command;
  TargetCompressedBehavior target_compressed_behavior;
  bool skip_templates;
  bool export_icon_family_as_image;
  bool export_icon_family_as_icns;
  ImageSaver image_saver;

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
    } else if (this->index_format == IndexFormat::MACBINARY) {
      this->parse = parse_macbinary_resource_fork;
    } else if (this->index_format == IndexFormat::APPLESINGLE_APPLEDOUBLE) {
      this->parse = parse_applesingle_appledouble_resource_fork;
    } else if (this->index_format == IndexFormat::MOHAWK) {
      this->parse = parse_mohawk;
    } else if (this->index_format == IndexFormat::HIRF) {
      this->parse = parse_hirf;
    } else if (this->index_format == IndexFormat::DC_DATA) {
      this->parse = parse_dc_data;
    } else if (this->index_format == IndexFormat::CBAG) {
      this->parse = parse_cbag;
    } else {
      throw logic_error("invalid index format");
    }
  }

  void set_decoder_alias(uint32_t from_type, uint32_t to_type) {
    try {
      this->type_to_decode_fn[to_type] = this->type_to_decode_fn.at(from_type);
    } catch (const out_of_range&) {
    }
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
      if (decompression_failed) {
        fwrite_fmt(stderr, "warning: failed to decompress resource {}:{}; saving raw compressed data\n", type_str, res->id);
      } else {
        fwrite_fmt(stderr, "note: resource {}:{} is compressed; saving raw compressed data\n", type_str, res->id);
      }
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

#ifndef PHOSG_WINDOWS
    // Run external preprocessor if possible. The resource could still be
    // compressed if --skip-decompression was used or if decompression failed;
    // in these cases it doesn't make sense to run the external preprocessor.
    if (!is_compressed && !this->external_preprocessor_command.empty()) {
      auto result = run_process(this->external_preprocessor_command, &res->data, false);
      if (result.exit_status != 0) {
        fwrite_fmt(stderr, "\
warning: external preprocessor failed with exit status 0x{:X}\n\
\n\
stdout ({} bytes):\n\
{}\n\
\n\
stderr ({} bytes):\n\
{}\n\
\n",
            result.exit_status, result.stdout_contents.size(), result.stdout_contents, result.stderr_contents.size(), result.stderr_contents);
      } else {
        fwrite_fmt(stderr, "note: external preprocessor succeeded and returned {} bytes\n", result.stdout_contents.size());
        res_to_decode = make_shared<ResourceFile::Resource>(
            res->type, res->id, res->flags, res->name, std::move(result.stdout_contents));
      }
    }
#else
    if (!is_compressed && !this->external_preprocessor_command.empty()) {
      throw std::runtime_error("External preprocessors are not supported on Windows");
    }
#endif

    // Decode if possible. If decompression failed, don't bother trying to
    // decode the resource.
    uint32_t remapped_type = res->type;
    try {
      remapped_type = remap_resource_type_id.at({res_to_decode->type, res_to_decode->id});
    } catch (const out_of_range&) {
    }
    try {
      remapped_type = remap_resource_type.at(remapped_type);
    } catch (const out_of_range&) {
    }

    resource_decode_fn decode_fn = nullptr;
    try {
      decode_fn = type_to_decode_fn.at(remapped_type);
    } catch (const out_of_range&) {
    }

    bool decoded = false;
    if (!is_compressed && decode_fn) {
      try {
        (this->*decode_fn)(base_filename, res_to_decode);
        decoded = true;
      } catch (const exception& e) {
        auto type_str = string_for_resource_type(res->type);
        if (remapped_type != res->type) {
          auto remapped_type_str = string_for_resource_type(remapped_type);
          fwrite_fmt(stderr, "warning: failed to decode resource {}:{} (remapped to {}): {}\n", type_str, res->id, remapped_type_str, e.what());
        } else {
          fwrite_fmt(stderr, "warning: failed to decode resource {}:{}: {}\n", type_str, res->id, e.what());
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
      } catch (const out_of_range& e) {
      }

      if (tmpl_res.get()) {
        try {
          string result = std::format("# (decoded with TMPL {})\n", tmpl_res->id);
          result += this->current_rf->disassemble_from_template(
              res->data.data(), res->data.size(), this->current_rf->decode_TMPL(tmpl_res));
          this->write_decoded_data(base_filename, res_to_decode, ".txt", result);
          decoded = true;
        } catch (const exception& e) {
          auto type_str = string_for_resource_type(res->type);
          if (remapped_type != res->type) {
            auto remapped_type_str = string_for_resource_type(remapped_type);
            fwrite_fmt(stderr, "warning: failed to decode resource {}:{} (remapped to {}) with template {}: {}\n", type_str, res->id, remapped_type_str, tmpl_res->id, e.what());
          } else {
            fwrite_fmt(stderr, "warning: failed to decode resource {}:{} with template {}: {}\n", type_str, res->id, tmpl_res->id, e.what());
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
            fwrite_fmt(stderr, "warning: failed to decode resource {}:{} (remapped to {}) with system template: {}\n", type_str, res->id, remapped_type_str, e.what());
          } else {
            fwrite_fmt(stderr, "warning: failed to decode resource {}:{} with system template: {}\n", type_str, res->id, e.what());
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
      } catch (const out_of_range&) {
      }

      string out_filename_after = std::format(".{}", out_ext);
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
        fwrite_fmt(stderr, "... {}\n", out_filename);
      } catch (const exception& e) {
        fwrite_fmt(stderr, "warning: failed to save raw data: {}\n", e.what());
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
    {RESOURCE_TYPE_CDRV, &ResourceExporter::write_decoded_DRVR},
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
    {RESOURCE_TYPE_DITL, &ResourceExporter::write_decoded_DITL},
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
    {RESOURCE_TYPE_icns, &ResourceExporter::write_decoded_icns},
    {RESOURCE_TYPE_ICON, &ResourceExporter::write_decoded_ICON},
    {RESOURCE_TYPE_ics4, &ResourceExporter::write_decoded_ics4},
    {RESOURCE_TYPE_ics8, &ResourceExporter::write_decoded_ics8},
    {RESOURCE_TYPE_icsN, &ResourceExporter::write_decoded_icsN},
    {RESOURCE_TYPE_INIT, &ResourceExporter::write_decoded_inline_68k},
    {RESOURCE_TYPE_INST, &ResourceExporter::write_decoded_INST},
    {RESOURCE_TYPE_kcs4, &ResourceExporter::write_decoded_kcs4},
    {RESOURCE_TYPE_kcs8, &ResourceExporter::write_decoded_kcs8},
    {RESOURCE_TYPE_kcsN, &ResourceExporter::write_decoded_kcsN},
    {RESOURCE_TYPE_KCHR, &ResourceExporter::write_decoded_KCHR},
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
    {RESOURCE_TYPE_PAT, &ResourceExporter::write_decoded_PAT},
    {RESOURCE_TYPE_PATN, &ResourceExporter::write_decoded_PATN},
    {RESOURCE_TYPE_PICT, &ResourceExporter::write_decoded_PICT},
    {RESOURCE_TYPE_pltt, &ResourceExporter::write_decoded_pltt},
    {RESOURCE_TYPE_ppat, &ResourceExporter::write_decoded_ppat},
    {RESOURCE_TYPE_ppct, &ResourceExporter::write_decoded_pef},
    {RESOURCE_TYPE_pptN, &ResourceExporter::write_decoded_pptN},
    {RESOURCE_TYPE_proc, &ResourceExporter::write_decoded_inline_68k},
    {RESOURCE_TYPE_ptch, &ResourceExporter::write_decoded_inline_68k},
    {RESOURCE_TYPE_PTCH, &ResourceExporter::write_decoded_inline_68k},
    {RESOURCE_TYPE_qtcm, &ResourceExporter::write_decoded_pef},
    {RESOURCE_TYPE_res1, &ResourceExporter::write_decoded_STRN},
    {RESOURCE_TYPE_ROvN, &ResourceExporter::write_decoded_ROvN},
    {RESOURCE_TYPE_ROvr, &ResourceExporter::write_decoded_inline_68k},
    {RESOURCE_TYPE_RSSC, &ResourceExporter::write_decoded_RSSC},
    {RESOURCE_TYPE_scal, &ResourceExporter::write_decoded_pef},
    {RESOURCE_TYPE_seg1, &ResourceExporter::write_decoded_STRN},
    {RESOURCE_TYPE_SERD, &ResourceExporter::write_decoded_inline_68k},
    {RESOURCE_TYPE_sfvr, &ResourceExporter::write_decoded_pef},
    {RESOURCE_TYPE_SICN, &ResourceExporter::write_decoded_SICN},
    {RESOURCE_TYPE_SIZE, &ResourceExporter::write_decoded_SIZE},
    {RESOURCE_TYPE_SMOD, &ResourceExporter::write_decoded_inline_68k},
    {RESOURCE_TYPE_SMSD, &ResourceExporter::write_decoded_SMSD},
    {RESOURCE_TYPE_snd, &ResourceExporter::write_decoded_snd},
    {RESOURCE_TYPE_snth, &ResourceExporter::write_decoded_inline_68k},
    {RESOURCE_TYPE_SONG, &ResourceExporter::write_decoded_SONG},
    {RESOURCE_TYPE_SOUN, &ResourceExporter::write_decoded_SOUN},
    {RESOURCE_TYPE_STR, &ResourceExporter::write_decoded_STR},
    {RESOURCE_TYPE_STRN, &ResourceExporter::write_decoded_STRN},
    {RESOURCE_TYPE_styl, &ResourceExporter::write_decoded_styl},
    {RESOURCE_TYPE_TEXT, &ResourceExporter::write_decoded_TEXT},
    {RESOURCE_TYPE_TMPL, &ResourceExporter::write_decoded_TMPL},
    {RESOURCE_TYPE_Tune, &ResourceExporter::write_decoded_Tune},
    {RESOURCE_TYPE_TwCS, &ResourceExporter::write_decoded_TwCS},
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
    {RESOURCE_TYPE_dem, &ResourceExporter::write_decoded_inline_68k},
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
    {RESOURCE_TYPE_osl, &ResourceExporter::write_decoded_inline_68k},
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
    {RESOURCE_TYPE_mod, "mod"},
    {RESOURCE_TYPE_icns, "icns"},
    {RESOURCE_TYPE_MADH, "madh"},
    {RESOURCE_TYPE_MADI, "madi"},
    {RESOURCE_TYPE_MIDI, "midi"},
    {RESOURCE_TYPE_Midi, "midi"},
    {RESOURCE_TYPE_midi, "midi"},
    {RESOURCE_TYPE_PICT, "pict"},
    {RESOURCE_TYPE_sfnt, "ttf"},
});

const map<pair<uint32_t, int16_t>, uint32_t> ResourceExporter::remap_resource_type_id = {
    {{RESOURCE_TYPE_PREC, 0}, RESOURCE_TYPE_PRC0},
    {{RESOURCE_TYPE_PREC, 1}, RESOURCE_TYPE_PRC0},
    {{RESOURCE_TYPE_PREC, 3}, RESOURCE_TYPE_PRC3},
    {{RESOURCE_TYPE_PREC, 4}, RESOURCE_TYPE_PRC3},
    {{RESOURCE_TYPE_INTL, 0}, RESOURCE_TYPE_itl0},
    {{RESOURCE_TYPE_INTL, 1}, RESOURCE_TYPE_itl1},
};

const unordered_map<uint32_t, uint32_t> ResourceExporter::remap_resource_type = {
    {RESOURCE_TYPE_68k1, RESOURCE_TYPE_mem1},
    {RESOURCE_TYPE_ppc1, RESOURCE_TYPE_mem1},
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
  --data-fork\n\
      Disassemble the file\'s data fork as if it were the resource fork.\n\
  --index-format=FORMAT\n\
      Parse the input as a resource index in this format. Valid FORMATs are:\n\
        resource-fork (default): Mac OS resource fork\n\
        as/ad: Mac OS resource fork inside an AppleSingle or AppleDouble file\n\
        macbinary: Mac OS resource fork inside a MacBinary file\n\
        mohawk: Mohawk archive\n\
        hirf: Beatnik HIRF archive (also known as IREZ, HSB, or RMF)\n\
        dc-data: DC Data file\n\
        cbag: CBag archive\n\
      If the index format is not resource-fork, --data-fork is implied.\n\
  --target=TYPE[:ID]\n\
      Only extract resources of this type and optionally IDs (can be given\n\
      multiple times). To specify characters with special meanings or\n\
      non-ASCII characters escape them as %<hex>. For example, to specify\n\
      the $ character in the type, escape it as %24. The % character\n\
      itself can be written as %25.\n\
      The optional IDs are a comma-separated list of single IDs or ID\n\
      ranges, where an ID range has the format <min id>..<max id>. Both\n\
      <min id> and <max_id> are optional and default to -32768 and\n\
      32767, respectively. Prefixing an ID [range] with '~' (the tilde)\n\
      excludes instead of includes.\n\
      For example, --target=PICT:128,1000..2000,~1234,..-12345 exports only\n\
      PICT resources with IDs -32768 to -12345, 128, and 1000 to 2000,\n\
      except for ID 1234.\n\
      Another example: --target=CODE:~0 exports only CODE resources with\n\
      an ID other than 0.\n\
  --target-type=TYPE\n\
      Only extract resources of this type (can be given multiple times). See\n\
      '--target' above for how to escape characters.\n\
  --target-id=ID\n\
      Only extract resources with this ID/these IDs (can be given multiple\n\
      times).  See '--target' above for how to specify ID(s).\n\
  --target-name=NAME\n\
      Only extract resources with this name.\n\
  --target-compressed\n\
      Only extract resources that are compressed in the source file.\n\
  --skip-type=TYPE\n\
      Don\'t extract resources of this type (can be given multiple times). See\n\
      '--target' above for how to escape characters.\n\
  --skip-id=ID\n\
      Don\'t extract resources with with this ID/these IDs (can be given\n\
      multiple times).  See '--target' above for how to specify ID(s).\n\
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
  --disassemble-system-dcmp=N\n\
  --disassemble-system-ncmp=N\n\
      Disassemble the included default 68K or PEF decompressor and print the\n\
      result to stdout. If either of these options is given, all other options\n\
      are ignored (no operation is done on any resource file). These options\n\
      are generally only useful for finding bugs in the emulators or native\n\
      decompressor implementations.\n\
  --describe-system-template=TYPE\n\
      Describe the included system template for resource type TYPE and print\n\
      the result to stdout. If this option is given, all other options are\n\
      ignored (no operation is done on any resource file).\n\
\n\
Resource decoding options:\n\
  --copy-handler=TYPE1:TYPE2\n\
      Decode TYPE2 resources as if they were TYPE1. Non-ASCII bytes in the\n\
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
        %T:     the resource's type as a hex string\n\
        %%:     a percent sign\n\
      FORMAT can also be one of the following values, which produce output\n\
      filenames like these examples:\n\
        std:     OutDir/Folder1/FileA_snd_128_Name.wav (this is the default)\n\
        std-hex: OutDir/Folder1/FileA_736E6420_128_Name.wav ('snd ' as hex)\n\
        dirs:    OutDir/Folder1/FileA/snd_128_Name.wav\n\
        tdirs:   OutDir/Folder1/FileA/snd/128_Name.wav\n\
        t1:      OutDir/snd/Folder1/FileA_128_Name.wav\n\
        t1dirs:  OutDir/snd/Folder1/FileA/128_Name.wav\n\
      When using the tdirs, t1dirs or similar custom formats, any generated JSON\n\
      files from SONG resources will not play with smssynth unless you manually put\n\
      the required sound and MIDI resources in the same directory as the SONG JSON\n\
      after decoding.\n\
\n" IMAGE_SAVER_HELP
        "Resource-type specific options:\n\
  --icon-family-format=image,icns\n\
      Export icon families (icl8, ICN# etc) in one or several formats. A comma-\n\
      separated list of one or more of:\n\
        image:  Save each icon of the family as a separate image file (the format\n\
                can be set with " IMAGE_SAVER_OPTION ")\n\
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
\n",
      stderr);
}

int main(int argc, char* argv[]) {
#ifndef PHOSG_WINDOWS
  signal(SIGPIPE, SIG_IGN);
#endif

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
      string res_name; // Only used for ADD and RENAME
      string filename; // Only used for ADD

      ModificationOperation()
          : op_type(Type::ADD),
            res_type(0),
            res_id(0),
            new_res_id(0),
            res_flags(0) {}
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
    int32_t disassemble_system_dcmp_id = 0x7FFFFFFF;
    int32_t disassemble_system_ncmp_id = 0x7FFFFFFF;
    uint32_t describe_system_template_type = 0;
    for (int x = 1; x < argc; x++) {
      if (argv[x][0] == '-') {
        if (!strncmp(argv[x], "--disassemble-system-dcmp=", 26)) {
          disassemble_system_dcmp_id = strtol(&argv[x][26], nullptr, 0);
        } else if (!strncmp(argv[x], "--disassemble-system-ncmp=", 26)) {
          disassemble_system_ncmp_id = strtol(&argv[x][26], nullptr, 0);
        } else if (!strncmp(argv[x], "--describe-system-template=", 27)) {
          describe_system_template_type = parse_cli_type(&argv[x][27]);

        } else if (!strcmp(argv[x], "--index-format=resource-fork")) {
          exporter.set_index_format(IndexFormat::RESOURCE_FORK);
        } else if (!strcmp(argv[x], "--index-format=as/ad")) {
          exporter.set_index_format(IndexFormat::APPLESINGLE_APPLEDOUBLE);
          exporter.use_data_fork = true;
        } else if (!strcmp(argv[x], "--index-format=macbinary")) {
          exporter.set_index_format(IndexFormat::MACBINARY);
          exporter.use_data_fork = true;
        } else if (!strcmp(argv[x], "--index-format=mohawk")) {
          exporter.set_index_format(IndexFormat::MOHAWK);
          exporter.use_data_fork = true;
        } else if (!strcmp(argv[x], "--index-format=hirf")) {
          exporter.set_index_format(IndexFormat::HIRF);
          exporter.use_data_fork = true;
        } else if (!strcmp(argv[x], "--index-format=dc-data")) {
          exporter.set_index_format(IndexFormat::DC_DATA);
          exporter.use_data_fork = true;
        } else if (!strcmp(argv[x], "--index-format=cbag")) {
          exporter.set_index_format(IndexFormat::CBAG);
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
          modifications.emplace_back(std::move(op));

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
          modifications.emplace_back(std::move(op));

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
          exporter.target_types_ids.emplace(parse_cli_type(&argv[x][14]), ResourceIDs(ResourceIDs::Init::ALL));
        } else if (!strncmp(argv[x], "--skip-type=", 12)) {
          exporter.skip_types_ids.emplace(parse_cli_type(&argv[x][12]), ResourceIDs(ResourceIDs::Init::ALL));

        } else if (!strncmp(argv[x], "--target-id=", 12)) {
          if (!exporter.target_ids) {
            exporter.target_ids = ResourceIDs(ResourceIDs::Init::NONE);
          }
          parse_cli_ids(&argv[x][12], *exporter.target_ids);
        } else if (!strncmp(argv[x], "--skip-id=", 10)) {
          if (!exporter.skip_ids) {
            exporter.skip_ids = ResourceIDs(ResourceIDs::Init::NONE);
          }
          parse_cli_ids(&argv[x][10], *exporter.skip_ids);

        } else if (!strncmp(argv[x], "--target=", 9)) {
          ResourceIDs ids(ResourceIDs::Init::NONE);
          exporter.target_types_ids.emplace(parse_cli_type_ids(&argv[x][9], &ids), ids);

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
        } else if (!strcmp(argv[x], "--filename-format=std-hex")) {
          exporter.filename_format = FILENAME_FORMAT_STANDARD_HEX;
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
          exporter.export_icon_family_as_image = false;
          exporter.export_icon_family_as_icns = false;
          for (const auto& format : formats) {
            if (format == "image") {
              exporter.export_icon_family_as_image = true;
            } else if (format == "icns") {
              exporter.export_icon_family_as_icns = true;
            } else {
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

        } else if (!exporter.image_saver.process_cli_arg(argv[x])) {
          fwrite_fmt(stderr, "invalid option: {}\n", argv[x]);
          print_usage();
          return 2;
        }
      } else {
        if (filename.empty()) {
          filename = argv[x];
        } else if (out_dir.empty()) {
          out_dir = argv[x];
        } else {
          print_usage();
          return 2;
        }
      }
    }

    if (disassemble_system_dcmp_id != 0x7FFFFFFF) {
      auto data = get_system_decompressor(false, disassemble_system_dcmp_id);
      auto decoded = ResourceFile::decode_dcmp(data.first, data.second);
      string disassembly = disassembly_for_dcmp(decoded);
      fwritex(stdout, disassembly);
      return 0;
    } else if (disassemble_system_ncmp_id != 0x7FFFFFFF) {
      auto data = get_system_decompressor(true, disassemble_system_ncmp_id);
      auto pef = ResourceFile::decode_pef(data.first, data.second);
      pef.print(stdout);
      return 0;
    } else if (describe_system_template_type != 0) {
      const auto& tmpl = get_system_template(describe_system_template_type);
      if (tmpl.empty()) {
        fwrite_fmt(stderr, "No system template exists for the given type\n");
        return 1;
      } else {
        string description = ResourceFile::describe_template(tmpl);
        fwrite_fmt(stdout, "{}\n", description);
        return 0;
      }
    }

    if (modify_resource_map && modifications.empty() && !create_resource_map) {
      throw runtime_error("multiple incompatible modes were specified");
    }

    if (!modify_resource_map) {
      if (filename.empty()) {
        print_usage();
        return 2;
      }

      if (single_resource.type) {
        exporter.save_raw = ResourceExporter::SaveRawBehavior::NEVER;
        exporter.target_types_ids.clear();
        exporter.target_ids.reset();
        exporter.target_names.clear();
        exporter.skip_types_ids.clear();
        exporter.skip_ids.reset();
        exporter.skip_names.clear();
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
        rf.add(std::move(single_resource));

        size_t last_slash_pos = filename.rfind('/');
        string base_filename = (last_slash_pos == string::npos) ? filename : filename.substr(last_slash_pos + 1);

        const auto& res = rf.get_resource(type, id, exporter.decompress_flags);
        return exporter.export_resource(filename, res) ? 0 : 3;

      } else {
        if (out_dir.empty()) {
          out_dir = filename + ".out";
        }
        std::filesystem::create_directories(out_dir);
        if (!exporter.disassemble(filename, out_dir)) {
          return 3;
        } else {
          return 0;
        }
      }

    } else { // modify_resource_map == true
      if (filename.empty()) {
        print_usage();
        return 2;
      }

      string input_data;
      if (!create_resource_map) {
        string input_filename;
        if (exporter.use_data_fork) {
          input_filename = filename;
        } else if (std::filesystem::is_regular_file(filename + RESOURCE_FORK_FILENAME_SUFFIX)) {
          input_filename = filename + RESOURCE_FORK_FILENAME_SUFFIX;
        } else if (std::filesystem::is_regular_file(filename + RESOURCE_FORK_FILENAME_SHORT_SUFFIX)) {
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

      fwrite_fmt(stderr, "... (load input) {} bytes\n", input_data.size());

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
            if (!rf.add(std::move(res))) {
              throw runtime_error("cannot add resource");
            }
            fwrite_fmt(stderr, "... (add) {}:{} flags={:02X} name=\"{}\" data=\"{}\" ({} bytes) OK\n",
                type_str, op.res_id, op.res_flags, op.res_name, op.filename, data_bytes);
            break;
          }
          case ModificationOperation::Type::DELETE:
            if (!rf.remove(op.res_type, op.res_id)) {
              throw runtime_error("cannot delete resource");
            }
            fwrite_fmt(stderr, "... (delete) {}:{} OK\n", type_str, op.res_id);
            break;
          case ModificationOperation::Type::CHANGE_ID:
            if (!rf.change_id(op.res_type, op.res_id, op.new_res_id)) {
              throw runtime_error("cannot change resource id");
            }
            fwrite_fmt(stderr, "... (change id) {}:{}=>{} OK\n", type_str,
                op.res_id, op.new_res_id);
            break;
          case ModificationOperation::Type::RENAME:
            if (!rf.rename(op.res_type, op.res_id, op.res_name)) {
              throw runtime_error("cannot rename resource");
            }
            fwrite_fmt(stderr, "... (rename) {}:{}=>\"{}\" OK\n", type_str,
                op.res_id, op.res_name);
            break;
          default:
            throw logic_error("invalid modification operation");
        }
      }

      if (!use_output_data_fork) {
        out_dir += RESOURCE_FORK_FILENAME_SUFFIX;
      }

      string output_data = serialize_resource_fork(rf);
      fwrite_fmt(stderr, "... (serialize output) {} bytes\n", output_data.size());

      // Attempting to open the resource fork of a nonexistent file will fail
      // without creating the file, so if we're writing to a resource fork, we
      // touch the file first to make sure it will exist when we write the output.
      if (out_dir.ends_with(RESOURCE_FORK_FILENAME_SUFFIX)) {
        fopen_unique(out_dir.substr(0, out_dir.size() - RESOURCE_FORK_FILENAME_SUFFIX.size()), "a+");
      } else if (out_dir.ends_with(RESOURCE_FORK_FILENAME_SHORT_SUFFIX)) {
        fopen_unique(out_dir.substr(0, out_dir.size() - RESOURCE_FORK_FILENAME_SHORT_SUFFIX.size()), "a+");
      }
      save_file(out_dir, output_data);

      return 0;
    }
  } catch (const exception& e) {
    fwrite_fmt(stderr, "Error: {}\n", e.what());
    return 1;
  }
}
