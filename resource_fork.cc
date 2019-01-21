#include "resource_fork.hh"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <exception>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <vector>
#include <string>

#include "audio_codecs.hh"
#include "mc68k.hh"

using namespace std;



// note: all structs in this file are packed
#pragma pack(push)
#pragma pack(1)



string string_for_resource_type(uint32_t type) {
  string result;
  for (ssize_t s = 24; s >= 0; s -= 8) {
    uint8_t ch = (type >> s) & 0xFF;
    if (ch == '\\') {
      result += "\\\\";
    } else if ((ch < ' ') || (ch > 0x7E)) {
      result += string_printf("\\x%02hhX", ch);
    } else {
      result += static_cast<char>(ch);
    }
  }
  return result;
}

Color::Color(uint16_t r, uint16_t g, uint16_t b) : r(r), g(g), b(b) { }

uint64_t Color::to_u64() const {
  return (static_cast<uint64_t>(this->r) << 32) |
         (static_cast<uint64_t>(this->g) << 16) |
         (static_cast<uint64_t>(this->b));
}




////////////////////////////////////////////////////////////////////////////////
// resource fork parsing

void resource_fork_header::read(int fd, size_t offset) {
  preadx(fd, this, sizeof(*this), offset);
  this->resource_data_offset = bswap32(this->resource_data_offset);
  this->resource_map_offset = bswap32(this->resource_map_offset);
  this->resource_data_size = bswap32(this->resource_data_size);
  this->resource_map_size = bswap32(this->resource_map_size);
}

void resource_map_header::read(int fd, size_t offset) {
  preadx(fd, this, sizeof(*this), offset);
  this->attributes = bswap16(this->attributes);
  this->resource_type_list_offset = bswap16(this->resource_type_list_offset);
  this->resource_name_list_offset = bswap16(this->resource_name_list_offset);
}

void resource_type_list_entry::read(int fd, size_t offset) {
  preadx(fd, this, sizeof(*this), offset);
  this->resource_type = bswap32(this->resource_type);
  this->num_items = bswap16(this->num_items);
  this->reference_list_offset = bswap16(this->reference_list_offset);
}

void resource_type_list::read(int fd, size_t offset) {
  preadx(fd, &this->num_types, sizeof(this->num_types), offset);
  this->num_types = bswap16(this->num_types);

  // 0xFFFF means an empty resource fork
  if (this->num_types != 0xFFFF) {
    for (uint32_t i = 0; i <= this->num_types; i++) {
      this->entries.emplace_back();
      this->entries.back().read(fd, offset + 2 + i * sizeof(resource_type_list_entry));
    }
  }
}

void resource_reference_list_entry::read(int fd, size_t offset) {
  preadx(fd, this, sizeof(*this), offset);
  this->resource_id = (int16_t)bswap16((uint16_t)this->resource_id);
  this->name_offset = bswap16(this->name_offset);
  this->attributes_and_offset = bswap32(this->attributes_and_offset);
}



ResourceFile::ResourceFile(const char* filename) {
  if (filename == NULL) {
    return;
  }
  this->fd = scoped_fd(filename, O_RDONLY);
  this->header.read(this->fd, 0);
  this->map_header.read(this->fd, this->header.resource_map_offset);
  this->map_type_list.read(this->fd,
      this->header.resource_map_offset + this->map_header.resource_type_list_offset);
}

vector<resource_reference_list_entry>* ResourceFile::get_reference_list(uint32_t type) {
  vector<resource_reference_list_entry>* reference_list = NULL;
  try {
    reference_list = &this->reference_list_cache.at(type);

  } catch (const out_of_range&) {
    const resource_type_list_entry* type_list = NULL;
    for (const auto& entry : this->map_type_list.entries) {
      if (entry.resource_type == type) {
        type_list = &entry;
      }
    }

    if (!type_list) {
      throw out_of_range("file doesn\'t contain resources of the given type");
    }

    // look in resource list for something with the given ID
    reference_list = &this->reference_list_cache[type];
    reference_list->reserve(type_list->num_items + 1);
    size_t base_offset = this->map_header.resource_type_list_offset +
        this->header.resource_map_offset + type_list->reference_list_offset;
    for (size_t x = 0; x <= type_list->num_items; x++) {
      reference_list->emplace_back();
      reference_list->back().read(fd, base_offset + x * sizeof(resource_reference_list_entry));
    }
  }

  return reference_list;
}

const string& ResourceFile::get_system_decompressor(int16_t resource_id) {
  static unordered_map<int16_t, string> id_to_data;
  try {
    return id_to_data.at(resource_id);
  } catch (const out_of_range&) {
    return id_to_data.emplace(resource_id, load_file(string_printf(
        "system_dcmps/dcmp_%hd.bin", resource_id))).first->second;
  }
}

struct compressed_resource_header {
  uint32_t magic; // 0xA89F6572
  uint32_t type_flags; // appears to be 0x00000901 or 0x00120801 in most cases

  // the kreativekorp definition is missing this field. TODO: which is right?
  uint32_t decompressed_size;
  union {
    // header1 is used when type_flags is 0x00000901
    uint16_t dcmp_resource_id;

    // header2 is used when type_flags is 0x00120801
    // note: kreativekorp has a similar definition but it's missing the
    // decompressed_size field. TODO: which is right?
    struct {
      uint8_t working_buffer_fractional_size; // length of compressed data relative to length of uncompressed data, out of 256
      uint8_t expansion_buffer_size; // greatest number of bytes compressed data will grow while being decompressed
      int16_t dcmp_resource_id;
      uint16_t unused;
    } header2;
  };

  void byteswap() {
    this->magic = bswap32(this->magic);
    this->type_flags = bswap32(this->type_flags);
    this->decompressed_size = bswap32(this->decompressed_size);

    if (this->type_flags & 0x00000100) {
      this->dcmp_resource_id = bswap16(this->dcmp_resource_id);

    } else {
      this->header2.dcmp_resource_id = bswap16(this->header2.dcmp_resource_id);
    }
  }
};

struct dcmp_input_header {
  // this is used to tell the program where to "return" to
  uint32_t return_addr;

  // actual parameters to the decompressor
  union {
    struct { // used when type_flags == 0x00000901
      uint32_t source_resource_header;
      uint32_t dest_buffer_addr;
      uint32_t source_buffer_addr;
      uint32_t data_size;
    } arguments1;
    struct { // used when type_flags == 0x00120801
      uint32_t data_size;
      uint32_t working_buffer_addr;
      uint32_t dest_buffer_addr;
      uint32_t source_buffer_addr;
    } arguments2;
  };

  // this is where the program "returns" to; the reset opcode stops emulation
  uint16_t reset_opcode;
  uint16_t unused;
};

string ResourceFile::decompress_resource(const string& data,
    DebuggingMode debug) {
  if (data.size() < sizeof(compressed_resource_header)) {
    return data; // resource cannot be compressed
  }

  compressed_resource_header header;
  memcpy(&header, data.data(), sizeof(compressed_resource_header));
  header.byteswap();
  if (header.magic != 0xA89F6572) {
    return data; // resource is not compressed
  }

  int16_t dcmp_resource_id;
  if (header.type_flags & 0x00000100) {
    dcmp_resource_id = header.dcmp_resource_id;
  } else {
    dcmp_resource_id = header.header2.dcmp_resource_id;
  }
  if (debug != DebuggingMode::Disabled) {
    fprintf(stderr, "using dcmp %hd\n", dcmp_resource_id);
    fprintf(stderr, "resource header looks like:\n");
    print_data(stderr, data.data(), data.size() > 0x40 ? 0x40 : data.size());
    fprintf(stderr, "note: decompressed data size is %" PRIu32 " (0x%" PRIX32 ") bytes\n",
        header.decompressed_size, header.decompressed_size);
  }

  // get the decompressor code. if it's not in the file, look in system as well
  string dcmp_contents;
  try {
    dcmp_contents = this->get_resource_data(RESOURCE_TYPE_DCMP, dcmp_resource_id);
  } catch (const out_of_range&) {
    dcmp_contents = this->get_system_decompressor(dcmp_resource_id);
  }

  // figure out where in the dcmp to start execution. there appear to be two
  // formats: one that has 'dcmp' in bytes 4-8 where execution appears to just
  // start at byte 0 (usually it's a branch opcode), and one where the first
  // three words appear to be offsets to various functions, followed by code.
  // the second word appears to be the main entry point in this format, so we'll
  // use that to determine where to start execution.
  uint32_t dcmp_entry_offset;
  if (dcmp_contents.size() < 10) {
    throw runtime_error("decompressor resource is too short");
  }
  if (dcmp_contents.substr(4, 4) == "dcmp") {
    dcmp_entry_offset = 0;
  } else {
    dcmp_entry_offset = bswap16(*reinterpret_cast<const uint16_t*>(
        dcmp_contents.data() + 2));
  }
  if (debug != DebuggingMode::Disabled) {
    fprintf(stderr, "dcmp entry offset is %08" PRIX32 "\n", dcmp_entry_offset);
  }

  MC68KEmulator emu;

  // set up memory regions
  // slightly awkward assumption: decompressed data is never more than 256 times
  // the size of the input data. TODO: it looks like we probably should be using
  // ((data.size() * 256) / working_buffer_fractional_size) instead here?
  uint32_t stack_base = 0x20000000;
  uint32_t output_base = 0x40000000;
  uint32_t input_base = 0x60000000;
  uint32_t working_buffer_base = 0xA0000000;
  uint32_t code_base = 0xE0000000;
  string& stack_region = emu.memory_regions[stack_base];
  string& output_region = emu.memory_regions[output_base];
  string& input_region = emu.memory_regions[input_base];
  string& working_buffer_region = emu.memory_regions[working_buffer_base];
  string& code_region = emu.memory_regions[code_base];
  stack_region.resize(1024 * 4);
  output_region.resize(header.decompressed_size + 0x100);
  input_region = data;
  working_buffer_region.resize(data.size() * 256);
  code_region = move(dcmp_contents);

  // TODO: looks like some decompressors expect zero bytes after the compressed
  // data? find out if this is actually true and fix it if not
  input_region.resize(input_region.size() + 0x100);

  // set up header in input region
  dcmp_input_header* input_header = reinterpret_cast<dcmp_input_header*>(
      const_cast<char*>(stack_region.data() + stack_region.size() - sizeof(dcmp_input_header)));
  input_header->return_addr = bswap32(stack_base + stack_region.size() - 4);
  if (header.type_flags & 0x00000100) {
    input_header->arguments1.data_size = bswap32(input_region.size() - sizeof(compressed_resource_header));
    input_header->arguments1.source_resource_header = bswap32(input_base);
    input_header->arguments1.dest_buffer_addr = bswap32(output_base);
    input_header->arguments1.source_buffer_addr = bswap32(input_base + sizeof(compressed_resource_header));
  } else {
    input_header->arguments2.data_size = bswap32(input_region.size() - sizeof(compressed_resource_header));
    input_header->arguments2.working_buffer_addr = bswap32(working_buffer_base);
    input_header->arguments2.dest_buffer_addr = bswap32(output_base);
    input_header->arguments2.source_buffer_addr = bswap32(input_base + sizeof(compressed_resource_header));
  }
  input_header->reset_opcode = 0x704E;
  input_header->unused = 0x0000;

  // set up registers
  for (size_t x = 0; x < 8; x++) {
    emu.d[x] = 0;
  }
  for (size_t x = 0; x < 7; x++) {
    emu.a[x] = 0;
  }
  emu.a[7] = 0x20000000 + stack_region.size() - sizeof(dcmp_input_header);

  emu.pc = 0xE0000000 + dcmp_entry_offset;
  emu.ccr = 0x0000;

  emu.debug = debug;

  // let's roll, son
  try {
    emu.execute_forever();
  } catch (const exception& e) {
    if (debug != DebuggingMode::Disabled) {
      fprintf(stderr, "execution failed: %s\n", e.what());
      emu.print_state(stderr, true);
    }
    throw;
  }

  if (debug != DebuggingMode::Disabled) {
    emu.print_state(stderr, true);
  }
  output_region.resize(header.decompressed_size);
  return output_region;
}

bool ResourceFile::resource_exists(uint32_t resource_type, int16_t resource_id) {
  try {
    for (const auto& e : *this->get_reference_list(resource_type)) {
      if (e.resource_id == resource_id) {
        return true;
      }
    }
  } catch (const out_of_range&) { }

  return false;
}

string ResourceFile::get_resource_data(uint32_t resource_type,
    int16_t resource_id, bool decompress, DebuggingMode decompress_debug) {

  auto* reference_list = this->get_reference_list(resource_type);

  for (const auto& e : *reference_list) {
    if (e.resource_id != resource_id) {
      continue;
    }

    // yay we found it! now read the thing
    size_t offset = header.resource_data_offset + (e.attributes_and_offset & 0x00FFFFFF);
    uint32_t size;
    preadx(fd, &size, sizeof(size), offset);
    size = bswap32(size);

    string result(size, 0);
    preadx(fd, const_cast<char*>(result.data()), size, offset + sizeof(size));

    if ((e.attributes_and_offset & 0x01000000) && decompress) {
      return this->decompress_resource(result, decompress_debug);
    }

    return result;
  }

  throw out_of_range("file doesn\'t contain resource with the given id");
}

bool ResourceFile::resource_is_compressed(uint32_t resource_type,
    int16_t resource_id) {
  auto* reference_list = this->get_reference_list(resource_type);
  for (const auto& e : *reference_list) {
    if (e.resource_id != resource_id) {
      continue;
    }
    return (e.attributes_and_offset & 0x01000000);
  }
  return false;
}

vector<int16_t> ResourceFile::all_resources_of_type(uint32_t type) {
  vector<int16_t> all_resources;
  for (const auto& x : *this->get_reference_list(type)) {
    all_resources.emplace_back(x.resource_id);
  }
  return all_resources;
}

vector<pair<uint32_t, int16_t>> ResourceFile::all_resources() {
  vector<pair<uint32_t, int16_t>> all_resources;
  for (const auto& entry : this->map_type_list.entries) {
    for (const auto& x : *this->get_reference_list(entry.resource_type)) {
      all_resources.emplace_back(entry.resource_type, x.resource_id);
    }
  }
  return all_resources;
}



////////////////////////////////////////////////////////////////////////////////
// image decoding helpers

static Image decode_monochrome_image(const void* vdata, size_t size, size_t w, size_t h) {
  if (w & 7) {
    throw runtime_error("width is not a multiple of 8");
  }
  if (size != w * h / 8) {
    throw runtime_error("incorrect data size");
  }
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  Image result(w, h);
  for (size_t y = 0; y < h; y++) {
    for (size_t x = 0; x < w; x += 8) {
      uint8_t pixels = data[y * w / 8 + x / 8];
      for (size_t z = 0; z < 8; z++) {
        uint8_t value = (pixels & 0x80) ? 0x00 : 0xFF;
        pixels <<= 1;
        result.write_pixel(x + z, y, value, value, value);
      }
    }
  }

  return result;
}

static Image decode_monochrome_image_masked(const void* vdata,
    size_t size, size_t w, size_t h) {
  // this resource contains two images - one monochrome and one mask
  const uint8_t* image_data = reinterpret_cast<const uint8_t*>(vdata);
  const uint8_t* mask_data = image_data + (w * h / 8);

  if (w & 7) {
    throw runtime_error("width is not a multiple of 8");
  }
  if (size != w * h / 4) {
    throw runtime_error("incorrect data size");
  }

  Image result(w, h, true);
  for (size_t y = 0; y < h; y++) {
    for (size_t x = 0; x < w; x += 8) {
      uint8_t pixels = image_data[y * w / 8 + x / 8];
      uint8_t mask_pixels = mask_data[y * w / 8 + x / 8];
      for (size_t z = 0; z < 8; z++) {
        uint8_t value = (pixels & 0x80) ? 0x00 : 0xFF;
        uint8_t mask_value = (mask_pixels & 0x80) ? 0xFF : 0x00;
        pixels <<= 1;
        mask_pixels <<= 1;
        result.write_pixel(x + z, y, value, value, value, mask_value);
      }
    }
  }

  return result;
}

static const uint32_t icon_color_table_16[0x100] = {
  0xFFFFFF, 0xFFFF00, 0xFF6600, 0xDD0000, 0xFF0099, 0x330099, 0x0000DD, 0x0099FF,
  0x00BB00, 0x006600, 0x663300, 0x996633, 0xCCCCCC, 0x888888, 0x444444, 0x000000,
};

Image decode_4bit_image(const void* vdata, size_t size, size_t w, size_t h) {
  if (w & 1) {
    throw runtime_error("width is not even");
  }
  if (size != w * h / 2) {
    throw runtime_error("incorrect data size");
  }
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  Image result(w, h);
  for (size_t y = 0; y < h; y++) {
    for (size_t x = 0; x < w; x += 2) {
      uint8_t indexes = data[y * w / 2 + x / 2];
      uint32_t left_pixel = icon_color_table_16[(indexes >> 4) & 0x0F];
      uint32_t right_pixel = icon_color_table_16[indexes & 0x0F];
      result.write_pixel(x, y, (left_pixel >> 16) & 0xFF,
          (left_pixel >> 8) & 0xFF, left_pixel & 0xFF);
      result.write_pixel(x + 1, y, (right_pixel >> 16) & 0xFF,
          (right_pixel >> 8) & 0xFF, right_pixel & 0xFF);
    }
  }

  return result;
}

static const uint32_t icon_color_table_256[0x100] = {
  0xFFFFFF, 0xFFFFCC, 0xFFFF99, 0xFFFF66, 0xFFFF33, 0xFFFF00,
  0xFFCCFF, 0xFFCCCC, 0xFFCC99, 0xFFCC66, 0xFFCC33, 0xFFCC00,
  0xFF99FF, 0xFF99CC, 0xFF9999, 0xFF9966, 0xFF9933, 0xFF9900,
  0xFF66FF, 0xFF66CC, 0xFF6699, 0xFF6666, 0xFF6633, 0xFF6600,
  0xFF33FF, 0xFF33CC, 0xFF3399, 0xFF3366, 0xFF3333, 0xFF3300,
  0xFF00FF, 0xFF00CC, 0xFF0099, 0xFF0066, 0xFF0033, 0xFF0000,
  0xCCFFFF, 0xCCFFCC, 0xCCFF99, 0xCCFF66, 0xCCFF33, 0xCCFF00,
  0xCCCCFF, 0xCCCCCC, 0xCCCC99, 0xCCCC66, 0xCCCC33, 0xCCCC00,
  0xCC99FF, 0xCC99CC, 0xCC9999, 0xCC9966, 0xCC9933, 0xCC9900,
  0xCC66FF, 0xCC66CC, 0xCC6699, 0xCC6666, 0xCC6633, 0xCC6600,
  0xCC33FF, 0xCC33CC, 0xCC3399, 0xCC3366, 0xCC3333, 0xCC3300,
  0xCC00FF, 0xCC00CC, 0xCC0099, 0xCC0066, 0xCC0033, 0xCC0000,
  0x99FFFF, 0x99FFCC, 0x99FF99, 0x99FF66, 0x99FF33, 0x99FF00,
  0x99CCFF, 0x99CCCC, 0x99CC99, 0x99CC66, 0x99CC33, 0x99CC00,
  0x9999FF, 0x9999CC, 0x999999, 0x999966, 0x999933, 0x999900,
  0x9966FF, 0x9966CC, 0x996699, 0x996666, 0x996633, 0x996600,
  0x9933FF, 0x9933CC, 0x993399, 0x993366, 0x993333, 0x993300,
  0x9900FF, 0x9900CC, 0x990099, 0x990066, 0x990033, 0x990000,
  0x66FFFF, 0x66FFCC, 0x66FF99, 0x66FF66, 0x66FF33, 0x66FF00,
  0x66CCFF, 0x66CCCC, 0x66CC99, 0x66CC66, 0x66CC33, 0x66CC00,
  0x6699FF, 0x6699CC, 0x669999, 0x669966, 0x669933, 0x669900,
  0x6666FF, 0x6666CC, 0x666699, 0x666666, 0x666633, 0x666600,
  0x6633FF, 0x6633CC, 0x663399, 0x663366, 0x663333, 0x663300,
  0x6600FF, 0x6600CC, 0x660099, 0x660066, 0x660033, 0x660000,
  0x33FFFF, 0x33FFCC, 0x33FF99, 0x33FF66, 0x33FF33, 0x33FF00,
  0x33CCFF, 0x33CCCC, 0x33CC99, 0x33CC66, 0x33CC33, 0x33CC00,
  0x3399FF, 0x3399CC, 0x339999, 0x339966, 0x339933, 0x339900,
  0x3366FF, 0x3366CC, 0x336699, 0x336666, 0x336633, 0x336600,
  0x3333FF, 0x3333CC, 0x333399, 0x333366, 0x333333, 0x333300,
  0x3300FF, 0x3300CC, 0x330099, 0x330066, 0x330033, 0x330000,
  0x00FFFF, 0x00FFCC, 0x00FF99, 0x00FF66, 0x00FF33, 0x00FF00,
  0x00CCFF, 0x00CCCC, 0x00CC99, 0x00CC66, 0x00CC33, 0x00CC00,
  0x0099FF, 0x0099CC, 0x009999, 0x009966, 0x009933, 0x009900,
  0x0066FF, 0x0066CC, 0x006699, 0x006666, 0x006633, 0x006600,
  0x0033FF, 0x0033CC, 0x003399, 0x003366, 0x003333, 0x003300,
  0x0000FF, 0x0000CC, 0x000099, 0x000066, 0x000033, // note: no black here

  0xEE0000, 0xDD0000, 0xBB0000, 0xAA0000, 0x880000,
  0x770000, 0x550000, 0x440000, 0x220000, 0x110000,
  0x00EE00, 0x00DD00, 0x00BB00, 0x00AA00, 0x008800,
  0x007700, 0x005500, 0x004400, 0x002200, 0x001100,
  0x0000EE, 0x0000DD, 0x0000BB, 0x0000AA, 0x000088,
  0x000077, 0x000055, 0x000044, 0x000022, 0x000011,
  0xEEEEEE, 0xDDDDDD, 0xBBBBBB, 0xAAAAAA, 0x888888,
  0x777777, 0x555555, 0x444444, 0x222222, 0x111111,
  0x000000,
};

Image decode_8bit_image(const void* vdata, size_t size, size_t w, size_t h) {
  if (size != w * h) {
    throw runtime_error("incorrect data size");
  }
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  Image result(w, h);
  for (size_t y = 0; y < h; y++) {
    for (size_t x = 0; x < w; x++) {
      uint32_t pixel = icon_color_table_256[data[y * w + x]];
      result.write_pixel(x, y, (pixel >> 16) & 0xFF, (pixel >> 8) & 0xFF,
          pixel & 0xFF);
    }
  }

  return result;
}

ResourceFile::decoded_cicn::decoded_cicn(Image&& image, Image&& bitmap) :
    image(move(image)), bitmap(move(bitmap)) { }

ResourceFile::decoded_curs::decoded_curs(Image&& bitmap, uint16_t hotspot_x,
    uint16_t hotspot_y) : bitmap(move(bitmap)), hotspot_x(hotspot_x),
    hotspot_y(hotspot_y) { }

ResourceFile::decoded_crsr::decoded_crsr(Image&& image, Image&& bitmap,
    uint16_t hotspot_x, uint16_t hotspot_y) : image(move(image)),
    bitmap(move(bitmap)), hotspot_x(hotspot_x), hotspot_y(hotspot_y) { }



////////////////////////////////////////////////////////////////////////////////
// image resource decoding



struct pixel_map_header {
  uint32_t base_addr; // unused for resources
  uint16_t flags_row_bytes;
  uint16_t x;
  uint16_t y;
  uint16_t h;
  uint16_t w;
  uint16_t version;
  uint16_t pack_format;
  uint32_t pack_size;
  uint32_t h_res;
  uint32_t v_res;
  uint16_t pixel_type;
  uint16_t pixel_size; // bits per pixel
  uint16_t component_count;
  uint16_t component_size;
  uint32_t plane_offset;
  uint32_t color_table_offset;
  uint32_t reserved;

  void byteswap() {
    this->base_addr = bswap32(this->base_addr);
    this->flags_row_bytes = bswap16(this->flags_row_bytes);
    this->x = bswap16(this->x);
    this->y = bswap16(this->y);
    this->h = bswap16(this->h);
    this->w = bswap16(this->w);
    this->version = bswap16(this->version);
    this->pack_format = bswap16(this->pack_format);
    this->pack_size = bswap32(this->pack_size);
    this->h_res = bswap32(this->h_res);
    this->v_res = bswap32(this->v_res);
    this->pixel_type = bswap16(this->pixel_type);
    this->pixel_size = bswap16(this->pixel_size);
    this->component_count = bswap16(this->component_count);
    this->component_size = bswap16(this->component_size);
    this->plane_offset = bswap32(this->plane_offset);
    this->color_table_offset = bswap32(this->color_table_offset);
    this->reserved = bswap32(this->reserved);
  }
};

struct pixel_map_data {
  uint8_t data[0];

  uint8_t lookup_entry(uint16_t pixel_size, size_t row_bytes, size_t x, size_t y) const {
    if (pixel_size == 1) {
      return (this->data[(y * row_bytes) + (x / 8)] >> (7 - (x & 7))) & 1;

    } else if (pixel_size == 2) {
      return (this->data[(y * row_bytes) + (x / 4)] >> (6 - ((x & 3) * 2))) & 3;

    } else if (pixel_size == 4) {
      return (this->data[(y * row_bytes) + (x / 2)] >> (4 - ((x & 1) * 4))) & 15;

    } else if (pixel_size == 8) {
      return this->data[(y * row_bytes) + x];

    } else {
      throw runtime_error("pixel size is not 1, 2, 4, or 8 bits");
    }
  }

  static size_t size(uint16_t row_bytes, size_t h) {
    return row_bytes * h;
  }
};

struct color_table_entry {
  uint16_t color_num;
  uint16_t r;
  uint16_t g;
  uint16_t b;

  void byteswap() {
    this->color_num = bswap16(this->color_num);
    this->r = bswap16(this->r);
    this->g = bswap16(this->g);
    this->b = bswap16(this->b);
  }
};

struct color_table {
  uint32_t seed;
  uint16_t flags;
  int16_t num_entries; // actually num_entries - 1
  color_table_entry entries[0];

  size_t size() {
    return sizeof(color_table) + (this->num_entries + 1) * sizeof(color_table_entry);
  }

  size_t size_swapped() {
    return sizeof(color_table) + (bswap16(this->num_entries) + 1) * sizeof(color_table_entry);
  }

  void byteswap() {
    this->num_entries = bswap16(this->num_entries);
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

Image decode_color_image(const pixel_map_header& header,
    const pixel_map_data& pixel_map, const color_table& ctable,
    const pixel_map_data* mask_map = NULL, size_t mask_row_bytes = 0) {
  Image img(header.w, header.h, (mask_map != NULL));
  for (size_t y = 0; y < header.h; y++) {
    for (size_t x = 0; x < header.w; x++) {
      uint8_t color_id = pixel_map.lookup_entry(header.pixel_size,
          header.flags_row_bytes & 0xFF, x, y);
      const auto* e = ctable.get_entry(color_id);
      if (e) {
        uint8_t alpha = 0xFF;
        if (mask_map) {
          alpha = mask_map->lookup_entry(1, mask_row_bytes, x, y) ? 0xFF : 0x00;
        }
        img.write_pixel(x, y, e->r >> 8, e->g >> 8, e->b >> 8, alpha);
      } else {
        throw runtime_error("color not found in color map");
      }
    }
  }
  return img;
}


struct cicn_header {
  // pixMap fields
  pixel_map_header pix_map;

  // mask bitmap fields
  uint32_t unknown1;
  uint16_t mask_row_bytes;
  uint32_t unknown2;
  uint16_t mask_h;
  uint16_t mask_w;

  // 1-bit icon bitmap fields
  uint32_t unknown3;
  uint16_t bitmap_row_bytes;
  uint32_t unknown4;
  uint16_t bitmap_h;
  uint16_t bitmap_w;

  // icon data fields
  uint32_t icon_data; // ignored

  void byteswap() {
    this->pix_map.byteswap();
    this->mask_row_bytes = bswap16(this->mask_row_bytes);
    this->mask_h = bswap16(this->mask_h);
    this->mask_w = bswap16(this->mask_w);
    this->bitmap_row_bytes = bswap16(this->bitmap_row_bytes);
    this->bitmap_h = bswap16(this->bitmap_h);
    this->bitmap_w = bswap16(this->bitmap_w);
    this->icon_data = bswap32(this->icon_data);
  }
};

ResourceFile::decoded_cicn ResourceFile::decode_cicn(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() < sizeof(cicn_header)) {
    throw runtime_error("cicn too small for header");
  }
  uint8_t* bdata = reinterpret_cast<uint8_t*>(const_cast<char*>(data.data()));

  cicn_header* header = reinterpret_cast<cicn_header*>(bdata);
  header->byteswap();

  // the mask is required, but the bitmap may be missing
  if ((header->pix_map.w != header->mask_w) || (header->pix_map.h != header->mask_h)) {
    throw runtime_error("mask dimensions don\'t match icon dimensions");
  }
  if (header->bitmap_row_bytes &&
      ((header->pix_map.w != header->bitmap_w) || (header->pix_map.h != header->bitmap_h))) {
    throw runtime_error("bitmap dimensions don\'t match icon dimensions");
  }
  if ((header->pix_map.pixel_size != 8) && (header->pix_map.pixel_size != 4) &&
      (header->pix_map.pixel_size != 2) && (header->pix_map.pixel_size != 1)) {
    throw runtime_error("pixel bit depth is not 1, 2, 4, or 8");
  }

  size_t mask_map_size = pixel_map_data::size(header->mask_row_bytes, header->mask_h);
  pixel_map_data* mask_map = reinterpret_cast<pixel_map_data*>(bdata + sizeof(*header));
  if (sizeof(*header) + mask_map_size > data.size()) {
    throw runtime_error("mask map too large");
  }

  size_t bitmap_size = pixel_map_data::size(header->bitmap_row_bytes, header->bitmap_h);
  pixel_map_data* bitmap = reinterpret_cast<pixel_map_data*>(bdata + sizeof(*header) + mask_map_size);
  if (sizeof(*header) + mask_map_size + bitmap_size > data.size()) {
    throw runtime_error("bitmap too large");
  }

  color_table* ctable = reinterpret_cast<color_table*>(
      bdata + sizeof(*header) + mask_map_size + bitmap_size);
  if (sizeof(*header) + mask_map_size + bitmap_size + sizeof(*ctable) > data.size()) {
    throw runtime_error("color table header too large");
  }
  if (static_cast<int16_t>(bswap16(ctable->num_entries)) < 0) {
    throw runtime_error("color table has negative size");
  }
  if (sizeof(*header) + mask_map_size + bitmap_size + ctable->size_swapped() > data.size()) {
    throw runtime_error("color table contents too large");
  }
  ctable->byteswap();

  // decode the image data
  size_t pixel_map_size = pixel_map_data::size(
      header->pix_map.flags_row_bytes & 0xFF, header->pix_map.h);
  pixel_map_data* pixel_map = reinterpret_cast<pixel_map_data*>(
      bdata + sizeof(*header) + mask_map_size + bitmap_size + ctable->size());
  if (sizeof(*header) + mask_map_size + bitmap_size + ctable->size() + pixel_map_size > data.size()) {
    throw runtime_error("pixel map too large");
  }

  Image img = decode_color_image(header->pix_map, *pixel_map, *ctable, mask_map,
      header->mask_row_bytes);

  // decode the mask and bitmap
  Image bitmap_img(header->bitmap_row_bytes ? header->bitmap_w : 0,
      header->bitmap_row_bytes ? header->bitmap_h : 0, true);
  for (size_t y = 0; y < header->pix_map.h; y++) {
    for (size_t x = 0; x < header->pix_map.w; x++) {
      uint8_t alpha = mask_map->lookup_entry(1, header->mask_row_bytes, x, y) ? 0xFF : 0x00;

      if (header->bitmap_row_bytes) {
        if (bitmap->lookup_entry(1, header->bitmap_row_bytes, x, y)) {
          bitmap_img.write_pixel(x, y, 0x00, 0x00, 0x00, alpha);
        } else {
          bitmap_img.write_pixel(x, y, 0xFF, 0xFF, 0xFF, alpha);
        }
      }
    }
  }

  return decoded_cicn(move(img), move(bitmap_img));
}



struct crsr_header {
  uint16_t type; // 0x8000 (monochrome) or 0x8001 (color)
  uint32_t pixel_map_offset; // offset from beginning of resource data
  uint32_t pixel_data_offset; // offset from beginning of resource data
  uint32_t expanded_data; // ignore this (Color QuickDraw stuff)
  uint16_t expanded_depth;
  uint32_t unused;
  uint8_t bitmap[0x20];
  uint8_t mask[0x20];
  uint16_t hotspot_x;
  uint16_t hotspot_y;
  uint32_t color_table_offset; // offset from beginning of resource
  uint32_t cursor_id; // ignore this (resource id)

  void byteswap() {
    this->type = bswap16(this->type);
    this->pixel_map_offset = bswap32(this->pixel_map_offset);
    this->pixel_data_offset = bswap32(this->pixel_data_offset);
    this->expanded_data = bswap32(this->expanded_data);
    this->expanded_depth = bswap16(this->expanded_depth);
    this->unused = bswap32(this->unused);
    this->hotspot_x = bswap16(this->hotspot_x);
    this->hotspot_y = bswap16(this->hotspot_y);
    this->color_table_offset = bswap32(this->color_table_offset);
    this->cursor_id = bswap32(this->cursor_id);
  }
};

ResourceFile::decoded_crsr ResourceFile::decode_crsr(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() < sizeof(crsr_header)) {
    throw runtime_error("crsr too small for header");
  }
  uint8_t* bdata = reinterpret_cast<uint8_t*>(const_cast<char*>(data.data()));

  crsr_header* header = reinterpret_cast<crsr_header*>(bdata);
  header->byteswap();

  if ((header->type & 0xFFFE) != 0x8000) {
    throw runtime_error("unknown crsr type");
  }

  Image bitmap = decode_monochrome_image(&header->bitmap, 0x20, 16, 16);

  // get the pixel map header
  pixel_map_header* pixmap_header = reinterpret_cast<pixel_map_header*>(
      bdata + header->pixel_map_offset);
  if (header->pixel_map_offset + sizeof(*pixmap_header) > data.size()) {
    throw runtime_error("pixel map header too large");
  }
  pixmap_header->byteswap();

  // get the pixel map data
  size_t pixel_map_size = pixel_map_data::size(
      pixmap_header->flags_row_bytes & 0xFF, pixmap_header->h);
  if (header->pixel_data_offset + pixel_map_size > data.size()) {
    throw runtime_error("pixel map data too large");
  }
  pixel_map_data* pixmap_data = reinterpret_cast<pixel_map_data*>(
      bdata + header->pixel_data_offset);

  // get the color table
  color_table* ctable = reinterpret_cast<color_table*>(
      bdata + pixmap_header->color_table_offset);
  if (pixmap_header->color_table_offset + sizeof(*ctable) > data.size()) {
    throw runtime_error("color table header too large");
  }
  if (static_cast<int16_t>(bswap16(ctable->num_entries)) < 0) {
    throw runtime_error("color table has negative size");
  }
  if (pixmap_header->color_table_offset + ctable->size_swapped() > data.size()) {
    throw runtime_error("color table contents too large");
  }
  ctable->byteswap();

  // decode the color image
  Image img = decode_color_image(*pixmap_header, *pixmap_data, *ctable);

  return decoded_crsr(move(img), move(bitmap), header->hotspot_x,
      header->hotspot_y);
}



struct ppat_header {
  uint16_t type;
  uint32_t pixel_map_offset;
  uint32_t pixel_data_offset;
  uint32_t unused1; // used internally by QuickDraw apparently
  uint16_t unused2;
  uint32_t reserved;
  uint8_t monochrome_pattern[8];

  void byteswap() {
    this->type = bswap16(this->type);
    this->pixel_map_offset = bswap32(this->pixel_map_offset);
    this->pixel_data_offset = bswap32(this->pixel_data_offset);
  }
};

// note: we intentionally pass by value here so we can modify it while decoding
static pair<Image, Image> decode_ppat_data(string data) {
  if (data.size() < sizeof(ppat_header)) {
    throw runtime_error("ppat too small for header");
  }
  uint8_t* bdata = reinterpret_cast<uint8_t*>(const_cast<char*>(data.data()));

  ppat_header* header = reinterpret_cast<ppat_header*>(bdata);
  header->byteswap();

  Image monochrome_pattern = decode_monochrome_image(header->monochrome_pattern,
      8, 8, 8);

  // type 1 is a full-color pattern; types 0 and 2 apparently are only
  // monochrome
  if ((header->type == 0) || (header->type == 2)) {
    return make_pair(monochrome_pattern, monochrome_pattern);
  }
  if (header->type != 1) {
    throw runtime_error("unknown ppat type");
  }

  // get the pixel map header
  pixel_map_header* pixmap_header = reinterpret_cast<pixel_map_header*>(
      bdata + header->pixel_map_offset);
  if (header->pixel_map_offset + sizeof(*pixmap_header) > data.size()) {
    throw runtime_error("pixel map header too large");
  }
  pixmap_header->byteswap();

  // get the pixel map data
  size_t pixel_map_size = pixel_map_data::size(
      pixmap_header->flags_row_bytes & 0xFF, pixmap_header->h);
  if (header->pixel_data_offset + pixel_map_size > data.size()) {
    throw runtime_error("pixel map data too large");
  }
  pixel_map_data* pixmap_data = reinterpret_cast<pixel_map_data*>(
      bdata + header->pixel_data_offset);

  // get the color table
  color_table* ctable = reinterpret_cast<color_table*>(
      bdata + pixmap_header->color_table_offset);
  if (pixmap_header->color_table_offset + sizeof(*ctable) > data.size()) {
    throw runtime_error("color table header too large");
  }
  if (static_cast<int16_t>(bswap16(ctable->num_entries)) < 0) {
    throw runtime_error("color table has negative size");
  }
  if (pixmap_header->color_table_offset + ctable->size_swapped() > data.size()) {
    throw runtime_error("color table contents too large");
  }
  ctable->byteswap();

  // decode the color image
  Image pattern = decode_color_image(*pixmap_header, *pixmap_data, *ctable);

  return make_pair(move(pattern), move(monochrome_pattern));
}

pair<Image, Image> ResourceFile::decode_ppat(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  return decode_ppat_data(data);
}

vector<pair<Image, Image>> ResourceFile::decode_pptN(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);

  // these resources are composed of a 2-byte count field, then N 4-byte
  // offsets, then the ppat data
  if (data.size() < 2) {
    throw runtime_error("ppt# does not contain count field");
  }
  uint16_t count = bswap16(*reinterpret_cast<const uint16_t*>(data.data()));

  if (data.size() < 2 + sizeof(uint32_t) * count) {
    throw runtime_error("ppt# does not contain all offsets");
  }
  const uint32_t* r_offsets = reinterpret_cast<const uint32_t*>(data.data() + 2);

  vector<pair<Image, Image>> ret;
  for (size_t x = 0; x < count; x++) {
    uint32_t offset = bswap32(r_offsets[x]);
    uint32_t end_offset = (x == count - 1) ? data.size() : bswap32(r_offsets[x + 1]);
    if (offset >= data.size()) {
      throw runtime_error("offset is past end of resource data");
    }
    if (end_offset <= offset) {
      throw runtime_error("subpattern size is zero or negative");
    }
    string ppat_data = data.substr(offset, end_offset - offset);
    if (ppat_data.size() != end_offset - offset) {
      throw runtime_error("ppt# contains incorrect offsets");
    }
    ret.emplace_back(decode_ppat_data(ppat_data));
  }
  return ret;
}

Image ResourceFile::decode_pat(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() != 8) {
    throw runtime_error("PAT not exactly 8 bytes in size");
  }
  return decode_monochrome_image(data.data(), data.size(), 8, 8);
}

struct patN_header {
  uint16_t num_patterns;
  uint64_t pattern_data[0];
};

vector<Image> ResourceFile::decode_patN(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() < 2) {
    throw runtime_error("PAT# not large enough for count");
  }
  uint16_t num_patterns = bswap16(*reinterpret_cast<const uint16_t*>(data.data()));

  vector<Image> ret;
  while (ret.size() < num_patterns) {
    size_t offset = 2 + ret.size() * 8;
    if (offset > data.size() - 8) {
      throw runtime_error("PAT# not large enough for all data");
    }
    const uint8_t* bdata = reinterpret_cast<const uint8_t*>(data.data()) + offset;
    ret.emplace_back(decode_monochrome_image(bdata, 8, 8, 8));
  }

  return ret;
}

vector<Image> ResourceFile::decode_sicn(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);

  // so simple, there isn't even a header struct!
  // SICN resources are just several 0x20-byte monochrome images concatenated
  // together

  if (data.size() & 0x1F) {
    throw runtime_error("SICN size not a multiple of 32");
  }

  vector<Image> ret;
  while (ret.size() < (data.size() >> 5)) {
    const uint8_t* bdata = reinterpret_cast<const uint8_t*>(data.data()) +
        (ret.size() * 0x20);
    ret.emplace_back(decode_monochrome_image(bdata, 0x20, 16, 16));
  }

  return ret;
}

static Image apply_alpha_from_mask(const Image& img, const Image& mask) {
  if ((img.get_width() != mask.get_width()) || (img.get_height() != mask.get_height())) {
    throw runtime_error("image and mask dimensions are unequal");
  }

  Image ret(img.get_width(), img.get_height(), true);
  for (size_t y = 0; y < img.get_height(); y++) {
    for (size_t x = 0; x < img.get_width(); x++) {
      uint8_t r, g, b, a;
      img.read_pixel(x, y, &r, &g, &b, NULL);
      mask.read_pixel(x, y, NULL, NULL, NULL, &a);
      ret.write_pixel(x, y, r, g, b, a);
    }
  }
  return ret;
}

Image ResourceFile::decode_ics8(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  Image decoded = decode_8bit_image(data.data(), data.size(), 16, 16);
  try {
    Image mask = this->decode_icsN(id, RESOURCE_TYPE_ICSN);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_icl8(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  Image decoded = decode_8bit_image(data.data(), data.size(), 32, 32);
  try {
    Image mask = this->decode_icnN(id, RESOURCE_TYPE_ICNN);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_ics4(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  Image decoded = decode_4bit_image(data.data(), data.size(), 16, 16);
  try {
    Image mask = this->decode_icsN(id, RESOURCE_TYPE_ICSN);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_icl4(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  Image decoded = decode_4bit_image(data.data(), data.size(), 32, 32);
  try {
    Image mask = this->decode_icnN(id, RESOURCE_TYPE_ICNN);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_icon(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  return decode_monochrome_image(data.data(), data.size(), 32, 32);
}

struct curs_header { // kind of a misnomer; this is actually the entire thing
  uint8_t bitmap[0x20];
  uint8_t mask[0x20];
  uint16_t hotspot_x;
  uint16_t hotspot_y;

  void byteswap() {
    this->hotspot_x = bswap16(this->hotspot_x);
    this->hotspot_y = bswap16(this->hotspot_y);
  }
};

ResourceFile::decoded_curs ResourceFile::decode_curs(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);

  // these should always be the same size
  if (data.size() < 0x40) {
    throw runtime_error("CURS resource is too small");
  }
  curs_header* header = reinterpret_cast<curs_header*>(const_cast<char*>(data.data()));
  header->byteswap();

  Image img = decode_monochrome_image_masked(header, 0x40, 16, 16);
  return decoded_curs(move(img), (data.size() >= 0x42) ? header->hotspot_x : 0xFFFF,
      (data.size() >= 0x44) ? header->hotspot_y : 0xFFFF);
}

Image ResourceFile::decode_icnN(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  return decode_monochrome_image_masked(data.data(), data.size(), 32, 32);
}

Image ResourceFile::decode_icsN(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  return decode_monochrome_image_masked(data.data(), data.size(), 16, 16);
}

Image ResourceFile::decode_pict(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);

  char temp_filename[36] = "/tmp/resource_dasm.XXXXXXXXXXXX";
  {
    int fd = mkstemp(temp_filename);
    auto f = fdopen_unique(fd, "wb");
    fwrite(data.data(), data.size(), 1, f.get());
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

struct pltt_entry {
  uint16_t r;
  uint16_t g;
  uint16_t b;
  uint16_t unknown[5];
};

vector<Color> ResourceFile::decode_pltt(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);

  if (data.size() < sizeof(pltt_entry)) {
    throw runtime_error("pltt too small for header");
  }

  // pltt resources have a 16-byte header, which is coincidentally also the size
  // of each entry. I'm lazy so we'll just load it all at once and use the first
  // "entry" instead of manually making a header struct
  const pltt_entry* pltt = reinterpret_cast<const pltt_entry*>(data.data());

  // the first header word is the entry count; the rest of the header seemingly
  // doesn't matter at all
  uint16_t count = bswap16(pltt->r);
  if (data.size() < sizeof(pltt_entry) * (count + 1)) {
    throw runtime_error("pltt too small for all entries");
  }

  vector<Color> ret;
  for (size_t x = 1; x < count + 1; x++) {
    ret.emplace_back(bswap16(pltt[x].r), bswap16(pltt[x].g), bswap16(pltt[x].b));
  }
  return ret;
}

struct clut_entry {
  uint16_t index;
  uint16_t r;
  uint16_t g;
  uint16_t b;
};

vector<Color> ResourceFile::decode_clut(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);

  if (data.size() < sizeof(clut_entry)) {
    throw runtime_error("clut too small for header");
  }

  // clut resources have an 8-byte header, which is coincidentally also the size
  // of each entry. I'm lazy so we'll just load it all at once and use the first
  // "entry" instead of manually making a header struct
  const clut_entry* clut = reinterpret_cast<const clut_entry*>(data.data());

  // the last header word is the entry count; the rest of the header seemingly
  // doesn't matter at all
  uint16_t count = bswap16(clut->b);
  if (data.size() < sizeof(clut_entry) * (count + 1)) {
    throw runtime_error("clut too small for all entries");
  }

  // unlike for pltt resources, clut counts are inclusive - there are actually
  // (count + 1) colors
  vector<Color> ret;
  for (size_t x = 1; x <= count + 1; x++) {
    ret.emplace_back(bswap16(clut[x].r), bswap16(clut[x].g), bswap16(clut[x].b));
  }
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// sound decoding

struct wav_header {
  uint32_t riff_magic;   // 0x52494646 ('RIFF')
  uint32_t file_size;    // size of file - 8
  uint32_t wave_magic;   // 0x57415645

  uint32_t fmt_magic;    // 0x666d7420 ('fmt ')
  uint32_t fmt_size;     // 16
  uint16_t format;       // 1 = PCM
  uint16_t num_channels;
  uint32_t sample_rate;
  uint32_t byte_rate;    // num_channels * sample_rate * bits_per_sample / 8
  uint16_t block_align;  // num_channels * bits_per_sample / 8
  uint16_t bits_per_sample;

  union {
    struct {
      uint32_t smpl_magic;
      uint32_t smpl_size;
      uint32_t manufacturer;
      uint32_t product;
      uint32_t sample_period;
      uint32_t base_note;
      uint32_t pitch_fraction;
      uint32_t smtpe_format;
      uint32_t smtpe_offset;
      uint32_t num_loops; // = 1
      uint32_t sampler_data;

      uint32_t loop_cue_point_id; // can be zero? we'll only have at most one loop in this context
      uint32_t loop_type; // 0 = normal, 1 = ping-pong, 2 = reverse
      uint32_t loop_start; // start and end are byte offsets into the wave data, not sample indexes
      uint32_t loop_end;
      uint32_t loop_fraction; // fraction of a sample to loop (0)
      uint32_t loop_play_count; // 0 = loop forever

      uint32_t data_magic;   // 0x64617461 ('data')
      uint32_t data_size;    // num_samples * num_channels * bits_per_sample / 8
      uint8_t data[0];
    } with_loop;

    struct {
      uint32_t data_magic;   // 0x64617461 ('data')
      uint32_t data_size;    // num_samples * num_channels * bits_per_sample / 8
      uint8_t data[0];
    } without_loop;
  };

  wav_header(uint32_t num_samples, uint16_t num_channels, uint32_t sample_rate,
      uint16_t bits_per_sample, uint32_t loop_start = 0, uint32_t loop_end = 0,
      uint8_t base_note = 0x3C) {

    this->riff_magic = bswap32(0x52494646);
    // this->file_size is set below (it depends on whether there's a loop)
    this->wave_magic = bswap32(0x57415645);
    this->fmt_magic = bswap32(0x666D7420);
    this->fmt_size = 16;
    this->format = 1;
    this->num_channels = num_channels;
    this->sample_rate = sample_rate;
    this->byte_rate = num_channels * sample_rate * bits_per_sample / 8;
    this->block_align = num_channels * bits_per_sample / 8;
    this->bits_per_sample = bits_per_sample;

    if (((loop_start > 0) && (loop_end > 0)) || (base_note != 0x3C) || (base_note != 0)) {
      this->file_size = num_samples * num_channels * bits_per_sample / 8 +
          sizeof(*this) - 8;

      this->with_loop.smpl_magic = bswap32(0x736D706C);
      this->with_loop.smpl_size = 0x3C;
      this->with_loop.manufacturer = 0;
      this->with_loop.product = 0;
      this->with_loop.sample_period = 1000000000 / this->sample_rate;
      this->with_loop.base_note = base_note;
      this->with_loop.pitch_fraction = 0;
      this->with_loop.smtpe_format = 0;
      this->with_loop.smtpe_offset = 0;
      this->with_loop.num_loops = 1;
      this->with_loop.sampler_data = 0x18; // includes the loop struct below

      this->with_loop.loop_cue_point_id = 0;
      this->with_loop.loop_type = 0; // 0 = normal, 1 = ping-pong, 2 = reverse

      // note: loop_start and loop_end are given to this function as sample
      // offsets, but in the wav file, they should be byte offsets
      this->with_loop.loop_start = loop_start * (bits_per_sample >> 3);
      this->with_loop.loop_end = loop_end * (bits_per_sample >> 3);

      this->with_loop.loop_fraction = 0;
      this->with_loop.loop_play_count = 0; // 0 = loop forever

      this->with_loop.data_magic = bswap32(0x64617461);
      this->with_loop.data_size = num_samples * num_channels * bits_per_sample / 8;

    } else {
      // with_loop is longer than without_loop so we correct for the size
      // disparity manually here
      const uint32_t header_size = sizeof(*this) - sizeof(this->with_loop) +
          sizeof(this->without_loop);
      this->file_size = num_samples * num_channels * bits_per_sample / 8 +
          header_size - 8;

      this->without_loop.data_magic = bswap32(0x64617461);
      this->without_loop.data_size = num_samples * num_channels * bits_per_sample / 8;
    }
  }

  bool has_loop() const {
    return (this->with_loop.smpl_magic == bswap32(0x736D706C));
  }

  size_t size() const {
    if (this->has_loop()) {
      return sizeof(*this);
    } else {
      return sizeof(*this) - sizeof(this->with_loop) + sizeof(this->without_loop);
    }
  }

  uint32_t get_data_size() const {
    if (this->has_loop()) {
      return this->with_loop.data_size;
    } else {
      return this->without_loop.data_size;
    }
  }
};

struct snd_resource_header_format2 {
  uint16_t format_code; // = 2
  uint16_t reference_count;
  uint16_t num_commands;

  void byteswap() {
    this->format_code = bswap16(this->format_code);
    this->reference_count = bswap16(this->reference_count);
    this->num_commands = bswap16(this->num_commands);
  }
};

struct snd_resource_header_format1 {
  uint16_t format_code; // = 1
  uint16_t data_format_count; // we only support 0 or 1 here
  uint16_t data_format_id; // we only support 5 here (sampled sound)
  uint32_t flags; // 0x40 = stereo
  uint16_t num_commands;

  void byteswap() {
    this->format_code = bswap16(this->format_code);
    this->data_format_count = bswap16(this->data_format_count);
    this->data_format_id = bswap16(this->data_format_id);
    this->flags = bswap32(this->flags);
    this->num_commands = bswap16(this->num_commands);
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
    this->command = bswap16(this->command);
    this->param1 = bswap16(this->param1);
    this->param2 = bswap32(this->param2);
  }
};

struct snd_sample_buffer {
  uint32_t data_offset; // from end of this struct
  uint32_t data_bytes;
  uint32_t sample_rate;
  uint32_t loop_start;
  uint32_t loop_end;
  uint8_t encoding;
  uint8_t base_note;

  uint8_t data[0];

  void byteswap() {
    this->data_offset = bswap32(this->data_offset);
    this->data_bytes = bswap32(this->data_bytes);
    this->sample_rate = bswap32(this->sample_rate);
    this->loop_start = bswap32(this->loop_start);
    this->loop_end = bswap32(this->loop_end);
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
    this->num_frames = bswap32(this->num_frames);
    this->marker_chunk = bswap32(this->marker_chunk);
    this->format = bswap32(this->format);
    this->reserved1 = bswap32(this->reserved1);
    this->state_vars = bswap32(this->state_vars);
    this->left_over_block_ptr = bswap32(this->left_over_block_ptr);
    this->compression_id = bswap16(this->compression_id);
    this->packet_size = bswap16(this->packet_size);
    this->synth_id = bswap16(this->synth_id);
    this->bits_per_sample = bswap16(this->bits_per_sample);
  }
};

string ResourceFile::decode_snd(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() < 2) {
    throw runtime_error("snd doesn\'t even contain a format code");
  }
  uint16_t format_code = bswap16(*reinterpret_cast<const uint16_t*>(data.data()));
  uint8_t* bdata = reinterpret_cast<uint8_t*>(const_cast<char*>(data.data()));

  // parse the resource header
  int num_channels = 1;
  size_t commands_offset;
  size_t num_commands;
  if (format_code == 0x0001) {
    if (data.size() < sizeof(snd_resource_header_format1)) {
      throw runtime_error("snd is too small to contain format 1 resource header");
    }
    snd_resource_header_format1* header = reinterpret_cast<snd_resource_header_format1*>(bdata);
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
    if (data.size() < sizeof(snd_resource_header_format2)) {
      throw runtime_error("snd is too small to contain format 2 resource header");
    }
    snd_resource_header_format2* header = reinterpret_cast<snd_resource_header_format2*>(bdata);
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
  if (command_end_offset > data.size()) {
    throw runtime_error("snd contains more commands than fit in resource");
  }

  size_t sample_buffer_offset = 0;
  snd_command* commands = reinterpret_cast<snd_command*>(bdata + commands_offset);
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
  if (sample_buffer_offset + sizeof(snd_sample_buffer) > data.size()) {
    throw runtime_error("sample buffer is outside snd resource");
  }
  snd_sample_buffer* sample_buffer = reinterpret_cast<snd_sample_buffer*>(bdata + sample_buffer_offset);
  sample_buffer->byteswap();
  uint16_t sample_rate = sample_buffer->sample_rate >> 16;

  // uncompressed data can be copied verbatim
  if (sample_buffer->encoding == 0x00) {
    if (sample_buffer->data_bytes == 0) {
      throw runtime_error("snd contains no samples");
    }

    size_t available_data = data.size() - ((const uint8_t*)sample_buffer->data - (const uint8_t*)bdata);
    if (available_data < sample_buffer->data_bytes) {
      sample_buffer->data_bytes = available_data;
    }

    wav_header wav(sample_buffer->data_bytes, num_channels, sample_rate, 8,
        sample_buffer->loop_start, sample_buffer->loop_end,
        sample_buffer->base_note);

    string ret;
    ret.append(reinterpret_cast<const char*>(&wav), wav.size());
    ret.append(reinterpret_cast<const char*>(sample_buffer->data), sample_buffer->data_bytes);
    return ret;

  // compressed data will need to be processed somehow... sigh
  } else if ((sample_buffer->encoding == 0xFE) || (sample_buffer->encoding == 0xFF)) {
    if (data.size() < sample_buffer_offset + sizeof(snd_sample_buffer) + sizeof(snd_compressed_buffer)) {
      throw runtime_error("snd is too small to contain compressed buffer");
    }
    snd_compressed_buffer* compressed_buffer = reinterpret_cast<snd_compressed_buffer*>(bdata + sample_buffer_offset + sizeof(snd_sample_buffer));
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
        uint32_t loop_factor = is_mace3 ? 3 : 6;

        wav_header wav(decoded_samples.size(), num_channels, sample_rate, 16,
            sample_buffer->loop_start * loop_factor,
            sample_buffer->loop_end * loop_factor, sample_buffer->base_note);
        if (wav.get_data_size() != 2 * decoded_samples.size()) {
          throw runtime_error("computed data size does not match decoded data size");
        }

        string ret;
        ret.append(reinterpret_cast<const char*>(&wav), wav.size());
        ret.append(reinterpret_cast<const char*>(decoded_samples.data()), wav.get_data_size());
        return ret;
      }

      case 0xFFFF:

        // 'twos' and 'sowt' are equivalent to no compression and fall through
        // to the uncompressed case below. for all others, we'll have to
        // decompress somehow
        if ((compressed_buffer->format != 0x74776F73) && (compressed_buffer->format != 0x736F7774)) {
          vector<int16_t> decoded_samples;

          uint32_t loop_factor;
          if (compressed_buffer->format == 0x696D6134) { // ima4
            decoded_samples = decode_ima4(compressed_buffer->data,
                compressed_buffer->num_frames * 34 * num_channels,
                num_channels == 2);
            loop_factor = 4; // TODO: verify this. I don't actually have any examples right now
          } else if (compressed_buffer->format == 0x756C6177) { // ulaw
            decoded_samples = decode_ulaw(compressed_buffer->data,
                compressed_buffer->num_frames);
            loop_factor = 2;
          } else {
            throw runtime_error(string_printf("snd uses unknown compression (%08" PRIX32 ")",
                compressed_buffer->format));
          }

          wav_header wav(decoded_samples.size() / num_channels, num_channels,
              sample_rate, 16, sample_buffer->loop_start * loop_factor,
              sample_buffer->loop_end * loop_factor, sample_buffer->base_note);
          if (wav.get_data_size() != 2 * decoded_samples.size()) {
            throw runtime_error(string_printf(
              "computed data size (%" PRIu32 ") does not match decoded data size (%zu)",
              wav.get_data_size(), 2 * decoded_samples.size()));
          }

          string ret;
          ret.append(reinterpret_cast<const char*>(&wav), wav.size());
          ret.append(reinterpret_cast<const char*>(decoded_samples.data()), wav.get_data_size());
          return ret;
        }

        // intentional fallthrough to uncompressed case

      case 0: { // no compression
        uint32_t num_samples = compressed_buffer->num_frames;
        uint16_t bits_per_sample = compressed_buffer->bits_per_sample;
        if (bits_per_sample == 0) {
          bits_per_sample = compressed_buffer->state_vars >> 16;
        }

        size_t available_data = data.size() - ((const uint8_t*)compressed_buffer->data - (const uint8_t*)bdata);

        // hack: if the sound is stereo and the computed data size is exactly
        // twice the available data size, treat it as mono
        if ((num_channels == 2) && (
            num_samples * num_channels * (bits_per_sample / 8)) == 2 * available_data) {
          num_channels = 1;
        }

        wav_header wav(num_samples, num_channels, sample_rate, bits_per_sample,
            sample_buffer->loop_start, sample_buffer->loop_end,
            sample_buffer->base_note);
        if (wav.get_data_size() == 0) {
          throw runtime_error(string_printf(
            "computed data size is zero (%" PRIu32 " samples, %d channels, %" PRIu16 " kHz, %" PRIu16 " bits per sample)",
            num_samples, num_channels, sample_rate, bits_per_sample));
        }
        if (wav.get_data_size() > available_data) {
          throw runtime_error(string_printf("computed data size exceeds actual data (%" PRIu32 " computed, %zu available)",
              wav.get_data_size(), available_data));
        }

        string ret;
        ret.append(reinterpret_cast<const char*>(&wav), wav.size());
        ret.append(reinterpret_cast<const char*>(compressed_buffer->data), wav.get_data_size());

        // byteswap the samples if it's 16-bit and not 'swot'
        if ((wav.bits_per_sample == 0x10) && (compressed_buffer->format != 0x736F7774)) {
          uint16_t* samples = const_cast<uint16_t*>(reinterpret_cast<const uint16_t*>(
              ret.data() + wav.size()));
          for (uint32_t x = 0; x < wav.get_data_size() / 2; x++) {
            samples[x] = bswap16(samples[x]);
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



////////////////////////////////////////////////////////////////////////////////
// sequenced music decoding

struct inst_header {
  struct key_region {
    // low/high are inclusive
    uint8_t key_low;
    uint8_t key_high;

    int16_t snd_id;
    int16_t unknown[2];

    void byteswap() {
      this->snd_id = bswap16(this->snd_id);
    }
  };

  int16_t snd_id;
  uint16_t base_note; // unsure about this; seems to be zero a lot
  int16_t unknown[4];
  uint16_t num_key_regions;
  key_region key_regions[0];

  void byteswap() {
    this->snd_id = bswap16(this->snd_id);
    this->base_note = bswap16(this->base_note);
    this->num_key_regions = bswap16(this->num_key_regions);
    for (size_t x = 0; x < this->num_key_regions; x++) {
      this->key_regions[x].byteswap();
    }
  }
};

ResourceFile::decoded_inst ResourceFile::decode_inst(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() < sizeof(inst_header)) {
    throw runtime_error("INST too small for header");
  }

  inst_header* header = reinterpret_cast<inst_header*>(const_cast<char*>(data.data()));
  if (sizeof(inst_header) + (bswap16(header->num_key_regions) * sizeof(inst_header::key_region)) > data.size()) {
    throw runtime_error("INST too small for data");
  }
  header->byteswap();

  decoded_inst ret;
  if (header->num_key_regions == 0) {
    ret.key_regions.emplace_back(0x00, 0x7F, header->base_note, header->snd_id);
  } else {
    for (size_t x = 0; x < header->num_key_regions; x++) {
      const auto& rgn = header->key_regions[x];
      // TODO: it's probably wrong to use header->base_note here
      ret.key_regions.emplace_back(rgn.key_low, rgn.key_high, header->base_note, rgn.snd_id);
    }
  }

  return ret;
}



struct song_header {
  struct inst_override {
    uint16_t midi_channel_id;
    uint16_t inst_resource_id;

    void byteswap() {
      this->midi_channel_id = bswap16(this->midi_channel_id);
      this->inst_resource_id = bswap16(this->inst_resource_id);
    }
  };

  int16_t midi_id;
  uint16_t unknown[7];
  uint16_t instrument_override_count;
  inst_override instrument_overrides[0];

  void byteswap() {
    this->midi_id = bswap16(this->midi_id);
    this->instrument_override_count = bswap16(this->instrument_override_count);
    for (size_t x = 0; x < this->instrument_override_count; x++) {
      this->instrument_overrides[x].byteswap();
    }
  }
};

ResourceFile::decoded_song ResourceFile::decode_song(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() < sizeof(song_header)) {
    throw runtime_error("SONG too small for header");
  }

  song_header* header = reinterpret_cast<song_header*>(const_cast<char*>(data.data()));
  if (sizeof(song_header) + (bswap16(header->instrument_override_count) * sizeof(song_header::inst_override)) > data.size()) {
    throw runtime_error("SONG too small for data");
  }
  header->byteswap();

  decoded_song ret;
  ret.midi_id = header->midi_id;
  for (size_t x = 0; x < header->instrument_override_count; x++) {
    const auto& override = header->instrument_overrides[x];
    ret.instrument_overrides.emplace(override.midi_channel_id, override.inst_resource_id);
  }
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// string decoding

vector<string> ResourceFile::decode_strN(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() < 2) {
    throw runtime_error("STR# size is too small");
  }

  const char* cdata = data.data() + sizeof(uint16_t); // ignore the count; just read all of them
  size_t size = data.size() - 2;

  vector<string> ret;
  while (size > 0) {
    uint8_t len = *(uint8_t*)cdata;
    cdata++;
    size--;
    if (len > size) {
      throw runtime_error("corrupted STR# resource");
    }

    ret.emplace_back(cdata, len);
    cdata += len;
    size -= len;
  }

  return ret;
}

pair<string, string> ResourceFile::decode_str(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.empty()) {
    return make_pair("", "");
  }

  uint8_t len = static_cast<uint8_t>(data[0]);
  if (len > data.size() - 1) {
    throw runtime_error("length is too large for data");
  }

  return make_pair(data.substr(1, len), data.substr(len + 1));
}

string ResourceFile::decode_text(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  for (auto& ch : data) {
    if (ch == '\r') {
      ch = '\n';
    }
  }
  return data;
}

static const unordered_map<uint16_t, string> standard_font_ids({
  {0, "Chicago"},
  {1, "Helvetica"}, // this is actually "inherit"
  {2, "New York"},
  {3, "Geneva"},
  {4, "Monaco"},
  {5, "Venice"},
  {6, "London"},
  {7, "Athens"},
  {8, "San Francisco"},
  {9, "Toronto"},
  {11, "Cairo"},
  {12, "Los Angeles"},
  {13, "Zapf Dingbats"},
  {14, "Bookman"},
  {15, "N Helvetica Narrow"},
  {16, "Palatino"},
  {18, "Zapf Chancery"},
  {20, "Times"},
  {21, "Helvetica"},
  {22, "Courier"},
  {23, "Symbol"},
  {24, "Taliesin"},
  {33, "Avant Garde"},
  {34, "New Century Schoolbook"},
  {169, "O Futura BookOblique"},
  {173, "L Futura Light"},
  {174, "Futura"},
  {176, "H Futura Heavy"},
  {177, "O Futura Oblique"},
  {179, "BO Futura BoldOblique"},
  {221, "HO Futura HeavyOblique"},
  {258, "ProFont"},
  {260, "LO Futura LightOblique"},
  {513, "ISO Latin Nr 1"},
  {514, "PCFont 437"},
  {515, "PCFont 850"},
  {1029, "VT80 Graphics"},
  {1030, "3270 Graphics"},
  {1109, "Trebuchet MS"},
  {1345, "ProFont"},
  {1895, "Nu Sans Regular"},
  {2001, "Arial"},
  {2002, "Charcoal"},
  {2003, "Capitals"},
  {2004, "Sand"},
  {2005, "Courier New"},
  {2006, "Techno"},
  {2010, "Times New Roman"},
  {2011, "Wingdings"},
  {2013, "Hoefler Text"},
  {2018, "Hoefler Text Ornaments"},
  {2039, "Impact"},
  {2040, "Skia"},
  {2305, "Textile"},
  {2307, "Gadget"},
  {2311, "Apple Chancery"},
  {2515, "MT Extra"},
  {4513, "Comic Sans MS"},
  {7092, "Monotype.com"},
  {7102, "Andale Mono"},
  {7203, "Verdana"},
  {9728, "Espi Sans"},
  {9729, "Charcoal"},
  {9840, "Espy Sans/Copland"},
  {9841, "Espi Sans Bold"},
  {9842, "Espy Sans Bold/Copland"},
  {10840, "Klang MT"},
  {10890, "Script MT Bold"},
  {10897, "Old English Text MT"},
  {10909, "New Berolina MT"},
  {10957, "Bodoni MT Ultra Bold"},
  {10967, "Arial MT Condensed Light"},
  {11103, "Lydian MT"},
  {12077, "Arial Black"},
  {12171, "Georgia"},
  {14868, "B Futura Bold"},
  {14870, "Futura Book"},
  {15011, "Gill Sans Condensed Bold"},
  {16383, "Chicago"},
});

enum style_flag {
  Bold = 0x01,
  Italic = 0x02,
  Underline = 0x04,
  Outline = 0x08,
  Shadow = 0x10,
  Condensed = 0x20,
  Extended = 0x40,
};

struct styl_command {
  uint32_t offset;
  // these two fields seem to scale with size; they might be line/char spacing
  uint16_t unknown1;
  uint16_t unknown2;
  uint16_t font_id;
  uint16_t style_flags;
  uint16_t size;
  uint16_t r;
  uint16_t g;
  uint16_t b;

  void byteswap() {
    this->offset = bswap32(this->offset);
    this->unknown1 = bswap16(this->unknown1);
    this->unknown2 = bswap16(this->unknown2);
    this->font_id = bswap16(this->font_id);
    this->style_flags = bswap16(this->style_flags);
    this->size = bswap16(this->size);
    this->r = bswap16(this->r);
    this->g = bswap16(this->g);
    this->b = bswap16(this->b);
  }
};

string ResourceFile::decode_styl(int16_t id, uint32_t type) {
  // get the text now, so we'll fail early if there's no resource
  string text;
  try {
    text = this->decode_text(id, RESOURCE_TYPE_TEXT);
  } catch (const out_of_range&) {
    throw runtime_error("style has no corresponding TEXT");
  }

  string data = this->get_resource_data(type, id);
  if (data.size() < 2) {
    throw runtime_error("styl size is too small");
  }

  uint16_t num_commands = bswap16(*reinterpret_cast<const uint16_t*>(data.data()));
  if (data.size() < 2 + num_commands * sizeof(styl_command)) {
    throw runtime_error("styl size is too small for all commands");
  }

  const styl_command* cmds = reinterpret_cast<const styl_command*>(data.data() + 2);

  string ret = "{\\rtf1\\ansi\n{\\fonttbl";

  // collect all the fonts and write the font table
  map<uint16_t, uint16_t> font_table;
  for (size_t x = 0; x < num_commands; x++) {
    styl_command cmd = cmds[x];
    cmd.byteswap();

    size_t font_table_entry = font_table.size();
    if (font_table.emplace(cmd.font_id, font_table_entry).second) {
      string font_name;
      try {
        font_name = standard_font_ids.at(cmd.font_id);
      } catch (const out_of_range&) {
        // TODO: this is a bad assumption
        font_name = "Helvetica";
      }
      // TODO: we shouldn't say every font is a swiss font
      ret += string_printf("\\f%zu\\fswiss %s;", font_table_entry, font_name.c_str());
    }
  }
  ret += "}\n{\\colortbl";

  // collect all the colors and write the color table
  map<uint64_t, uint16_t> color_table;
  for (size_t x = 0; x < num_commands; x++) {
    styl_command cmd = cmds[x];
    cmd.byteswap();

    Color c(cmd.r, cmd.g, cmd.b);

    size_t color_table_entry = color_table.size();
    if (color_table.emplace(c.to_u64(), color_table_entry).second) {
      ret += string_printf("\\red%hu\\green%hu\\blue%hu;", c.r >> 8, c.g >> 8, c.b >> 8);
    }
  }
  ret += "}\n";

  // write the stylized blocks
  for (size_t x = 0; x < num_commands; x++) {
    styl_command cmd = cmds[x];
    cmd.byteswap();

    uint32_t offset = cmd.offset;
    uint32_t end_offset = (x == num_commands - 1) ? text.size() : bswap32(cmds[x + 1].offset);
    if (offset >= text.size()) {
      throw runtime_error("offset is past end of TEXT resource data");
    }
    if (end_offset <= offset) {
      throw runtime_error("block size is zero or negative");
    }
    string text_block = text.substr(offset, end_offset - offset);

    // TODO: we can produce smaller files by omitting commands for parts of the
    // format that haven't changed
    size_t font_id = font_table.at(cmd.font_id);
    size_t color_id = color_table.at(Color(cmd.r, cmd.g, cmd.b).to_u64());
    ssize_t expansion = 0;
    if (cmd.style_flags & style_flag::Condensed) {
      expansion = -cmd.size / 2;
    } else if (cmd.style_flags & style_flag::Extended) {
      expansion = cmd.size / 2;
    }
    ret += string_printf("\\f%zu\\%s\\%s\\%s\\%s\\fs%zu \\cf%zu \\expan%zd ",
        font_id, (cmd.style_flags & style_flag::Bold) ? "b" : "b0",
        (cmd.style_flags & style_flag::Italic) ? "i" : "i0",
        (cmd.style_flags & style_flag::Outline) ? "outl" : "outl0",
        (cmd.style_flags & style_flag::Outline) ? "shad" : "shad0",
        cmd.size * 2, color_id, expansion);
    if (cmd.style_flags & style_flag::Underline) {
      ret += string_printf("\\ul \\ulc%zu ", color_id);
    } else {
      ret += "\\ul0 ";
    }

    for (char ch : text_block) {
      if (ch & 0x80) {
        throw runtime_error("non-ASCII text cannot be styled");
      }
      if (ch == '\\') {
        ret += "\\\\";
      } else if (ch == '\n') {
        ret += "\\line ";
      } else {
        ret += ch;
      }
    }
  }
  ret += "}";

  return ret;
}


////////////////////////////////////////////////////////////////////////////////
// single resource file implementation

SingleResourceFile::SingleResourceFile(uint32_t type, int16_t id,
    const void* data, size_t size) : ResourceFile(NULL), type(type), id(id),
    data(reinterpret_cast<const char*>(data), size) { }

SingleResourceFile::SingleResourceFile(uint32_t type, int16_t id,
    const string& data) : ResourceFile(NULL), type(type), id(id), data(data) { }

bool SingleResourceFile::resource_exists(uint32_t type, int16_t id) {
  return (type == this->type) && (id == this->id);
}

string SingleResourceFile::get_resource_data(uint32_t type, int16_t id,
    bool decompress, DebuggingMode decompress_debug) {
  if ((type != this->type) || (id != this->id)) {
    throw out_of_range("file doesn\'t contain resource with the given id");
  }
  return this->data;
}

bool SingleResourceFile::resource_is_compressed(uint32_t type, int16_t id) {
  return false;
}

vector<int16_t> SingleResourceFile::all_resources_of_type(uint32_t type) {
  if (type == this->type) {
    return {this->id};
  }
  return {};
}

vector<pair<uint32_t, int16_t>> SingleResourceFile::all_resources() {
  return {make_pair(this->type, this->id)};
}



#pragma pack(pop)
