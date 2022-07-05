#include <inttypes.h>
#include <string.h>

#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

#include "Emulators/X86Emulator.hh"
#include "Emulators/M68KEmulator.hh"
#include "Emulators/PPC32Emulator.hh"
#include "ExecutableFormats/PEFile.hh"
#include "ExecutableFormats/DOLFile.hh"

using namespace std;



struct SegmentDefinition {
  uint32_t addr;
  uint32_t size;
  string data; // may be shorter than size; the rest will be zeroed
  string filename;
  bool assemble;

  SegmentDefinition() : addr(0), size(0), assemble(false) { }
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



template <typename EmuT>
void print_audit_results_t(EmuT&) {
  throw logic_error("audit not supported for this architecture");
}

template <>
void print_audit_results_t<X86Emulator>(X86Emulator& emu) {
  const auto& audit_results = emu.get_audit_results();
  const char* same_token = "\033[0m";
  const char* different_token = "\033[33;1m";
  for (const auto& res_coll : audit_results) {
    if (res_coll.empty()) {
      continue;
    }

    fprintf(stderr, "\n====== AUDIT GROUP\n");
    for (const auto& res : res_coll) {
      string overrides_str = res.overrides.str();
      string flags_before = res.regs_before.flags_str();
      string flags_after = res.regs_after.flags_str();
      fprintf(stderr, "%08" PRIX64 " @ %08" PRIX32 " %s  overrides:%s\n",
          res.cycle_num, res.regs_before.eip, res.disassembly.c_str(), overrides_str.c_str());
      fprintf(stderr, "BEFORE: %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 "(%s) @ %08" PRIX32 "\n",
          res.regs_before.r_eax(),
          res.regs_before.r_ecx(),
          res.regs_before.r_edx(),
          res.regs_before.r_ebx(),
          res.regs_before.r_esp(),
          res.regs_before.r_ebp(),
          res.regs_before.r_esi(),
          res.regs_before.r_edi(),
          res.regs_before.read_eflags(),
          flags_before.c_str(), res.regs_before.eip);
      fprintf(stderr, "AFTER:  %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 "(%s)%s @ %08" PRIX32 "\n",
          (res.regs_before.r_eax() != res.regs_after.r_eax()) ? different_token : same_token,
          res.regs_after.r_eax(),
          (res.regs_before.r_ecx() != res.regs_after.r_ecx()) ? different_token : same_token,
          res.regs_after.r_ecx(),
          (res.regs_before.r_edx() != res.regs_after.r_edx()) ? different_token : same_token,
          res.regs_after.r_edx(),
          (res.regs_before.r_ebx() != res.regs_after.r_ebx()) ? different_token : same_token,
          res.regs_after.r_ebx(),
          (res.regs_before.r_esp() != res.regs_after.r_esp()) ? different_token : same_token,
          res.regs_after.r_esp(),
          (res.regs_before.r_ebp() != res.regs_after.r_ebp()) ? different_token : same_token,
          res.regs_after.r_ebp(),
          (res.regs_before.r_esi() != res.regs_after.r_esi()) ? different_token : same_token,
          res.regs_after.r_esi(),
          (res.regs_before.r_edi() != res.regs_after.r_edi()) ? different_token : same_token,
          res.regs_after.r_edi(),
          (res.regs_before.read_eflags() != res.regs_after.read_eflags()) ? different_token : same_token,
          res.regs_after.read_eflags(),
          flags_after.c_str(),
          same_token, res.regs_after.eip);
      // TODO: We currently don't collect these anywhere, but we should!
      for (const auto& acc : res.mem_accesses) {
        string value_str;
        // if (acc.size == 8) {
        //   value_str = string_printf("%02" PRIX32, acc.value);
        // } else if (acc.size == 16) {
        //   value_str = string_printf("%04" PRIX32, acc.value);
        // } else if (acc.size == 32) {
        //   value_str = string_printf("%08" PRIX32, acc.value);
        // } else {
        //   value_str = string_printf("%08" PRIX32 " (!)", acc.value);
        // }
        fprintf(stderr, "MEMORY: [%08" PRIX32 "] %s (%d bytes)\n",
            acc.addr, acc.is_write ? "<=" : "=>", acc.size / 8);
      }
    }
  }
}



void print_usage() {
  fprintf(stderr, "\
Usage: m68kexec <options>\n\
\n\
For this program to be useful, --pc and at least one --mem should be given, or\n\
--state should be given, or one of the --load-* options should be given.\n\
\n\
The emulated CPUs implement common user-mode opcodes, but do not yet implement\n\
some rarer opcodes. No supervisor-mode or privileged opcodes are supported.\n\
\n\
All numbers are specified in hexadecimal.\n\
\n\
Options:\n\
  --m68k\n\
      Emulate a Motorola 68000 CPU (default).\n\
  --ppc32\n\
      Emulate a 32-bit PowerPC CPU.\n\
  --x86\n\
      Emulate an Intel x86 CPU.\n\
  --mem=ADDR:SIZE\n\
      Create a memory region at the given address with the given size,\n\
      containing zeroes.\n\
  --mem=ADDR+FILENAME\n\
      Create a memory region at the given address initialized with data from\n\
      the given file.\n\
  --mem=ADDR:SIZE+FILENAME\n\
      Like the above, but truncate the file contents in memory or append zeroes\n\
      to make the memory region the given size.\n\
  --mem=ADDR/DATA\n\
      Create a memory region with the given data. The data is specified in\n\
      immediate format (hex characters, quoted strings, etc.).\n\
  --mem=ADDR:SIZE/DATA\n\
      Like the above, but truncate or extend the region to the given size.\n\
  --mem=ADDR@FILENAME\n\
      Create a memory region with the given assembly code. This option\n\
      assembles the file referenced by FILENAME and puts the result in the\n\
      created memory region. If the code contains a label named \"start\",\n\
      execution will begin at that label unless overridden by --pc.\n\
  --patch=ADDR/DATA\n\
      Before starting emulation, write the given data to the given address.\n\
  --load-pe=FILENAME\n\
      Load the given PE (.exe) file before starting emulation. Emulation will\n\
      start at the file\'s entrypoint by default, but this can be overridden\n\
      with the --pc option. Implies --x86, but this can also be overridden.\n\
  --load-dol=FILENAME\n\
      Load the given DOL executable before starting emulation. Emulation will\n\
      start at the file\'s entrypoint by default, but this can be overridden\n\
      with the --pc option. Implies --ppc32, but this can also be overridden.\n\
  --push=VALUE\n\
      Push the given 32-bit value on the stack immediately before starting\n\
      execution. If this option is given multiple times, the values are pushed\n\
      in the order they are specified (that is, the last one specified ends up\n\
      at the lowest address on the stack, with A7/ESP pointing to it).\n\
  --pc=ADDR\n\
      Start emulation at ADDR.\n\
  --reg=REG:VALUE\n\
      Set the given register\'s value before starting emulation. For 68000\n\
      emulation, REG may be D0-D7 or A0-A7; for x86 emulation, REG may be EAX,\n\
      ECX, etc. If A7/ESP is not explicitly set using this option, a stack\n\
      region will be created automatically and A7/ESP will point to the end of\n\
      that region.\n\
  --state=FILENAME\n\
      Load the emulation state from the given file. Note that state outside of\n\
      the CPU engine itself (for example, breakpoints and the step/trace flags)\n\
      are not saved in the state file, so they will not persist across save and\n\
      load operations. If this option is given, the above options may also be\n\
      given; their effects will occur immediately after loading the state.\n\
  --no-syscalls\n\
      By default, m68kexec implements a few very basic Macintosh system calls\n\
      in M68K mode, and some basic Windows system calls in x86 mode. This\n\
      option disables the system call handler, so emulation will stop at any\n\
      system call instead. Note that in x86 emulation, calling an unimplemented\n\
      imported function will result in an `int FF` opcode being executed.\n\
  --strict\n\
      Without this option, some data before or after each allcoated block may\n\
      be accessible to the emulated CPU since the underlying allocator\n\
      allocates entire pages at a time. This option adds an additional check\n\
      before each memory access to disallow access to the technically-\n\
      unallocated-but-otherwise-accessible space. It also slows down emulation.\n\
  --trace-data-sources\n\
      Enable data tracing. Currently this is only implemented in x86 emulation.\n\
      When enabled, the inputs and outputs of every cycle are tracked and\n\
      linked together, so you can use the source-trace command in single-step\n\
      mode to see all of the previous CPU cycles that led to the current value\n\
      in a certain register or memory location. This option increases memory\n\
      usage and slows down emulation significantly.\n\
  --trace-data-source-addrs\n\
      Include registers involved in effective address calculations in data\n\
      source traces. No effect if --trace-data-sources is not used.\n\
  --break=ADDR\n\
  --breakpoint=ADDR\n\
      Switch to single-step (shell) mode when execution reaches this address.\n\
  --break-cycles=COUNT\n\
      Switch to single-step (shell) mode after this many cycles have executed.\n\
  --trace\n\
      Start emulation in trace mode (show state after each cycle).\n\
  --step\n\
      Start emulation in single-step mode.\n\
");
}



uint32_t load_pe(shared_ptr<MemoryContext> mem, const string& filename) {
  PEFile pe(filename.c_str());
  pe.load_into(mem);

  // Allocate the syscall stubs. These are tiny bits of code that invoke the
  // syscall handler; we set the imported function addresses to point to them.
  // The stubs look like:
  //   call   do_syscall
  //   .data  "LibraryName.dll:ImportedFunctionName"
  // do_syscall:
  //   int    FF
  //   add    esp, 4
  //   ret
  const auto& header = pe.unloaded_header();
  StringWriter stubs_w;
  unordered_map<uint32_t, uint32_t> addr_addr_to_stub_offset;
  for (const auto& it : pe.labels_for_loaded_imports()) {
    uint32_t addr_addr = it.first;
    const string& name = it.second;

    addr_addr_to_stub_offset.emplace(addr_addr, stubs_w.size());

    // call    do_syscall
    stubs_w.put_u8(0xE8);
    stubs_w.put_u32l(name.size() + 1);
    // .data   name
    stubs_w.write(name.c_str(), name.size() + 1);
    // int     FF
    stubs_w.put_u16b(0xCDFF);
    stubs_w.put_u32b(0x83C404C3);
  }

  uint32_t stubs_addr = mem->allocate_within(0xF0000000, 0xFFFFFFFF, stubs_w.size());
  mem->memcpy(stubs_addr, stubs_w.str().data(), stubs_w.size());
  for (const auto& it : addr_addr_to_stub_offset) {
    mem->write_u32l(it.first, it.second + stubs_addr);
  }

  fprintf(stderr, "note: generated import stubs at %08" PRIX32 "\n", stubs_addr);

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
        fprintf(stderr, "[syscall_handler] NewPtr size=%08" PRIX32 " => %08" PRIX32 "\n",
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
        fprintf(stderr, "[syscall_handler] NewHandle size=%08" PRIX32 " => %08" PRIX32 "\n",
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
        fprintf(stderr, "[syscall_handler] GetHandleSize handle=%08" PRIX32 " => %08" PRIX32 "\n",
            regs.a[0], regs.d[0].s);
      }

    } else if ((trap_number == 0x0029) || (trap_number == 0x002A)) { // HLock/HUnlock
      // A0 = handle
      // We ignore this; blocks are never moved in our emulated system.
      if (verbose) {
        fprintf(stderr, "[syscall_handler] %s handle=%08" PRIX32 "\n",
            (trap_number == 0x0029) ? "HLock" : "HUnlock", regs.a[0]);
      }
      regs.d[0].u = 0; // Result code (success)

    } else if (trap_number == 0x002E) { // BlockMove
      // A0 = src, A1 = dst, D0 = size
      mem->memcpy(regs.a[1], regs.a[0], regs.d[0].u);
      if (verbose) {
        fprintf(stderr, "[syscall_handler] BlockMove dst=%08" PRIX32 " src=%08" PRIX32 " size=%" PRIX32 "\n",
            regs.a[1], regs.a[0], regs.d[0].u);
      }
      regs.d[0].u = 0; // Result code (success)

    } else {
      if (trap_number & 0x0800) {
        throw runtime_error(string_printf(
            "unimplemented toolbox trap (num=%hX, auto_pop=%s)\n",
            static_cast<uint16_t>(trap_number & 0x0BFF), auto_pop ? "true" : "false"));
      } else {
        throw runtime_error(string_printf(
            "unimplemented os trap (num=%hX, flags=%hhu)\n",
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
      uint32_t name_addr = emu.pop<le_uint32_t>();
      uint32_t return_addr = emu.pop<le_uint32_t>();
      string name = mem->read_cstring(name_addr);

      if (name == "kernel32.dll:LoadLibraryA") {
        // Args: [esp+00] = library_name
        uint32_t lib_name_addr = emu.pop<le_uint32_t>();
        string name = mem->read_cstring(lib_name_addr);

        // Load the library
        uint32_t entrypoint = load_pe(mem, name.c_str());
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
        throw runtime_error(string_printf("unhandled library call: %s", name.c_str()));
      }
    } else {
      throw runtime_error(string_printf("unhandled interrupt: %02hhX", int_num));
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
void set_trace_flags_t(
    EmuT&,
    bool audit,
    bool trace_data_sources,
    bool trace_data_source_addrs) {
  if (audit) {
    throw runtime_error("audit not supported for this architecture");
  }
  if (trace_data_sources || trace_data_source_addrs) {
    throw runtime_error("data tracing not supported for this architecture");
  }
}

template <>
void set_trace_flags_t<X86Emulator>(
    X86Emulator& emu,
    bool audit,
    bool trace_data_sources,
    bool trace_data_source_addrs) {
  emu.set_audit(audit);
  emu.set_trace_data_sources(trace_data_sources);
  emu.set_trace_data_source_addrs(trace_data_source_addrs);
}



template <typename EmuT>
int main_t(int argc, char** argv) {
  shared_ptr<MemoryContext> mem(new MemoryContext());
  EmuT emu(mem);
  auto& regs = emu.registers();

  shared_ptr<EmulatorDebugger<EmuT>> debugger(new EmulatorDebugger<EmuT>());
  debugger->bind(emu);

  bool audit = false;
  bool trace_data_sources = false;
  bool trace_data_source_addrs = false;
  uint32_t pc = 0;
  const char* pe_filename = nullptr;
  const char* dol_filename = nullptr;
  vector<SegmentDefinition> segment_defs;
  vector<uint32_t> values_to_push;
  unordered_map<uint32_t, string> patches;
  const char* state_filename = nullptr;
  bool enable_syscalls = true;
  for (int x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "--mem=", 6)) {
      segment_defs.emplace_back(parse_segment_definition(&argv[x][6]));
    } else if (!strncmp(argv[x], "--patch=", 8)) {
      char* resume_str = &argv[x][8];
      uint32_t addr = strtoul(resume_str, &resume_str, 16);
      if (*resume_str != '/') {
        throw invalid_argument("invalid patch definition");
      }
      string data = parse_data_string(resume_str + 1);
      patches.emplace(addr, move(data));
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
    } else if (!strncmp(argv[x], "--state=", 8)) {
      state_filename = &argv[x][8];
    } else if (!strncmp(argv[x], "--break=", 8)) {
      debugger->state.breakpoints.emplace(stoul(&argv[x][8], nullptr, 16));
    } else if (!strncmp(argv[x], "--breakpoint=", 13)) {
      debugger->state.breakpoints.emplace(stoul(&argv[x][13], nullptr, 16));
    } else if (!strncmp(argv[x], "--break-cycles=", 15)) {
      debugger->state.cycle_breakpoints.emplace(stoul(&argv[x][15], nullptr, 16));
    } else if (!strcmp(argv[x], "--m68k") || !strcmp(argv[x], "--ppc32") || !strcmp(argv[x], "--x86")) {
      // These are handled in the calling function (main)
    } else if (!strcmp(argv[x], "--no-syscalls")) {
      enable_syscalls = false;
    } else if (!strcmp(argv[x], "--strict")) {
      mem->set_strict(true);
    } else if (!strcmp(argv[x], "--audit")) {
      audit = true;
    } else if (!strcmp(argv[x], "--trace-data-sources")) {
      trace_data_sources = true;
    } else if (!strcmp(argv[x], "--trace-data-source-addrs")) {
      trace_data_source_addrs = true;
    } else if (!strcmp(argv[x], "--trace")) {
      debugger->state.mode = DebuggerMode::TRACE;
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
      std::unordered_set<string> get_include_stack; // For mutual recursion detection
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
          if (isfile(filename)) {
            return PPC32Emulator::assemble(load_file(filename), get_include).code;
          }
          filename = name + ".inc.bin";
          if (isfile(filename)) {
            return load_file(filename);
          }
        }
        throw runtime_error("data not found for include " + name);
      };

      auto assembled = PPC32Emulator::assemble(def.data, get_include);
      def.data = move(assembled.code);
      def.size = def.data.size();

      if (!pc) {
        try {
          pc = def.addr + assembled.label_offsets.at("start");
        } catch (const out_of_range&) { }
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
    fprintf(stderr, "note: automatically creating stack region at %08" PRIX32 ":%zX with stack pointer %08" PRIX32 "\n",
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
  regs.reset_access_flags();

  // Apply any patches from the command line
  for (const auto& patch : patches) {
    mem->memcpy(patch.first, patch.second.data(), patch.second.size());
  }

  if (enable_syscalls) {
    create_syscall_handler_t(emu, debugger);
  }

  // Run it
  set_trace_flags_t(emu, audit, trace_data_sources, trace_data_source_addrs);
  emu.execute();
  if (audit) {
    print_audit_results_t(emu);
  }

  return 0;
}



int main(int argc, char** argv) {
  enum class Architecture {
    M68K = 0,
    PPC32,
    X86,
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
  } else {
    throw logic_error("invalid architecture");
  }
}
