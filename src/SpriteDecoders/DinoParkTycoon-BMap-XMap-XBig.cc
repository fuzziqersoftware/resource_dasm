#include "Decoders.hh"

#include <string.h>

#include <map>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

#include "../DataCodecs/Codecs.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {

ImageGA11 decode_BMap(const string& data) {
  // A BMap is really just a BitMapHeader and the associated data, stuffed into
  // an uncompressed resource, with a couple of extra header fields.

  StringReader r(data);
  r.skip(4); // Buffer pointer in memory, reserved in file
  const auto& header = r.get<BitMapHeader>();
  if (header.flags_row_bytes & 0xC000) {
    throw runtime_error("monochrome bitmap has flags set");
  }

  size_t image_bytes = header.bytes();

  r.skip(4); // Unknown
  if (r.get_u32b() != image_bytes) {
    throw runtime_error("data size field is incorrect");
  }
  size_t mask_region_size = r.get_u32b();
  ImageG1 decoded = decode_monochrome_image(
      r.getv(image_bytes),
      image_bytes,
      header.bounds.width(),
      header.bounds.height(),
      header.flags_row_bytes & 0x3FFF);
  ImageGA11 ret = decoded.change_pixel_format<PixelFormat::GA11>();

  size_t region_start_offset = r.where();
  Region rgn(r);
  if (r.where() - region_start_offset != mask_region_size) {
    throw runtime_error("region parsing did not consume all region data");
  }

  auto rgn_it = rgn.iterate(header.bounds);
  for (size_t y = 0; y < ret.get_height(); y++) {
    for (size_t x = 0; x < ret.get_width(); x++) {
      uint32_t color = ret.read(x, y);
      color = rgn_it.check() ? (color | 0x000000FF) : (color & 0xFFFFFF00);
      ret.write(x, y, color);
      rgn_it.right();
    }
    rgn_it.next_line();
  }

  return ret;
}

vector<ImageG1> decode_XBig(const string& data) {
  // An XBig is a sequence of 4 bitmaps (similar to BMap) stuffed into a
  // resource. The number of images is not specified anywhere; some of them may
  // be missing (headers will all be zero). We don't check for this, and just
  // return an empty Image for the bitmaps that are absent.

  string decompressed_data;

  StringReader r(data);
  uint32_t encoding = r.get_u32b(false);
  if (encoding == 0x524C4520) {
    decompressed_data = decompress_dinopark_tycoon_rle(data);
    r = StringReader(decompressed_data);
  } else if (encoding == 0x4C5A5353) {
    decompressed_data = decompress_dinopark_tycoon_lzss(data);
    r = StringReader(decompressed_data);
  }

  // The headers are all at the beginning, and the image data for each bitmap
  // follows the last header (in the same order as the headers).
  BitMapHeader headers[4];
  for (size_t x = 0; x < 4; x++) {
    r.skip(4); // Buffer pointer in memory, reserved in file
    headers[x] = r.get<BitMapHeader>();
    if (headers[x].flags_row_bytes & 0xC000) {
      throw runtime_error("monochrome bitmap has flags set");
    }
  }

  r.skip(4); // image_bytes (we compute this from each header instead)

  vector<ImageG1> images;
  for (size_t x = 0; x < 4; x++) {
    size_t image_bytes = headers[x].bytes();
    images.emplace_back(decode_monochrome_image(
        r.getv(image_bytes),
        image_bytes,
        headers[x].bounds.width(),
        headers[x].bounds.height(),
        headers[x].flags_row_bytes & 0x3FFF));
  }
  return images;
}

ImageRGBA8888 decode_XMap(const string& data, const vector<ColorTableEntry>& clut) {
  // XMap is the color analogue of BMap; it consists of a PixMapHeader and the
  // corresponding data, but also optionally includes to Regions. One of these
  // is the clipping region, but it's not clear what the other is for.

  string decompressed_data;

  StringReader r(data);
  uint32_t encoding = r.get_u32b(false);
  if (encoding == 0x524C4520) {
    decompressed_data = decompress_dinopark_tycoon_rle(data);
    r = StringReader(decompressed_data);
  } else if (encoding == 0x4C5A5353) {
    decompressed_data = decompress_dinopark_tycoon_lzss(data);
    r = StringReader(decompressed_data);
  }

  r.skip(0x0C); // Unknown
  const auto& header = r.get<PixelMapHeader>();
  if (!(header.flags_row_bytes & 0x8000)) {
    throw runtime_error("color pixel map is missing color flag");
  }

  Region rgn1(r); // Unknown what this is for

  size_t pixel_data_size = r.get_u32b();
  size_t mask_region_size = r.get_u32b();

  const PixelMapData& pixel_data = r.get<PixelMapData>(true, pixel_data_size);

  auto ctable = ColorTable::from_entries(clut);
  auto ret = decode_color_image(header, pixel_data, ctable.get()).change_pixel_format<PixelFormat::RGBA8888>();

  size_t region_start_offset = r.where();
  Region mask_rgn(r);
  if (r.where() - region_start_offset != mask_region_size) {
    throw runtime_error("region parsing did not consume all region data");
  }

  auto mask_rgn_it = mask_rgn.iterate(header.bounds);
  for (size_t y = 0; y < ret.get_height(); y++) {
    for (size_t x = 0; x < ret.get_width(); x++) {
      uint32_t color = ret.read(x, y);
      color = mask_rgn_it.check() ? (color | 0x000000FF) : (color & 0xFFFFFF00);
      ret.write(x, y, color);
      mask_rgn_it.right();
    }
    mask_rgn_it.next_line();
  }

  return ret;
}

} // namespace ResourceDASM
