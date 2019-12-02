#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <phosg/Encoding.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <string>

#include "resource_fork.hh"

using namespace std;



enum ColorFormat {
  Grayscale1 = 0,
  Grayscale2,
  Grayscale4,
  Grayscale8,
  RGBX5551,
  XRGB1555,
  RGB565,
};

ColorFormat color_format_for_name(const char* name) {
  if (!strcmp(name, "1")) {
    return ColorFormat::Grayscale1;
  } else if (!strcmp(name, "grayscale1")) {
    return ColorFormat::Grayscale1;
  } else if (!strcmp(name, "2")) {
    return ColorFormat::Grayscale2;
  } else if (!strcmp(name, "grayscale2")) {
    return ColorFormat::Grayscale2;
  } else if (!strcmp(name, "4")) {
    return ColorFormat::Grayscale4;
  } else if (!strcmp(name, "grayscale4")) {
    return ColorFormat::Grayscale4;
  } else if (!strcmp(name, "8")) {
    return ColorFormat::Grayscale8;
  } else if (!strcmp(name, "grayscale8")) {
    return ColorFormat::Grayscale8;
  } else if (!strcmp(name, "rgbx5551")) {
    return ColorFormat::RGBX5551;
  } else if (!strcmp(name, "xrgb1555")) {
    return ColorFormat::XRGB1555;
  } else if (!strcmp(name, "rgb565")) {
    return ColorFormat::RGB565;
  } else {
    throw out_of_range("invalid color format");
  }
}

size_t bits_for_format(ColorFormat format) {
  switch (format) {
    case ColorFormat::Grayscale1:
      return 1;
    case ColorFormat::Grayscale2:
      return 2;
    case ColorFormat::Grayscale4:
      return 4;
    case ColorFormat::Grayscale8:
      return 8;
    case ColorFormat::RGBX5551:
      return 16;
    case ColorFormat::XRGB1555:
      return 16;
    case ColorFormat::RGB565:
      return 16;
    default:
      throw out_of_range("invalid color format");
  }
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
      8 (grayscale), xrgb1555, rgbx5551, and rgb565.\n\
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
  ColorFormat color_format = ColorFormat::Grayscale1;
  bool reverse_endian = false;
  const char* input_filename = NULL;
  const char* output_filename = NULL;
  for (size_t x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "--width=", 8)) {
      w = strtoull(&argv[x][8], NULL, 0);
    } else if (!strncmp(argv[x], "--height=", 9)) {
      h = strtoull(&argv[x][9], NULL, 0);
    } else if (!strncmp(argv[x], "--bits=", 7)) {
      color_format = color_format_for_name(&argv[x][7]);
    } else if (!strcmp(argv[x], "--reverse-endian")) {
      reverse_endian = true;
    } else if (!strncmp(argv[x], "--offset=", 9)) {
      offset = strtoull(&argv[x][9], NULL, 0);
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

  size_t pixel_count = (data.size() * 8) / bits_for_format(color_format);

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
    switch (color_format) {
      case ColorFormat::Grayscale1:
        if ((data[z >> 3] >> (7 - (z & 7))) & 0x01) {
          img.write_pixel(x, y, 0x00, 0x00, 0x00);
        } else {
          img.write_pixel(x, y, 0xFF, 0xFF, 0xFF);
        }
        break;

      case ColorFormat::Grayscale2: {
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

      case ColorFormat::Grayscale4: {
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

      case ColorFormat::Grayscale8:
        img.write_pixel(x, y, data[z], data[z], data[z]);
        break;

      case ColorFormat::RGBX5551: {
        uint16_t pixel = *reinterpret_cast<const uint16_t*>(&data[z * 2]);
        if (reverse_endian) {
          pixel = bswap16(pixel);
        }
        img.write_pixel(x, y, ((pixel >> 8) & 0xF8), ((pixel >> 3) & 0xF8),
            ((pixel << 2) & 0xF8));
        break;
      }

      case ColorFormat::XRGB1555: {
        uint16_t pixel = *reinterpret_cast<const uint16_t*>(&data[z * 2]);
        if (reverse_endian) {
          pixel = bswap16(pixel);
        }
        img.write_pixel(x, y, ((pixel >> 7) & 0xF8), ((pixel >> 2) & 0xF8),
            ((pixel << 3) & 0xF8));
        break;
      }

      case ColorFormat::RGB565: {
        uint16_t pixel = *reinterpret_cast<const uint16_t*>(&data[z * 2]);
        if (reverse_endian) {
          pixel = bswap16(pixel);
        }
        img.write_pixel(x, y, ((pixel >> 8) & 0xF8), ((pixel >> 3) & 0xFC),
            ((pixel << 3) & 0xF8));
        break;
      }

      default:
        fprintf(stderr, "invalid color format");
        return 1;
    }
  }

  if (output_filename) {
    img.save(output_filename, Image::ImageFormat::WindowsBitmap);
  } else if (input_filename) {
    string output_filename = string_printf("%s.bmp", input_filename);
    img.save(output_filename.c_str(), Image::ImageFormat::WindowsBitmap);
  } else {
    img.save(stdout, Image::ImageFormat::WindowsBitmap);
  }

  return 0;
}
