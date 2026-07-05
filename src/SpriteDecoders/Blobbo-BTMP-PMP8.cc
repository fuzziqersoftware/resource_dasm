#include "Decoders.hh"

#include <string.h>

#include <map>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

namespace ResourceDASM {

// These appear to be just directly saved out of the memory of whatever program created them. The bitmap pointers are
// even still present in the reserved fields.

phosg::ImageG1 decode_BTMP(const std::string& data) {
  phosg::StringReader r(data);
  r.skip(4); // Buffer pointer in memory, reserved in file
  const auto& header = r.get<BitMapHeader>();
  if (header.flags_row_bytes & 0xC000) {
    throw std::runtime_error("monochrome bitmap has flags set");
  }

  size_t data_bytes = header.bytes();
  return decode_monochrome_image(
      r.getv(data_bytes), data_bytes, header.bounds.width(), header.bounds.height(), header.flags_row_bytes & 0x3FFF);
}

phosg::ImageRGB888 decode_PMP8(const std::string& data, const std::vector<ColorTableEntry>& clut) {
  auto ctable = ColorTable::from_entries(clut);
  // TODO: This is not always correct behavior. Refactor render_sprite (and probably also ResourceFile::decode_clut) to
  // preserve the flags from the input file.
  ctable->flags |= 0x8000;

  phosg::StringReader r(data);
  r.skip(4); // Buffer pointer in memory, reserved in file
  const auto& header = r.get<PixelMapHeader>();
  if ((header.flags_row_bytes & 0x8000) == 0) {
    throw std::runtime_error("color pixel map is missing color flag");
  }

  size_t image_bytes = PixelMapData::size(header.flags_row_bytes & 0x3FFF, header.bounds.height());
  const auto& map = r.get<PixelMapData>(true, image_bytes);
  return decode_color_image(header, map, ctable.get());
}

} // namespace ResourceDASM
