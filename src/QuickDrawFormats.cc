#include "QuickDrawFormats.hh"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <exception>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <phosg/Time.hh>
#include <stdexcept>
#include <string>
#include <vector>

namespace ResourceDASM {

Region::Region(phosg::StringReader& r) {
  size_t start_offset = r.where();

  uint16_t size = r.get_u16b();
  if (size < 0x0A) {
    throw std::runtime_error("region cannot be smaller than 10 bytes");
  }
  if (size & 1) {
    throw std::runtime_error("region size is not even");
  }

  this->rect = r.get<Rect>();
  std::string rect_str = this->rect.str();

  while (r.where() < start_offset + size) {
    int16_t y = r.get_u16b();
    if (y == 0x7FFF) {
      break;
    }
    auto& row_pts = this->inversions.emplace(y, std::set<int16_t>{}).first->second;
    while (r.where() < start_offset + size) {
      int16_t x = r.get_u16b();
      if (x == 0x7FFF) {
        break;
      }
      // Remove duplicate inversion points like Quickdraw does: invert, then invert back => no change (see Quickdraw's
      // CloseRgn function in Regions.a and CullPoints in SortPoints.a)
      auto emplace_ret = row_pts.emplace(x);
      if (!emplace_ret.second) {
        row_pts.erase(emplace_ret.first);
      }
    }
  }

  if (r.where() != start_offset + size) {
    throw std::runtime_error("region ends before all data is parsed");
  }
}

Region::Region(const Rect& r) : rect(r) {}

std::string Region::serialize() const {
  phosg::StringWriter w;
  w.put_u16(0); // This will be overwritten at the end
  w.put(this->rect);

  for (const auto& row_it : this->inversions) {
    w.put_u16b(row_it.first); // y
    for (int16_t x : row_it.second) {
      w.put_u16b(x);
    }
    w.put_u16b(0x7FFF);
  }
  w.put_u16b(0x7FFF);

  // Write the size field
  w.pput_u16b(0, w.str().size());

  return w.str();
}

bool Region::is_inversion_point(int16_t x, int16_t y) const {
  try {
    return this->inversions.at(y).count(x);
  } catch (const std::out_of_range&) {
    return false;
  }
}

phosg::ImageG1 Region::render() const {
  size_t width = this->rect.width();
  size_t height = this->rect.height();
  phosg::ImageG1 ret(width, height);

  auto it = this->iterate();
  for (size_t y = 0; y < height; y++) {
    for (size_t x = 0; x < width; x++) {
      ret.write(x, y, it.check() ? 0x000000FF : 0xFFFFFFFF);
      it.right();
    }
    it.next_line();
  }

  return ret;
}

Region::Iterator Region::iterate() const {
  return Iterator(this);
}

Region::Iterator Region::iterate(const Rect& rect) const {
  return Iterator(this, rect);
}

Region::Iterator::Iterator(const Region* region) : Iterator(region, region->rect) {}

Region::Iterator::Iterator(const Region* region, const Rect& target_rect)
    : region(region),
      target_rect(target_rect),
      // Note: We don't have to initialize x since we call next_line() at the end of the constructor
      y(std::min<ssize_t>(this->region->rect.y1, this->target_rect.y1) - 1),
      region_is_rect(this->region->inversions.empty()),
      current_loc_in_region(false),
      inversions_row_it(this->region->inversions.begin()),
      current_row_it(this->current_row_inversions.begin()) {
  while (this->y < this->target_rect.y1) {
    this->advance_y();
  }
  this->reset_x();
}

void Region::Iterator::right() {
  this->x++;

  if (this->x == this->region->rect.x2) {
    // If we've moved off the right edge of the rect, we've left the region
    this->current_loc_in_region = false;

  } else if (this->region_is_rect && (this->x == this->region->rect.x1) && (this->y >= this->region->rect.y1) &&
      (this->y < this->region->rect.y2)) {
    // If we've moved onto the left edge and the region has no inversion points, then we're now in the region
    this->current_loc_in_region = true;

  } else if ((this->current_row_it != this->current_row_inversions.end()) && (*this->current_row_it == this->x)) {
    // If we've hit an inversion point, we have entered or left the region
    this->current_loc_in_region = !this->current_loc_in_region;
    this->current_row_it++;
  }
}

void Region::Iterator::advance_y() {
  this->y++;

  // The inversion points on this row are the same as the previous row's points xor'd with the new row's points, if any
  if ((this->inversions_row_it != this->region->inversions.end()) && (this->inversions_row_it->first == this->y)) {
    for (int16_t inv_x : this->inversions_row_it->second) {
      auto emplace_ret = this->current_row_inversions.emplace(inv_x);
      if (!emplace_ret.second) {
        this->current_row_inversions.erase(emplace_ret.first);
      }
    }
    this->inversions_row_it++;
  }
}

void Region::Iterator::reset_x() {
  this->x = std::min<ssize_t>(this->region->rect.x1, this->target_rect.x1) - 1;
  this->current_loc_in_region = false;
  this->current_row_it = this->current_row_inversions.begin();
  while (this->x < this->target_rect.x1) {
    this->right();
  }
}

void Region::Iterator::next_line() {
  this->advance_y();
  this->reset_x();
}

bool Region::Iterator::check() const {
  return this->current_loc_in_region;
}

Fixed::Fixed() : value(0) {}

Fixed::Fixed(int16_t whole, uint16_t decimal) : value((whole << 16) | decimal) {}

double Fixed::as_double() const {
  return static_cast<double>(this->value) / 0x10000;
}

Pattern::Pattern(uint64_t pattern) : pattern(pattern) {}

bool Pattern::pixel_at(uint8_t x, uint8_t y) const {
  return (this->rows[y & 7] >> (7 - (x & 7))) & 1;
}

phosg::ImageG1 decode_monochrome_image(const void* vdata, size_t size, size_t w, size_t h, size_t row_bytes) {
  if (row_bytes == 0) {
    if (w & 7) {
      throw std::runtime_error("width must be a multiple of 8 unless row_bytes is specified");
    }
    row_bytes = w / 8;
  }
  if (size != row_bytes * h) {
    throw std::runtime_error(std::format("incorrect data size: expected {} bytes, got {} bytes", row_bytes * h, size));
  }
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  phosg::ImageG1 result(w, h);
  for (size_t y = 0; y < h; y++) {
    result.write_row(y, &data[y * row_bytes], w);
  }
  return result;
}

phosg::ImageGA11 decode_monochrome_image_masked(const void* vdata, size_t size, size_t w, size_t h) {
  const uint8_t* image_data = reinterpret_cast<const uint8_t*>(vdata);
  const uint8_t* mask_data = image_data + (w * h / 8);

  if (w & 7) {
    throw std::runtime_error("width is not a multiple of 8");
  }
  if (size != w * h / 4) {
    throw std::runtime_error(std::format("incorrect data size: expected {} bytes, got {} bytes", w * h / 4, size));
  }

  phosg::ImageGA11 result(w, h, true);
  for (size_t y = 0; y < h; y++) {
    for (size_t x = 0; x < w; x += 8) {
      uint8_t pixels = image_data[y * w / 8 + x / 8];
      uint8_t mask_pixels = mask_data[y * w / 8 + x / 8];
      for (size_t z = 0; z < 8; z++) {
        uint8_t value = (pixels & 0x80) ? 0x00 : 0xFF;
        uint8_t mask_value = (mask_pixels & 0x80) ? 0xFF : 0x00;
        pixels <<= 1;
        mask_pixels <<= 1;
        result.write(x + z, y, phosg::rgba8888_gray(value, mask_value));
      }
    }
  }
  return result;
}

phosg::ImageRGB888 decode_4bit_image(
    const void* vdata, size_t size, size_t w, size_t h, const std::vector<Color8>* clut) {
  if (w & 1) {
    throw std::runtime_error("width is not even");
  }
  if (size != w * h / 2) {
    throw std::runtime_error(std::format(
        "incorrect data size: expected {} bytes, got {} bytes", w * h / 2, size));
  }
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  phosg::ImageRGB888 result(w, h);
  for (size_t y = 0; y < h; y++) {
    for (size_t x = 0; x < w; x += 2) {
      uint8_t indexes = data[y * w / 2 + x / 2];
      if (clut) {
        const Color8& left_c = clut->at((indexes >> 4) & 0x0F);
        const Color8& right_c = clut->at(indexes & 0x0F);
        result.write(x, y, left_c.rgba8888());
        result.write(x + 1, y, right_c.rgba8888());
      } else {
        uint8_t left_v = (indexes & 0xF0) | ((indexes & 0xF0) >> 4);
        uint8_t right_v = ((indexes & 0x0F) << 4) | (indexes & 0x0F);
        result.write(x, y, phosg::rgba8888_gray(left_v));
        result.write(x + 1, y, phosg::rgba8888_gray(right_v));
      }
    }
  }

  return result;
}

phosg::ImageRGB888 decode_8bit_image(
    const void* vdata, size_t size, size_t w, size_t h, const std::vector<Color8>* clut) {
  if (size != w * h) {
    throw std::runtime_error(std::format("incorrect data size: expected {} bytes, got {} bytes", w * h, size));
  }
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  phosg::ImageRGB888 result(w, h);
  for (size_t y = 0; y < h; y++) {
    for (size_t x = 0; x < w; x++) {
      if (clut) {
        result.write(x, y, clut->at(data[y * w + x]).rgba8888());
      } else {
        result.write(x, y, phosg::rgba8888_gray(data[y * w + x]));
      }
    }
  }

  return result;
}

uint32_t PixelMapData::lookup_entry(uint16_t pixel_size, size_t row_bytes, size_t x, size_t y) const {
  switch (pixel_size) {
    case 1:
      return (this->data[(y * row_bytes) + (x / 8)] >> (7 - (x & 7))) & 1;
    case 2:
      return (this->data[(y * row_bytes) + (x / 4)] >> (6 - ((x & 3) * 2))) & 3;
    case 4:
      return (this->data[(y * row_bytes) + (x / 2)] >> (4 - ((x & 1) * 4))) & 15;
    case 8:
      return this->data[(y * row_bytes) + x];
    case 16:
      return *reinterpret_cast<const phosg::be_uint16_t*>(&this->data[(y * row_bytes) + (x * 2)]);
    case 32:
      return *reinterpret_cast<const phosg::be_uint32_t*>(&this->data[(y * row_bytes) + (x * 4)]);
    default:
      throw std::runtime_error("pixel size is not 1, 2, 4, 8, 16, or 32 bits");
  }
}

size_t PixelMapData::size(uint16_t row_bytes, size_t h) {
  return row_bytes * h;
}

std::shared_ptr<ColorTable> ColorTable::from_entries(
    const std::vector<ColorTableEntry>& entries) {
  if (entries.empty()) {
    throw std::logic_error("cannot construct an empty color table");
  }

  size_t size = sizeof(ColorTable) + entries.size() * sizeof(ColorTableEntry);
  std::shared_ptr<ColorTable> ret(reinterpret_cast<ColorTable*>(malloc(size)), free);
  ret->seed = 0;
  ret->flags = 0;
  ret->num_entries = entries.size() - 1;
  for (size_t x = 0; x < entries.size(); x++) {
    ret->entries[x] = entries[x];
  }
  return ret;
}

size_t ColorTable::size() const {
  return sizeof(ColorTable) + (this->num_entries + 1) * sizeof(ColorTableEntry);
}

uint32_t ColorTable::get_num_entries() const {
  return this->num_entries + 1;
}

const ColorTableEntry* ColorTable::get_entry(int16_t id) const {
  // It looks like if the highest flag is set (8000) then id is just the index, not the color number, and we should
  // ignore the color_num field
  if (this->flags & 0x8000) {
    if (id <= this->num_entries) {
      return &this->entries[id];
    }
  } else {
    for (int32_t x = 0; x <= this->num_entries; x++) {
      if (this->entries[x].color_num == id) {
        return &this->entries[x];
      }
    }
  }
  return nullptr;
}

template <phosg::PixelFormat Format>
phosg::Image<Format> decode_color_image_t(
    const PixelMapHeader& header,
    const PixelMapData& pixel_map,
    const ColorTable* ctable,
    const PixelMapData* mask_map,
    size_t mask_row_bytes) {

  // According to Apple's docs, pixel_type is 0 for indexed color and 0x0010 for direct color, even for 32-bit images
  if (header.pixel_type != 0 && header.pixel_type != 0x0010) {
    throw std::runtime_error("unknown pixel type");
  }
  if (header.pixel_type == 0 && !ctable) {
    throw std::runtime_error("color table must be given for indexed-color image");
  }

  // We only support 3-component direct color images (RGB)
  if (header.pixel_type == 0x0010 && header.component_count != 3) {
    throw std::runtime_error("unsupported channel count");
  }
  if (header.pixel_type == 0x0010 && header.pixel_size == 0x0010 && header.component_size != 5) {
    throw std::runtime_error("unsupported 16-bit channel width");
  }
  if (header.pixel_type == 0x0010 && header.pixel_size == 0x0020 && header.component_size != 8) {
    throw std::runtime_error("unsupported 32-bit channel width");
  }

  size_t width = header.bounds.width();
  size_t height = header.bounds.height();
  phosg::Image<Format> img(width, height);
  for (size_t y = 0; y < height; y++) {
    for (size_t x = 0; x < width; x++) {
      uint32_t color_id = pixel_map.lookup_entry(header.pixel_size, header.flags_row_bytes & 0x3FFF, x, y);

      if (header.pixel_type == 0) {
        const auto* e = ctable->get_entry(color_id);
        if (e) {
          uint8_t alpha = 0xFF;
          if (mask_map) {
            alpha = mask_map->lookup_entry(1, mask_row_bytes, x, y) ? 0xFF : 0x00;
          }
          img.write(x, y, e->c.rgba8888(alpha));

        } else if (color_id == static_cast<uint32_t>((1 << header.pixel_size) - 1)) {
          // Some rare pixmaps appear to use 0xFF as black, so we handle that manually here. TODO: figure out if this
          // is the right behavior
          img.write(x, y, 0x0000000FF);

        } else {
          throw std::runtime_error(std::format("color {:X} not found in color map", color_id));
        }

      } else if (header.pixel_size == 0x0010 && header.component_size == 5) { // xrgb1555
        img.write(x, y, phosg::rgba8888_for_xrgb1555(color_id));

      } else if (header.pixel_size == 0x0020 && header.component_size == 8) { // xrgb8888
        img.write(x, y, phosg::rgba8888_for_argb8888(color_id) | 0x000000FF);

      } else {
        throw std::runtime_error("unsupported pixel format");
      }
    }
  }
  return img;
}

phosg::ImageRGB888 decode_color_image(
    const PixelMapHeader& header, const PixelMapData& pixel_map, const ColorTable* ctable) {
  return decode_color_image_t<phosg::PixelFormat::RGB888>(header, pixel_map, ctable, nullptr, 0);
}
phosg::ImageRGBA8888N decode_color_image_masked(
    const PixelMapHeader& header,
    const PixelMapData& pixel_map,
    const ColorTable* ctable,
    const PixelMapData& mask_map,
    size_t mask_row_bytes) {
  return decode_color_image_t<phosg::PixelFormat::RGBA8888_NATIVE>(
      header, pixel_map, ctable, &mask_map, mask_row_bytes);
}

std::vector<Color8> to_color8(const std::vector<Color>& cs) {
  std::vector<Color8> ret;
  for (const auto& c : cs) {
    ret.emplace_back(c.as8());
  }
  return ret;
}

std::vector<Color8> to_color8(const std::vector<ColorTableEntry>& cs) {
  std::vector<Color8> ret;
  for (const auto& c : cs) {
    ret.emplace_back(c.c.as8());
  }
  return ret;
}

std::vector<Color8> to_color8(const std::vector<PaletteEntry>& cs) {
  std::vector<Color8> ret;
  for (const auto& c : cs) {
    ret.emplace_back(c.c.as8());
  }
  return ret;
}

} // namespace ResourceDASM
