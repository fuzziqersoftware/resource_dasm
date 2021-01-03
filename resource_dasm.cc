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
#include <phosg/Strings.hh>
#include <unordered_map>
#include <vector>

#include "ResourceFile.hh"
#include "M68KEmulator.hh"
#include "PPC32Emulator.hh"

using namespace std;



static string output_filename(const string& out_dir, const string& base_filename,
    uint32_t type, int16_t id, const std::string& name, const std::string& after) {
  if (base_filename.empty()) {
    return out_dir;
  }

  string name_token;
  if (!name.empty()) {
    name_token = '_';
    for (char ch : name) {
      if (ch < 0x20 || ch > 0x7E || ch == '/') {
        name_token += '_';
      } else {
        name_token += ch;
      }
    }
  }

  uint32_t type_sw = bswap32(type);
  if (out_dir.empty()) {
    return string_printf("%s_%.4s_%d%s%s", base_filename.c_str(),
        (const char*)&type_sw, id, name_token.c_str(), after.c_str());
  } else {
    return string_printf("%s/%s_%.4s_%d%s%s", out_dir.c_str(),
        base_filename.c_str(), (const char*)&type_sw, id, name_token.c_str(), after.c_str());
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
    uint32_t type, int16_t id, const string& name, const string& after, const string& data) {
  string filename = output_filename(out_dir, base_filename, type, id, name, after);
  save_file(filename.c_str(), data);
  fprintf(stderr, "... %s\n", filename.c_str());
}

void write_decoded_image(const string& out_dir, const string& base_filename,
    uint32_t type, int16_t id, const string& name, const string& after, const Image& img) {
  string filename = output_filename(out_dir, base_filename, type, id, name, after);
  img.save(filename.c_str(), Image::WindowsBitmap);
  fprintf(stderr, "... %s\n", filename.c_str());
}

void write_decoded_CURS(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_CURS(id, type);
  string after = string_printf("_%hu_%hu.bmp", decoded.hotspot_x, decoded.hotspot_y);
  write_decoded_image(out_dir, base_filename, type, id, name, after, decoded.bitmap);
}

void write_decoded_crsr(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_crsr(id, type);
  string bitmap_after = string_printf("_%hu_%hu_bitmap.bmp", decoded.hotspot_x, decoded.hotspot_y);
  string after = string_printf("_%hu_%hu.bmp", decoded.hotspot_x, decoded.hotspot_y);
  write_decoded_image(out_dir, base_filename, type, id, name, bitmap_after, decoded.bitmap);
  write_decoded_image(out_dir, base_filename, type, id, name, after, decoded.image);
}

void write_decoded_ppat(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_ppat(id, type);

  Image tiled = tile_image(decoded.pattern, 8, 8);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded.pattern);
  write_decoded_image(out_dir, base_filename, type, id, name, "_tiled.bmp", tiled);

  tiled = tile_image(decoded.monochrome_pattern, 8, 8);
  write_decoded_image(out_dir, base_filename, type, id, name, "_bitmap.bmp", decoded.monochrome_pattern);
  write_decoded_image(out_dir, base_filename, type, id, name, "_bitmap_tiled.bmp", tiled);
}

void write_decoded_pptN(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_pptN(id, type);

  for (size_t x = 0; x < decoded.size(); x++) {
    string after = string_printf("_%zu.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, name, after, decoded[x].pattern);

    Image tiled = tile_image(decoded[x].pattern, 8, 8);
    after = string_printf("_%zu_tiled.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, name, after, tiled);

    after = string_printf("_%zu_bitmap.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, name, after, decoded[x].monochrome_pattern);

    tiled = tile_image(decoded[x].monochrome_pattern, 8, 8);
    after = string_printf("_%zu_bitmap_tiled.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, name, after, tiled);
  }
}

void write_decoded_color_table(const string& out_dir,
    const string& base_filename, uint32_t type, int16_t id, const string& name,
    const vector<Color>& decoded) {
  Image img(100, 16 * decoded.size(), false);
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
  }
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", img);
}

void write_decoded_pltt(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  // always write the raw for this resource type because the decoded version
  // loses precision
  write_decoded_file(out_dir, base_filename, type, id, name, ".bin", res.get_resource(type, id).data);

  auto decoded = res.decode_pltt(id, type);
  write_decoded_color_table(out_dir, base_filename, type, id, name, decoded);
}

void write_decoded_clut(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  // always write the raw for this resource type because the decoded version
  // loses precision
  write_decoded_file(out_dir, base_filename, type, id, name, ".bin", res.get_resource(type, id).data);

  auto decoded = res.decode_clut(id, type);
  write_decoded_color_table(out_dir, base_filename, type, id, name, decoded);
}

void write_decoded_PAT(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  Image decoded = res.decode_PAT(id, type);

  Image tiled = tile_image(decoded, 8, 8);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
  write_decoded_image(out_dir, base_filename, type, id, name, "_tiled.bmp", tiled);
}

void write_decoded_PATN(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_PATN(id, type);

  for (size_t x = 0; x < decoded.size(); x++) {
    string after = string_printf("_%zu.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, name, after, decoded[x]);

    Image tiled = tile_image(decoded[x], 8, 8);
    after = string_printf("_%zu_tiled.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, name, after, tiled);
  }
}

void write_decoded_SICN(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_SICN(id, type);

  for (size_t x = 0; x < decoded.size(); x++) {
    string after = string_printf("_%zu.bmp", x);
    write_decoded_image(out_dir, base_filename, type, id, name, after, decoded[x]);
  }
}

void write_decoded_ICNN(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_ICNN(id, type);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
}

void write_decoded_icmN(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_icmN(id, type);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
}

void write_decoded_icsN(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_icsN(id, type);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
}

void write_decoded_kcsN(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_kcsN(id, type);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
}

void write_decoded_cicn(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_cicn(id, type);

  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded.image);

  if (decoded.bitmap.get_width() && decoded.bitmap.get_height()) {
    write_decoded_image(out_dir, base_filename, type, id, name, "_bitmap.bmp", decoded.bitmap);
  }
}

void write_decoded_icl8(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_icl8(id, type);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
}

void write_decoded_icm8(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_icm8(id, type);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
}

void write_decoded_ics8(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_ics8(id, type);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
}

void write_decoded_kcs8(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_kcs8(id, type);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
}

void write_decoded_icl4(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_icl4(id, type);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
}

void write_decoded_icm4(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_icm4(id, type);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
}

void write_decoded_ics4(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_ics4(id, type);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
}

void write_decoded_kcs4(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_kcs4(id, type);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
}

void write_decoded_ICON(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_ICON(id, type);
  write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded);
}

void write_decoded_PICT_internal(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_PICT_internal(id, type);
  if (!decoded.embedded_image_data.empty()) {
    write_decoded_file(out_dir, base_filename, type, id, name, "." + decoded.embedded_image_format, decoded.embedded_image_data);
  } else {
    write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded.image);
  }
}

void write_decoded_PICT(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_PICT(id, type);
  if (!decoded.embedded_image_data.empty()) {
    write_decoded_file(out_dir, base_filename, type, id, name, "." + decoded.embedded_image_format, decoded.embedded_image_data);
  } else {
    write_decoded_image(out_dir, base_filename, type, id, name, ".bmp", decoded.image);
  }
}

void write_decoded_snd(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  string decoded = res.decode_snd(id, type);
  write_decoded_file(out_dir, base_filename, type, id, name, ".wav", decoded);
}

void write_decoded_SMSD(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  string decoded = res.decode_SMSD(id, type);
  write_decoded_file(out_dir, base_filename, type, id, name, ".wav", decoded);
}

void write_decoded_csnd(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  string decoded = res.decode_csnd(id, type);
  write_decoded_file(out_dir, base_filename, type, id, name, ".wav", decoded);
}

void write_decoded_esnd(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  string decoded = res.decode_esnd(id, type);
  write_decoded_file(out_dir, base_filename, type, id, name, ".wav", decoded);
}

void write_decoded_ESnd(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  string decoded = res.decode_ESnd(id, type);
  write_decoded_file(out_dir, base_filename, type, id, name, ".wav", decoded);
}

void write_decoded_cmid(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  string decoded = res.decode_cmid(id, type);
  write_decoded_file(out_dir, base_filename, type, id, name, ".midi", decoded);
}

void write_decoded_emid(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  string decoded = res.decode_emid(id, type);
  write_decoded_file(out_dir, base_filename, type, id, name, ".midi", decoded);
}

void write_decoded_ecmi(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  string decoded = res.decode_ecmi(id, type);
  write_decoded_file(out_dir, base_filename, type, id, name, ".midi", decoded);
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
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  string description = generate_text_for_cfrg(res.decode_cfrg(id, type));
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", description);
}

void write_decoded_SIZE(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_SIZE(id, type);
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
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", disassembly);
}

void write_decoded_CODE(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  string disassembly;
  if (id == 0) {
    auto decoded = res.decode_CODE_0(0, type);
    disassembly += string_printf("# above A5 size: 0x%08X\n", decoded.above_a5_size);
    disassembly += string_printf("# below A5 size: 0x%08X\n", decoded.below_a5_size);
    for (size_t x = 0; x < decoded.jump_table.size(); x++) {
      const auto& e = decoded.jump_table[x];
      disassembly += string_printf("# export %zu: CODE %hd offset 0x%hX after header\n",
          x, e.code_resource_id, e.offset);
    }

  } else {
    auto decoded = res.decode_CODE(id, type);

    // attempt to decode CODE 0 to get the exported label offsets
    unordered_multimap<uint32_t, string> labels;
    try {
      auto code0_data = res.decode_CODE_0(0, type);
      for (size_t x = 0; x < code0_data.jump_table.size(); x++) {
        const auto& e = code0_data.jump_table[x];
        if (e.code_resource_id == id) {
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

  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", disassembly);
}

void write_decoded_dcmp(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_dcmp(id, type));
}

void write_decoded_CDEF(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_CDEF(id, type));
}

void write_decoded_INIT(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_INIT(id, type));
}

void write_decoded_LDEF(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_LDEF(id, type));
}

void write_decoded_MDBF(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_MDBF(id, type));
}

void write_decoded_MDEF(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_MDEF(id, type));
}

void write_decoded_PACK(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_PACK(id, type));
}

void write_decoded_PTCH(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_PTCH(id, type));
}

void write_decoded_WDEF(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_WDEF(id, type));
}

void write_decoded_ADBS(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_ADBS(id, type));
}

void write_decoded_clok(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_clok(id, type));
}

void write_decoded_proc(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_proc(id, type));
}

void write_decoded_ptch(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_ptch(id, type));
}

void write_decoded_ROvr(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_ROvr(id, type));
}

void write_decoded_SERD(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_SERD(id, type));
}

void write_decoded_snth(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_snth(id, type));
}

void write_decoded_SMOD(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", res.decode_SMOD(id, type));
}

void write_decoded_peff_file(const string& out_dir, const string& base_filename,
    uint32_t type, int16_t id, const string& name, const PEFFFile& peff) {
  string filename = output_filename(out_dir, base_filename, type, id, name, ".txt");
  auto f = fopen_unique(filename, "wt");
  peff.print(f.get());
  fprintf(stderr, "... %s\n", filename.c_str());
}

void write_decoded_ncmp(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_peff_file(out_dir, base_filename, type, id, name, res.decode_ncmp(id, type));
}

void write_decoded_ndmc(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_peff_file(out_dir, base_filename, type, id, name, res.decode_ndmc(id, type));
}

void write_decoded_ndrv(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_peff_file(out_dir, base_filename, type, id, name, res.decode_ndrv(id, type));
}

void write_decoded_nift(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_peff_file(out_dir, base_filename, type, id, name, res.decode_nift(id, type));
}

void write_decoded_nitt(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_peff_file(out_dir, base_filename, type, id, name, res.decode_nitt(id, type));
}

void write_decoded_nlib(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_peff_file(out_dir, base_filename, type, id, name, res.decode_nlib(id, type));
}

void write_decoded_nsnd(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_peff_file(out_dir, base_filename, type, id, name, res.decode_nsnd(id, type));
}

void write_decoded_ntrb(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  write_decoded_peff_file(out_dir, base_filename, type, id, name, res.decode_nsnd(id, type));
}

void write_decoded_TEXT(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  string decoded = res.decode_TEXT(id, type);
  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", decoded);
}

void write_decoded_styl(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  string decoded = res.decode_styl(id, type);
  write_decoded_file(out_dir, base_filename, type, id, name, ".rtf", decoded);
}

void write_decoded_STR(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_STR(id, type);

  write_decoded_file(out_dir, base_filename, type, id, name, ".txt", decoded.str);
  if (!decoded.after_data.empty()) {
    write_decoded_file(out_dir, base_filename, type, id, name, "_data.bin", decoded.after_data);
  }
}

void write_decoded_STRN(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto decoded = res.decode_STRN(id, type);

  for (size_t x = 0; x < decoded.strs.size(); x++) {
    string after = string_printf("_%lu.txt", x);
    write_decoded_file(out_dir, base_filename, type, id, name, after, decoded.strs[x]);
  }
  if (!decoded.after_data.empty()) {
    write_decoded_file(out_dir, base_filename, type, id, name, "_excess.bin", decoded.after_data);
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
        midi_filename = output_filename("", base_filename, midi_type, s->midi_id, res.name, ".midi");
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
      string snd_filename = output_filename("", base_filename, snd_res.type, snd_res.id, snd_res.name, ".wav");
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
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  auto song = res.decode_SONG(id, type);
  string json_data = generate_json_for_SONG(base_filename, res, &song);
  write_decoded_file(out_dir, base_filename, type, id, name, "_smssynth_env.json", json_data);
}

void write_decoded_Tune(const string& out_dir, const string& base_filename,
    ResourceFile& res, uint32_t type, int16_t id, const string& name) {
  string decoded = res.decode_Tune(id, type);
  write_decoded_file(out_dir, base_filename, type, id, name, ".midi", decoded);
}



typedef void (*resource_decode_fn)(const string& out_dir,
    const string& base_filename, ResourceFile& file, uint32_t type, int16_t id,
    const string& name);

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



enum class SaveRawBehavior {
  Never = 0,
  IfDecodeFails,
  Always,
};

bool export_resource(const string& base_filename, ResourceFile& rf,
    const string& out_dir, uint32_t type, int16_t id, SaveRawBehavior save_raw,
    DecompressionMode decompress_mode = DecompressionMode::ENABLED_SILENT) {

  const auto& res = rf.get_resource(type, id, decompress_mode);
  if (res.flags & ResourceFlag::FLAG_DECOMPRESSION_FAILED) {
    auto type_str = string_for_resource_type(type);
    fprintf(stderr, "warning: failed to decompress resource %s:%d; saving compressed data\n",
        type_str.c_str(), id);
  }

  bool write_raw = (save_raw == SaveRawBehavior::Always);

  // decode if possible
  resource_decode_fn decode_fn = type_to_decode_fn[type];
  if (!(res.flags & ResourceFlag::FLAG_COMPRESSED) && decode_fn) {
    try {
      decode_fn(out_dir, base_filename, rf, res.type, res.id, res.name);
    } catch (const runtime_error& e) {
      fprintf(stderr, "warning: failed to decode resource: %s\n", e.what());

      // write the raw version if decoding failed and we didn't write it already
      if (save_raw == SaveRawBehavior::IfDecodeFails) {
        write_raw = true;
      }
    }
  } else if (save_raw == SaveRawBehavior::IfDecodeFails) {
    write_raw = true;
  }

  if (write_raw) {
    // filter the type so it only contains valid filename characters
    uint32_t filtered_type = type;
    char* type_str = reinterpret_cast<char*>(&filtered_type);
    for (size_t x = 0; x < 4; x++) {
      if (type_str[x] < 0x20 || type_str[x] > 0x7E || type_str[x] == '/') {
        type_str[x] = '_';
      }
    }

    const char* out_ext = "bin";
    if (type_to_ext.count(type)) {
      out_ext = type_to_ext.at(type);
    }

    string out_filename_after = string_printf(".%s", out_ext);
    string out_filename = output_filename(out_dir, base_filename, filtered_type, id, res.name, out_filename_after);

    try {
      // hack: PICT resources, when saved to disk, should be prepended with a
      // 512-byte unused header
      if (type == RESOURCE_TYPE_PICT) {
        static const string pict_header(512, 0);
        auto f = fopen_unique(out_filename, "wb");
        fwritex(f.get(), pict_header);
        fwritex(f.get(), res.data);
      } else {
        save_file(out_filename, res.data);
      }
      fprintf(stderr, "... %s\n", out_filename.c_str());
    } catch (const exception& e) {
      uint32_t swapped_filtered_type = bswap32(filtered_type);
      fprintf(stderr, "warning: failed to save raw data for %.4s %d: %s\n",
          (const char*)&swapped_filtered_type, id, e.what());
    }
  }
  return true;
}



bool disassemble_file(const string& filename, const string& out_dir,
    bool use_data_fork, const unordered_set<uint32_t>& target_types,
    const unordered_set<int16_t>& target_ids, SaveRawBehavior save_raw,
    DecompressionMode decompress_mode = DecompressionMode::ENABLED_SILENT) {

  // open resource fork if present
  string resource_fork_filename;
  if (use_data_fork) {
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
      if (!target_types.empty() && !target_types.count(it.first)) {
        continue;
      }
      if (!target_ids.empty() && !target_ids.count(it.second)) {
        continue;
      }
      if (it.first == RESOURCE_TYPE_INST) {
        has_INST = true;
      }
      ret |= export_resource(base_filename.c_str(), *rf, out_dir.c_str(),
          it.first, it.second, save_raw, decompress_mode);
    }

    // special case: if we disassembled any INSTs and the save-raw behavior is
    // not Never, generate an smssynth template file from all the INSTs
    if (has_INST && (save_raw != SaveRawBehavior::Never)) {
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

bool disassemble_path(const string& filename, const string& out_dir,
    bool use_data_fork, const unordered_set<uint32_t>& target_types,
    const unordered_set<int16_t>& target_ids, SaveRawBehavior save_raw,
    DecompressionMode decompress_mode = DecompressionMode::ENABLED_SILENT) {

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
      ret |= disassemble_path(filename + "/" + item, sub_out_dir, use_data_fork,
          target_types, target_ids, save_raw, decompress_mode);
    }
    if (!ret) {
      rmdir(sub_out_dir.c_str());
    }
    return ret;

  } else {
    fprintf(stderr, ">>> %s\n", filename.c_str());
    return disassemble_file(filename, out_dir, use_data_fork, target_types,
        target_ids, save_raw, decompress_mode);
  }
}



void print_usage(const char* argv0) {
  fprintf(stderr, "\
Usage: %s [options] filename [out_directory]\n\
\n\
If out_directory is not given, the directory <filename>.out is created, and the\n\
output is written there.\n\
\n\
Options:\n\
  --decode-type=TYPE\n\
      Decode the file\'s data fork as if it\'s a single resource of this type.\n\
      If this option is given, all other options are ignored. This can be used\n\
      to decode already-exported resources.\n\
  --disassemble-68k\n\
      Disassemble the input file as raw 68K code. If this option is given, all\n\
      other options are ignored.\n\
  --disassemble-ppc\n\
      Disassemble the input file as raw PowerPC code. If this option is given,\n\
      all other options are ignored.\n\
  --target-type=TYPE\n\
      Only extract resources of this type (can be given multiple times).\n\
  --target-id=ID\n\
      Only extract resources with this ID (can be given multiple times).\n\
  --skip-decode\n\
      Don\'t decode resources to modern formats; extract raw contents only.\n\
  --save-raw=no\n\
      Don\'t save any raw files; only save decoded resources.\n\
  --save-raw=if-decode-fails\n\
      Only save a raw file if the resource can\'t be decoded (default).\n\
  --save-raw=yes\n\
      Save raw files even for resources that are successfully decoded.\n\
  --copy-handler=TYP1,TYP2\n\
      Decode TYP2 resources as if they were TYP1.\n\
  --no-external-decoders\n\
      Only use internal decoders. Currently, this only disabled the use of\n\
      picttoppm for decoding PICT resources.\n\
  --data-fork\n\
      Disassemble the file\'s data fork as if it were the resource fork.\n\
  --skip-decompression\n\
      Do not attempt to decompress compressed resources.\n\
  --debug-decompression\n\
      Show debugging output when running resource decompressors.\n\
\n", argv0);
}

int main(int argc, char* argv[]) {

  fprintf(stderr, "fuzziqer software macos resource fork disassembler\n\n");

  string filename;
  string out_dir;
  bool use_data_fork = false;
  SaveRawBehavior save_raw = SaveRawBehavior::IfDecodeFails;
  unordered_set<uint32_t> target_types;
  unordered_set<int16_t> target_ids;
  uint32_t decode_type = 0;
  DecompressionMode decompress_mode = DecompressionMode::ENABLED_SILENT;
  bool disassemble_68k = false;
  bool disassemble_ppc = false;
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

        target_types.emplace(target_type);
        fprintf(stderr, "note: added %08" PRIX32 " (%.4s) to target types\n",
            target_type, &argv[x][14]);

      } else if (!strncmp(argv[x], "--target-id=", 12)) {
        int16_t target_id = strtol(&argv[x][12], NULL, 0);
        target_ids.emplace(target_id);
        fprintf(stderr, "note: added %04" PRIX16 " (%" PRId16 ") to target ids\n",
            target_id, target_id);

      } else if (!strcmp(argv[x], "--skip-decode")) {
        fprintf(stderr, "note: skipping all decoding steps\n");
        type_to_decode_fn.clear();

      } else if (!strcmp(argv[x], "--save-raw=no")) {
        fprintf(stderr, "note: only writing decoded resources\n");
        save_raw = SaveRawBehavior::Never;

      } else if (!strcmp(argv[x], "--save-raw=if-decode-fails")) {
        fprintf(stderr, "note: writing raw resources if decode fails\n");
        save_raw = SaveRawBehavior::IfDecodeFails;

      } else if (!strcmp(argv[x], "--save-raw=yes")) {
        fprintf(stderr, "note: writing all raw resources\n");
        save_raw = SaveRawBehavior::Always;

      } else if (!strcmp(argv[x], "--data-fork")) {
        fprintf(stderr, "note: reading data forks as resource forks\n");
        use_data_fork = true;

      } else if (!strcmp(argv[x], "--skip-decompression")) {
        decompress_mode = DecompressionMode::DISABLED;

      } else if (!strcmp(argv[x], "--debug-decompression")) {
        decompress_mode = DecompressionMode::ENABLED_VERBOSE;

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

    string disassembly;
    if (disassemble_68k) {
      disassembly = M68KEmulator::disassemble(data.data(), data.size(), 0, nullptr);
    } else {
      disassembly = PPC32Emulator::disassemble(data.data(), data.size());
    }
    fwritex(stderr, disassembly);
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

    ResourceFile rf(ResourceFile::Resource(decode_type, 1, load_file(filename)));

    try {
      decode_fn(out_dir, filename, rf, decode_type, 1, "");
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

  disassemble_path(filename, out_dir, use_data_fork, target_types, target_ids,
      save_raw, decompress_mode);

  return 0;
}
