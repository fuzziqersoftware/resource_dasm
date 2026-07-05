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
  ImageSaver() : image_format(phosg::ImageFormat::WINDOWS_BITMAP) {}

  // Returns whether arg was processed
  bool process_cli_arg(const char* arg);

  // Returns the filename *with* extension (e.g. for logging)
  template <phosg::PixelFormat Format>
  [[nodiscard]] std::string save_image(const phosg::Image<Format>& img, const std::string& file_name_without_ext) const {
    std::string file_name = file_name_without_ext + "." + file_extension_for_image_format(this->image_format);
    phosg::save_file(file_name, img.serialize(this->image_format));
    return file_name;
  }

  template <phosg::PixelFormat Format>
  void save_image(const phosg::Image<Format>& img, FILE* file) const {
    phosg::fwritex(file, img.serialize(this->image_format));
  }

private:
  phosg::ImageFormat image_format;
};

} // namespace ResourceDASM
