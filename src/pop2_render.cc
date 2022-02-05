#include <stdio.h>
#include <string.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <string>

#include "QuickDrawFormats.hh"

using namespace std;



struct SHAPHeader {
  enum Flags {
    RowRLECompressed = 0x100,
    RLECompressed = 0x200, // Use decode_SHAP_RLE
    LZCompressed = 0x400, // Use decode_SHAP_LZ (before RLE, if both are used)
  };
  uint16_t flags;
  int16_t width;
  int16_t row_bytes;
  int16_t height;
  uint32_t unknown2;
  uint8_t data[0];

  void byteswap() {
    this->flags = bswap16(this->flags);
    this->width = bswap16(this->width);
    this->row_bytes = bswap16(this->row_bytes);
    this->height = bswap16(this->height);
  }
} __attribute__((packed));

string decode_lz(const string& data) {
  StringReader r(data);
  size_t decompressed_size = r.get_u32r() - 0x0C;

  StringWriter w;
  w.str().reserve(decompressed_size);

  // TODO: The original code allocates 0x442 bytes. Are those extra 0x42 bytes
  // important? Seems like the code never uses them.
  u8string dict(0x400, '\0');
  uint16_t dict_offset = 0x3BE;

  uint16_t control_bits = 0; // w
  while (w.str().size() < decompressed_size) {
    control_bits >>= 1;
    if ((control_bits & 0x100) == 0) {
      control_bits = r.get_u8() | 0xFF00;
    }
    if (control_bits & 1) { // Direct byte
      uint8_t v = r.get_u8();
      w.put_u8(v);
      dict[dict_offset] = v;
      dict_offset = (dict_offset + 1) & 0x3FF;

    } else { // Backreference
      // Spec bits are ssssssii iiiiiiii (size x 6, start_index x 10)
      uint16_t spec = r.get_u16r();
      uint16_t offset = spec & 0x3FF;
      size_t count = ((spec >> 10) & 0x3F) + 3;
      for (size_t z = 0; (z < count) && (w.str().size() < decompressed_size); z++) {
        uint8_t v = dict[((offset + z) & 0x3FF)];
        w.put_u8(v);
        dict[dict_offset] = v;
        dict_offset = (dict_offset + 1) & 0x3FF;
      }
    }
  }

  return w.str();
}

string decode_standard_rle(const string& data) {
  StringReader r(data);
  StringWriter w;

  while (!r.eof()) {
    uint8_t count = r.get_u8();
    if (count & 0x80) {
      count = (count & 0x7F) + 3;
      uint8_t value = r.get_u8();
      for (uint8_t z = 0; z < count; z++) {
        w.put_u8(value);
      }
    } else {
      w.write(r.read(count));
    }
  }
  return w.str();
}

string decode_rows_rle(const string& data, size_t num_rows, size_t row_bytes) {
  StringReader r(data);
  StringWriter w;

  for (size_t x = 0; x < num_rows; x++) {
    uint16_t bytes = r.get_u16r();
    StringReader row_r = r.sub(r.where(), bytes);
    r.skip(bytes);

    size_t size_before_row = w.str().size();
    while (!row_r.eof()) {
      uint8_t count = row_r.get_u8();
      if (count & 0x80) {
        count = (count & 0x7F) + 1;
        uint8_t v = row_r.get_u8();
        for (uint8_t x = 0; x < count; x++) {
          w.put_u8(v);
        }
      } else {
        w.write(row_r.read(count + 1));
      }
    }
    size_t size_after_row = w.str().size();
    if (size_after_row - size_before_row != row_bytes) {
      throw runtime_error("incorrect result row length");
    }
  }

  return w.str();
}

int main(int argc, char** argv) {

  const char* shap_filename = nullptr;
  const char* ctbl_filename = nullptr;
  const char* out_filename = nullptr;
  for (int x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "--shap=", 7)) {
      shap_filename = &argv[x][7];
    } else if (!strncmp(argv[x], "--ctbl=", 7)) {
      ctbl_filename = &argv[x][7];
    } else if (!strncmp(argv[x], "--output=", 9)) {
      out_filename = &argv[x][9];
    } else {
      throw invalid_argument("unknown option: " + string(argv[x]));
    }
  }

  if (!shap_filename) {
    fprintf(stderr, "Usage: pop2_render --shap=SHAP.bin [--ctbl=CTBL.bin] [--output=file.bmp]\n");
    return 1;
  }

  string shap_contents = load_file(shap_filename);
  StringReader r(shap_contents);

  auto header = r.get_sw<SHAPHeader>();
  string data = r.read(r.remaining());

  fprintf(stderr,
      "flags=%04hX width=%04hX row_bytes=%04hX height=%04hX in_bytes=%zX",
      header.flags,
      header.width,
      header.row_bytes,
      header.height,
      data.size());

  uint8_t compression_type = (header.flags & 0x0F00) >> 8;
  size_t row_bytes = header.width;

  if (compression_type & 4) {
    fprintf(stderr, " (decode_lz %zX->", data.size());
    data = decode_lz(data);
    fprintf(stderr, "%zX)", data.size());
  }

  if (compression_type & 2) {
    fprintf(stderr, " (decode_standard_rle %zX->", data.size());
    data = decode_standard_rle(data);
    fprintf(stderr, "%zX)", data.size());
  }

  if (compression_type & 1) {
    fprintf(stderr, " (decode_rows_rle %zX->", data.size());
    data = decode_rows_rle(data, header.height, header.row_bytes);
    fprintf(stderr, "%zX)", data.size());

    // For this compression type, the actual image width is the row_bytes field,
    // not the width field. (Why did they do this...?)
    row_bytes = header.row_bytes;
  }

  size_t area_bytes = row_bytes * header.height;
  if (data.size() != area_bytes) {
    fprintf(stderr, " INCORRECT SIZE (expected %zX)\n", area_bytes);
    throw runtime_error("incorrect data size after decompression");
  }

  // If a CTBL is given, parse it
  unordered_map<uint8_t, Color8> ctbl;
  if (ctbl_filename) {
    string ctbl_contents = load_file(ctbl_filename);
    StringReader r(ctbl_contents);
    uint16_t num_colors = r.get_u16r();
    for (size_t z = 0; z < num_colors; z++) {
      uint8_t r8 = r.get_u8();
      uint8_t g8 = r.get_u8();
      uint8_t b8 = r.get_u8();
      uint8_t color_id = r.get_u8();
      ctbl.emplace(color_id, Color8(r8, g8, b8));
    }
  }

  Image result(row_bytes, header.height, true);
  for (size_t y = 0; y < static_cast<size_t>(header.height); y++) {
    for (size_t x = 0; x < row_bytes; x++) {
      uint8_t v = data.at(y * row_bytes + x);
      if (v == 0) {
        result.write_pixel(x, y, 0x00000000);
      } else {
        // If there's no CTBL, just write the index as a grayscale value
        if (ctbl.empty()) {
          result.write_pixel(x, y, v, v, v, 0xFF);
        } else {
          try {
            const Color8& c = ctbl.at(v);
            result.write_pixel(x, y, c.r, c.g, c.b, 0xFF);
          } catch (const out_of_range&) {
            result.write_pixel(x, y, 0xFFFFFFFF);
          }
        }
      }
    }
  }

  string save_filename = out_filename
      ? out_filename : string_printf("%s.bmp", shap_filename);
  result.save(save_filename, Image::ImageFormat::WindowsBitmap);
  fprintf(stderr, " => %s\n", save_filename.c_str());

  return 0;
}
