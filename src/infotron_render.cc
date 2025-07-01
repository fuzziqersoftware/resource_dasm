#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <phosg/Encoding.hh>
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

static pair<uint8_t, uint8_t> read_coords(StringReader& r) {
  uint8_t x = r.get_u8();
  return make_pair(x, r.get_u8());
}

struct InfotronLevel {
  string name;

  uint16_t w;
  uint16_t h;
  uint8_t player_x;
  uint8_t player_y;
  uint16_t infotron_count;

  vector<pair<uint8_t, uint8_t>> scissor_coords;
  vector<pair<uint8_t, uint8_t>> quark_coords;
  vector<pair<uint8_t, uint8_t>> bug_coords;

  vector<uint16_t> field;

  InfotronLevel(const string& level_data) {
    StringReader r(level_data.data(), level_data.size());

    uint8_t name_length = r.get_u8();
    this->name = r.read(name_length);
    r.go(r.where() + 0xFF - name_length);

    this->w = r.get_u16b();
    this->h = r.get_u16b();
    this->player_x = r.get_u8();
    this->player_y = r.get_u8();

    // Terminal coordinates (we don't care; should also be in the tilemap)
    r.go(r.where() + 2);

    uint16_t scissor_count = r.get_u16b();
    uint16_t quark_count = r.get_u16b();
    this->infotron_count = r.get_u16b();
    uint16_t bug_count = r.get_u16b();

    // unknown2
    r.skip(4);

    for (; scissor_count; scissor_count--) {
      this->scissor_coords.emplace_back(read_coords(r));
    }
    for (; quark_count; quark_count--) {
      this->quark_coords.emplace_back(read_coords(r));
    }
    for (; bug_count; bug_count--) {
      this->bug_coords.emplace_back(read_coords(r));
    }

    if (r.get_s16b() != -1) {
      throw invalid_argument("end of coordinate list was not -1");
    }

    // Read drawing commands
    this->field.resize(this->w * this->h);
    size_t offset = 0;

    int16_t last_command = 0;
    int16_t command = r.get_s16b();
    while (command) {
      if (offset >= this->field.size()) {
        throw invalid_argument("reached the end of the field with more commands to execute");
      }

      if (command > 0) {
        this->field[offset++] = command;
      } else if (command < 0) {
        for (size_t end_offset = offset + (-command) - 1; offset < end_offset; offset++) {
          this->field[offset] = last_command;
        }
      }
      last_command = command;
      command = r.get_s16b();
    }

    // Auto-truncate the level to the appropriate width and height
    uint16_t new_h = (offset / this->w) + 1;
    uint16_t new_w = 0;
    for (uint16_t y = 0; y < new_h; y++) {
      uint16_t x;
      for (x = this->w - 1; x > 0; x--) {
        int16_t tile_id = this->field[y * this->w + x];
        if (tile_id && (tile_id != 0x80)) {
          break;
        }
      }
      if (x > new_w) {
        new_w = x + 1;
      }
    }

    if (new_w < this->w) {
      for (size_t y = 1; y < new_h; y++) {
        memcpy(&this->field[y * new_w], &this->field[y * this->w], new_w * 2);
      }
    }
    this->field.resize(new_w * new_h);
    this->w = new_w;
    this->h = new_h;
  }
};

static void print_usage() {
  fwrite_fmt(stderr, "\
Usage: infotron_render [options]\n\
\n" IMAGE_SAVER_HELP);
}

int main(int argc, char** argv) {
  ImageSaver image_saver;
  for (int x = 1; x < argc; x++) {
    if (!image_saver.process_cli_arg(argv[x])) {
      fwrite_fmt(stderr, "excess argument: {}\n", argv[x]);
      print_usage();
      return 2;
    }
  }

  const string levels_filename = "Infotron Levels/..namedfork/rsrc";
  const string pieces_filename = "Infotron Pieces/..namedfork/rsrc";

  ResourceFile levels(parse_resource_fork(load_file(levels_filename)));
  ResourceFile pieces(parse_resource_fork(load_file(pieces_filename)));
  auto level_resources = levels.all_resources();
  auto tile_resources = pieces.all_resources();

  const uint32_t level_resource_type = 0x6C9F566C;

  unordered_map<int16_t, ImageRGBA8888> tile_cache;
  for (const auto& it : level_resources) {
    if (it.first != level_resource_type) {
      continue;
    }
    int16_t level_id = it.second;
    string level_data = levels.get_resource(level_resource_type, level_id)->data;

    InfotronLevel level(level_data);

    ImageRGBA8888 result(level.w * 32, level.h * 32);
    for (size_t y = 0; y < level.h; y++) {
      for (size_t x = 0; x < level.w; x++) {

        uint16_t tile_id = level.field[y * level.w + x];
        if (tile_id == 0) {
          continue;
        }

        ImageRGBA8888* tile_src = nullptr;
        try {
          tile_src = &tile_cache.at(tile_id);
        } catch (const out_of_range&) {
          tile_src = &tile_cache.emplace(tile_id, pieces.decode_icl8(tile_id)).first->second;
        }

        if (!tile_src) {
          throw invalid_argument(std::format("tile {} (0x{:X}) does not exist", tile_id, tile_id));
        }

        result.copy_from(*tile_src, x * 32, y * 32, 32, 32, 0, 0);
      }
    }

    result.draw_text(0, 0, 0xFFFFFFFF, 0x00000080,
        "Level {} ({}): {}x{}, {} infotron{} needed",
        level_id, level.name, level.w, level.h, level.infotron_count,
        (level.infotron_count == 1 ? "" : "s"));

    string sanitized_name;
    for (char ch : level.name) {
      if (ch > 0x20 && ch <= 0x7E) {
        sanitized_name.push_back(ch);
      } else {
        sanitized_name.push_back('_');
      }
    }

    string result_filename = std::format("Infotron_Level_{}_{}", level_id, sanitized_name);
    result_filename = image_saver.save_image(result, result_filename);
    fwrite_fmt(stderr, "... {}\n", result_filename);
  }

  return 0;
}
