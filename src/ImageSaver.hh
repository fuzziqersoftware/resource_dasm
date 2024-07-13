#pragma once

#include <phosg/Image.hh>

#include <cstdio>

namespace ResourceDASM {

#define IMAGE_SAVER_OPTION "--image-format"

// clang-format off
#define IMAGE_SAVER_HELP \
"Image-specific options:\n\
  " IMAGE_SAVER_OPTION "=bmp\n\
      Save images as Windows bitmaps (default)\n\
  " IMAGE_SAVER_OPTION "=ppm\n\
      Save images as portable pixmaps\n\
  " IMAGE_SAVER_OPTION "=png\n\
      Save images as PNG files\n\
\n"
// clang-format on

class ImageSaver {
public:
  ImageSaver() : image_format(Image::Format::WINDOWS_BITMAP) {
  }

  // Returns whether arg was processed
  bool process_cli_arg(const char* arg);

  // Returns the filename *with* extension (e.g. for logging)
  [[nodiscard]] std::string save_image(const Image& img, const std::string& file_name_without_ext) const;

  void save_image(const Image& img, FILE* file) const;

private:
  Image::Format image_format;
};

} // namespace ResourceDASM
