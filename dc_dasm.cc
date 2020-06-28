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

#include "resource_fork.hh"
#include "dc_decode_sprite.hh"

using namespace std;



struct resource_header {
  uint32_t unknown1;
  uint16_t resource_count;
  uint16_t unknown2[2];

  void byteswap() {
    this->resource_count = bswap16(this->resource_count);
  }
} __attribute__((packed));

struct resource_entry {
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

vector<resource_entry> load_index(FILE* f) {
  resource_header h;
  fread(&h, sizeof(resource_header), 1, f);
  h.byteswap();

  vector<resource_entry> e(h.resource_count);
  fread(e.data(), sizeof(resource_entry), h.resource_count, f);

  for (auto& it : e)
    it.byteswap();

  return e;
}

string get_resource_data(FILE* f, const resource_entry& e) {
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

  vector<resource_entry> resources = load_index(f);

  for (const auto& it : resources) {
    string filename_prefix = string_printf("%s/%s_%.4s_%hd",
        output_directory, base_filename.c_str(),
        reinterpret_cast<const char*>(&it.type), it.id);
    try {
      string data = get_resource_data(f, it);
      if (bswap32(it.type) == RESOURCE_TYPE_snd) {
        SingleResourceFile srf(RESOURCE_TYPE_snd, 0, data);
        save_file(filename_prefix + ".wav", srf.decode_snd(0));
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
