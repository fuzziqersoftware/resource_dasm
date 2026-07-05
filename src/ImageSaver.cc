#include "ImageSaver.hh"

#include <cstring>

namespace ResourceDASM {

bool ImageSaver::process_cli_arg(const char* arg) {
  if (!strcmp(arg, IMAGE_SAVER_OPTION "=bmp")) {
    this->image_format = phosg::ImageFormat::WINDOWS_BITMAP;
    return true;

  } else if (!strcmp(arg, IMAGE_SAVER_OPTION "=ppm")) {
    this->image_format = phosg::ImageFormat::COLOR_PPM;
    return true;

  } else if (!strcmp(arg, IMAGE_SAVER_OPTION "=png")) {
    this->image_format = phosg::ImageFormat::PNG;
    return true;
  }
  return false;
}

} // namespace ResourceDASM
