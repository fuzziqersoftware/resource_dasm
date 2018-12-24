#include <stdint.h>
#include <stdlib.h>

#include <phosg/Image.hh>
#include <utility>

std::pair<Image, Image> decode_dc2_sprite(const void* input_data, size_t size);
