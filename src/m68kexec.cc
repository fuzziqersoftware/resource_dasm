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

using namespace std;



struct SegmentDefinition {
  uint32_t addr;
  uint32_t size;
  string data; // may be shorter than size; the rest will be zeroed

  SegmentDefinition() : addr(0), size(0) { }
};

SegmentDefinition parse_segment_definition(const string& def_str) {
  // Segment definition strings are like:
  // E0000000:4000 (blank space)
  // E0000000+file.bin (initialized memory)
  // E0000000:4000+file.bin (initialized memory with custom size)
  // E0000000:4000/010203... (immediately-initialized memory)

  SegmentDefinition def;
  char* resume_str = const_cast<char*>(def_str.c_str());
  def.addr = strtoul(resume_str, &resume_str, 16);
  while (*resume_str) {
    char* new_resume_str;
    if (*resume_str == ':') {
      resume_str++;
      def.size = strtoul(resume_str, &new_resume_str, 16);
    } else if (*resume_str == '+') {
      def.data = load_file(resume_str + 1);
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



void print_x86_audit_results(X86Emulator& emu_x86) {
  const auto& audit_results = emu_x86.get_audit_results();
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
For this program to be useful, --pc and at least one --mem option should be\n\
given, or --state should be given, or --load-pe should be given.\n\
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
  --patch=ADDR/DATA\n\
      Before starting emulation, write the given data to the given address.\n\
  --load-pe=FILENAME\n\
      Load the given PE (.exe) file before starting emulation. Emulation will\n\
      start at the file\'s entrypoint by default, but this can be overridden\n\
      with the --pc option. Implies --x86, but this can also be overridden.\n\
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



enum class DebugMode {
  NONE,
  TRACE,
  STEP,
};

struct DebugState {
  bool should_print_state_header;
  set<uint32_t> breakpoints;
  set<uint64_t> cycle_breakpoints;
  DebugMode mode;

  DebugState() : should_print_state_header(true), mode(DebugMode::NONE) { }
};

DebugState state;

template <typename EmuT>
void debug_hook_generic(EmuT& emu) {
  auto mem = emu.memory();
  auto& regs = emu.registers();

  if (state.cycle_breakpoints.erase(emu.cycles())) {
    fprintf(stderr, "reached cycle breakpoint at %08" PRIX64 "\n", emu.cycles());
    state.mode = DebugMode::STEP;
  } else if (state.breakpoints.count(regs.pc)) {
    fprintf(stderr, "reached execution breakpoint at %08" PRIX32 "\n", regs.pc);
    state.mode = DebugMode::STEP;
  }
  if (state.mode != DebugMode::NONE) {
    if ((state.mode == DebugMode::STEP) ||
        ((state.mode == DebugMode::TRACE) && ((emu.cycles() & 0x1F) == 0)) ||
        state.should_print_state_header) {
      emu.print_state_header(stderr);
      state.should_print_state_header = false;
    }
    auto accesses = emu.get_and_clear_memory_access_log();
    for (const auto& acc : accesses) {
      const char* type_name = "unknown";
      if (acc.size == 8) {
        type_name = "byte";
      } else if (acc.size == 16) {
        type_name = "word";
      } else if (acc.size == 32) {
        type_name = "dword";
      } else if (acc.size == 64) {
        type_name = "qword";
      } else if (acc.size == 128) {
        type_name = "oword";
      }
      fprintf(stderr, "  memory: [%08" PRIX32 "] %s (%s)\n",
          acc.addr, acc.is_write ? "<=" : "=>", type_name);
    }
    emu.print_state(stderr);
  }

  // If in trace or step mode, log all memory accesses (so they can be printed
  // before the current paused state, above)
  emu.set_log_memory_access(state.mode != DebugMode::NONE);

  bool should_continue = false;
  while ((state.mode == DebugMode::STEP) && !should_continue) {
    fprintf(stderr, "pc=%08" PRIX32 "> ", regs.pc);
    fflush(stderr);
    string input_line(0x400, '\0');
    if (!fgets(input_line.data(), input_line.size(), stdin)) {
      fprintf(stderr, "stdin was closed; stopping emulation\n");
      throw typename EmuT::terminate_emulation();
    }
    strip_trailing_zeroes(input_line);
    strip_trailing_whitespace(input_line);

    try {
      auto input_tokens = split(input_line, ' ', 1);
      const string& cmd = input_tokens.at(0);
      const string& args = input_tokens.size() == 2 ? input_tokens.at(1) : "";
      if (cmd.empty()) {
        fprintf(stderr, "no command; try \'h\'\n");

      } else if ((cmd == "h") || (cmd == "help")) {
        fprintf(stderr, "\
Commands:\n\
  r ADDR SIZE [FILENAME]\n\
  read ADDR SIZE [FILENAME]\n\
    Read memory. If FILENAME is given, save it to the file; otherwise, display\n\
    it in the terminal.\n\
  d ADDR SIZE [FILENAME]\n\
  disas ADDR SIZE [FILENAME]\n\
    Disassemble memory. If FILENAME is given, save it to the file; otherwise,\n\
    display it in the terminal.\n\
  w ADDR DATA\n\
  write ADDR DATA\n\
    Write memory. Data is given as hex characters.\n\
  cp DSTADDR SRCADDR SIZE\n\
  copy DSTADDR SRCADDR SIZE\n\
    Copy SIZE bytes from SRCADDR to DESTADDR.\n\
  a [ADDR] SIZE\n\
  alloc [ADDR] SIZE\n\
    Allocate memory. If ADDR is given, allocate it at a specific address.\n\
  b ADDR\n\
  break ADDR\n\
    Set an execution breakpoint at ADDR. When the emulator's PC register\n\
    reaches this address, the emulator switches to single-step mode.\n\
  u ADDR\n\
  unbreak ADDR\n\
    Delete the breakpoint at ADDR.\n\
  j ADDR\n\
  jump ADDR\n\
    Jump to ADDR. This only changes PC; emulation is not resumed.\n\
  sr REG VALUE\n\
  setreg REG VALUE\n\
    Set the value of a register.\n\
  ss FILENAME\n\
  savestate FILENAME\n\
    Save memory and emulation state to a file.\n\
  ls FILENAME\n\
  loadstate FILENAME\n\
    Load memory and emulation state from a file.\n\
  st WHAT [MAXDEPTH]\n\
  source-trace WHAT [MAXDEPTH]\n\
    Show where data came from. WHAT may be a register name or memory address.\n\
    This command only works if m68kexec is started with --trace-data-sources.\n\
  s\n\
  step\n\
    Execute a single opcode, then prompt for commands again.\n\
  c\n\
  continue\n\
    Resume execution without tracing state.\n\
  t\n\
  trace\n\
    Resume execution with tracing state.\n\
  q\n\
  quit\n\
    Stop emulation and exit.\n\
");

      } else if ((cmd == "r") || (cmd == "read")) {
        auto tokens = split(args, ' ', 2);
        uint32_t addr = stoul(tokens.at(0), nullptr, 16);
        uint32_t size = stoul(tokens.at(1), nullptr, 16);
        const void* data = mem->template at<void>(addr, size);
        try {
          auto f = fopen_unique(tokens.at(2), "wb");
          fwritex(f.get(), data, size);
        } catch (const out_of_range&) {
          print_data(stderr, data, size, addr);
        }

      } else if ((cmd == "d") || (cmd == "disas")) {
        auto tokens = split(args, ' ', 2);
        uint32_t addr, size;
        if (tokens.size() == 1 && tokens[0].empty()) {
          addr = regs.pc;
          size = 0x20;
        } else {
          addr = stoul(tokens.at(0), nullptr, 16);
          size = stoul(tokens.at(1), nullptr, 16);
        }
        const void* data = mem->template at<void>(addr, size);

        multimap<uint32_t, string> labels;
        for (const auto& symbol_it : mem->all_symbols()) {
          if (symbol_it.second >= addr && symbol_it.second < addr + size) {
            labels.emplace(symbol_it.second, symbol_it.first);
          }
        }
        uint32_t pc = regs.pc;
        labels.emplace(pc, "pc");

        string disassembly = EmuT::disassemble(data, size, addr, &labels);
        try {
          save_file(tokens.at(2), disassembly);
        } catch (const out_of_range&) {
          fwritex(stderr, disassembly);
        }

      } else if ((cmd == "w") || (cmd == "write")) {
        auto tokens = split(args, ' ', 1);
        uint32_t addr = stoul(tokens.at(0), nullptr, 16);
        string data = parse_data_string(tokens.at(1));
        mem->memcpy(addr, data.data(), data.size());

      } else if ((cmd == "cp") || (cmd == "copy")) {
        auto tokens = split(args, ' ');
        uint32_t dest_addr = stoul(tokens.at(0), nullptr, 16);
        uint32_t src_addr = stoul(tokens.at(1), nullptr, 16);
        size_t size = stoull(tokens.at(2), nullptr, 16);
        mem->memcpy(dest_addr, src_addr, size);

      } else if ((cmd == "a") || (cmd == "alloc")) {
        auto tokens = split(args, ' ');
        uint32_t addr, size;
        if (tokens.size() < 2) {
          size = stoul(tokens.at(0), nullptr, 16);
          addr = mem->allocate(size);
        } else {
          addr = stoul(tokens.at(0), nullptr, 16);
          size = stoul(tokens.at(1), nullptr, 16);
          mem->allocate_at(addr, size);
        }
        fprintf(stderr, "allocated memory at %08" PRIX32 ":%" PRIX32 "\n",
            addr, size);

      } else if ((cmd == "j") || (cmd == "jump")) {
        regs.pc = stoul(args, nullptr, 16);
        emu.print_state_header(stderr);
        emu.print_state(stderr);

      } else if ((cmd == "b") || (cmd == "break")) {
        uint32_t addr = stoul(args, nullptr, 16);
        state.breakpoints.emplace(addr);
        fprintf(stderr, "added breakpoint at %08" PRIX32 "\n", addr);

      } else if ((cmd == "bc") || (cmd == "break-cycles")) {
        uint64_t count = stoull(args, nullptr, 16);
        if (count <= emu.cycles()) {
          fprintf(stderr, "cannot add cycle breakpoint at or before current cycle count\n");
        } else {
          state.cycle_breakpoints.emplace(count);
          fprintf(stderr, "added cycle breakpoint at %08" PRIX64 "\n", count);
        }

      } else if ((cmd == "u") || (cmd == "unbreak")) {
        uint32_t addr = args.empty() ? regs.pc : stoul(args, nullptr, 16);
        if (!state.breakpoints.erase(addr)) {
          fprintf(stderr, "no breakpoint existed at %08" PRIX32 "\n", addr);
        } else {
          fprintf(stderr, "deleted breakpoint at %08" PRIX32 "\n", addr);
        }

      } else if ((cmd == "uc") || (cmd == "unbreak-cycles")) {
        uint64_t count = stoull(args, nullptr, 16);
        if (!state.cycle_breakpoints.erase(count)) {
          fprintf(stderr, "no cycle breakpoint existed at %08" PRIX64 "\n", count);
        } else {
          fprintf(stderr, "deleted cycle breakpoint at %08" PRIX64 "\n", count);
        }

      } else if ((cmd == "sr") || (cmd == "setreg")) {
        auto tokens = split(args, ' ');
        regs.set_by_name(tokens.at(0), stoul(tokens.at(1), nullptr, 16));
        emu.print_state_header(stderr);
        emu.print_state(stderr);

      } else if ((cmd == "ss") || (cmd == "savestate")) {
        auto f = fopen_unique(args, "wb");
        emu.export_state(f.get());

      } else if ((cmd == "ls") || (cmd == "loadstate")) {
        auto f = fopen_unique(args, "rb");
        emu.import_state(f.get());
        emu.print_state_header(stderr);
        emu.print_state(stderr);

      } else if ((cmd == "st") || (cmd == "source-trace")) {
        auto tokens = split(args, ' ');
        size_t max_depth = 0;
        try {
          max_depth = stoull(tokens.at(1), nullptr, 0);
        } catch (const out_of_range& e) { }
        emu.print_source_trace(stderr, tokens.at(0), max_depth);

      } else if ((cmd == "s") || (cmd == "step")) {
        should_continue = true;

      } else if ((cmd == "c") || (cmd == "continue")) {
        state.mode = DebugMode::NONE;

      } else if ((cmd == "t") || (cmd == "trace")) {
        state.mode = DebugMode::TRACE;
        state.should_print_state_header = true;

      } else if ((cmd == "q") || (cmd == "quit")) {
        throw typename EmuT::terminate_emulation();

      } else {
        fprintf(stderr, "invalid command\n");
      }
    } catch (const typename EmuT::terminate_emulation&) {
      throw;
    } catch (const exception& e) {
      fprintf(stderr, "FAILED: %s\n", e.what());
    }
  }
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



enum class Architecture {
  M68K = 0,
  PPC32,
  X86,
};

int main(int argc, char** argv) {
  shared_ptr<MemoryContext> mem(new MemoryContext());
  X86Emulator emu_x86(mem);
  M68KEmulator emu_m68k(mem);
  PPC32Emulator emu_ppc32(mem);
  auto& regs_x86 = emu_x86.registers();
  auto& regs_m68k = emu_m68k.registers();
  auto& regs_ppc32 = emu_ppc32.registers();

  Architecture arch = Architecture::M68K;
  bool audit = false;
  bool trace_data_sources = false;
  bool trace_data_source_addrs = false;
  uint32_t pc = 0;
  const char* pe_filename = nullptr;
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
      arch = Architecture::X86;
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
      bool succeeded = false;
      try {
        regs_x86.set_by_name(tokens[0], value);
        succeeded = true;
      } catch (const invalid_argument&) { }
      try {
        regs_m68k.set_by_name(tokens[0], value);
        succeeded = true;
      } catch (const invalid_argument&) { }
      try {
        regs_ppc32.set_by_name(tokens[0], value);
        succeeded = true;
      } catch (const invalid_argument&) { }
      if (!succeeded) {
        throw invalid_argument("invalid register definition");
      }
    } else if (!strncmp(argv[x], "--state=", 8)) {
      state_filename = &argv[x][8];
    } else if (!strncmp(argv[x], "--break=", 8)) {
      state.breakpoints.emplace(stoul(&argv[x][8], nullptr, 16));
    } else if (!strncmp(argv[x], "--breakpoint=", 13)) {
      state.breakpoints.emplace(stoul(&argv[x][13], nullptr, 16));
    } else if (!strncmp(argv[x], "--break-cycles=", 15)) {
      state.cycle_breakpoints.emplace(stoul(&argv[x][15], nullptr, 16));
    } else if (!strcmp(argv[x], "--m68k")) {
      arch = Architecture::M68K;
    } else if (!strcmp(argv[x], "--ppc32")) {
      arch = Architecture::PPC32;
    } else if (!strcmp(argv[x], "--x86")) {
      arch = Architecture::X86;
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
      state.mode = DebugMode::TRACE;
    } else if (!strcmp(argv[x], "--step")) {
      state.mode = DebugMode::STEP;
    } else {
      throw invalid_argument("unknown argument: " + string(argv[x]));
    }
  }

  if (segment_defs.empty() && !state_filename && !pe_filename) {
    print_usage();
    return 1;
  }

  if (state_filename) {
    auto f = fopen_unique(state_filename, "rb");
    if (arch == Architecture::X86) {
      emu_x86.import_state(f.get());
    } else if (arch == Architecture::M68K) {
      emu_m68k.import_state(f.get());
    } else if (arch == Architecture::PPC32) {
      emu_ppc32.import_state(f.get());
    } else {
      throw logic_error("invalid architecture");
    }
  }

  // Load executable if needed
  if (pe_filename) {
    regs_x86.pc = load_pe(mem, pe_filename);
    regs_m68k.pc = regs_x86.pc;
    regs_ppc32.pc = regs_x86.pc;
  }

  // Apply pc if needed
  if (pc) {
    regs_x86.pc = pc;
    regs_m68k.pc = pc;
    regs_ppc32.pc = pc;
  }

  // Apply memory definitions
  for (const auto& def : segment_defs) {
    mem->allocate_at(def.addr, def.size);
    if (def.size <= def.data.size()) {
      mem->memcpy(def.addr, def.data.data(), def.size);
    } else {
      mem->memcpy(def.addr, def.data.data(), def.data.size());
      mem->memset(def.addr + def.data.size(), 0, def.size - def.data.size());
    }
  }

  // If the stack pointer doesn't make sense, allocate a stack region
  uint32_t sp;
  if (arch == Architecture::X86) {
    sp = regs_x86.r_esp();
  } else if (arch == Architecture::M68K) {
    sp = regs_m68k.a[7];
  } else if (arch == Architecture::PPC32) {
    sp = regs_ppc32.r[1].u;
  } else {
    throw logic_error("invalid architecture");
  }
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
    if (arch == Architecture::X86) {
      mem->write_u32l(sp, value);
    } else {
      mem->write_u32b(sp, value);
    }
  }

  // Save the possibly-modified stack pointer back to the regs structs
  regs_x86.w_esp(sp);
  regs_m68k.a[7] = sp;
  regs_ppc32.r[1].u = sp;
  regs_x86.reset_access_flags();

  // Apply any patches from the command line
  for (const auto& patch : patches) {
    mem->memcpy(patch.first, patch.second.data(), patch.second.size());
  }

  if (enable_syscalls) {
    // In M68K land, implement basic Mac syscalls
    emu_m68k.set_syscall_handler([&](M68KEmulator& emu, uint16_t syscall) {
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

      if (trap_number == 0x001E) { // NewPtr
        // D0 = size, A0 = returned ptr
        uint32_t addr = mem->allocate(regs.d[0].u);
        if (addr == 0) {
          throw runtime_error("cannot allocate memory for NewPtr");
        }
        regs.a[0] = addr; // Ptr

        if (state.mode != DebugMode::NONE) {
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

        if (state.mode != DebugMode::NONE) {
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

        if (state.mode != DebugMode::NONE) {
          fprintf(stderr, "[syscall_handler] GetHandleSize handle=%08" PRIX32 " => %08" PRIX32 "\n",
              regs.a[0], regs.d[0].s);
        }

      } else if ((trap_number == 0x0029) || (trap_number == 0x002A)) { // HLock/HUnlock
        // A0 = handle
        // We ignore this; blocks are never moved in our emulated system.
        if (state.mode != DebugMode::NONE) {
          fprintf(stderr, "[syscall_handler] %s handle=%08" PRIX32 "\n",
              (trap_number == 0x0029) ? "HLock" : "HUnlock", regs.a[0]);
        }
        regs.d[0].u = 0; // Result code (success)

      } else if (trap_number == 0x002E) { // BlockMove
        // A0 = src, A1 = dst, D0 = size
        mem->memcpy(regs.a[1], regs.a[0], regs.d[0].u);
        if (state.mode != DebugMode::NONE) {
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

    // In X86 land, we use a syscall to emulate library calls. This little stub
    // is used to transform the result of LoadLibraryA so it will return the
    // module handle if the DLL entry point returned nonzero.
    //   test eax, eax
    //   je return_null
    //   pop eax
    //   ret
    // return_null:
    //   add esp, 4
    //   ret
    static const string load_library_stub_data = "\x85\xC0\x74\x02\x58\xC3\x83\xC4\x04\xC3";
    uint32_t load_library_return_stub_addr = mem->allocate_within(
        0xF0000000, 0xFFFFFFFF, load_library_stub_data.size());
    mem->memcpy(load_library_return_stub_addr, load_library_stub_data.data(), load_library_stub_data.size());
    emu_x86.set_syscall_handler([&](X86Emulator& emu, uint8_t int_num) {
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

  // Set up the debug interfaces
  emu_x86.set_debug_hook(debug_hook_generic<X86Emulator>);
  emu_m68k.set_debug_hook(debug_hook_generic<M68KEmulator>);
  emu_ppc32.set_debug_hook(debug_hook_generic<PPC32Emulator>);

  // Run it
  if (arch == Architecture::X86) {
    emu_x86.set_audit(audit);
    emu_x86.set_trace_data_sources(trace_data_sources);
    emu_x86.set_trace_data_source_addrs(trace_data_source_addrs);
    emu_x86.execute();
    if (audit) {
      print_x86_audit_results(emu_x86);
    }

  } else if (arch == Architecture::M68K) {
    emu_m68k.execute();

  } else if (arch == Architecture::PPC32) {
    emu_ppc32.execute();

  } else {
    throw logic_error("invalid architecture");
  }

  return 0;
}
