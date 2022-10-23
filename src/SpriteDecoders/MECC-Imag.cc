#include "Decoders.hh"

#include <string.h>

#include <string>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>

#include "../QuickDrawFormats.hh"

using namespace std;



// MECC's Imag resource format is... an adventure.
//
// All Imag resources may contain multiple images. The overall structure is:
// struct Imag {
//   be_uint16_t num_images;
//   struct ImagEntry {
//     be_uint32_t size; // Total entry size, including this field
//     be_uint32_t unused;
//     // Test the high bit of flags_row_bytes (the first field in both of these
//     // header types) to determine which header is present. If the bit is set,
//     // it's a PixelMapHeader.
//     BitMapHeader OR PixelMapHeader header;
//     // The color table is only present if header is a PixelMapHeader and
//     // header.color_table_offset != 0xFFFFFFFF.
//     ColorTable color_table;
//     // Most of the color formats have an additional header within the
//     // compressed data here. See the various decoding functions for details.
//     uint8_t compressed_data[...until end of entry];
//   } entries[...EOF];
// };
//
// There are 5 sub-formats, each used in different scenarios:
// 1. Monochrome format. This format is the same across all MECC games that use
//    Imag. The format is relatively simple; see decode_monochrome_Imag_section
//    for details.
// 2. Fraction Munchers color format. This is the simplest of the color formats;
//    it uses the same compression as monochrome format but interprets the
//    decompressed result as indexed color data instead of as 8-pixel blocks.
//    See decode_fraction_munchers_color_Imag_section for details. This format
//    was used in all the Munchers games, including Word Munchers, Number
//    Munchers, Fraction Munchers, and Super Munchers.
// 3. Color commands format. This format encodes a bytestream in a series of
//    commands, each of which must produce the same amount of output. See
//    decode_color_Imag_commands for details. This format was used in many
//    (perhaps all?) color games after the Munchers series.
// 4. Color blocks format v1. This format was used in The Secret Island of Dr.
//    Quandary, SnapDragon, and a few other titles. Images are encoded as
//    sequences of 8x8-pixel blocks, which may be compressed individually using
//    some rather complex mechanics. Like most of the other formats described
//    here, the blocks are assembled in column-major order rather than row-major
//    order. See decode_color_Imag_blocks for details on this algorithm.
// 5. Color blocks format v2. This format was used in The Amazon Trail and Odell
//    Down Under, two of MECC's latest releases. It makes some changes to the
//    command codes used in v1, adds a few features for more efficient
//    compression, and simplifies some of the behaviors of various v1 commands.
//    This is also implemented in decode_color_Imag_blocks, since many of the
//    commands are the same as in v1.
// 
// Unfortunately, there is no good way to tell whether a color image resource
// uses Fraction Munchers format or the other color formats based only on the
// contents of the resource. For all other formats, including monochrome, there
// are flags within the data that we use to choose the appropriate behaviors.
// 
// The titles in which each format was used shed some light on the order the
// formats were developed (though this is also fairly evident from the code):
//   Title             | Mono | Fraction Munchers | Commands | Blocks1 | Blocks2
//   ------------------+------+-------------------+----------+---------+--------
//   Number Munchers   | ++++ | +++++++++++++++++ |          |         |
//   Word Munchers     | ++++ | +++++++++++++++++ |          |         |
//   Super Munchers    | ++++ | +++++++++++++++++ |          |         |
//   Fraction Munchers | ++++ | +++++++++++++++++ |          |         |
//   Oregon Trail      | ++++ |                   | ++++++++ |         |
//   SnapDragon        | ++++ |                   | ++++++++ | +++++++ |
//   BodyScope         |      |                   | ++++++++ | +++++++ |
//   Dr. Quandary      | ++++ |                   | ++++++++ | +++++++ |
//   Odell Down Under  | ++++ |                   | ++++++++ |         | +++++++
//   Amazon Trail      |      |                   | ++++++++ | +++++++ | +++++++
//
// There are some quirks in the various encodings' designs that make it
// difficult to understand why they went to such lengths to compress images.
// Some of the methods seem inspired by techniques used in JPEG and other
// advanced (for the time) compression formats, such as diagonalization and
// Huffman-like encoding of const table references, but there are also some
// cases in which space is wasted or suboptimal compression is forced by the
// design. The complexity of the decoders implies that a fair amount of work
// went into them, so it's hard to believe that these choices were accidental.
//
// Also, most 2-byte integers being encoded in little-endian byte order is a
// curiosity - were the authoring tools written for Windows, perhaps?



string split_uniform_little_endian_bit_fields(
    StringReader& r, size_t count, uint8_t bits, bool is_delta) {
  // This function reads (count) (bits)-bit integers from the input,
  // sign-extending them to 8 bits if is_delta is true. The bits are arranged in
  // little-endian order; that is, the next highest bit above the high bit of
  // input byte 0 is the low bit of input byte 1.

  // For example, if count=4 and bits=6, then this function reads 3 bytes from
  // the input (4 * 6 == 24 bits == 3 bytes) and rearranges them like so:
  // Input bytes  = ABCDEFGH IJKLMNOP QRSTUVWX
  // Output bytes = ccCDEFGH mmMNOPAB wwWXIJKL qqQRSTUV
  // The output bits cc, mm, ww, and qq are 1 if is_delta is true and their
  // corresponding source bits (C, M, W, and Q) are 1; otherwise they are 0.

  string ret;

  uint8_t mask = (1 << bits) - 1;
  uint8_t sign_mask = (1 << (bits - 1));

  uint8_t bits_valid = 0;
  uint16_t bits_pending = 0;
  while (ret.size() < count) {
    if (bits_valid < bits) {
      bits_pending |= (r.get_u8() << bits_valid);
      bits_valid += 8;
    }

    uint8_t v = bits_pending & mask;
    if (is_delta && (v & sign_mask)) {
      v |= ~mask;
    }
    ret.push_back(v);
    bits_pending >>= bits;
    bits_valid -= bits;
  }

  return ret;
}

string decode_from_const_table(
    StringReader& r,
    size_t output_bytes,
    uint8_t bits,
    bool is_delta,
    bool is_v2,
    const string& const_table) {
  // This function decodes a const-table-encoded sequence.

  // Input values are read as a sequence of (bits)-bit integers encoded in
  // separate bytes (as produced by split_uniform_little_endian_bit_fields).
  // The first (1 << bits) - 1 entries of the const table are referenced
  // directly by their indexes; the remaining entries are referenced by
  // prefixing their values with a maximum-value entry.

  // For example, if bits=3, then only the values 0-7 may occur in the input
  // bytes, but if the const table has 11 entries, then we need a way to encode
  // the remaining entries. So, entries 0-6 in the const table are encoded as
  // the values 0-6 in the input bytes, and entries 7-11 are encoded as a byte
  // with the value 7, followed by a byte with the value 0-4. (The actual
  // referenced const table entry is the sum of all the 7-valued bytes, plus the
  // next non-7-valued byte.)

  // In blocks format v2, this behavior is slightly modified: the maximum value
  // is encoded without a terminating non-maximum-value byte. For example, if
  // bits=2 (so the input bytes are all 0-3) and the const table has 6 entries,
  // the sequence 03 03 00 would refer to the last entry in blocks v1. But in
  // blocks v2, there is a special case that skips reading the 00 byte if the
  // accumulated max-value bytes reach the end of the const table, so this is
  // encoded instead as 03 03 in blocks v2.

  // If is_delta is true, the bytes are encoded as signed deltas from the
  // previous index instead of absolute indexes on their own. To extend the
  // first example (with the 11-entry const table and bits=3), if is_delta is
  // true, then the input bytes may contain the values -4 through 3. The values
  // -3, -2, -1, 0, 1, and 2 mean to use the previous output value's index, plus
  // the input byte's value; the values -4 and 3 are used to extend the deltas
  // beyond the input byte range. For example, the sequence -4, -2 means the
  // next output byte should use the previous output byte's index - 6. Indexes
  // may wrap around both ends of the const table; for example, if the previous
  // index was 1 and the next delta byte is -2, the next output byte's index
  // will be 10 (the last entry in the 11-entry const table).

  if (const_table.empty()) {
    throw runtime_error("const table is empty");
  }

  int8_t min_value_s = 0xFF << (bits - 1);
  int8_t max_value_s = ~min_value_s;
  uint8_t max_value_u = (1 << bits) - 1;
  ssize_t max_index = const_table.size() - 1;

  ssize_t index = 0;
  StringWriter w;
  while (w.size() < output_bytes) {
    if (is_delta) {
      int8_t v = r.get_s8();
      while ((v == min_value_s) || (v == max_value_s)) {
        index += v;
        v = r.get_s8();
      }
      index += v;

      // Handle indexes wrapping around either end of the const table
      while (index < 0) {
        index += const_table.size();
      }
      while (index >= static_cast<ssize_t>(const_table.size())) {
        index -= const_table.size();
      }

    } else {
      // In v2, maximum values are encoded without a trailing zero byte, whereas
      // they have a trailing zero byte in v1 - hence the slightly different
      // logic here.
      if (is_v2) {
        index = r.get_u8();
        if (index == max_value_u) {
          uint8_t v;
          do {
            v = r.get_u8();
            index += v;
          } while ((v == max_value_u) && (index != max_index));
        }

      } else {
        index = 0;
        uint8_t v = r.get_u8();
        while (v == max_value_u) {
          index += v;
          v = r.get_u8();
        }
        index += v;
      }
    }

    w.put_u8(const_table.at(index));
  }

  return move(w.str());
}

string decode_rle(StringReader& r, size_t output_size, ssize_t run_length) {
  // This function decodes a fairly simple RLE scheme. If run_length >= 0,
  // the commands are:
  // (00-7F) <data> = write <data> (run_length bytes of it) cmd + 1 times
  // (80-FF) <data> = write <data> (cmd - 0x7F bytes of it)
  // If run_length < 0, the commands are:
  // (00-7F) LL <data> = write <data> (L bytes of it) cmd + 1 times
  // (80-FF) <data> = write <data> (cmd - 0x7F bytes of it)

  StringWriter w;
  while (w.size() < output_size) {
    uint8_t cmd = r.get_u8();
    if (!(cmd & 0x80)) {
      size_t run_count = cmd + 1;
      size_t byte_count = (run_length < 0) ? r.get_u8() : run_length;
      const uint8_t* data = &r.get<uint8_t>(true, byte_count);
      for (size_t z = 0; z < run_count; z++) {
        w.write(data, byte_count);
      }
    } else {
      size_t byte_count = cmd - 0x7F;
      w.write(&r.get<uint8_t>(true, byte_count), byte_count);
    }
  }

  return move(w.str());
}

void render_direct_block(
    Image& i,
    StringReader& r,
    size_t dest_x,
    size_t dest_y,
    const vector<ColorTableEntry>& clut) {
  // This function reads 0x40 bytes from the input, transforms them into colors
  // with the given color table, and writes them in natural (reading) order to
  // an 8x8 square in the image.
  const uint8_t* data = &r.get<uint8_t>(true, 0x40);
  size_t max_x = min<size_t>(i.get_width(), dest_x + 8) - dest_x;
  size_t max_y = min<size_t>(i.get_height(), dest_y + 8) - dest_y;
  for (size_t y = 0; y < max_y; y++) {
    for (size_t x = 0; x < max_x; x++) {
      uint8_t v = data[(y * 8) + x];
      auto c = clut.at(v).c.as8();
      i.write_pixel(dest_x + x, dest_y + y, c.r, c.g, c.b);
    }
  }
}

void render_diagonalized_block(
    Image& i,
    StringReader& r,
    size_t dest_x,
    size_t dest_y,
    const vector<ColorTableEntry>& clut) {
  // This function renders a diagonalized 8x8 block of pixels using the given
  // color table, ordered as specified in this table.
  static const uint8_t indexes[8][8] = {
    {0x00, 0x01, 0x05, 0x06, 0x0E, 0x0F, 0x1B, 0x1C},
    {0x02, 0x04, 0x07, 0x0D, 0x10, 0x1A, 0x1D, 0x2A},
    {0x03, 0x08, 0x0C, 0x11, 0x19, 0x1E, 0x29, 0x2B},
    {0x09, 0x0B, 0x12, 0x18, 0x1F, 0x28, 0x2C, 0x35},
    {0x0A, 0x13, 0x17, 0x20, 0x27, 0x2D, 0x34, 0x36},
    {0x14, 0x16, 0x21, 0x26, 0x2E, 0x33, 0x37, 0x3C},
    {0x15, 0x22, 0x25, 0x2F, 0x32, 0x38, 0x3B, 0x3D},
    {0x23, 0x24, 0x30, 0x31, 0x39, 0x3A, 0x3E, 0x3F},
  };

  const uint8_t* data = &r.get<uint8_t>(true, 0x40);
  for (size_t y = 0; y < 8; y++) {
    for (size_t x = 0; x < 8; x++) {
      uint8_t v = data[indexes[y][x]];
      auto c = clut.at(v).c.as8();
      i.write_pixel(dest_x + x, dest_y + y, c.r, c.g, c.b);
    }
  }
}

Image decode_color_Imag_blocks(
    StringReader& r,
    size_t width,
    size_t height,
    uint16_t format_version,
    const vector<ColorTableEntry>& clut) {
  // This function decodes the MECC block-based color image formats (v1 and v2).

  // Blocks may overlap the edges of the image, but then those blocks may be
  // copied into blocks that don't. To handle this, we expand the image to a
  // multiple of 8 pixels in both dimensions, and truncate it later if needed.
  Image ret((width + 7) & (~7), (height + 7) & (~7), false);

  // For v1, the header format is:
  //   le_uint16_t block_count;
  //   uint8_t unused;
  //   uint8_t short_const_table[8];
  // For v2, the header format is:
  //   le_uint16_t block_count;
  //   uint8_t unused;
  //   uint8_t short_const_table_size;
  //   uint8_t skip_packed_block_args;
  //   uint8_t short_const_table[short_const_table_size];
  // The skip_packed_block_args field is only present if short_const_table_size
  // < 6; otherwise, it is assumed to be zero. If skip_packed_block_args is
  // nonzero, then the packed block command does not take any extended
  // arguments, and can only reference the first 5 entries of the short const
  // table. (Note that it wouldn't make sense to just implicitly enable this
  // flag every time the short const table has 5 or fewer entries, because the
  // extended arguments also allow the command to use entries not in the short
  // const table.)

  size_t block_count = r.get_u16l();
  size_t column_blocks = (height + 7) >> 3;
  r.skip(1);

  uint8_t skip_packed_block_args = 0;
  string short_const_table;
  if (format_version == 1) {
    short_const_table = r.read(8);
  } else if (format_version == 2) {
    uint8_t short_const_table_size = r.get_u8();
    if (short_const_table_size < 6) {
      skip_packed_block_args = r.get_u8();
    }
    short_const_table = r.read(short_const_table_size);
  } else {
    throw runtime_error("unknown block format version");
  }

  size_t x = 0;
  size_t y = 0;

  // Some commands refer to offsets within the command stream, which are
  // relative to the first command. However, the StringReader contains some data
  // before the command stream, so we need to correct for that when we handle
  // those commands.
  size_t commands_start_offset = r.where();
  StringReader& main_r = r;

  function<void(StringReader& r)> execute_command = [&](StringReader& r) -> void {
    uint8_t cmd = r.get_u8();
    switch (cmd & 7) {
      case 0:
        // -----000 <data>: Decode an uncompressed block (diagonalized if v1,
        //     direct if v2)
        if (format_version == 1) {
          render_diagonalized_block(ret, r, x, y, clut);
        } else {
          render_direct_block(ret, r, x, y, clut);
        }
        break;

      case 1: {
        if (format_version == 1) {
          // -----001 BBBBBBBB BBBBBBBB: Copy block number B (little-endian) to
          //     the current block. For the purpose of this command, block
          //     numbers start at 0 and increase by 1 every 8 pixels going down,
          //     then continue at the next column after reaching the bottom.
          size_t z = r.get_u16l();
          size_t src_x = (z / column_blocks) << 3;
          size_t src_y = (z % column_blocks) << 3;
          ret.blit(ret, x, y, 8, 8, src_x, src_y);

        } else {
          // ZZZZZ001 BBBBBBBB BBBBBBBB: Repeat the command starting at offset
          //     Z.B (21 bits in total) from the beginning of the command stream
          size_t offset = r.get_u16l() | ((cmd & 0xF8) << 13);
          StringReader sub_r = main_r.sub(commands_start_offset + offset);
          execute_command(sub_r);
        }
        break;
      }

      case 2: {
        // FZZZZ010 [VVVVVVVV] [CCCCCCCC]: Write one or more blocks of solid
        //     color Z-1 from the short const table. If Z = 0, read the color
        //     from the following byte (V) instead of looking it up in the short
        //     const table. If F = 0, write one block; otherwise read another
        //     byte (C) and write C+1 blocks.
        uint8_t const_table_index = (cmd >> 3) & 0x0F;
        uint8_t v;
        if (const_table_index == 0) {
          v = r.get_u8();
        } else {
          v = short_const_table.at(const_table_index - 1);
        }

        size_t count = (cmd & 0x80) ? (r.get_u8() + 1) : 1;
        auto c = clut.at(v).c.as8();
        for (size_t z = 0; z < count; z++) {
          ret.fill_rect(x, y, 8, 8, c.r, c.g, c.b);
          // Advance to the next block unless the block we just wrote is the
          // last one for this command (because the end of the loop will advance
          // to the next block anyway, and if we didn't check for this, we would
          // write N blocks but advance N+1 spaces, leaving an incorrectly-blank
          // block in the output).
          if (z != count - 1) {
            y += 8;
            if (y >= height) {
              y = 0;
              x += 8;
            }
          }
        }
        break;
      }

      case 3:
      case 4:
      case 5:
      case 6:
      case 7: {
        // There's some command number overlap here. Command 3 means the same
        // thing in both v1 and v2, but in v2, command 3 was extended to cover
        // commands 4-6 as well. Command 4 was used in v1, so in v2 they moved
        // it to command 7, but the implementation remains mostly the same.
        if (((format_version == 1) && ((cmd & 7) == 4)) ||
            ((format_version == 2) && ((cmd & 7) == 7))) {
          // ---LL100 (v1) or ---LL111 (v2): Decode fixed-length RLE (with
          //     run_length=L+1), then decode the result as a diagonalized block
          //     (v1) or a direct block (v2)
          uint8_t run_length = ((cmd >> 3) & 3) + 1;
          string decompressed = decode_rle(r, 0x40, run_length);
          StringReader decompressed_r(decompressed);
          if (format_version == 1) {
            render_diagonalized_block(ret, decompressed_r, x, y, clut);
          } else {
            render_direct_block(ret, decompressed_r, x, y, clut);
          }

        } else {
          string const_table;
          bool compressed = false;
          uint8_t bits = ((cmd >> 3) & 3) + 1;
          uint8_t index_count;
          if (format_version == 1) {
            // BAXWW011 JJHGFEDC (v1): Decode a const-table block. Arguments:
            //     ABCDEFGH = For each 1 bit, populate the corresponding short
            //         const table entry into the const table used for this
            //         block. A refers to short_const_table[0], B to [1], etc.
            //     X = 1 if data is RLE-compressed; 0 if not
            //     W = Bits per encoded index entry, minus 1 (so e.g. 10 here
            //         means 3 bits per entry)
            //     J = If this is 2, extend the const table with custom bytes
            //         (see below)
            uint8_t args = r.get_u8();
            compressed = (cmd >> 5) & 1;
            uint8_t short_const_entries_used = ((cmd >> 6) & 3) | (args << 2);
            uint8_t has_extended_const_table = (args >> 6) & 3;

            size_t max_short_const_table_entries = min<size_t>(8, short_const_table.size());
            for (size_t z = 0; z < max_short_const_table_entries; z++) {
              if (short_const_entries_used & (1 << z)) {
                const_table.push_back(short_const_table[z]);
              }
            }
            if (has_extended_const_table == 2) {
              // If the command has an extended const table, it's encoded as one
              // byte specifying the number of entries, followed by the entries.
              // The entries are appended after the entries copied from the
              // short const table.
              const_table += r.read(r.get_u8());
            }
            index_count = r.get_u8();

          } else {
            // CBAWWQQQ [ZYKJIHGF] (v2): Decode a const-table block. Arguments:
            //     ABCFGHIJK = Auto-populate these short const table entries
            //         (0-3 and 6-11) into the const table used for this block,
            //         similar to how A-H work in the v1 version of this command
            //     Q = Can only be 3-6 (since this is the same field as the
            //         command number); specifies whether to add short const
            //         table entries 4 and 5 to this block's const table
            //     W = Bits per encoded index table entry, minus 1, as in v1
            //     Y = If set, a const table extension is present (like if J=2
            //         in the v1 version of this command)
            //     Z = If set, more than the above 11 short const table
            //         references are present; see below for their encoding
            // The second byte is only present if skip_packed_block_args is
            // false. This value is global to the entire image, and is read from
            // the image header. If the second byte is not present, it is
            // treated as all zeroes.
            for (size_t z = 0; z < 3; z++) {
              if (cmd & (0x20 << z)) {
                const_table.push_back(short_const_table.at(z));
              }
            }
            uint8_t cmd_hidden_flags = (cmd & 7) - 3;
            if (cmd_hidden_flags & 1) {
              const_table.push_back(short_const_table.at(3));
            }
            if (cmd_hidden_flags & 2) {
              const_table.push_back(short_const_table.at(4));
            }
            if (skip_packed_block_args == 0) {
              uint8_t args = r.get_u8();
              for (size_t z = 0; z < 6; z++) {
                if (args & (0x01 << z)) {
                  const_table.push_back(short_const_table.at(5 + z));
                }
              }
              if (args & 0x80) {
                // Extended include flags are encoded as groups of 7 flags
                // packed into the low bits of each byte, where the high bit
                // specifies if another byte follows. Each bit (starting from
                // the low bit of each byte) specifies whether the corresponding
                // entry from the short const table should be included.
                uint8_t include_flags = 0x80;
                size_t offset = 11;
                while (include_flags & 0x80) {
                  include_flags = r.get_u8();
                  for (size_t z = 0; z < 7; z++) {
                    if (include_flags & (1 << z)) {
                      const_table.push_back(short_const_table.at(offset + z));
                    }
                  }
                  offset += 7;
                }
              }
              if (args & 0x40) {
                const_table += r.read(r.get_u8());
              }
            }

            // If an N-bit integer can fully cover the entire range of the const
            // table, then there's no point in decoding the index bytes; they
            // can just be used directly as table indexes.
            if ((1 << bits) < const_table.size()) {
              index_count = r.get_u8();
            } else {
              index_count = 0x40;
            }
          }

          string const_indexes;
          if (!compressed) {
            const_indexes = split_uniform_little_endian_bit_fields(
                r, index_count, bits, false);
          } else {
            size_t rle_output_bytes = ((bits * index_count) + 7) >> 3;
            string decompressed = decode_rle(r, rle_output_bytes, 1);
            StringReader decompressed_r(decompressed);
            const_indexes = split_uniform_little_endian_bit_fields(
                decompressed_r, index_count, bits, false);
            if (!decompressed_r.eof()) {
              throw runtime_error(string_printf(
                  "not all decompressed data was used (%zu bytes remain)", decompressed_r.remaining()));
            }
          }

          // In v1, the indexes are always encoded; in v2, they're encoded only
          // if there are more than 0x40 of them.
          if (format_version == 1 || index_count != 0x40) {
            StringReader const_indexes_r(const_indexes);
            string decoded = decode_from_const_table(
                const_indexes_r, 0x40, bits, false, format_version != 1, const_table);
            if (!const_indexes_r.eof()) {
              throw runtime_error("not all const index data was used");
            }
            StringReader decoded_r(decoded);
            if (format_version == 1) {
              render_diagonalized_block(ret, decoded_r, x, y, clut);
            } else {
              render_direct_block(ret, decoded_r, x, y, clut);
            }
            if (!decoded_r.eof()) {
              throw runtime_error("not all decoded data was used");
            }

          } else { // Not v1 and index_count == 0x40; indexes not encoded
            string decoded;
            for (char ch : const_indexes) {
              decoded.push_back(const_table.at(static_cast<uint8_t>(ch)));
            }
            StringReader decoded_r(decoded);
            render_direct_block(ret, decoded_r, x, y, clut);
            if (!decoded_r.eof()) {
              throw runtime_error("not all decoded data was used");
            }
          }
        }

        break;
      }

      default:
        throw runtime_error("unknown command");
    }
  };

  for (size_t block_num = 0; block_num < block_count; block_num++) {
    execute_command(main_r);
    y += 8;
    if (y >= height) {
      y = 0;
      x += 8;
    }
    if (x >= width) {
      break;
    }
  }

  // TODO: This is slow, but I'm lazy. The problem is that the image dimensions
  // may have been rounded up at the beginning, and we now should trim off the
  // extra space. It'd be nice if Image had a resize() function, but... I'm lazy
  Image real_ret(width, height, false);
  real_ret.blit(ret, 0, 0, width, height, 0, 0);
  return real_ret;
}

Image decode_color_Imag_commands(
    StringReader& r, const vector<ColorTableEntry>& external_clut) {
  const vector<ColorTableEntry>* clut = &external_clut;
  vector<ColorTableEntry> internal_clut;

  const auto& header = r.get<PixelMapHeader>();
  size_t width = header.bounds.width();
  size_t height = header.bounds.height();

  if (header.color_table_offset != 0xFFFFFFFF) {
    r.skip(6);
    size_t color_count = r.get_u16b() + 1;
    while (internal_clut.size() < color_count) {
      internal_clut.emplace_back(r.get<ColorTableEntry>());
    }
    clut = &internal_clut;
  }

  // The header goes like this (after the pixel map header & color table):
  //   le_uint16_t command_bytes; // Output bytes produced per command
  //   le_uint16_t num_commands;
  //   uint8_t unused;
  //   uint8_t command_data[...EOF];
  // If command_bytes is zero, then the image is block-encoded instead, and
  // num_commands is replaced with a be_uint16_t specifying the blocks format
  // version. In that case, the blocks header (see decode_color_Imag_blocks)
  // begins immediately after the format version field.
  size_t command_bytes = r.get_u16l();
  if (command_bytes == 0) {
    uint16_t format_version = r.get_u16b();
    return decode_color_Imag_blocks(r, width, height, format_version, *clut);
  }

  // row_bytes is always an even number, presumably because having word-aligned
  // rows is handy on a 68K machine.
  size_t effective_width = (width + 1) & (~1);

  size_t command_count = r.get_u16l();
  r.skip(1);
  if (effective_width * height != command_bytes * command_count) {
    throw runtime_error(string_printf(
        "commands (0x%zX bytes) do not cover entire image (0x%zX/0x%zX, 0x%zX bytes)",
        command_bytes * command_count, width, height, effective_width * height));
  }

  string ret_data;
  string const_table;

  // The original code kept a fixed-length array of pointers that refer back to
  // the places in the input stream where const tables were defined. Instead of
  // doing that, we keep a record of all the defined const tables separately.
  // (Memory is much more abundant now than it was in the early 1990s!)
  unordered_map<size_t, string> command_to_const_table;

  size_t current_command = 0;
  while (!r.eof() && current_command < command_count) {
    string command_data;
    bool expect_command_data = true;

    uint8_t cmd = r.get_u8();
    if (((cmd >> 6) & 3) != 3) {
      // The first few commands are similar enough that we combine the handlers
      // for them into one. These commands are:
      // 0DBBB000 NNNNNNNN NNNNNNNN <data>: Read N B-bit integers (as deltas if
      //     D=0, else as absolute values), and use them as encoded indexes to
      //     decode the result from the const table. Note that N is encoded as a
      //     little-endian 16-bit integer.
      // 0DBBB111 NNNNNNNN NNNNNNNN <data>: Decode variable-length RLE data from
      //     the input, then decode it as in the first case.
      // 0DBBBLLL NNNNNNNN NNNNNNNN <data>: Decode fixed-length RLE data from
      //     the input (with run_length=L), then decode is as in the first case.
      // 10BBBLLL <data>: Same as above 3 cases, but skip the index decoding
      //     step; use the values as direct indexes into the const table. In
      //     this case, the data size is fixed (as if N == command_bytes).

      bool is_delta = (cmd & 0xC0) == 0x00;
      bool use_direct_indexes = (cmd & 0xC0) == 0x80;
      uint8_t bits = (cmd >> 3) & 7;
      uint8_t run_length = cmd & 7;
      uint16_t index_count = use_direct_indexes ? command_bytes : r.get_u16l();

      string const_indexes;
      if (run_length == 0) {
        const_indexes = split_uniform_little_endian_bit_fields(
            r, index_count, bits, is_delta);

      } else {
        size_t rle_output_bytes = ((index_count * bits) + 7) >> 3;
        // If run_length == 7, use pattern (variable) RLE
        string decompressed = decode_rle(
            r, rle_output_bytes, (run_length == 7) ? -1 : run_length);
        StringReader decompressed_r(decompressed);
        const_indexes = split_uniform_little_endian_bit_fields(
            decompressed_r, index_count, bits, is_delta);
      }

      if (use_direct_indexes) {
        for (char const_index : const_indexes) {
          command_data.push_back(const_table.at(static_cast<uint8_t>(const_index)));
        }
      } else {
        StringReader const_indexes_r(const_indexes);
        command_data = decode_from_const_table(
            const_indexes_r, command_bytes, bits, is_delta, false, const_table);
      }

    } else {
      switch ((cmd >> 3) & 7) {
        case 0:
          // 11000--- <data>: Uncompressed section; write command_bytes bytes
          //     directly from the input.
          command_data = r.read(command_bytes);
          break;
        case 1:
          // 11001--- VVVVVVVV: Solid section; write V command_bytes times.
          command_data = string(command_bytes, r.get_s8());
          break;
        case 2:
          // 11010--- RRRRRRRR: Copy section number R (where R=0 is the first
          //     command_bytes bytes of the output) to this section.
          command_data = ret_data.substr(r.get_u8() * command_bytes, command_bytes);
          break;
        case 3:
          // 11011--- RRRRRRRR RRRRRRRR: Like the above case, but R is a 16-bit
          //     little-endian integer instead.
          command_data = ret_data.substr(r.get_u16l() * command_bytes, command_bytes);
          break;
        case 4:
          // 11100---: Duplicate the previous section.
          command_data = ret_data.substr((current_command - 1) * command_bytes, command_bytes);
          break;
        case 5: {
          // 11101LLL <data>: Decode the section data as variable-length RLE (if
          //     L = 7) or fixed-length RLE (with run_length=L).
          uint8_t run_length = cmd & 7;
          command_data = decode_rle(r, command_bytes, (run_length == 7) ? -1 : run_length);
          break;
        }

        case 6:
          // These commands modify the decoder state and do not produce any
          // output. Since many of the above commands use the const table, it's
          // common for an image to begin with a 6/0 command to set up the const
          // table before writing any sections.
          expect_command_data = false;
          switch (cmd & 7) {
            case 0:
              // 11110000 NNNNNNNN <data>: Replace const table with data (N
              //     bytes of it).
              const_table = r.read(r.get_u8());
              command_to_const_table.emplace(current_command, const_table);
              break;
            case 1:
              // 11110001 DDDDDDDD: Replace const table with the one defined D
              //     sections ago.
              const_table = command_to_const_table.at(current_command - r.get_u8());
              break;
            case 2: {
              // 11110010 NNNNNNNN DDDDDDDD: Replace const table with the one
              //     defined D sections ago, but truncate it to N entries.
              uint8_t count = r.get_u8();
              const_table = command_to_const_table.at(current_command - r.get_u8());
              if (count > const_table.size()) {
                throw runtime_error("const table memo lookup command would extend table");
              }
              const_table.resize(count);
              break;
            }
            default:
              throw runtime_error("invalid command 6/x");
          }
          break;

        default:
          throw runtime_error("invalid command 7");
      }
    }

    if (expect_command_data) {
      if (command_data.size() != command_bytes) {
        throw runtime_error("incorrect row size");
      }
      ret_data += command_data;
      current_command++;
    } else {
      if (!command_data.empty()) {
        throw runtime_error("row data produced on non-row command");
      }
    }
  }

  if (ret_data.size() != command_bytes * command_count) {
    throw logic_error("incorrect final data size");
  }

  Image ret(width, height, false);
  for (size_t y = 0; y < height; y++) {
    size_t row_start_index = y * effective_width;
    for (size_t x = 0; x < width; x++) {
      uint8_t color_index = ret_data[row_start_index + x];
      // Treat FF as black unless the clut contains an entry for it (Oregon
      // Trail appears to need this).
      // TODO: This may be wrong. Figure out the correct behavior.
      if (color_index >= clut->size() && color_index != 0xFF) {
        throw runtime_error("invalid color reference");
      } else if (color_index == 0xFF) {
        ret.write_pixel(x, y, 0x000000FF);
      } else {
        auto c = clut->at(color_index).c.as8();
        ret.write_pixel(x, y, c.r, c.g, c.b);
      }
    }
  }
  return ret;
}



string decompress_monochrome_Imag_data(StringReader& r) {
  // Decodes a fairly simple RLE-like scheme. The various commands are
  // documented in the comments below.

  StringWriter w;
  while (!r.eof()) {
    uint8_t cmd = r.get_u8();
    if (cmd & 0x80) {
      if (!(cmd & 0x40)) {
        // (80-BF) XX YY: Write (0xC0-cmd) pairs of alternating bytes XX and YY
        size_t count = 0xC0 - cmd;
        uint8_t v1 = r.get_u8();
        uint8_t v2 = r.get_u8();
        for (size_t z = 0; z < count; z++) {
          w.put_u8(v1);
          w.put_u8(v2);
        }
      } else {
        // (C0-FF) XX: Write (0x100-cmd) bytes of XX
        size_t count = 0x100 - cmd;
        uint8_t v = r.get_u8();
        for (size_t z = 0; z < count; z++) {
          w.put_u8(v);
        }
      }
    } else {
      // (00-7F) <data>: Write <data> (cmd bytes of it)
      w.write(r.read(cmd));
    }
  }
  return move(w.str());
}

Image decode_monochrome_Imag_section(StringReader& r) {
  const auto& header = r.get<BitMapHeader>();
  size_t row_bytes = header.flags_row_bytes & 0x3FFF;
  size_t width = header.bounds.width();
  size_t height = header.bounds.height();

  string decompressed = decompress_monochrome_Imag_data(r);
  if (decompressed.size() != row_bytes * height) {
    throw runtime_error(string_printf(
        "expected 0x%zX bytes, received 0x%zX bytes",
        row_bytes * height,
        decompressed.size()));
  }

  // The decompressed result is in bytewise column-major order. That is, the
  // first byte specifies the values for the 8 leftmost pixels in the top row;
  // the second byte specifies the values for the 8 pixels below those, etc.
  Image ret(width, height, false);
  StringReader decompressed_r(decompressed);
  for (size_t x = 0; x < width; x += 8) {
    for (size_t y = 0; y < height; y++) {
      uint8_t bits = decompressed_r.get_u8();
      size_t valid_bits = min<size_t>(8, width - x);
      for (size_t z = 0; z < valid_bits; z++, bits <<= 1) {
        ret.write_pixel(x + z, y, (bits & 0x80) ? 0x000000FF : 0xFFFFFFFF);
      }
    }
  }
  return ret;
}



Image decode_fraction_munchers_color_Imag_section(
    StringReader& r, const vector<ColorTableEntry>& external_clut) {
  const vector<ColorTableEntry>* clut = &external_clut;
  vector<ColorTableEntry> internal_clut;

  const auto& header = r.get<PixelMapHeader>();
  size_t row_bytes = header.flags_row_bytes & 0x3FFF;
  size_t width = header.bounds.width();
  size_t height = header.bounds.height();

  if (header.color_table_offset != 0xFFFFFFFF) {
    r.skip(6);
    size_t color_count = r.get_u16b() + 1;
    while (internal_clut.size() < color_count) {
      internal_clut.emplace_back(r.get<ColorTableEntry>());
    }
    clut = &internal_clut;
  }

  string decompressed = decompress_monochrome_Imag_data(r);
  if (decompressed.size() != row_bytes * height) {
    throw runtime_error(string_printf(
        "expected 0x%zX bytes, received 0x%zX bytes",
        (header.flags_row_bytes & 0x3FFF) * height,
        decompressed.size()));
  }

  // Like in monochrome Imag decoding, the resulting data is in column-major
  // order, hence the odd-looking index expression here.
  Image ret(width, height, false);
  for (size_t y = 0; y < height; y++) {
    for (size_t x = 0; x < width; x++) {
      uint8_t color_index = decompressed[height * x + y];
      auto c = clut->at(color_index).c.as8();
      ret.write_pixel(x, y, c.r, c.g, c.b);
    }
  }
  return ret;
}



vector<Image> decode_Imag(
    const string& data,
    const vector<ColorTableEntry>& clut,
    bool use_later_formats) {
  StringReader r(data);
  vector<Image> ret;
  size_t count = r.get_u16b();
  while (ret.size() < count) {
    size_t section_start_offset = r.where();
    size_t section_end_offset = section_start_offset + r.get_u32b();
    // This field is probably completely unused - it's likely the result of MECC
    // using the BitMap and PixMap structs directly in the resource format,
    // which include pointers to the decompressed data when loaded in memory.
    // These fields are unused in files and resources. We don't have these
    // fields in the BitMapHeader and PixMapHeader structs here in
    // resource_dasm, so we have to skip the field manually here.
    r.skip(4);
    // Hack: If this is the last section, ignore the end offset and just use the
    // rest of the data. This is needed because some Imag resources have
    // incorrect values in the frame header when only one image is present.
    StringReader section_r = (ret.size() == count - 1)
        ? r.sub(r.where())
        : r.sub(r.where(), section_end_offset - r.where());
    r.go(section_end_offset);

    // As in many QuickDraw-compatible formats, the high bit of flags_row_bytes
    // specifies whether the image is color or monochrome.
    if (section_r.get_u8(false) & 0x80) {
      if (use_later_formats) {
        ret.emplace_back(decode_color_Imag_commands(section_r, clut));
      } else {
        ret.emplace_back(decode_fraction_munchers_color_Imag_section(section_r, clut));
      }
    } else {
      ret.emplace_back(decode_monochrome_Imag_section(section_r));
    }
  }
  return ret;
}
