#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <exception>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <vector>
#include <string>

#include "resource_fork.hh"

using namespace std;



static uint16_t byteswap16(uint16_t a) {
  return ((a >> 8) & 0x00FF) | ((a << 8) & 0xFF00);
}

static uint32_t byteswap32(uint32_t a) {
  return ((a >> 24) & 0x000000FF) |
         ((a >> 8)  & 0x0000FF00) |
         ((a << 8)  & 0x00FF0000) |
         ((a << 24) & 0xFF000000);
}



////////////////////////////////////////////////////////////////////////////////
// resource fork parsing

#pragma pack(push)
#pragma pack(1)

struct resource_fork_header {
  uint32_t resource_data_offset;
  uint32_t resource_map_offset;
  uint32_t resource_data_size;
  uint32_t resource_map_size;

  void byteswap() {
    this->resource_data_offset = byteswap32(this->resource_data_offset);
    this->resource_map_offset = byteswap32(this->resource_map_offset);
    this->resource_data_size = byteswap32(this->resource_data_size);
    this->resource_map_size = byteswap32(this->resource_map_size);
  }

  resource_fork_header(FILE* f) {
    fread(this, sizeof(*this), 1, f);
    this->byteswap();
  }
};

struct resource_data {
  uint32_t size;
  uint8_t* data;

  void byteswap() {
    this->size = byteswap32(this->size);
  }

  resource_data(FILE* f) {
    fread(&this->size, sizeof(this->size), 1, f);
    this->byteswap();
    this->data = new uint8_t[this->size];
    fread(this->data, this->size, 1, f);
  }

  ~resource_data() {
    delete[] this->data;
  }
};

struct resource_map_header {
  uint8_t reserved[16];
  uint32_t reserved_handle;
  uint16_t reserved_file_ref_num;
  uint16_t attributes;
  uint16_t resource_type_list_offset; // relative to start of this struct
  uint16_t resource_name_list_offset; // relative to start of this struct

  void byteswap() {
    this->attributes = byteswap16(this->attributes);
    this->resource_type_list_offset = byteswap16(this->resource_type_list_offset);
    this->resource_name_list_offset = byteswap16(this->resource_name_list_offset);
  }

  resource_map_header(FILE* f) {
    fread(this, sizeof(*this), 1, f);
    this->byteswap();
  }
};

struct resource_type_list_entry {
  uint32_t resource_type;
  uint16_t num_items; // actually num_items - 1
  uint16_t reference_list_offset; // relative to start of type list

  void byteswap() {
    this->resource_type = byteswap32(this->resource_type);
    this->num_items = byteswap16(this->num_items);
    this->reference_list_offset = byteswap16(this->reference_list_offset);
  }

  resource_type_list_entry(FILE* f) {
    fread(this, sizeof(*this), 1, f);
    this->byteswap();
  }
};

struct resource_type_list {
  uint16_t num_types; // actually num_types - 1
  vector<resource_type_list_entry> entries;

  void byteswap() {
    this->num_types = byteswap16(this->num_types);
  }

  resource_type_list(FILE* f) {
    fread(&this->num_types, sizeof(this->num_types), 1, f);
    this->byteswap();
    for (int i = 0; i <= this->num_types; i++)
      this->entries.emplace_back(f);
  }
};

struct resource_reference_list_entry {
  int16_t resource_id;
  uint16_t name_offset;
  uint32_t attributes_and_offset; // attr = high 8 bits; offset relative to resource data segment start
  uint32_t reserved;

  void byteswap() {
    this->resource_id = (int16_t)byteswap16((uint16_t)this->resource_id);
    this->name_offset = byteswap16(this->name_offset);
    this->attributes_and_offset = byteswap32(this->attributes_and_offset);
  }

  resource_reference_list_entry(FILE* f) {
    fread(this, sizeof(*this), 1, f);
    this->byteswap();
  }
};

#pragma pack(pop)



void load_resource_from_file(const char* filename, uint32_t resource_type,
    int16_t resource_id, void** data, size_t* size) {

  *data = NULL;
  *size = 0;

  FILE* f = fopen(filename, "rb");
  if (!f)
    throw runtime_error("file not found");

  // load overall header
  resource_fork_header header(f);

  // load resource map header
  fseek(f, header.resource_map_offset, SEEK_SET);
  resource_map_header map_header(f);

  // look in resource type map for a matching type
  fseek(f, map_header.resource_type_list_offset + header.resource_map_offset, SEEK_SET);
  resource_type_list map_type_list(f);

  const resource_type_list_entry* type_list = NULL;
  for (const auto& entry : map_type_list.entries)
    if (entry.resource_type == resource_type)
      type_list = &entry;

  if (!type_list) {
    fclose(f);
    throw runtime_error("file doesn\'t contain resources of the given type");
  }

  // look in resource list for something with the given ID
  fseek(f, map_header.resource_type_list_offset + header.resource_map_offset + type_list->reference_list_offset, SEEK_SET);
  int x;
  for (x = 0; x <= type_list->num_items; x++) {
    resource_reference_list_entry e(f);
    if (e.resource_id != resource_id)
      continue;

    // yay we found it! now read the thing
    fseek(f, header.resource_data_offset + (e.attributes_and_offset & 0x00FFFFFF), SEEK_SET);
    resource_data d(f);
    *data = malloc(d.size);
    memcpy(*data, d.data, d.size);
    *size = d.size;
    break;
  }

  fclose(f);

  if (x > type_list->num_items)
    throw runtime_error("file doesn\'t contain resource with the given id");
}

vector<pair<uint32_t, int16_t>> enum_file_resources(const char* filename) {

  vector<pair<uint32_t, int16_t>> all_resources;

  FILE* f = fopen(filename, "rb");
  if (!f)
    throw runtime_error("file not found");

  // load overall header
  resource_fork_header header(f);

  // load resource map header
  fseek(f, header.resource_map_offset, SEEK_SET);
  resource_map_header map_header(f);

  // look in resource type map for a matching type
  fseek(f, map_header.resource_type_list_offset + header.resource_map_offset, SEEK_SET);
  resource_type_list map_type_list(f);

  for (const auto& entry : map_type_list.entries) {
    fseek(f, map_header.resource_type_list_offset + header.resource_map_offset + entry.reference_list_offset, SEEK_SET);

    for (int x = 0; x <= entry.num_items; x++) {
      resource_reference_list_entry e(f);
      all_resources.emplace_back(entry.resource_type, e.resource_id);
    }
  }

  fclose(f);
  return all_resources;
}



////////////////////////////////////////////////////////////////////////////////
// image decoding

#pragma pack(push)
#pragma pack(1)

struct cicn_header {
  uint8_t unknown1[10];
  uint16_t h;
  uint16_t w;
  uint8_t unknown2[0x37];
  uint8_t has_bw_image;
  uint8_t unknown3[0x0C];

  void byteswap() {
    this->w = byteswap16(this->w);
    this->h = byteswap16(this->h);
  }
};

struct cicn_mask_map {
  uint8_t data[0];

  bool lookup_entry(int w, int x, int y) {
    int row_size = (w < 64) ? (((w + 31) / 32) * 4) : ((w + 63) / 64) * 8;
    int entry_num = (y * row_size) + (x / 8);
    return !!(this->data[entry_num] & (1 << (7 - (x & 7))));
  }
  static size_t size(int w, int h) {
    int row_size = (w < 64) ? (((w + 31) / 32) * 4) : ((w + 63) / 64) * 8;
    return row_size * h;
  }
};

struct color_table_entry {
  uint16_t color_num;
  uint16_t r;
  uint16_t g;
  uint16_t b;

  void byteswap() {
    this->color_num = byteswap16(this->color_num);
    this->r = byteswap16(this->r);
    this->g = byteswap16(this->g);
    this->b = byteswap16(this->b);
  }
};

struct color_table {
  uint32_t unknown1;
  int32_t num_entries; // actually num_entries - 1
  color_table_entry entries[0];

  size_t size() {
    return sizeof(color_table) + (this->num_entries + 1) * sizeof(color_table_entry);
  }

  size_t size_swapped() {
    return sizeof(color_table) + (byteswap32(this->num_entries) + 1) * sizeof(color_table_entry);
  }

  void byteswap() {
    this->num_entries = byteswap32(this->num_entries);
    for (int32_t y = 0; y <= this->num_entries; y++)
      this->entries[y].byteswap();
  }

  uint32_t get_num_entries() {
    return this->num_entries + 1;
  }

  const color_table_entry* get_entry(int16_t id) const {
    for (int32_t x = 0; x <= this->num_entries; x++)
      if (this->entries[x].color_num == id)
        return &this->entries[x];
    return NULL;
  }
};

#pragma pack(pop)



Image decode_cicn(const void* data, size_t size, uint8_t tr, uint8_t tg, uint8_t tb) {

  uint8_t* begin_bound = (uint8_t*)data;
  uint8_t* end_bound = begin_bound + size;

  if (size < sizeof(cicn_header))
    throw runtime_error("corrupt cicn (too small for header)");

  cicn_header* header = (cicn_header*)data;
  header->byteswap();

  cicn_mask_map* mask_map = (cicn_mask_map*)(header + 1);
  size_t mask_map_size = mask_map->size(header->w, header->h);
  if ((uint8_t*)mask_map + mask_map_size > end_bound)
    throw runtime_error("corrupt cicn (mask map too large)");

  color_table* ctable = (color_table*)((uint8_t*)mask_map + (header->has_bw_image == 0x04 ? 2 : 1) * mask_map_size);
  if ((int32_t)byteswap32(ctable->num_entries) < 0)
    throw runtime_error("corrupt cicn (color table has negative size)");
  if ((uint8_t*)ctable >= end_bound)
    throw runtime_error("corrupt cicn (color table beyond end bound)");
  if ((uint8_t*)ctable + ctable->size_swapped() > end_bound)
    throw runtime_error("corrupt cicn (color table too large)");

  ctable->byteswap();

  uint8_t* color_ids = (uint8_t*)((uint8_t*)ctable + ctable->size());

  Image img(header->w, header->h);

  for (int y = 0; y < header->h; y++) {
    for (int x = 0; x < header->w; x++) {
      if (mask_map->lookup_entry(header->w, x, y)) {

        uint8_t color_id;

        if (ctable->get_num_entries() > 16)
          color_id = color_ids[y * header->w + x];

        else if (ctable->get_num_entries() > 4) {
          int pixel_index = y * header->w + x;
          color_id = color_ids[pixel_index / 2];
          if (pixel_index & 1)
            color_id &= 0xF;
          else
            color_id = (color_id >> 4) & 0xF;

        } else if (ctable->get_num_entries() > 2) {
          int pixel_index = y * header->w + x;
          color_id = color_ids[pixel_index / 4];
          color_id = (color_id >> (6 - (pixel_index & 3) * 2)) & 3;

        } else {
          int pixel_index = y * header->w + x;
          color_id = color_ids[pixel_index / 8];
          color_id = (color_id >> (7 - (pixel_index & 7))) & 1;
        }

        const color_table_entry* e = ctable->get_entry(color_id);
        if (e)
          img.write_pixel(x, y, e->r, e->g, e->b);
        else
          throw runtime_error("color not found in color map");
      } else
        img.write_pixel(x, y, tr, tg, tb);
    }
  }

  return img;
}

Image decode_pict(const void* data, size_t size) {
  char temp_filename[30] = "/tmp/realmz_dasm.XXXXXXXXXXXX";
  {
    int fd = mkstemp(temp_filename);
    auto f = fdopen_unique(fd, "wb");
    fwrite(data, size, 1, f.get());
  }

  char command[0x100];
  sprintf(command, "picttoppm -noheader %s", temp_filename);
  FILE* p = popen(command, "r");
  if (!p) {
    unlink(temp_filename);
    pclose(p);
    throw runtime_error("can\'t run picttoppm");
  }

  try {
    Image img(p);
    pclose(p);
    unlink(temp_filename);
    return img;

  } catch (const exception& e) {
    pclose(p);
    unlink(temp_filename);
    throw;
  }
}



////////////////////////////////////////////////////////////////////////////////
// sound decoding

#pragma pack(push)
#pragma pack(1)

struct snd_file_header {
  uint16_t unknown1[4];
  uint16_t flags; // 0x40 = stereo; 0x80 = something (always appears to be set)
  uint16_t format_code;
  uint16_t unknown2[2];

  void byteswap() {
    this->flags = byteswap16(this->flags);
    this->format_code = byteswap16(this->format_code);
  }
};

struct snd_data_header {
  uint32_t data_size;
  uint16_t sample_rate;
  uint16_t unknown1[5];
  uint8_t use_extended_header;
  uint8_t unknown2;

  uint8_t data[0];

  void byteswap() {
    this->data_size = byteswap32(this->data_size);
    this->sample_rate = byteswap16(this->sample_rate);
  }
};

struct snd_data_header_extended {
  snd_data_header basic;

  uint32_t num_samples;
  uint16_t unknown2[11];
  uint16_t bits_per_sample;
  uint16_t unknown3[7];

  uint8_t data[0];

  void byteswap() {
    this->basic.byteswap();
    this->num_samples = byteswap32(this->num_samples);
    this->bits_per_sample = byteswap16(this->bits_per_sample);
  }
};

struct wav_header {
  uint32_t riff_magic;   // 0x52494646
  uint32_t file_size;    // size of file - 8
  uint32_t wave_magic;   // 0x57415645

  uint32_t fmt_magic;    // 0x666d7420
  uint32_t fmt_size;     // 16
  uint16_t format;       // 1 = PCM
  uint16_t num_channels;
  uint32_t sample_rate;
  uint32_t byte_rate;    // num_channels * sample_rate * bits_per_sample / 8
  uint16_t block_align;  // num_channels * bits_per_sample / 8
  uint16_t bits_per_sample;

  uint32_t data_magic;   // 0x64617461
  uint32_t data_size;    // num_samples * num_channels * bits_per_sample / 8

  wav_header(uint32_t num_samples, uint16_t num_channels, uint32_t sample_rate,
      uint16_t bits_per_sample) {

    this->riff_magic = byteswap32(0x52494646);
    this->file_size = num_samples * num_channels * bits_per_sample / 8 +
        sizeof(wav_header) - 8;
    this->wave_magic = byteswap32(0x57415645);
    this->fmt_magic = byteswap32(0x666d7420);
    this->fmt_size = 16;
    this->format = 1;
    this->num_channels = num_channels;
    this->sample_rate = sample_rate;
    this->byte_rate = num_channels * sample_rate * bits_per_sample / 8;
    this->block_align = num_channels * bits_per_sample / 8;
    this->bits_per_sample = bits_per_sample;
    this->data_magic = byteswap32(0x64617461);
    this->data_size = num_samples * num_channels * bits_per_sample / 8;
  }
};

#pragma pack(pop)

vector<uint8_t> decode_snd(const void* data, size_t size) {
  snd_file_header* snd = (snd_file_header*)data;
  snd->byteswap();

  snd_data_header* header;
  if (snd->format_code == 0x0000) {
    header = (snd_data_header*)((const uint8_t*)data + sizeof(snd_file_header) + 0x02);
  } else if (snd->format_code == 0x0001) {
    header = (snd_data_header*)((const uint8_t*)data + sizeof(snd_file_header) + 0x08);
  } else if (snd->format_code == 0x0002) {
    header = (snd_data_header*)((const uint8_t*)data + sizeof(snd_file_header) + 0x10);
  } else {
    throw runtime_error(string_printf("unknown format code: %04" PRIX16, snd->format_code));
  }

  int num_channels = (snd->flags & 0x40) ? 2 : 1;

  if (header->use_extended_header) {
    snd_data_header_extended* ext = (snd_data_header_extended*)header;
    ext->byteswap();

    wav_header wav(ext->num_samples, num_channels, ext->basic.sample_rate,
        ext->bits_per_sample);
    if (wav.data_size > size - sizeof(snd_data_header_extended)) {
      throw runtime_error(string_printf("computed data_size exceeds actual data (%" PRIu32 " computed, %zu available)",
          wav.data_size, size - sizeof(snd_data_header_extended)));
    }

    uint32_t ret_size = sizeof(wav_header) + wav.data_size;
    vector<uint8_t> ret(ret_size);
    memcpy(ret.data(), &wav, sizeof(wav_header));
    memcpy(ret.data() + sizeof(wav_header), ext->data, wav.data_size);

    if (wav.bits_per_sample == 0x10) {
      uint16_t* samples = (uint16_t*)(ret.data() + sizeof(wav_header));
      for (uint32_t x = 0; x < wav.data_size / 2; x++) {
        samples[x] = byteswap16(samples[x]);
      }
    }

    return ret;

  } else {
    header->byteswap();

    wav_header wav(header->data_size, num_channels, header->sample_rate, 8);
    size_t available_data = size - ((const uint8_t*)header->data - (const uint8_t*)data);
    uint32_t ret_size = sizeof(wav_header) + available_data;

    vector<uint8_t> ret(ret_size);
    memcpy(ret.data(), &wav, sizeof(wav_header));
    memcpy(ret.data() + sizeof(wav_header), header->data, available_data);
    return ret;
  }
}

vector<string> decode_strN(const void* vdata, size_t size) {
  if (size < 2)
    throw runtime_error("STR# size is too small");

  char* data = (char*)vdata + sizeof(uint16_t); // ignore the count; just read all of them
  size -= 2;

  vector<string> ret;
  while (size > 0) {
    uint8_t len = *(uint8_t*)data;
    data++;
    size--;
    if (len > size)
      throw runtime_error("corrupted STR# resource");

    ret.emplace_back(data, len);
    data += len;
    size -= len;
  }

  return ret;
}
