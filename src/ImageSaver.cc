#include "ImageSaver.hh"

#include <cstring>

using namespace std;

namespace ResourceDASM {

bool ImageSaver::process_cli_arg(const char* arg) {
  if (!strcmp(arg, IMAGE_SAVER_OPTION "=bmp")) {
    this->image_format = Image::Format::WINDOWS_BITMAP;
    return true;

  } else if (!strcmp(arg, IMAGE_SAVER_OPTION "=ppm")) {
    this->image_format = Image::Format::COLOR_PPM;
    return true;

  } else if (!strcmp(arg, IMAGE_SAVER_OPTION "=png")) {
    this->image_format = Image::Format::PNG;
    return true;
  }
  return false;
}

string ImageSaver::save_image(const Image& img, const string& file_name_without_ext) const {
  string file_name = file_name_without_ext + "." + Image::file_extension_for_format(this->image_format);

  img.save(file_name, this->image_format);

  return file_name;
}

void ImageSaver::save_image(const Image& img, FILE* file) const {
  img.save(file, this->image_format);
}

} // namespace ResourceDASM
