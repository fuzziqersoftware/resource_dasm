#include "Codecs.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

using namespace std;



string unpack_bits(const void* data, size_t size) {
  StringReader r(data, size);
  StringWriter w;

  // Commands:
  // 0CCCCCCC <data> - write data (1 + C bytes of it) directly from the input
  // 1CCCCCCC DDDDDDDD - write (1 - C) bytes of D (C treated as negative number)
  // 10000000 - no-op (for some reason)
  while (!r.eof()) {
    int8_t cmd = r.get_s8();
    if (cmd == -128) {
      continue;
    } else if (cmd < 0) {
      uint8_t v = r.get_u8();
      size_t count = 1 - cmd;
      for (size_t z = 0; z < count; z++) {
        w.put_u8(v);
      }
    } else {
      size_t count = 1 + cmd;
      for (size_t z = 0; z < count; z++) {
        w.put_u8(r.get_u8());
      }
    }
  }

  return move(w.str());
}

string unpack_bits(const string& data) {
  return unpack_bits(data.data(), data.size());
}

void unpack_bits(StringReader& in, void* uncompressed_data, uint32_t uncompressed_size) {
  uint8_t*        out = static_cast<uint8_t*>(uncompressed_data);
  uint8_t* const  out_end = out + uncompressed_size;
  while (out < out_end) {
    int8_t len = in.get_s8();
    if (len == -128) {
      // For backwards compatibility, says QuickDraw 1.0
      continue;
    }
    if (len < 0) {
      // -len+1 repetitions of the next byte
      uint8_t byte = in.get_u8();
      for (int i = 0; (i < -len + 1) && (out < out_end); ++i) {
        *out++ = byte;
      }
    } else {
      // len + 1 raw bytes
      size_t to_read = min<size_t>(out_end - out, len + 1);
      in.readx(out, to_read);
      out += to_read;
    }
  }
}


string pack_bits(const void* data, size_t size) {
  // See unpack_bits (above) for descriptions of the commands.
  StringReader r(data, size);
  StringWriter w;

  size_t run_start_offset;
  while (!r.eof()) {
    uint8_t ch = r.get_u8();
    if (r.eof()) {
      // Only one byte left in the input; just write it verbatim
      w.put_u8(0x00);
      w.put_u8(ch);
      break;
    }

    run_start_offset = r.where() - 1;

    if (r.get_u8() == ch) { // Run of same byte
      while (((r.where() - run_start_offset) < 128) &&
             !r.eof() &&
             (r.get_u8(false) == ch)) {
        r.skip(1);
      }
      w.put_u8(1 - (r.where() - run_start_offset));
      w.put_u8(ch);

    } else { // Run of different bytes
      while (((r.where() - run_start_offset) < 128) &&
             !r.eof() &&
             (r.get_u8(false) != ch)) {
        ch = r.get_u8();
      }
      w.put_u8(r.where() - run_start_offset - 1);
      w.write(r.pread(run_start_offset, r.where() - run_start_offset));
    }
  }

  return move(w.str());
}

string pack_bits(const string& data) {
  return pack_bits(data.data(), data.size());
}



string decompress_packed_icns_data(const void* data, size_t size) {
  StringWriter w;
  StringReader r(data, size);
  while (!r.eof()) {
    uint16_t cmd = r.get_u8();
    if (cmd < 0x80) {
      // 00-7F: Write (cmd + 1) bytes directly from the input
      w.write(r.getv(cmd + 1), cmd + 1);
    } else {
      // 80-FF VV: Write (cmd - 0x80 + 3) bytes of VV
      size_t target_size = w.size() + (cmd - 0x80 + 3);
      uint8_t v = r.get_u8();
      while (w.size() < target_size) {
        w.put_u8(v);
      }
    }
  }
  return move(w.str());
}

string decompress_packed_icns_data(const string& data) {
  return decompress_packed_icns_data(data.data(), data.size());
}


uint32_t compress_strided_icns_data(StringWriter& out, const void* uncompressed_data, uint32_t uncompressed_size, uint32_t uncompressed_stride) {
  // Reverse of the following decompression pseudo-code:
  //
  //  if bit 8 of the byte is set (byte >= 128, signed_byte < 0):
  //    This is a compressed run, for some value (next byte).
  //    The length is byte - 125.
  //    Put so many copies of the byte in the current color channel.
  //  else:
  //    This is an uncompressed run, whose values follow.
  //    The length is byte +1.
  //    Read the bytes and put them in the current color channel.
  //
  // From: https://www.macdisk.com/maciconen.php#RLE
  //
  const uint8_t*  in = static_cast<const uint8_t*>(uncompressed_data);
  const uint8_t*  in_end = static_cast<const uint8_t*>(uncompressed_data) + uncompressed_size;
  uint32_t        in_stride = uncompressed_stride;
  
  uint32_t        old_out_size = out.size();
  while (in < in_end) {
    if (in + 2 * in_stride < in_end && in[0] == in[in_stride] && in[0] == in[2 * in_stride]) {
      // At least three identical bytes
      uint32_t count = 3;
      while (count < 130 && in + count * in_stride < in_end && in[count * in_stride] == in[0])
        ++count;
      
      out.put_u8(count + 128 - 3);
      out.put_u8(in[0]);
      in += count * in_stride;
    } else { 
      uint32_t count = 1;
      while (count < 128 && in + count * in_stride < in_end && in[count * in_stride] != in[(count - 1) * in_stride])
        ++count;
      
      out.put_u8(count - 1);
      for (uint32_t c = count; c > 0; --c) {
        out.put_u8(*in);
        in += in_stride;
      }
    }
  }
  return out.size() - old_out_size;
}
