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

#include "audio_codecs.hh"
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

  resource_fork_header(int fd) {
    readx(fd, this, sizeof(*this));
    this->byteswap();
  }
};

struct resource_data {
  uint32_t size;
  uint8_t* data;

  void byteswap() {
    this->size = byteswap32(this->size);
  }

  resource_data(int fd) {
    readx(fd, &this->size, sizeof(this->size));
    this->byteswap();
    this->data = new uint8_t[this->size];
    readx(fd, this->data, this->size);
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

  resource_map_header(int fd) {
    readx(fd, this, sizeof(*this));
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

  resource_type_list_entry(int fd) {
    readx(fd, this, sizeof(*this));
    this->byteswap();
  }
};

struct resource_type_list {
  uint16_t num_types; // actually num_types - 1
  vector<resource_type_list_entry> entries;

  void byteswap() {
    this->num_types = byteswap16(this->num_types);
  }

  resource_type_list(int fd) {
    readx(fd, &this->num_types, sizeof(this->num_types));
    this->byteswap();

    // 0xFFFF means an empty resource fork
    if (this->num_types != 0xFFFF) {
      for (uint32_t i = 0; i <= this->num_types; i++) {
        this->entries.emplace_back(fd);
      }
    }
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

  resource_reference_list_entry(int fd) {
    readx(fd, this, sizeof(*this));
    this->byteswap();
  }
};

#pragma pack(pop)



void load_resource_from_file(const char* filename, uint32_t resource_type,
    int16_t resource_id, void** data, size_t* size) {

  *data = NULL;
  *size = 0;

  scoped_fd fd(filename, O_RDONLY);

  // load overall header
  resource_fork_header header(fd);

  // load resource map header
  lseek(fd, header.resource_map_offset, SEEK_SET);
  resource_map_header map_header(fd);

  // look in resource type map for a matching type
  lseek(fd, map_header.resource_type_list_offset + header.resource_map_offset, SEEK_SET);
  resource_type_list map_type_list(fd);

  const resource_type_list_entry* type_list = NULL;
  for (const auto& entry : map_type_list.entries) {
    if (entry.resource_type == resource_type) {
      type_list = &entry;
    }
  }

  if (!type_list) {
    throw runtime_error("file doesn\'t contain resources of the given type");
  }

  // look in resource list for something with the given ID
  lseek(fd, map_header.resource_type_list_offset + header.resource_map_offset + type_list->reference_list_offset, SEEK_SET);
  int x;
  for (x = 0; x <= type_list->num_items; x++) {
    resource_reference_list_entry e(fd);
    if (e.resource_id != resource_id) {
      continue;
    }

    // yay we found it! now read the thing
    lseek(fd, header.resource_data_offset + (e.attributes_and_offset & 0x00FFFFFF), SEEK_SET);
    resource_data d(fd);
    *data = malloc(d.size);
    memcpy(*data, d.data, d.size);
    *size = d.size;
    break;
  }

  if (x > type_list->num_items) {
    throw runtime_error("file doesn\'t contain resource with the given id");
  }
}

vector<pair<uint32_t, int16_t>> enum_file_resources(const char* filename) {

  vector<pair<uint32_t, int16_t>> all_resources;

  scoped_fd fd(filename, O_RDONLY);
  if (fstat(fd).st_size < sizeof(resource_fork_header)) {
    return vector<pair<uint32_t, int16_t>>();
  }

  // load overall header
  resource_fork_header header(fd);

  // load resource map header
  lseek(fd, header.resource_map_offset, SEEK_SET);
  resource_map_header map_header(fd);

  // look in resource type map for a matching type
  lseek(fd, map_header.resource_type_list_offset + header.resource_map_offset, SEEK_SET);
  resource_type_list map_type_list(fd);

  for (const auto& entry : map_type_list.entries) {
    lseek(fd, map_header.resource_type_list_offset + header.resource_map_offset + entry.reference_list_offset, SEEK_SET);

    for (int x = 0; x <= entry.num_items; x++) {
      resource_reference_list_entry e(fd);
      all_resources.emplace_back(entry.resource_type, e.resource_id);
    }
  }

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



Image decode_cicn(const void* vdata, size_t size, uint8_t tr, uint8_t tg, uint8_t tb) {
  // make a local copy so we can modify it
  vector<uint8_t> copied_data(size);
  void* data = copied_data.data();
  memcpy(data, vdata, size);

  uint8_t* begin_bound = (uint8_t*)data;
  uint8_t* end_bound = begin_bound + size;

  if (size < sizeof(cicn_header)) {
    throw runtime_error("corrupt cicn (too small for header)");
  }

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
  char temp_filename[36] = "/tmp/resource_dump.XXXXXXXXXXXX";
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

struct snd_resource_header_format2 {
  uint16_t format_code; // = 2
  uint16_t reference_count;
  uint16_t num_commands;

  void byteswap() {
    this->format_code = byteswap16(this->format_code);
    this->reference_count = byteswap16(this->reference_count);
    this->num_commands = byteswap16(this->num_commands);
  }
};

struct snd_resource_header_format1 {
  uint16_t format_code; // = 1
  uint16_t data_format_count; // we only support 0 or 1 here
  uint16_t data_format_id; // we only support 5 here (sampled sound)
  uint32_t flags; // 0x40 = stereo
  uint16_t num_commands;

  void byteswap() {
    this->format_code = byteswap16(this->format_code);
    this->data_format_count = byteswap16(this->data_format_count);
    this->data_format_id = byteswap16(this->data_format_id);
    this->flags = byteswap32(this->flags);
    this->num_commands = byteswap16(this->num_commands);
  }
};

struct snd_command {
  // we only support command 0x8051 (bufferCmd)
  // for this command, param1 is ignored; param2 is the offset to the sample
  // buffer struct from the beginning of the resource
  uint16_t command;
  uint16_t param1;
  uint32_t param2;

  void byteswap() {
    this->command = byteswap16(this->command);
    this->param1 = byteswap16(this->param1);
    this->param2 = byteswap32(this->param2);
  }
};

struct snd_sample_buffer {
  uint32_t data_offset; // from end of this struct
  uint32_t data_bytes;
  uint32_t sample_rate;
  uint32_t loop_start;
  uint32_t loop_end;
  uint8_t encoding;
  uint8_t base_freq;

  uint8_t data[0];

  void byteswap() {
    this->data_offset = byteswap32(this->data_offset);
    this->data_bytes = byteswap32(this->data_bytes);
    this->sample_rate = byteswap32(this->sample_rate);
    this->loop_start = byteswap32(this->loop_start);
    this->loop_end = byteswap32(this->loop_end);
  }
};

struct snd_compressed_buffer {
  uint32_t num_frames;
  uint8_t sample_rate[10]; // what kind of encoding is this? lolz
  uint32_t marker_chunk;
  uint32_t format;
  uint32_t reserved1;
  uint32_t state_vars; // high word appears to be sample size
  uint32_t left_over_block_ptr;
  uint16_t compression_id;
  uint16_t packet_size;
  uint16_t synth_id;
  uint16_t bits_per_sample;

  uint8_t data[0];

  void byteswap() {
    this->num_frames = byteswap32(this->num_frames);
    this->marker_chunk = byteswap32(this->marker_chunk);
    this->format = byteswap32(this->format);
    this->reserved1 = byteswap32(this->reserved1);
    this->state_vars = byteswap32(this->state_vars);
    this->left_over_block_ptr = byteswap32(this->left_over_block_ptr);
    this->compression_id = byteswap16(this->compression_id);
    this->packet_size = byteswap16(this->packet_size);
    this->synth_id = byteswap16(this->synth_id);
    this->bits_per_sample = byteswap16(this->bits_per_sample);
  }
};

vector<uint8_t> decode_snd(const void* vdata, size_t size) {
  if (size < 2) {
    throw runtime_error("snd doesn\'t even contain a format code");
  }
  uint16_t format_code = byteswap16(*reinterpret_cast<const uint16_t*>(vdata));

  // make a local copy so we can modify it
  vector<uint8_t> copied_data(size);
  void* data = copied_data.data();
  memcpy(data, vdata, size);

  // parse the resource header
  int num_channels = 1;
  size_t commands_offset;
  size_t num_commands;
  if (format_code == 0x0001) {
    if (size < sizeof(snd_resource_header_format1)) {
      throw runtime_error("snd is too small to contain resource header");
    }
    snd_resource_header_format1* header = (snd_resource_header_format1*)data;
    header->byteswap();

    // ugly hack: if data format count is 0, assume sampled mono and subtract
    // the fields from the offset
    if (header->data_format_count == 0) {
      num_channels = 1;
      commands_offset = sizeof(snd_resource_header_format1) - 6;
      num_commands = header->data_format_id; // shifted back by 6

    } else if (header->data_format_count == 1) {
      if (header->data_format_id != 5) {
        throw runtime_error("snd data format is not sampled");
      }
      num_channels = (header->flags & 0x40) ? 2 : 1;
      commands_offset = sizeof(snd_resource_header_format1);
      num_commands = header->num_commands;

    } else {
      throw runtime_error("snd has multiple data formats");
    }

  } else if (format_code == 0x0002) {
    if (size < sizeof(snd_resource_header_format2)) {
      throw runtime_error("snd is too small to contain resource header");
    }
    snd_resource_header_format2* header = (snd_resource_header_format2*)data;
    header->byteswap();

    commands_offset = sizeof(snd_resource_header_format2);
    num_commands = header->num_commands;

  } else {
    throw runtime_error("snd is not format 1 or 2");
  }

  if (num_commands == 0) {
    throw runtime_error("snd contains no commands");
  }
  size_t command_end_offset = commands_offset + num_commands * sizeof(snd_command);
  if (command_end_offset > size) {
    throw runtime_error("snd contains more commands than fit in resource");
  }

  size_t sample_buffer_offset = 0;
  snd_command* commands = (snd_command*)((uint8_t*)data + commands_offset);
  for (size_t x = 0; x < num_commands; x++) {
    commands[x].byteswap();

    if (commands[x].command == 0x000) {
      continue; // does this command do anything?
    }
    if ((commands[x].command != 0x8050) && (commands[x].command != 0x8051)) {
      throw runtime_error(string_printf("unknown command: %04hX", commands[x].command));
    }
    if (sample_buffer_offset) {
      throw runtime_error("snd contains multiple buffer commands");
    }
    sample_buffer_offset = commands[x].param2;
  }

  // some snds have an incorrect sample buffer offset, but they still play! I
  // guess sound manager ignores the offset in the command?
  sample_buffer_offset = command_end_offset;
  if (sample_buffer_offset + sizeof(snd_sample_buffer) > size) {
    throw runtime_error("sample buffer is outside snd resource");
  }
  snd_sample_buffer* sample_buffer = (snd_sample_buffer*)((uint8_t*)data + sample_buffer_offset);
  sample_buffer->byteswap();
  uint16_t sample_rate = sample_buffer->sample_rate >> 16;

  // uncompressed data can be copied verbatim
  if (sample_buffer->encoding == 0x00) {
    if (sample_buffer->data_bytes == 0) {
      throw runtime_error("snd contains no samples");
    }

    size_t available_data = size - ((const uint8_t*)sample_buffer->data - (const uint8_t*)data);
    if (available_data < sample_buffer->data_bytes) {
      sample_buffer->data_bytes = available_data;
    }

    wav_header wav(sample_buffer->data_bytes, num_channels, sample_rate, 8);
    uint32_t ret_size = sizeof(wav_header) + sample_buffer->data_bytes;

    vector<uint8_t> ret(ret_size);
    memcpy(ret.data(), &wav, sizeof(wav_header));
    memcpy(ret.data() + sizeof(wav_header), sample_buffer->data, sample_buffer->data_bytes);
    return ret;

  // compressed data will need to be processed somehow... sigh
  } else if ((sample_buffer->encoding == 0xFE) || (sample_buffer->encoding == 0xFF)) {
    if (size < sample_buffer_offset + sizeof(snd_sample_buffer) + sizeof(snd_compressed_buffer)) {
      throw runtime_error("snd is too small to contain compressed buffer");
    }
    snd_compressed_buffer* compressed_buffer = (snd_compressed_buffer*)((uint8_t*)data + sample_buffer_offset + sizeof(snd_sample_buffer));
    compressed_buffer->byteswap();

    switch (compressed_buffer->compression_id) {
      case 0xFFFE:
        throw runtime_error("snd uses variable-ratio compression");

      case 3:
      case 4: {
        bool is_mace3 = compressed_buffer->compression_id == 3;
        auto decoded_samples = decode_mace(compressed_buffer->data,
            compressed_buffer->num_frames * (is_mace3 ? 2 : 1) * num_channels,
            num_channels == 2, is_mace3);

        wav_header wav(decoded_samples.size(), num_channels, sample_rate, 16);
        if (wav.data_size != 2 * decoded_samples.size()) {
          throw runtime_error("computed data size does not match decoded data size");
        }

        uint32_t ret_size = sizeof(wav_header) + wav.data_size;
        vector<uint8_t> ret(ret_size);
        memcpy(ret.data(), &wav, sizeof(wav_header));
        memcpy(ret.data() + sizeof(wav_header), decoded_samples.data(), wav.data_size);
        return ret;
      }

      case 0xFFFF:

        if (compressed_buffer->format == 0x696D6134) { // ima4
          auto decoded_samples = decode_ima4(compressed_buffer->data,
              compressed_buffer->num_frames * 34 * num_channels,
              num_channels == 2);

          wav_header wav(decoded_samples.size() / num_channels, num_channels, sample_rate, 16);
          if (wav.data_size != 2 * decoded_samples.size()) {
            throw runtime_error(string_printf(
              "computed data size (%" PRIu32 ") does not match decoded data size (%zu)",
              wav.data_size, 2 * decoded_samples.size()));
          }

          uint32_t ret_size = sizeof(wav_header) + wav.data_size;
          vector<uint8_t> ret(ret_size);
          memcpy(ret.data(), &wav, sizeof(wav_header));
          memcpy(ret.data() + sizeof(wav_header), decoded_samples.data(), wav.data_size);
          return ret;
        }

        // allow 'twos' and 'sowt' - this is equivalent to no compression
        if ((compressed_buffer->format != 0x74776F73) && (compressed_buffer->format != 0x736F7774)) {
          throw runtime_error("snd uses unknown compression");
        }

      case 0: { // no compression
        uint32_t num_samples = compressed_buffer->num_frames;
        uint16_t bits_per_sample = compressed_buffer->bits_per_sample;
        if (bits_per_sample == 0) {
          bits_per_sample = compressed_buffer->state_vars >> 16;
        }

        size_t available_data = size - ((const uint8_t*)compressed_buffer->data - (const uint8_t*)data);

        // hack: if the sound is stereo and the computed data size is exactly
        // twice the available data size, treat it as mono
        if ((num_channels == 2) && (
            num_samples * num_channels * (bits_per_sample / 8)) == 2 * available_data) {
          num_channels = 1;
        }

        wav_header wav(num_samples, num_channels, sample_rate, bits_per_sample);
        if (wav.data_size == 0) {
          throw runtime_error(string_printf(
            "computed data size is zero (%" PRIu32 " samples, %d channels, %" PRIu16 " kHz, %" PRIu16 " bits per sample)",
            num_samples, num_channels, sample_rate, bits_per_sample));
        }
        if (wav.data_size > available_data) {
          throw runtime_error(string_printf("computed data size exceeds actual data (%" PRIu32 " computed, %zu available)",
              wav.data_size, available_data));
        }

        uint32_t ret_size = sizeof(wav_header) + wav.data_size;
        vector<uint8_t> ret(ret_size);
        memcpy(ret.data(), &wav, sizeof(wav_header));
        memcpy(ret.data() + sizeof(wav_header), compressed_buffer->data, wav.data_size);

        // byteswap the samples if it's 16-bit and not 'swot'
        if ((wav.bits_per_sample == 0x10) && (compressed_buffer->format != 0x736F7774)) {
          uint16_t* samples = (uint16_t*)(ret.data() + sizeof(wav_header));
          for (uint32_t x = 0; x < wav.data_size / 2; x++) {
            samples[x] = byteswap16(samples[x]);
          }
        }
        return ret;
      }

      default:
        throw runtime_error("snd is compressed using unknown algorithm");
    }

  } else {
    throw runtime_error(string_printf("unknown encoding for snd data: %02hhX", sample_buffer->encoding));
  }
}

#pragma pack(pop)



////////////////////////////////////////////////////////////////////////////////
// string decoding

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
