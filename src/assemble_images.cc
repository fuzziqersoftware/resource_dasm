#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <string>

using namespace std;
using namespace phosg;

struct ImagePlacement {
  string filename;
  Image image;
  ssize_t x = 0;
  ssize_t y = 0;
  ssize_t w = -1;
  ssize_t h = -1;
  ssize_t sx = 0;
  ssize_t sy = 0;
};

int main(int argc, char* argv[]) {
  if (argc < 3) {
    fprintf(stderr, "\
Basic usage:\n\
  assemble_images filename1 filename2 ... --output=outfile.bmp\n\
In this form, assemble_images concatenates the images (BMP or PPM files)\n\
horizontally and writes the result to outfile.bmp. \"Line breaks\" can be\n\
inserted by specifying '.' as a filename - all following images are placed\n\
below the previous images, starting again at the left side.\n\
\n\
Advanced usage:\n\
  assemble_images --place filename1@x,y[,w,h[,sx,sy]] ... --output=outfile.bmp\n\
In this form, assemble_images copies the given images onto a transparent\n\
canvas in the order they are specified. Each image must be suffixed with the\n\
coordinates to copy it to on the canvas (x and y), and may be further\n\
(optionally) suffixed with the width and height of data to copy (w and h), and\n\
the coordinates from which to copy in the source image (sx and sy). If not\n\
specified, w and h default to -1 (which means to copy the entire source image),\n\
and sx and sy default to 0.\n\
\n\
In both forms, if --output is not given, the output is written to stdout.\n\
\n");
    return 1;
  }

  bool place = false;
  vector<ImagePlacement> placements;
  string output_filename;
  for (ssize_t z = 1; z < argc; z++) {
    if (!strcmp(argv[z], "--place")) {
      place = true;
    } else if (!strncmp(argv[z], "--output=", 9)) {
      output_filename = &argv[z][9];
    } else {
      auto& placement = placements.emplace_back();
      auto tokens = split(argv[z], '@');
      if (tokens.size() < 1 || tokens.size() > 2) {
        throw runtime_error("invalid placement: " + string(argv[z]));
      }

      placement.filename = tokens[0];

      if (tokens.size() > 1) {
        auto coord_tokens = split(tokens[1], ',');
        if ((coord_tokens.size() & 1) || (coord_tokens.size() > 6)) {
          throw runtime_error("invalid placement: " + string(argv[z]));
        }

        if (coord_tokens.size() >= 2) {
          placement.x = stoll(coord_tokens[0], nullptr, 0);
          placement.y = stoll(coord_tokens[1], nullptr, 0);
        }
        if (coord_tokens.size() >= 4) {
          placement.w = stoll(coord_tokens[2], nullptr, 0);
          placement.h = stoll(coord_tokens[3], nullptr, 0);
        }
        if (coord_tokens.size() >= 6) {
          placement.sx = stoll(coord_tokens[4], nullptr, 0);
          placement.sy = stoll(coord_tokens[5], nullptr, 0);
        }
      }
    }
  }

  if (placements.empty()) {
    throw runtime_error("no source images given");
  }

  if (!place) {
    ssize_t line_height = 0;
    ssize_t dest_x = 0;
    ssize_t dest_y = 0;
    for (auto& placement : placements) {
      if (placement.filename == ".") {
        dest_x = 0;
        dest_y += line_height;
        line_height = 0;
      } else {
        placement.image = Image(placement.filename);
        placement.x = dest_x;
        placement.y = dest_y;
        placement.w = placement.image.get_width();
        placement.h = placement.image.get_height();
        placement.sx = 0;
        placement.sy = 0;
        dest_x += placement.w;
        line_height = max<ssize_t>(line_height, placement.h);
      }
    }
  } else {
    for (auto& placement : placements) {
      placement.image = Image(placement.filename);
    }
  }

  // Compute canvas dimensions
  ssize_t canvas_xmin = placements[0].x;
  ssize_t canvas_ymin = placements[0].y;
  ssize_t canvas_xmax = placements[0].x;
  ssize_t canvas_ymax = placements[0].y;
  for (auto& placement : placements) {
    placement.w = (placement.w < 0) ? placement.image.get_width() : placement.w;
    placement.h = (placement.h < 0) ? placement.image.get_height() : placement.h;
    if ((placement.sx < 0) || (placement.sx + placement.w > static_cast<ssize_t>(placement.image.get_width())) ||
        (placement.sy < 0) || (placement.sy + placement.h > static_cast<ssize_t>(placement.image.get_height()))) {
      throw runtime_error("source area for " + placement.filename + " extends beyond image boundary");
    }
    canvas_xmin = min<ssize_t>(canvas_xmin, placement.x);
    canvas_xmax = max<ssize_t>(canvas_xmax, placement.x + placement.w);
    canvas_ymin = min<ssize_t>(canvas_ymin, placement.y);
    canvas_ymax = max<ssize_t>(canvas_ymax, placement.y + placement.h);
  }

  Image result(canvas_xmax - canvas_xmin, canvas_ymax - canvas_ymin, true);
  for (const auto& placement : placements) {
    if (!placement.image.empty()) {
      result.blit(
          placement.image,
          placement.x - canvas_xmin,
          placement.y - canvas_ymin,
          placement.w,
          placement.h,
          placement.sx,
          placement.sy);
    }
  }

  if (output_filename.empty()) {
    result.save(stdout, Image::Format::WINDOWS_BITMAP);
  } else {
    result.save(output_filename, Image::Format::WINDOWS_BITMAP);
  }

  return 0;
}
