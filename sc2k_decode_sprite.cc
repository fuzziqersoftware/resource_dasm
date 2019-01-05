#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

using namespace std;



struct SpriteEntry {
  uint16_t id;
  uint32_t offset;
  uint16_t height;
  uint16_t width;

  void byteswap() {
    this->id = bswap16(this->id);
    this->offset = bswap32(this->offset);
    this->height = bswap16(this->height);
    this->width = bswap16(this->width);
  }
} __attribute__((packed));

struct SpriteHeader {
  uint16_t count;
  SpriteEntry entries[0];

  void byteswap() {
    this->count = bswap16(this->count);
    for (size_t x = 0; x < this->count; x++) {
      this->entries[x].byteswap();
    }
  }
} __attribute__((packed));



struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;

  Color(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) { }
};

struct PaletteEntry {
  uint16_t r;
  uint16_t g;
  uint16_t b;
  uint16_t unknown[5];
};

vector<Color> load_pltt(const char* filename) {
  // pltt resources have a 16-byte header, which is coincidentally also the size
  // of each entry. I'm lazy so we'll just load it all at once and use the first
  // "entry" instead of manually making a header struct
  string data = load_file(filename);
  PaletteEntry* pltt = reinterpret_cast<PaletteEntry*>(const_cast<char*>(data.data()));

  // the first word is the entry count; the rest of the header seemingly doesn't
  // matter at all
  uint16_t count = bswap16(pltt->r);

  vector<Color> ret;
  for (size_t x = 1; x < count + 1; x++) {
    ret.emplace_back(pltt[x].r >> 8, pltt[x].g >> 8, pltt[x].b >> 8);
  }
  return ret;
}



Image decode_sprite(const void* vdata, uint16_t width, uint16_t height,
    const vector<Color>& pltt) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  // SC2K sprites are encoded as byte streams. opcodes are 2 bytes. some opcodes
  // are followed by multiple bytes (possibly an odd number), but opcodes are
  // always word-aligned. there are only 5 opcodes

  Image ret(width, height, true);
  ret.clear(0xFF, 0xFF, 0xFF, 0x00); // make it all transparent

  int16_t y = -1;
  int16_t x = 0;

  size_t offset = 0;
  for (;;) {
    uint16_t opcode = bswap16(*reinterpret_cast<const uint16_t*>(data));
    data += 2;
    offset += 2;

    switch (opcode & 0xFF) {
      case 0: // no-op
        break;
      case 1: // end of row
        y++;
        x = 0;
        break;
      case 2: // end of stream
        return ret;
      case 3: // skip pixels to the right
        x += (opcode >> 8);
        break;
      case 4: { // write pixels
        uint16_t end_x = x + (opcode >> 8);
        for (; x < end_x; x++) {
          uint8_t color = *(data++);
          offset++;
          const Color& c = pltt.at(color);
          ret.write_pixel(x, y, c.r, c.g, c.b, 0xFF);
        }
        // the opcodes are always word-aligned, so adjust ptr if needed
        if (opcode & 0x0100) {
          data++;
        }
        break;
      }
      default:
        throw runtime_error(string_printf("invalid opcode: %04hX", opcode));
    }
  }
}



int main(int argc, char* argv[]) {
  printf("fuzziqer software simcity 2000 sprite renderer\n\n");

  if (argc != 3) {
    fprintf(stderr, "usage: %s sprt_file pltt_file\n", argv[0]);
    return 2;
  }

  auto pltt = load_pltt(argv[2]);

  string sprite_table_data = load_file(argv[1]);
  SpriteHeader* header = reinterpret_cast<SpriteHeader*>(const_cast<char*>(sprite_table_data.data()));
  header->byteswap();

  for (size_t x = 0; x < header->count; x++) {
    const auto& entry = header->entries[x];

    string filename_prefix = string_printf("%s_%04hX", argv[1], entry.id);

    try {
      Image decoded = decode_sprite(sprite_table_data.data() + entry.offset,
          entry.width, entry.height, pltt);

      auto filename = filename_prefix + ".bmp";
      decoded.save(filename.c_str(), Image::ImageFormat::WindowsBitmap);
      printf("... %s.bmp\n", filename_prefix.c_str());

    } catch (const exception& e) {
      printf("... %s (FAILED: %s)\n", filename_prefix.c_str(), e.what());
    }
  }

  return 0;
}
