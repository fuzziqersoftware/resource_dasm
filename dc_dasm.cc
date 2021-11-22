#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <exception>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <vector>

#include "ResourceFile.hh"

using namespace std;



struct InputFormat {
  int16_t height;
  int16_t width;
  uint8_t bits_per_pixel; // Actually bits per pixel - 1, but whatever
  uint8_t unknown[2];
  uint8_t generate_transparency_map;
  uint8_t data[0];
};

void generate_transparency_map(size_t count, void* data) {
  // Note: this function is unused in this implementation (see the comment at
  // the callsite below)
  uint32_t* src_ptr = reinterpret_cast<uint32_t*>(data) + (count / 4);
  uint32_t* dst_ptr = src_ptr + (count / 4);
  while (src_ptr != data) {
    uint32_t values = bswap32(*(src_ptr--));
    uint32_t t_values = 0;
    if ((values & 0xFF) == 0) {
      t_values = 0x000000FF;
    }
    if (((values >> 8) & 0xFF) == 0) {
      t_values |= 0x0000FF00;
    }
    if (((values >> 16) & 0xFF) == 0) {
      t_values |= 0x00FF0000;
    }
    if (((values >> 24) & 0xFF) == 0) {
      t_values |= 0xFF000000;
    }

    *(dst_ptr--) = bswap32(t_values);
    *(dst_ptr--) = bswap32(values);
  }
}

uint32_t get_bits_at_offset(const void* data, size_t bit_offset, size_t count) { // fn658
  // Note: the original implementation has two special cases of this function
  // where count=1 and count=2 respectively

  // Note: the original implementation is ((offset >> 4) << 1); this should be
  // equivalent. The right shift is signed in the original implementation, but
  // args to this function always appear to be positive so it shouldn't matter.
  const uint8_t* u8_data = reinterpret_cast<const uint8_t*>(data);
  size_t byte_offset = (bit_offset >> 3) & 0xFFFFFFFE;
  uint32_t value = bswap32(*reinterpret_cast<const uint32_t*>(u8_data + byte_offset)) << (bit_offset & 0x0F);
  return (value >> (32 - count));
}


void decode_dc2_sprite(const void* input_data, void* output_data) {
  // Not part of the original implementation; added to improve readability.
  const InputFormat* input = reinterpret_cast<const InputFormat*>(input_data);

  // The original implementation called this function and then didn't appear to
  // use the result at all. We don't have this on modern systems because it's a
  // relic of ancient times, but fortunately we apparently don't need it.
  // InterfaceLib::Gestalt('cput', &var56); // TOC entry at offset 4

  // Note: the original code appears to have a missing bounds check here. It
  // uses a small table to look up max_color instead of doing a shift like this,
  // so if input->bits_per_pixel is more than 7, it would read invalid data.
  uint8_t max_color = 1 << input->bits_per_pixel;
  const void* input_bitstream = &input->data[(max_color - 2) << 1];

  // TODO: make the code for the following computation not look dumb as hell
  uint8_t chunk_count_bits;
  {
    uint8_t max_chunk_count;
    for (chunk_count_bits = 7, max_chunk_count = 0x80;
         (chunk_count_bits > 3) && (max_chunk_count >= bswap16(input->width));
         chunk_count_bits--, max_chunk_count >>= 1);
  }

  // Start reading the bit stream and executing its commands
  uint8_t* output_ptr = reinterpret_cast<uint8_t*>(output_data);
  size_t output_count = bswap16(input->height) * bswap16(input->width);
  uint8_t transparent_color = max_color - 1;
  size_t input_bitstream_offset = 0;
  ssize_t output_count_remaining = output_count;
  while (output_count_remaining > 0) {

    uint8_t opcode = get_bits_at_offset(input_bitstream, input_bitstream_offset, 3);
    input_bitstream_offset += 3;

    size_t chunk_count;
    switch (opcode) {
      case 0: // label228
        chunk_count = get_bits_at_offset(input_bitstream, input_bitstream_offset, chunk_count_bits);
        input_bitstream_offset += chunk_count_bits;

        // Write chunk_count + 1 zeroes to output
        for (size_t x = 0; x < chunk_count + 1; x++) {
          (*output_ptr++) = 0;
        }

        break;

      case 1: { // label26C
        chunk_count = get_bits_at_offset(input_bitstream, input_bitstream_offset, chunk_count_bits);
        input_bitstream_offset += chunk_count_bits;
        uint8_t color = get_bits_at_offset(input_bitstream, input_bitstream_offset, input->bits_per_pixel);
        input_bitstream_offset += input->bits_per_pixel;

        if (color == transparent_color) {
          color = 0xFF;
        }

        // Write chunk_count + 1 copies of r3 to output
        for (size_t x = 0; x < chunk_count + 1; x++) {
          (*output_ptr++) = color;
        }

        break;
      }

      case 2: { // label2D4
        chunk_count = get_bits_at_offset(input_bitstream, input_bitstream_offset, chunk_count_bits);
        input_bitstream_offset += chunk_count_bits;

        uint8_t values[2];
        values[0] = get_bits_at_offset(input_bitstream, input_bitstream_offset, input->bits_per_pixel);
        input_bitstream_offset += input->bits_per_pixel;
        if (values[0] == transparent_color) {
          values[0] = 0xFF;
        }

        values[1] = get_bits_at_offset(input_bitstream, input_bitstream_offset, input->bits_per_pixel);
        input_bitstream_offset += input->bits_per_pixel;
        if (values[1] == transparent_color) {
          values[1] = 0xFF;
        }

        // Write first color followed by a bitstream-determined alternation of
        // the two colors. Note that we write exactly the count instead of
        // count + 1, presumably because the first color is always written to
        // save 1 bit. Nice hyper-optimization, Delta Tao. Was it worth it?
        *(output_ptr++) = values[0];
        for (size_t x = 1; x < chunk_count + 1; x++) {
          bool which = get_bits_at_offset(input_bitstream, input_bitstream_offset, 1);
          input_bitstream_offset++;
          *(output_ptr++) = values[which];
        }

        break;
      }

      case 3: { // label3A0
        chunk_count = get_bits_at_offset(input_bitstream, input_bitstream_offset, chunk_count_bits);
        input_bitstream_offset += chunk_count_bits;

        uint8_t values[4];

        values[0] = get_bits_at_offset(input_bitstream, input_bitstream_offset, input->bits_per_pixel);
        input_bitstream_offset += input->bits_per_pixel;
        if (values[0] == transparent_color) {
          values[0] = 0xFF;
        }

        values[1] = get_bits_at_offset(input_bitstream, input_bitstream_offset, input->bits_per_pixel);
        input_bitstream_offset += input->bits_per_pixel;
        if (values[1] == transparent_color) {
          values[1] = 0xFF;
        }

        values[2] = get_bits_at_offset(input_bitstream, input_bitstream_offset, input->bits_per_pixel);
        input_bitstream_offset += input->bits_per_pixel;
        if (values[2] == transparent_color) {
          values[2] = 0xFF;
        }

        values[3] = get_bits_at_offset(input_bitstream, input_bitstream_offset, input->bits_per_pixel);
        input_bitstream_offset += input->bits_per_pixel;
        if (values[3] == transparent_color) {
          values[3] = 0xFF;
        }

        // Similar to opcode 2 (above), but 4 possible values instead of 2
        *(output_ptr++) = values[0];
        for (size_t x = 1; x < chunk_count + 1; x++) {
          uint8_t which = get_bits_at_offset(input_bitstream, input_bitstream_offset, 2);
          input_bitstream_offset += 2;
          *(output_ptr++) = values[which];
        }

        break;
      }

      default: // 4, 5, 6, or 7. label4E4
        // Opcodes 4, 5, and 6 write 1, 2, or 3 colors directly from the
        // bitstream. Opcode 7 writes a variable number of colors directly from
        // the bitstream.
        if (opcode == 7) {
          chunk_count = get_bits_at_offset(input_bitstream, input_bitstream_offset, chunk_count_bits);
          input_bitstream_offset += chunk_count_bits;
        } else {
          chunk_count = opcode - 4;
        }

        // Copy chunk_count + 1 items from the input bitstream to the output
        for (size_t x = 0; x < chunk_count + 1; x++) {
          uint8_t value = get_bits_at_offset(input_bitstream, input_bitstream_offset, input->bits_per_pixel);
          input_bitstream_offset += input->bits_per_pixel;

          if (value == transparent_color) {
            value = 0xFF;
          }
          *(output_ptr++) = value;
        }
    }

    output_count_remaining -= (chunk_count + 1);
  }
  if (output_count_remaining < 0) {
    // Note: the original implementation logged this string and then returned
    // anyway, even though it probably caused memory corruption because it
    // overstepped the bounds of the output buffer. We also cause memory
    // corruption on this kind of failure because I'm lazy.
    // InterfaceLib::DebugStr("Uh-Oh. too many pixels."); // TOC entry at offset 0
    throw runtime_error("Uh-Oh. too many pixels.");
  }

  // The original code does this, but we don't because it just makes the output
  // harder to parse. Probably they did this for some draw-time optimizations.
  // if (input->generate_transparency_map) {
  //   generate_transparency_map(output_count, output_data);
  // }
}

Image decode_dc2_sprite(const void* input_data, size_t size) {
  // Not part of the original; added to improve readability
  const InputFormat* input = reinterpret_cast<const InputFormat*>(input_data);
  int16_t h = bswap16(input->height);
  int16_t w = bswap16(input->width);
  size_t output_size = h * w * (input->generate_transparency_map ? 2 : 1);

  string output_data(output_size, '\0');
  decode_dc2_sprite(input_data, const_cast<char*>(output_data.data()));

  const int16_t* colors = reinterpret_cast<const int16_t*>(&input->data[0]);

  // Convert the colors into 24-bit rgb and a transparency mask
  Image ret(w, h, true);
  for (ssize_t y = 0; y < h; y++) {
    for (ssize_t x = 0; x < w; x++) {
      uint8_t color_index = output_data[y * w + x];
      if (color_index == 0) {
        ret.write_pixel(x, y, 0x00, 0x00, 0x00, 0x00);
      } else if (color_index == 0xFF) {
        ret.write_pixel(x, y, 0x00, 0x00, 0x00, 0xFF);
      } else {
        // Guess: it's rgb565
        int16_t color = bswap16(colors[color_index - 1]);
        uint8_t r = (((color >> 10) & 0x1F) * 0xFF) / 0x1F;
        uint8_t g = (((color >> 5) & 0x1F) * 0xFF) / 0x1F;
        uint8_t b = (((color >> 0) & 0x1F) * 0xFF) / 0x1F;
        ret.write_pixel(x, y, r, g, b, 0xFF);
      }
    }
  }

  return ret;
}



struct ResourceHeader {
  uint32_t unknown1;
  uint16_t resource_count;
  uint16_t unknown2[2];

  void byteswap() {
    this->resource_count = bswap16(this->resource_count);
  }
} __attribute__((packed));

struct ResourceEntry {
  uint32_t offset;
  uint32_t size;
  uint32_t type;
  int16_t id;

  void byteswap() {
    this->id = bswap16(this->id);
    this->offset = bswap32(this->offset);
    this->size = bswap32(this->size);
  }
} __attribute__((packed));

vector<ResourceEntry> load_index(FILE* f) {
  ResourceHeader h;
  fread(&h, sizeof(ResourceHeader), 1, f);
  h.byteswap();

  vector<ResourceEntry> e(h.resource_count);
  fread(e.data(), sizeof(ResourceEntry), h.resource_count, f);

  for (auto& it : e)
    it.byteswap();

  return e;
}

string get_resource_data(FILE* f, const ResourceEntry& e) {
  fseek(f, e.offset, SEEK_SET);
  return freadx(f, e.size);
}



int main(int argc, char* argv[]) {
  printf("fuzziqer software dark castle resource disassembler\n\n");

  const char* filename = NULL;
  const char* output_directory = NULL;
  for (int x = 1; x < argc; x++) {
    if (filename == NULL) {
      filename = argv[x];
    } else if (output_directory == NULL) {
      output_directory = argv[x];
    } else {
      throw runtime_error("excess command-line argument");
    }
  }
  if (filename == NULL) {
    filename = "DC Data";
  }
  if (output_directory == NULL) {
    output_directory = ".";
  }

  string base_filename = split(filename, '/').back();

  FILE* f = fopen(filename, "rb");
  if (!f) {
    printf("%s is missing\n", filename);
    return 1;
  }

  vector<ResourceEntry> resources = load_index(f);

  for (const auto& it : resources) {
    string filename_prefix = string_printf("%s/%s_%.4s_%hd",
        output_directory, base_filename.c_str(),
        reinterpret_cast<const char*>(&it.type), it.id);
    try {
      string data = get_resource_data(f, it);
      if (bswap32(it.type) == RESOURCE_TYPE_snd) {
        save_file(filename_prefix + ".wav",
            ResourceFile::decode_snd(data.data(), data.size()));
        printf("... %s.wav\n", filename_prefix.c_str());

      } else if (it.type == 0x52545343) { // 'CSTR'
        if ((data.size() > 0) && (data[data.size() - 1] == 0)) {
          data.resize(data.size() - 1);
        }
        save_file(filename_prefix + ".txt", data);
        printf("... %s.txt\n", filename_prefix.c_str());

      } else if (it.type == 0x20324344) { // 'DC2 '
        try {
          Image decoded = decode_dc2_sprite(data.data(), data.size());

          auto filename = filename_prefix + ".bmp";
          decoded.save(filename.c_str(), Image::ImageFormat::WindowsBitmap);
          printf("... %s.bmp\n", filename_prefix.c_str());

        } catch (const runtime_error& e) {
          fprintf(stderr, "failed to decode DC2 %hd: %s\n", it.id, e.what());
          save_file(filename_prefix + ".bin", data);
          printf("... %s.bin\n", filename_prefix.c_str());
        }

      } else {
        save_file(filename_prefix + ".bin", data);
        printf("... %s.bin\n", filename_prefix.c_str());
      }

    } catch (const runtime_error& e) {
      printf("... %s (FAILED: %s)\n", filename_prefix.c_str(), e.what());
    }
  }

  fclose(f);
  return 0;
}
