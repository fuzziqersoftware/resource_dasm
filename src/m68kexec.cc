#include <inttypes.h>
#include <string.h>

#include <filesystem>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

#include "Emulators/M68KEmulator.hh"
#include "Emulators/PPC32Emulator.hh"
#include "Emulators/SH4Emulator.hh"
#include "Emulators/X86Emulator.hh"
#include "ExecutableFormats/DOLFile.hh"
#include "ExecutableFormats/PEFile.hh"

using namespace std;
using namespace phosg;
using namespace ResourceDASM;

struct SegmentDefinition {
  uint32_t addr;
  uint32_t size;
  string data; // may be shorter than size; the rest will be zeroed
  string filename;
  bool assemble;

  SegmentDefinition() : addr(0),
                        size(0),
                        assemble(false) {}
};

SegmentDefinition parse_segment_definition(const string& def_str) {
  // Segment definition strings are like:
  // E0000000:4000 (blank space)
  // E0000000+file.bin (initialized memory)
  // E0000000:4000+file.bin (initialized memory with custom size)
  // E0000000:4000/010203... (immediately-initialized memory)
  // E0000000@file.s (code assembled from text file)

  SegmentDefinition def;
  char* resume_str = const_cast<char*>(def_str.c_str());
  def.addr = strtoul(resume_str, &resume_str, 16);
  while (*resume_str) {
    char* new_resume_str;
    if (*resume_str == ':') {
      resume_str++;
      def.size = strtoul(resume_str, &new_resume_str, 16);
    } else if (*resume_str == '+') {
      def.filename = resume_str + 1;
      def.data = load_file(def.filename);
      if (def.size == 0) {
        def.size = def.data.size();
      }
      new_resume_str = resume_str + strlen(resume_str);
    } else if (*resume_str == '/') {
      def.data = parse_data_string(resume_str + 1);
      if (def.size == 0) {
        def.size = def.data.size();
      }
      new_resume_str = resume_str + strlen(resume_str);
    } else if (*resume_str == '@') {
      def.filename = resume_str + 1;
      def.data = load_file(def.filename);
      def.assemble = true;
      new_resume_str = resume_str + strlen(resume_str);
    } else {
      throw invalid_argument("invalid field in memory segment definition");
    }
    if (new_resume_str == resume_str) {
      throw invalid_argument("invalid integer field in memory segment definition");
    }
    resume_str = new_resume_str;
  }

  return def;
}

void print_usage() {
  fwrite_fmt(stderr, "\
Usage: m68kexec <options>\n\
\n\
For this program to be useful, --pc and at least one --mem should be given, or\n\
--load-state should be given, or one of the --load-* options should be given.\n\
\n\
The emulated CPUs implement many user-mode opcodes, but do not yet implement\n\
some rarer opcodes. No supervisor-mode or privileged opcodes are supported.\n\
\n\
All numbers are specified in hexadecimal.\n\
\n\
CPU setup options:\n\
  --m68k\n\
      Emulates a Motorola 68000 CPU (default).\n\
  --ppc32\n\
      Emulates a 32-bit PowerPC CPU.\n\
  --x86\n\
      Emulates an Intel x86 CPU.\n\
  --sh4\n\
      Emulates a SuperH-4 CPU.\n\
  --behavior=BEHAVIOR\n\
      Sets behavior flags for the CPU engine. Currently this is used only for\n\
      x86 emulation; the valid BEHAVIOR values for x86 are:\n\
        specification: Implement behavior identical to what the Intel manuals\n\
          describe. This is the default behavior.\n\
        windows-arm-emu: Implement behavior like the x86 emulator included with\n\
          Windows 11 for ARM64 machines.\n\
  --time-base=TIME\n\
      Sets the time base (TSC on x86, or TBR on PowerPC) to the given value at\n\
      start time. If TIME contains commas, sets an override list instead, so\n\
      the first query to the time base will return the first value, the second\n\
      query will return the second value, etc. This option has no effect for\n\
      M68K and SH4 emulation.\n\
  --pc=ADDR\n\
      Starts emulation at ADDR.\n\
  --reg=REG:VALUE\n\
      Sets the given register\'s value before starting emulation. For 68000\n\
      emulation, REG may be D0-D7 or A0-A7; for x86 emulation, REG may be EAX,\n\
      ECX, etc.; for PowerPC emulation, REG may be r0-r31 or the common SPRs\n\
      (LR, CTR, XER, FPSCR, etc.); for SH4 emulation, REG may be SR, GBR, FPUL,\n\
      etc. or r0-r15. If the stack pointer (A7 on 68000, ESP on x86, r1 on\n\
      PowerPC, or r15 on SH4) is not explicitly set using this option, a stack\n\
      region is created automatically and A7/r1/ESP points to the end of that\n\
      region.\n\
\n\
Memory setup options:\n\
  --mem=DESCRIPTOR\n\
      Creates a memory region. DESCRIPTOR may be any of the following formats:\n\
      ADDR:SIZE\n\
        Creates a memory region at the given address with the given size\n\
        containing zeroes.\n\
      ADDR+FILENAME\n\
        Creates a memory region at the given address initialized with data from\n\
        the given file.\n\
      ADDR:SIZE+FILENAME\n\
        Like the above, but truncates the file contents in memory or appends\n\
        zeroes to make the memory region the given size.\n\
      ADDR/DATA\n\
        Creates a memory region with the given data. The data is specified in\n\
        phosg immediate format (hex characters, quoted strings, etc.).\n\
      ADDR:SIZE/DATA\n\
        Like the above, but truncates or extends the region to the given size.\n\
      ADDR@FILENAME\n\
        Creates a memory region with the given assembly code. This option\n\
        assembles the file referenced by FILENAME and puts the result in the\n\
        created memory region. If the code contains a label named \"start\",\n\
        execution begins at that label unless overridden by --pc.\n\
  --push=VALUE\n\
      Pushes the given 32-bit value on the stack immediately before starting\n\
      execution. If this option is given multiple times, the values are pushed\n\
      in the order they are specified (that is, the last one specified ends up\n\
      at the lowest address on the stack, with A7/ESP/r1/r15 pointing to it).\n\
  --patch=ADDR/DATA\n\
      Before starting emulation, writes the given data to the given address.\n\
      The address must be in a valid region created with --mem or loaded from\n\
      within a state or executable file.\n\
  --load-pe=FILENAME\n\
      Loads the given PE (.exe) file before starting emulation. Emulation\n\
      starts at the file\'s entrypoint by default, but this can be overridden\n\
      with the --pc option. Implies --x86, but this can also be overridden.\n\
  --load-dol=FILENAME\n\
      Loads the given DOL executable before starting emulation. Emulation\n\
      starts at the file\'s entrypoint by default, but this can be overridden\n\
      with the --pc option. Implies --ppc32, but this can also be overridden.\n\
  --load-state=FILENAME\n\
      Loads emulation state from the given file, saved with the savestate\n\
      command in single-step mode. Note that state outside of the CPU engine\n\
      itself (for example, breakpoints and the step/trace flags) are not saved\n\
      in the state file, so they will not persist across save and load\n\
      operations. If this option is given, other options like --mem and --push\n\
      may also be given; those options\' effects will occur immediately after\n\
      loading the state.\n\
  --symbol=ADDR=NAME\n\
      Creates a named symbol at ADDR with name NAME. This can be used to create\n\
      a TIB for Windows programs by setting the \"fs\" symbol appropriately.\n\
\n\
Environment behavior options:\n\
  --no-syscalls\n\
      By default, m68kexec implements a few very basic Macintosh system calls\n\
      in M68K mode, and some basic Windows system calls in x86 mode. This\n\
      option disables the system call handler, so emulation will stop at any\n\
      system call instead. Note that in x86 emulation, calling an unimplemented\n\
      imported function will result in an `int FF` opcode being executed.\n\
  --strict-memory\n\
      Without this option, some data before or after each allocated block may\n\
      be accessible to the emulated CPU since the underlying allocator\n\
      allocates entire pages at a time. This option adds an additional check\n\
      before each memory access to disallow access to the technically-\n\
      unallocated-but-otherwise-accessible space. It also slows down emulation.\n\
\n\
Debugger options:\n\
  --break=ADDR\n\
  --breakpoint=ADDR\n\
      Switches to single-step mode when execution reaches this address.\n\
  --break-cycles=COUNT\n\
      Switches to single-step mode after this many instructions have executed.\n\
  --trace\n\
      Starts emulation in trace mode (shows CPU state after each cycle).\n\
  --periodic-trace=N\n\
      Starts emulation in periodic trace mode (shows CPU state after every Nth\n\
      cycle).\n\
  --step\n\
      Starts emulation in single-step mode.\n\
  --max-cycles=CYCLES\n\
      Stop emulation after this many cycles.\n\
  --no-state-headers\n\
      Suppresses all CPU state headers (register names) in the trace and step\n\
      output.\n\
  --no-memory-log\n\
      Suppresses all memory access messages in the trace and step output.\n\
");
}

uint32_t load_pe(shared_ptr<MemoryContext> mem, const string& filename) {
  PEFile pe(filename.c_str());
  uint32_t base = pe.load_into(mem);

  // Set the base and exported function address symbols
  string symbol_prefix = basename(filename) + ":";
  mem->set_symbol_addr(symbol_prefix + "<base>", base);
  for (const auto& it : pe.labels_for_loaded_exports(base)) {
    mem->set_symbol_addr(symbol_prefix + it.second, it.first);
  }

  // Allocate the syscall stubs. These are tiny bits of code that invoke the
  // syscall handler; we set the imported function addresses to point to them.
  // The stubs look like:
  //   call   do_syscall
  //   .u32   thunk_ptr_addr
  //   .data  "LibraryName.dll:ImportedFunctionName"
  // do_syscall:
  //   int    FF
  const auto& header = pe.unloaded_header();
  StringWriter stubs_w;
  unordered_map<uint32_t, uint32_t> addr_addr_to_stub_offset;
  for (const auto& it : pe.labels_for_loaded_imports(base)) {
    uint32_t addr_addr = it.first;
    const string& name = it.second;

    addr_addr_to_stub_offset.emplace(addr_addr, stubs_w.size());

    // call    do_syscall
    stubs_w.put_u8(0xE8);
    stubs_w.put_u32l(name.size() + 1);
    // .u32    addr_addr
    stubs_w.put_u32l(0); // This is filled in during the second loop
    // .data   name
    stubs_w.write(name.c_str(), name.size() + 1);
    // int     FF
    stubs_w.put_u16b(0xCDFF);
  }

  uint32_t stubs_addr = mem->allocate_within(0xF0000000, 0xFFFFFFFF, stubs_w.size());
  mem->memcpy(stubs_addr, stubs_w.str().data(), stubs_w.size());
  for (const auto& it : addr_addr_to_stub_offset) {
    uint32_t stub_addr = it.second + stubs_addr;
    mem->write_u32l(it.first, stub_addr);
    mem->write_u32l(stub_addr + 5, it.first);
  }

  fwrite_fmt(stderr, "note: generated import stubs at {:08X}\n", stubs_addr);

  return header.entrypoint_rva + header.image_base;
}

uint32_t load_dol(shared_ptr<MemoryContext> mem, const string& filename) {
  DOLFile dol(filename.c_str());
  dol.load_into(mem);
  return dol.entrypoint;
}

template <typename EmuT>
void create_syscall_handler_t(EmuT&, shared_ptr<EmulatorDebugger<EmuT>>) {
  throw logic_error("unspecialized create_syscall_handler_t should never be called");
}

template <>
void create_syscall_handler_t<M68KEmulator>(
    M68KEmulator& emu, shared_ptr<EmulatorDebugger<M68KEmulator>> debugger) {
  // In M68K land, implement basic Mac syscalls
  emu.set_syscall_handler([debugger](M68KEmulator& emu, uint16_t syscall) -> void {
    auto& regs = emu.registers();
    uint16_t trap_number;
    bool auto_pop = false;
    uint8_t flags = 0;

    if (syscall & 0x0800) {
      trap_number = syscall & 0x0BFF;
      auto_pop = syscall & 0x0400;
    } else {
      trap_number = syscall & 0x00FF;
      flags = (syscall >> 9) & 3;
    }

    auto mem = emu.memory();
    bool verbose = debugger->state.mode != DebuggerMode::NONE;

    if (trap_number == 0x001E) { // NewPtr
      // D0 = size, A0 = returned ptr
      uint32_t addr = mem->allocate(regs.d[0].u);
      if (addr == 0) {
        throw runtime_error("cannot allocate memory for NewPtr");
      }
      regs.a[0] = addr; // Ptr

      if (verbose) {
        fwrite_fmt(stderr, "[syscall_handler] NewPtr size={:08X} => {:08X}\n",
            regs.d[0].u, regs.a[0]);
      }
      regs.d[0].u = 0; // Result code (success)

    } else if (trap_number == 0x0022) { // NewHandle
      // D0 = size, A0 = returned handle
      // Note that this must return a HANDLE, not a pointer... we cheat by
      // allocating the pointer in the same space as the data, immediately
      // preceding the data
      uint32_t addr = mem->allocate(regs.d[0].u + 4);
      if (addr == 0) {
        throw runtime_error("cannot allocate memory for NewHandle");
      }
      regs.a[0] = addr; // Handle
      mem->write_u32b(addr, addr + 4);

      if (verbose) {
        fwrite_fmt(stderr, "[syscall_handler] NewHandle size={:08X} => {:08X}\n",
            regs.d[0].u, regs.a[0]);
      }
      regs.d[0].u = 0; // Result code (success)

    } else if (trap_number == 0x0025) { // GetHandleSize
      // A0 = handle, D0 = returned size or error code (if <0)
      try {
        regs.d[0].u = mem->get_block_size(mem->read_u32b(regs.a[0]));
      } catch (const out_of_range&) {
        regs.d[0].s = -111; // memWZErr
      }

      if (verbose) {
        fwrite_fmt(stderr, "[syscall_handler] GetHandleSize handle={:08X} => {:08X}\n",
            regs.a[0], regs.d[0].s);
      }

    } else if ((trap_number == 0x0029) || (trap_number == 0x002A)) { // HLock/HUnlock
      // A0 = handle
      // We ignore this; blocks are never moved in our emulated system.
      if (verbose) {
        fwrite_fmt(stderr, "[syscall_handler] {} handle={:08X}\n",
            (trap_number == 0x0029) ? "HLock" : "HUnlock", regs.a[0]);
      }
      regs.d[0].u = 0; // Result code (success)

    } else if (trap_number == 0x002E) { // BlockMove
      // A0 = src, A1 = dst, D0 = size
      mem->memcpy(regs.a[1], regs.a[0], regs.d[0].u);
      if (verbose) {
        fwrite_fmt(stderr, "[syscall_handler] BlockMove dst={:08X} src={:08X} size={:X}\n",
            regs.a[1], regs.a[0], regs.d[0].u);
      }
      regs.d[0].u = 0; // Result code (success)

    } else {
      if (trap_number & 0x0800) {
        throw runtime_error(std::format(
            "unimplemented toolbox trap (num={:X}, auto_pop={})\n",
            static_cast<uint16_t>(trap_number & 0x0BFF), auto_pop ? "true" : "false"));
      } else {
        throw runtime_error(std::format(
            "unimplemented os trap (num={:X}, flags={})\n",
            static_cast<uint16_t>(trap_number & 0x00FF), flags));
      }
    }
  });
}

template <>
void create_syscall_handler_t<X86Emulator>(
    X86Emulator& emu, shared_ptr<EmulatorDebugger<X86Emulator>>) {
  // In X86 land, we use a syscall to emulate library calls. This little stub is
  // used to transform the result of LoadLibraryA so it will return the module
  // handle if the DLL entry point returned nonzero.
  //   test eax, eax
  //   je return_null
  //   pop eax
  //   ret
  // return_null:
  //   add esp, 4
  //   ret
  static const string load_library_stub_data = "\x85\xC0\x74\x02\x58\xC3\x83\xC4\x04\xC3";
  auto mem = emu.memory();
  uint32_t load_library_return_stub_addr = mem->allocate_within(
      0xF0000000, 0xFFFFFFFF, load_library_stub_data.size());
  mem->memcpy(load_library_return_stub_addr, load_library_stub_data.data(), load_library_stub_data.size());

  emu.set_syscall_handler([load_library_return_stub_addr](X86Emulator& emu, uint8_t int_num) {
    if (int_num == 0xFF) {
      auto mem = emu.memory();
      auto& regs = emu.registers();
      uint32_t descriptor_addr = emu.pop<le_uint32_t>();
      uint32_t return_addr = emu.pop<le_uint32_t>();
      uint32_t thunk_ptr_addr = mem->read_u32l(descriptor_addr);
      string name = mem->read_cstring(descriptor_addr + 4);

      // A few special library calls are implemented separately
      if (name == "kernel32.dll:LoadLibraryA") {
        // Args: [esp+00] = library_name
        uint32_t lib_name_addr = emu.pop<le_uint32_t>();
        string name = mem->read_cstring(lib_name_addr);

        // Load the library
        uint32_t entrypoint = load_pe(mem, name);
        uint32_t lib_handle = entrypoint; // TODO: We should use something better for library handles

        // Call DllMain (entrypoint), setting up the stack so it will return to
        // the stub, which will then return to the caller.
        // TODO: do we need to preseve any regs here? The calling convention is
        // the same for LoadLibraryA as for DllMain, and the stub only modifies
        // eax, so it should be ok to not worry about saving regs here?
        emu.push(return_addr);
        emu.push(lib_handle);
        emu.push(0x00000000); // lpReserved (null for dynamic loading)
        emu.push(0x00000000); // DLL_PROCESS_ATTACH
        emu.push(lib_handle); // hinstDLL
        emu.push(load_library_return_stub_addr);
        regs.eip = entrypoint;

      } else if (name == "kernel32.dll:GetCurrentThreadId") {
        regs.w_eax(0xEEEEEEEE);
        regs.eip = return_addr;

      } else {
        // The library might already be loaded (since we don't prepopulate the
        // thunk pointers when another call triggers loading), so check for that
        // first
        uint32_t function_addr = 0;
        try {
          function_addr = mem->get_symbol_addr(name);
        } catch (const out_of_range&) {
          // The library is not loaded, so load it

          size_t colon_offset = name.find(':');
          if (colon_offset == string::npos) {
            throw runtime_error("invalid library call: " + name);
          }
          string lib_name = name.substr(0, colon_offset);

          load_pe(mem, lib_name);

          try {
            function_addr = mem->get_symbol_addr(name);
          } catch (const out_of_range&) {
            throw runtime_error("imported module does not export requested symbol: " + name);
          }
        }

        // Replace the stub addr with the actual function addr so the stub won't
        // get called again
        mem->write_u32l(thunk_ptr_addr, function_addr);

        // Jump directly to the function (since we already popped the stub args
        // off the stack)
        regs.eip = function_addr;
      }
    } else {
      throw runtime_error(std::format("unhandled interrupt: {:02X}", int_num));
    }
  });
}

template <>
void create_syscall_handler_t<PPC32Emulator>(
    PPC32Emulator& emu, shared_ptr<EmulatorDebugger<PPC32Emulator>>) {
  emu.set_syscall_handler(+[](PPC32Emulator&) -> void {
    throw logic_error("PPC32 syscall handler is not implemented");
  });
}

template <typename EmuT>
int main_t(int argc, char** argv) {
  auto mem = make_shared<MemoryContext>();
  EmuT emu(mem);
  auto& regs = emu.registers();

  auto debugger = make_shared<EmulatorDebugger<EmuT>>();
  debugger->bind(emu);

  uint32_t pc = 0;
  const char* pe_filename = nullptr;
  const char* dol_filename = nullptr;
  vector<SegmentDefinition> segment_defs;
  vector<uint32_t> values_to_push;
  unordered_map<uint32_t, string> patches;
  vector<pair<uint32_t, uint32_t>> preallocations;
  const char* state_filename = nullptr;
  bool enable_syscalls = true;
  for (int x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "--mem=", 6)) {
      segment_defs.emplace_back(parse_segment_definition(&argv[x][6]));
    } else if (!strncmp(argv[x], "--arena=", 8)) {
      auto tokens = split(&argv[x][8], ':');
      if (tokens.size() != 2) {
        throw invalid_argument("invalid arena definition");
      }
      preallocations.emplace_back(stoul(tokens[0], nullptr, 16), stoul(tokens[1], nullptr, 16));
    } else if (!strncmp(argv[x], "--symbol=", 9)) {
      string arg(&argv[x][9]);
      size_t equals_pos = arg.find('=');
      if (equals_pos == string::npos) {
        throw invalid_argument("invalid symbol definition");
      }
      uint32_t addr = stoull(arg.substr(0, equals_pos), nullptr, 16);
      mem->set_symbol_addr(arg.substr(equals_pos + 1), addr);
    } else if (!strncmp(argv[x], "--patch=", 8)) {
      char* resume_str = &argv[x][8];
      uint32_t addr = strtoul(resume_str, &resume_str, 16);
      if (*resume_str != '/') {
        throw invalid_argument("invalid patch definition");
      }
      string data = parse_data_string(resume_str + 1);
      patches.emplace(addr, std::move(data));
    } else if (!strncmp(argv[x], "--load-pe=", 10)) {
      pe_filename = &argv[x][10];
    } else if (!strncmp(argv[x], "--load-dol=", 11)) {
      dol_filename = &argv[x][11];
    } else if (!strncmp(argv[x], "--push=", 7)) {
      values_to_push.emplace_back(strtoul(&argv[x][7], nullptr, 16));
    } else if (!strncmp(argv[x], "--pc=", 5)) {
      pc = stoul(&argv[x][5], nullptr, 16);
    } else if (!strncmp(argv[x], "--reg=", 6)) {
      auto tokens = split(&argv[x][6], ':');
      if (tokens.size() != 2) {
        throw invalid_argument("invalid register definition");
      }
      uint32_t value = stoul(tokens[1], nullptr, 16);
      regs.set_by_name(tokens[0], value);
    } else if (!strcmp(argv[x], "--no-state-headers")) {
      debugger->state.print_state_headers = false;
    } else if (!strcmp(argv[x], "--no-memory-log")) {
      debugger->state.print_memory_accesses = false;
    } else if (!strncmp(argv[x], "--load-state=", 13)) {
      state_filename = &argv[x][13];
    } else if (!strncmp(argv[x], "--break=", 8)) {
      debugger->state.breakpoints.emplace(stoul(&argv[x][8], nullptr, 16));
    } else if (!strncmp(argv[x], "--breakpoint=", 13)) {
      debugger->state.breakpoints.emplace(stoul(&argv[x][13], nullptr, 16));
    } else if (!strncmp(argv[x], "--break-cycles=", 15)) {
      debugger->state.cycle_breakpoints.emplace(stoul(&argv[x][15], nullptr, 16));
    } else if (!strncmp(argv[x], "--max-cycles=", 13)) {
      debugger->state.max_cycles = stoull(&argv[x][13], nullptr, 16);
    } else if (!strcmp(argv[x], "--m68k") || !strcmp(argv[x], "--ppc32") || !strcmp(argv[x], "--x86")) {
      // These are handled in the calling function (main)
    } else if (!strncmp(argv[x], "--behavior=", 11)) {
      emu.set_behavior_by_name(&argv[x][11]);
    } else if (!strncmp(argv[x], "--time-base=", 12)) {
      if (strchr(&argv[x][12], ',')) {
        vector<uint64_t> overrides;
        for (const auto& s : split(&argv[x][12], ',')) {
          overrides.emplace_back(stoull(s, nullptr, 16));
        }
        emu.set_time_base(overrides);
      } else {
        emu.set_time_base(strtoull(&argv[x][12], nullptr, 16));
      }
    } else if (!strcmp(argv[x], "--no-syscalls")) {
      enable_syscalls = false;
    } else if (!strcmp(argv[x], "--strict-memory")) {
      mem->set_strict(true);
    } else if (!strcmp(argv[x], "--trace")) {
      debugger->state.mode = DebuggerMode::TRACE;
    } else if (!strncmp(argv[x], "--periodic-trace=", 17)) {
      debugger->state.mode = DebuggerMode::PERIODIC_TRACE;
      debugger->state.trace_period = strtoull(&argv[x][17], nullptr, 16);
    } else if (!strcmp(argv[x], "--step")) {
      debugger->state.mode = DebuggerMode::STEP;
    } else {
      throw invalid_argument("unknown argument: " + string(argv[x]));
    }
  }

  if (segment_defs.empty() && !state_filename && !pe_filename && !dol_filename) {
    print_usage();
    return 1;
  }

  for (const auto& preallocation : preallocations) {
    mem->preallocate_arena(preallocation.first, preallocation.second);
  }

  if (state_filename) {
    auto f = fopen_unique(state_filename, "rb");
    emu.import_state(f.get());
  }

  // Load executable if needed
  if (pe_filename) {
    regs.pc = load_pe(mem, pe_filename);
  } else if (dol_filename) {
    regs.pc = load_dol(mem, dol_filename);
  }

  // Apply memory definitions
  for (auto& def : segment_defs) {
    if (def.assemble) {
      unordered_set<string> get_include_stack; // For mutual recursion detection
      function<string(const string&)> get_include = [&](const string& name) -> string {
        if (!get_include_stack.emplace(name).second) {
          throw runtime_error("mutual recursion between includes");
        }

        vector<string> prefixes;
        prefixes.emplace_back(dirname(def.filename));
        if (!prefixes.back().empty()) {
          prefixes.back().push_back('/');
          prefixes.emplace_back("");
        }
        for (const auto& prefix : prefixes) {
          string filename = prefix + name + ".inc.s";
          if (std::filesystem::is_regular_file(filename)) {
            return EmuT::assemble(load_file(filename), get_include).code;
          }
          filename = name + ".inc.bin";
          if (std::filesystem::is_regular_file(filename)) {
            return load_file(filename);
          }
        }
        throw runtime_error("data not found for include " + name);
      };

      auto assembled = EmuT::assemble(def.data, get_include);
      def.data = std::move(assembled.code);
      def.size = def.data.size();

      if (!pc) {
        try {
          pc = def.addr + assembled.label_offsets.at("start");
        } catch (const out_of_range&) {
        }
      }
    }
    mem->allocate_at(def.addr, def.size);
    if (def.size <= def.data.size()) {
      mem->memcpy(def.addr, def.data.data(), def.size);
    } else {
      mem->memcpy(def.addr, def.data.data(), def.data.size());
      mem->memset(def.addr + def.data.size(), 0, def.size - def.data.size());
    }
  }

  // Apply pc if needed
  if (pc) {
    regs.pc = pc;
  }

  // If the stack pointer doesn't make sense, allocate a stack region
  uint32_t sp = regs.get_sp();
  if (sp == 0) {
    static const size_t stack_size = 0x10000;
    uint32_t stack_addr = mem->allocate(stack_size);
    sp = stack_addr + stack_size;
    fwrite_fmt(stderr, "note: automatically creating stack region at {:08X}:{:X} with stack pointer {:08X}\n",
        stack_addr, stack_size, sp);
  }

  // Push the requested values to the stack
  for (uint32_t value : values_to_push) {
    sp -= 4;
    if (EmuT::is_little_endian) {
      mem->write_u32l(sp, value);
    } else {
      mem->write_u32b(sp, value);
    }
  }

  // Save the possibly-modified stack pointer back to the regs structs
  regs.set_sp(sp);

  // Apply any patches from the command line
  for (const auto& patch : patches) {
    mem->memcpy(patch.first, patch.second.data(), patch.second.size());
  }

  if (enable_syscalls) {
    create_syscall_handler_t(emu, debugger);
  }

  // Run it
  emu.execute();

  return 0;
}

int main(int argc, char** argv) {
  enum class Architecture {
    M68K = 0,
    PPC32,
    X86,
    SH4,
  };

  Architecture arch = Architecture::M68K;
  for (int x = 1; x < argc; x++) {
    if (!strcmp(argv[x], "--m68k")) {
      arch = Architecture::M68K;
    } else if (!strcmp(argv[x], "--ppc32")) {
      arch = Architecture::PPC32;
    } else if (!strcmp(argv[x], "--x86")) {
      arch = Architecture::X86;
    } else if (!strncmp(argv[x], "--load-pe=", 10)) {
      arch = Architecture::X86;
    } else if (!strncmp(argv[x], "--load-dol=", 11)) {
      arch = Architecture::PPC32;
    }
  }

  if (arch == Architecture::M68K) {
    return main_t<M68KEmulator>(argc, argv);
  } else if (arch == Architecture::PPC32) {
    return main_t<PPC32Emulator>(argc, argv);
  } else if (arch == Architecture::X86) {
    return main_t<X86Emulator>(argc, argv);
  } else if (arch == Architecture::SH4) {
    return main_t<SH4Emulator>(argc, argv);
  } else {
    throw logic_error("invalid architecture");
  }
}
