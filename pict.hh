#pragma once

#include <stdint.h>

#include <utility>

#include <phosg/Image.hh>

std::pair<Image, bool> render_quickdraw_picture(const void* data, size_t size);
