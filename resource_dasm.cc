#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
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
#include "M68KEmulator.hh"
#include "PPC32Emulator.hh"

using namespace std;



static string output_filename(const string& out_dir, const string& base_filename,
    const ResourceFile::Resource& res, const std::string& after) {
  if (base_filename.empty()) {
    return out_dir;
  }

  // filter the type so it only contains valid filename characters
  uint32_t filtered_type = bswap32(res.type);
  char* type_str = reinterpret_cast<char*>(&filtered_type);
  for (size_t x = 0; x < 4; x++) {
    if (type_str[x] < 0x20 || type_str[x] > 0x7E || type_str[x] == '/') {
      type_str[x] = '_';
    }
  }

  string name_token;
  if (!res.name.empty()) {
    name_token = '_';
    for (char ch : res.name) {
      if (ch < 0x20 || ch > 0x7E || ch == '/') {
        name_token += '_';
      } else {
        name_token += ch;
      }
    }
  }

  if (out_dir.empty()) {
    return string_printf("%s_%.4s_%d%s%s", base_filename.c_str(),
        (const char*)&filtered_type, res.id, name_token.c_str(), after.c_str());
  } else {
    return string_printf("%s/%s_%.4s_%d%s%s", out_dir.c_str(),
        base_filename.c_str(), (const char*)&filtered_type, res.id,
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
    const ResourceFile::Resource& res, const string& after, const string& data) {
  string filename = output_filename(out_dir, base_filename, res, after);
  save_file(filename.c_str(), data);
  fprintf(stderr, "... %s\n", filename.c_str());
}

void write_decoded_image(const string& out_dir, const string& base_filename,
    const ResourceFile::Resource& res, const string& after, const Image& img) {
  string filename = output_filename(out_dir, base_filename, res, after);
  img.save(filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", filename.c_str());
}

void write_decoded_CURS(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_CURS(res);
  string after = string_printf("_%hu_%hu.bmp", decoded.hotspot_x, decoded.hotspot_y);
  write_decoded_image(out_dir, base_filename, res, after, decoded.bitmap);
}

void write_decoded_crsr(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_crsr(res);
  string bitmap_after = string_printf("_%hu_%hu_bitmap.bmp", decoded.hotspot_x, decoded.hotspot_y);
  string after = string_printf("_%hu_%hu.bmp", decoded.hotspot_x, decoded.hotspot_y);
  write_decoded_image(out_dir, base_filename, res, bitmap_after, decoded.bitmap);
  write_decoded_image(out_dir, base_filename, res, after, decoded.image);
}

void write_decoded_ppat(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_ppat(res);

  Image tiled = tile_image(decoded.pattern, 8, 8);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded.pattern);
  write_decoded_image(out_dir, base_filename, res, "_tiled.bmp", tiled);

  tiled = tile_image(decoded.monochrome_pattern, 8, 8);
  write_decoded_image(out_dir, base_filename, res, "_bitmap.bmp", decoded.monochrome_pattern);
  write_decoded_image(out_dir, base_filename, res, "_bitmap_tiled.bmp", tiled);
}

void write_decoded_pptN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
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
    const string& base_filename, const ResourceFile::Resource& res,
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

      img.draw_text(x, y, &width, nullptr, 0xFF0000FF, 0x00000000, "%04hX", decoded[z].c.r);
      x += width;

      img.draw_text(x, y, &width, nullptr, 0x00FF00FF, 0x00000000, "%04hX", decoded[z].c.g);
      x += width;

      img.draw_text(x, y, &width, nullptr, 0x0000FFFF, 0x00000000, "%04hX", decoded[z].c.b);
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
        img.draw_text(x, y, &width, nullptr, 0xFFFFFFFF, 0x00000000, " (%zu)", decoded[z].color_num);
      }
      x += width;
    }
    write_decoded_image(out_dir, base_filename, res, ".bmp", img);
  }
}

void write_decoded_pltt(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  // Always write the raw for this resource type because the decoded version
  // loses precision
  write_decoded_file(out_dir, base_filename, res, ".bin", res.data);

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
    ResourceFile& rf, const ResourceFile::Resource& res) {
  // Always write the raw for this resource type because the decoded version
  // loses precision
  write_decoded_file(out_dir, base_filename, res, ".bin", res.data);

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
    index_names = &index_names_for_type.at(res.type);
  } catch (const out_of_range&) { }

  // These resources are all the same format, so it's ok to call decode_clut
  // here instead of the type-specific functions
  auto decoded = rf.decode_clut(res);
  write_decoded_color_table(out_dir, base_filename, res, decoded, index_names);
}

void write_decoded_PAT(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  Image decoded = rf.decode_PAT(res);

  Image tiled = tile_image(decoded, 8, 8);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
  write_decoded_image(out_dir, base_filename, res, "_tiled.bmp", tiled);
}

void write_decoded_PATN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
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
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_SICN(res);

  for (size_t x = 0; x < decoded.size(); x++) {
    string after = string_printf("_%zu.bmp", x);
    write_decoded_image(out_dir, base_filename, res, after, decoded[x]);
  }
}

void write_decoded_ICNN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_ICNN(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_icmN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_icmN(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_icsN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_icsN(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_kcsN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_kcsN(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_cicn(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_cicn(res);

  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded.image);

  if (decoded.bitmap.get_width() && decoded.bitmap.get_height()) {
    write_decoded_image(out_dir, base_filename, res, "_bitmap.bmp", decoded.bitmap);
  }
}

void write_decoded_icl8(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_icl8(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_icm8(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_icm8(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_ics8(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_ics8(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_kcs8(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_kcs8(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_icl4(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_icl4(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_icm4(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_icm4(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_ics4(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_ics4(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_kcs4(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_kcs4(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_ICON(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_ICON(res);
  write_decoded_image(out_dir, base_filename, res, ".bmp", decoded);
}

void write_decoded_PICT_internal(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_PICT_internal(res);
  if (!decoded.embedded_image_data.empty()) {
    write_decoded_file(out_dir, base_filename, res, "." + decoded.embedded_image_format, decoded.embedded_image_data);
  } else {
    write_decoded_image(out_dir, base_filename, res, ".bmp", decoded.image);
  }
}

void write_decoded_PICT(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_PICT(res);
  if (!decoded.embedded_image_data.empty()) {
    write_decoded_file(out_dir, base_filename, res, "." + decoded.embedded_image_format, decoded.embedded_image_data);
  } else {
    write_decoded_image(out_dir, base_filename, res, ".bmp", decoded.image);
  }
}

void write_decoded_snd(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  string decoded = rf.decode_snd(res);
  write_decoded_file(out_dir, base_filename, res, ".wav", decoded);
}

void write_decoded_SMSD(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  string decoded = rf.decode_SMSD(res);
  write_decoded_file(out_dir, base_filename, res, ".wav", decoded);
}

void write_decoded_csnd(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  string decoded = rf.decode_csnd(res);
  write_decoded_file(out_dir, base_filename, res, ".wav", decoded);
}

void write_decoded_esnd(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  string decoded = rf.decode_esnd(res);
  write_decoded_file(out_dir, base_filename, res, ".wav", decoded);
}

void write_decoded_ESnd(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  string decoded = rf.decode_ESnd(res);
  write_decoded_file(out_dir, base_filename, res, ".wav", decoded);
}

void write_decoded_cmid(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  string decoded = rf.decode_cmid(res);
  write_decoded_file(out_dir, base_filename, res, ".midi", decoded);
}

void write_decoded_emid(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  string decoded = rf.decode_emid(res);
  write_decoded_file(out_dir, base_filename, res, ".midi", decoded);
}

void write_decoded_ecmi(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  string decoded = rf.decode_ecmi(res);
  write_decoded_file(out_dir, base_filename, res, ".midi", decoded);
}

void write_decoded_FONT_NFNT(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
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
    string after = string_printf("_glyph_%02hX.bmp", decoded.first_char + x);
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

    this_entry_ret += string_printf("  offset: 0x%08X\n", entry.offset);
    this_entry_ret += string_printf("  length: 0x%08X\n", entry.length);
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
    ResourceFile& rf, const ResourceFile::Resource& res) {
  string description = generate_text_for_cfrg(rf.decode_cfrg(res));
  write_decoded_file(out_dir, base_filename, res, ".txt", description);
}

void write_decoded_SIZE(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
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
    ResourceFile& rf, const ResourceFile::Resource& res) {
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
    ResourceFile& rf, const ResourceFile::Resource& res) {
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
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_ROvN(res);

  string disassembly = string_printf("# ROM version: 0x%04hX\n", decoded.rom_version);
  for (size_t x = 0; x < decoded.overrides.size(); x++) {
    const auto& override = decoded.overrides[x];
    string type_name = string_for_resource_type(override.type);
    disassembly += string_printf("# override %zu: %08X (%s) #%hd\n",
        x, override.type, type_name.c_str(), override.id);
  }

  write_decoded_file(out_dir, base_filename, res, ".txt", disassembly);
}

void write_decoded_CODE(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  string disassembly;
  if (res.id == 0) {
    auto decoded = rf.decode_CODE_0(res);
    disassembly += string_printf("# above A5 size: 0x%08X\n", decoded.above_a5_size);
    disassembly += string_printf("# below A5 size: 0x%08X\n", decoded.below_a5_size);
    for (size_t x = 0; x < decoded.jump_table.size(); x++) {
      const auto& e = decoded.jump_table[x];
      disassembly += string_printf("# export %zu: CODE %hd offset 0x%hX after header\n",
          x, e.code_resource_id, e.offset);
    }

  } else {
    auto decoded = rf.decode_CODE(res);

    // attempt to decode CODE 0 to get the exported label offsets
    multimap<uint32_t, string> labels;
    try {
      auto code0_data = rf.decode_CODE_0(0, res.type);
      for (size_t x = 0; x < code0_data.jump_table.size(); x++) {
        const auto& e = code0_data.jump_table[x];
        if (e.code_resource_id == res.id) {
          labels.emplace(e.offset, string_printf("export_%zu", x));
        }
      }
    } catch (const exception& e) {
      fprintf(stderr, "warning: cannot decode CODE 0 for export labels: %s\n", e.what());
    }

    if (decoded.entry_offset < 0) {
      disassembly += "# far model CODE resource\n";
      disassembly += string_printf("# near model jump table entries starting at A5 + 0x%08X (%u of them)\n",
          decoded.near_entry_start_a5_offset, decoded.near_entry_count);
      disassembly += string_printf("# far model jump table entries starting at A5 + 0x%08X (%u of them)\n",
          decoded.far_entry_start_a5_offset, decoded.far_entry_count);
      disassembly += string_printf("# A5 relocation data at 0x%08X\n", decoded.a5_relocation_data_offset);
      disassembly += string_printf("# A5 is 0x%08X\n", decoded.a5);
      disassembly += string_printf("# PC relocation data at 0x%08X\n", decoded.pc_relocation_data_offset);
      disassembly += string_printf("# load address is 0x%08X\n", decoded.load_address);
    } else {
      disassembly += "# near model CODE resource\n";
      disassembly += string_printf("# entry label at 0x%04X\n", decoded.entry_offset);
      labels.emplace(decoded.entry_offset, "entry");
    }

    disassembly += M68KEmulator::disassemble(decoded.code.data(), decoded.code.size(), 0, &labels);
  }

  write_decoded_file(out_dir, base_filename, res, ".txt", disassembly);
}

void write_decoded_DRVR(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
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
    ResourceFile& rf, const ResourceFile::Resource& res) {
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
    ResourceFile& rf, const ResourceFile::Resource& res) {
  multimap<uint32_t, string> labels;
  labels.emplace(0, "start");
  string result = M68KEmulator::disassemble(res.data.data(), res.data.size(), 0,
      &labels);
  write_decoded_file(out_dir, base_filename, res, ".txt", result);
}

void write_decoded_inline_ppc32(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  multimap<uint32_t, string> labels;
  labels.emplace(0, "start");
  string result = PPC32Emulator::disassemble(res.data.data(), res.data.size(),
      0, &labels);
  write_decoded_file(out_dir, base_filename, res, ".txt", result);
}

void write_decoded_peff(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto peff = rf.decode_peff(res);
  string filename = output_filename(out_dir, base_filename, res, ".txt");
  auto f = fopen_unique(filename, "wt");
  peff.print(f.get());
  fprintf(stderr, "... %s\n", filename.c_str());
}

void write_decoded_inline_68k_or_peff(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  if (res.data.size() < 4) {
    throw runtime_error("can\'t determine code type");
  }
  if (*reinterpret_cast<const uint32_t*>(res.data.data()) == bswap32(0x4A6F7921)) {
    write_decoded_peff(out_dir, base_filename, rf, res);
  } else {
    write_decoded_inline_68k(out_dir, base_filename, rf, res);
  }
}

void write_decoded_TEXT(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_TEXT(res));
}

void write_decoded_card(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_card(res));
}

void write_decoded_styl(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".rtf", rf.decode_styl(res));
}

void write_decoded_STR(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_STR(res);

  write_decoded_file(out_dir, base_filename, res, ".txt", decoded.str);
  if (!decoded.after_data.empty()) {
    write_decoded_file(out_dir, base_filename, res, "_data.bin", decoded.after_data);
  }
}

void write_decoded_STRN(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto decoded = rf.decode_STRN(res);

  for (size_t x = 0; x < decoded.strs.size(); x++) {
    string after = string_printf("_%lu.txt", x);
    write_decoded_file(out_dir, base_filename, res, after, decoded.strs[x]);
  }
  if (!decoded.after_data.empty()) {
    write_decoded_file(out_dir, base_filename, res, "_excess.bin", decoded.after_data);
  }
}

string generate_json_for_SONG(const string& base_filename, ResourceFile& rf,
    const ResourceFile::DecodedSongResource* s) {
  string midi_filename;
  if (s) {
    static const vector<uint32_t> midi_types({RESOURCE_TYPE_MIDI, RESOURCE_TYPE_Midi, RESOURCE_TYPE_midi, RESOURCE_TYPE_cmid});
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

  auto add_instrument = [&](uint16_t id, const ResourceFile::DecodedInstrumentResource& inst) {

    // soundmusicsys has a (bug? feature?) where the instrument's base note
    // affects which key region is used, but then the key region's base note
    // determines the played note pitch and the instrument's base note is
    // ignored. to correct for this, we have to shift all the key regions
    // up/down by an appropriate amount, but also use freq_mult to adjust
    // their pitches
    int8_t key_region_boundary_shift = 0;
    if ((inst.key_regions.size() > 1) && inst.base_note) {
      key_region_boundary_shift = inst.base_note - 0x3C;
    }

    vector<shared_ptr<JSONObject>> key_regions_list;
    for (const auto& rgn : inst.key_regions) {
      const auto& snd_res = rf.get_resource(rgn.snd_type, rgn.snd_id);
      string snd_filename = output_filename("", base_filename, snd_res, ".wav");
      unordered_map<string, shared_ptr<JSONObject>> key_region_dict;
      key_region_dict.emplace("key_low", new JSONObject(static_cast<int64_t>(rgn.key_low + key_region_boundary_shift)));
      key_region_dict.emplace("key_high", new JSONObject(static_cast<int64_t>(rgn.key_high + key_region_boundary_shift)));
      key_region_dict.emplace("filename", new JSONObject(snd_filename));

      uint8_t snd_base_note = 0x3C;
      uint32_t snd_sample_rate = 22050;
      try {
        // TODO: this is dumb; we only need the sample rate and base note.
        // find a way to not have to re-decode the sound
        // also this code is bad because it uses raw offsets into the wav header
        // which will break if we add new sections or something in the future
        string decoded_snd;
        if (rgn.snd_type == RESOURCE_TYPE_esnd) {
          decoded_snd = rf.decode_esnd(rgn.snd_id, rgn.snd_type);
        } else if (rgn.snd_type == RESOURCE_TYPE_csnd) {
          decoded_snd = rf.decode_csnd(rgn.snd_id, rgn.snd_type);
        } else if (rgn.snd_type == RESOURCE_TYPE_snd) {
          decoded_snd = rf.decode_snd(rgn.snd_id, rgn.snd_type);
        } else {
          throw logic_error("invalid snd type");
        }
        if (decoded_snd.size() < 0x3C) {
          throw logic_error("decoded snd is too small");
        }
        snd_sample_rate = *reinterpret_cast<const uint32_t*>(decoded_snd.data() + 0x18);
        if (*reinterpret_cast<const uint32_t*>(decoded_snd.data() + 0x24) == bswap32(0x736D706C)) {
          snd_base_note = *reinterpret_cast<const uint8_t*>(decoded_snd.data() + 0x38);
        }

      } catch (const exception& e) {
        fprintf(stderr, "warning: failed to get sound metadata for instrument %hu region %hhX-%hhX from snd/csnd/esnd %hu: %s\n",
            id, rgn.key_low, rgn.key_high, rgn.snd_id, e.what());
      }

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

    instruments.emplace_back(new JSONObject(inst_dict));
  };

  // first add the overrides, then add all the other instruments
  if (s) {
    for (const auto& it : s->instrument_overrides) {
      try {
        add_instrument(it.first, rf.decode_INST(it.second));
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
      add_instrument(id, rf.decode_INST(id));
    } catch (const exception& e) {
      fprintf(stderr, "warning: failed to add instrument %hu: %s\n", id, e.what());
    }
  }

  unordered_map<string, shared_ptr<JSONObject>> base_dict;
  base_dict.emplace("sequence_type", new JSONObject("MIDI"));
  base_dict.emplace("sequence_filename", new JSONObject(midi_filename));
  base_dict.emplace("instruments", new JSONObject(instruments));
  if (s && s->tempo_bias && (s->tempo_bias != 16667)) {
    base_dict.emplace("tempo_bias", new JSONObject(static_cast<double>(s->tempo_bias) / 16667.0));
  }
  if (s && s->percussion_instrument) {
    base_dict.emplace("percussion_instrument", new JSONObject(static_cast<int64_t>(s->percussion_instrument)));
  }
  base_dict.emplace("allow_program_change", new JSONObject(static_cast<bool>(s ? s->allow_program_change : true)));

  shared_ptr<JSONObject> json(new JSONObject(base_dict));
  return json->format();
}

void write_decoded_SONG(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  auto song = rf.decode_SONG(res);
  string json_data = generate_json_for_SONG(base_filename, rf, &song);
  write_decoded_file(out_dir, base_filename, res, "_smssynth_env.json", json_data);
}

void write_decoded_Tune(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  string decoded = rf.decode_Tune(res);
  write_decoded_file(out_dir, base_filename, res, ".midi", decoded);
}



typedef void (*resource_decode_fn)(const string& out_dir,
    const string& base_filename, ResourceFile& file,
    const ResourceFile::Resource& res);

static unordered_map<uint32_t, resource_decode_fn> type_to_decode_fn({
  {RESOURCE_TYPE_actb, write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_ADBS, write_decoded_inline_68k},
  {RESOURCE_TYPE_card, write_decoded_card},
  {RESOURCE_TYPE_cctb, write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_CDEF, write_decoded_inline_68k},
  {RESOURCE_TYPE_cfrg, write_decoded_cfrg},
  {RESOURCE_TYPE_cicn, write_decoded_cicn},
  {RESOURCE_TYPE_clok, write_decoded_inline_68k},
  {RESOURCE_TYPE_clut, write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_cmid, write_decoded_cmid},
  {RESOURCE_TYPE_CODE, write_decoded_CODE},
  {RESOURCE_TYPE_crsr, write_decoded_crsr},
  {RESOURCE_TYPE_csnd, write_decoded_csnd},
  {RESOURCE_TYPE_CURS, write_decoded_CURS},
  {RESOURCE_TYPE_dcmp, write_decoded_dcmp},
  {RESOURCE_TYPE_dctb, write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_DRVR, write_decoded_DRVR},
  {RESOURCE_TYPE_ecmi, write_decoded_ecmi},
  {RESOURCE_TYPE_emid, write_decoded_emid},
  {RESOURCE_TYPE_esnd, write_decoded_esnd},
  {RESOURCE_TYPE_ESnd, write_decoded_ESnd},
  {RESOURCE_TYPE_fctb, write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_finf, write_decoded_finf},
  {RESOURCE_TYPE_FONT, write_decoded_FONT_NFNT},
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
  {RESOURCE_TYPE_kcs4, write_decoded_kcs4},
  {RESOURCE_TYPE_kcs8, write_decoded_kcs8},
  {RESOURCE_TYPE_kcsN, write_decoded_kcsN},
  {RESOURCE_TYPE_LDEF, write_decoded_inline_68k},
  {RESOURCE_TYPE_MBDF, write_decoded_inline_68k},
  {RESOURCE_TYPE_MDEF, write_decoded_inline_68k},
  {RESOURCE_TYPE_ncmp, write_decoded_peff},
  {RESOURCE_TYPE_ndmc, write_decoded_peff},
  {RESOURCE_TYPE_ndrv, write_decoded_peff},
  {RESOURCE_TYPE_NFNT, write_decoded_FONT_NFNT},
  {RESOURCE_TYPE_nift, write_decoded_peff},
  {RESOURCE_TYPE_nitt, write_decoded_peff},
  {RESOURCE_TYPE_nlib, write_decoded_peff},
  {RESOURCE_TYPE_nsnd, write_decoded_peff},
  {RESOURCE_TYPE_ntrb, write_decoded_peff},
  {RESOURCE_TYPE_PACK, write_decoded_inline_68k},
  {RESOURCE_TYPE_PAT , write_decoded_PAT},
  {RESOURCE_TYPE_PATN, write_decoded_PATN},
  {RESOURCE_TYPE_PICT, write_decoded_PICT},
  {RESOURCE_TYPE_pltt, write_decoded_pltt},
  {RESOURCE_TYPE_ppat, write_decoded_ppat},
  {RESOURCE_TYPE_pptN, write_decoded_pptN},
  {RESOURCE_TYPE_proc, write_decoded_inline_68k},
  {RESOURCE_TYPE_PTCH, write_decoded_inline_68k},
  {RESOURCE_TYPE_ptch, write_decoded_inline_68k},
  {RESOURCE_TYPE_ROvN, write_decoded_ROvN},
  {RESOURCE_TYPE_ROvr, write_decoded_inline_68k},
  {RESOURCE_TYPE_SERD, write_decoded_inline_68k},
  {RESOURCE_TYPE_SICN, write_decoded_SICN},
  {RESOURCE_TYPE_SIZE, write_decoded_SIZE},
  {RESOURCE_TYPE_SMOD, write_decoded_inline_68k},
  {RESOURCE_TYPE_SMSD, write_decoded_SMSD},
  {RESOURCE_TYPE_snd , write_decoded_snd},
  {RESOURCE_TYPE_snth, write_decoded_inline_68k},
  {RESOURCE_TYPE_SONG, write_decoded_SONG},
  {RESOURCE_TYPE_STR , write_decoded_STR},
  {RESOURCE_TYPE_STRN, write_decoded_STRN},
  {RESOURCE_TYPE_styl, write_decoded_styl},
  {RESOURCE_TYPE_TEXT, write_decoded_TEXT},
  {RESOURCE_TYPE_Tune, write_decoded_Tune},
  {RESOURCE_TYPE_wctb, write_decoded_clut_actb_cctb_dctb_fctb_wctb},
  {RESOURCE_TYPE_WDEF, write_decoded_inline_68k},
  {RESOURCE_TYPE_cdek, write_decoded_peff},
  {RESOURCE_TYPE_dcod, write_decoded_peff},
  {RESOURCE_TYPE_fovr, write_decoded_peff},
  {RESOURCE_TYPE_ppct, write_decoded_peff},
  {RESOURCE_TYPE_qtcm, write_decoded_peff},
  {RESOURCE_TYPE_scal, write_decoded_peff},
  {RESOURCE_TYPE_sfvr, write_decoded_peff},
  {RESOURCE_TYPE_vers, write_decoded_vers},

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
  {RESOURCE_TYPE_MOOV, "mov"},
  {RESOURCE_TYPE_MooV, "mov"},
  {RESOURCE_TYPE_moov, "mov"},
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

  ResourceExporter()
    : use_data_fork(false),
      save_raw(SaveRawBehavior::IfDecodeFails),
      decompress_flags(0),
      skip_uncompressed(false) { }
  ~ResourceExporter() = default;

  bool use_data_fork;
  SaveRawBehavior save_raw;
  uint64_t decompress_flags;
  unordered_set<uint32_t> target_types;
  unordered_set<int16_t> target_ids;
  unordered_set<string> target_names;
  std::vector<std::string> external_preprocessor_command;
  bool skip_uncompressed;

  bool export_resource(const string& base_filename, const string& out_dir,
      ResourceFile& rf, const ResourceFile::Resource& res) {

    bool decompression_failed = res.flags & ResourceFlag::FLAG_DECOMPRESSION_FAILED;
    bool is_compressed = res.flags & ResourceFlag::FLAG_COMPRESSED;
    bool was_compressed = res.flags & ResourceFlag::FLAG_DECOMPRESSED;
    if (decompression_failed || is_compressed) {
      auto type_str = string_for_resource_type(res.type);
      fprintf(stderr,
          decompression_failed
            ? "warning: failed to decompress resource %s:%d; saving raw compressed data\n"
            : "note: resource %s:%d is compressed; saving raw compressed data\n",
          type_str.c_str(), res.id);
    }
    if (this->skip_uncompressed &&
        !(is_compressed || was_compressed || decompression_failed)) {
      return false;
    }

    bool write_raw = (this->save_raw == SaveRawBehavior::Always);
    ResourceFile::Resource preprocessed_res;
    const ResourceFile::Resource* res_to_decode = &res;

    // Run external preprocessor if possible. The resource could still be
    // compressed if --skip-decompression was used or if decompression failed;
    // in these cases it doesn't make sense to run the external preprocessor.
    if (!is_compressed && !this->external_preprocessor_command.empty()) {
      auto result = run_process(this->external_preprocessor_command, &res.data, false);
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
        preprocessed_res.type = res.type;
        preprocessed_res.id = res.id;
        preprocessed_res.flags = res.flags;
        preprocessed_res.name = res.name;
        preprocessed_res.data = move(result.stdout_contents);
        res_to_decode = &preprocessed_res;
      }
    }

    // Decode if possible. If decompression failed, don't bother trying to
    // decode the resource.
    resource_decode_fn decode_fn = type_to_decode_fn[res_to_decode->type];
    if (!is_compressed && decode_fn) {
      try {
        decode_fn(out_dir, base_filename, rf, *res_to_decode);
      } catch (const runtime_error& e) {
        fprintf(stderr, "warning: failed to decode resource: %s\n", e.what());

        // Write the raw version if decoding failed and we didn't write it already
        if (this->save_raw == SaveRawBehavior::IfDecodeFails) {
          write_raw = true;
        }
      }
    } else if (this->save_raw == SaveRawBehavior::IfDecodeFails) {
      write_raw = true;
    }

    if (write_raw) {
      const char* out_ext = "bin";
      try {
        out_ext = type_to_ext.at(res_to_decode->type);
      } catch (const out_of_range&) { }

      string out_filename_after = string_printf(".%s", out_ext);
      string out_filename = output_filename(out_dir, base_filename, *res_to_decode, out_filename_after);

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
    return true;
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
      rf.reset(new ResourceFile(load_file(resource_fork_filename)));
    } catch (const cannot_open_file&) {
      fprintf(stderr, "failed on %s: no resource fork present\n", filename.c_str());
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
        if (!this->target_types.empty() && !this->target_types.count(it.first)) {
          continue;
        }
        if (!this->target_ids.empty() && !this->target_ids.count(it.second)) {
          continue;
        }
        const auto& res = rf->get_resource(it.first, it.second, this->decompress_flags);
        if (!this->target_names.empty() && !this->target_names.count(res.name)) {
          continue;
        }
        if (it.first == RESOURCE_TYPE_INST) {
          has_INST = true;
        }
        ret |= export_resource(base_filename.c_str(), out_dir.c_str(), *rf, res);
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
          string json_data = generate_json_for_SONG(base_filename, *rf, nullptr);
          save_file(json_filename.c_str(), json_data);
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
        ret |= disassemble_path(filename + "/" + item, sub_out_dir);
      }
      if (!ret) {
        rmdir(sub_out_dir.c_str());
      }
      return ret;

    } else {
      fprintf(stderr, ">>> %s\n", filename.c_str());
      return disassemble_file(filename, out_dir);
    }
  }
};



void print_usage(const char* argv0) {
  fprintf(stderr, "\
Fuzziqer Software Classic Mac OS resource fork disassembler\n\
\n\
Usage: %s [options] input_filename [output_directory]\n\
\n\
If input_filename is a directory, resource_dasm decodes all resources in all\n\
files and subdirectories within that directory, producing a parallel directory\n\
structure in the output directory.\n\
\n\
If output_directory is not given, the directory <input_filename>.out is created\n\
and the output is written there.\n\
\n\
Standard options:\n\
  --target-type=TYPE\n\
      Only extract resources of this type (can be given multiple times).\n\
  --target-id=ID\n\
      Only extract resources with this ID (can be given multiple times).\n\
  --target-name=NAME\n\
      Only extract resources with this name (can be given multiple times).\n\
  --skip-decode\n\
      Don\'t decode resources into modern formats; extract raw contents only.\n\
  --save-raw=no\n\
      Don\'t save any raw files; only save decoded resources.\n\
  --save-raw=if-decode-fails\n\
      Only save a raw file if the resource can\'t be decoded (default).\n\
  --save-raw=yes\n\
      Save raw files even for resources that are successfully decoded.\n\
  --copy-handler=TYP1,TYP2\n\
      Decode TYP2 resources as if they were TYP1.\n\
  --data-fork\n\
      Disassemble the file\'s data fork as if it were the resource fork.\n\
  --no-external-decoders\n\
      Only use internal decoders. Currently, this only disables the use of\n\
      picttoppm for decoding PICT resources.\n\
  --external-preprocessor=COMMAND\n\
      Before decoding resource data, pass it through this external program.\n\
      The resource data, after built-in decompression if necessary, will be\n\
      passed to the specified command via stdin, and the command\'s output on\n\
      stdout will be treated as the resource data to decode. This can be used\n\
      to mostly-transparently decompress some custom compression formats.\n\
\n\
Decompression options:\n\
  --skip-uncompressed\n\
      Only export resources that are compressed in the source file.\n\
  --skip-decompression\n\
      Do not attempt to decompress compressed resources; instead, export the\n\
      compressed data as-is.\n\
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
  --skip-system-dcmp\n\
      Don\'t attempt to use the default 68K decompressors.\n\
  --skip-system-ncmp\n\
      Don\'t attempt to use the default PEFF decompressors.\n\
\n\
To decode an already-exported resource:\n\
  Use --decode-type=TYPE. resource_dasm will decode the input file\'s data fork\n\
  as if it\'s a single resource of the given type. If this option is given, all\n\
  other options are ignored.\n\
\n\
To disassemble machine code:\n\
  Use --disassemble-68k or --disassemble-ppc for raw machine code, or\n\
  --disassemble-pef for a PEFF (Preferred Executable Format) executable. If no\n\
  input filename is given in this mode, the data from stdin is disassembled\n\
  instead. If no output filename is given, the disassembly is written to\n\
  stdout. Note that CODE resources have a small header before the actual code;\n\
  to disassemble an exported CODE resource, use --decode-type=CODE instead.\n\
  Options for disassembling:\n\
    --parse-data\n\
        Treat the input data as a hexadecimal string instead of raw (binary)\n\
        machine code. This is useful when pasting data into a terminal from a\n\
        hex editor.\n\
\n", argv0);
}

int main(int argc, char* argv[]) {
  signal(SIGPIPE, SIG_IGN);

  ResourceExporter exporter;
  string filename;
  string out_dir;
  uint32_t decode_type = 0;
  bool disassemble_68k = false;
  bool disassemble_ppc = false;
  bool disassemble_pef = false;
  bool parse_data = false;
  for (int x = 1; x < argc; x++) {
    if (argv[x][0] == '-') {
      if (!strncmp(argv[x], "--decode-type=", 14)) {
        if (strlen(argv[x]) != 18) {
          fprintf(stderr, "incorrect format for --decode-type: %s (type must be 4 bytes)\n", argv[x]);
          return 1;
        }
        decode_type = bswap32(*(uint32_t*)&argv[x][14]);

      } else if (!strcmp(argv[x], "--disassemble-68k")) {
        disassemble_68k = true;
      } else if (!strcmp(argv[x], "--disassemble-ppc")) {
        disassemble_ppc = true;
      } else if (!strcmp(argv[x], "--disassemble-pef")) {
        disassemble_pef = true;

      } else if (!strcmp(argv[x], "--parse-data")) {
        parse_data = true;

      } else if (!strncmp(argv[x], "--copy-handler=", 15)) {
        if (strlen(argv[x]) != 24 || argv[x][19] != ',') {
          fprintf(stderr, "incorrect format for --copy-handler: %s (types must be 4 bytes each)\n", argv[x]);
          return 1;
        }
        uint32_t from_type = bswap32(*(uint32_t*)&argv[x][15]);
        uint32_t to_type = bswap32(*(uint32_t*)&argv[x][20]);
        if (!type_to_decode_fn.count(from_type)) {
          fprintf(stderr, "no handler exists for type %.4s\n", (const char*)&from_type);
          return 1;
        }
        fprintf(stderr, "note: treating %.4s resources as %.4s\n", (const char*)&to_type,
            (const char*)&from_type);
        type_to_decode_fn[to_type] = type_to_decode_fn[from_type];

      } else if (!strcmp(argv[x], "--no-external-decoders")) {
        type_to_decode_fn[RESOURCE_TYPE_PICT] = write_decoded_PICT_internal;

      } else if (!strncmp(argv[x], "--external-preprocessor=", 24)) {
        exporter.external_preprocessor_command = split(&argv[x][24], ' ');

      } else if (!strncmp(argv[x], "--target-type=", 14)) {
        uint32_t target_type;
        size_t type_len = strlen(argv[x]) - 14;
        if (type_len == 0) {
          target_type = 0x20202020;

        } else if (type_len == 1) {
          target_type = argv[x][14] << 24 | 0x00202020;

        } else if (type_len == 2) {
          target_type = (argv[x][14] << 24) | (argv[x][15] << 16) | 0x00002020;

        } else if (type_len == 3) {
          target_type = (argv[x][14] << 24) | (argv[x][15] << 16) | (argv[x][16] << 8) | 0x00000020;

        } else if (type_len == 4) {
          target_type = (argv[x][14] << 24) | (argv[x][15] << 16) | (argv[x][16] << 8) | argv[x][17];

        } else {
          fprintf(stderr, "incorrect format for --target-type: %s (type must be 4 bytes)\n", argv[x]);
          return 1;
        }

        exporter.target_types.emplace(target_type);
        fprintf(stderr, "note: added %08" PRIX32 " (%.4s) to target types\n",
            target_type, &argv[x][14]);

      } else if (!strncmp(argv[x], "--target-id=", 12)) {
        int16_t target_id = strtol(&argv[x][12], nullptr, 0);
        exporter.target_ids.emplace(target_id);
        fprintf(stderr, "note: added %04" PRIX16 " (%" PRId16 ") to target ids\n",
            target_id, target_id);

      } else if (!strncmp(argv[x], "--target-name=", 14)) {
        exporter.target_names.emplace(&argv[x][14]);
        fprintf(stderr, "note: added %s to target names\n", &argv[x][14]);

      } else if (!strcmp(argv[x], "--skip-decode")) {
        fprintf(stderr, "note: skipping all decoding steps\n");
        type_to_decode_fn.clear();

      } else if (!strcmp(argv[x], "--save-raw=no")) {
        fprintf(stderr, "note: only writing decoded resources\n");
        exporter.save_raw = ResourceExporter::SaveRawBehavior::Never;

      } else if (!strcmp(argv[x], "--save-raw=if-decode-fails")) {
        fprintf(stderr, "note: writing raw resources if decode fails\n");
        exporter.save_raw = ResourceExporter::SaveRawBehavior::IfDecodeFails;

      } else if (!strcmp(argv[x], "--save-raw=yes")) {
        fprintf(stderr, "note: writing all raw resources\n");
        exporter.save_raw = ResourceExporter::SaveRawBehavior::Always;

      } else if (!strcmp(argv[x], "--data-fork")) {
        fprintf(stderr, "note: reading data forks as resource forks\n");
        exporter.use_data_fork = true;

      } else if (!strcmp(argv[x], "--skip-uncompressed")) {
        exporter.skip_uncompressed = true;

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
        print_usage(argv[0]);
        return 1;
      }
    }
  }

  if (disassemble_ppc || disassemble_68k || disassemble_pef) {
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
        f.print(out.get());
      } else {
        f.print(stdout);
      }

    } else {
      string disassembly = disassemble_68k
          ? M68KEmulator::disassemble(data.data(), data.size(), 0, nullptr)
          : PPC32Emulator::disassemble(data.data(), data.size());
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
    print_usage(argv[0]);
    return 1;
  }

  if (decode_type) {
    if (!out_dir.empty()) {
      print_usage(argv[0]);
      return 1;
    }

    resource_decode_fn decode_fn = type_to_decode_fn[decode_type];
    if (!decode_fn) {
      fprintf(stderr, "error: cannot decode resources of this type\n");
      return 2;
    }

    ResourceFile::Resource res(decode_type, 1, load_file(filename));
    ResourceFile rf(res);
    try {
      decode_fn(out_dir, filename, rf, res);
    } catch (const runtime_error& e) {
      fprintf(stderr, "error: failed to decode %s: %s\n",
          filename.c_str(), e.what());
      return 3;
    }

    return 0;
  }

  if (out_dir.empty()) {
    out_dir = filename + ".out";
  }
  mkdir(out_dir.c_str(), 0777);

  exporter.disassemble_path(filename, out_dir);

  return 0;
}
