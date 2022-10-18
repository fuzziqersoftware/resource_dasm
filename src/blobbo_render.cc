#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <phosg/Image.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

#include "TextCodecs.hh"

using namespace std;



Image decode_Blev(const string& data, const Image& tile_sheet) {
  StringReader r(data);
  string header_data = r.read(0x0E); // Format unknown
  uint16_t key = r.get_u16b();

  string decoded;
  while (!r.eof()) {
    uint8_t ch = r.get_u8();
    decoded.push_back(key ^ ch);
    key = ch;
  }

  decoded = unpack_bits(decoded);
  if (decoded.size() != 0x280) {
    throw runtime_error("incorrect decompressed level size");
  }

  Image ret(512, 320, false);
  for (size_t y = 0; y < 0x14; y++) {
    for (size_t x = 0; x < 0x20; x++) {
      // Levels are stored in column-major order, hence the weird index here
      uint8_t tile_id = decoded[x * 0x14 + y];
      // Convert non-editor tiles into annotated tiles (e.g. show boat direction
      // on water tiles)
      if (tile_id >= 0x51 && tile_id <= 0x55) { // Directional water tiles
        tile_id += 0x50;
      }
      if (tile_id == 0x30) { // Passable wall tile
        tile_id = 0xA0;
      }
      if (tile_id == 0xC5) { // Movable wall tile
        tile_id = 0xA6;
      }
      // Tiles are 16x16, and arranged in column-major order on the tilesheet
      size_t tile_sheet_x = tile_id & 0xF0;
      size_t tile_sheet_y = (tile_id << 4) & 0xF0;
      ret.blit(tile_sheet, x << 4, y << 4, 16, 16, tile_sheet_x, tile_sheet_y);
    }
  }

  return ret;
}



int main(int argc, char** argv) {
  if (argc < 1 || argc > 4) {
    fprintf(stderr, "\
Usage: blobbo_render <Blev-file.bin> <PMP8-128.bmp> [output-filename]\n\
\n\
You can get Blev files by using resource_dasm on the Blobbo game itself.\n\
To generate PMP8-128.bmp, use render_sprite to decode the PMP8 resource with ID\n\
128, which also comes from Blobbo.\n\
\n\
If no output filename is given, the output is written to <Blev-file>.bmp.\n\
\n");
    return 2;
  }

  const char* input_filename = (argc > 1) ? argv[1] : nullptr;
  const char* tile_sheet_filename = (argc > 2) ? argv[2] : nullptr;
  string output_filename = (argc > 3) ? argv[3] : "";

  if (!input_filename) {
    throw runtime_error("input filename must be given");
  }
  if (!tile_sheet_filename) {
    throw runtime_error("tile sheet filename must be given");
  }
  if (output_filename.empty()) {
    output_filename = string_printf("%s.bmp", input_filename);
  }

  string input_data = load_file(input_filename);

  Image tile_sheet(tile_sheet_filename);
  if (tile_sheet.get_width() < 16 * 16) {
    throw runtime_error("tile sheet is too narrow");
  }
  if (tile_sheet.get_height() < 16 * 16) {
    throw runtime_error("tile sheet is too short");
  }

  Image map = decode_Blev(input_data, tile_sheet);
  map.save(output_filename, Image::Format::WINDOWS_BITMAP);

  fprintf(stderr, "... %s\n", output_filename.c_str());
  return 0;
}
