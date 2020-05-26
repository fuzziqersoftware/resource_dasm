#pragma once

#include <stdint.h>

#include <utility>

#include <phosg/Image.hh>

struct pict_render_result {
  Image image;
  std::string embedded_image_format;
  std::string embedded_image_data;

  pict_render_result();
};

pict_render_result render_quickdraw_picture(const void* data, size_t size);
