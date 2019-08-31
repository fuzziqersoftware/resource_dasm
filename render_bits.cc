#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <string>

#include "resource_fork.hh"

using namespace std;



int main(int argc, char* argv[]) {

  bool parse = false;
  size_t offset = 0;
  size_t w = 0, h = 0;
  size_t bit_depth = 1;
  for (size_t x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "--width=", 8)) {
      w = strtoull(&argv[x][8], NULL, 0);
    } else if (!strncmp(argv[x], "--height=", 9)) {
      h = strtoull(&argv[x][9], NULL, 0);
    } else if (!strncmp(argv[x], "--bits=", 7)) {
      bit_depth = strtoull(&argv[x][7], NULL, 0);
    } else if (!strncmp(argv[x], "--offset=", 9)) {
      offset = strtoull(&argv[x][9], NULL, 0);
    } else if (!strcmp(argv[x], "--parse")) {
      parse = true;
    }
  }

  string data = read_all(stdin);
  if (parse) {
    data = parse_data_string(data);
  }

  if (offset) {
    data = data.substr(offset);
  }

  size_t pixel_count = (data.size() * 8) / bit_depth;

  if (w == 0 && h == 0) {
    double z = sqrt(pixel_count);
    if (z != floor(z)) {
      w = z + 1;
    } else {
      w = z;
    }
    h = w;

  } else if (h == 0) {
    h = pixel_count / w;
    if (h * w < pixel_count) {
      h++;
    }

  } else if (w == 0) {
    w = pixel_count / h;
    if (h * w < pixel_count) {
      w++;
    }
  }

  Image img(w, h);
  for (size_t z = 0; z < pixel_count; z++) {
    size_t x = z % w;
    size_t y = z / w;
    if (y >= h) {
      break;
    }
    switch (bit_depth) {
      case 1:
        if ((data[z >> 3] >> (7 - (z & 7))) & 0x01) {
          img.write_pixel(x, y, 0x00, 0x00, 0x00);
        } else {
          img.write_pixel(x, y, 0xFF, 0xFF, 0xFF);
        }
        break;

      case 2: {
        uint8_t value = (data[z >> 2] >> (6 - ((z & 3) << 1))) & 3;
        if (value == 0) {
          img.write_pixel(x, y, 0x00, 0x00, 0x00);
        } else if (value == 1) {
          img.write_pixel(x, y, 0x55, 0x55, 0x55);
        } else if (value == 2) {
          img.write_pixel(x, y, 0xAA, 0xAA, 0xAA);
        } else if (value == 3) {
          img.write_pixel(x, y, 0xFF, 0xFF, 0xFF);
        } else {
          img.write_pixel(x, y, 0xFF, 0x00, 0x00);
        }
        break;
      }

      case 4: {
        uint8_t value = (data[z >> 1] >> (4 - ((z & 1) << 2))) & 7;
        if (value == 0) {
          img.write_pixel(x, y, 0x00, 0x00, 0x00);
        } else if (value == 1) {
          img.write_pixel(x, y, 0x24, 0x24, 0x24);
        } else if (value == 2) {
          img.write_pixel(x, y, 0x49, 0x49, 0x49);
        } else if (value == 3) {
          img.write_pixel(x, y, 0x6D, 0x6D, 0x6D);
        } else if (value == 4) {
          img.write_pixel(x, y, 0x92, 0x92, 0x92);
        } else if (value == 5) {
          img.write_pixel(x, y, 0xB6, 0xB6, 0xB6);
        } else if (value == 6) {
          img.write_pixel(x, y, 0xDA, 0xDA, 0xDA);
        } else if (value == 7) {
          img.write_pixel(x, y, 0xFF, 0xFF, 0xFF);
        } else {
          img.write_pixel(x, y, 0xFF, 0x00, 0x00);
        }
        break;
      }

      case 8:
        img.write_pixel(x, y, data[z], data[z], data[z]);
        break;

      default:
        fprintf(stderr, "invalid bit depth");
        return 1;
    }
  }
  img.save(stdout, Image::ImageFormat::WindowsBitmap);

  return 0;
}
