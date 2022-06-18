#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

using namespace std;



string decompress_flashback_lzss(const string& data) {
  StringReader r(data);
  size_t decompressed_size = r.get_u32b();

  StringWriter w;
  while (w.size() < decompressed_size) {
    uint8_t control_bits = r.get_u8();
    for (size_t x = 0; (x < 8) && (w.size() < decompressed_size); x++) {
      bool is_backreference = control_bits & 1;
      control_bits >>= 1;
      if (is_backreference) {
        uint16_t args = r.get_u16b();
        size_t offset = w.size() - ((args & 0x0FFF) + 1);
        size_t count = ((args >> 12) & 0x000F) + 3;
        fprintf(stderr, "[%zX => %zX/%zX] backreference %04hX (offset=%zu size=%zu)\n",
            r.where() - 2, w.str().size(), decompressed_size, args, offset, count);
        for (size_t x = 0; x < count; x++) {
          w.put_u8(w.str().at(offset++));
        }
      } else {
        uint8_t v = r.get_u8();
        fprintf(stderr, "[%zX => %zX/%zX] literal %02hhX\n",
            r.where() - 1, w.str().size(), decompressed_size, v);
        w.put_u8(v);
      }
    }
  }

  return w.str();
}

int main(int argc, char** argv) {
  if (argc < 1 || argc > 3) {
    fprintf(stderr, "\
Usage: flashback_decomp [input_filename [output_filename]]\n\
\n\
If input_filename is omitted or is '-', read from stdin.\n\
If output_filename is omitted, write to stdout.\n\
");
    return 2;
  }

  const char* input_filename = (argc > 1) ? argv[1] : nullptr;
  const char* output_filename = (argc > 2) ? argv[2] : nullptr;

  string input_data;
  if (!input_filename || !strcmp(input_filename, "-")) {
    input_data = read_all(stdin);
  } else {
    input_data = load_file(input_filename);
  }

  string data_dec = decompress_flashback_lzss(input_data);

  if (output_filename) {
    save_file(output_filename, data_dec);
  } else {
    fwritex(stdout, data_dec);
  }

  return 0;
}
