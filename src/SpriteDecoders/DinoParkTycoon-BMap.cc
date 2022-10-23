#include "Decoders.hh"

#include <string.h>

#include <map>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

using namespace std;



Image decode_BMap(const string& data) {
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
