#include "Decoders.hh"

#include <stdint.h>

#include <phosg/Encoding.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <vector>

#include "../DataCodecs/Codecs.hh"

namespace ResourceDASM {

ResourceFile::DecodedPICTResource decode_NPIC(const std::string& data) {
  auto decoded = decrypt_encrypt_odyssey(data);
  return ResourceFile::decode_PICT_only(decoded.data(), decoded.size());
}

// Odyssey: The Legend of Nemesis (Paranoid Productions, 1996) stores all of its sprites in 'SHPS' resources. The
// engine is a descendant of Bungie's Minotaur engine, so the embedded color table uses the same layout as Marathon's
// .256 images (see Bungie-256.cc), but the sprite container is its own format.
//
// Every SHPS resource is a set of fixed 32x32, 8-bit indexed frames. The frames are stored as two independent RLE
// streams that use the same codec: a stream of palette indices and a separate 1bpp mask stream (set bit = opaque). The
// frame pixels are contiguous and row-major (frame i occupies indices [i*1024, (i+1)*1024)); the mask uses 4 bytes per
// row, MSB first.

struct SHPSHeader {
  /* 0000 */ phosg::be_uint16_t frame_count;
  /* 0002 */ phosg::be_uint32_t decompressed_pixel_buffer_size;
  /* 0006 */ phosg::be_uint32_t decompressed_mask_buffer_size;
  /* 000A */ phosg::be_uint32_t pixel_buffer_offset;
  /* 000E */ phosg::be_uint32_t mask_buffer_offset;
  /* 0012 */ phosg::be_uint16_t frame_height;
  /* 0014 */ phosg::be_uint16_t frame_width;
  /* 0016 */ phosg::be_uint32_t unknown_a1;
  /* 001A */ uint8_t unknown_a2[0x1E];
  /* 0038 */ phosg::be_uint16_t used_color_table_entries;
  /* 003A */ ColorTableEntry color_table[0x100];
  /* 083A */ phosg::be_uint16_t unknown_a3;
  /* 083C (compressed pixel and mask RLE streams follow here, in that order) */
} __attribute__((packed));
static_assert(sizeof(SHPSHeader) == 0x83C, "SHPSHeader size is incorrect");

std::vector<phosg::ImageRGBA8888N> decode_SHPS(const std::string& data) {
  phosg::StringReader r(data);
  const auto& header = r.get<SHPSHeader>();

  if (header.frame_width != 32 || header.frame_height != 32) {
    throw std::runtime_error("SHPS frames must be 32x32");
  }

  // Decode the embedded color table. Entries can be sparse and out of order; a fully-zero entry is unused and must not
  // overwrite a real entry (several padding entries have value 0)
  uint32_t color_table[0x100];
  for (size_t z = 0; z < 0x100; z++) {
    const auto& e = header.color_table[z];
    uint8_t red = e.c.r >> 8;
    uint8_t green = e.c.g >> 8;
    uint8_t blue = e.c.b >> 8;
    if (((e.color_num | red | green | blue) != 0) && (e.color_num < sizeof(color_table) / sizeof(color_table[0]))) {
      color_table[e.color_num] = phosg::rgba8888(red, green, blue, 0xFF);
    }
  }

  // The two streams are stored back to back; the pixel stream starts immediately afte the header and ends at
  // header.mask_stream_offset, and the mask stream immediately follows that
  size_t pixel_data_max_size = r.size() - header.pixel_buffer_offset;
  size_t mask_data_max_size = r.size() - header.mask_buffer_offset;
  std::string pixel_data = unpack_pathways(r.pgetv(header.pixel_buffer_offset, pixel_data_max_size), pixel_data_max_size);
  std::string mask_data = unpack_pathways(r.pgetv(header.mask_buffer_offset, mask_data_max_size), mask_data_max_size);

  auto mask = phosg::ImageG1::from_data_reference(
      mask_data.data(), header.frame_width, header.frame_height * header.frame_count);
  auto pixels = phosg::ImageG8::from_data_reference(
      pixel_data.data(), header.frame_width, header.frame_height * header.frame_count);

  std::vector<phosg::ImageRGBA8888N> ret;
  ret.reserve(header.frame_count);
  for (size_t frame_index = 0; frame_index < header.frame_count; frame_index++) {
    phosg::ImageRGBA8888N frame(header.frame_width, header.frame_height);
    for (size_t y = 0; y < header.frame_height; y++) {
      size_t src_y = frame_index * header.frame_height + y;
      for (size_t x = 0; x < header.frame_width; x++) {
        frame.write(x, y,
            (mask.read(header.frame_width - x - 1, src_y) & 0xFFFFFF00)
                ? color_table[pixels.read(x, src_y) >> 24]
                : 0x00000000);
      }
    }
    ret.emplace_back(std::move(frame));
  }

  return ret;
}

} // namespace ResourceDASM
