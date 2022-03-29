#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <phosg/Encoding.hh>
#include <phosg/Image.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>

#include "ResourceFile.hh"

using namespace std;



enum ColorFormat {
  GRAY1 = 0,
  GRAY2,
  GRAY4,
  GRAY8,
  RGBX5551,
  XRGB1555,
  RGB565,
  RGB888,
  XRGB8888,
  ARGB8888,
  RGBX8888,
  RGBA8888,
  INDEXED,
};

ColorFormat color_format_for_name(const char* name) {
  if (!strcmp(name, "1")) {
    return ColorFormat::GRAY1;
  } else if (!strcmp(name, "grayscale1")) {
    return ColorFormat::GRAY1;
  } else if (!strcmp(name, "2")) {
    return ColorFormat::GRAY2;
  } else if (!strcmp(name, "grayscale2")) {
    return ColorFormat::GRAY2;
  } else if (!strcmp(name, "4")) {
    return ColorFormat::GRAY4;
  } else if (!strcmp(name, "grayscale4")) {
    return ColorFormat::GRAY4;
  } else if (!strcmp(name, "8")) {
    return ColorFormat::GRAY8;
  } else if (!strcmp(name, "grayscale8")) {
    return ColorFormat::GRAY8;
  } else if (!strcmp(name, "rgbx5551")) {
    return ColorFormat::RGBX5551;
  } else if (!strcmp(name, "xrgb1555")) {
    return ColorFormat::XRGB1555;
  } else if (!strcmp(name, "rgb565")) {
    return ColorFormat::RGB565;
  } else if (!strcmp(name, "rgb888")) {
    return ColorFormat::RGB888;
  } else if (!strcmp(name, "xrgb8888")) {
    return ColorFormat::XRGB8888;
  } else if (!strcmp(name, "argb8888")) {
    return ColorFormat::ARGB8888;
  } else if (!strcmp(name, "rgbx8888")) {
    return ColorFormat::RGBX8888;
  } else if (!strcmp(name, "rgba8888")) {
    return ColorFormat::RGBA8888;
  } else {
    throw out_of_range("invalid color format");
  }
}

size_t bits_for_format(ColorFormat format) {
  switch (format) {
    case ColorFormat::GRAY1:
      return 1;
    case ColorFormat::GRAY2:
      return 2;
    case ColorFormat::GRAY4:
      return 4;
    case ColorFormat::GRAY8:
      return 8;
    case ColorFormat::RGBX5551:
    case ColorFormat::XRGB1555:
    case ColorFormat::RGB565:
      return 16;
    case ColorFormat::RGB888:
      return 24;
    case ColorFormat::XRGB8888:
    case ColorFormat::ARGB8888:
    case ColorFormat::RGBX8888:
    case ColorFormat::RGBA8888:
      return 32;
    case ColorFormat::INDEXED:
      throw logic_error("indexed color format does not have a fixed width");
    default:
      throw out_of_range("invalid color format");
  }
}

bool color_format_has_alpha(ColorFormat format) {
  return (format == ColorFormat::ARGB8888) || (format == ColorFormat::RGBA8888);
}



int main(int argc, char* argv[]) {
  if (argc == 1) {
    fprintf(stderr, "Usage: %s [options] [input_filename [output_filename]]\n\
\n\
If you actually want to run with all default options, give --bits=1.\n\
\n\
If no filenames are given, read from stdin and write to stdout. You should\n\
redirect stdout to a file because it will contain binary data which will\n\
probably goof up your terminal if it happens to contain escape codes.\n\
\n\
If an input filename is given but no output filename is given, render_bits will\n\
write to a file named <input_filename>.bmp.\n\
\n\
The output width and height will be automatically computed. You can override\n\
this by giving --width, --height, or both. Usually only --width is sufficient\n\
(and most useful by itself since the height will be computed automatically).\n\
\n\
Options:\n\
  --width=N: Output an image with this many pixels per row.\n\
  --height=N: Output an image with this many pixels per column.\n\
  --bits=FORMAT: Specify the input color format. Valid formats are 1, 2, 4, or\n\
      8 (grayscale), xrgb1555, rgbx5551, rgb565, rgb888, xrgb8888, argb8888,\n\
      rgbx8888, and rgba8888. Ignored if --clut-file is given.\n\
  --clut-file=FILENAME: Use this clut (.bin file exported by resource_dasm) to\n\
      map channel values to colors.\n\
  --reverse-endian: For color formats, byteswap the values before decoding.\n\
  --offset=N: Ignore this many bytes at the beginning of the input. You can use\n\
      this to skip data that looks like the file\'s header.\n\
  --parse: Expect input in text format, and parse it using phosg\'s standard\n\
      data format. Use this if you have e.g. a hex string and you want to paste\n\
      it into your terminal.\n\
", argv[0]);
    return 1;
  }

  bool parse = false;
  size_t offset = 0;
  size_t w = 0, h = 0;
  ColorFormat color_format = ColorFormat::GRAY1;
  bool reverse_endian = false;
  const char* input_filename = nullptr;
  const char* output_filename = nullptr;
  const char* clut_filename = nullptr;
  for (int x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "--width=", 8)) {
      w = strtoull(&argv[x][8], nullptr, 0);
    } else if (!strncmp(argv[x], "--height=", 9)) {
      h = strtoull(&argv[x][9], nullptr, 0);
    } else if (!strncmp(argv[x], "--bits=", 7)) {
      color_format = color_format_for_name(&argv[x][7]);
    } else if (!strncmp(argv[x], "--clut-file=", 12)) {
      clut_filename = &argv[x][12];
      color_format = ColorFormat::INDEXED;
    } else if (!strcmp(argv[x], "--reverse-endian")) {
      reverse_endian = true;
    } else if (!strncmp(argv[x], "--offset=", 9)) {
      offset = strtoull(&argv[x][9], nullptr, 0);
    } else if (!strcmp(argv[x], "--parse")) {
      parse = true;
    } else if (!input_filename) {
      input_filename = argv[x];
    } else if (!output_filename) {
      output_filename = argv[x];
    } else {
      throw invalid_argument(string_printf("invalid or excessive option: %s", argv[x]));
    }
  }

  string data;
  if (input_filename) {
    data = load_file(input_filename);
  } else {
    data = read_all(stdin);
  }

  if (parse) {
    data = parse_data_string(data);
  }

  if (offset) {
    data = data.substr(offset);
  }

  vector<ColorTableEntry> clut;
  size_t pixel_bits;
  if (clut_filename) {
    string data = load_file(clut_filename);
    clut = ResourceFile::decode_clut(data.data(), data.size());
    if (clut.empty()) {
      throw invalid_argument("clut is empty");
    }
    if (clut.size() & (clut.size() - 1)) {
      fprintf(stderr, "warning: clut size is not a power of 2; extending with black\n");
      while (clut.size() & (clut.size() - 1)) {
        auto entry = clut.emplace_back();
        entry.color_num = clut.size() - 1;
        entry.c = Color(0, 0, 0);
      }
    }
    for (pixel_bits = 0;
         clut.size() != static_cast<size_t>(1 << pixel_bits);
         pixel_bits++);
  } else {
    pixel_bits = bits_for_format(color_format);
  }
  size_t pixel_count = (data.size() * 8) / pixel_bits;

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

  BitReader r(data);
  Image img(w, h, color_format_has_alpha(color_format));
  for (size_t z = 0; z < pixel_count; z++) {
    size_t x = z % w;
    size_t y = z / w;
    if (y >= h) {
      break;
    }
    switch (color_format) {
      case ColorFormat::GRAY1:
        // Note: This is the opposite of what you'd expect for other formats
        // (1 is black, 0 is white). We do this because it seems to be the
        // common behavior on old Mac OS.
        img.write_pixel(x, y, r.read(1) ? 0x000000FF : 0xFFFFFFFF);
        break;

      case ColorFormat::GRAY2: {
        static const uint32_t colors[4] = {
            0x000000FF, 0x555555FF, 0xAAAAAAFF, 0xFFFFFFFF};
        img.write_pixel(x, y, colors[r.read(2)]);
        break;
      }

      case ColorFormat::GRAY4: {
        static const uint32_t colors[16] = {
          0x000000FF,
          0x242424FF,
          0x494949FF,
          0x6D6D6DFF,
          0x929292FF,
          0xB6B6B6FF,
          0xDADADAFF,
          0xFFFFFFFF,
        };
        img.write_pixel(x, y, colors[r.read(4)]);
        break;
      }

      case ColorFormat::GRAY8:
        img.write_pixel(x, y, data[z], data[z], data[z]);
        break;

      case ColorFormat::INDEXED: {
        Color8 c = clut.at(r.read(pixel_bits)).c.as8();
        img.write_pixel(x, y, c.r, c.g, c.b, 0xFF);
        break;
      }

      case ColorFormat::RGBX5551: {
        uint16_t pixel = r.read(16);
        if (reverse_endian) {
          pixel = bswap16(pixel);
        }
        img.write_pixel(x, y, ((pixel >> 8) & 0xF8), ((pixel >> 3) & 0xF8),
            ((pixel << 2) & 0xF8));
        break;
      }

      case ColorFormat::XRGB1555: {
        uint16_t pixel = r.read(16);
        if (reverse_endian) {
          pixel = bswap16(pixel);
        }
        img.write_pixel(x, y, ((pixel >> 7) & 0xF8), ((pixel >> 2) & 0xF8),
            ((pixel << 3) & 0xF8));
        break;
      }

      case ColorFormat::RGB565: {
        uint16_t pixel = r.read(16);
        if (reverse_endian) {
          pixel = bswap16(pixel);
        }
        img.write_pixel(x, y, ((pixel >> 8) & 0xF8), ((pixel >> 3) & 0xFC),
            ((pixel << 3) & 0xF8));
        break;
      }

      case ColorFormat::RGB888: {
        uint32_t pixel = r.read(24);
        if (reverse_endian) {
          pixel = bswap32(pixel) >> 8;
        }
        img.write_pixel(x, y, ((pixel >> 16) & 0xFF), ((pixel >> 8) & 0xFF),
            (pixel & 0xFF));
        break;
      }

      case ColorFormat::XRGB8888: {
        uint32_t pixel = r.read(32);
        if (reverse_endian) {
          pixel = bswap32(pixel);
        }
        img.write_pixel(x, y, ((pixel >> 16) & 0xFF), ((pixel >> 8) & 0xFF),
            (pixel & 0xFF));
        break;
      }

      case ColorFormat::ARGB8888: {
        uint32_t pixel = r.read(32);
        if (reverse_endian) {
          pixel = bswap32(pixel);
        }
        img.write_pixel(x, y, ((pixel >> 16) & 0xFF), ((pixel >> 8) & 0xFF),
            (pixel & 0xFF), ((pixel >> 24) & 0xFF));
        break;
      }

      case ColorFormat::RGBX8888: {
        uint32_t pixel = r.read(32);
        if (reverse_endian) {
          pixel = bswap32(pixel);
        }
        img.write_pixel(x, y, pixel | 0xFF);
        break;
      }

      case ColorFormat::RGBA8888: {
        uint32_t pixel = r.read(32);
        if (reverse_endian) {
          pixel = bswap32(pixel);
        }
        img.write_pixel(x, y, pixel);
        break;
      }

      default:
        fprintf(stderr, "invalid color format");
        return 1;
    }
  }

  if (output_filename) {
    img.save(output_filename, Image::Format::WINDOWS_BITMAP);
  } else if (input_filename) {
    string output_filename = string_printf("%s.bmp", input_filename);
    img.save(output_filename.c_str(), Image::Format::WINDOWS_BITMAP);
  } else {
    img.save(stdout, Image::Format::WINDOWS_BITMAP);
  }

  return 0;
}
