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
  for (size_t x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "--width=", 8)) {
      w = strtoull(&argv[x][8], NULL, 0);
    } else if (!strncmp(argv[x], "--height=", 9)) {
      h = strtoull(&argv[x][9], NULL, 0);
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

  size_t pixel_count = data.size() * 8;

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
  for (size_t z = 0; z < data.size() * 8; z++) {
    size_t x = z % w;
    size_t y = z / w;
    if (y >= h) {
      break;
    }
    if ((data[z >> 3] >> (7 - (z & 7))) & 0x01) {
      img.write_pixel(x, y, 0x00, 0x00, 0x00);
    } else {
      img.write_pixel(x, y, 0xFF, 0xFF, 0xFF);
    }
  }
  img.save(stdout, Image::ImageFormat::WindowsBitmap);

  return 0;
}
