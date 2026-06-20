#include "Decoders.hh"

#include <stdint.h>

#include <phosg/Encoding.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace phosg;

namespace ResourceDASM {

// Odyssey: The Legend of Nemesis (Paranoid Productions, 1996) stores all of its
// sprites in 'SHPS' resources. The engine is a descendant of Bungie's Minotaur
// engine, so the embedded color table uses the same layout as Marathon's .256
// images (see Bungie-256.cc), but the sprite container is its own format.
//
// Every SHPS resource is a set of fixed 32x32, 8-bit indexed frames. The frames
// are stored as two independent RLE streams that use the same codec: a stream of
// palette indices and a separate 1bpp mask stream (set bit = opaque). The frame
// pixels are contiguous and row-major (frame i occupies indices
// [i*1024, (i+1)*1024)); the mask uses 4 bytes per row, MSB first.
//
// Layout (all integers big-endian):
//   0x00  u16  frame_count
//   0x02  u32  pixel_data_size   (== frame_count * 32 * 32)
//   0x06  u32  mask_data_size    (== frame_count * 32 * 4)
//   0x0A  u32  decompressed pixel offset hint (always 0x83C)
//   0x0E  u32  mask_stream_offset (also the end of the pixel stream)
//   0x12  u16  height            (always 32; asserted by the engine)
//   0x14  u16  width             (always 32; asserted by the engine)
//   0x3A  256 * { u16 value; u16 red; u16 green; u16 blue; }  -- color table
//   0x83A u16  unknown (not needed for decoding)
//   0x83C u32  decompressed pixel size (== pixel_data_size)
//   0x840 ...  pixel RLE stream, up to mask_stream_offset
//   ...   ...  mask RLE stream, to end of resource

struct SHPSHeader {
  be_uint16_t frame_count;
  be_uint32_t pixel_data_size;
  be_uint32_t mask_data_size;
  be_uint32_t decompressed_pixel_offset; // always 0x83C
  be_uint32_t mask_stream_offset;
  be_uint16_t height;
  be_uint16_t width;
} __attribute__((packed));

struct SHPSColorTableEntry {
  be_uint16_t value;
  be_uint16_t red;
  be_uint16_t green;
  be_uint16_t blue;
} __attribute__((packed));

static constexpr size_t SHPS_COLOR_TABLE_OFFSET = 0x3A;
static constexpr size_t SHPS_NUM_COLORS = 256;
static constexpr size_t SHPS_PIXEL_STREAM_OFFSET = 0x840;

// The pixel and mask streams share this codec. It is count-driven: it produces
// exactly target_size bytes and stops, ignoring any trailing input (trailing
// transparent runs are simply not encoded).
//   c & 0x80  -> literal run of (c & 0x7F) + 1 bytes
//   else      -> repeat the next byte (c + 3) times
static string decompress_SHPS_stream(StringReader& r, size_t target_size) {
  string ret;
  ret.reserve(target_size);
  while ((ret.size() < target_size) && !r.eof()) {
    uint8_t cmd = r.get_u8();
    if (cmd & 0x80) {
      size_t count = (cmd & 0x7F) + 1;
      for (size_t z = 0; z < count && !r.eof(); z++) {
        ret.push_back(r.get_s8());
      }
    } else {
      size_t count = cmd + 3;
      if (r.eof()) {
        break;
      }
      ret.append(count, r.get_s8());
    }
  }
  ret.resize(target_size, '\0'); // pad in case the stream ended early
  return ret;
}

vector<ImageRGBA8888N> decode_SHPS(const string& data) {
  StringReader r(data);
  const auto& header = r.get<SHPSHeader>();

  uint16_t width = header.width;
  uint16_t height = header.height;
  if (width != 32 || height != 32) {
    throw runtime_error("SHPS frames must be 32x32");
  }
  size_t frame_count = header.frame_count;

  // Decode the embedded color table. Each entry names the palette slot (value)
  // that a pixel index maps to; the entries can be sparse and out of order, and
  // unused slots are zero-filled. A fully-zero entry is unused and must not
  // overwrite a real entry (several padding entries carry value 0).
  uint32_t color_for_index[SHPS_NUM_COLORS] = {0};
  StringReader color_r = r.sub(SHPS_COLOR_TABLE_OFFSET, SHPS_NUM_COLORS * sizeof(SHPSColorTableEntry));
  for (size_t z = 0; z < SHPS_NUM_COLORS; z++) {
    const auto& e = color_r.get<SHPSColorTableEntry>();
    uint16_t value = e.value;
    uint8_t red = e.red >> 8;
    uint8_t green = e.green >> 8;
    uint8_t blue = e.blue >> 8;
    if ((value | red | green | blue) == 0) {
      continue;
    }
    if (value < SHPS_NUM_COLORS) {
      color_for_index[value] = (static_cast<uint32_t>(red) << 24) |
          (static_cast<uint32_t>(green) << 16) |
          (static_cast<uint32_t>(blue) << 8) | 0xFF;
    }
  }

  // The two streams are stored back to back; the pixel stream runs from
  // SHPS_PIXEL_STREAM_OFFSET to mask_stream_offset, and the mask stream from
  // there to the end of the resource.
  size_t mask_stream_offset = header.mask_stream_offset;
  StringReader pixel_r = r.sub(SHPS_PIXEL_STREAM_OFFSET, mask_stream_offset - SHPS_PIXEL_STREAM_OFFSET);
  StringReader mask_r = r.sub(mask_stream_offset, r.size() - mask_stream_offset);
  string pixels = decompress_SHPS_stream(pixel_r, header.pixel_data_size);
  string mask = decompress_SHPS_stream(mask_r, header.mask_data_size);

  size_t frame_pixels = static_cast<size_t>(width) * height;
  size_t mask_row_bytes = (width + 7) / 8;
  size_t frame_mask_bytes = mask_row_bytes * height;

  vector<ImageRGBA8888N> ret;
  ret.reserve(frame_count);
  for (size_t f = 0; f < frame_count; f++) {
    ImageRGBA8888N image(width, height);
    for (size_t y = 0; y < height; y++) {
      for (size_t x = 0; x < width; x++) {
        uint8_t index = pixels[f * frame_pixels + y * width + x];
        size_t mask_byte = f * frame_mask_bytes + y * mask_row_bytes + (x >> 3);
        bool opaque = (static_cast<uint8_t>(mask[mask_byte]) >> (7 - (x & 7))) & 1;
        image.write(x, y, opaque ? color_for_index[index] : 0x00000000);
      }
    }
    ret.emplace_back(std::move(image));
  }

  return ret;
}

} // namespace ResourceDASM
