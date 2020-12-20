#pragma once

#include <stdint.h>

#include <functional>
#include <utility>

#include <phosg/Image.hh>

#include "quickdraw_formats.hh"

struct PictRenderResult {
  Image image;
  std::string embedded_image_format;
  std::string embedded_image_data;

  PictRenderResult();
};

PictRenderResult render_quickdraw_picture(const void* data, size_t size,
    std::function<std::vector<Color>(int16_t id)> get_clut = NULL);
