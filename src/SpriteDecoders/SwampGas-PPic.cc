#include "Decoders.hh"

#include <string.h>

#include <map>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

using namespace std;



string decompress_PPic_pixel_map_data(const string& data, size_t row_bytes, size_t height) {
  // Decompression works in 4x4 blocks of pixels, organized in reading order
  // (left to right in each row, rows going down). The commands are documented
  // within the switch statement.

  StringReader r(data);
  StringWriter current_rows[4];
  StringWriter w;

  uint16_t last_0x_word = 0;
  uint8_t last_4x_colors[2] = {0, 0};
  uint8_t last_6x_colors[4] = {0, 0, 0, 0};
  for (size_t y = 0; y < height; y += 4) {
    while (current_rows[0].str().size() < row_bytes) {
      uint8_t cmd = r.get_u8();
      uint8_t cmd_low = cmd & 0x0F;

      switch (cmd & 0xF0) {
        // 0X XY - Write (XX + 1) blocks of color Y; remember Y
        // 1X - Write (X + 1) blocks of remembered color Y
        case 0x00:
        case 0x10: {
          size_t count;
          if (!(cmd & 0x10)) {
            uint8_t arg = r.get_u8();
            count = ((cmd_low << 4) | (arg >> 4)) + 1;
            uint8_t color = arg & 0x0F;
            last_0x_word = (color << 12) | (color << 8) | (color << 4) | color;
          } else {
            count = cmd_low + 1;
          }
          for (size_t line = 0; line < 4; line++) {
            for (size_t z = 0; z < count; z++) {
              // Technically we should use put_u16b here, but byteswapping would
              // have no effect here
              current_rows[line].put_u16(last_0x_word);
            }
          }
          break;
        }

        // 2X - Duplicate previous block (X + 1) times
        // 3X - Same as 2X but do it (X + 0x11) times instead
        case 0x20:
        case 0x30:
          cmd_low += (cmd & 0x10) + 1;
          for (size_t line = 0; line < 4; line++) {
            auto& row = current_rows[line].str();
            if (row.size() < 2) {
              throw runtime_error("repeat command given before any blocks were written");
            }
            // Like 0X/1X, we should byteswap here, but that would just waste time
            uint16_t v = *reinterpret_cast<const uint16_t*>(
                row.data() + (row.size() - 2));
            for (size_t z = 0; z < cmd_low; z++) {
              current_rows[line].put_u16(v);
            }
          }
          break;

        // 4X YZ [...] - Write (X + 1) 2-color blocks. Each block is given by a
        //     uint16 following YZ, where the first 4 bits specify the colors in
        //     row 0 (0=Y, 1=Z), the next 4 specify colors in row 1, etc.
        // 5X [...] - Same as 4X but use remembered YZ from previous 4X
        case 0x40:
        case 0x50: {
          cmd_low++;
          if (!(cmd & 0x10)) {
            uint8_t c = r.get_u8();
            last_4x_colors[0] = (c >> 4) & 0x0F;
            last_4x_colors[1] = c & 0x0F;
          }
          for (size_t z = 0; z < cmd_low; z++) {
            uint16_t block_bits = r.get_u16b();
            for (size_t line = 0; line < 4; line++) {
              uint16_t data = 0;
              for (size_t xx = 0; xx < 4; xx++) {
                data = (data << 4) | last_4x_colors[(block_bits >> 15) & 1];
                block_bits <<= 1;
              }
              current_rows[line].put_u16b(data);
            }
          }
          break;
        }

        // 6X ABCD [...] - Write (X + 1) 4-color blocks. Each block is given by a
        //     uint32 following ABCD, where the first 8 bits specify the colors in
        //     row 0 (2 bits for each pixel; 0=A, 1=B, 2=C, 3=D), etc.
        // 7X [...] - Same as 6X but use remembered ABCD from previous 6X
        case 0x60:
        case 0x70:
          cmd_low++;
          if (!(cmd & 0x10)) {
            uint8_t c = r.get_u8();
            last_6x_colors[0] = (c >> 4) & 0x0F;
            last_6x_colors[1] = c & 0x0F;
            c = r.get_u8();
            last_6x_colors[2] = (c >> 4) & 0x0F;
            last_6x_colors[3] = c & 0x0F;
          }
          for (size_t z = 0; z < cmd_low; z++) {
            uint32_t block_bits = r.get_u32b();
            for (size_t line = 0; line < 4; line++) {
              uint16_t data = 0;
              for (size_t xx = 0; xx < 4; xx++) {
                data = (data << 4) | last_6x_colors[(block_bits >> 30) & 3];
                block_bits <<= 2;
              }
              current_rows[line].put_u16b(data);
            }
          }
          break;

        // 8X - No-op
        // 9X - No-op
        case 0x80:
        case 0x90:
          // This looks like it does weird things in the original code - notably,
          // it doesn't change the row write pointers, but it DOES decrease the
          // remaining block count. Doesn't that mean the row would end up with
          // some uninitialized blocks at the end?
          throw runtime_error("no-op command in stream");

        // AX [...] - Write (X + 1) uncompressed blocks. Each block is given by a
        //     uint64_t following the command. The first 16 bits are written to
        //     row 0, the second 16 bits to row 1, etc.
        // BX [...] - Same as AX but write (X + 0x11) blocks
        case 0xA0:
        case 0xB0:
          cmd_low += (cmd & 0x10) + 1;
          for (size_t z = 0; z < cmd_low; z++) {
            uint64_t block_bits = r.get_u64b();
            for (size_t line = 0; line < 4; line++) {
              current_rows[line].put_u16b(block_bits >> (48 - (16 * line)));
            }
          }
          break;

        default:
          // The original code's jump table has only 11 entries, so it executes
          // garbage if this happens, which likely makes it crash catastrophically
          throw runtime_error("invalid opcode");
      }
    }

    // If the image height isn't a multiple if 4, the last row of blocks is
    // shifted up by a few pixels and the previous row of blocks is partially
    // overwritten.
    size_t remaining_rows = height - y;
    if (remaining_rows < 4) {
      w.str().resize(w.str().size() - (row_bytes * (4 - remaining_rows)));
    }

    for (size_t yy = 0; yy < 4; yy++) {
      if (current_rows[yy].size() != row_bytes) {
        throw runtime_error(string_printf(
            "decompressed row is not row_bytes in length (expected 0x%zX bytes, received 0x%zX bytes)",
            row_bytes, current_rows[yy].size()));
      }
      w.write(current_rows[yy].str());
      current_rows[yy].reset();
    }
  }

  return w.str();
}



string decompress_PPic_bitmap_data(const string& data, size_t row_bytes, size_t height) {
  // General format:
  // 00 XYYY <data> - repeat <data> (X + 1 bytes) Y times
  // 01-7F <data> - N raw data bytes
  // 80-FF VV - repeat V (~N + 1) times in the output
  StringReader r(data);
  StringWriter w;
  while (!r.eof() && w.str().size() < row_bytes * height) {
    uint8_t cmd = r.get_u8();
    if (cmd == 0) {
      uint16_t args = r.get_u16b();
      size_t bytes = ((args >> 12) & 0xF) + 1;
      size_t count = (args & 0x0FFF);
      string data = r.read(bytes);
      for (size_t z = 0; z < count; z++) {
        w.write(data);
      }
    } else if (cmd & 0x80) {
      uint8_t v = r.get_u8();
      size_t count = ((~cmd) + 1) & 0xFF;
      for (size_t x = 0; x < count; x++) {
        w.put_u8(v);
      }
    } else {
      w.write(r.read(cmd));
    }
  }

  if (w.str().size() != row_bytes * height) {
    throw runtime_error("decompression produced the wrong amount of data");
  }

  // The original code decompresses each line using row_bytes as a stride, so
  // the data is essentially in column-major format. We instead decompress
  // everything at once without doing this, so we need to transpose the data
  // after decompressing.
  StringWriter tw;
  const string& ts = w.str();
  for (size_t dest_y = 0; dest_y < height; dest_y++) {
    for (size_t dest_x = 0; dest_x < row_bytes; dest_x++) {
      size_t src_index = dest_x * height + dest_y;
      tw.put_u8(ts[src_index]);
    }
  }

  return tw.str();
}

vector<Image> decode_PPic(const string& data, const vector<ColorTableEntry>& clut) {
  StringReader r(data);

  uint16_t count = r.get_u16b();
  vector<Image> ret;
  while (ret.size() < count) {
    size_t block_start_offset = r.where();
    size_t block_end_offset = block_start_offset + r.get_u32b();
    r.skip(4); // unused (pixmap/bitmap data handle)
    if (r.get_u16b(false) & 0x8000) { // color (pixel map)
      const auto& header = r.get<PixelMapHeader>();

      shared_ptr<ColorTable> external_clut;
      const ColorTable* effective_clut = nullptr;
      if (header.color_table_offset == 0xFFFFFFFF) {
        if (clut.empty()) {
          throw runtime_error("PPic does not have embedded color table, and no clut was provided");
        }
        external_clut = ColorTable::from_entries(clut);
        effective_clut = external_clut.get();

      } else {
        if (header.color_table_offset != 0) {
          throw runtime_error("PPic embedded color table does not immediately follow header");
        }
        effective_clut = &r.get<ColorTable>(false);
        if (effective_clut->num_entries < 0) {
          throw runtime_error("color table has negative size");
        }
        r.skip(effective_clut->size());
      }

      uint16_t row_bytes = header.flags_row_bytes & 0x3FFF;
      uint16_t height = header.bounds.height();

      string data = decompress_PPic_pixel_map_data(
          r.read(block_end_offset - r.where()), row_bytes, height);

      size_t expected_size = PixelMapData::size(row_bytes, height);
      if (data.size() != expected_size) {
        throw runtime_error(string_printf(
            "decompressed pixel map data size is incorrect (expected 0x%zX bytes, received 0x%zX bytes)",
            expected_size, data.size()));
      }
      const PixelMapData* pixmap_data = reinterpret_cast<const PixelMapData*>(
          data.data());

      ret.emplace_back(decode_color_image(header, *pixmap_data, effective_clut));

    } else { // monochrome (bitmap)
      const auto& header = r.get<BitMapHeader>();
      string data = decompress_PPic_bitmap_data(r.read(block_end_offset - r.where()),
          header.flags_row_bytes, header.bounds.height());
      ret.emplace_back(decode_monochrome_image(
          data.data(),
          data.size(),
          header.bounds.width(),
          header.bounds.height(),
          header.flags_row_bytes & 0x3FFF));
    }

    r.go(block_end_offset);
  }

  return ret;
}
