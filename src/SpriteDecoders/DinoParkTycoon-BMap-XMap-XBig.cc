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

Image decode_BMap(const string& data) {
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
  Image ret = decode_monochrome_image(
      r.getv(image_bytes),
      image_bytes,
      header.bounds.width(),
      header.bounds.height(),
      header.flags_row_bytes & 0x3FFF);

  size_t region_start_offset = r.where();
  Region rgn(r);
  if (r.where() - region_start_offset != mask_region_size) {
    throw runtime_error("region parsing did not consume all region data");
  }

  ret.set_has_alpha(true);
  auto rgn_it = rgn.iterate(header.bounds);
  for (size_t y = 0; y < ret.get_height(); y++) {
    for (size_t x = 0; x < ret.get_width(); x++) {
      uint64_t r, g, b;
      ret.read_pixel(x, y, &r, &g, &b);
      ret.write_pixel(x, y, r, g, b, rgn_it.check() ? 0xFF : 0x00);
      rgn_it.right();
    }
    rgn_it.next_line();
  }

  return ret;
}

vector<Image> decode_XBig(const string& data) {
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

  vector<Image> images;
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

Image decode_XMap(const string& data, const vector<ColorTableEntry>& clut) {
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
  Image ret = decode_color_image(header, pixel_data, ctable.get());

  size_t region_start_offset = r.where();
  Region mask_rgn(r);
  if (r.where() - region_start_offset != mask_region_size) {
    throw runtime_error("region parsing did not consume all region data");
  }

  ret.set_has_alpha(true);
  auto mask_rgn_it = mask_rgn.iterate(header.bounds);
  for (size_t y = 0; y < ret.get_height(); y++) {
    for (size_t x = 0; x < ret.get_width(); x++) {
      uint64_t r, g, b;
      ret.read_pixel(x, y, &r, &g, &b);
      ret.write_pixel(x, y, r, g, b, mask_rgn_it.check() ? 0xFF : 0x00);
      mask_rgn_it.right();
    }
    mask_rgn_it.next_line();
  }

  return ret;
}

} // namespace ResourceDASM
