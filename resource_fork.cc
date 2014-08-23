#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <exception>
#include <stdexcept>
#include <vector>

#include "Image.hh"

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
  uint16_t w;
  uint16_t h;
  uint8_t unknown2[0x37];
  uint8_t has_bw_image;
  uint8_t unknown3[0x0C];

  void byteswap() {
    this->w = byteswap16(this->w);
    this->h = byteswap16(this->h);
  }
};

struct cicn_mask_map32 {
  uint32_t rows[32];

  void byteswap() {
    for (int y = 0; y < 32; y++)
      this->rows[y] = byteswap32(this->rows[y]);
  }

  bool solid(int x, int y) const {
    return (this->rows[y] & (1 << (31 - x))) ? true : false;
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
  uint32_t unknown;
  uint32_t num_entries; // actually num_entries - 1
  color_table_entry entries[0];

  size_t size() {
    return sizeof(color_table) + (this->num_entries + 1) * sizeof(color_table_entry);
  }

  void byteswap() {
    this->num_entries = byteswap32(this->num_entries);
    for (size_t y = 0; y <= this->num_entries; y++)
      this->entries[y].byteswap();
  }

  uint32_t get_num_entries() {
    return this->num_entries + 1;
  }

  const color_table_entry* get_entry(int16_t id) const {
    for (size_t x = 0; x <= this->num_entries; x++)
      if (this->entries[x].color_num == id)
        return &this->entries[x];
    return NULL;
  }
};

struct pict_header {
  int16_t size;
  int16_t left;
  int16_t top;
  int16_t h;
  int16_t w;
  int16_t unused[45];

  void byteswap() {
    this->size = byteswap16(this->size);
    this->left = byteswap16(this->left);
    this->top = byteswap16(this->top);
    this->h = byteswap16(this->h);
    this->w = byteswap16(this->w);
  }
};

struct pict_interim {
  int16_t unused[10];
};

#pragma pack(pop)



Image decode_cicn32(void* data, size_t size, uint8_t tr, uint8_t tg, uint8_t tb) {

  cicn_header* header = (cicn_header*)data;
  header->byteswap();

  if (header->w != 32 || header->h != 32)
    throw runtime_error("can only decode 32x32 cicns");

  cicn_mask_map32* mask_map = (cicn_mask_map32*)(header + 1);
  color_table* ctable = (color_table*)(mask_map + (header->has_bw_image == 0x04 ? 2 : 1));
  mask_map->byteswap();
  ctable->byteswap();

  uint8_t* color_ids = (uint8_t*)((uint8_t*)ctable + ctable->size());

  Image img(header->w, header->h);

  for (int y = 0; y < header->h; y++) {
    for (int x = 0; x < header->w; x++) {
      if (mask_map->solid(x, y)) {

        uint8_t color_id;
        if (ctable->get_num_entries() <= 16) {
          color_id = color_ids[y * (header->w / 2) + (x / 2)];
          if (x & 1)
            color_id &= 0xF;
          else
            color_id = (color_id >> 4) & 0xF;

        } else
          color_id = color_ids[y * header->w + x];

        const color_table_entry* e = ctable->get_entry(color_id);
        if (e)
          img.WritePixel(x, y, e->r, e->g, e->b);
        else
          throw runtime_error("color not found in color map");
      } else
        img.WritePixel(x, y, tr, tg, tb);
    }
  }

  return img;
}

Image decode_pict(void* data, size_t size) {
  const char* filename = tmpnam(NULL);
  FILE* f = fopen(filename, "wb");
  fwrite(data, size, 1, f);
  fclose(f);

  char command[0x100];
  sprintf(command, "picttoppm -noheader %s", filename);
  FILE* p = popen(command, "r");
  if (!p) {
    unlink(filename);
    throw runtime_error("can\'t run picttoppm");
  }
  Image img(p);
  pclose(p);

  unlink(filename);
  return img;
}
