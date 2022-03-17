#include "ResourceFile.hh"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <deque>
#include <exception>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <phosg/Process.hh>
#include <phosg/Time.hh>
#include <stdexcept>
#include <vector>
#include <string>

#include "AudioCodecs.hh"
#include "QuickDrawFormats.hh"
#include "QuickDrawEngine.hh"
#include "M68KEmulator.hh"
#include "PPC32Emulator.hh"
#include "Decompressors/System.hh"

using namespace std;



#pragma pack(push)
#pragma pack(1)



string string_for_resource_type(uint32_t type) {
  string result;
  for (ssize_t s = 24; s >= 0; s -= 8) {
    uint8_t ch = (type >> s) & 0xFF;
    if (ch == '\\') {
      result += "\\\\";
    } else if ((ch < ' ') || (ch > 0x7E) || (ch == '/') || (ch == ':')) {
      result += string_printf("\\x%02hhX", ch);
    } else {
      result += static_cast<char>(ch);
    }
  }
  return result;
}

string raw_string_for_resource_type(uint32_t type) {
  string result;
  for (ssize_t s = 24; s >= 0; s -= 8) {
    result += static_cast<char>((type >> s) & 0xFF);
  }
  return result;
}

const char* name_for_region_code(uint16_t region_code) {
  static const vector<const char*> names = {
    "United States", // 0
    "France", // 1
    "Great Britain", // 2
    "Germany", // 3
    "Italy", // 4
    "Netherlands", // 5
    "Belgium/Luxembourg (French)", // 6
    "Sweden", // 7
    nullptr, // 8
    "Denmark", // 9
    "Portugal", // 10
    "French Canada", // 11
    nullptr, // 12
    "Israel", // 13
    "Japan", // 14
    "Australia", // 15
    "Arabia", // 16
    "Finland", // 17
    "Switzerland (French)", // 18
    "Switzerland (German)", // 19
    "Greece", // 20
    "Iceland", // 21
    "Malta", // 22
    "Cyprus", // 23
    "Turkey", // 24
    "Yugoslavia (Croatian)", // 25
    nullptr, // 26
    nullptr, // 27
    nullptr, // 28
    nullptr, // 29
    nullptr, // 30
    nullptr, // 31
    nullptr, // 32
    "India (Hindi)", // 33
    "Pakistan", // 34
    nullptr, // 35
    nullptr, // 36
    nullptr, // 37
    nullptr, // 38
    nullptr, // 39
    nullptr, // 40
    "Lithuania", // 41
    "Poland", // 42
    "Hungary", // 43
    "Estonia", // 44
    "Latvia", // 45
    "Lapland", // 46
    "Faeroe Islands", // 47
    "Iran", // 48
    "Russia", // 49
    "Ireland", // 50
    "Korea", // 51
    "China", // 52
    "Taiwan", // 53
    "Thailand", // 54
  };

  try {
    return names.at(region_code);
  } catch (const out_of_range&) {
    return nullptr;
  }
}



void ResourceFile::add_name_index_entry(shared_ptr<Resource> res) {
  if (!res->name.empty()) {
    this->name_to_resource.emplace(res->name, res);
  }
}

void ResourceFile::delete_name_index_entry(shared_ptr<Resource> res) {
  if (!res->name.empty()) {
    for (auto its = this->name_to_resource.equal_range(res->name);
         its.first != its.second;) {
      if (its.first->second == res) {
        its.first = this->name_to_resource.erase(its.first);
      } else {
        its.first++;
      }
    }
  }
}

uint64_t ResourceFile::make_resource_key(uint32_t type, int16_t id) {
  return (static_cast<uint64_t>(type) << 16) | (static_cast<uint64_t>(id) & 0xFFFF);
}

uint32_t ResourceFile::type_from_resource_key(uint64_t key) {
  return (key >> 16) & 0xFFFFFFFF;
}

int16_t ResourceFile::id_from_resource_key(uint64_t key) {
  return key & 0xFFFF;
}

ResourceFile::Resource::Resource() : type(0), id(0), flags(0) { }

ResourceFile::Resource::Resource(uint32_t type, int16_t id, const string& data)
  : type(type), id(id), flags(0), data(data) { }

ResourceFile::Resource::Resource(uint32_t type, int16_t id, string&& data)
  : type(type), id(id), flags(0), data(move(data)) { }

ResourceFile::Resource::Resource(uint32_t type, int16_t id, uint16_t flags, const string& name, const string& data)
  : type(type), id(id), flags(flags), name(name), data(data) { }

ResourceFile::Resource::Resource(uint32_t type, int16_t id, uint16_t flags, string&& name, string&& data)
  : type(type), id(id), flags(flags), name(move(name)), data(move(data)) { }

ResourceFile::ResourceFile() : ResourceFile(IndexFormat::None) { }

ResourceFile::ResourceFile(IndexFormat format) : format(format) { }

bool ResourceFile::add(const Resource& res_obj) {
  shared_ptr<Resource> res(new Resource(res_obj));
  return this->add(res);
}

bool ResourceFile::add(Resource&& res_obj) {
  shared_ptr<Resource> res(new Resource(move(res_obj)));
  return this->add(res);
}

bool ResourceFile::add(shared_ptr<Resource> res) {
  uint64_t key = this->make_resource_key(res->type, res->id);
  auto emplace_ret = this->key_to_resource.emplace(key, res);
  if (emplace_ret.second) {
    this->add_name_index_entry(res);
  }
  return emplace_ret.second;
}

bool ResourceFile::change_id(uint32_t type, int16_t current_id, int16_t new_id) {
  uint64_t current_key = this->make_resource_key(type, current_id);
  uint64_t new_key = this->make_resource_key(type, new_id);
  auto it = this->key_to_resource.find(current_key);
  if (it != this->key_to_resource.end()) {
    if (current_id != new_id) {
      auto res = it->second;
      this->key_to_resource.erase(it);
      res->id = new_id;
      this->key_to_resource.emplace(new_key, res);
    }
    return true;
  }
  return false;
}

bool ResourceFile::rename(uint32_t type, int16_t id, const string& new_name) {
  if (new_name.size() > 0xFF) {
    throw invalid_argument("name must be 255 bytes or shorter");
  }
  uint64_t key = this->make_resource_key(type, id);
  auto it = this->key_to_resource.find(key);
  if (it != this->key_to_resource.end()) {
    auto& res = it->second;
    this->delete_name_index_entry(res);
    res->name = new_name;
    this->add_name_index_entry(res);
    return true;
  }
  return false;
}

bool ResourceFile::remove(uint32_t type, int16_t id) {
  uint64_t key = this->make_resource_key(type, id);
  auto it = this->key_to_resource.find(key);
  if (it != this->key_to_resource.end()) {
    this->delete_name_index_entry(it->second);
    this->key_to_resource.erase(it);
    return true;
  }
  return false;
}

IndexFormat ResourceFile::index_format() const {
  return this->format;
}

shared_ptr<const ResourceFile::Resource> ResourceFile::get_system_decompressor(
    bool use_ncmp, int16_t resource_id) {
  static unordered_map<uint64_t, shared_ptr<const Resource>> id_to_res;

  // If it's already in the cache, just return it verbatim
  uint32_t resource_type = use_ncmp ? RESOURCE_TYPE_ncmp : RESOURCE_TYPE_dcmp;
  uint64_t key = ResourceFile::make_resource_key(resource_type, resource_id);
  try {
    return id_to_res.at(key);
  } catch (const out_of_range&) { }

  string filename = string_printf("system_dcmps/%ccmp_%hd.bin",
      use_ncmp ? 'n' : 'd', resource_id);
  return id_to_res.emplace(key,
      new Resource(resource_type, resource_id, load_file(filename))).first->second;
}

struct M68KDecompressorInputHeader {
  // This is used to tell the program where to "return" to (stack pointer points
  // here at entry time)
  be_uint32_t return_addr;

  // Parameters to the decompressor - the m68k calling convention passes args on
  // the stack, so these are the actual args to the function
  union {
    struct { // used when header_version == 8
      be_uint32_t data_size;
      be_uint32_t working_buffer_addr;
      be_uint32_t dest_buffer_addr;
      be_uint32_t source_buffer_addr;
    } args_v8;
    struct { // used when header_version == 9
      be_uint32_t source_resource_header;
      be_uint32_t dest_buffer_addr;
      be_uint32_t source_buffer_addr;
      be_uint32_t data_size;
    } args_v9;
  };

  // This is where the program "returns" to; we use the reset opcode to stop
  // emulation
  be_uint16_t reset_opcode;
  be_uint16_t unused;
};

struct PPC32DecompressorInputHeader {
  be_uint32_t saved_r1;
  be_uint32_t saved_cr;
  be_uint32_t saved_lr;
  be_uint32_t reserved1;
  be_uint32_t reserved2;
  be_uint32_t saved_r2;

  be_uint32_t unused[2];

  // This is where the program "returns" to; we set r2 to -1 (which should
  // never happen normally) and make the syscall handler stop emulation
  be_uint32_t set_r2_opcode;
  be_uint32_t syscall_opcode;
};

void ResourceFile::decompress_resource(
    shared_ptr<Resource> res, uint64_t decompress_flags) {
  // If the resource isn't compressed, or we already failed to decompress it, or
  // recompression is disabled globally, then do nothing
  if (!(res->flags & ResourceFlag::FLAG_COMPRESSED) ||
      (res->flags & ResourceFlag::FLAG_DECOMPRESSION_FAILED) ||
      (decompress_flags & DecompressionFlag::DISABLED)) {
    return;
  }

  bool trace = !!(decompress_flags & DecompressionFlag::TRACE);
  bool verbose = trace || !!(decompress_flags & DecompressionFlag::VERBOSE);

  if (res->data.size() < sizeof(CompressedResourceHeader)) {
    throw runtime_error("resource marked as compressed but is too small");
  }

  const auto& header = *reinterpret_cast<const CompressedResourceHeader*>(
      res->data.data());
  if (header.magic != 0xA89F6572) {
    // It looks like some resources have the compression bit set but aren't
    // actually compressed. Reverse-engineering ResEdit makes it look like the
    // Resource Manager just treats the resource as uncompressed if this value
    // is missing, so let's also not fail in that case.
    res->flags = res->flags & ~ResourceFlag::FLAG_COMPRESSED;
    return;
  }

  if (!(header.attributes & 0x01)) {
    throw runtime_error("resource marked as compressed but does not have compression attribute set");
  }

  int16_t dcmp_resource_id;
  uint16_t output_extra_bytes;
  if (header.header_version == 9) {
    dcmp_resource_id = header.header9.dcmp_resource_id;
    output_extra_bytes = header.header9.output_extra_bytes;
  } else if (header.header_version == 8) {
    dcmp_resource_id = header.header8.dcmp_resource_id;
    output_extra_bytes = header.header8.output_extra_bytes;
  } else {
    throw runtime_error("compressed resource header version is not 8 or 9");
  }

  // In order of priority, we try:
  // 1. dcmp resource from the file
  // 2. ncmp resource from the file
  // 3. internal implementation from src/Decompressors/SystemN.cc
  // 4. system dcmp from system_dcmps/dcmp_N.bin
  // 5. system ncmp from system_dcmps/ncmp_N.bin
  // As an awful hack, we use nullptr to represent the internal implementation,
  // since it's the only one that can't be represented by a Resource struct.
  vector<shared_ptr<const Resource>> dcmp_resources;
  if (!(decompress_flags & DecompressionFlag::SKIP_FILE_DCMP)) {
    try {
      dcmp_resources.emplace_back(this->get_resource(RESOURCE_TYPE_dcmp, dcmp_resource_id));
    } catch (const out_of_range&) { }
  }
  if (!(decompress_flags & DecompressionFlag::SKIP_FILE_NCMP)) {
    try {
      dcmp_resources.emplace_back(this->get_resource(RESOURCE_TYPE_ncmp, dcmp_resource_id));
    } catch (const out_of_range&) { }
  }
  if (!(decompress_flags & DecompressionFlag::SKIP_INTERNAL)) {
    if ((dcmp_resource_id >= 0) && (dcmp_resource_id <= 3)) {
      dcmp_resources.emplace_back(nullptr);
    }
  }
  if (!(decompress_flags & DecompressionFlag::SKIP_SYSTEM_DCMP)) {
    try {
      dcmp_resources.emplace_back(this->get_system_decompressor(false, dcmp_resource_id));
    } catch (const cannot_open_file&) { }
  }
  if (!(decompress_flags & DecompressionFlag::SKIP_SYSTEM_NCMP)) {
    try {
      dcmp_resources.emplace_back(this->get_system_decompressor(true, dcmp_resource_id));
    } catch (const cannot_open_file&) { }
  }

  if (dcmp_resources.empty()) {
    throw runtime_error("no decompressors are available for this resource");
  }

  if (verbose) {
    fprintf(stderr, "using dcmp/ncmp %hd (%zu implementation(s) available)\n",
        dcmp_resource_id, dcmp_resources.size());
    fprintf(stderr, "note: data size is %zu (0x%zX); decompressed data size is %" PRIu32 " (0x%" PRIX32 ") bytes\n",
        res->data.size(), res->data.size(),
        header.decompressed_size.load(), header.decompressed_size.load());
  }

  for (size_t z = 0; z < dcmp_resources.size(); z++) {
    shared_ptr<const Resource> dcmp_res = dcmp_resources[z];
    if (verbose) {
      fprintf(stderr, "attempting decompression with implementation %zu of %zu\n",
          z + 1, dcmp_resources.size());
    }

    try {
      if (!dcmp_res.get()) {
        string (*decompress)(
            const CompressedResourceHeader& header,
            const void* source,
            size_t size) = nullptr;
        if (dcmp_resource_id == 0) {
          decompress = &decompress_system0;
        } else if (dcmp_resource_id == 1) {
          decompress = &decompress_system1;
        } else if (dcmp_resource_id == 2) {
          decompress = &decompress_system2;
        } else if (dcmp_resource_id == 3) {
          decompress = &decompress_system3;
        }

        if (!decompress) {
          throw logic_error(string_printf(
              "internal implementation of dcmp %hd requested, but does not exist",
              dcmp_resource_id));
        } else {
          uint64_t start_time = now();
          string decompressed_data = decompress(
              header,
              res->data.data() + sizeof(CompressedResourceHeader),
              res->data.size() - sizeof(CompressedResourceHeader));
          if (decompressed_data.size() != header.decompressed_size) {
            throw runtime_error(string_printf(
                "internal decompressor produced the wrong amount of data (%" PRIu32 " bytes expected, %zu bytes received)",
                header.decompressed_size.load(), decompressed_data.size()));
          }
          if (verbose) {
            float duration = static_cast<float>(now() - start_time) / 1000000.0f;
            fprintf(stderr, "note: decompressed resource using internal decompressor in %g seconds (%zu -> %zu bytes)\n",
                duration, res->data.size(), decompressed_data.size());
          }
          res->data = move(decompressed_data);
          res->flags = (res->flags & ~ResourceFlag::FLAG_COMPRESSED) | ResourceFlag::FLAG_DECOMPRESSED;
          return;
        }

      } else {
        shared_ptr<MemoryContext> mem(new MemoryContext());

        uint32_t entry_pc = 0;
        uint32_t entry_r2 = 0;
        bool is_ppc;
        if (dcmp_res->type == RESOURCE_TYPE_dcmp) {
          is_ppc = false;

          // Figure out where in the dcmp to start execution. There appear to be
          // two formats: one that has 'dcmp' in bytes 4-8 where execution appears
          // to just start at byte 0 (usually it's a branch opcode), and one where
          // the first three words appear to be offsets to various functions,
          // followed by code. The second word appears to be the main entry point
          // in this format, so we use that to determine where to start execution.
          // TODO: It looks like the decompression implementation in ResEdit
          // assumes the second format (with the three offsets) if and only if the
          // compressed resource has header format 9. This feels kind of bad
          // because... shouldn't the dcmp format be a property of the dcmp
          // resource, not the resource being decompressed? We use a heuristic
          // here instead, which seems correct for all decompressors I've seen.
          // TODO: Call init and exit for decompressors that have them. It's not
          // clear (yet) what the arguments to init and exit should be... they
          // each apparently take one argument based on how the adjust the stack
          // before returning, but every decompressor I've seen ignores the
          // argument.
          uint32_t entry_offset;
          if (dcmp_res->data.size() < 10) {
            throw runtime_error("decompressor resource is too short");
          }
          if (dcmp_res->data.substr(4, 4) == "dcmp") {
            entry_offset = 0;
          } else {
            entry_offset = *reinterpret_cast<const be_uint16_t*>(
                dcmp_res->data.data() + 2);
          }

          // Load the dcmp into emulated memory
          size_t code_region_size = dcmp_res->data.size();
          uint32_t code_addr = 0xF0000000;
          mem->allocate_at(code_addr, code_region_size);
          uint8_t* code_base = mem->at<uint8_t>(code_addr, code_region_size);
          memcpy(code_base, dcmp_res->data.data(), dcmp_res->data.size());

          entry_pc = code_addr + entry_offset;
          if (verbose) {
            fprintf(stderr, "loaded code at %08" PRIX32 ":%zX\n", code_addr, code_region_size);
            fprintf(stderr, "dcmp entry offset is %08" PRIX32 " (loaded at %" PRIX32 ")\n",
                entry_offset, entry_pc);
          }

        } else if (dcmp_res->type == RESOURCE_TYPE_ncmp) {
          PEFFFile f("<ncmp>", dcmp_res->data);
          f.load_into("<ncmp>", mem, 0xF0000000);
          is_ppc = f.is_ppc();

          // ncmp decompressors don't appear to define any of the standard export
          // symbols (init/main/term); instead, they define a single export symbol
          // in the export table.
          if (!f.init().name.empty()) {
            throw runtime_error("ncmp decompressor has init symbol");
          }
          if (!f.main().name.empty()) {
            throw runtime_error("ncmp decompressor has main symbol");
          }
          if (!f.term().name.empty()) {
            throw runtime_error("ncmp decompressor has term symbol");
          }
          const auto& exports = f.exports();
          if (exports.size() != 1) {
            throw runtime_error("ncmp decompressor does not export exactly one symbol");
          }

          // The start symbol is actually a transition vector, which is the code
          // addr followed by the desired value in r2
          string start_symbol_name = "<ncmp>:" + exports.begin()->second.name;
          uint32_t start_symbol_addr = mem->get_symbol_addr(start_symbol_name.c_str());
          entry_pc = mem->read_u32(start_symbol_addr);
          entry_r2 = mem->read_u32(start_symbol_addr + 4);

          if (verbose) {
            fprintf(stderr, "ncmp entry pc is %08" PRIX32 " with r2 = %08" PRIX32 "\n",
                entry_pc, entry_r2);
          }

        } else {
          throw runtime_error("decompressor resource is not dcmp or ncmp");
        }

        size_t stack_region_size = 1024 * 16; // 16KB should be enough
        size_t output_region_size = header.decompressed_size + output_extra_bytes;
        // TODO: Looks like some decompressors expect zero bytes after the
        // compressed input? Find out if this is actually true and fix it if not.
        size_t input_region_size = res->data.size() + 0x100;
        // TODO: This is probably way too big; probably we should use
        // ((data.size() * 256) / working_buffer_fractional_size) instead here?
        size_t working_buffer_region_size = res->data.size() * 256;

        // Set up data memory regions. Slightly awkward assumption: decompressed
        // data is never more than 256 times the size of the input data.
        uint32_t stack_addr = 0x10000000;
        mem->allocate_at(stack_addr, stack_region_size);
        if (!stack_addr) {
          throw runtime_error("cannot allocate stack region");
        }
        uint32_t output_addr = 0x20000000;
        mem->allocate_at(output_addr, output_region_size);
        if (!output_addr) {
          throw runtime_error("cannot allocate output region");
        }
        uint32_t working_buffer_addr = 0x80000000;
        mem->allocate_at(working_buffer_addr, working_buffer_region_size);
        if (!working_buffer_addr) {
          throw runtime_error("cannot allocate working buffer region");
        }
        uint32_t input_addr = 0xC0000000;
        mem->allocate_at(input_addr, input_region_size);
        if (!input_addr) {
          throw runtime_error("cannot allocate input region");
        }
        if (verbose) {
          fprintf(stderr, "memory:\n");
          fprintf(stderr, "  stack region at %08" PRIX32 ":%zX\n", stack_addr, stack_region_size);
          fprintf(stderr, "  output region at %08" PRIX32 ":%zX\n", output_addr, output_region_size);
          fprintf(stderr, "  working region at %08" PRIX32 ":%zX\n", working_buffer_addr, working_buffer_region_size);
          fprintf(stderr, "  input region at %08" PRIX32 ":%zX\n", input_addr, input_region_size);
        }
        uint8_t* stack_base = mem->at<uint8_t>(stack_addr, stack_region_size);
        mem->memcpy(input_addr, res->data.data(), res->data.size());

        uint64_t execution_start_time;
        if (is_ppc) {
          // Set up header in stack region
          uint32_t return_addr = stack_addr + stack_region_size - sizeof(PPC32DecompressorInputHeader) + offsetof(PPC32DecompressorInputHeader, set_r2_opcode);
          PPC32DecompressorInputHeader* input_header = reinterpret_cast<PPC32DecompressorInputHeader*>(
              stack_base + stack_region_size - sizeof(PPC32DecompressorInputHeader));
          input_header->saved_r1 = 0xAAAAAAAA;
          input_header->saved_cr = 0x00000000;
          input_header->saved_lr = return_addr;
          input_header->reserved1 = 0x00000000;
          input_header->reserved2 = 0x00000000;
          input_header->saved_r2 = entry_r2;
          input_header->unused[0] = 0x00000000;
          input_header->unused[1] = 0x00000000;
          input_header->set_r2_opcode = 0x3840FFFF; // li r2, -1
          input_header->syscall_opcode = 0x44000002; // sc

          // Set up registers
          PPC32Registers regs;
          regs.r[1].u = stack_addr + stack_region_size - sizeof(PPC32DecompressorInputHeader);
          regs.r[2].u = entry_r2;
          regs.r[3].u = input_addr + sizeof(CompressedResourceHeader);
          regs.r[4].u = output_addr;
          regs.r[5].u = (header.header_version == 9) ? input_addr : working_buffer_addr;
          regs.r[6].u = input_region_size - sizeof(CompressedResourceHeader);
          regs.lr = return_addr;
          regs.pc = entry_pc;
          if (verbose) {
            fprintf(stderr, "initial stack contents (input header data):\n");
            print_data(stderr, input_header, sizeof(*input_header), regs.r[1].u);
          }

          // Set up environment
          shared_ptr<InterruptManager> interrupt_manager(new InterruptManager());
          PPC32Emulator emu(mem);
          emu.set_interrupt_manager(interrupt_manager);
          if (trace) {
            emu.set_debug_hook([&](PPC32Emulator&, PPC32Registers& regs) -> bool {
              if (interrupt_manager->cycles() % 25 == 0) {
                regs.print_header(stderr);
                fprintf(stderr, " => -OPCODE- DISASSEMBLY\n");
              }
              regs.print(stderr);
              uint32_t opcode = mem->read<be_uint32_t>(regs.pc);
              string dasm = PPC32Emulator::disassemble_one(regs.pc, opcode);
              fprintf(stderr, " => %08X %s\n", opcode, dasm.c_str());
              return true;
            });
          }
          emu.set_syscall_handler([&](PPC32Emulator&, PPC32Registers& regs) -> bool {
            // We don't support any syscalls in PPC mode - the only syscall that
            // should occur is the one at the end of emulation, when r2 == -1.
            if (regs.r[2].u != 0xFFFFFFFF) {
              throw runtime_error("unimplemented syscall");
            }
            return false;
          });

          // Run the decompressor.
          execution_start_time = now();
          try {
            emu.execute(regs);
          } catch (const exception& e) {
            if (verbose) {
              uint64_t diff = now() - execution_start_time;
              float duration = static_cast<float>(diff) / 1000000.0f;
              fprintf(stderr, "powerpc decompressor execution failed (%gsec): %s\n", duration, e.what());
            }
            throw;
          }

        } else {
          // Set up header in stack region
          M68KDecompressorInputHeader* input_header = reinterpret_cast<M68KDecompressorInputHeader*>(
              stack_base + stack_region_size - sizeof(M68KDecompressorInputHeader));
          input_header->return_addr = stack_addr + stack_region_size - sizeof(M68KDecompressorInputHeader) + offsetof(M68KDecompressorInputHeader, reset_opcode);
          if (header.header_version == 9) {
            input_header->args_v9.data_size = input_region_size - sizeof(CompressedResourceHeader);
            input_header->args_v9.source_resource_header = input_addr;
            input_header->args_v9.dest_buffer_addr = output_addr;
            input_header->args_v9.source_buffer_addr = input_addr + sizeof(CompressedResourceHeader);
          } else {
            input_header->args_v8.data_size = input_region_size - sizeof(CompressedResourceHeader);
            input_header->args_v8.working_buffer_addr = working_buffer_addr;
            input_header->args_v8.dest_buffer_addr = output_addr;
            input_header->args_v8.source_buffer_addr = input_addr + sizeof(CompressedResourceHeader);
          }

          input_header->reset_opcode = 0x4E70;
          input_header->unused = 0x0000;

          // Set up registers
          M68KRegisters regs;
          regs.a[7] = stack_addr + stack_region_size - sizeof(M68KDecompressorInputHeader);
          regs.pc = entry_pc;
          if (verbose) {
            fprintf(stderr, "initial stack contents (input header data):\n");
            print_data(stderr, input_header, sizeof(*input_header), regs.a[7]);
          }

          // Set up environment
          unordered_map<uint16_t, uint32_t> trap_to_call_stub_addr;
          M68KEmulator emu(mem);
          if (trace) {
            emu.print_state_header(stderr);
            emu.set_debug_hook([&](M68KEmulator& emu, M68KRegisters&) -> bool {
              emu.print_state(stderr);
              return true;
            });
          }
          emu.set_syscall_handler([&](M68KEmulator&, M68KRegisters& regs, uint16_t opcode) -> bool {
            uint16_t trap_number;
            bool auto_pop = false;
            uint8_t flags = 0;

            if (opcode & 0x0800) {
              trap_number = opcode & 0x0BFF;
              auto_pop = opcode & 0x0400;
            } else {
              trap_number = opcode & 0x00FF;
              flags = (opcode >> 9) & 3;
            }

            // We only support a few traps here. Specifically:
            // - System dcmp 2 uses BlockMove
            // - Ben Mickaelian's self-modifying decompressor uses
            //   GetTrapAddress, but it suffices to simulate them with stubs
            if (trap_number == 0x002E) { // BlockMove
              // A0 = src, A1 = dst, D0 = size
              const void* src = mem->at<void>(regs.a[0], regs.d[0].u);
              void* dst = mem->at<void>(regs.a[1], regs.d[0].u);
              memcpy(dst, src, regs.d[0].u);
              regs.d[0].u = 0; // Result code (success)

            } else if (trap_number == 0x0046) { // GetTrapAddress
              uint16_t trap_number = regs.d[0].u & 0xFFFF;
              if ((trap_number > 0x4F) && (trap_number != 0x54) && (trap_number != 0x57)) {
                trap_number |= 0x0800;
              }

              // If it already has a call routine, just return that
              try {
                regs.a[0] = trap_to_call_stub_addr.at(trap_number);
                if (verbose) {
                  fprintf(stderr, "GetTrapAddress: using cached call stub for trap %04hX -> %08" PRIX32 "\n",
                      trap_number, regs.a[0]);
                }

              } catch (const out_of_range&) {
                // Create a call stub
                uint32_t call_stub_addr = mem->allocate(4);
                be_uint16_t* call_stub = mem->at<be_uint16_t>(call_stub_addr, 4);
                trap_to_call_stub_addr.emplace(trap_number, call_stub_addr);
                call_stub[0] = 0xA000 | trap_number; // A-trap opcode
                call_stub[1] = 0x4E75; // rts

                // Return the address
                regs.a[0] = call_stub_addr;

                if (verbose) {
                  fprintf(stderr, "GetTrapAddress: created call stub for trap %04hX -> %08" PRIX32 "\n",
                      trap_number, regs.a[0]);
                }
              }

            } else if (verbose) {
              if (trap_number & 0x0800) {
                fprintf(stderr, "warning: skipping unimplemented toolbox trap (num=%hX, auto_pop=%s)\n",
                    static_cast<uint16_t>(trap_number & 0x0BFF), auto_pop ? "true" : "false");
              } else {
                fprintf(stderr, "warning: skipping unimplemented os trap (num=%hX, flags=%hhu)\n",
                    static_cast<uint16_t>(trap_number & 0x00FF), flags);
              }
            }

            return true;
          });

          // Run the decompressor.
          execution_start_time = now();
          try {
            emu.execute(regs);
          } catch (const exception& e) {
            if (verbose) {
              uint64_t diff = now() - execution_start_time;
              float duration = static_cast<float>(diff) / 1000000.0f;
              fprintf(stderr, "m68k decompressor execution failed (%gsec): %s\n", duration, e.what());
              emu.print_state(stderr);
            }
            throw;
          }
        }

        if (verbose) {
          uint64_t diff = now() - execution_start_time;
          float duration = static_cast<float>(diff) / 1000000.0f;
          fprintf(stderr, "note: decompressed resource using %s %hd in %g seconds (%zu -> %" PRIu32 " bytes)\n",
              (dcmp_res->type == RESOURCE_TYPE_dcmp) ? "dcmp" : "ncmp", dcmp_res->id,
              duration, res->data.size(), header.decompressed_size.load());
        }

        res->data = mem->read(output_addr, header.decompressed_size);
        res->flags = (res->flags & ~ResourceFlag::FLAG_COMPRESSED) | ResourceFlag::FLAG_DECOMPRESSED;
        return;
      }

    } catch (const exception& e) {
      if (verbose) {
        fprintf(stderr, "decompressor implementation %zu of %zu failed: %s\n",
            z + 1, dcmp_resources.size(), e.what());
      }
    }
  }

  throw runtime_error("no decompressor succeeded");
}

bool ResourceFile::resource_exists(uint32_t type, int16_t id) const {
  return this->key_to_resource.count(this->make_resource_key(type, id));
}

bool ResourceFile::resource_exists(uint32_t type, const char* name) const {
  auto its = this->name_to_resource.equal_range(name);
  for (; its.first != its.second; its.first++) {
    if (its.first->second->type == type) {
      return true;
    }
  }
  return false;
}

shared_ptr<ResourceFile::Resource> ResourceFile::get_resource(
    uint32_t type, int16_t id, uint64_t decompress_flags) {
  auto res = this->key_to_resource.at(this->make_resource_key(type, id));
  this->decompress_resource(res, decompress_flags);
  return res;
}

shared_ptr<ResourceFile::Resource> ResourceFile::get_resource(
    uint32_t type, const char* name, uint64_t decompress_flags) {
  auto its = this->name_to_resource.equal_range(name);
  for (; its.first != its.second; its.first++) {
    auto res = its.first->second;
    if (res->type == type) {
      this->decompress_resource(res, decompress_flags);
      return res;
    }
  }
  throw out_of_range("no such resource");
}

vector<int16_t> ResourceFile::all_resources_of_type(uint32_t type) const {
  vector<int16_t> ret;
  for (auto it = this->key_to_resource.lower_bound(this->make_resource_key(type, 0));
       it != this->key_to_resource.end(); it++) {
    if (this->type_from_resource_key(it->first) != type) {
      break;
    }
    ret.emplace_back(this->id_from_resource_key(it->first));
  }
  return ret;
}

vector<uint32_t> ResourceFile::all_resource_types() const {
  vector<uint32_t> ret;
  for (auto it : this->key_to_resource) {
    uint32_t type = this->type_from_resource_key(it.first);
    if (ret.empty() || ret.back() != type) {
      ret.emplace_back(type);
    }
  }
  return ret;
}

vector<pair<uint32_t, int16_t>> ResourceFile::all_resources() const {
  vector<pair<uint32_t, int16_t>> ret;
  for (const auto& it : this->key_to_resource) {
    ret.emplace_back(make_pair(
        this->type_from_resource_key(it.first), this->id_from_resource_key(it.first)));
  }
  return ret;
}

uint32_t ResourceFile::find_resource_by_id(int16_t id,
    const vector<uint32_t>& types) {
  for (uint32_t type : types) {
    if (this->resource_exists(type, id)) {
      return type;
    }
  }
  throw runtime_error("referenced resource not found");
}



////////////////////////////////////////////////////////////////////////////////
// Meta resources

ResourceFile::TemplateEntry::TemplateEntry(
    string&& name,
    Type type,
    Format format,
    uint16_t width,
    uint8_t end_alignment,
    uint8_t align_offset,
    bool is_signed)
  : name(move(name)),
    type(type),
    format(format),
    width(width),
    end_alignment(end_alignment),
    align_offset(align_offset),
    is_signed(is_signed) { }

ResourceFile::TemplateEntry::TemplateEntry(
    string&& name,
    Type type,
    TemplateEntryList&& list_entries)
  : name(move(name)),
    type(type),
    format(Format::FLAG),
    width(2),
    end_alignment(0),
    align_offset(0),
    is_signed(true),
    list_entries(move(list_entries)) { }

ResourceFile::TemplateEntryList ResourceFile::decode_TMPL(int16_t id, uint32_t type) {
  return this->decode_TMPL(this->get_resource(type, id));
}

ResourceFile::TemplateEntryList ResourceFile::decode_TMPL(shared_ptr<const Resource> res) {
  return ResourceFile::decode_TMPL(res->data.data(), res->data.size());
}

ResourceFile::TemplateEntryList ResourceFile::decode_TMPL(const void* data, size_t size) {
  StringReader r(data, size);

  using Entry = TemplateEntry;
  using Type = Entry::Type;
  using Format = Entry::Format;

  vector<shared_ptr<Entry>> ret;
  vector<vector<shared_ptr<Entry>>*> write_stack;
  write_stack.emplace_back(&ret);
  bool in_bbit_array = false;
  while (!r.eof()) {
    string name = decode_mac_roman(r.readx(r.get_u8()));
    uint32_t type = r.get_u32b();

    if (write_stack.empty()) {
      throw runtime_error("TMPL ended list with no list open");
    }
    auto* entries = write_stack.back();

    if (in_bbit_array && type != 0x42424954) {
      throw runtime_error("BBIT array length is not a multiple of 8");
    }

    switch (type) {
      case 0x44564452: // DVDR; not in documentation. Looks like a comment? ("Divider"?)
        entries->emplace_back(new Entry(move(name), Type::VOID, Format::DECIMAL, 0, 0, 0));
        break;
      case 0x43415345: { // CASE; not in documentation.
        // These appear to be of the format <name>=<value>. <value> is an
        // integer in decimal format or hex (preceded by a $).
        auto tokens = split(name, '=');
        if (tokens.size() != 2) {
          throw runtime_error("CASE entry does not contain exactly one '=' character");
        }
        if (tokens[1].empty()) {
          throw runtime_error("CASE value token is empty");
        }
        name = tokens[0];
        int32_t value = (tokens[1][0] == '$')
            ? strtol(&tokens[1][1], nullptr, 16)
            : stol(tokens[1], nullptr, 10);
        entries->back()->case_names.emplace(value, move(tokens[0]));
        break;
      }
      case 0x55425954: // UBYT; not in documentation. Presumably "unsigned byte"
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DECIMAL, 1, 0, 0, false));
        break;
      case 0x55575244: // UWRD; not in documentation. Presumably "unsigned word"
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DECIMAL, 2, 0, 0, false));
        break;
      case 0x554C4E47: // ULNG; not in documentation. Presumably "unsigned long"
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DECIMAL, 4, 0, 0, false));
        break;
      case 0x44415445: // DATE; not in documentation. Looks like an unsigned long
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DATE, 4, 0, 0, false));
        break;
      case 0x44425954: // DBYT
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DECIMAL, 1, 0, 0));
        break;
      case 0x44575244: // DWRD
      case 0x52534944: // RSID; not in documentation. Presumably "resource ID"
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DECIMAL, 2, 0, 0));
        break;
      case 0x444C4E47: // DLNG
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::DECIMAL, 4, 0, 0));
        break;
      case 0x48425954: // HBYT
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::HEX, 1, 0, 0));
        break;
      case 0x48575244: // HWRD
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::HEX, 2, 0, 0));
        break;
      case 0x484C4E47: // HLNG
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::HEX, 4, 0, 0));
        break;
      case 0x46495844: // FIXD; not in documentation. Presumably 32-bit fixed-point
        // .width specifies the width per component (16+16=32)
        entries->emplace_back(new Entry(move(name), Type::FIXED_POINT, Format::DECIMAL, 2, 0, 0));
        break;
      case 0x504E5420: // PNT ; not in documentation. Looks like 16-bit 2D point
        // .width specifies the width per component (16+16=32)
        entries->emplace_back(new Entry(move(name), Type::POINT_2D, Format::DECIMAL, 2, 0, 0));
        break;
      case 0x41575244: // AWRD
        entries->emplace_back(new Entry(move(name), Type::ALIGNMENT, Format::HEX, 0, 2, 0));
        break;
      case 0x414C4E47: // ALNG
        entries->emplace_back(new Entry(move(name), Type::ALIGNMENT, Format::HEX, 0, 4, 0));
        break;
      case 0x46425954: // FBYT
        entries->emplace_back(new Entry(move(name), Type::ZERO_FILL, Format::HEX, 1, 0, 0));
        break;
      case 0x46575244: // FWRD
        entries->emplace_back(new Entry(move(name), Type::ZERO_FILL, Format::HEX, 2, 0, 0));
        break;
      case 0x464C4E47: // FLNG
        entries->emplace_back(new Entry(move(name), Type::ZERO_FILL, Format::HEX, 4, 0, 0));
        break;
      case 0x48455844: // HEXD
        entries->emplace_back(new Entry(move(name), Type::EOF_STRING, Format::HEX, 0, 0, 0));
        break;
      case 0x50535452: // PSTR
        entries->emplace_back(new Entry(move(name), Type::PSTRING, Format::TEXT, 1, 0, 0));
        break;
      case 0x57535452: // WSTR
        entries->emplace_back(new Entry(move(name), Type::PSTRING, Format::TEXT, 2, 0, 0));
        break;
      case 0x4C535452: // LSTR
        entries->emplace_back(new Entry(move(name), Type::PSTRING, Format::TEXT, 4, 0, 0));
        break;
      case 0x45535452: // ESTR
        entries->emplace_back(new Entry(move(name), Type::PSTRING, Format::TEXT, 1, 2, 0));
        break;
      case 0x4F535452: // OSTR
        entries->emplace_back(new Entry(move(name), Type::PSTRING, Format::TEXT, 1, 2, 1));
        break;
      case 0x43535452: // CSTR
        entries->emplace_back(new Entry(move(name), Type::CSTRING, Format::TEXT, 1, 0, 0));
        break;
      case 0x45435354: // ECST
        entries->emplace_back(new Entry(move(name), Type::CSTRING, Format::TEXT, 1, 2, 0));
        break;
      case 0x4F435354: // OCST
        entries->emplace_back(new Entry(move(name), Type::CSTRING, Format::TEXT, 1, 2, 1));
        break;
      case 0x424F4F4C: // BOOL
        entries->emplace_back(new Entry(move(name), Type::BOOL, Format::FLAG, 2, 0, 0));
        break;
      case 0x42424954: // BBIT
        if (in_bbit_array) {
          entries->emplace_back(new Entry(move(name), Type::BOOL, Format::FLAG, 2, 0, 0));
          if (entries->size() == 8) {
            write_stack.pop_back();
            in_bbit_array = false;
          }
        } else {
          entries->emplace_back(new Entry("", Type::BITFIELD, Format::FLAG, 1, 0, 0));
          entries = write_stack.emplace_back(&entries->back()->list_entries);
          entries->emplace_back(new Entry(move(name), Type::BOOL, Format::FLAG, 2, 0, 0));
          in_bbit_array = true;
        }
        break;
      case 0x43484152: // CHAR
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::TEXT, 1, 0, 0));
        break;
      case 0x544E414D: // TNAM
        entries->emplace_back(new Entry(move(name), Type::INTEGER, Format::TEXT, 4, 0, 0));
        break;
      case 0x52454354: // RECT
        entries->emplace_back(new Entry(move(name), Type::RECT, Format::DECIMAL, 2, 0, 0));
        break;
      case 0x434F4C52: // COLR
        entries->emplace_back(new Entry(move(name), Type::COLOR, Format::HEX, 2, 0, 0, false));
        break;
      case 0x4C53545A: // LSTZ
        entries->emplace_back(new Entry(move(name), Type::LIST_ZERO_BYTE, Format::FLAG, 0, 0, 0));
        write_stack.emplace_back(&entries->back()->list_entries);
        break;
      case 0x4C535442: // LSTB
        entries->emplace_back(new Entry(move(name), Type::LIST_EOF, Format::FLAG, 0, 0, 0));
        write_stack.emplace_back(&entries->back()->list_entries);
        break;
      case 0x5A434E54: // ZCNT
        entries->emplace_back(new Entry(move(name), Type::LIST_ZERO_COUNT, Format::HEX, 2, 0, 0));
        write_stack.emplace_back(&entries->back()->list_entries);
        break;
      case 0x4F434E54: // OCNT
        entries->emplace_back(new Entry(move(name), Type::LIST_ONE_COUNT, Format::HEX, 2, 0, 0));
        write_stack.emplace_back(&entries->back()->list_entries);
        break;
      case 0x4C434E54: // LCNT; not in documentation. Looks like a 32-bit one-based list count
        entries->emplace_back(new Entry(move(name), Type::LIST_ONE_COUNT, Format::HEX, 4, 0, 0));
        write_stack.emplace_back(&entries->back()->list_entries);
        break;
      case 0x4C535443: { // LSTC
        // ZCNT or OCNT should have already opened the list; make sure that it
        // was the previous command.
        if (write_stack.size() < 2) {
          throw runtime_error("LSTC without preceding ZCNT/OCNT/LCNT in TMPL");
        }
        if (!write_stack.back()->empty()) {
          throw runtime_error("commands between ZCNT/OCNT/LCNT and LSTC");
        }
        auto list_type = write_stack[write_stack.size() - 2]->back()->type;
        if ((list_type != Type::LIST_ZERO_COUNT) && (list_type != Type::LIST_ONE_COUNT)) {
          throw runtime_error("LSTC used for non-ZCNT/OCNT list");
        }
        break;
      }
      case 0x4C535445: // LSTE
        write_stack.pop_back();
        break;
      case 0x50313030: // P100; not in documentation.
        // This appears to be a bug. Stuffit Expander has a TMPL with a P100
        // field in it, but P100 isn't valid - a 1-byte pstring can only be 0xFF
        // bytes long. It looks like whoever wrote the TMPL mistakely included
        // the length byte here... reading some of the resources that would
        // match the relevant TMPL makes it look like we should treat P100 as if
        // it were P0FF, so that's what we'll do.
        entries->emplace_back(new Entry(move(name), Type::FIXED_PSTRING, Format::TEXT, 0xFF, 0, 0));
        break;
      default:
        try {
          if ((type & 0xFF000000) == 0x48000000) {
            uint16_t width = 
                value_for_hex_char(type & 0xFF) |
                (value_for_hex_char((type >> 8) & 0xFF) << 4) |
                (value_for_hex_char((type >> 16) & 0xFF) << 8);
            entries->emplace_back(new Entry(move(name), Type::STRING, Format::HEX, width, 0, 0));
          } else if ((type & 0xFF000000) == 0x43000000) {
            uint16_t width = 
                value_for_hex_char(type & 0xFF) |
                (value_for_hex_char((type >> 8) & 0xFF) << 4) |
                (value_for_hex_char((type >> 16) & 0xFF) << 8);
            entries->emplace_back(new Entry(move(name), Type::FIXED_CSTRING, Format::TEXT, width, 0, 0));
          } else if ((type & 0xFFFF0000) == 0x50300000) {
            uint16_t width = 
                value_for_hex_char(type & 0xFF) |
                (value_for_hex_char((type >> 8) & 0xFF) << 4);
            entries->emplace_back(new Entry(move(name), Type::FIXED_PSTRING, Format::TEXT, width, 0, 0));
          } else {
            throw runtime_error("unknown field type: " + string_for_resource_type(type));
          }
        } catch (const out_of_range&) {
          throw runtime_error("unknown field type: " + string_for_resource_type(type));
        }
    }
  }

  if (write_stack.size() != 1) {
    throw runtime_error("unterminated list in TMPL");
  }
  return ret;
}

static void disassemble_from_template_inner(
    deque<string>& lines,
    StringReader& r,
    const ResourceFile::TemplateEntryList& entries,
    size_t indent_level) {

  using Entry = ResourceFile::TemplateEntry;
  using Type = Entry::Type;
  using Format = Entry::Format;

  static auto format_string = +[](Entry::Format format, const string& str) -> string {
    if (format == Format::HEX) {
      return format_data_string(str);
    } else if (format == Format::TEXT) {
      return str;
    } else {
      throw logic_error("invalid string display format");
    }
  };

  static auto format_integer = +[](shared_ptr<const Entry> entry, int64_t value) -> string {
    string case_name_suffix;
    try {
      case_name_suffix = string_printf(" (%s)", entry->case_names.at(value).c_str());
    } catch (const out_of_range&) { }

    switch (entry->format) {
      case Format::DECIMAL:
        return string_printf("%" PRId64, value);
      case Format::HEX:
      case Format::FLAG:
        if (entry->width == 1) {
          if (entry->is_signed && (value & 0x80)) {
            return string_printf("-0x%02hhX", static_cast<uint8_t>(-value));
          } else {
            return string_printf("0x%02hhX", static_cast<uint8_t>(value));
          }
        } else if (entry->width == 2) {
          if (entry->is_signed && (value & 0x8000)) {
            return string_printf("-0x%04hX", static_cast<uint16_t>(-value));
          } else {
            return string_printf("0x%04hX", static_cast<uint16_t>(value));
          }
        } else if (entry->width == 4) {
          if (entry->is_signed && (value & 0x80000000)) {
            return string_printf("-0x%08X", static_cast<uint32_t>(-value));
          } else {
            return string_printf("0x%08X", static_cast<uint32_t>(value));
          }
        } else {
          throw logic_error("invalid integer width");
        }
      case Format::TEXT:
        if (entry->width == 1) {
          return string_printf("\'%c\' (0x%02" PRIX64 ")",
              static_cast<char>(value), value);
        } else if (entry->width == 2) {
          return string_printf("\'%c%c\' (0x%04" PRIX64 ")",
              static_cast<char>((value >> 8) & 0xFF),
              static_cast<char>(value & 0xFF), value);
        } else if (entry->width == 4) {
          return string_printf("\'%c%c%c%c\' (0x%08" PRIX64 ")",
              static_cast<char>((value >> 24) & 0xFF),
              static_cast<char>((value >> 16) & 0xFF),
              static_cast<char>((value >> 8) & 0xFF),
              static_cast<char>(value & 0xFF),
              value);
        } else {
          throw logic_error("invalid integer width");
        }
      case Format::DATE: {
        // Classic Mac timestamps are based on 1904-01-01 instead of 1970-01-01
        int64_t ts = value - 2082826800;
        if (ts < 0) {
          // TODO: Handle this case properly. Probably it's quite rare
          return string_printf("%" PRId64 " seconds before 1970-01-01 00:00:00 (0x%08" PRIX64 ")",
              -ts, value);
        } else {
          return format_time(ts * 1000000) + string_printf(" (0x%" PRIX64 ")", value);
        }
      }
      default:
        throw logic_error("invalid integer display format");
    }
  };

  static auto align_to_boundary = +[](StringReader& r, uint8_t boundary, uint8_t offset) {
    if (boundary == 0) {
      return; // No alignment requested (for optionally-aligned types like PSTRING)
    }
    // We currently only support offset == 0 or (offset == 1 and boundary == 2)
    // since those seem to be the only cases supported by the TMPL format.
    if (offset == 1) {
      if (boundary != 2) {
        throw logic_error("boundary must be 2 when offset is 1");
      }
      if (!(r.where() & 1)) {
        r.skip(1);
      }
    } else if (offset == 0) {
      r.go((r.where() + (boundary - 1)) & (~(boundary - 1)));
    } else {
      throw logic_error("offset is not 1 or 0");
    }
  };

  for (const auto& entry : entries) {
    string prefix(indent_level * 2, ' ');
    if (entry->type == Type::VOID) {
      if (entry->name.empty()) {
        lines.emplace_back(prefix + "# (empty comment)");
      } else {
        lines.emplace_back(prefix + "# " + entry->name);
      }
      continue;
    }

    if (!entry->name.empty()) {
      prefix += entry->name;
      prefix += ": ";
    }

    switch (entry->type) {
      // Note: Type::VOID is already handled above
      case Type::ZERO_FILL:
        if ((entry->width != 1) && (entry->width != 2) && (entry->width != 4)) {
          string data = r.readx(entry->width);
          if (data.find_first_not_of('\0') != string::npos) {
            lines.emplace_back(prefix + format_data_string(data) + " (type = zero fill in template)");
          }
          continue;
        }

        // Handle ZERO_FILL like INTEGER if its size matches an integer
        [[fallthrough]];
      case Type::INTEGER: {
        int64_t value;
        if (entry->is_signed) {
          if (entry->width == 1) {
            value = r.get_s8();
          } else if (entry->width == 2) {
            value = r.get_s16b();
          } else if (entry->width == 4) {
            value = r.get_s32b();
          } else {
            throw logic_error("invalid width in disassemble_from_template");
          }
        } else {
          if (entry->width == 1) {
            value = r.get_u8();
          } else if (entry->width == 2) {
            value = r.get_u16b();
          } else if (entry->width == 4) {
            value = r.get_u32b();
          } else {
            throw logic_error("invalid width in disassemble_from_template");
          }
        }
        if (entry->end_alignment) {
          throw logic_error("integer has nonzero end_alignment");
        }

        if (entry->type == Type::INTEGER) {
          lines.emplace_back(prefix + format_integer(entry, value));
        } else if (entry->type == Type::ZERO_FILL && value != 0) {
          lines.emplace_back(prefix + format_integer(entry, value) + " (type = zero fill in template)");
        }
        break;
      }
      case Type::ALIGNMENT:
        align_to_boundary(r, entry->end_alignment, entry->align_offset);
        break;
      case Type::FIXED_POINT: {
        int16_t integer_part = r.get_s16b();
        uint16_t fractional_part = r.get_u16b();
        if (entry->format == Format::DECIMAL) {
          double value = (integer_part >= 0)
              ? (integer_part + static_cast<double>(fractional_part) / 65536)
              : (integer_part - static_cast<double>(fractional_part) / 65536);
          lines.emplace_back(string_printf("%s%lg\n", prefix.c_str(), value));
        } else if (entry->format == Format::HEX) {
          lines.emplace_back(string_printf("%s%s0x%d.0x%hu\n", prefix.c_str(),
              integer_part < 0 ? "-" : "",
              (integer_part < 0) ? -integer_part : integer_part,
              fractional_part));
        } else {
          throw logic_error("invalid fixed-point display format");
        }
        break;
      }
      case Type::EOF_STRING:
        lines.emplace_back(prefix + format_string(entry->format, r.read(r.remaining())));
        break;
      case Type::STRING:
        lines.emplace_back(prefix + format_string(entry->format, r.readx(entry->width)));
        break;
      case Type::PSTRING:
      case Type::CSTRING: {
        string data;
        if (entry->type == Type::PSTRING) {
          data = r.readx(r.get_u8());
        } else {
          data = r.get_cstr();
        }
        lines.emplace_back(prefix + format_string(entry->format, data));

        if (entry->end_alignment == 2) {
          if (((data.size() + 1) & 1) != (entry->align_offset)) {
            r.skip(1);
          }
        } else if (entry->end_alignment) {
          throw logic_error("unsupported pstring end alignment");
        }
        break;
      }
      case Type::FIXED_PSTRING: {
        size_t size = r.get_u8();
        if (size > entry->width) {
          throw runtime_error("p-string too long for field");
        }
        lines.emplace_back(prefix + format_string(entry->format, r.readx(size)));
        r.skip(entry->width - size);
        break;
      }
      case Type::FIXED_CSTRING: {
        string data = r.get_cstr();
        if (data.size() > static_cast<size_t>(entry->width + 1)) {
          throw runtime_error("c-string too long for field");
        }
        lines.emplace_back(prefix + format_string(entry->format, data));
        r.skip(entry->width - data.size() - 1);
        break;
      }
      case Type::BOOL:
        // Note: Yes, Type::BOOL apparently is actually 2 bytes.
        lines.emplace_back(prefix + (r.get_u16b() ? "true" : "false"));
        break;
      case Type::POINT_2D: {
        Point pt = r.get<Point>();
        string x_str = format_integer(entry, pt.x);
        string y_str = format_integer(entry, pt.y);
        lines.emplace_back(prefix + "x=" + x_str + ", y=" + y_str);
        break;
      }
      case Type::RECT: {
        Rect rect = r.get<Rect>();
        string x1_str = format_integer(entry, rect.x1);
        string y1_str = format_integer(entry, rect.y1);
        string x2_str = format_integer(entry, rect.x2);
        string y2_str = format_integer(entry, rect.y2);
        lines.emplace_back(prefix + "x1=" + x1_str + ", y1=" + y1_str + ", x2=" + x2_str + ", y2=" + y2_str);
        break;
      }
      case Type::COLOR: {
        Color c = r.get<Color>();
        string r_str = format_integer(entry, c.r);
        string g_str = format_integer(entry, c.g);
        string b_str = format_integer(entry, c.b);
        lines.emplace_back(prefix + "r=" + r_str + ", g=" + g_str + ", b=" + b_str);
        break;
      }
      case Type::BITFIELD: {
        uint8_t flags = r.get_u8();
        for (const auto& bit_entry : entry->list_entries) {
          lines.emplace_back(prefix + bit_entry->name + ": " + ((flags & 0x80) ? "true" : "false"));
          flags <<= 1;
        }
        break;
      }
      case Type::LIST_ZERO_BYTE:
        lines.emplace_back(prefix + "(zero-terminated list)");
        for (size_t z = 0; r.get_u8(false); z++) {
          string item_prefix(indent_level * 2, ' ');
          item_prefix += entry->name;
          lines.emplace_back(item_prefix + string_printf("[%zu]", z));
          disassemble_from_template_inner(lines, r, entry->list_entries, indent_level + 1);
        }
        break;
      case Type::LIST_EOF:
        lines.emplace_back(prefix + "(EOF-terminated list)");
        for (size_t z = 0; !r.eof(); z++) {
          string item_prefix(indent_level * 2, ' ');
          item_prefix += entry->name;
          lines.emplace_back(item_prefix + string_printf("[%zu]", z));
          disassemble_from_template_inner(lines, r, entry->list_entries, indent_level + 1);
        }
        break;
      case Type::LIST_ZERO_COUNT:
      case Type::LIST_ONE_COUNT: {
        size_t num_items;
        if (entry->width == 2) {
          num_items = r.get_u16b() + (entry->type == Type::LIST_ZERO_COUNT);
          // 0xFFFF actually means zero in LIST_ZERO_COUNT
          if (num_items == 0x10000) {
            num_items = 0;
          }
        } else if (entry->width == 4) {
          // It's (currently) not possible to get a LIST_ZERO_COUNT with a
          // 4-byte width field
          if (entry->type == Type::LIST_ZERO_COUNT) {
            throw logic_error("4-byte width LIST_ZERO_COUNT");
          }
          num_items = r.get_u32b();
        } else {
          throw logic_error("invalid list length width");
        }
        lines.emplace_back(prefix + string_printf("(%zu entries)", num_items));
        for (size_t z = 0; z < num_items; z++) {
          string item_prefix(indent_level * 2, ' ');
          item_prefix += entry->name;
          lines.emplace_back(item_prefix + string_printf("[%zu]", z));
          disassemble_from_template_inner(lines, r, entry->list_entries, indent_level + 1);
        }
        break;
      }
      default:
        throw logic_error("unknown field type in disassemble_from_template");
    }
  }
}

string ResourceFile::disassemble_from_template(
    const void* data,
    size_t size,
    const ResourceFile::TemplateEntryList& tmpl) {
  StringReader r(data, size);
  deque<string> lines;
  disassemble_from_template_inner(lines, r, tmpl, 0);
  if (!r.eof()) {
    string extra_data = r.read(r.remaining());
    lines.emplace_back("\nNote: template did not parse all data in resource; remaining data: "
        + format_data_string(extra_data));
  }
  return join(lines, "\n");
}



////////////////////////////////////////////////////////////////////////////////
// CODE helpers

struct SizeResource {
  be_uint16_t flags;
  be_uint32_t size;
  be_uint32_t min_size;
};

ResourceFile::DecodedSizeResource ResourceFile::decode_SIZE(int16_t id, uint32_t type) {
  return this->decode_SIZE(this->get_resource(type, id));
}

ResourceFile::DecodedSizeResource ResourceFile::decode_SIZE(shared_ptr<const Resource> res) {
  return ResourceFile::decode_SIZE(res->data.data(), res->data.size());
}

ResourceFile::DecodedSizeResource ResourceFile::decode_SIZE(const void* vdata, size_t size) {
  if (size < sizeof(SizeResource)) {
    throw runtime_error("SIZE too small for structure");
  }

  const auto* r = reinterpret_cast<const SizeResource*>(vdata);

  DecodedSizeResource decoded;
  decoded.save_screen = !!(r->flags & 0x8000);
  decoded.accept_suspend_events = !!(r->flags & 0x4000);
  decoded.disable_option = !!(r->flags & 0x2000);
  decoded.can_background = !!(r->flags & 0x1000);
  decoded.activate_on_fg_switch = !!(r->flags & 0x0800);
  decoded.only_background = !!(r->flags & 0x0400);
  decoded.get_front_clicks = !!(r->flags & 0x0200);
  decoded.accept_died_events = !!(r->flags & 0x0100);
  decoded.clean_addressing = !!(r->flags & 0x0080);
  decoded.high_level_event_aware = !!(r->flags & 0x0040);
  decoded.local_and_remote_high_level_events = !!(r->flags & 0x0020);
  decoded.stationery_aware = !!(r->flags & 0x0010);
  decoded.use_text_edit_services = !!(r->flags & 0x0008);
  // Low 3 bits in r->flags are unused
  decoded.size = r->size;
  decoded.min_size = r->min_size;
  return decoded;
}

ResourceFile::DecodedVersionResource ResourceFile::decode_vers(int16_t id, uint32_t type) {
  return this->decode_vers(this->get_resource(type, id));
}

ResourceFile::DecodedVersionResource ResourceFile::decode_vers(shared_ptr<const Resource> res) {
  return ResourceFile::decode_vers(res->data.data(), res->data.size());
}

ResourceFile::DecodedVersionResource ResourceFile::decode_vers(const void* vdata, size_t size) {
  StringReader r(vdata, size);
  DecodedVersionResource decoded;
  decoded.major_version = r.get_u8();
  decoded.minor_version = r.get_u8();
  decoded.development_stage = r.get_u8();
  decoded.prerelease_version_level = r.get_u8();
  decoded.region_code = r.get_u16b();
  decoded.version_number = r.readx(r.get_u8());
  decoded.version_message = r.readx(r.get_u8());
  return decoded;
}

struct CodeFragmentResourceEntry {
  be_uint32_t architecture;
  be_uint16_t reserved1;
  uint8_t reserved2;
  uint8_t update_level;
  be_uint32_t current_version;
  be_uint32_t old_def_version;
  be_uint32_t app_stack_size;
  union {
    be_int16_t app_subdir_id;
    be_uint16_t lib_flags;
  };

  // Values for usage:
  // kImportLibraryCFrag   = 0 // Standard CFM import library
  // kApplicationCFrag     = 1 // MacOS application
  // kDropInAdditionCFrag  = 2 // Application or library private extension/plug-in
  // kStubLibraryCFrag     = 3 // Import library used for linking only
  // kWeakStubLibraryCFrag = 4 // Import library used for linking only and will be automatically weak linked
  uint8_t usage;

  // Values for where:
  // kMemoryCFragLocator        = 0 // Container is already addressable
  // kDataForkCFragLocator      = 1 // Container is in a file's data fork
  // kResourceCFragLocator      = 2 // Container is in a file's resource fork
  // kByteStreamCFragLocator    = 3 // Reserved
  // kNamedFragmentCFragLocator = 4 // Reserved
  uint8_t where;

  be_uint32_t offset;
  be_uint32_t length; // If zero, fragment fills the entire space (e.g. entire data fork)
  union {
    be_uint32_t space_id;
    be_uint32_t fork_kind;
  };
  be_uint16_t fork_instance;
  be_uint16_t extension_count;
  be_uint16_t entry_size; // Total size of this entry (incl. name) in bytes
  char name[0]; // p-string (first byte is length)
};

struct CodeFragmentResourceHeader {
  be_uint32_t reserved1;
  be_uint32_t reserved2;
  be_uint16_t reserved3;
  be_uint16_t version;
  be_uint32_t reserved4;
  be_uint32_t reserved5;
  be_uint32_t reserved6;
  be_uint32_t reserved7;
  be_uint16_t reserved8;
  be_uint16_t entry_count;
  // Entries immediately follow this field
};

vector<ResourceFile::DecodedCodeFragmentEntry> ResourceFile::decode_cfrg(
    int16_t id, uint32_t type) {
  return this->decode_cfrg(this->get_resource(type, id));
}

vector<ResourceFile::DecodedCodeFragmentEntry> ResourceFile::decode_cfrg(
    shared_ptr<const Resource> res) {
  return ResourceFile::decode_cfrg(res->data.data(), res->data.size());
}

vector<ResourceFile::DecodedCodeFragmentEntry> ResourceFile::decode_cfrg(
    const void* vdata, size_t size) {
  StringReader r(vdata, size);

  const auto& header = r.get<CodeFragmentResourceHeader>();
  if (header.version != 1) {
    throw runtime_error("cfrg is not version 1");
  }

  vector<DecodedCodeFragmentEntry> ret;
  while (ret.size() < header.entry_count) {
    size_t entry_start_offset = r.where();

    const auto& src_entry = r.get<CodeFragmentResourceEntry>();
    string name = r.readx(r.get_u8());

    ret.emplace_back();
    auto& ret_entry = ret.back();

    ret_entry.architecture = src_entry.architecture;
    ret_entry.update_level = src_entry.update_level;
    ret_entry.current_version = src_entry.current_version;
    ret_entry.old_def_version = src_entry.old_def_version;
    ret_entry.app_stack_size = src_entry.app_stack_size;
    ret_entry.app_subdir_id = src_entry.app_subdir_id; // Also lib_flags

    if (src_entry.usage > 4) {
      throw runtime_error("code fragment entry usage is invalid");
    }
    ret_entry.usage = static_cast<DecodedCodeFragmentEntry::Usage>(src_entry.usage);

    if (src_entry.usage > 4) {
      throw runtime_error("code fragment entry location (where) is invalid");
    }
    ret_entry.where = static_cast<DecodedCodeFragmentEntry::Where>(src_entry.where);

    ret_entry.offset = src_entry.offset;
    ret_entry.length = src_entry.length;
    ret_entry.space_id = src_entry.space_id; // Also fork_kind
    ret_entry.fork_instance = src_entry.fork_instance;
    ret_entry.name = string(&src_entry.name[1], static_cast<size_t>(src_entry.name[0]));
    ret_entry.extension_count = src_entry.extension_count;

    // TODO: it looks like there is probably some alignment logic that we should
    // implement here (see System cfrg 0, for example)
    size_t extension_data_end_offset = entry_start_offset + src_entry.entry_size;
    if (r.where() > extension_data_end_offset) {
      throw runtime_error("code fragment entry size is smaller than header + name");
    }
    if (ret_entry.extension_count) {
      ret_entry.extension_data = r.readx(extension_data_end_offset - r.where());
    } else {
      r.skip(extension_data_end_offset - r.where());
    }
  }

  return ret;
}

struct Code0ResourceHeader {
  be_uint32_t above_a5_size;
  be_uint32_t below_a5_size;
  be_uint32_t jump_table_size; // Should be == resource_size - 0x10
  be_uint32_t jump_table_offset;

  struct MethodEntry {
    be_uint16_t offset; // Need to add 4 to this apparently
    be_uint16_t push_opcode;
    be_int16_t resource_id; // id of target CODE resource
    be_uint16_t trap_opcode; // Disassembles as `trap _LoadSeg`
  };

  MethodEntry entries[0];
};

struct CodeResourceHeader {
  be_uint16_t first_jump_table_entry;
  be_uint16_t num_jump_table_entries;
};

struct CodeResourceFarHeader {
  be_uint16_t entry_offset; // 0xFFFF
  be_uint16_t unused; // 0x0000
  be_uint32_t near_entry_start_a5_offset;
  be_uint32_t near_entry_count;
  be_uint32_t far_entry_start_a5_offset;
  be_uint32_t far_entry_count;
  be_uint32_t a5_relocation_data_offset;
  be_uint32_t a5;
  be_uint32_t pc_relocation_data_offset;
  be_uint32_t load_address;
  be_uint32_t reserved; // 0x00000000
};

vector<uint32_t> parse_relocation_data(StringReader& r) {
  // Note: we intentionally do not check r.eof here, since the format has an
  // explicit end command
  vector<uint32_t> ret;
  uint32_t offset = 0;
  for (;;) {
    uint8_t a = r.get_u8();
    if (a == 0) {
      a = r.get_u8();
      if (a == 0) {
        return ret;
      } else if (a & 0x80) {
        offset += (static_cast<uint32_t>(a & 0x7F) << 25) | (r.get_u24b() << 1);
      } else {
        throw runtime_error("invalid relocation command (0001-007F)");
      }
    } else if (a & 0x80) {
      offset += (static_cast<uint16_t>(a & 0x7F) << 9) | (r.get_u8() << 1);
    } else {
      offset += (a << 1);
    }
    ret.push_back(offset);
  }
}

ResourceFile::DecodedCode0Resource ResourceFile::decode_CODE_0(int16_t id, uint32_t type) {
  return this->decode_CODE_0(this->get_resource(type, id));
}

ResourceFile::DecodedCode0Resource ResourceFile::decode_CODE_0(shared_ptr<const Resource> res) {
  return ResourceFile::decode_CODE_0(res->data.data(), res->data.size());
}

ResourceFile::DecodedCode0Resource ResourceFile::decode_CODE_0(
    const void* vdata, size_t size) {
  StringReader r(vdata, size);
  const auto& header = r.get<Code0ResourceHeader>();

  DecodedCode0Resource ret;
  ret.above_a5_size = header.above_a5_size;
  ret.below_a5_size = header.below_a5_size;

  // Some apps have what looks like a compressed jump table - it has a single
  // entry (usually pointing to the beginning of the last CODE resource),
  // followed by obviously non-jump-table data. Since this data often has a size
  // that isn't a multiple of 8, we can't just check r.eof() here.
  while (r.remaining() >= sizeof(Code0ResourceHeader::MethodEntry)) {
    const auto& e = r.get<Code0ResourceHeader::MethodEntry>();
    if (e.push_opcode != 0x3F3C || e.trap_opcode != 0xA9F0) {
      ret.jump_table.push_back({0, 0});
    } else {
      ret.jump_table.push_back({e.resource_id, e.offset});
    }
  }

  return ret;
}

ResourceFile::DecodedCodeResource ResourceFile::decode_CODE(int16_t id, uint32_t type) {
  return this->decode_CODE(this->get_resource(type, id));
}

ResourceFile::DecodedCodeResource ResourceFile::decode_CODE(shared_ptr<const Resource> res) {
  return ResourceFile::decode_CODE(res->data.data(), res->data.size());
}

ResourceFile::DecodedCodeResource ResourceFile::decode_CODE(
    const void* vdata, size_t size) {
  StringReader r(vdata, size);

  const auto& header = r.get<CodeResourceHeader>(false);

  DecodedCodeResource ret;
  if ((header.first_jump_table_entry == 0xFFFF) &&
      (header.num_jump_table_entries == 0x0000)) {
    const auto& far_header = r.get<CodeResourceFarHeader>();

    ret.first_jump_table_entry = -1;
    ret.num_jump_table_entries = 0;
    ret.near_entry_start_a5_offset = far_header.near_entry_start_a5_offset;
    ret.near_entry_count = far_header.near_entry_count;
    ret.far_entry_start_a5_offset = far_header.far_entry_start_a5_offset;
    ret.far_entry_count = far_header.far_entry_count;
    ret.a5_relocation_data_offset = far_header.a5_relocation_data_offset;
    ret.a5 = far_header.a5;
    ret.pc_relocation_data_offset = far_header.pc_relocation_data_offset;
    ret.load_address = far_header.load_address;

    {
      auto sub_r = r.sub(ret.a5_relocation_data_offset);
      ret.a5_relocation_addresses = parse_relocation_data(sub_r);
    }
    {
      auto sub_r = r.sub(ret.pc_relocation_data_offset);
      ret.pc_relocation_addresses = parse_relocation_data(sub_r);
    }

  } else {
    r.skip(sizeof(CodeResourceHeader));
    ret.first_jump_table_entry = header.first_jump_table_entry;
    ret.num_jump_table_entries = header.num_jump_table_entries;
  }

  ret.code = r.read(r.remaining());
  return ret;
}

struct DriverResourceHeader {
  be_uint16_t flags;
  be_uint16_t delay;
  be_uint16_t event_mask;
  be_int16_t menu_id;
  be_uint16_t open_label;
  be_uint16_t prime_label;
  be_uint16_t control_label;
  be_uint16_t status_label;
  be_uint16_t close_label;
};

ResourceFile::DecodedDriverResource ResourceFile::decode_DRVR(int16_t id, uint32_t type) {
  return this->decode_DRVR(this->get_resource(type, id));
}

ResourceFile::DecodedDriverResource ResourceFile::decode_DRVR(shared_ptr<const Resource> res) {
  return ResourceFile::decode_DRVR(res->data.data(), res->data.size());
}

ResourceFile::DecodedDriverResource ResourceFile::decode_DRVR(
    const void* data, size_t size) {
  StringReader r(data, size);

  const auto& header = r.get<DriverResourceHeader>();
  string name = r.readx(r.get_u8());

  // Code starts at the next word-aligned boundary after the name
  if (r.where() & 1) {
    r.skip(1);
  }
  size_t code_start_offset = r.where();

  auto handle_label = +[](int32_t* dest, uint16_t src, size_t code_start_offset, const char* name) {
    if (src == 0) {
      *dest = -1;
    } else if (src < code_start_offset) {
      throw runtime_error(string(name) + " label is before code start");
    } else {
      *dest = src - code_start_offset;
    }
  };

  DecodedDriverResource ret;
  ret.flags = header.flags;
  ret.delay = header.delay;
  ret.event_mask = header.event_mask;
  ret.menu_id = header.menu_id;
  handle_label(&ret.open_label, header.open_label, code_start_offset, "open");
  handle_label(&ret.prime_label, header.prime_label, code_start_offset, "prime");
  handle_label(&ret.control_label, header.control_label, code_start_offset, "control");
  handle_label(&ret.status_label, header.status_label, code_start_offset, "status");
  handle_label(&ret.close_label, header.close_label, code_start_offset, "close");
  ret.name = move(name);
  ret.code = r.read(r.remaining());
  return ret;
}

ResourceFile::DecodedDecompressorResource ResourceFile::decode_dcmp(int16_t id, uint32_t type) {
  return this->decode_dcmp(this->get_resource(type, id));
}

ResourceFile::DecodedDecompressorResource ResourceFile::decode_dcmp(shared_ptr<const Resource> res) {
  return ResourceFile::decode_dcmp(res->data.data(), res->data.size());
}

ResourceFile::DecodedDecompressorResource ResourceFile::decode_dcmp(const void* vdata, size_t size) {
  StringReader r(vdata, size);

  DecodedDecompressorResource ret;
  if ((r.pget_u8(0) == 0x60) && (r.pget_u32b(4) == RESOURCE_TYPE_dcmp)) {
    ret.init_label = -1;
    ret.decompress_label = 0;
    ret.exit_label = -1;
    ret.pc_offset = 0;
  } else {
    ret.init_label = r.get_u16b();
    ret.decompress_label = r.get_u16b();
    ret.exit_label = r.get_u16b();
    ret.pc_offset = 6;
  }
  ret.code = r.read(r.remaining());
  return ret;
}

PEFFFile ResourceFile::decode_peff(int16_t id, uint32_t type) {
  return this->decode_peff(this->get_resource(type, id));
}

PEFFFile ResourceFile::decode_peff(shared_ptr<const Resource> res) {
  return ResourceFile::decode_peff(res->data.data(), res->data.size());
}

PEFFFile ResourceFile::decode_peff(const void* data, size_t size) {
  return PEFFFile("__unnamed__", data, size);
}

ResourceFile::DecodedPEFFDriver ResourceFile::decode_expt(int16_t id, uint32_t type) {
  return this->decode_expt(this->get_resource(type, id));
}

ResourceFile::DecodedPEFFDriver ResourceFile::decode_expt(shared_ptr<const Resource> res) {
  return ResourceFile::decode_expt(res->data.data(), res->data.size());
}

ResourceFile::DecodedPEFFDriver ResourceFile::decode_expt(const void* data, size_t size) {
  if (size < 0x20) {
    throw runtime_error("expt too small for header");
  }
  // TODO: Figure out the format (and actual size) of this header and parse it
  return {string(reinterpret_cast<const char*>(data), 0x20),
          PEFFFile("__unnamed__", reinterpret_cast<const char*>(data) + 0x20, size - 0x20)};
}

ResourceFile::DecodedPEFFDriver ResourceFile::decode_nsrd(int16_t id, uint32_t type) {
  return this->decode_nsrd(this->get_resource(type, id));
}

ResourceFile::DecodedPEFFDriver ResourceFile::decode_nsrd(shared_ptr<const Resource> res) {
  return ResourceFile::decode_nsrd(res->data.data(), res->data.size());
}

ResourceFile::DecodedPEFFDriver ResourceFile::decode_nsrd(const void* data, size_t size) {
  if (size < 0x20) {
    throw runtime_error("nsrd too small for header");
  }
  // TODO: Figure out the format (and actual size) of this header and parse it
  return {string(reinterpret_cast<const char*>(data), 0x20),
          PEFFFile("__unnamed__", reinterpret_cast<const char*>(data) + 0x20, size - 0x20)};
}



////////////////////////////////////////////////////////////////////////////////
// Image resource decoding

ResourceFile::DecodedColorIconResource::DecodedColorIconResource(Image&& image,
    Image&& bitmap) : image(move(image)), bitmap(move(bitmap)) { }

ResourceFile::DecodedCursorResource::DecodedCursorResource(Image&& bitmap,
    uint16_t hotspot_x, uint16_t hotspot_y) : bitmap(move(bitmap)),
    hotspot_x(hotspot_x), hotspot_y(hotspot_y) { }

ResourceFile::DecodedColorCursorResource::DecodedColorCursorResource(
    Image&& image, Image&& bitmap, uint16_t hotspot_x, uint16_t hotspot_y) :
    image(move(image)), bitmap(move(bitmap)), hotspot_x(hotspot_x),
    hotspot_y(hotspot_y) { }

struct ColorIconResourceHeader {
  // pixMap fields
  be_uint32_t pix_map_unused;
  PixelMapHeader pix_map;

  // mask bitmap fields
  be_uint32_t mask_unused;
  BitMapHeader mask_header;

  // 1-bit icon bitmap fields
  be_uint32_t bitmap_unused;
  BitMapHeader bitmap_header;

  // icon data fields
  be_uint32_t icon_data; // ignored
};

ResourceFile::DecodedColorIconResource ResourceFile::decode_cicn(int16_t id, uint32_t type) {
  return this->decode_cicn(this->get_resource(type, id));
}

ResourceFile::DecodedColorIconResource ResourceFile::decode_cicn(shared_ptr<const Resource> res) {
  return ResourceFile::decode_cicn(res->data.data(), res->data.size());
}

ResourceFile::DecodedColorIconResource ResourceFile::decode_cicn(const void* vdata, size_t size) {
  if (size < sizeof(ColorIconResourceHeader)) {
    throw runtime_error("cicn too small for header");
  }

  const uint8_t* bdata = reinterpret_cast<const uint8_t*>(vdata);

  const auto* header = reinterpret_cast<const ColorIconResourceHeader*>(bdata);

  // The mask is required, but the bitmap may be missing
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

  size_t mask_map_size = PixelMapData::size(
      header->mask_header.flags_row_bytes, header->mask_header.bounds.height());
  const auto* mask_map = reinterpret_cast<const PixelMapData*>(bdata + sizeof(*header));
  if (sizeof(*header) + mask_map_size > size) {
    throw runtime_error("mask map too large");
  }

  size_t bitmap_size = PixelMapData::size(
      header->bitmap_header.flags_row_bytes, header->bitmap_header.bounds.height());
  const auto* bitmap = reinterpret_cast<const PixelMapData*>(bdata + sizeof(*header) + mask_map_size);
  if (sizeof(*header) + mask_map_size + bitmap_size > size) {
    throw runtime_error("bitmap too large");
  }

  const auto* ctable = reinterpret_cast<const ColorTable*>(
      bdata + sizeof(*header) + mask_map_size + bitmap_size);
  if (sizeof(*header) + mask_map_size + bitmap_size + sizeof(*ctable) > size) {
    throw runtime_error("color table header too large");
  }
  if (ctable->num_entries < 0) {
    throw runtime_error("color table has negative size");
  }
  if (sizeof(*header) + mask_map_size + bitmap_size + ctable->size() > size) {
    throw runtime_error("color table contents too large");
  }

  // Decode the image data
  size_t pixel_map_size = PixelMapData::size(
      header->pix_map.flags_row_bytes & 0x3FFF, header->pix_map.bounds.height());
  const auto* pixel_map = reinterpret_cast<const PixelMapData*>(
      bdata + sizeof(*header) + mask_map_size + bitmap_size + ctable->size());
  if (sizeof(*header) + mask_map_size + bitmap_size + ctable->size() + pixel_map_size > size) {
    throw runtime_error("pixel map too large");
  }

  Image img = decode_color_image(header->pix_map, *pixel_map, ctable, mask_map,
      header->mask_header.flags_row_bytes);

  // Decode the mask and bitmap
  Image bitmap_img(header->bitmap_header.flags_row_bytes ? header->bitmap_header.bounds.width() : 0,
      header->bitmap_header.flags_row_bytes ? header->bitmap_header.bounds.height() : 0, true);
  for (ssize_t y = 0; y < header->pix_map.bounds.height(); y++) {
    for (ssize_t x = 0; x < header->pix_map.bounds.width(); x++) {
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

  return DecodedColorIconResource(move(img), move(bitmap_img));
}



struct ColorCursorResourceHeader {
  be_uint16_t type; // 0x8000 (monochrome) or 0x8001 (color)
  be_uint32_t pixel_map_offset; // offset from beginning of resource data
  be_uint32_t pixel_data_offset; // offset from beginning of resource data
  be_uint32_t expanded_data; // ignore this (Color QuickDraw stuff)
  be_uint16_t expanded_depth;
  be_uint32_t unused;
  uint8_t bitmap[0x20];
  uint8_t mask[0x20];
  be_uint16_t hotspot_x;
  be_uint16_t hotspot_y;
  be_uint32_t color_table_offset; // offset from beginning of resource
  be_uint32_t cursor_id; // ignore this (resource id)
};

ResourceFile::DecodedColorCursorResource ResourceFile::decode_crsr(int16_t id, uint32_t type) {
  return this->decode_crsr(this->get_resource(type, id));
}

ResourceFile::DecodedColorCursorResource ResourceFile::decode_crsr(shared_ptr<const Resource> res) {
  return ResourceFile::decode_crsr(res->data.data(), res->data.size());
}

ResourceFile::DecodedColorCursorResource ResourceFile::decode_crsr(const void* vdata, size_t size) {
  if (size < sizeof(ColorCursorResourceHeader)) {
    throw runtime_error("crsr too small for header");
  }

  const uint8_t* bdata = reinterpret_cast<const uint8_t*>(vdata);

  const auto* header = reinterpret_cast<const ColorCursorResourceHeader*>(bdata);

  if ((header->type & 0xFFFE) != 0x8000) {
    throw runtime_error("unknown crsr type");
  }

  Image bitmap = decode_monochrome_image_masked(&header->bitmap, 0x40, 16, 16);

  // Get the pixel map header
  const auto* pixmap_header = reinterpret_cast<const PixelMapHeader*>(
      bdata + header->pixel_map_offset + 4);
  if (header->pixel_map_offset + sizeof(*pixmap_header) > size) {
    throw runtime_error("pixel map header too large");
  }

  // Get the pixel map data
  size_t pixel_map_size = PixelMapData::size(
      pixmap_header->flags_row_bytes & 0x3FFF, pixmap_header->bounds.height());
  if (header->pixel_data_offset + pixel_map_size > size) {
    throw runtime_error("pixel map data too large");
  }
  const auto* pixmap_data = reinterpret_cast<const PixelMapData*>(
      bdata + header->pixel_data_offset);

  // Get the color table
  const auto* ctable = reinterpret_cast<const ColorTable*>(
      bdata + pixmap_header->color_table_offset);
  if (pixmap_header->color_table_offset + sizeof(*ctable) > size) {
    throw runtime_error("color table header too large");
  }
  if (static_cast<be_int16_t>(ctable->num_entries) < 0) {
    throw runtime_error("color table has negative size");
  }
  if (pixmap_header->color_table_offset + ctable->size() > size) {
    throw runtime_error("color table contents too large");
  }

  // Decode the color image
  Image img = apply_alpha_from_mask(
      decode_color_image(*pixmap_header, *pixmap_data, ctable), bitmap);

  return DecodedColorCursorResource(move(img), move(bitmap), header->hotspot_x,
      header->hotspot_y);
}



struct PixelPatternResourceHeader {
  be_uint16_t type;
  be_uint32_t pixel_map_offset;
  be_uint32_t pixel_data_offset;
  be_uint32_t unused1; // TMPL: "Expanded pixel image" (probably ptr to decompressed data when used by QuickDraw)
  be_uint16_t unused2; // TMPL: "Pattern valid flag" (unused in stored resource)
  be_uint32_t reserved; // TMPL: "Expanded pattern"
  uint8_t monochrome_pattern[8];
};

static ResourceFile::DecodedPattern decode_ppat_data(StringReader& r) {
  const auto& header = r.get<PixelPatternResourceHeader>();

  Image monochrome_pattern = decode_monochrome_image(header.monochrome_pattern,
      8, 8, 8);

  // Type 0 is monochrome; type 1 is indexed color; type 2 is apparently RGB
  // color, but it's not clear if these are ever stored in resources or only
  // used when loaded in memory
  if ((header.type == 0) || (header.type == 2)) {
    return {monochrome_pattern, monochrome_pattern};
  }
  if ((header.type != 1) && (header.type != 3)) {
    throw runtime_error("unknown ppat type");
  }

  // Get the pixel map header
  const auto& pixmap_header = r.pget<PixelMapHeader>(
      header.pixel_map_offset + 4);

  // Get the pixel map data
  size_t pixel_map_size = PixelMapData::size(
      pixmap_header.flags_row_bytes & 0x3FFF, pixmap_header.bounds.height());
  const auto& pixmap_data = r.pget<PixelMapData>(header.pixel_data_offset,
      pixel_map_size);

  // Get the color table
  const auto& ctable = r.pget<ColorTable>(pixmap_header.color_table_offset);
  if (ctable.num_entries < 0) {
    throw runtime_error("color table has negative size");
  }
  // We can't know the color table size until we have the struct, so pget it
  // again with the correct size to check that it's all within bounds
  r.pget<ColorTable>(pixmap_header.color_table_offset, ctable.size());

  // Decode the color image
  Image pattern = decode_color_image(pixmap_header, pixmap_data, &ctable);

  return {move(pattern), move(monochrome_pattern)};
}

ResourceFile::DecodedPattern ResourceFile::decode_ppat(int16_t id, uint32_t type) {
  return this->decode_ppat(this->get_resource(type, id));
}

ResourceFile::DecodedPattern ResourceFile::decode_ppat(shared_ptr<const Resource> res) {
  return ResourceFile::decode_ppat(res->data.data(), res->data.size());
}

ResourceFile::DecodedPattern ResourceFile::decode_ppat(const void* data, size_t size) {
  StringReader r(data, size);
  return decode_ppat_data(r);
}

vector<ResourceFile::DecodedPattern> ResourceFile::decode_pptN(int16_t id, uint32_t type) {
  return this->decode_pptN(this->get_resource(type, id));
}

vector<ResourceFile::DecodedPattern> ResourceFile::decode_pptN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_pptN(res->data.data(), res->data.size());
}

vector<ResourceFile::DecodedPattern> ResourceFile::decode_pptN(const void* vdata, size_t size) {
  // These resources are composed of a 2-byte count field, then N 4-byte
  // offsets, then the ppat data

  StringReader r(vdata, size);

  uint16_t count = r.get_u16b();
  vector<uint32_t> offsets;
  while (offsets.size() < count) {
    offsets.emplace_back(r.get_u32b());
  }
  offsets.emplace_back(size);

  vector<DecodedPattern> ret;
  for (size_t x = 0; x < count; x++) {
    StringReader sub_r = r.subx(offsets[x], offsets[x + 1] - offsets[x]);
    ret.emplace_back(decode_ppat_data(sub_r));
  }
  return ret;
}

Image ResourceFile::decode_PAT(int16_t id, uint32_t type) {
  return this->decode_PAT(this->get_resource(type, id));
}

Image ResourceFile::decode_PAT(shared_ptr<const Resource> res) {
  return ResourceFile::decode_PAT(res->data.data(), res->data.size());
}

Image ResourceFile::decode_PAT(const void* data, size_t size) {
  if (size != 8) {
    throw runtime_error("PAT not exactly 8 bytes in size");
  }
  return decode_monochrome_image(data, size, 8, 8);
}

struct PatternSequenceResourceHeader {
  uint16_t num_patterns;
  uint64_t pattern_data[0];
};

vector<Image> ResourceFile::decode_PATN(int16_t id, uint32_t type) {
  return this->decode_PATN(this->get_resource(type, id));
}

vector<Image> ResourceFile::decode_PATN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_PATN(res->data.data(), res->data.size());
}

vector<Image> ResourceFile::decode_PATN(const void* vdata, size_t size) {
  if (size < 2) {
    throw runtime_error("PAT# not large enough for count");
  }

  string data(reinterpret_cast<const char*>(vdata), size);
  uint16_t num_patterns = *reinterpret_cast<be_uint16_t*>(data.data());

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
  return this->decode_SICN(this->get_resource(type, id));
}

vector<Image> ResourceFile::decode_SICN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_SICN(res->data.data(), res->data.size());
}

vector<Image> ResourceFile::decode_SICN(const void* vdata, size_t size) {
  // So simple, there isn't even a header struct! SICN resources are just
  // several 0x20-byte monochrome images concatenated together.

  if (size & 0x1F) {
    throw runtime_error("SICN size not a multiple of 32");
  }

  vector<Image> ret;
  while (ret.size() < (size >> 5)) {
    const uint8_t* bdata = reinterpret_cast<const uint8_t*>(vdata) +
        (ret.size() * 0x20);
    ret.emplace_back(decode_monochrome_image(bdata, 0x20, 16, 16));
  }
  return ret;
}

Image ResourceFile::decode_ics8(int16_t id, uint32_t type) {
  return this->decode_ics8(this->get_resource(type, id));
}

Image ResourceFile::decode_ics8(shared_ptr<const Resource> res) {
  Image decoded = decode_8bit_image(res->data.data(), res->data.size(), 16, 16);
  try {
    uint32_t mask_type = (res->type & 0xFFFFFF00) | '#';
    Image mask = this->decode_icsN(res->id, mask_type);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_kcs8(int16_t id, uint32_t type) {
  return this->decode_kcs8(this->get_resource(type, id));
}

Image ResourceFile::decode_kcs8(shared_ptr<const Resource> res) {
  return this->decode_ics8(res);
}

Image ResourceFile::decode_icl8(int16_t id, uint32_t type) {
  return this->decode_icl8(this->get_resource(type, id));
}

Image ResourceFile::decode_icl8(shared_ptr<const Resource> res) {
  Image decoded = decode_8bit_image(res->data.data(), res->data.size(), 32, 32);
  try {
    Image mask = this->decode_ICNN(res->id, RESOURCE_TYPE_ICNN);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_icm8(int16_t id, uint32_t type) {
  return this->decode_icm8(this->get_resource(type, id));
}

Image ResourceFile::decode_icm8(shared_ptr<const Resource> res) {
  Image decoded = decode_8bit_image(res->data.data(), res->data.size(), 16, 12);
  try {
    Image mask = this->decode_icmN(res->id, RESOURCE_TYPE_icmN);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_ics4(int16_t id, uint32_t type) {
  return this->decode_ics4(this->get_resource(type, id));
}

Image ResourceFile::decode_ics4(shared_ptr<const Resource> res) {
  Image decoded = decode_4bit_image(res->data.data(), res->data.size(), 16, 16);
  try {
    uint32_t mask_type = (res->type & 0xFFFFFF00) | '#';
    Image mask = this->decode_icsN(res->id, mask_type);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_kcs4(int16_t id, uint32_t type) {
  return this->decode_kcs4(this->get_resource(type, id));
}

Image ResourceFile::decode_kcs4(shared_ptr<const Resource> res) {
  return this->decode_ics4(res);
}

Image ResourceFile::decode_icl4(int16_t id, uint32_t type) {
  return this->decode_icl4(this->get_resource(type, id));
}

Image ResourceFile::decode_icl4(shared_ptr<const Resource> res) {
  Image decoded = decode_4bit_image(res->data.data(), res->data.size(), 32, 32);
  try {
    Image mask = this->decode_ICNN(res->id, RESOURCE_TYPE_ICNN);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_icm4(int16_t id, uint32_t type) {
  return this->decode_icm4(this->get_resource(type, id));
}

Image ResourceFile::decode_icm4(shared_ptr<const Resource> res) {
  Image decoded = decode_4bit_image(res->data.data(), res->data.size(), 16, 12);
  try {
    Image mask = this->decode_icmN(res->id, RESOURCE_TYPE_icmN);
    return apply_alpha_from_mask(decoded, mask);
  } catch (const exception&) {
    return decoded;
  }
}

Image ResourceFile::decode_ICON(int16_t id, uint32_t type) {
  return this->decode_ICON(this->get_resource(type, id));
}

Image ResourceFile::decode_ICON(shared_ptr<const Resource> res) {
  return ResourceFile::decode_ICON(res->data.data(), res->data.size());
}

Image ResourceFile::decode_ICON(const void* data, size_t size) {
  return decode_monochrome_image(data, size, 32, 32);
}

struct CursorResource {
  uint8_t bitmap[0x20];
  uint8_t mask[0x20];
  be_uint16_t hotspot_x;
  be_uint16_t hotspot_y;
};

ResourceFile::DecodedCursorResource ResourceFile::decode_CURS(int16_t id, uint32_t type) {
  return this->decode_CURS(this->get_resource(type, id));
}

ResourceFile::DecodedCursorResource ResourceFile::decode_CURS(shared_ptr<const Resource> res) {
  return ResourceFile::decode_CURS(res->data.data(), res->data.size());
}

ResourceFile::DecodedCursorResource ResourceFile::decode_CURS(const void* vdata, size_t size) {
  if (size < 0x40) {
    throw runtime_error("CURS resource is too small");
  }

  const auto* header = reinterpret_cast<const CursorResource*>(vdata);

  Image img = decode_monochrome_image_masked(header, 0x40, 16, 16);
  return DecodedCursorResource(move(img),
      (size >= 0x42) ? header->hotspot_x.load() : 0xFFFF,
      (size >= 0x44) ? header->hotspot_y.load() : 0xFFFF);
}

Image ResourceFile::decode_ICNN(int16_t id, uint32_t type) {
  return this->decode_ICNN(this->get_resource(type, id));
}

Image ResourceFile::decode_ICNN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_ICNN(res->data.data(), res->data.size());
}

Image ResourceFile::decode_ICNN(const void* data, size_t size) {
  return decode_monochrome_image_masked(data, size, 32, 32);
}

Image ResourceFile::decode_icsN(int16_t id, uint32_t type) {
  return this->decode_icsN(this->get_resource(type, id));
}

Image ResourceFile::decode_icsN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_icsN(res->data.data(), res->data.size());
}

Image ResourceFile::decode_icsN(const void* data, size_t size) {
  return decode_monochrome_image_masked(data, size, 16, 16);
}

Image ResourceFile::decode_kcsN(int16_t id, uint32_t type) {
  return this->decode_kcsN(this->get_resource(type, id));
}

Image ResourceFile::decode_kcsN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_kcsN(res->data.data(), res->data.size());
}

Image ResourceFile::decode_kcsN(const void* data, size_t size) {
  return ResourceFile::decode_icsN(data, size);
}

Image ResourceFile::decode_icmN(int16_t id, uint32_t type) {
  return this->decode_icmN(this->get_resource(type, id));
}

Image ResourceFile::decode_icmN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_icmN(res->data.data(), res->data.size());
}

Image ResourceFile::decode_icmN(const void* data, size_t size) {
  return decode_monochrome_image_masked(data, size, 16, 12);
}

class QuickDrawResourceDasmPort : public QuickDrawPortInterface {
public:
  QuickDrawResourceDasmPort(ResourceFile* rf, size_t x, size_t y)
    : bounds(0, 0, y, x),
      clip_region(this->bounds),
      foreground_color(0xFFFF, 0xFFFF, 0xFFFF),
      background_color(0x0000, 0x0000, 0x0000),
      highlight_color(0xFFFF, 0x0000, 0xFFFF), // TODO: use the right color here
      op_color(0xFFFF, 0xFFFF, 0x0000), // TODO: use the right color here
      extra_space_nonspace(0),
      extra_space_space(0, 0),
      pen_loc(0, 0),
      pen_loc_frac(0),
      pen_size(1, 1),
      pen_mode(0), // TODO
      pen_visibility(0), // visible
      text_font(0), // TODO
      text_mode(0), // TODO
      text_size(0), // TODO
      text_style(0),
      foreground_color_index(0),
      background_color_index(0),
      pen_pixel_pattern(0, 0),
      fill_pixel_pattern(0, 0),
      background_pixel_pattern(0, 0),
      pen_mono_pattern(0xFFFFFFFFFFFFFFFF),
      fill_mono_pattern(0xAA55AA55AA55AA55),
      background_mono_pattern(0x0000000000000000),
      rf(rf),
      img(0, 0) {
    if (x >= 0x10000 || y >= 0x10000) {
      throw runtime_error("PICT resources cannot specify images larger than 65535x65535");
    }
    this->img = Image(x, y);
  }
  virtual ~QuickDrawResourceDasmPort() = default;

  const Image& image() const {
    return this->img;
  }

  // Image data accessors (Image, pixel map, or bitmap)
  virtual size_t width() const {
    return this->img.get_width();
  }
  virtual size_t height() const {
    return this->img.get_height();
  }
  virtual void write_pixel(ssize_t x, ssize_t y, uint8_t r, uint8_t g, uint8_t b) {
    this->img.write_pixel(x, y, r, g, b);
  }
  virtual void blit(const Image& src, ssize_t dest_x, ssize_t dest_y,
      size_t w, size_t h, ssize_t src_x = 0, ssize_t src_y = 0,
      shared_ptr<Region> mask = nullptr) {
    if (mask.get()) {
      this->img.mask_blit(src, dest_x, dest_y, w, h, src_x, src_y, mask->render());
    } else {
      this->img.blit(src, dest_x, dest_y, w, h, src_x, src_y);
    }
  }

  // External resource data accessors
  virtual vector<ColorTableEntry> read_clut(int16_t id) {
    return this->rf->decode_clut(id);
  }

  // QuickDraw state accessors
  Rect bounds;
  virtual const Rect& get_bounds() const {
    return this->bounds;
  }
  virtual void set_bounds(Rect z) {
    this->bounds = z;
  }

  Region clip_region;
  virtual const Region& get_clip_region() const {
    return this->clip_region;
  }
  virtual void set_clip_region(Region&& z) {
    this->clip_region = move(z);
  }

  Color foreground_color;
  virtual Color get_foreground_color() const {
    return this->foreground_color;
  }
  virtual void set_foreground_color(Color z) {
    this->foreground_color = z;
  }

  Color background_color;
  virtual Color get_background_color() const {
    return this->background_color;
  }
  virtual void set_background_color(Color z) {
    this->background_color = z;
  }

  Color highlight_color;
  virtual Color get_highlight_color() const {
    return this->highlight_color;
  }
  virtual void set_highlight_color(Color z) {
    this->highlight_color = z;
  }

  Color op_color;
  virtual Color get_op_color() const {
    return this->op_color;
  }
  virtual void set_op_color(Color z) {
    this->op_color = z;
  }

  int16_t extra_space_nonspace;
  virtual int16_t get_extra_space_nonspace() const {
    return this->extra_space_nonspace;
  }
  virtual void set_extra_space_nonspace(int16_t z) {
    this->extra_space_nonspace = z;
  }

  Fixed extra_space_space;
  virtual Fixed get_extra_space_space() const {
    return this->extra_space_space;
  }
  virtual void set_extra_space_space(Fixed z) {
    this->extra_space_space = z;
  }

  Point pen_loc;
  virtual Point get_pen_loc() const {
    return this->pen_loc;
  }
  virtual void set_pen_loc(Point z) {
    this->pen_loc = z;
  }

  int16_t pen_loc_frac;
  virtual int16_t get_pen_loc_frac() const {
    return this->pen_loc_frac;
  }
  virtual void set_pen_loc_frac(int16_t z) {
    this->pen_loc_frac = z;
  }

  Point pen_size;
  virtual Point get_pen_size() const {
    return this->pen_size;
  }
  virtual void set_pen_size(Point z) {
    this->pen_size = z;
  }

  int16_t pen_mode;
  virtual int16_t get_pen_mode() const {
    return this->pen_mode;
  }
  virtual void set_pen_mode(int16_t z) {
    this->pen_mode = z;
  }

  int16_t pen_visibility;
  virtual int16_t get_pen_visibility() const {
    return this->pen_visibility;
  }
  virtual void set_pen_visibility(int16_t z) {
    this->pen_visibility = z;
  }

  int16_t text_font;
  virtual int16_t get_text_font() const {
    return this->text_font;
  }
  virtual void set_text_font(int16_t z) {
    this->text_font = z;
  }

  int16_t text_mode;
  virtual int16_t get_text_mode() const {
    return this->text_mode;
  }
  virtual void set_text_mode(int16_t z) {
    this->text_mode = z;
  }

  int16_t text_size;
  virtual int16_t get_text_size() const {
    return this->text_size;
  }
  virtual void set_text_size(int16_t z) {
    this->text_size = z;
  }

  uint8_t text_style;
  virtual uint8_t get_text_style() const {
    return this->text_style;
  }
  virtual void set_text_style(uint8_t z) {
    this->text_style = z;
  }

  int16_t foreground_color_index;
  virtual int16_t get_foreground_color_index() const {
    return this->foreground_color_index;
  }
  virtual void set_foreground_color_index(int16_t z) {
    this->foreground_color_index = z;
  }

  int16_t background_color_index;
  virtual int16_t get_background_color_index() const {
    return this->background_color_index;
  }
  virtual void set_background_color_index(int16_t z) {
    this->background_color_index = z;
  }

  Image pen_pixel_pattern;
  virtual const Image& get_pen_pixel_pattern() const {
    return this->pen_pixel_pattern;
  }
  virtual void set_pen_pixel_pattern(Image&& z) {
    this->pen_pixel_pattern = move(z);
  }

  Image fill_pixel_pattern;
  virtual const Image& get_fill_pixel_pattern() const {
    return this->fill_pixel_pattern;
  }
  virtual void set_fill_pixel_pattern(Image&& z) {
    this->fill_pixel_pattern = move(z);
  }

  Image background_pixel_pattern;
  virtual const Image& get_background_pixel_pattern() const {
    return this->background_pixel_pattern;
  }
  virtual void set_background_pixel_pattern(Image&& z) {
    this->background_pixel_pattern = move(z);
  }

  Pattern pen_mono_pattern;
  virtual Pattern get_pen_mono_pattern() const {
    return this->pen_mono_pattern;
  }
  virtual void set_pen_mono_pattern(Pattern z) {
    this->pen_mono_pattern = z;
  }

  Pattern fill_mono_pattern;
  virtual Pattern get_fill_mono_pattern() const {
    return this->fill_mono_pattern;
  }
  virtual void set_fill_mono_pattern(Pattern z) {
    this->fill_mono_pattern = z;
  }

  Pattern background_mono_pattern;
  virtual Pattern get_background_mono_pattern() const {
    return this->background_mono_pattern;
  }
  virtual void set_background_mono_pattern(Pattern z) {
    this->background_mono_pattern = z;
  }

protected:
  ResourceFile* rf;
  Image img;
};

ResourceFile::DecodedPictResource ResourceFile::decode_PICT(int16_t id, uint32_t type) {
  return this->decode_PICT(this->get_resource(type, id));
}

ResourceFile::DecodedPictResource ResourceFile::decode_PICT(shared_ptr<const Resource> res) {
  try {
    return this->decode_PICT_internal(res);
  } catch (const exception& e) {
    fprintf(stderr, "warning: PICT rendering failed (%s); attempting rendering using picttoppm\n", e.what());
    return {this->decode_PICT_external(res), "", ""};
  }
}

ResourceFile::DecodedPictResource ResourceFile::decode_PICT_internal(int16_t id, uint32_t type) {
  return this->decode_PICT_internal(this->get_resource(type, id));
}

ResourceFile::DecodedPictResource ResourceFile::decode_PICT_internal(shared_ptr<const Resource> res) {
  if (res->data.size() < sizeof(PictHeader)) {
    throw runtime_error("PICT too small for header");
  }

  try {
    const auto& header = *reinterpret_cast<const PictHeader*>(res->data.data());
    QuickDrawResourceDasmPort port(this, header.bounds.width(), header.bounds.height());
    QuickDrawEngine eng;
    eng.set_port(&port);
    eng.render_pict(res->data.data(), res->data.size());
    return {move(port.image()), "", ""};

  } catch (const pict_contains_undecodable_quicktime& e) {
    return {Image(0, 0), e.extension, e.data};
  }
}

Image ResourceFile::decode_PICT_external(int16_t id, uint32_t type) {
  return this->decode_PICT_external(this->get_resource(type, id));
}

Image ResourceFile::decode_PICT_external(shared_ptr<const Resource> res) {
  return ResourceFile::decode_PICT_external(res->data.data(), res->data.size());
}

Image ResourceFile::decode_PICT_external(const void* data, size_t size) {
  Subprocess proc({"picttoppm", "-noheader"}, -1, -1, fileno(stderr));
  string ppm_data = proc.communicate(data, size, 10000000);
  int proc_ret = proc.wait(true);
  if (proc_ret != 0) {
    throw runtime_error(string_printf("picttoppm failed (%d)", proc_ret));
  }
  if (ppm_data.empty()) {
    throw runtime_error("picttoppm succeeded but produced no output");
  }
  auto f = fmemopen_unique(ppm_data.data(), ppm_data.size());
  return Image(f.get());
}

vector<Color> ResourceFile::decode_pltt(int16_t id, uint32_t type) {
  return this->decode_pltt(this->get_resource(type, id));
}

vector<Color> ResourceFile::decode_pltt(shared_ptr<const Resource> res) {
  return ResourceFile::decode_pltt(res->data.data(), res->data.size());
}

vector<Color> ResourceFile::decode_pltt(const void* vdata, size_t size) {
  if (size < sizeof(PaletteEntry)) {
    throw runtime_error("pltt too small for header");
  }

  // pltt resources have a 16-byte header, which is coincidentally also the size
  // of each entry. I'm lazy so we'll just load it all at once and use the first
  // "entry" instead of manually making a header struct
  const PaletteEntry* pltt = reinterpret_cast<const PaletteEntry*>(vdata);

  // The first header word is the entry count; the rest of the header seemingly
  // doesn't matter at all
  uint16_t count = pltt->c.r;
  if (size < sizeof(PaletteEntry) * (count + 1)) {
    throw runtime_error("pltt too small for all entries");
  }

  vector<Color> ret;
  for (size_t x = 1; x - 1 < count; x++) {
    ret.emplace_back(pltt[x].c);
  }
  return ret;
}

vector<ColorTableEntry> ResourceFile::decode_clut(int16_t id, uint32_t type) {
  return this->decode_clut(this->get_resource(type, id));
}

vector<ColorTableEntry> ResourceFile::decode_clut(shared_ptr<const Resource> res) {
  return ResourceFile::decode_clut(res->data.data(), res->data.size());
}

vector<ColorTableEntry> ResourceFile::decode_clut(const void* data, size_t size) {
  if (size < sizeof(ColorTableEntry)) {
    throw runtime_error("color table too small for header");
  }

  // clut resources have an 8-byte header, which is coincidentally also the size
  // of each entry. I'm lazy so we'll just load it all at once and use the first
  // "entry" instead of manually making a header struct
  const ColorTableEntry* clut = reinterpret_cast<const ColorTableEntry*>(data);

  // The last header word is the entry count; the rest of the header seemingly
  // doesn't matter at all
  uint16_t count = clut->c.b;
  if (count == 0xFFFF) {
    return vector<ColorTableEntry>();
  }
  if (size < sizeof(ColorTableEntry) * (count + 1)) {
    throw runtime_error("color table too small for all entries");
  }

  // Unlike for pltt resources, clut counts are inclusive - there are actually
  // (count + 1) colors
  vector<ColorTableEntry> ret;
  ret.reserve(count + 1);
  for (size_t x = 1; x - 1 <= count; x++) {
    ret.emplace_back(clut[x]);
  }
  return ret;
}

vector<ColorTableEntry> ResourceFile::decode_actb(int16_t id, uint32_t type) {
  return this->decode_clut(id, type);
}

vector<ColorTableEntry> ResourceFile::decode_actb(shared_ptr<const Resource> res) {
  return ResourceFile::decode_clut(res);
}

vector<ColorTableEntry> ResourceFile::decode_actb(const void* data, size_t size) {
  return ResourceFile::decode_clut(data, size);
}

vector<ColorTableEntry> ResourceFile::decode_cctb(int16_t id, uint32_t type) {
  return this->decode_clut(id, type);
}

vector<ColorTableEntry> ResourceFile::decode_cctb(shared_ptr<const Resource> res) {
  return ResourceFile::decode_clut(res);
}

vector<ColorTableEntry> ResourceFile::decode_cctb(const void* data, size_t size) {
  return ResourceFile::decode_clut(data, size);
}

vector<ColorTableEntry> ResourceFile::decode_dctb(int16_t id, uint32_t type) {
  return this->decode_clut(id, type);
}

vector<ColorTableEntry> ResourceFile::decode_dctb(shared_ptr<const Resource> res) {
  return ResourceFile::decode_clut(res);
}

vector<ColorTableEntry> ResourceFile::decode_dctb(const void* data, size_t size) {
  return ResourceFile::decode_clut(data, size);
}

vector<ColorTableEntry> ResourceFile::decode_fctb(int16_t id, uint32_t type) {
  return this->decode_clut(id, type);
}

vector<ColorTableEntry> ResourceFile::decode_fctb(shared_ptr<const Resource> res) {
  return ResourceFile::decode_clut(res);
}

vector<ColorTableEntry> ResourceFile::decode_fctb(const void* data, size_t size) {
  return ResourceFile::decode_clut(data, size);
}

vector<ColorTableEntry> ResourceFile::decode_wctb(int16_t id, uint32_t type) {
  return this->decode_clut(id, type);
}

vector<ColorTableEntry> ResourceFile::decode_wctb(shared_ptr<const Resource> res) {
  return ResourceFile::decode_clut(res);
}

vector<ColorTableEntry> ResourceFile::decode_wctb(const void* data, size_t size) {
  return ResourceFile::decode_clut(data, size);
}

vector<ColorTableEntry> ResourceFile::decode_CTBL(int16_t id, uint32_t type) {
  return this->decode_CTBL(this->get_resource(type, id));
}

vector<ColorTableEntry> ResourceFile::decode_CTBL(shared_ptr<const Resource> res) {
  return ResourceFile::decode_CTBL(res->data.data(), res->data.size());
}

vector<ColorTableEntry> ResourceFile::decode_CTBL(const void* data, size_t size) {
  StringReader r(data, size);
  uint16_t num_colors = r.get_u16b();
  vector<ColorTableEntry> ret;
  for (size_t z = 0; z < num_colors; z++) {
    auto& e = ret.emplace_back();
    e.c.r = r.get_u8() * 0x101;
    e.c.g = r.get_u8() * 0x101;
    e.c.b = r.get_u8() * 0x101;
    e.color_num = r.get_u8();
  }
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// Sound decoding

struct WaveFileHeader {
  be_uint32_t riff_magic;   // 0x52494646 ('RIFF')
  uint32_t file_size;    // size of file - 8
  be_uint32_t wave_magic;   // 0x57415645

  be_uint32_t fmt_magic;    // 0x666d7420 ('fmt ')
  uint32_t fmt_size;     // 16
  uint16_t format;       // 1 = PCM
  uint16_t num_channels;
  uint32_t sample_rate;
  uint32_t byte_rate;    // num_channels * sample_rate * bits_per_sample / 8
  uint16_t block_align;  // num_channels * bits_per_sample / 8
  uint16_t bits_per_sample;

  union {
    struct {
      be_uint32_t smpl_magic;
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

      uint32_t loop_cue_point_id; // Can be zero? We'll only have at most one loop in this context
      uint32_t loop_type; // 0 = normal, 1 = ping-pong, 2 = reverse
      uint32_t loop_start; // Start and end are byte offsets into the wave data, not sample indexes
      uint32_t loop_end;
      uint32_t loop_fraction; // Fraction of a sample to loop (0)
      uint32_t loop_play_count; // 0 = loop forever

      be_uint32_t data_magic;   // 0x64617461 ('data')
      uint32_t data_size;    // num_samples * num_channels * bits_per_sample / 8
      uint8_t data[0];
    } with_loop;

    struct {
      be_uint32_t data_magic;   // 0x64617461 ('data')
      uint32_t data_size;    // num_samples * num_channels * bits_per_sample / 8
      uint8_t data[0];
    } without_loop;
  };

  WaveFileHeader(uint32_t num_samples, uint16_t num_channels, uint32_t sample_rate,
      uint16_t bits_per_sample, uint32_t loop_start = 0, uint32_t loop_end = 0,
      uint8_t base_note = 0x3C) {

    this->riff_magic = 0x52494646; // 'RIFF"'
    // this->file_size is set below (it depends on whether there's a loop)
    this->wave_magic = 0x57415645; // 'WAVE'
    this->fmt_magic = 0x666D7420; // 'fmt '
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

      this->with_loop.smpl_magic = 0x736D706C; // 'smpl'
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

      // Note: loop_start and loop_end are given to this function as sample
      // offsets, but in the wav file, they should be byte offsets
      this->with_loop.loop_start = loop_start * (bits_per_sample >> 3);
      this->with_loop.loop_end = loop_end * (bits_per_sample >> 3);

      this->with_loop.loop_fraction = 0;
      this->with_loop.loop_play_count = 0; // 0 = loop forever

      this->with_loop.data_magic = 0x64617461; // 'data'
      this->with_loop.data_size = num_samples * num_channels * bits_per_sample / 8;

    } else {
      // with_loop is longer than without_loop so we correct for the size
      // disparity manually here
      const uint32_t header_size = sizeof(*this) - sizeof(this->with_loop) +
          sizeof(this->without_loop);
      this->file_size = num_samples * num_channels * bits_per_sample / 8 +
          header_size - 8;

      this->without_loop.data_magic = 0x64617461; // 'data'
      this->without_loop.data_size = num_samples * num_channels * bits_per_sample / 8;
    }
  }

  bool has_loop() const {
    return (this->with_loop.smpl_magic == 0x736D706C);
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

struct SoundResourceHeaderFormat2 {
  be_uint16_t format_code; // = 2
  be_uint16_t reference_count;
  be_uint16_t num_commands;
};

struct SoundResourceHeaderFormat1 {
  be_uint16_t format_code; // = 1
  be_uint16_t data_format_count; // we only support 0 or 1 here
};

// 3 is not a standard header format; it's used by Beatnik for MPEG-encoded
// samples. This format is only parsed when the ResourceFile's index format is
// HIRF.
struct SoundResourceHeaderFormat3 {
  be_uint16_t format_code;
  be_uint32_t type; // 'none', 'ima4', 'imaW', 'mac3', 'mac6', 'ulaw', 'alaw', or 'mpga'-'mpgn'
  be_uint32_t sample_rate; // actually a Fixed16
  be_uint32_t decoded_bytes;
  be_uint32_t frame_count; // If MPEG, the number of blocks
  be_uint32_t encoded_bytes;
  be_uint32_t unused;
  be_uint32_t start_frame; // If MPEG, the number of uint16_ts to skip
  be_uint32_t channel_loop_start_frame[6];
  be_uint32_t channel_loop_end_frame[6];
  be_uint32_t name_resource_type;
  be_uint32_t name_resource_id;
  uint8_t base_note;
  uint8_t channel_count; // up to 6
  uint8_t bits_per_sample; // 8 or 16
  uint8_t is_embedded;
  uint8_t is_encrypted;
  uint8_t is_little_endian;
  uint8_t reserved1[2];
  be_uint32_t reserved2[8];
} __attribute__((packed));

struct SoundResourceHeaderMohawkChunkHeader {
  be_uint32_t type;
  be_uint32_t size; // not including this header
} __attribute__((packed));

struct SoundResourceHeaderMohawkFormat {
  // Used when header.type = 'Data' or 'Cue#'
  be_uint16_t sample_rate;
  be_uint32_t num_samples; // could be sample bytes, could also be uint16_t
  uint8_t sample_bits;
  uint8_t num_channels;
  be_uint32_t unknown[3];
  // Sample data immediately follows
} __attribute__((packed));

struct SoundResourceDataFormatHeader {
  be_uint16_t data_format_id; // we only support 5 here (sampled sound)
  be_uint32_t flags; // 0x40 = stereo
} __attribute__((packed));

struct SoundResourceCommand {
  be_uint16_t command;
  be_uint16_t param1;
  be_uint32_t param2;
} __attribute__((packed));

struct SoundResourceSampleBuffer {
  be_uint32_t data_offset; // from end of this struct
  be_uint32_t data_bytes;
  be_uint32_t sample_rate;
  be_uint32_t loop_start;
  be_uint32_t loop_end;
  uint8_t encoding;
  uint8_t base_note;
  uint8_t data[0];
};

struct SoundResourceCompressedBuffer {
  be_uint32_t num_frames;
  uint8_t sample_rate[10]; // what kind of encoding is this? lolz
  be_uint32_t marker_chunk;
  be_uint32_t format;
  be_uint32_t reserved1;
  be_uint32_t state_vars; // high word appears to be sample size
  be_uint32_t left_over_block_ptr;
  be_uint16_t compression_id;
  be_uint16_t packet_size;
  be_uint16_t synth_id;
  be_uint16_t bits_per_sample;
  uint8_t data[0];
};

static ResourceFile::DecodedSoundResource decode_snd_data(
    const void* vdata, size_t size, bool metadata_only, bool hirf_semantics,
    bool decompress_ysnd = false) {
  if (size < 4) {
    throw runtime_error("snd doesn\'t even contain a format code");
  }

  StringReader r(vdata, size);

  // These format codes ('Cue#' or 'Data') are the type codes of the first chunk
  // for a Mohawk-specific chunk-based format - we don't want to consume the
  // format code from r because it's part of the first chunk header
  uint32_t format_code32 = r.get_u32b(false);
  if (format_code32 == 0x43756523 || format_code32 == 0x44617461) {

    while (r.remaining() >= sizeof(SoundResourceHeaderMohawkChunkHeader)) {
      const auto& header = r.get<SoundResourceHeaderMohawkChunkHeader>();
      if (header.type == 0x43756523) {
        r.skip(header.size);
      } else if (header.type == 0x44617461) {
        const auto& data_header = r.get<SoundResourceHeaderMohawkFormat>();
        // TODO: we should obviously support different values for these fields
        // but I currently don't have any example files with different values so
        // I can't tell how the samples are interleaved, or even if num_samples
        // is actually num_bytes, num_frames, or something else.
        if (data_header.num_channels != 1) {
          throw runtime_error("MHK snd does not have exactly 1 channel");
        }
        if (data_header.sample_bits != 8) {
          throw runtime_error("MHK snd does not have 8-bit samples");
        }
        WaveFileHeader wav(
            data_header.num_samples,
            data_header.num_channels,
            data_header.sample_rate,
            data_header.sample_bits);
        string ret_data;
        ret_data.append(reinterpret_cast<const char*>(&wav), wav.size());
        ret_data.append(r.readx(data_header.num_samples));
        return {
          .is_mp3 = false,
          .sample_rate = data_header.sample_rate,
          .base_note = 0x3C,
          .data = move(ret_data),
        };
      }
    }
    throw runtime_error("MHK snd does not contain a Data section");
  }

  uint16_t format_code16 = r.get_u16b(false);

  // Parse the resource header
  int num_channels = 1;
  size_t num_commands;
  if (format_code16 == 0x0001) {
    const auto& header = r.get<SoundResourceHeaderFormat1>();

    // If data format count is 0, assume mono
    if (header.data_format_count == 0) {
      num_channels = 1;

    } else if (header.data_format_count == 1) {
      const auto& data_format = r.get<SoundResourceDataFormatHeader>();
      if (data_format.data_format_id != 5) {
        throw runtime_error("snd data format is not sampled");
      }
      num_channels = (data_format.flags & 0x40) ? 2 : 1;

    } else {
      throw runtime_error("snd has multiple data formats");
    }

    num_commands = r.get_u16b();

  } else if (format_code16 == 0x0002) {
    const auto& header = r.get<SoundResourceHeaderFormat2>();
    num_commands = header.num_commands;

  } else if ((format_code16 == 0x0003) && hirf_semantics) {
    const auto& header = r.get<SoundResourceHeaderFormat3>();

    if ((header.type & 0xFFFFFF00) != 0x6D706700) {
      throw runtime_error("format 3 snd is not mp3");
    }

    // TODO: for little-endian samples, do we just byteswap the entire stream?
    if (header.is_little_endian) {
      throw runtime_error("format 3 snd is little-endian");
    }
    // TODO: for encrypted samples, do we just call decrypt_soundmusicsys_data
    // on the sample buffer?
    if (header.is_encrypted) {
      throw runtime_error("format 3 snd is encrypted");
    }
    if (decompress_ysnd) {
      throw runtime_error("cannot decompress Ysnd-encoded format 3 snd");
    }

    return {
        true,
        header.sample_rate >> 16,
        static_cast<uint8_t>(header.base_note ? header.base_note : 0x3C),
        metadata_only ? "" : r.read(r.remaining())};

  } else {
    throw runtime_error("snd is not format 1 or 2");
  }

  if (num_commands == 0) {
    throw runtime_error("snd contains no commands");
  }

  size_t sample_buffer_offset = 0;
  for (size_t x = 0; x < num_commands; x++) {
    const auto& command = r.get<SoundResourceCommand>();

    static const unordered_map<uint16_t, const char*> command_names({
        {0x0003, "quiet"},
        {0x0004, "flush"},
        {0x0005, "reinit"},
        {0x000A, "wait"},
        {0x000B, "pause"},
        {0x000C, "resume"},
        {0x000D, "callback"},
        {0x000E, "sync"},
        {0x0018, "available"},
        {0x0019, "version"},
        {0x001A, "get total cpu load"},
        {0x001B, "get channel cpu load"},
        {0x0028, "note"},
        {0x0029, "rest"},
        {0x002A, "set pitch"},
        {0x002B, "set amplitude"},
        {0x002C, "set timbre"},
        {0x002D, "get amplitude"},
        {0x002E, "set volume"},
        {0x002F, "get volume"},
        {0x003C, "load wave table"},
        {0x0052, "set sampled pitch"},
        {0x0053, "get sampled pitch"},
    });

    switch (command.command) {
      case 0x0000: // null (do nothing)
        break;
      case 0x8050: // load sample voice
      case 0x8051: // play sampled sound
        if (sample_buffer_offset) {
          throw runtime_error("snd contains multiple buffer commands");
        }
        sample_buffer_offset = command.param2;
        break;
      default:
        const char* name = nullptr;
        try {
          name = command_names.at(command.command);
        } catch (const out_of_range&) { }
        if (name) {
          throw runtime_error(string_printf(
              "command not implemented: %04hX (%s) %04hX %08X",
              command.command.load(), name, command.param1.load(), command.param2.load()));
        } else {
          throw runtime_error(string_printf(
              "command not implemented: %04hX %04hX %08X",
              command.command.load(), command.param1.load(), command.param2.load()));
        }
    }
  }

  // Some snds have an incorrect sample buffer offset, but they still play! I
  // guess Sound Manager ignores the offset in the command? (We do so here)
  const auto& sample_buffer = r.get<SoundResourceSampleBuffer>();
  uint16_t sample_rate = sample_buffer.sample_rate >> 16;
  uint8_t base_note = sample_buffer.base_note ? sample_buffer.base_note : 0x3C;

  if (metadata_only) {
    return {false, sample_rate, base_note, ""};
  }

  if (decompress_ysnd) {
    if (sample_buffer.encoding != 0x00) {
      throw runtime_error("Ysnd contains doubly-compressed buffer");
    }

    StringWriter w;
    uint8_t p = 0x80;
    while (w.str().size() < sample_buffer.data_bytes) {
      uint8_t x = r.get_u8();
      uint8_t d1 = (x >> 4) - 8;
      p += (d1 * 2);
      d1 += 8;
      if ((d1 != 0) && (d1 != 0x0F)) {
        w.put_u8(p);
        if (w.str().size() >= sample_buffer.data_bytes) {
          break;
        }
      }
      x = (x & 0x0F) - 8;
      p += (x * 2);
      x += 8;
      if ((x != 0) && (x != 0x0F)) {
        w.put_u8(p);
      }
    }

    WaveFileHeader wav(w.str().size(), num_channels, sample_rate, 8,
        sample_buffer.loop_start, sample_buffer.loop_end, base_note);

    string ret;
    ret.append(reinterpret_cast<const char*>(&wav), wav.size());
    ret.append(w.str());
    return {false, sample_rate, base_note, move(ret)};
  }

  // Uncompressed data can be copied verbatim
  if (sample_buffer.encoding == 0x00) {
    if (sample_buffer.data_bytes == 0) {
      throw runtime_error("snd contains no samples");
    }

    // Some snds have erroneously large values in the data_bytes field, so only
    // trust it if it fits within the resource
    size_t num_samples = min<size_t>(sample_buffer.data_bytes, r.remaining());

    WaveFileHeader wav(num_samples, num_channels, sample_rate, 8,
        sample_buffer.loop_start, sample_buffer.loop_end, base_note);

    string ret;
    ret.append(reinterpret_cast<const char*>(&wav), wav.size());
    ret.append(r.readx(num_samples));
    return {false, sample_rate, base_note, move(ret)};

  // Compressed data will need to be decompressed first
  } else if ((sample_buffer.encoding == 0xFE) || (sample_buffer.encoding == 0xFF)) {
    const auto& compressed_buffer = r.get<SoundResourceCompressedBuffer>();

    // Hack: it appears Beatnik archives set the stereo flag even when the snd
    // is mono, so we ignore it in that case. (TODO: Does this also apply to
    // MACE3/6? I'm assuming it does here, but haven't verified this. Also, what
    // about the uncompressed case above?)
    if (hirf_semantics && (num_channels == 2)) {
      num_channels = 1;
    }

    switch (compressed_buffer.compression_id) {
      case 0xFFFE:
        throw runtime_error("snd uses variable-ratio compression");

      case 3:
      case 4: {
        bool is_mace3 = compressed_buffer.compression_id == 3;
        auto decoded_samples = decode_mace(compressed_buffer.data,
            compressed_buffer.num_frames * (is_mace3 ? 2 : 1) * num_channels,
            num_channels == 2, is_mace3);
        uint32_t loop_factor = is_mace3 ? 3 : 6;

        WaveFileHeader wav(decoded_samples.size() / num_channels, num_channels,
            sample_rate, 16, sample_buffer.loop_start * loop_factor,
            sample_buffer.loop_end * loop_factor, base_note);
        if (wav.get_data_size() != 2 * decoded_samples.size()) {
          throw runtime_error("computed data size does not match decoded data size");
        }

        string ret;
        ret.append(reinterpret_cast<const char*>(&wav), wav.size());
        ret.append(reinterpret_cast<const char*>(decoded_samples.data()), wav.get_data_size());
        return {false, sample_rate, base_note, move(ret)};
      }

      case 0xFFFF:
        // 'twos' and 'sowt' are equivalent to no compression and fall through
        // to the uncompressed case below. For all others, we'll have to
        // decompress somehow
        if ((compressed_buffer.format != 0x74776F73) && (compressed_buffer.format != 0x736F7774)) {
          vector<le_int16_t> decoded_samples;

          size_t num_frames = compressed_buffer.num_frames;
          uint32_t loop_factor;
          if (compressed_buffer.format == 0x696D6134) { // ima4
            decoded_samples = decode_ima4(
                compressed_buffer.data,
                num_frames * 34 * num_channels,
                (num_channels == 2));
            loop_factor = 4; // TODO: verify this. I don't actually have any examples right now

          } else if ((compressed_buffer.format == 0x4D414333) || (compressed_buffer.format == 0x4D414336)) { // MAC3, MAC6
            bool is_mace3 = compressed_buffer.format == 0x4D414333;
            decoded_samples = decode_mace(
                compressed_buffer.data,
                num_frames * (is_mace3 ? 2 : 1) * num_channels,
                num_channels == 2, is_mace3);
            loop_factor = is_mace3 ? 3 : 6;

          } else if (compressed_buffer.format == 0x756C6177) { // ulaw
            decoded_samples = decode_ulaw(compressed_buffer.data, num_frames);
            loop_factor = 2;

          } else if (compressed_buffer.format == 0x616C6177) { // alaw (guess)
            decoded_samples = decode_alaw(compressed_buffer.data, num_frames);
            loop_factor = 2;

          } else {
            throw runtime_error(string_printf("snd uses unknown compression (%08" PRIX32 ")",
                compressed_buffer.format.load()));
          }

          WaveFileHeader wav(decoded_samples.size() / num_channels, num_channels,
              sample_rate, 16, sample_buffer.loop_start * loop_factor,
              sample_buffer.loop_end * loop_factor, base_note);
          if (wav.get_data_size() != 2 * decoded_samples.size()) {
            throw runtime_error(string_printf(
              "computed data size (%" PRIu32 ") does not match decoded data size (%zu)",
              wav.get_data_size(), 2 * decoded_samples.size()));
          }

          string ret;
          ret.append(reinterpret_cast<const char*>(&wav), wav.size());
          ret.append(reinterpret_cast<const char*>(decoded_samples.data()), wav.get_data_size());
          return {false, sample_rate, base_note, move(ret)};
        }

        [[fallthrough]];
      case 0: { // No compression
        uint32_t num_samples = compressed_buffer.num_frames;
        uint16_t bits_per_sample = compressed_buffer.bits_per_sample;
        if (bits_per_sample == 0) {
          bits_per_sample = compressed_buffer.state_vars >> 16;
        }

        // Hack: if the sound is stereo and the computed data size is exactly
        // twice the available data size, treat it as mono
        if ((num_channels == 2) &&
            (num_samples * num_channels * (bits_per_sample / 8)) == 2 * r.remaining()) {
          num_channels = 1;
        }

        WaveFileHeader wav(num_samples, num_channels, sample_rate, bits_per_sample,
            sample_buffer.loop_start, sample_buffer.loop_end, base_note);
        if (wav.get_data_size() == 0) {
          throw runtime_error(string_printf(
            "computed data size is zero (%" PRIu32 " samples, %d channels, %" PRIu16 " kHz, %" PRIu16 " bits per sample)",
            num_samples, num_channels, sample_rate, bits_per_sample));
        }
        if (wav.get_data_size() > r.remaining()) {
          throw runtime_error(string_printf("computed data size exceeds actual data (%" PRIu32 " computed, %zu available)",
              wav.get_data_size(), r.remaining()));
        }

        string ret;
        ret.append(reinterpret_cast<const char*>(&wav), wav.size());
        ret.append(r.readx(wav.get_data_size()));

        // Byteswap the samples if it's 16-bit and not 'swot'
        if ((wav.bits_per_sample == 0x10) && (compressed_buffer.format != 0x736F7774)) {
          uint16_t* samples = reinterpret_cast<uint16_t*>(ret.data() + wav.size());
          for (uint32_t x = 0; x < wav.get_data_size() / 2; x++) {
            samples[x] = bswap16(samples[x]);
          }
        }
        return {false, sample_rate, base_note, move(ret)};
      }

      default:
        throw runtime_error("snd is compressed using unknown algorithm");
    }

  } else {
    throw runtime_error(string_printf("unknown encoding for snd data: %02hhX",
        sample_buffer.encoding));
  }
}



ResourceFile::DecodedSoundResource ResourceFile::decode_snd(
    int16_t id, uint32_t type, bool metadata_only) {
  return this->decode_snd(this->get_resource(type, id), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_snd(
    shared_ptr<const Resource> res, bool metadata_only) {
  return ResourceFile::decode_snd(res->data.data(), res->data.size(), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_snd(
    const void* data, size_t size, bool metadata_only) {
  return decode_snd_data(data, size, metadata_only,
      this->index_format() == IndexFormat::HIRF);
}



static string lzss_decompress(const void* vsrc, size_t size) {
  string ret;
  const char* src = reinterpret_cast<const char*>(vsrc);
  size_t offset = 0;

  for (;;) {
    if (offset >= size) {
      return ret;
    }
    uint8_t control_bits = src[offset++];

    for (uint8_t control_mask = 0x01; control_mask; control_mask <<= 1) {
      if (control_bits & control_mask) {
        if (offset >= size) {
          return ret;
        }
        ret += src[offset++];

      } else {
        if (offset >= size - 1) {
          return ret;
        }
        uint16_t params = (static_cast<uint16_t>(src[offset]) << 8) | static_cast<uint8_t>(src[offset + 1]);
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

static string decompress_soundmusicsys_data(const void* data, size_t size) {
  if (size < 4) {
    throw runtime_error("resource too small for compression header");
  }

  // It looks like encrypted resources sometimes have 0xFF in the type field
  // (high byte of decompressed_size), even if delta-encoding wouldn't make
  // sense, like for MIDI resources. We assume we'll never decode a 4GB
  // resource, so mask out the high byte if it's 0xFF.
  // TODO: Do we need to support the other delta-encoding types here? Should we
  // factor the delta decoding logic into here?
  uint32_t decompressed_size = *reinterpret_cast<const be_uint32_t*>(data);
  if (decompressed_size & 0xFF000000) {
    decompressed_size &= 0x00FFFFFF;
  }
  string decompressed = lzss_decompress(
      reinterpret_cast<const uint8_t*>(data) + 4, size - 4);
  if (decompressed.size() != decompressed_size) {
    throw runtime_error(string_printf(
        "decompression produced incorrect amount of data (0x%zX bytes expected, 0x%" PRIX32 " bytes received)",
        decompressed.size(), decompressed_size));
  }
  return decompressed;
}

static string decrypt_soundmusicsys_data(const void* vsrc, size_t size) {
  const uint8_t* src = reinterpret_cast<const uint8_t*>(vsrc);
  string ret;
  uint32_t v = 56549L;
  for (size_t x = 0; x < size; x++) {
    uint8_t ch = src[x];
    ret.push_back(ch ^ (v >> 8L));
    v = (static_cast<uint32_t>(ch) + v) * 52845L + 22719L;
  }
  return ret;
}

static string decrypt_soundmusicsys_cstr(StringReader& r) {
  uint32_t v = 56549L;
  string ret;
  for (;;) {
    uint8_t ch = r.get_u8();
    uint8_t ch_out = ch ^ (v >> 8L);
    if (ch_out == 0) {
      return ret;
    }
    ret.push_back(ch_out);
    v = (static_cast<uint32_t>(ch) + v) * 52845L + 22719L;
  }
}

string ResourceFile::decode_SMSD(int16_t id, uint32_t type) {
  return this->decode_SMSD(this->get_resource(type, id));
}

string ResourceFile::decode_SMSD(shared_ptr<const Resource> res) {
  return ResourceFile::decode_SMSD(res->data.data(), res->data.size());
}

string ResourceFile::decode_SMSD(const void* data, size_t size) {
  if (size < 8) {
    throw runtime_error("resource too small for header");
  }

  // There's just an 8-byte header, then the rest of it is 22050Hz 8-bit mono.
  // TODO: Is there anything useful in this 8-byte header? All of the examples
  // I've seen have various values in there but are all 22050Hz 8-bit mono, so
  // maybe it doesn't matter?
  WaveFileHeader wav(size - 8, 1, 22050, 8);
  string ret;
  ret.append(reinterpret_cast<const char*>(&wav), wav.size());
  ret.append(reinterpret_cast<const char*>(data) + 8, size - 8);
  return ret;
}

string ResourceFile::decode_SOUN(int16_t id, uint32_t type) {
  return this->decode_SOUN(this->get_resource(type, id));
}

string ResourceFile::decode_SOUN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_SOUN(res->data.data(), res->data.size());
}

string ResourceFile::decode_SOUN(const void* data, size_t size) {
  if (size < 0x16) {
    throw runtime_error("resource too small for header");
  }

  // SOUN resources are like this:
  //   uint8_t unknown[6]; // usually 07D001020001 (last byte is sometimes 00)
  //   int8_t delta_table[0x10];
  //   uint8_t data[0];

  StringReader r(data, size);
  r.skip(6); // unknown[6]

  // Technically this should be signed, but we'll just let it overflow
  uint8_t delta_table[0x10];
  r.readx(delta_table, 0x10);

  // We already know how much data is going to be produced, so we can write the
  // WAV header before decoding the data
  StringWriter w;
  WaveFileHeader wav(r.remaining() * 2, 1, 11025, 8);
  w.write(&wav, wav.size());

  // The data is a sequence of nybbles, which are indexes into the delta table.
  // The initial sample is 0x80 (center), and each nybble specifies which delta
  // to add to the initial sample.
  uint8_t sample = 0x80;
  while (!r.eof()) {
    uint8_t d = r.get_u8();
    sample += delta_table[(d >> 4) & 0x0F];
    w.put_u8(sample);
    sample += delta_table[d & 0x0F];
    w.put_u8(sample);
  }

  return w.str();
}

ResourceFile::DecodedSoundResource ResourceFile::decode_csnd(
    int16_t id, uint32_t type, bool metadata_only) {
  return this->decode_csnd(this->get_resource(type, id),  metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_csnd(
    shared_ptr<const Resource> res, bool metadata_only) {
  return ResourceFile::decode_csnd(res->data.data(), res->data.size(), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_csnd(
    const void* data, size_t size, bool metadata_only) {
  if (size < 4) {
    throw runtime_error("csnd too small for header");
  }
  uint32_t type_and_size = *reinterpret_cast<const be_uint32_t*>(data);

  uint8_t sample_type = type_and_size >> 24;
  if ((sample_type > 3) && (sample_type != 0xFF)) {
    throw runtime_error("invalid csnd sample type");
  }

  // Check that decompressed_size makes sense for the type (for types 1 and 2,
  // it must be a multiple of 2; for type 3, it must be a multiple of 4)
  size_t decompressed_size = type_and_size & 0x00FFFFFF;
  if (sample_type != 0xFF) {
    uint8_t sample_bytes = (sample_type == 2) ? sample_type : (sample_type + 1);
    if (decompressed_size % sample_bytes) {
      throw runtime_error("decompressed size is not a multiple of frame size");
    }
  }

  string decompressed = lzss_decompress(reinterpret_cast<const char*>(data) + 4, size - 4);
  if (decompressed.size() < decompressed_size) {
    throw runtime_error("decompression did not produce enough data");
  }
  decompressed.resize(decompressed_size);

  // If sample_type isn't 0xFF, then the buffer is delta-encoded
  if (sample_type == 0) { // mono8
    uint8_t* data = reinterpret_cast<uint8_t*>(decompressed.data());
    uint8_t* data_end = data + decompressed.size();
    for (uint8_t sample = *data++; data != data_end; data++) {
      *data = (sample += *data);
    }

  } else if (sample_type == 2) { // mono16
    be_uint16_t* data = reinterpret_cast<be_uint16_t*>(decompressed.data());
    be_uint16_t* data_end = data + decompressed.size();
    for (uint16_t sample = *(data++); data != data_end; data++) {
      *data = (sample += *data);
    }

  } else if (sample_type == 1) { // stereo8
    uint8_t* data = reinterpret_cast<uint8_t*>(decompressed.data());
    uint8_t* data_end = data + decompressed.size();
    data += 2;
    for (uint8_t sample0 = data[-2], sample1 = data[-1]; data != data_end; data += 2) {
      data[0] = (sample0 += data[0]);
      data[1] = (sample1 += data[1]);
    }

  } else if (sample_type == 3) { // stereo16
    be_uint16_t* data = reinterpret_cast<be_uint16_t*>(decompressed.data());
    be_uint16_t* data_end = data + decompressed.size();
    data += 2;
    for (uint16_t sample0 = data[-2], sample1 = data[-1]; data != data_end; data += 2) {
      data[0] = (sample0 += data[0]);
      data[1] = (sample1 += data[1]);
    }
  }

  // The result is a snd resource, which we can then decode normally
  return decode_snd_data(decompressed.data(), decompressed.size(),
      metadata_only, this->index_format() == IndexFormat::HIRF);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_esnd(
    int16_t id, uint32_t type, bool metadata_only) {
  return this->decode_esnd(this->get_resource(type, id), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_esnd(
    shared_ptr<const Resource> res, bool metadata_only) {
  return ResourceFile::decode_esnd(res->data.data(), res->data.size(), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_esnd(
    const void* data, size_t size, bool metadata_only) {
  string decrypted = decrypt_soundmusicsys_data(data, size);
  return decode_snd_data(decrypted.data(), decrypted.size(), metadata_only,
      this->index_format() == IndexFormat::HIRF);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_ESnd(
    int16_t id, uint32_t type, bool metadata_only) {
  return this->decode_ESnd(this->get_resource(type, id), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_ESnd(
    shared_ptr<const Resource> res, bool metadata_only) {
  return ResourceFile::decode_ESnd(res->data.data(), res->data.size(), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_ESnd(
    const void* vdata, size_t size, bool metadata_only) {
  string data(reinterpret_cast<const char*>(vdata), size);
  uint8_t* ptr = reinterpret_cast<uint8_t*>(data.data());
  uint8_t* data_end = ptr + data.size();
  for (uint8_t sample = (*ptr++ ^= 0xFF); ptr != data_end; ptr++) {
    *ptr = (sample += (*ptr ^ 0xFF));
  }
  return decode_snd_data(data.data(), data.size(), metadata_only,
      (this->index_format() == IndexFormat::HIRF));
}

ResourceFile::DecodedSoundResource ResourceFile::decode_Ysnd(
    int16_t id, uint32_t type, bool metadata_only) {
  return this->decode_Ysnd(this->get_resource(type, id), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_Ysnd(
    shared_ptr<const Resource> res, bool metadata_only) {
  return ResourceFile::decode_Ysnd(res->data.data(), res->data.size(), metadata_only);
}

ResourceFile::DecodedSoundResource ResourceFile::decode_Ysnd(
    const void* vdata, size_t size, bool metadata_only) {
  return decode_snd_data(vdata, size, metadata_only,
      (this->index_format() == IndexFormat::HIRF), true);
}

string ResourceFile::decode_cmid(int16_t id, uint32_t type) {
  return this->decode_cmid(this->get_resource(type, id));
}

string ResourceFile::decode_cmid(shared_ptr<const Resource> res) {
  return ResourceFile::decode_cmid(res->data.data(), res->data.size());
}

string ResourceFile::decode_cmid(const void* data, size_t size) {
  return decompress_soundmusicsys_data(data, size);
}

string ResourceFile::decode_emid(int16_t id, uint32_t type) {
  return this->decode_emid(this->get_resource(type, id));
}

string ResourceFile::decode_emid(shared_ptr<const Resource> res) {
  return ResourceFile::decode_emid(res->data.data(), res->data.size());
}

string ResourceFile::decode_emid(const void* vdata, size_t size) {
  return decrypt_soundmusicsys_data(vdata, size);
}

string ResourceFile::decode_ecmi(int16_t id, uint32_t type) {
  return this->decode_ecmi(this->get_resource(type, id));
}

string ResourceFile::decode_ecmi(shared_ptr<const Resource> res) {
  return ResourceFile::decode_ecmi(res->data.data(), res->data.size());
}

string ResourceFile::decode_ecmi(const void* data, size_t size) {
  string decrypted = decrypt_soundmusicsys_data(data, size);
  return decompress_soundmusicsys_data(decrypted.data(), decrypted.size());
}



////////////////////////////////////////////////////////////////////////////////
// Sequenced music decoding

struct InstrumentResourceHeader {
  enum Flags1 {
    EnableInterpolate = 0x80,
    EnableAmpScale = 0x40,
    DisableSoundLoops = 0x20,
    UseSampleRate = 0x08,
    SampleAndHold = 0x04,
    ExtendedFormat = 0x02,
    DisableReverb = 0x01,
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

  be_int16_t snd_id; // or csnd or esnd
  be_uint16_t base_note; // if zero, use the snd's base_note
  uint8_t panning;
  uint8_t flags1;
  uint8_t flags2;
  int8_t smod_id;
  be_int16_t smod_params[2];
  be_uint16_t num_key_regions;
};

struct InstrumentResourceKeyRegion {
  // low/high are inclusive
  uint8_t key_low;
  uint8_t key_high;

  be_int16_t snd_id;
  be_int16_t smod_params[2];
};

ResourceFile::DecodedInstrumentResource::KeyRegion::KeyRegion(uint8_t key_low,
    uint8_t key_high, uint8_t base_note, int16_t snd_id, uint32_t snd_type) :
    key_low(key_low), key_high(key_high), base_note(base_note), snd_id(snd_id),
    snd_type(snd_type) { }

ResourceFile::DecodedInstrumentResource ResourceFile::decode_INST(int16_t id, uint32_t type) {
  return this->decode_INST(this->get_resource(type, id));
}

ResourceFile::DecodedInstrumentResource ResourceFile::decode_INST(shared_ptr<const Resource> res) {
  StringReader r(res->data);

  const auto& header = r.get<InstrumentResourceHeader>();

  DecodedInstrumentResource ret;
  ret.base_note = header.base_note;
  ret.constant_pitch = (header.flags2 & InstrumentResourceHeader::Flags2::PlayAtSampledFreq);
  // If the UseSampleRate flag is not set, then the synthesizer apparently
  // doesn't correct for sample rate differences at all. This means that if your
  // INSTs refer to snds that are 11025kHz but you're playing at 22050kHz, your
  // song will be shifted up an octave. Even worse, if you have snds with
  // different sample rates, the pitches of all notes will be messed up. (Why
  // does this even exist? Shouldn't it always be enabled? Apparently it's not
  // enabled in a lot of cases, and some songs depend on this!)
  ret.use_sample_rate = (header.flags1 & InstrumentResourceHeader::Flags1::UseSampleRate);
  if (header.num_key_regions == 0) {
    uint32_t snd_type = this->find_resource_by_id(header.snd_id, {RESOURCE_TYPE_esnd, RESOURCE_TYPE_csnd, RESOURCE_TYPE_snd});
    ret.key_regions.emplace_back(0x00, 0x7F, header.base_note, header.snd_id, snd_type);
  } else {
    for (size_t x = 0; x < header.num_key_regions; x++) {
      const auto& rgn = r.get<InstrumentResourceKeyRegion>();

      uint32_t snd_type = this->find_resource_by_id(rgn.snd_id,
          {RESOURCE_TYPE_esnd, RESOURCE_TYPE_csnd, RESOURCE_TYPE_snd});

      // If the snd has PlayAtSampledFreq, set a fake base_note of 0x3C to
      // ignore whatever the snd/csnd/esnd says.
      uint8_t base_note = (header.flags2 & InstrumentResourceHeader::Flags2::PlayAtSampledFreq) ?
          0x3C : header.base_note.load();
      ret.key_regions.emplace_back(rgn.key_low, rgn.key_high, base_note,
          rgn.snd_id, snd_type);
    }
  }

  uint16_t tremolo_count = r.get_u16b();
  while (ret.tremolo_data.size() < tremolo_count) {
    ret.tremolo_data.emplace_back(r.get_u16b());
  }
  if (r.get_u16b() != 0x8000) {
    throw runtime_error("tremolo data terminator is incorrect");
  }
  r.skip(2); // reserved

  ret.copyright = r.readx(r.get_u8());
  ret.author = r.readx(r.get_u8());
  return ret;
}



struct SMSSongResourceHeader {
  struct InstrumentOverride {
    be_uint16_t midi_channel_id;
    be_uint16_t inst_resource_id;
  };

  enum Flags1 {
    TerminateDecayNotesEarly = 0x40,
    NoteInterpolateEntireSong = 0x20,
    NoteInterpolateLeadInstrument = 0x10,
    DefaultProgramsPerTrack = 0x08, // If true, track 1 is inst 1, etc.; otherwise channel 1 is inst 1, etc. (currently unimplemented here)
    EnableMIDIProgramChange = 0x04, // Ignored; we always allow program change
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

  be_int16_t midi_id;
  // RMF docs call this field unused (and indeed, resource_dasm doesn't use it)
  uint8_t lead_inst_id;
  // Reverb types from RMF documentation (these are the names they used):
  // 0 = default/current (don't override from environment)
  // 1 = no reverb
  // 2 = closet
  // 3 = garage
  // 4 = lab
  // 5 = cavern
  // 6 = dungeon
  // 7 = small reflections
  // 8 = early reflections
  // 9 = basement
  // 10 = banquet hall
  // 11 = catacombs
  uint8_t reverb_type;
  be_uint16_t tempo_bias; // 0 = default = 16667; linear, so 8333 = half-speed
  // Note: Some older TMPLs show the following two fields as a single 16-bit
  // semitone_shift field; it looks like the filter_type field was added later
  // in development. I haven't yet seen any SONGs that have nonzero filter_type.
  // Similarly, RMF docs combine these two bytes into one field (as it was in
  // earlier SoundMusicSys versions). When exactly did RMF branch from SMS?
  uint8_t filter_type; // 0 = sms, 1 = rmf, 2 = mod (we only support 0 here)
  int8_t semitone_shift;
  // Similarly, RMF docs combine these two bytes int a single field ("Maximum
  // number of simultaneous digital audio files and digital audio streams"). We
  // ignore this difference because resource_dasm doesn't use these fields.
  uint8_t max_effects; // TMPL: "Extra channels for sound effects"
  uint8_t max_notes;
  be_uint16_t mix_level;
  uint8_t flags1;
  uint8_t note_decay; // In 1/60ths apparently
  uint8_t percussion_instrument; // Channel 10; 0 = none, 0xFF = GM percussion
  uint8_t flags2;

  be_uint16_t instrument_override_count;

  // Variable-length fields follow:
  // InstrumentOverride instrument_overrides[instrument_override_count];
  // pstring copyright;
  // pstring author;
};

struct RMFSongResourceHeader {
  // Many of these fields are the same as those in SMSSongResourceHeader; see
  // that structure for comments.
  be_int16_t midi_id;
  uint8_t reserved1;
  uint8_t reverb_type;
  be_uint16_t tempo_bias;
  uint8_t midi_format; // (RMF) 0 = private, 1 = RMF structure, 2 = RMF linear
  uint8_t encrypted;
  be_int16_t semitone_shift;
  be_uint16_t max_concurrent_streams;
  be_uint16_t max_voices;
  be_uint16_t max_signals;
  be_uint16_t volume_bias; // 0 = normal = 007F; linear, so 00FE = double volume
  uint8_t is_in_instrument_bank;
  uint8_t reserved2;
  be_uint32_t reserved3[7];
  be_uint16_t num_subresources;
};

static ResourceFile::DecodedSongResource decode_SONG_SMS(
    const void* vdata, size_t size) {
  StringReader r(vdata, size);

  const auto& header = r.get<SMSSongResourceHeader>();

  // Note: They split the pitch shift field in a later version of the library;
  // some older SONGs that have a negative value in the pitch_shift field may
  // also set filter_type to 0xFF because it was part of pitch_shift before.
  // We currently don't use filter_type at all, so we don't bother changing it.
  // if (header.filter_type == 0xFF) {
  //   header.filter_type = 0;
  // }

  ResourceFile::DecodedSongResource ret;
  ret.is_rmf = false;
  ret.midi_id = header.midi_id;
  ret.midi_format = 0xFFFF; // standard MIDI
  ret.tempo_bias = header.tempo_bias;
  ret.volume_bias = 127;
  ret.semitone_shift = header.semitone_shift;
  ret.percussion_instrument = header.percussion_instrument;
  ret.allow_program_change = (header.flags1 & SMSSongResourceHeader::Flags1::EnableMIDIProgramChange);
  for (size_t x = 0; x < header.instrument_override_count; x++) {
    const auto& override = r.get<SMSSongResourceHeader::InstrumentOverride>();
    ret.instrument_overrides.emplace(
        override.midi_channel_id, override.inst_resource_id);
  }
  if (!r.eof()) {
    ret.copyright_text = r.readx(r.get_u8());
  }
  if (!r.eof()) {
    ret.composer = r.readx(r.get_u8());
  }
  return ret;
}

static ResourceFile::DecodedSongResource decode_SONG_RMF(
    const void* vdata, size_t size) {
  StringReader r(vdata, size);

  const auto& header = r.get<RMFSongResourceHeader>();
  ResourceFile::DecodedSongResource ret;
  ret.is_rmf = true;
  ret.midi_id = header.midi_id;
  ret.midi_format = header.midi_format;
  ret.tempo_bias = header.tempo_bias;
  ret.volume_bias = header.volume_bias;
  ret.semitone_shift = header.semitone_shift;
  ret.percussion_instrument = -1;
  ret.allow_program_change = true;

  for (uint16_t x = 0; x < header.num_subresources; x++) {
    uint32_t type = r.get_u32b();
    if (type == 0x524D4150) { // RMAP (instrument override)
      uint16_t from_inst = r.get_u16b();
      uint16_t to_inst = r.get_u16b();
      ret.instrument_overrides.emplace(from_inst, to_inst);
    } else if (type == 0x56454C43) {
      ret.velocity_override_map.clear();
      ret.velocity_override_map.reserve(0x80);
      while (ret.velocity_override_map.size() < 0x80) {
        ret.velocity_override_map.emplace_back(r.get_u16b());
      }

    } else if (type == 0x5449544C) { // 'TITL'
      ret.title = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x50455246) { // 'PERF'
      ret.performer = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x434F4D50) { // 'COMP'
      ret.composer = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x434F5044) { // 'COPD'
      ret.copyright_date = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x434F504C) { // 'COPL'
      ret.copyright_text = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x4C494343) { // 'LICC'
      ret.license_contact = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x4C555345) { // 'LUSE'
      ret.license_uses = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x4C444F4D) { // 'LDOM'
      ret.license_domain = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x4C54524D) { // 'LTRM'
      ret.license_term = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x45585044) { // 'EXPD'
      ret.license_expiration = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x4E4F5445) { // 'NOTE'
      ret.note = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x494E4458) { // 'INDX'
      ret.index_number = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x47454E52) { // 'GENR'
      ret.genre = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();
    } else if (type == 0x53554247) { // 'SUBG'
      ret.subgenre = header.encrypted ? decrypt_soundmusicsys_cstr(r) : r.get_cstr();

    } else {
      throw runtime_error("unknown SONG subresource type");
    }
  }

  return ret;
}

ResourceFile::DecodedSongResource ResourceFile::decode_SONG(int16_t id, uint32_t type) {
  return this->decode_SONG(this->get_resource(type, id));
}

ResourceFile::DecodedSongResource ResourceFile::decode_SONG(shared_ptr<const Resource> res) {
  return this->decode_SONG(res->data.data(), res->data.size());
}

ResourceFile::DecodedSongResource ResourceFile::decode_SONG(const void* vdata, size_t size) {
  if (this->index_format() == IndexFormat::HIRF) {
    return decode_SONG_RMF(vdata, size);
  } else {
    return decode_SONG_SMS(vdata, size);
  }
}

struct TuneResourceHeader {
  be_uint32_t header_size; // Includes the sample description commands in the MIDI stream
  be_uint32_t magic; // 'musi'
  be_uint32_t reserved1;
  be_uint16_t reserved2;
  be_uint16_t index;
  be_uint32_t flags;
  // MIDI track data immediately follows
};

string ResourceFile::decode_Tune(int16_t id, uint32_t type) {
  return this->decode_Tune(this->get_resource(type, id));
}

string ResourceFile::decode_Tune(shared_ptr<const Resource> res) {
  return ResourceFile::decode_Tune(res->data.data(), res->data.size());
}

string ResourceFile::decode_Tune(const void* vdata, size_t size) {
  struct MIDIChunkHeader {
    be_uint32_t magic; // MThd or MTrk
    be_uint32_t size;
  };
  struct MIDIHeader {
    MIDIChunkHeader header;
    be_uint16_t format;
    be_uint16_t track_count;
    be_uint16_t division;
  };

  StringReader r(vdata, size);

  const auto& tune = r.get<TuneResourceHeader>();
  if (tune.magic != 0x6D757369) { // 'musi'
    throw runtime_error("Tune identifier is incorrect");
  }

  // Convert Tune events into MIDI events
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
    uint32_t event = r.get_u32b();
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
          uint32_t options = r.get_u32b();
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
          uint32_t options = r.get_u32b();
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

        string message_data = r.readx(message_size - 4);
        if (message_data.size() != message_size - 4) {
          throw runtime_error("metadata message exceeds track boundary");
        }

        // the second-to-last word is the message type
        uint16_t message_type = *reinterpret_cast<const be_uint16_t*>(
            message_data.data() + message_data.size() - 4) & 0x3FFF;

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
            uint32_t instrument = *reinterpret_cast<const be_uint32_t*>(message_data.data() + 0x50);
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
            uint32_t instrument = *reinterpret_cast<const be_uint32_t*>(message_data.data() + 0x7C);
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
                event, message_type, r.where() + sizeof(TuneResourceHeader)));
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

  // generate the file and return it
  string ret;
  ret.append(reinterpret_cast<const char*>(&midi_header), sizeof(MIDIHeader));
  ret.append(reinterpret_cast<const char*>(&track_header), sizeof(MIDIChunkHeader));
  ret.append(midi_track_data);
  return ret;
}



////////////////////////////////////////////////////////////////////////////////
// String decoding

static const string mac_roman_table[0x100] = {
  // 00
  // Note: we intentionally incorrectly decode \r as \n here to convert CR line
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
};

static const string mac_roman_table_rtf[0x100] = {
  // 00
  // Note: we intentionally incorrectly decode \r as \n here to convert CR line
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
};

string decode_mac_roman(const char* data, size_t size) {
  string ret;
  while (size--) {
    ret += mac_roman_table[static_cast<uint8_t>(*(data++))];
  }
  return ret;
}

string decode_mac_roman(const string& data) {
  return decode_mac_roman(data.data(), data.size());
}

ResourceFile::DecodedStringSequence ResourceFile::decode_STRN(int16_t id, uint32_t type) {
  return this->decode_STRN(this->get_resource(type, id));
}

ResourceFile::DecodedStringSequence ResourceFile::decode_STRN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_STRN(res->data.data(), res->data.size());
}

ResourceFile::DecodedStringSequence ResourceFile::decode_STRN(const void* vdata, size_t size) {
  StringReader r(vdata, size);
  size_t count = r.get_u16b();

  vector<string> ret;
  while (ret.size() < count) {
    ret.emplace_back(decode_mac_roman(r.readx(r.get_u8())));
  }

  return {ret, r.read(r.remaining())};
}

ResourceFile::DecodedString ResourceFile::decode_STR(int16_t id, uint32_t type) {
  return this->decode_STR(this->get_resource(type, id));
}

ResourceFile::DecodedString ResourceFile::decode_STR(shared_ptr<const Resource> res) {
  return ResourceFile::decode_STR(res->data.data(), res->data.size());
}

ResourceFile::DecodedString ResourceFile::decode_STR(const void* vdata, size_t size) {
  if (size == 0) {
    return {"", ""};
  }

  StringReader r(vdata, size);
  string s = decode_mac_roman(r.readx(r.get_u8()));
  return {move(s), r.read(r.remaining())};
}

string ResourceFile::decode_card(int16_t id, uint32_t type) {
  return this->decode_card(this->get_resource(type, id));
}

string ResourceFile::decode_card(shared_ptr<const Resource> res) {
  return ResourceFile::decode_card(res->data.data(), res->data.size());
}

string ResourceFile::decode_card(const void* vdata, size_t size) {
  if (size == 0) {
    return "";
  }

  const char* data = reinterpret_cast<const char*>(vdata);
  uint8_t len = static_cast<uint8_t>(data[0]);
  if (len > size - 1) {
    throw runtime_error("length is too large for data");
  }

  return decode_mac_roman(data + 1, len);
}

string ResourceFile::decode_TEXT(int16_t id, uint32_t type) {
  return this->decode_TEXT(this->get_resource(type, id));
}

string ResourceFile::decode_TEXT(shared_ptr<const Resource> res) {
  return ResourceFile::decode_TEXT(res->data.data(), res->data.size());
}

string ResourceFile::decode_TEXT(const void* data, size_t size) {
  return decode_mac_roman(reinterpret_cast<const char*>(data), size);
}

const char* name_for_font_id(uint16_t font_id) {
  static const unordered_map<uint16_t, const char*> standard_font_ids({
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
    {10, "Seattle"},
    {11, "Cairo"},
    {12, "Los Angeles"},
    {13, "Zapf Dingbats"},
    {14, "Bookman"},
    {15, "N Helvetica Narrow"},
    {16, "Palatino"},
    {18, "Zapf Chancery"},
    {20, "Times"}, // Mac Almanac lists "Times Roman" here - same font?
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
  try {
    return standard_font_ids.at(font_id);
  } catch (const out_of_range&) {
    return nullptr;
  }
}

struct StyleResourceCommand {
  be_uint32_t offset;
  // These two fields seem to scale with size; they might be line/char spacing
  be_uint16_t unknown1;
  be_uint16_t unknown2;
  be_uint16_t font_id;
  be_uint16_t style_flags;
  be_uint16_t size;
  Color color;
};

string ResourceFile::decode_styl(int16_t id, uint32_t type) {
  return this->decode_styl(this->get_resource(type, id));
}

string ResourceFile::decode_styl(shared_ptr<const Resource> res) {
  // Get the text now, so we'll fail early if there's no resource
  string text;
  try {
    text = this->get_resource(RESOURCE_TYPE_TEXT, res->id)->data;
  } catch (const out_of_range&) {
    throw runtime_error("style has no corresponding TEXT");
  }

  if (text.empty()) {
    throw runtime_error("corresponding TEXT resource is empty");
  }

  StringReader r(res->data);
  size_t num_commands = r.get_u16b();
  size_t commands_start_offset = r.where(); // This should always be 2

  string ret = "{\\rtf1\\ansi\n{\\fonttbl";

  // Collect all the fonts and write the font table
  map<uint16_t, uint16_t> font_table;
  for (size_t x = 0; x < num_commands; x++) {
    const auto& cmd = r.get<StyleResourceCommand>();

    size_t font_table_entry = font_table.size();
    if (font_table.emplace(cmd.font_id, font_table_entry).second) {
      const char* font_name = name_for_font_id(cmd.font_id);
      if (font_name == nullptr) {
        // TODO: This is a bad assumption
        font_name = "Helvetica";
      }
      // TODO: We shouldn't necessarily say every font is a swiss font
      ret += string_printf("\\f%zu\\fswiss %s;", font_table_entry, font_name);
    }
  }
  ret += "}\n{\\colortbl";

  // Collect all the colors and write the color table
  map<uint64_t, uint16_t> color_table;
  r.go(commands_start_offset);
  for (size_t x = 0; x < num_commands; x++) {
    const auto& cmd = r.get<StyleResourceCommand>();

    size_t color_table_entry = color_table.size();
    if (color_table.emplace(cmd.color.to_u64(), color_table_entry).second) {
      ret += string_printf("\\red%d\\green%d\\blue%d;",
          cmd.color.r >> 8, cmd.color.g >> 8, cmd.color.b >> 8);
    }
  }
  ret += "}\n";

  // Write the stylized blocks
  r.go(commands_start_offset);
  for (size_t x = 0; x < num_commands; x++) {
    const auto& cmd = r.get<StyleResourceCommand>();

    uint32_t offset = cmd.offset;
    uint32_t end_offset = (x + 1 == num_commands)
        ? text.size()
        : r.get<StyleResourceCommand>(false).offset.load();
    if (offset >= text.size()) {
      throw runtime_error("offset is past end of TEXT resource data");
    }
    if (end_offset <= offset) {
      throw runtime_error("block size is zero or negative");
    }
    string text_block = text.substr(offset, end_offset - offset);

    // TODO: We can produce smaller files by omitting commands for parts of the
    // format that haven't changed
    size_t font_id = font_table.at(cmd.font_id);
    size_t color_id = color_table.at(cmd.color.to_u64());
    ssize_t expansion = 0;
    if (cmd.style_flags & TextStyleFlag::CONDENSED) {
      expansion = -cmd.size / 2;
    } else if (cmd.style_flags & TextStyleFlag::EXTENDED) {
      expansion = cmd.size / 2;
    }
    ret += string_printf("\\f%zu\\%s\\%s\\%s\\%s\\fs%d \\cf%zu \\expan%zd ",
        font_id,
        (cmd.style_flags & TextStyleFlag::BOLD) ? "b" : "b0",
        (cmd.style_flags & TextStyleFlag::ITALIC) ? "i" : "i0",
        (cmd.style_flags & TextStyleFlag::OUTLINE) ? "outl" : "outl0",
        (cmd.style_flags & TextStyleFlag::SHADOW) ? "shad" : "shad0",
        cmd.size * 2,
        color_id,
        expansion);
    if (cmd.style_flags & TextStyleFlag::UNDERLINE) {
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
// Font decoding

ResourceFile::DecodedFontResource ResourceFile::decode_FONT(int16_t id, uint32_t type) {
  return this->decode_FONT(this->get_resource(type, id));
}

struct FontResourceHeader {
  enum TypeFlags {
    ContainsImageHeightTable = 0x0001,
    ContainsGlyphWidthTable = 0x0002,
    BitDepthMask = 0x000C,
    Monochrome = 0x0000,
    BitDepth2 = 0x0004,
    BitDepth4 = 0x0008,
    BitDepth8 = 0x000C,
    HasColorTable = 0x0080,
    IsDynamic = 0x0010,
    HasNonBlackColors = 0x0020,
    FixedWidth = 0x2000,
    CannotExpand = 0x4000,
  };
  be_uint16_t type_flags;
  be_uint16_t first_char;
  be_uint16_t last_char;
  be_uint16_t max_width;
  be_int16_t max_kerning;
  be_int16_t descent; // if positive, this is the high word of the width offset table offset
  be_uint16_t rect_width;
  be_uint16_t rect_height; // also bitmap height
  be_uint16_t width_offset_table_offset;
  be_int16_t max_ascent;
  be_int16_t max_descent;
  be_int16_t leading;
  be_uint16_t bitmap_row_width;
  // Variable-length fields follow:
  // - bitmap image table (each aligned to 16-bit boundary)
  // - bitmap location table
  // - width offset table
  // - glyph-width table
  // - image height table
};

ResourceFile::DecodedFontResource ResourceFile::decode_FONT(
    shared_ptr<const Resource> res) {
  StringReader r(res->data.data(), res->data.size());
  const auto& header = r.get<FontResourceHeader>();

  DecodedFontResource ret;
  uint16_t depth_flags = header.type_flags & FontResourceHeader::TypeFlags::BitDepthMask;
  if (depth_flags == FontResourceHeader::TypeFlags::Monochrome) {
    ret.source_bit_depth = 1;
  } else if (depth_flags == FontResourceHeader::TypeFlags::BitDepth2) {
    ret.source_bit_depth = 2;
  } else if (depth_flags == FontResourceHeader::TypeFlags::BitDepth4) {
    ret.source_bit_depth = 4;
  } else { // depth_flags == FontResourceHeader::TypeFlags::BitDepth8
    ret.source_bit_depth = 8;
  }
  ret.is_dynamic = !!(header.type_flags & FontResourceHeader::TypeFlags::IsDynamic);
  ret.has_non_black_colors = !!(header.type_flags & FontResourceHeader::TypeFlags::HasNonBlackColors);
  ret.fixed_width = !!(header.type_flags & FontResourceHeader::TypeFlags::FixedWidth);
  ret.first_char = header.first_char;
  ret.last_char = header.last_char;
  ret.max_width = header.max_width;
  ret.max_kerning = header.max_kerning;
  ret.rect_width = header.rect_width;
  ret.rect_height = header.rect_height;
  ret.max_ascent = header.max_ascent;
  ret.max_descent = header.max_descent;
  ret.leading = header.leading;

  if (header.type_flags & FontResourceHeader::TypeFlags::HasColorTable) {
    ret.color_table = this->decode_fctb(res->id);
  }

  Image glyphs_bitmap;
  string bitmap_data = r.readx(header.bitmap_row_width * header.rect_height * 2);
  if (ret.source_bit_depth == 1) {
    glyphs_bitmap = decode_monochrome_image(
        bitmap_data.data(), bitmap_data.size(), header.bitmap_row_width * 16,
        header.rect_height);
  } else if (ret.source_bit_depth == 2) {
    throw runtime_error("2-bit font bitmaps are not implemented");
  } else if (ret.source_bit_depth == 4) {
    throw runtime_error("4-bit font bitmaps are not implemented");
  } else if (ret.source_bit_depth == 8) {
    throw runtime_error("8-bit font bitmaps are not implemented");
  } else {
    throw logic_error("unknown font bit depth");
  }

  // +2 here because last_char is inclusive, and there's the missing glyph at
  // the end as well
  size_t num_glyphs = (header.last_char + 2) - header.first_char;
  ret.glyphs.resize(num_glyphs);

  uint16_t glyph_start_x = r.get_u16b();
  for (uint32_t ch = header.first_char; ch < header.first_char + num_glyphs; ch++) {
    // TODO: clean this up a little to not use a prev variable
    uint16_t next_glyph_start_x = r.get_u16b();
    auto& glyph = ret.glyphs.at(ch - header.first_char);
    glyph.ch = ch;
    glyph.bitmap_offset = glyph_start_x;
    glyph.bitmap_width = next_glyph_start_x - glyph_start_x;
    glyph_start_x = next_glyph_start_x;
  }
  if (ret.glyphs.empty()) {
    throw runtime_error("no glyphs in font");
  }

  for (uint32_t ch = header.first_char; ch < header.first_char + num_glyphs; ch++) {
    auto& glyph = ret.glyphs.at(ch - header.first_char);
    glyph.offset = r.get_s8();
    glyph.width = r.get_u8();
    if (glyph.offset == -1 && glyph.width == 0xFF) {
      continue;
    }

    // TODO: handle negative offsets here
    if (glyph.offset < 0) {
      throw runtime_error("glyphs with negative offsets are not implemented");
    }

    if (glyph.width > 0) {
      glyph.img = Image(glyph.width + glyph.offset, header.rect_height);
      glyph.img.clear(0xE0E0E0FF);
    }
    if (glyph.bitmap_width > 0) {
      glyph.img.blit(glyphs_bitmap, glyph.offset, 0, glyph.bitmap_width, header.rect_height, glyph.bitmap_offset, 0);
    }
  }

  ret.missing_glyph = move(ret.glyphs.back());
  ret.glyphs.pop_back();

  return ret;
}

ResourceFile::DecodedFontResource ResourceFile::decode_NFNT(int16_t id, uint32_t type) {
  return this->decode_FONT(this->get_resource(type, id));
}

ResourceFile::DecodedFontResource ResourceFile::decode_NFNT(shared_ptr<const Resource> res) {
  return this->decode_FONT(res);
}

vector<ResourceFile::DecodedFontInfo> ResourceFile::decode_finf(int16_t id, uint32_t type) {
  return this->decode_finf(this->get_resource(type, id));
}

vector<ResourceFile::DecodedFontInfo> ResourceFile::decode_finf(shared_ptr<const Resource> res) {
  return ResourceFile::decode_finf(res->data.data(), res->data.size());
}

vector<ResourceFile::DecodedFontInfo> ResourceFile::decode_finf(const void* data, size_t size) {
  if (size == 0) {
    return {};
  }
  if (size < 2) {
    throw runtime_error("finf resource too small for count");
  }

  const uint16_t* data16 = reinterpret_cast<const uint16_t*>(data);
  size_t count = bswap16(data16[0]);
  if (size < (2 + count * 6)) {
    throw runtime_error("finf resource too small for all entries");
  }

  vector<DecodedFontInfo> ret;
  for (size_t x = 0; x < count; x++) {
    auto& finf = ret.emplace_back();
    finf.font_id = bswap16(data16[1 + x * 3]);
    finf.style_flags = bswap16(data16[2 + x * 3]);
    finf.size = bswap16(data16[3 + x * 3]);
  }
  return ret;
}

void ResourceFile::DecodedROMOverride::byteswap() {
  this->type = bswap32(this->type);
  this->id = bswap16(this->id);
}

ResourceFile::DecodedROMOverridesResource ResourceFile::decode_ROvN(int16_t id, uint32_t type) {
  return this->decode_ROvN(this->get_resource(type, id));
}

ResourceFile::DecodedROMOverridesResource ResourceFile::decode_ROvN(shared_ptr<const Resource> res) {
  return ResourceFile::decode_ROvN(res->data.data(), res->data.size());
}

ResourceFile::DecodedROMOverridesResource ResourceFile::decode_ROvN(const void* data, size_t size) {
  if (size == 0) {
    return {};
  }
  if (size < 4) {
    throw runtime_error("ROv# resource too small for header");
  }

  uint16_t count = bswap16(reinterpret_cast<const uint16_t*>(data)[1]);
  if (size < (4 + count * sizeof(DecodedROMOverride))) {
    throw runtime_error(string_printf(
        "ROv# resource too small for all entries (0x%zX/0x%zX bytes)",
        size, (4 + count * sizeof(DecodedROMOverride))));
  }
  const DecodedROMOverride* entries = reinterpret_cast<const DecodedROMOverride*>(
      reinterpret_cast<const uint16_t*>(data) + 2);

  DecodedROMOverridesResource ret;
  ret.rom_version = bswap16(reinterpret_cast<const uint16_t*>(data)[0]);
  for (size_t x = 0; x < count; x++) {
    ret.overrides.emplace_back(entries[x]).byteswap();
  }
  return ret;
}

#pragma pack(pop)
