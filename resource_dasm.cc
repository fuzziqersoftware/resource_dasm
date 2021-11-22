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
    const vector<Color>& decoded) {
  Image img(140, 16 * decoded.size(), false);
  img.clear(0x00, 0x00, 0x00);
  for (size_t z = 0; z < decoded.size(); z++) {
    img.fill_rect(0, 16 * z, 16, 16, decoded[z].r >> 8, decoded[z].g >> 8, decoded[z].b >> 8);

    ssize_t x = 20, y = 16 * z + 4, width = 0;
    img.draw_text(x, y, &width, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
        0x00, "#");
    x += width;

    img.draw_text(x, y, &width, NULL, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
        0x00, "%04hX", decoded[z].r);
    x += width;

    img.draw_text(x, y, &width, NULL, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00,
        0x00, "%04hX", decoded[z].g);
    x += width;

    img.draw_text(x, y, &width, NULL, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00,
        0x00, "%04hX", decoded[z].b);
    x += width;

    img.draw_text(x, y, &width, NULL, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
        0x00, " (%zu)", z);
    x += width;
  }
  write_decoded_image(out_dir, base_filename, res, ".bmp", img);
}

void write_decoded_pltt(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  // always write the raw for this resource type because the decoded version
  // loses precision
  write_decoded_file(out_dir, base_filename, res, ".bin", res.data);

  auto decoded = rf.decode_pltt(res);
  write_decoded_color_table(out_dir, base_filename, res, decoded);
}

void write_decoded_clut(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  // always write the raw for this resource type because the decoded version
  // loses precision
  write_decoded_file(out_dir, base_filename, res, ".bin", res.data);

  auto decoded = rf.decode_clut(res);
  write_decoded_color_table(out_dir, base_filename, res, decoded);
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

string generate_text_for_cfrg(const vector<ResourceFile::DecodedCodeFragmentEntry>& entries) {
  string ret;
  for (size_t x = 0; x < entries.size(); x++) {
    const auto& entry = entries[x];

    string arch_str = string_for_resource_type(entry.architecture);
    string this_entry_ret;
    if (entry.name.empty()) {
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

void write_decoded_dcmp(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_dcmp(res));
}

void write_decoded_CDEF(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_CDEF(res));
}

void write_decoded_INIT(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_INIT(res));
}

void write_decoded_LDEF(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_LDEF(res));
}

void write_decoded_MDBF(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_MDBF(res));
}

void write_decoded_MDEF(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_MDEF(res));
}

void write_decoded_PACK(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_PACK(res));
}

void write_decoded_PTCH(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_PTCH(res));
}

void write_decoded_WDEF(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_WDEF(res));
}

void write_decoded_ADBS(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_ADBS(res));
}

void write_decoded_clok(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_clok(res));
}

void write_decoded_proc(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_proc(res));
}

void write_decoded_ptch(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_ptch(res));
}

void write_decoded_ROvr(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_ROvr(res));
}

void write_decoded_SERD(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_SERD(res));
}

void write_decoded_snth(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_snth(res));
}

void write_decoded_SMOD(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_SMOD(res));
}

void write_decoded_peff_file(const string& out_dir, const string& base_filename,
    const ResourceFile::Resource& res, const PEFFFile& peff) {
  string filename = output_filename(out_dir, base_filename, res, ".txt");
  auto f = fopen_unique(filename, "wt");
  peff.print(f.get());
  fprintf(stderr, "... %s\n", filename.c_str());
}

void write_decoded_ncmp(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_peff_file(out_dir, base_filename, res, rf.decode_ncmp(res));
}

void write_decoded_ndmc(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_peff_file(out_dir, base_filename, res, rf.decode_ndmc(res));
}

void write_decoded_ndrv(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_peff_file(out_dir, base_filename, res, rf.decode_ndrv(res));
}

void write_decoded_nift(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_peff_file(out_dir, base_filename, res, rf.decode_nift(res));
}

void write_decoded_nitt(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_peff_file(out_dir, base_filename, res, rf.decode_nitt(res));
}

void write_decoded_nlib(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_peff_file(out_dir, base_filename, res, rf.decode_nlib(res));
}

void write_decoded_nsnd(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_peff_file(out_dir, base_filename, res, rf.decode_nsnd(res));
}

void write_decoded_ntrb(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_peff_file(out_dir, base_filename, res, rf.decode_nsnd(res));
}

void write_decoded_TEXT(const string& out_dir, const string& base_filename,
    ResourceFile& rf, const ResourceFile::Resource& res) {
  write_decoded_file(out_dir, base_filename, res, ".txt", rf.decode_TEXT(res));
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
  {RESOURCE_TYPE_ADBS, write_decoded_ADBS},
  {RESOURCE_TYPE_CDEF, write_decoded_CDEF},
  {RESOURCE_TYPE_cfrg, write_decoded_cfrg},
  {RESOURCE_TYPE_cicn, write_decoded_cicn},
  {RESOURCE_TYPE_clok, write_decoded_clok},
  {RESOURCE_TYPE_clut, write_decoded_clut},
  {RESOURCE_TYPE_cmid, write_decoded_cmid},
  {RESOURCE_TYPE_CODE, write_decoded_CODE},
  {RESOURCE_TYPE_crsr, write_decoded_crsr},
  {RESOURCE_TYPE_csnd, write_decoded_csnd},
  {RESOURCE_TYPE_CURS, write_decoded_CURS},
  {RESOURCE_TYPE_dcmp, write_decoded_dcmp},
  {RESOURCE_TYPE_ecmi, write_decoded_ecmi},
  {RESOURCE_TYPE_emid, write_decoded_emid},
  {RESOURCE_TYPE_esnd, write_decoded_esnd},
  {RESOURCE_TYPE_ESnd, write_decoded_ESnd},
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
  {RESOURCE_TYPE_INIT, write_decoded_INIT},
  {RESOURCE_TYPE_kcs4, write_decoded_kcs4},
  {RESOURCE_TYPE_kcs8, write_decoded_kcs8},
  {RESOURCE_TYPE_kcsN, write_decoded_kcsN},
  {RESOURCE_TYPE_LDEF, write_decoded_LDEF},
  {RESOURCE_TYPE_MDBF, write_decoded_MDBF},
  {RESOURCE_TYPE_MDEF, write_decoded_MDEF},
  {RESOURCE_TYPE_ncmp, write_decoded_ncmp},
  {RESOURCE_TYPE_ndmc, write_decoded_ndmc},
  {RESOURCE_TYPE_ndrv, write_decoded_ndrv},
  {RESOURCE_TYPE_nift, write_decoded_nift},
  {RESOURCE_TYPE_nitt, write_decoded_nitt},
  {RESOURCE_TYPE_nlib, write_decoded_nlib},
  {RESOURCE_TYPE_nsnd, write_decoded_nsnd},
  {RESOURCE_TYPE_ntrb, write_decoded_ntrb},
  {RESOURCE_TYPE_PACK, write_decoded_PACK},
  {RESOURCE_TYPE_PAT , write_decoded_PAT},
  {RESOURCE_TYPE_PATN, write_decoded_PATN},
  {RESOURCE_TYPE_PICT, write_decoded_PICT},
  {RESOURCE_TYPE_pltt, write_decoded_pltt},
  {RESOURCE_TYPE_ppat, write_decoded_ppat},
  {RESOURCE_TYPE_pptN, write_decoded_pptN},
  {RESOURCE_TYPE_proc, write_decoded_proc},
  {RESOURCE_TYPE_PTCH, write_decoded_PTCH},
  {RESOURCE_TYPE_ptch, write_decoded_ptch},
  {RESOURCE_TYPE_ROvr, write_decoded_ROvr},
  {RESOURCE_TYPE_SERD, write_decoded_SERD},
  {RESOURCE_TYPE_SICN, write_decoded_SICN},
  {RESOURCE_TYPE_SIZE, write_decoded_SIZE},
  {RESOURCE_TYPE_SMOD, write_decoded_SMOD},
  {RESOURCE_TYPE_SMSD, write_decoded_SMSD},
  {RESOURCE_TYPE_snd , write_decoded_snd},
  {RESOURCE_TYPE_snth, write_decoded_snth},
  {RESOURCE_TYPE_SONG, write_decoded_SONG},
  {RESOURCE_TYPE_STR , write_decoded_STR},
  {RESOURCE_TYPE_STRN, write_decoded_STRN},
  {RESOURCE_TYPE_styl, write_decoded_styl},
  {RESOURCE_TYPE_TEXT, write_decoded_TEXT},
  {RESOURCE_TYPE_Tune, write_decoded_Tune},
  {RESOURCE_TYPE_WDEF, write_decoded_WDEF},
});

static const unordered_map<uint32_t, const char*> type_to_ext({
  {RESOURCE_TYPE_icns, "icns"},
  {RESOURCE_TYPE_MADH, "madh"},
  {RESOURCE_TYPE_MOOV, "mov"},
  {RESOURCE_TYPE_MooV, "mov"},
  {RESOURCE_TYPE_moov, "mov"},
  {RESOURCE_TYPE_MIDI, "midi"},
  {RESOURCE_TYPE_Midi, "midi"},
  {RESOURCE_TYPE_midi, "midi"},
  {RESOURCE_TYPE_PICT, "pict"},
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
      decompress_flags(0) { }
  ~ResourceExporter() = default;

  bool use_data_fork;
  SaveRawBehavior save_raw;
  uint64_t decompress_flags;
  unordered_set<uint32_t> target_types;
  unordered_set<int16_t> target_ids;
  unordered_set<string> target_names;
  std::vector<std::string> external_preprocessor_command;

  bool export_resource(const string& base_filename, const string& out_dir,
      ResourceFile& rf, const ResourceFile::Resource& res) {

    if (res.flags & ResourceFlag::FLAG_DECOMPRESSION_FAILED) {
      auto type_str = string_for_resource_type(res.type);
      fprintf(stderr, "warning: failed to decompress resource %s:%d; saving compressed data\n",
          type_str.c_str(), res.id);
    }

    bool write_raw = (this->save_raw == SaveRawBehavior::Always);
    ResourceFile::Resource preprocessed_res;
    const ResourceFile::Resource* res_to_decode = &res;

    // run external preprocessor if needed
    if (!this->external_preprocessor_command.empty()) {
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

    // decode if possible
    resource_decode_fn decode_fn = type_to_decode_fn[res_to_decode->type];
    if (!(res_to_decode->flags & ResourceFlag::FLAG_COMPRESSED) && decode_fn) {
      try {
        decode_fn(out_dir, base_filename, rf, *res_to_decode);
      } catch (const runtime_error& e) {
        fprintf(stderr, "warning: failed to decode resource: %s\n", e.what());

        // write the raw version if decoding failed and we didn't write it already
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
        // hack: PICT resources, when saved to disk, should be prepended with a
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
          string json_data = generate_json_for_SONG(base_filename, *rf, NULL);
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
Decompression debugging options:\n\
  --skip-decompression\n\
      Do not attempt to decompress compressed resources; instead, export the\n\
      compressed data as-is.\n\
  --debug-decompression\n\
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
Exclusive options (if any of these are given, all other options are ignored):\n\
  --decode-type=TYPE\n\
      Decode the input file\'s data fork as if it\'s a single resource of this\n\
      type. This can be used to decode already-exported resources.\n\
  --disassemble-68k\n\
      Disassemble the input file as raw 68K code. Note that CODE resources have\n\
      a small header before the actual code; to disassemble an exported CODE\n\
      resource, use --decode-type=CODE instead.\n\
  --disassemble-ppc\n\
      Disassemble the input file as raw PowerPC code.\n\
  --disassemble-pef\n\
      Disassemble the input file as a Preferred Executable Format (PEF) file.\n\
\n\
Options for --disassemble-68k and --disassemble-ppc:\n\
  --parse-data\n\
      Expect a hexadecimal string instead of raw (binary) machine code. Useful\n\
      when pasting a chunk of data into the terminal, for example.\n\
\n", argv0);
}

int main(int argc, char* argv[]) {
  fprintf(stderr, "Fuzziqer Software Classic Mac OS resource fork disassembler\n\n");

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
        int16_t target_id = strtol(&argv[x][12], NULL, 0);
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

      } else if (!strcmp(argv[x], "--skip-decompression")) {
        exporter.decompress_flags |= DecompressionFlag::DISABLED;

      } else if (!strcmp(argv[x], "--debug-decompression")) {
        exporter.decompress_flags |= DecompressionFlag::VERBOSE;

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

  if (disassemble_ppc || disassemble_68k) {
    string data;
    if (filename.empty()) {
      data = read_all(stdin);
    } else {
      data = load_file(filename);
    }
    if (parse_data) {
      data = parse_data_string(data);
    }

    string disassembly = disassemble_68k
        ? M68KEmulator::disassemble(data.data(), data.size(), 0, nullptr)
        : PPC32Emulator::disassemble(data.data(), data.size());
    if (!out_dir.empty()) {
      auto out = fopen_unique(out_dir, "wt");
      fwritex(out.get(), disassembly);
    } else {
      fwritex(stdout, disassembly);
    }
    return 0;
  }

  if (disassemble_pef) {
    PEFFFile f(filename.c_str());
    if (!out_dir.empty()) {
      auto out = fopen_unique(out_dir, "wt");
      f.print(out.get());
    } else {
      f.print(stdout);
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
