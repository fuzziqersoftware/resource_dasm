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
#include <phosg/Time.hh>
#include <stdexcept>
#include <vector>
#include <string>

#include "audio_codecs.hh"
#include "quickdraw_formats.hh"
#include "mc68k.hh"
#include "pict.hh"

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



ResourceFile::ResourceFile(const string& filename) : ResourceFile(filename.c_str()) { }

ResourceFile::ResourceFile(const char* filename) : empty(false) {
  if (filename == NULL) {
    this->empty = true;
    return;
  }
  this->fd = scoped_fd(filename, O_RDONLY);
  // if the resource fork is empty, treat it as a valid index with no contents
  if (fstat(this->fd).st_size == 0) {
    this->empty = true;
    return;
  }
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
  uint16_t header_size; // may be zero apparently
  uint8_t header_version; // 8 or 9
  uint8_t attributes; // bit 0 specifies compression

  // note: the kreativekorp definition is missing this field
  uint32_t decompressed_size;

  union {
    struct {
      uint8_t working_buffer_fractional_size; // length of compressed data relative to length of uncompressed data, out of 256
      uint8_t expansion_buffer_size; // greatest number of bytes compressed data will grow while being decompressed
      int16_t dcmp_resource_id;
      uint16_t unused;
    } header8;

    struct {
      uint16_t dcmp_resource_id;
      uint16_t unused1;
      uint8_t unused2;
      uint8_t unused3;
    } header9;
  };

  void byteswap() {
    this->magic = bswap32(this->magic);
    this->header_size = bswap16(this->header_size);
    this->decompressed_size = bswap32(this->decompressed_size);

    if (this->header_version & 1) {
      this->header9.dcmp_resource_id = bswap16(this->header9.dcmp_resource_id);

    } else {
      this->header8.dcmp_resource_id = bswap16(this->header8.dcmp_resource_id);
    }
  }
};

struct dcmp_input_header {
  // this is used to tell the program where to "return" to
  uint32_t return_addr;

  // actual parameters to the decompressor
  union {
    struct { // used when header_version == 9
      uint32_t source_resource_header;
      uint32_t dest_buffer_addr;
      uint32_t source_buffer_addr;
      uint32_t data_size;
    } arguments1;
    struct { // used when header_version == 8
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
    fprintf(stderr, "warning: resource marked as compressed but is too small\n");
    return data;
  }

  compressed_resource_header header;
  memcpy(&header, data.data(), sizeof(compressed_resource_header));
  header.byteswap();
  if (header.magic != 0xA89F6572) {
    fprintf(stderr, "warning: resource marked as compressed but does not appear to be compressed\n");
    return data;
  }

  int16_t dcmp_resource_id;
  if (header.header_version == 9) {
    dcmp_resource_id = header.header9.dcmp_resource_id;
  } else if (header.header_version == 8) {
    dcmp_resource_id = header.header8.dcmp_resource_id;
  } else {
    throw runtime_error("compressed resource header version is not 8 or 9");
  }
  if ((debug != DebuggingMode::Disabled) && (debug != DebuggingMode::Passive)) {
    fprintf(stderr, "using dcmp %hd\n", dcmp_resource_id);
    fprintf(stderr, "resource header looks like:\n");
    print_data(stderr, data.data(), data.size() > 0x40 ? 0x40 : data.size());
    fprintf(stderr, "note: data size is %zu (0x%zX); decompressed data size is %" PRIu32 " (0x%" PRIX32 ") bytes\n",
        data.size(), data.size(), header.decompressed_size, header.decompressed_size);
  }

  // get the decompressor code. if it's not in the file, look in system as well
  string dcmp_contents;
  try {
    dcmp_contents = this->get_resource_data(RESOURCE_TYPE_dcmp, dcmp_resource_id);
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
  if ((debug != DebuggingMode::Disabled) && (debug != DebuggingMode::Passive)) {
    fprintf(stderr, "dcmp entry offset is %08" PRIX32 "\n", dcmp_entry_offset);
  }

  MC68KEmulator emu;

  // set up memory regions
  // slightly awkward assumption: decompressed data is never more than 256 times
  // the size of the input data. TODO: it looks like we probably should be using
  // ((data.size() * 256) / working_buffer_fractional_size) instead here?
  static const uint32_t stack_base = 0x10000000;
  static const uint32_t output_base = 0x20000000;
  static const uint32_t working_buffer_base = 0x80000000;
  static const uint32_t input_base = 0xC0000000;
  static const uint32_t code_base = 0xE0000000;
  static const unordered_map<uint32_t, const char*> region_labels({
    {stack_base, "stack"},
    {output_base, "output"},
    {input_base, "input"},
    {working_buffer_base, "working buffer"},
    {code_base, "code"},
  });
  string& stack_region = emu.memory_regions[stack_base];
  string& output_region = emu.memory_regions[output_base];
  string& input_region = emu.memory_regions[input_base];
  string& working_buffer_region = emu.memory_regions[working_buffer_base];
  string& code_region = emu.memory_regions[code_base];
  stack_region.resize(1024 * 16);
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
  if (header.header_version == 9) {
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
  emu.a[7] = stack_base + stack_region.size() - sizeof(dcmp_input_header);

  emu.pc = code_base + dcmp_entry_offset;
  emu.ccr = 0x0000;

  emu.debug = debug;

  if ((debug != DebuggingMode::Disabled) && (debug != DebuggingMode::Passive)) {
    fprintf(stderr, "memory map:\n");
    for (const auto& rgn_it : emu.memory_regions) {
      try {
        fprintf(stderr, "  %08" PRIX32 ":%08zX (%s)\n", rgn_it.first,
            rgn_it.second.size(), region_labels.at(rgn_it.first));
      } catch (const out_of_range&) {
        fprintf(stderr, "  %08" PRIX32 ":%08zX\n", rgn_it.first,
            rgn_it.second.size());
      }
    }
    fprintf(stderr, "initial stack contents (input header data):\n");
    print_data(stderr, input_header, sizeof(*input_header), emu.a[7]);
    fprintf(stderr, "start emulation\n");
  }

  // let's roll, son
  uint64_t execution_start_time = now();
  try {
    emu.execute_forever();
  } catch (const exception& e) {
    if (debug != DebuggingMode::Disabled) {
      uint64_t diff = now() - execution_start_time;
      float duration = static_cast<float>(diff) / 1000000.0f;
      fprintf(stderr, "decompressor execution failed (%gsec): %s\n", duration, e.what());
      emu.print_state(stderr, true);
    }
    throw;
  }

  if (debug != DebuggingMode::Disabled) {
    uint64_t diff = now() - execution_start_time;
    float duration = static_cast<float>(diff) / 1000000.0f;
    fprintf(stderr, "note: decompressed resource using dcmp %hd in %g seconds\n",
        dcmp_resource_id, duration);
  }

  output_region.resize(header.decompressed_size);
  return output_region;
}

bool ResourceFile::resource_exists(uint32_t resource_type, int16_t resource_id) {
  if (this->empty) {
    return false;
  }
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

  uint64_t cache_key = (static_cast<uint64_t>(resource_type) << 16) |
      (static_cast<uint64_t>(resource_id) & 0xFFFF);
  try {
    return this->resource_data_cache.at(cache_key);
  } catch (const out_of_range&) { }

  if (!this->empty) {
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

      string result = preadx(fd, size, offset + sizeof(size));

      if ((e.attributes_and_offset & 0x01000000) && decompress) {
        string ret = this->decompress_resource(result, decompress_debug);
        this->resource_data_cache.emplace(cache_key, ret);
        return ret;

      } else {
        this->resource_data_cache.emplace(cache_key, result);
        return result;
      }
    }
  }

  throw out_of_range("file doesn\'t contain resource with the given id");
}

bool ResourceFile::resource_is_compressed(uint32_t resource_type,
    int16_t resource_id) {
  if (this->empty) {
    return false;
  }

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
  if (!this->empty) {
    for (const auto& x : *this->get_reference_list(type)) {
      all_resources.emplace_back(x.resource_id);
    }
  }
  return all_resources;
}

vector<pair<uint32_t, int16_t>> ResourceFile::all_resources() {
  vector<pair<uint32_t, int16_t>> all_resources;
  if (!this->empty) {
    for (const auto& entry : this->map_type_list.entries) {
      for (const auto& x : *this->get_reference_list(entry.resource_type)) {
        all_resources.emplace_back(entry.resource_type, x.resource_id);
      }
    }
  }
  return all_resources;
}

uint32_t ResourceFile::find_resource_by_id(int16_t id,
    const vector<uint32_t>& types) {
  if (!this->empty) {
    for (uint32_t type : types) {
      if (this->resource_exists(type, id)) {
        return type;
      }
    }
  }
  throw runtime_error("referenced resource not found");
}



////////////////////////////////////////////////////////////////////////////////
// code helpers

struct CODE_0_header {
  uint32_t above_a5_size;
  uint32_t below_a5_size;
  uint32_t jump_table_size; // should be == resource_size - 0x10
  uint32_t jump_table_offset;

  struct method_entry {
    uint16_t offset; // meed to add 4 to this apparently
    uint16_t push_opcode;
    int16_t resource_id; // id of target CODE resource
    uint16_t trap_opcode; // disassembles as `trap _LoadSeg`

    void byteswap() {
      this->offset = bswap16(this->offset);
      this->push_opcode = bswap16(this->push_opcode);
      this->resource_id = bswap16(this->resource_id);
      this->trap_opcode = bswap16(this->trap_opcode);
    }
  };

  method_entry entries[0];

  void byteswap(size_t resource_size) {
    this->above_a5_size = bswap32(this->above_a5_size);
    this->below_a5_size = bswap32(this->below_a5_size);
    this->jump_table_size = bswap32(this->jump_table_size);
    this->jump_table_offset = bswap32(this->jump_table_offset);

    size_t count = (resource_size - sizeof(*this)) / sizeof(method_entry);
    for (size_t x = 0; x < count; x++) {
      this->entries[x].byteswap();
    }
  }
};

struct CODE_header {
  uint16_t entry_offset;
  uint16_t unknown;

  void byteswap() {
    this->entry_offset = bswap32(this->entry_offset);
  }
};

struct CODE_far_header {
  uint16_t entry_offset; // 0xFFFF
  uint16_t unused; // 0x0000
  uint32_t near_entry_start_offset;
  uint32_t near_entry_count;
  uint32_t far_entry_start_offset;
  uint32_t far_entry_count;
  uint32_t a5_relocation_data_offset;
  uint32_t a5;
  uint32_t segment_relocation_data_offset;
  uint32_t load_address;
  uint32_t reserved; // 0x00000000

  void byteswap() {
    this->near_entry_start_offset = bswap32(this->near_entry_start_offset);
    this->near_entry_count = bswap32(this->near_entry_count);
    this->far_entry_start_offset = bswap32(this->far_entry_start_offset);
    this->far_entry_count = bswap32(this->far_entry_count);
    this->a5_relocation_data_offset = bswap32(this->a5_relocation_data_offset);
    this->a5 = bswap32(this->a5);
    this->segment_relocation_data_offset = bswap32(this->segment_relocation_data_offset);
    this->load_address = bswap32(this->load_address);
  }
};

string ResourceFile::decode_CODE(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (id == 0) {
    if (data.size() < sizeof(CODE_0_header)) {
      throw runtime_error("CODE 0 too small for header");
    }
    auto* header = reinterpret_cast<CODE_0_header*>(const_cast<char*>(data.data()));
    header->byteswap(data.size());

    size_t specified_count = header->jump_table_size / sizeof(header->entries[0]);
    size_t present_count = (data.size() - sizeof(CODE_0_header)) / sizeof(header->entries[0]);
    string ret = string_printf("# above A5 size: %08" PRIX32 "\n\
# below A5 size: %08" PRIX32 "\n\
# jump table offset/size: %08" PRIX32 ":%08" PRIX32 " (%zu entries specified, %zu entries present)\n",
        header->above_a5_size, header->below_a5_size, header->jump_table_offset,
        header->jump_table_size, specified_count, present_count);
    for (size_t x = 0; x < present_count; x++) {
      auto& e = header->entries[x];
      if (e.push_opcode != 0x3F3C || e.trap_opcode != 0xA9F0) {
        string contents = format_data_string(&e, sizeof(e));
        ret += string_printf("# export %zu -> (invalid) %s\n",
            x, contents.c_str());
      }
      ret += string_printf("# export %zu -> CODE %hd offset 0x%04hX\n",
          x, e.resource_id, e.offset);
    }
    ret += "# end of export table\n";
    return ret;

  } else {
    if (data.size() < sizeof(CODE_header)) {
      throw runtime_error("CODE too small for header");
    }
    auto* header = reinterpret_cast<CODE_header*>(const_cast<char*>(data.data()));
    header->byteswap();

    size_t header_bytes;
    string ret;
    if (header->entry_offset == 0xFFFF && header->unknown == 0x0000) {
      if (data.size() < sizeof(CODE_far_header)) {
        throw runtime_error("CODE too small for far model header");
      }
      auto* far_header = reinterpret_cast<CODE_far_header*>(const_cast<char*>(data.data()));
      far_header->byteswap();

      ret += string_printf("# near jump table start offset: %08" PRIX32 "\n",
          far_header->near_entry_start_offset);
      ret += string_printf("# near jump table count: %08" PRIX32 "\n",
          far_header->near_entry_count);
      ret += string_printf("# far jump table start offset: %08" PRIX32 "\n",
          far_header->far_entry_start_offset);
      ret += string_printf("# far jump table count: %08" PRIX32 "\n",
          far_header->far_entry_count);
      ret += string_printf("# A5 relocation data offset: %08" PRIX32 "\n",
          far_header->a5_relocation_data_offset);
      ret += string_printf("# saved A5 value: %08" PRIX32 "\n",
          far_header->a5);
      ret += string_printf("# segment relocation data offset: %08" PRIX32 "\n",
          far_header->segment_relocation_data_offset);
      ret += string_printf("# load address: %08" PRIX32 "\n",
          far_header->load_address);

      header_bytes = sizeof(CODE_far_header);

    } else {
      ret += string_printf("# entry offset: %04hX\n", header->entry_offset);
      header_bytes = sizeof(CODE_header);
    }

    // attempt to decode CODE 0 to get the exported label offsets
    unordered_multimap<uint32_t, string> labels;
    try {
      string code0_data = this->get_resource_data(type, 0);
      if (code0_data.size() < sizeof(CODE_0_header)) {
        throw runtime_error("CODE 0 too small for header");
      }
      auto* header = reinterpret_cast<CODE_0_header*>(const_cast<char*>(code0_data.data()));
      header->byteswap(code0_data.size());

      size_t count = (code0_data.size() - sizeof(CODE_0_header)) / sizeof(header->entries[0]);
      for (size_t x = 0; x < count; x++) {
        auto& e = header->entries[x];
        if (e.push_opcode != 0x3F3C || e.trap_opcode != 0xA9F0) {
          continue;
        }
        if (e.resource_id == id) {
          labels.emplace(e.offset + header_bytes, string_printf("export_%zu", x));
        }
      }

    } catch (const exception& e) {
      // TODO: we probably should report this somehow
    }

    ret += MC68KEmulator::disassemble(data.data() + header_bytes,
        data.size() - header_bytes, header_bytes, &labels);
    return ret;
  }
}

string ResourceFile::decode_dcmp(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() < 10) {
    throw runtime_error("inline code resource is too short");
  }

  // note: this logic mirrors the logic in decompress_resource (the exact header
  // format is still not known)
  unordered_multimap<uint32_t, string> labels;
  size_t header_bytes = 0;
  uint32_t declared_type = bswap32(*reinterpret_cast<const uint32_t*>(data.data() + 4));
  if (declared_type == type) {
    if (data[0] == 0x60 && data[1] != 0x00) {
      labels.emplace(data[1] + 2, "start");
      header_bytes = data[1] + 2;
    } else if (data[0] == 0x60 && data[1] == 0x00) {
      uint16_t start_offset = bswap16(*reinterpret_cast<const uint16_t*>(data.data() + 2)) + 2;
      labels.emplace(start_offset, "start");
      header_bytes = start_offset;
    } else {
      labels.emplace(0, "start");
    }
  } else {
    const uint16_t* offsets = reinterpret_cast<const uint16_t*>(data.data());
    labels.emplace(bswap16(offsets[0]), "fn0");
    labels.emplace(bswap16(offsets[1]), "start");
    labels.emplace(bswap16(offsets[2]), "fn2");
    header_bytes = 6;
  }

  string header_comment;
  if (header_bytes) {
    header_comment = "# header: " + format_data_string(data.data(), header_bytes) + "\n";
  }
  return header_comment + MC68KEmulator::disassemble(data.data() + header_bytes,
      data.size() - header_bytes, header_bytes, &labels);
}

static string decode_inline_code_resource(const string& data) {
  unordered_multimap<uint32_t, string> labels;
  labels.emplace(0, "start");
  return MC68KEmulator::disassemble(data.data(),data.size(), 0, &labels);
}

string ResourceFile::decode_ADBS(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_clok(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_proc(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_ptch(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_ROvr(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_SERD(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_snth(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_SMOD(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_CDEF(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_INIT(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_LDEF(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_MDBF(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_MDEF(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_PACK(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_PTCH(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}

string ResourceFile::decode_WDEF(int16_t id, uint32_t type) {
  return decode_inline_code_resource(this->get_resource_data(type, id));
}



////////////////////////////////////////////////////////////////////////////////
// image resource decoding

ResourceFile::decoded_cicn::decoded_cicn(Image&& image, Image&& bitmap) :
    image(move(image)), bitmap(move(bitmap)) { }

ResourceFile::decoded_CURS::decoded_CURS(Image&& bitmap, uint16_t hotspot_x,
    uint16_t hotspot_y) : bitmap(move(bitmap)), hotspot_x(hotspot_x),
    hotspot_y(hotspot_y) { }

ResourceFile::decoded_crsr::decoded_crsr(Image&& image, Image&& bitmap,
    uint16_t hotspot_x, uint16_t hotspot_y) : image(move(image)),
    bitmap(move(bitmap)), hotspot_x(hotspot_x), hotspot_y(hotspot_y) { }

struct cicn_header {
  // pixMap fields
  uint32_t pix_map_unused;
  pixel_map_header pix_map;

  // mask bitmap fields
  uint32_t mask_unused;
  bit_map_header mask_header;

  // 1-bit icon bitmap fields
  uint32_t bitmap_unused;
  bit_map_header bitmap_header;

  // icon data fields
  uint32_t icon_data; // ignored

  void byteswap() {
    this->pix_map.byteswap();
    this->mask_header.byteswap();
    this->bitmap_header.byteswap();
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
  if ((header->pix_map.bounds.width() != header->mask_header.bounds.width()) ||
      (header->pix_map.bounds.height() != header->mask_header.bounds.height())) {
    throw runtime_error("mask dimensions don\'t match icon dimensions");
  }
  if (header->bitmap_header.flags_row_bytes &&
      ((header->pix_map.bounds.width() != header->mask_header.bounds.width()) ||
       (header->pix_map.bounds.height() != header->mask_header.bounds.height()))) {
    throw runtime_error("bitmap dimensions don\'t match icon dimensions");
  }
  if ((header->pix_map.pixel_size != 8) && (header->pix_map.pixel_size != 4) &&
      (header->pix_map.pixel_size != 2) && (header->pix_map.pixel_size != 1)) {
    throw runtime_error("pixel bit depth is not 1, 2, 4, or 8");
  }

  size_t mask_map_size = pixel_map_data::size(
      header->mask_header.flags_row_bytes, header->mask_header.bounds.height());
  pixel_map_data* mask_map = reinterpret_cast<pixel_map_data*>(bdata + sizeof(*header));
  if (sizeof(*header) + mask_map_size > data.size()) {
    throw runtime_error("mask map too large");
  }

  size_t bitmap_size = pixel_map_data::size(
      header->bitmap_header.flags_row_bytes, header->bitmap_header.bounds.height());
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
      header->pix_map.flags_row_bytes & 0x3FFF, header->pix_map.bounds.height());
  pixel_map_data* pixel_map = reinterpret_cast<pixel_map_data*>(
      bdata + sizeof(*header) + mask_map_size + bitmap_size + ctable->size());
  if (sizeof(*header) + mask_map_size + bitmap_size + ctable->size() + pixel_map_size > data.size()) {
    throw runtime_error("pixel map too large");
  }

  Image img = decode_color_image(header->pix_map, *pixel_map, *ctable, mask_map,
      header->mask_header.flags_row_bytes);

  // decode the mask and bitmap
  Image bitmap_img(header->bitmap_header.flags_row_bytes ? header->bitmap_header.bounds.width() : 0,
      header->bitmap_header.flags_row_bytes ? header->bitmap_header.bounds.height() : 0, true);
  for (size_t y = 0; y < header->pix_map.bounds.height(); y++) {
    for (size_t x = 0; x < header->pix_map.bounds.width(); x++) {
      uint8_t alpha = mask_map->lookup_entry(1, header->mask_header.flags_row_bytes, x, y) ? 0xFF : 0x00;

      if (header->bitmap_header.flags_row_bytes) {
        if (bitmap->lookup_entry(1, header->bitmap_header.flags_row_bytes, x, y)) {
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
      bdata + header->pixel_map_offset + 4);
  if (header->pixel_map_offset + sizeof(*pixmap_header) > data.size()) {
    throw runtime_error("pixel map header too large");
  }
  pixmap_header->byteswap();

  // get the pixel map data
  size_t pixel_map_size = pixel_map_data::size(
      pixmap_header->flags_row_bytes & 0x3FFF, pixmap_header->bounds.height());
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
  if ((header->type != 1) && (header->type != 3)) {
    throw runtime_error("unknown ppat type");
  }

  // get the pixel map header
  pixel_map_header* pixmap_header = reinterpret_cast<pixel_map_header*>(
      bdata + header->pixel_map_offset + 4);
  if (header->pixel_map_offset + sizeof(*pixmap_header) > data.size()) {
    throw runtime_error("pixel map header too large");
  }
  pixmap_header->byteswap();

  // get the pixel map data
  size_t pixel_map_size = pixel_map_data::size(
      pixmap_header->flags_row_bytes & 0x3FFF, pixmap_header->bounds.height());
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

Image ResourceFile::decode_PAT(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() != 8) {
    throw runtime_error("PAT not exactly 8 bytes in size");
  }
  return decode_monochrome_image(data.data(), data.size(), 8, 8);
}

struct PATN_header {
  uint16_t num_patterns;
  uint64_t pattern_data[0];
};

vector<Image> ResourceFile::decode_PATN(int16_t id, uint32_t type) {
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

vector<Image> ResourceFile::decode_SICN(int16_t id, uint32_t type) {
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

Image ResourceFile::decode_ics8(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  Image decoded = decode_8bit_image(data.data(), data.size(), 16, 16);
  try {
    uint32_t mask_type = (type & 0xFFFFFF00) | '#';
    Image mask = this->decode_icsN(id, mask_type);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_kcs8(int16_t id, uint32_t type) {
  return this->decode_ics8(id, type);
}

Image ResourceFile::decode_icl8(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  Image decoded = decode_8bit_image(data.data(), data.size(), 32, 32);
  try {
    Image mask = this->decode_ICNN(id, RESOURCE_TYPE_ICNN);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_icm8(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  Image decoded = decode_8bit_image(data.data(), data.size(), 16, 12);
  try {
    Image mask = this->decode_icmN(id, RESOURCE_TYPE_icmN);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_ics4(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  Image decoded = decode_4bit_image(data.data(), data.size(), 16, 16);
  try {
    uint32_t mask_type = (type & 0xFFFFFF00) | '#';
    Image mask = this->decode_icsN(id, mask_type);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_kcs4(int16_t id, uint32_t type) {
  return this->decode_ics4(id, type);
}

Image ResourceFile::decode_icl4(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  Image decoded = decode_4bit_image(data.data(), data.size(), 32, 32);
  try {
    Image mask = this->decode_ICNN(id, RESOURCE_TYPE_ICNN);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_icm4(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  Image decoded = decode_4bit_image(data.data(), data.size(), 16, 12);
  try {
    Image mask = this->decode_icmN(id, RESOURCE_TYPE_icmN);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_ICON(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  return decode_monochrome_image(data.data(), data.size(), 32, 32);
}

struct CURS_header { // kind of a misnomer; this is actually the entire thing
  uint8_t bitmap[0x20];
  uint8_t mask[0x20];
  uint16_t hotspot_x;
  uint16_t hotspot_y;

  void byteswap() {
    this->hotspot_x = bswap16(this->hotspot_x);
    this->hotspot_y = bswap16(this->hotspot_y);
  }
};

ResourceFile::decoded_CURS ResourceFile::decode_CURS(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);

  // these should always be the same size
  if (data.size() < 0x40) {
    throw runtime_error("CURS resource is too small");
  }
  CURS_header* header = reinterpret_cast<CURS_header*>(const_cast<char*>(data.data()));
  header->byteswap();

  Image img = decode_monochrome_image_masked(header, 0x40, 16, 16);
  return decoded_CURS(move(img), (data.size() >= 0x42) ? header->hotspot_x : 0xFFFF,
      (data.size() >= 0x44) ? header->hotspot_y : 0xFFFF);
}

Image ResourceFile::decode_ICNN(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  return decode_monochrome_image_masked(data.data(), data.size(), 32, 32);
}

Image ResourceFile::decode_icsN(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  return decode_monochrome_image_masked(data.data(), data.size(), 16, 16);
}

Image ResourceFile::decode_kcsN(int16_t id, uint32_t type) {
  return this->decode_icsN(id, type);
}

Image ResourceFile::decode_icmN(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  return decode_monochrome_image_masked(data.data(), data.size(), 16, 12);
}

pict_render_result ResourceFile::decode_PICT(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  try {
    return render_quickdraw_picture(data.data(), data.size());
  } catch (const exception& e) {
    fprintf(stderr, "warning: PICT rendering failed (%s); attempting rendering using picttoppm\n", e.what());
  }

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

    pict_render_result result;
    result.image = move(img);
    return result;

  } catch (const exception& e) {
    pclose(p);
    unlink(temp_filename);
    throw;
  }
}

vector<color> ResourceFile::decode_pltt(int16_t id, uint32_t type) {
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

  vector<color> ret;
  for (size_t x = 1; x < count + 1; x++) {
    ret.emplace_back(bswap16(pltt[x].r), bswap16(pltt[x].g), bswap16(pltt[x].b));
  }
  return ret;
}

vector<color> ResourceFile::decode_clut(int16_t id, uint32_t type) {
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
  vector<color> ret;
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

  void byteswap() {
    this->format_code = bswap16(this->format_code);
    this->data_format_count = bswap16(this->data_format_count);
  }
};

struct snd_resource_data_format_header {
  uint16_t data_format_id; // we only support 5 here (sampled sound)
  uint32_t flags; // 0x40 = stereo

  void byteswap() {
    this->data_format_id = bswap16(this->data_format_id);
    this->flags = bswap32(this->flags);
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

string decode_snd_data(string data) {
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

    commands_offset = sizeof(snd_resource_header_format1) + 2 +
        header->data_format_count * sizeof(snd_resource_data_format_header);
    num_commands = bswap16(*reinterpret_cast<const uint16_t*>(
        bdata + (commands_offset - 2)));

    // if data format count is 0, assume mono
    if (header->data_format_count == 0) {
      num_channels = 1;

    } else if (header->data_format_count == 1) {
      auto* data_format = reinterpret_cast<snd_resource_data_format_header*>(
          bdata + sizeof(snd_resource_header_format1));
      data_format->byteswap();
      if (data_format->data_format_id != 5) {
        throw runtime_error("snd data format is not sampled");
      }
      num_channels = (data_format->flags & 0x40) ? 2 : 1;

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

        wav_header wav(decoded_samples.size() / num_channels, num_channels,
            sample_rate, 16, sample_buffer->loop_start * loop_factor,
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

          } else if ((compressed_buffer->format == 0x4D414333) || (compressed_buffer->format == 0x4D414336)) { // MAC3, MAC6
            bool is_mace3 = compressed_buffer->format == 0x4D414333;
            decoded_samples = decode_mace(compressed_buffer->data,
                compressed_buffer->num_frames * (is_mace3 ? 2 : 1) * num_channels,
                num_channels == 2, is_mace3);
            loop_factor = is_mace3 ? 3 : 6;

          } else if (compressed_buffer->format == 0x756C6177) { // ulaw
            decoded_samples = decode_ulaw(compressed_buffer->data,
                compressed_buffer->num_frames);
            loop_factor = 2;

          } else if (compressed_buffer->format == 0x616C6177) { // alaw (guess)
            decoded_samples = decode_alaw(compressed_buffer->data,
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



string ResourceFile::decode_snd(int16_t id, uint32_t type) {
  return decode_snd_data(this->get_resource_data(type, id));
}



static string lzss_decompress(const string& src) {
  string ret;
  size_t offset = 0;

  for (;;) {
    if (offset >= src.size()) {
      return ret;
    }
    uint8_t control_bits = src.at(offset++);

    for (uint8_t control_mask = 0x01; control_mask; control_mask <<= 1) {
      if (control_bits & control_mask) {
        if (offset >= src.size()) {
          return ret;
        }
        ret += src.at(offset++);

      } else {
        if (offset >= src.size() - 1) {
          return ret;
        }
        uint16_t params = (static_cast<uint16_t>(src.at(offset)) << 8) | static_cast<uint8_t>(src.at(offset + 1));
        offset += 2;

        size_t copy_offset = ret.size() - ((1 << 12) - (params & 0x0FFF));
        uint8_t count = ((params >> 12) & 0x0F) + 3;
        size_t copy_end_offset = copy_offset + count;

        for (; copy_offset != copy_end_offset; copy_offset++) {
          ret += ret.at(copy_offset);
        }
      }
    }
  }
  return ret;
}

static string decompress_soundmusicsys_data(const string& data) {
  if (data.size() < 4) {
    throw runtime_error("resource too small for compression header");
  }

  uint32_t decompressed_size = bswap32(*reinterpret_cast<const uint32_t*>(data.data()));
  string decompressed = lzss_decompress(data.substr(4));
  if (decompressed.size() < decompressed_size) {
    throw runtime_error("decompression did not produce enough data");
  }
  if (decompressed.size() > decompressed_size) {
    throw runtime_error("decompression produced too much data");
  }
  return decompressed;
}

static string decrypt_soundmusicsys_data(const string& src) {
  string ret;
  uint32_t r = 56549L;
  for (uint8_t ch : src) {
    ret.push_back(ch ^ (r >> 8L));
    r = (static_cast<uint32_t>(ch) + r) * 52845L + 22719L;
  }
  return ret;
}

string ResourceFile::decode_SMSD(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() < 8) {
    throw runtime_error("resource too small for header");
  }

  // there's just an 8-byte header, then the rest of it is 22050khz 8-bit mono
  wav_header wav(data.size() - 8, 1, 22050, 8);
  string ret;
  ret.append(reinterpret_cast<const char*>(&wav), wav.size());
  ret.append(data.substr(8));
  return ret;
}

string ResourceFile::decode_csnd(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() < 4) {
    throw runtime_error("csnd too small for header");
  }
  uint32_t type_and_size = bswap32(*reinterpret_cast<const uint32_t*>(data.data()));

  uint8_t sample_type = type_and_size >> 24;
  if ((sample_type > 3) && (sample_type != 0xFF)) {
    throw runtime_error("invalid csnd sample type");
  }

  // check that decompressed_size makes sense for the type (for types 1 and 2,
  // it must be a multiple of 2; for type 3, it must be a multiple of 4)
  size_t decompressed_size = type_and_size & 0x00FFFFFF;
  if (sample_type != 0xFF) {
    uint8_t sample_bytes = (sample_type == 2) ? sample_type : (sample_type + 1);
    if (decompressed_size % sample_bytes) {
      throw runtime_error("decompressed size is not a multiple of frame size");
    }
  }

  string decompressed = lzss_decompress(data.substr(4));
  if (decompressed.size() < decompressed_size) {
    throw runtime_error("decompression did not produce enough data");
  }
  decompressed.resize(decompressed_size);

  // if sample_type isn't 0xFF, then the buffer is delta-encoded
  if (sample_type == 0) { // mono8
    uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(decompressed.data()));
    uint8_t* data_end = data + decompressed.size();
    for (uint8_t sample = *data++; data != data_end; data++) {
      *data = (sample += *data);
    }

  } else if (sample_type == 2) { // mono16
    uint16_t* data = reinterpret_cast<uint16_t*>(const_cast<char*>(decompressed.data()));
    uint16_t* data_end = data + decompressed.size();
    for (uint16_t sample = bswap16(*data++); data != data_end; data++) {
      *data = (sample += *data);
    }

  } else if (sample_type == 1) { // stereo8
    uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(decompressed.data()));
    uint8_t* data_end = data + decompressed.size();
    data += 2;
    for (uint8_t sample0 = data[-2], sample1 = data[-1]; data != data_end; data += 2) {
      data[0] = (sample0 += data[0]);
      data[1] = (sample1 += data[1]);
    }

  } else if (sample_type == 3) { // stereo16
    uint16_t* data = reinterpret_cast<uint16_t*>(const_cast<char*>(decompressed.data()));
    uint16_t* data_end = data + decompressed.size();
    data += 2;
    for (uint16_t sample0 = bswap16(data[-2]), sample1 = bswap16(data[-1]); data != data_end; data += 2) {
      data[0] = bswap16(sample0 += bswap16(data[0]));
      data[1] = bswap16(sample1 += bswap16(data[1]));
    }
  }

  // the result is a normal snd resource
  return decode_snd_data(decompressed);
}

string ResourceFile::decode_esnd(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  string decrypted = decrypt_soundmusicsys_data(data);
  return decode_snd_data(decrypted);
}

string ResourceFile::decode_ESnd(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);

  uint8_t* ptr = reinterpret_cast<uint8_t*>(const_cast<char*>(data.data()));
  uint8_t* data_end = ptr + data.size();
  for (uint8_t sample = (*ptr++ ^= 0xFF); ptr != data_end; ptr++) {
    *ptr = (sample += (*ptr ^ 0xFF));
  }

  return decode_snd_data(data);
}

string ResourceFile::decode_cmid(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  return decompress_soundmusicsys_data(data);
}

string ResourceFile::decode_emid(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  return decrypt_soundmusicsys_data(data);
}

string ResourceFile::decode_ecmi(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  string decrypted = decrypt_soundmusicsys_data(data);
  return decompress_soundmusicsys_data(decrypted);
}



////////////////////////////////////////////////////////////////////////////////
// sequenced music decoding

struct INST_header {
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

  enum Flags1 {
    EnableInterpolate = 0x80,
    EnableAmpScale = 0x40,
    DisableSoundLoops = 0x20,
    UseSampleRate = 0x08,
    SampleAndHold = 0x04,
    ExtendedFormat = 0x02,
    AvoidReverb = 0x01,
  };
  enum Flags2 {
    NeverInterpolate = 0x80,
    PlayAtSampledFreq = 0x40,
    FitKeySplits = 0x20,
    EnableSoundModifier = 0x10,
    UseSoundModifierAsBaseNote = 0x08,
    NotPolyphonic = 0x04,
    EnablePitchRandomness = 0x02,
    PlayFromSplit = 0x01,
  };

  int16_t snd_id; // or csnd or esnd
  uint16_t base_note; // if zero, use snd field
  uint8_t panning;
  uint8_t flags1;
  uint8_t flags2;
  uint8_t smod_id;
  int16_t params[2];
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

ResourceFile::decoded_INST::key_region::key_region(uint8_t key_low,
    uint8_t key_high, uint8_t base_note, int16_t snd_id, uint32_t snd_type) :
    key_low(key_low), key_high(key_high), base_note(base_note), snd_id(snd_id),
    snd_type(snd_type) { }

ResourceFile::decoded_INST ResourceFile::decode_INST(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() < sizeof(INST_header)) {
    throw runtime_error("INST too small for header");
  }

  INST_header* header = reinterpret_cast<INST_header*>(const_cast<char*>(data.data()));
  if (sizeof(INST_header) + (bswap16(header->num_key_regions) * sizeof(INST_header::key_region)) > data.size()) {
    throw runtime_error("INST too small for data");
  }
  header->byteswap();

  decoded_INST ret;
  ret.base_note = header->base_note;
  ret.constant_pitch = (header->flags2 & INST_header::Flags2::PlayAtSampledFreq);
  ret.use_sample_rate = (header->flags1 & INST_header::Flags1::UseSampleRate);
  if (header->num_key_regions == 0) {
    uint32_t snd_type = this->find_resource_by_id(header->snd_id, {RESOURCE_TYPE_esnd, RESOURCE_TYPE_csnd, RESOURCE_TYPE_snd});
    ret.key_regions.emplace_back(0x00, 0x7F, header->base_note, header->snd_id, snd_type);
  } else {
    for (size_t x = 0; x < header->num_key_regions; x++) {
      const auto& rgn = header->key_regions[x];

      uint32_t snd_type = this->find_resource_by_id(rgn.snd_id, {RESOURCE_TYPE_esnd, RESOURCE_TYPE_csnd, RESOURCE_TYPE_snd});

      // if the snd has PlayAtSampledFreq, set a fake base note of 0x3C to
      // ignore whatever the snd/csnd/esnd says
      uint8_t base_note = (header->flags2 & INST_header::Flags2::PlayAtSampledFreq) ?
          0x3C : header->base_note;

      // if the UseSampleRate flag is not set, then the library apparently
      // doesn't correct for sample rate differences at all. this means that if
      // your INSTs refer to snds that are 11025kHz but you're playing at
      // 22050kHz, your song will be shifted up an octave. even worse, if you
      // have snds with different sample rates, the pitches of all notes will be
      // messed up. (why does this even exist? shouldn't it always be enabled?
      // apparently it's not in a lot of cases, and some songs depend on this!)
      ret.key_regions.emplace_back(rgn.key_low, rgn.key_high, base_note,
          rgn.snd_id, snd_type);
    }
  }

  return ret;
}



struct SONG_header {
  struct inst_override {
    uint16_t midi_channel_id;
    uint16_t inst_resource_id;

    void byteswap() {
      this->midi_channel_id = bswap16(this->midi_channel_id);
      this->inst_resource_id = bswap16(this->inst_resource_id);
    }
  };

  enum Flags1 {
    TerminateDecayNotesEarly = 0x40,
    NoteInterpolateEntireSong = 0x20,
    NoteInterpolateLeadInstrument = 0x10,
    DefaultProgramsPerTrack = 0x08, // if true, track 1 is inst 1, etc.; otherwise channel 1 is inst 1, etc. (currently unimplemented here)
    EnableMIDIProgramChange = 0x04, // ignored; we always allow program change
    DisableClickRemoval = 0x02,
    UseLeadInstrumentForAllVoices = 0x01,
  };
  enum Flags2 {
    Interpolate11kHzBuffer = 0x20,
    EnablePitchRandomness = 0x10,
    AmplitudeScaleLeadInstrument = 0x08,
    AmplitudeScaleAllInstruments = 0x04,
    EnableAmplitudeScaling = 0x02,
  };

  int16_t midi_id;
  uint8_t lead_inst_id;
  uint8_t reverb_type;
  uint16_t tempo_bias; // 0 = default = 16667. doesn't appear to be linear though
  uint8_t type; // 0 = sms, 1 = rmf, 2 = mod (we only support 0 here)
  int8_t semitone_shift;
  uint8_t max_effects;
  uint8_t max_notes;
  uint16_t mix_level;
  uint8_t flags1;
  uint8_t note_decay; // in 1/60ths apparently
  uint8_t percussion_instrument; // 0 = none, 0xFF = GM percussion
  uint8_t flags2;

  uint16_t instrument_override_count;
  inst_override instrument_overrides[0];

  void byteswap() {
    this->midi_id = bswap16(this->midi_id);
    this->tempo_bias = bswap16(this->tempo_bias);
    this->mix_level = bswap16(this->mix_level);
    this->instrument_override_count = bswap16(this->instrument_override_count);
    for (size_t x = 0; x < this->instrument_override_count; x++) {
      this->instrument_overrides[x].byteswap();
    }
  }
};

ResourceFile::decoded_SONG ResourceFile::decode_SONG(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() < sizeof(SONG_header)) {
    throw runtime_error("SONG too small for header");
  }

  SONG_header* header = reinterpret_cast<SONG_header*>(const_cast<char*>(data.data()));
  if (sizeof(SONG_header) + (bswap16(header->instrument_override_count) * sizeof(SONG_header::inst_override)) > data.size()) {
    throw runtime_error("SONG too small for data");
  }
  header->byteswap();

  // note: apparently they split the pitch shift field in some later version of
  // the library; some older SONGs that have a negative value in the pitch_shift
  // field may also set type to 0xFF because it was part of pitch_shift before.
  if (header->type == 0xFF) {
    header->type = 0;
  }

  if (header->type != 0) {
    throw runtime_error("SONG is not type 0 (SMS)");
  }

  decoded_SONG ret;
  ret.midi_id = header->midi_id;
  ret.tempo_bias = header->tempo_bias;
  ret.semitone_shift = header->semitone_shift;
  ret.percussion_instrument = header->percussion_instrument;
  ret.allow_program_change = (header->flags1 & SONG_header::Flags1::EnableMIDIProgramChange);
  for (size_t x = 0; x < header->instrument_override_count; x++) {
    const auto& override = header->instrument_overrides[x];
    ret.instrument_overrides.emplace(override.midi_channel_id, override.inst_resource_id);
  }
  return ret;
}

struct Tune_header {
  uint32_t header_size; // includes the sample description commands in the MIDI stream
  uint32_t magic; // 'musi'
  uint32_t reserved1;
  uint16_t reserved2;
  uint16_t index;
  uint32_t flags;
  // MIDI track data immediately follows

  void byteswap() {
    this->header_size = bswap32(this->header_size);
    this->magic = bswap32(this->magic);
    this->reserved1 = bswap32(this->reserved1);
    this->reserved2 = bswap16(this->reserved2);
    this->index = bswap16(this->index);
    this->flags = bswap32(this->flags);
  }
};

string ResourceFile::decode_Tune(int16_t id, uint32_t type) {
  struct MIDIChunkHeader {
    uint32_t magic; // MThd or MTrk
    uint32_t size;

    void byteswap() {
      this->magic = bswap32(this->magic);
      this->size = bswap32(this->size);
    }
  };
  struct MIDIHeader {
    MIDIChunkHeader header;
    uint16_t format;
    uint16_t track_count;
    uint16_t division;

    void byteswap() {
      this->header.byteswap();
      this->format = bswap16(this->format);
      this->track_count = bswap16(this->track_count);
      this->division = bswap16(this->division);
    }
  };

  string data = this->get_resource_data(type, id);
  if (data.size() < sizeof(Tune_header)) {
    throw runtime_error("Tune size is too small");
  }

  Tune_header* tune = reinterpret_cast<Tune_header*>(const_cast<char*>(data.data()));
  tune->byteswap();
  size_t tune_track_bytes = data.size() - sizeof(Tune_header);
  StringReader r(data.data() + sizeof(Tune_header), tune_track_bytes);

  // convert Tune events into MIDI events
  struct Event {
    uint64_t when;
    uint8_t status;
    string data;

    Event(uint64_t when, uint8_t status, uint8_t param) :
        when(when), status(status) {
      this->data.push_back(param);
    }

    Event(uint64_t when, uint8_t status, uint8_t param1, uint8_t param2) :
        when(when), status(status) {
      this->data.push_back(param1);
      this->data.push_back(param2);
    }
  };

  vector<Event> events;
  unordered_map<uint16_t, uint8_t> partition_id_to_channel;
  uint64_t current_time = 0;

  while (!r.eof()) {
    uint32_t event = r.get_u32r();
    uint8_t type = (event >> 28) & 0x0F;

    switch (type) {
      case 0x00:
      case 0x01: // pause
        current_time += (event & 0x00FFFFFF);
        break;

      case 0x02: // simple note event
      case 0x03: // simple note event
      case 0x09: { // extended note event
        uint8_t key, vel;
        uint16_t partition_id, duration;
        if (type == 0x09) {
          uint32_t options = r.get_u32r();
          partition_id = (event >> 16) & 0xFFF;
          key = (event >> 8) & 0xFF;
          vel = (options >> 22) & 0x7F;
          duration = options & 0x3FFFFF;
        } else {
          partition_id = (event >> 24) & 0x1F;
          key = ((event >> 18) & 0x3F) + 32;
          vel = (event >> 11) & 0x7F;
          duration = event & 0x7FF;
        }

        uint8_t channel;
        try {
          channel = partition_id_to_channel.at(partition_id);
        } catch (const out_of_range&) {
          throw runtime_error("notes produced on uninitialized partition");
        }

        events.emplace_back(current_time, 0x90 | channel, key, vel);
        events.emplace_back(current_time + duration, 0x80 | channel, key, vel);
        break;
      }

      case 0x04: // simple controller event
      case 0x05: // simple controller event
      case 0x0A: { // extended controller event
        uint16_t message, partition_id, value;
        if (type == 0x0A) {
          uint32_t options = r.get_u32r();
          message = (options >> 16) & 0x3FFF;
          partition_id = (event >> 16) & 0xFFF;
          value = options & 0xFFFF;
        } else {
          message = (event >> 16) & 0xFF;
          partition_id = (event >> 24) & 0x1F;
          value = event & 0xFFFF;
        }

        // controller messages can create channels
        uint8_t channel = partition_id_to_channel.emplace(
            partition_id, partition_id_to_channel.size()).first->second;
        if (channel >= 0x10) {
          throw runtime_error("not enough MIDI channels");
        }

        if (message == 0) {
          // bank select (ignore for now)
          break;

        } else if (message == 32) {
          // pitch bend

          // clamp the value and convert to MIDI range (14-bit)
          int16_t s_value = static_cast<int16_t>(value);
          if (s_value < -0x0200) {
            s_value = -0x0200;
          }
          if (s_value > 0x01FF) {
            s_value = 0x01FF;
          }
          s_value = (s_value + 0x200) * 0x10;

          events.emplace_back(current_time, 0xE0 | channel, s_value & 0x7F, (s_value >> 7) & 0x7F);

        } else {
          // some other controller message
          events.emplace_back(current_time, 0xB0 | channel, message, value >> 8);
        }

        break;
      }

      case 0x0F: { // metadata message
        uint16_t partition_id = (event >> 16) & 0xFFF;
        uint32_t message_size = (event & 0xFFFF) * 4;
        if (message_size < 8) {
          throw runtime_error("metadata message too short for type field");
        }

        string message_data = r.read(message_size - 4);
        if (message_data.size() != message_size - 4) {
          throw runtime_error("metadata message exceeds track boundary");
        }

        // the second-to-last word is the message type
        uint16_t message_type = bswap16(*reinterpret_cast<const uint16_t*>(
            message_data.data() + message_data.size() - 4)) & 0x3FFF;

        // meta messages can create channels
        uint8_t channel = partition_id_to_channel.emplace(
            partition_id, partition_id_to_channel.size()).first->second;
        if (channel >= 0x10) {
          throw runtime_error("not enough MIDI channels");
        }

        switch (message_type) {
          case 1: { // instrument definition
            if (message_size != 0x5C) {
              throw runtime_error("message size is incorrect");
            }
            uint32_t instrument = bswap32(*reinterpret_cast<const uint32_t*>(message_data.data() + 0x50));
            events.emplace_back(current_time, 0xC0 | channel, instrument);
            events.emplace_back(current_time, 0xB0 | channel, 7, 0x7F); // volume
            events.emplace_back(current_time, 0xB0 | channel, 10, 0x40); // panning
            events.emplace_back(current_time, 0xE0 | channel, 0x00, 0x40); // pitch bend
            break;
          }

          case 6: { // extended (?) instrument definition
            if (message_size != 0x88) {
              throw runtime_error("message size is incorrect");
            }
            uint32_t instrument = bswap32(*reinterpret_cast<const uint32_t*>(message_data.data() + 0x7C));
            events.emplace_back(current_time, 0xC0 | channel, instrument);
            events.emplace_back(current_time, 0xB0 | channel, 7, 0x7F); // volume
            events.emplace_back(current_time, 0xB0 | channel, 10, 0x40); // panning
            events.emplace_back(current_time, 0xE0 | channel, 0x00, 0x40); // pitch bend
            break;
          }

          case 5: // tune difference
          case 8: // MIDI channel (probably we should use this)
          case 10: // nop
          case 11: // notes used
            break;

          default:
            throw runtime_error(string_printf(
                "unknown metadata event %08" PRIX32 "/%hX (end offset 0x%zX)",
                event, message_type, r.where() + sizeof(Tune_header)));
        }

        break;
      }

      case 0x08: // reserved (ignored; has 4-byte argument)
      case 0x0C: // reserved (ignored; has 4-byte argument)
      case 0x0D: // reserved (ignored; has 4-byte argument)
      case 0x0E: // reserved (ignored; has 4-byte argument)
        r.go(r.where() + 4);
      case 0x06: // marker (ignored)
      case 0x07: // marker (ignored)
        break;

      default:
        throw runtime_error("unsupported event in stream");
    }
  }

  // append the MIDI track end event
  events.emplace_back(current_time, 0xFF, 0x2F, 0x00);

  // sort the events by time, since there can be out-of-order note off events
  stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
    return a.when < b.when;
  });

  // generate the MIDI track
  string midi_track_data;
  current_time = 0;
  for (const Event& event : events) {
    uint64_t delta = event.when - current_time;
    current_time = event.when;

    // write the delay field (encoded as variable-length int)
    string delta_str;
    while (delta > 0x7F) {
      delta_str.push_back(delta & 0x7F);
      delta >>= 7;
    }
    delta_str.push_back(delta);
    for (size_t x = 1; x < delta_str.size(); x++) {
      delta_str[x] |= 0x80;
    }
    reverse(delta_str.begin(), delta_str.end());
    midi_track_data += delta_str;

    // write the event contents
    midi_track_data.push_back(event.status);
    midi_track_data += event.data;
  }

  // generate the MIDI headers
  MIDIHeader midi_header;
  midi_header.header.magic = 0x4D546864; // 'MThd'
  midi_header.header.size = 6;
  midi_header.format = 0;
  midi_header.track_count = 1;
  midi_header.division = 600; // ticks per quarter note

  MIDIChunkHeader track_header;
  track_header.magic = 0x4D54726B; // 'MTrk'
  track_header.size = midi_track_data.size();

  midi_header.byteswap();
  track_header.byteswap();

  // generate the file and return it
  string ret;
  ret.append(reinterpret_cast<const char*>(&midi_header), sizeof(MIDIHeader));
  ret.append(reinterpret_cast<const char*>(&track_header), sizeof(MIDIChunkHeader));
  ret.append(midi_track_data);
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// string decoding

static vector<const string> mac_roman_table({
  // 00
  // note: we intentionally incorrectly decode \r as \n here to convert CR line
  // breaks to LF line breaks which modern systems use
  string("\x00", 1), "\x01", "\x02", "\x03", "\x04", "\x05", "\x06", "\x07",
  "\x08", "\t", "\n", "\x0B", "\x0C", "\n", "\x0E",  "\x0F",
  // 10
  "\x10", "\xE2\x8C\x98", "\xE2\x87\xA7", "\xE2\x8C\xA5",
  "\xE2\x8C\x83", "\x15", "\x16", "\x17",
  "\x18", "\x19", "\x1A", "\x1B", "\x1C", "\x1D", "\x1E", "\x1F",
  // 20
  " ", "!", "\"", "#", "$", "%", "&", "\'",
  "(", ")", "*", "+", ",", "-", ".", "/",
  // 30
  "0", "1", "2", "3", "4", "5", "6", "7",
  "8", "9", ":", ";", "<", "=", ">", "?",
  // 40
  "@", "A", "B", "C", "D", "E", "F", "G",
  "H", "I", "J", "K", "L", "M", "N", "O",
  // 50
  "P", "Q", "R", "S", "T", "U", "V", "W",
  "X", "Y", "Z", "[", "\\", "]", "^", "_",
  // 60
  "`", "a", "b", "c", "d", "e", "f", "g",
  "h", "i", "j", "k", "l", "m", "n", "o",
  // 70
  "p", "q", "r", "s", "t", "u", "v", "w",
  "x", "y", "z", "{", "|", "}", "~", "\x7F",
  // 80
  "\xC3\x84", "\xC3\x85", "\xC3\x87", "\xC3\x89",
  "\xC3\x91", "\xC3\x96", "\xC3\x9C", "\xC3\xA1",
  "\xC3\xA0", "\xC3\xA2", "\xC3\xA4", "\xC3\xA3",
  "\xC3\xA5", "\xC3\xA7", "\xC3\xA9", "\xC3\xA8",
  // 90
  "\xC3\xAA", "\xC3\xAB", "\xC3\xAD", "\xC3\xAC",
  "\xC3\xAE", "\xC3\xAF", "\xC3\xB1", "\xC3\xB3",
  "\xC3\xB2", "\xC3\xB4", "\xC3\xB6", "\xC3\xB5",
  "\xC3\xBA", "\xC3\xB9", "\xC3\xBB", "\xC3\xBC",
  // A0
  "\xE2\x80\xA0", "\xC2\xB0", "\xC2\xA2", "\xC2\xA3",
  "\xC2\xA7", "\xE2\x80\xA2", "\xC2\xB6", "\xC3\x9F",
  "\xC2\xAE", "\xC2\xA9", "\xE2\x84\xA2", "\xC2\xB4",
  "\xC2\xA8", "\xE2\x89\xA0", "\xC3\x86", "\xC3\x98",
  // B0
  "\xE2\x88\x9E", "\xC2\xB1", "\xE2\x89\xA4", "\xE2\x89\xA5",
  "\xC2\xA5", "\xC2\xB5", "\xE2\x88\x82", "\xE2\x88\x91",
  "\xE2\x88\x8F", "\xCF\x80", "\xE2\x88\xAB", "\xC2\xAA",
  "\xC2\xBA", "\xCE\xA9", "\xC3\xA6", "\xC3\xB8",
  // C0
  "\xC2\xBF", "\xC2\xA1", "\xC2\xAC", "\xE2\x88\x9A",
  "\xC6\x92", "\xE2\x89\x88", "\xE2\x88\x86", "\xC2\xAB",
  "\xC2\xBB", "\xE2\x80\xA6", "\xC2\xA0", "\xC3\x80",
  "\xC3\x83", "\xC3\x95", "\xC5\x92", "\xC5\x93",
  // D0
  "\xE2\x80\x93", "\xE2\x80\x94", "\xE2\x80\x9C", "\xE2\x80\x9D",
  "\xE2\x80\x98", "\xE2\x80\x99", "\xC3\xB7", "\xE2\x97\x8A",
  "\xC3\xBF", "\xC5\xB8", "\xE2\x81\x84", "\xE2\x82\xAC",
  "\xE2\x80\xB9", "\xE2\x80\xBA", "\xEF\xAC\x81", "\xEF\xAC\x82",
  // E0
  "\xE2\x80\xA1", "\xC2\xB7", "\xE2\x80\x9A", "\xE2\x80\x9E",
  "\xE2\x80\xB0", "\xC3\x82", "\xC3\x8A", "\xC3\x81",
  "\xC3\x8B", "\xC3\x88", "\xC3\x8D", "\xC3\x8E",
  "\xC3\x8F", "\xC3\x8C", "\xC3\x93", "\xC3\x94",
  // F0
  "\xEF\xA3\xBF", "\xC3\x92", "\xC3\x9A", "\xC3\x9B",
  "\xC3\x99", "\xC4\xB1", "\xCB\x86", "\xCB\x9C",
  "\xC2\xAF", "\xCB\x98", "\xCB\x99", "\xCB\x9A",
  "\xC2\xB8", "\xCB\x9D", "\xCB\x9B", "\xCB\x87",
});

static vector<const string> mac_roman_table_rtf({
  // 00
  // note: we intentionally incorrectly decode \r as \n here to convert CR line
  // breaks to LF line breaks which modern systems use
  "\\\'00", "\\'01", "\\'02", "\\'03", "\\'04", "\\'05", "\\'06", "\\'07",
  "\\'08", "\\line ", "\n", "\\'0B", "\\'0C", "\\line ", "\\'0E",  "\\'0F",
  // 10
  "\\'10", "\xE2\x8C\x98", "\xE2\x87\xA7", "\xE2\x8C\xA5",
  "\xE2\x8C\x83", "\\'15", "\\'16", "\\'17",
  "\\'18", "\\'19", "\\'1A", "\\'1B", "\\'1C", "\\'1D", "\\'1E", "\\'1F",
  // 20
  " ", "!", "\"", "#", "$", "%", "&", "\'",
  "(", ")", "*", "+", ",", "-", ".", "/",
  // 30
  "0", "1", "2", "3", "4", "5", "6", "7",
  "8", "9", ":", ";", "<", "=", ">", "?",
  // 40
  "@", "A", "B", "C", "D", "E", "F", "G",
  "H", "I", "J", "K", "L", "M", "N", "O",
  // 50
  "P", "Q", "R", "S", "T", "U", "V", "W",
  "X", "Y", "Z", "[", "\\\\", "]", "^", "_",
  // 60
  "`", "a", "b", "c", "d", "e", "f", "g",
  "h", "i", "j", "k", "l", "m", "n", "o",
  // 70
  "p", "q", "r", "s", "t", "u", "v", "w",
  "x", "y", "z", "{", "|", "}", "~", "\\'7F",
  // 80
  "\\u196A", "\\u197A", "\\u199C", "\\u201E", "\\u209N", "\\u214O", "\\u220U", "\\u225a",
  "\\u224a", "\\u226a", "\\u228a", "\\u227a", "\\u229a", "\\u231c", "\\u233e", "\\u232e",
  // 90
  "\\u234e", "\\u235e", "\\u237i", "\\u236i", "\\u238i", "\\u239i", "\\u241n", "\\u243o",
  "\\u242o", "\\u244o", "\\u246o", "\\u245o", "\\u250u", "\\u249u", "\\u251u", "\\u252u",
  // A0
  "\\u8224?", "\\u176?", "\\u162c", "\\u163?", "\\u167?", "\\u8226?", "\\u182?", "\\u223?",
  "\\u174R", "\\u169C", "\\u8482?", "\\u180?", "\\u168?", "\\u8800?", "\\u198?", "\\u216O",
  // B0
  "\\u8734?", "\\u177?", "\\u8804?", "\\u8805?", "\\u165?", "\\u181?", "\\u8706?", "\\u8721?",
  "\\u8719?", "\\u960?", "\\u8747?", "\\u170?", "\\u186?", "\\u937?", "\\u230?", "\\u248o",
  // C0
  "\\u191?", "\\u161?", "\\u172?", "\\u8730?", "\\u402?", "\\u8776?", "\\u8710?", "\\u171?",
  "\\u187?", "\\u8230?", "\\u160 ", "\\u192A", "\\u195A", "\\u213O", "\\u338?", "\\u339?",
  // D0
  "\\u8211-", "\\u8212-", "\\u8220\"", "\\u8221\"", "\\u8216\'", "\\u8217\'", "\\u247/", "\\u9674?",
  "\\u255y", "\\u376Y", "\\u8260/", "\\u8364?", "\\u8249<", "\\u8250>", "\\u-1279?", "\\u-1278?",
  // E0
  "\\u8225?", "\\u183?", "\\u8218,", "\\u8222?", "\\u8240?", "\\u194A", "\\u202E", "\\u193A",
  "\\u203E", "\\u200E", "\\u205I", "\\u206I", "\\u207I", "\\u204I", "\\u211O", "\\u212O",
  // F0
  "\\u-1793?", "\\u210O", "\\u218U", "\\u219U", "\\u217U", "\\u305i", "\\u710^", "\\u732~",
  "\\u175?", "\\u728?", "\\u729?", "\\u730?", "\\u184?", "\\u733?", "\\u731?", "\\u711?",
});

static string decode_mac_roman(const char* data, size_t size) {
  string ret;
  while (size--) {
    ret += mac_roman_table[static_cast<uint8_t>(*(data++))];
  }
  return ret;
}

static string decode_mac_roman(const string& data) {
  return decode_mac_roman(data.data(), data.size());
}

pair<vector<string>, string> ResourceFile::decode_STRN(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.size() < 2) {
    throw runtime_error("STR# size is too small");
  }

  uint16_t count = bswap16(*reinterpret_cast<const uint16_t*>(data.data()));

  vector<string> ret;
  size_t offset = 0;
  for (offset = 2; count > 0; count--) {
    if (offset >= data.size()) {
      throw runtime_error(string_printf("expected %zu more strings in STR# resource", count));
    }
    uint8_t len = data[offset++];
    if (offset + len > data.size()) {
      throw runtime_error("STR# resource ends before end of string");
    }

    ret.emplace_back();
    string& s = ret.back();
    for (; len; len--) {
      s += mac_roman_table[static_cast<uint8_t>(data[offset++])];
    }
  }

  return make_pair(ret, data.substr(offset));
}

pair<string, string> ResourceFile::decode_STR(int16_t id, uint32_t type) {
  string data = this->get_resource_data(type, id);
  if (data.empty()) {
    return make_pair("", "");
  }

  uint8_t len = static_cast<uint8_t>(data[0]);
  if (len > data.size() - 1) {
    throw runtime_error("length is too large for data");
  }

  return make_pair(decode_mac_roman(data.substr(1, len)), data.substr(len + 1));
}

string ResourceFile::decode_TEXT(int16_t id, uint32_t type) {
  return decode_mac_roman(this->get_resource_data(type, id));
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
    text = this->get_resource_data(RESOURCE_TYPE_TEXT, id);
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

    color c(cmd.r, cmd.g, cmd.b);

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
    size_t color_id = color_table.at(color(cmd.r, cmd.g, cmd.b).to_u64());
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
      ret += mac_roman_table_rtf[static_cast<uint8_t>(ch)];
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
