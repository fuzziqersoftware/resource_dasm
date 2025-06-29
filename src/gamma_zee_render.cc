#include <inttypes.h>
#include <stdlib.h>

#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <vector>

#include "ImageSaver.hh"
#include "IndexFormats/Formats.hh"
#include "ResourceFile.hh"

using namespace std;
using namespace phosg;
using namespace ResourceDASM;

static void print_usage() {
  fwrite_fmt(stderr, "\
Usage: gamma_zee_render [options] <game-application> <levels-file>\n\
\n\
Options:\n\
  --help, -h\n\
      Show this help\n\
\n" IMAGE_SAVER_HELP);
}

int main(int argc, char** argv) {
  ImageSaver image_saver;
  string game_filename;
  string levels_filename;
  for (int z = 1; z < argc; z++) {
    if (!strcmp(argv[z], "--help") || !strcmp(argv[z], "-h")) {
      print_usage();
      return 0;
    } else if (image_saver.process_cli_arg(argv[z])) {
      // Nothing
    } else if (game_filename.empty()) {
      game_filename = argv[z];
    } else if (levels_filename.empty()) {
      levels_filename = argv[z];
    } else {
      fwrite_fmt(stderr, "excess argument: {}\n", argv[z]);
      print_usage();
      return 2;
    }
  }

  if (game_filename.empty() || levels_filename.empty()) {
    print_usage();
    return 2;
  }

  ResourceFile game_rf(parse_resource_fork(load_file(game_filename + "/..namedfork/rsrc")));
  ResourceFile levels_rf(parse_resource_fork(load_file(levels_filename + "/..namedfork/rsrc")));

  auto info_f = fopen_unique(levels_filename + "_info.txt", "wt");

  unordered_map<int16_t, ResourceFile::DecodedColorIconResource> cicn_cache;
  for (int16_t level_id : levels_rf.all_resources_of_type(0x67616D65)) { // 'game'
    try {
      const auto& info_res = levels_rf.decode_STR(level_id, 0x4C496E66); // 'LInf'
      fwrite_fmt(info_f.get(), "(Level {})\n{}\n", level_id, info_res.str);
      if (!info_res.after_data.empty()) {
        fputs("\nExtra data:\n", info_f.get());
        print_data(info_f.get(), info_res.after_data);
        fputc('\n', info_f.get());
      }

    } catch (const out_of_range&) {
      fwrite_fmt(info_f.get(), "(Level {}) Level information missing\n\n", level_id);
    }

    uint16_t start_x = 0, start_y = 0;
    try {
      auto pts_res = levels_rf.get_resource(0xA9707473, level_id);
      StringReader r(pts_res->data.data(), pts_res->data.size());
      start_y = r.get_u16b() - 1;
      start_x = r.get_u16b() - 1;
    } catch (const out_of_range&) {
    }

    try {
      auto game_res = levels_rf.get_resource(0x67616D65, level_id); // 'game'
      if (game_res->data.size() != 10000) {
        throw runtime_error("game size is not 10000 bytes");
      }

      size_t result_w = 0, result_h = 0;
      for (size_t z = 0; z < 10000; z++) {
        if (game_res->data.at(z) == 1) {
          continue;
        }
        size_t x = z / 100;
        size_t y = z % 100;
        if (x >= result_w) {
          result_w = x + 1;
        }
        if (y >= result_h) {
          result_h = y + 1;
        }
      }

      ImageRGB888 result(result_w * 32, result_h * 32);

      for (size_t y = 0; y < result_h; y++) {
        for (size_t x = 0; x < result_w; x++) {
          uint16_t tile_id = game_res->data.at(x * 100 + y);
          int16_t cicn_id = tile_id + 128;

          ResourceFile::DecodedColorIconResource* cicn = nullptr;
          try {
            cicn = &cicn_cache.at(cicn_id);
          } catch (const out_of_range&) {
            try {
              cicn = &cicn_cache.emplace(cicn_id, game_rf.decode_cicn(cicn_id)).first->second;
            } catch (const exception& e) {
              fwrite_fmt(stderr, "warning: cannot decode cicn {}\n", cicn_id);
            }
          }

          if (cicn) {
            if ((cicn->image.get_width() != 32) || (cicn->image.get_height() != 32)) {
              throw runtime_error("cicn dimensions are not 32x32");
            }
            result.copy_from(cicn->image, x * 32, y * 32, 32, 32, 0, 0);
          } else {
            result.write_rect(x * 32, y * 32, 32, 32, 0xFF0000FF);
            result.draw_text(x * 32 + 1, y * 32 + 1, 0x000000FF, "{:02X}", tile_id);
          }

          if (x == start_x && y == start_y) {
            result.draw_text(x * 32 + 1, y * 32 + 1, 0x008000FF, "START");
          }
        }
      }

      string map_filename = std::format("{}_Level_{}", levels_filename, level_id);
      map_filename = image_saver.save_image(result, map_filename);
      fwrite_fmt(stderr, "... {}\n", map_filename);

    } catch (const exception& e) {
      fwrite_fmt(info_f.get(), "Map render failed: {}\n", e.what());
    }

    fputc('\n', info_f.get());
  }

  return 0;
}
