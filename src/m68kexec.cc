#include <inttypes.h>
#include <string.h>

#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

#include "M68KEmulator.hh"
#include "X86Emulator.hh"
#include "PEFile.hh"

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

struct M68KRegisterDefinition {
  bool a;
  uint8_t reg;
  uint32_t value;

  M68KRegisterDefinition() : a(false), reg(0), value(0) { }
};

struct X86RegisterDefinition {
  uint8_t reg; // 0 = eax, 1 = ecx, ... 7 = edi, 8 = eflags
  uint32_t value;

  X86RegisterDefinition() : reg(0), value(0) { }
};

using RegisterDefinition = std::variant<M68KRegisterDefinition, X86RegisterDefinition>;

RegisterDefinition parse_register_definition(const char* def_str) {
  if (!*def_str) {
    throw invalid_argument("register definition is empty");
  }
  string lower_def_str = tolower(def_str);

  // M68K registers start with A or D; x86 registers start with E
  if ((lower_def_str[0] == 'a') || (lower_def_str[0] == 'd')) {
    if ((lower_def_str[1] < '0') || (lower_def_str[1] > '7')) {
      throw invalid_argument("invalid register number");
    }
    if (lower_def_str[2] != ':') {
      throw invalid_argument("invalid register specification");
    }

    M68KRegisterDefinition def;
    def.a = (lower_def_str[0] == 'A') || (lower_def_str[0] == 'a');
    def.reg = lower_def_str[1] - '0';
    def.value = strtoul(&lower_def_str[3], nullptr, 16);
    return def;

  } else if (lower_def_str[0] == 'e') {
    X86RegisterDefinition def;
    if (starts_with(lower_def_str, "eax:")) {
      def.reg = 0;
      def.value = stoul(lower_def_str.substr(4), nullptr, 16);
    } else if (starts_with(lower_def_str, "ecx:")) {
      def.reg = 1;
      def.value = stoul(lower_def_str.substr(4), nullptr, 16);
    } else if (starts_with(lower_def_str, "edx:")) {
      def.reg = 2;
      def.value = stoul(lower_def_str.substr(4), nullptr, 16);
    } else if (starts_with(lower_def_str, "ebx:")) {
      def.reg = 3;
      def.value = stoul(lower_def_str.substr(4), nullptr, 16);
    } else if (starts_with(lower_def_str, "esp:")) {
      def.reg = 4;
      def.value = stoul(lower_def_str.substr(4), nullptr, 16);
    } else if (starts_with(lower_def_str, "ebp:")) {
      def.reg = 5;
      def.value = stoul(lower_def_str.substr(4), nullptr, 16);
    } else if (starts_with(lower_def_str, "esi:")) {
      def.reg = 6;
      def.value = stoul(lower_def_str.substr(4), nullptr, 16);
    } else if (starts_with(lower_def_str, "edi:")) {
      def.reg = 7;
      def.value = stoul(lower_def_str.substr(4), nullptr, 16);
    } else if (starts_with(lower_def_str, "eflags:")) {
      def.reg = 8;
      def.value = stoul(lower_def_str.substr(4), nullptr, 16);
    } else {
      throw invalid_argument("unknown x86 register");
    }
    return def;

  } else {
    throw invalid_argument("unknown register type");
  }
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
          res.regs_before.eax.u.load(), res.regs_before.ecx.u.load(), res.regs_before.edx.u.load(),
          res.regs_before.ebx.u.load(), res.regs_before.esp.u.load(), res.regs_before.ebp.u.load(),
          res.regs_before.esi.u.load(), res.regs_before.edi.u.load(), res.regs_before.eflags,
          flags_before.c_str(), res.regs_before.eip);
      fprintf(stderr, "AFTER:  %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 " %s%08" PRIX32 "(%s)%s @ %08" PRIX32 "\n",
          (res.regs_before.eax.u != res.regs_after.eax.u) ? different_token : same_token,
          res.regs_after.eax.u.load(),
          (res.regs_before.ecx.u != res.regs_after.ecx.u) ? different_token : same_token,
          res.regs_after.ecx.u.load(),
          (res.regs_before.edx.u != res.regs_after.edx.u) ? different_token : same_token,
          res.regs_after.edx.u.load(),
          (res.regs_before.ebx.u != res.regs_after.ebx.u) ? different_token : same_token,
          res.regs_after.ebx.u.load(),
          (res.regs_before.esp.u != res.regs_after.esp.u) ? different_token : same_token,
          res.regs_after.esp.u.load(),
          (res.regs_before.ebp.u != res.regs_after.ebp.u) ? different_token : same_token,
          res.regs_after.ebp.u.load(),
          (res.regs_before.esi.u != res.regs_after.esi.u) ? different_token : same_token,
          res.regs_after.esi.u.load(),
          (res.regs_before.edi.u != res.regs_after.edi.u) ? different_token : same_token,
          res.regs_after.edi.u.load(),
          (res.regs_before.eflags != res.regs_after.eflags) ? different_token : same_token,
          res.regs_after.eflags,
          flags_after.c_str(),
          same_token, res.regs_after.eip);
      // TODO: We currently don't collect these anywhere, but we should!
      for (const auto& acc : res.mem_accesses) {
        string value_str;
        if (acc.size == 8) {
          value_str = string_printf("%02" PRIX32, acc.value);
        } else if (acc.size == 16) {
          value_str = string_printf("%04" PRIX32, acc.value);
        } else if (acc.size == 32) {
          value_str = string_printf("%08" PRIX32, acc.value);
        } else {
          value_str = string_printf("%08" PRIX32 " (!)", acc.value);
        }
        if (acc.is_write) {
          fprintf(stderr, "MEMORY (W): [%08" PRIX32 "] <= %s\n", acc.addr, value_str.c_str());
        } else {
          fprintf(stderr, "MEMORY (R): %s <= [%08" PRIX32 "]\n", value_str.c_str(), acc.addr);
        }
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
All numbers are specified in hexadecimal.\n\
\n\
Options:\n\
  --m68k\n\
      Emulate a Motorola 68000 CPU (default).\n\
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
  --breakpoint=ADDR\n\
      Switch to single-step (shell) mode when execution reaches this address.\n\
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
  DebugMode mode;

  DebugState() : should_print_state_header(true), mode(DebugMode::NONE) { }
};

DebugState state;

template <typename EmuT, typename RegsT>
void debug_hook_generic(EmuT& emu, RegsT& regs) {
  auto mem = emu.memory();

  if (state.breakpoints.count(regs.pc)) {
    fprintf(stderr, "reached breakpoint at %08" PRIX32 "\n", regs.pc);
    state.mode = DebugMode::STEP;
  }
  if (state.mode != DebugMode::NONE) {
    if ((state.mode == DebugMode::STEP) ||
        ((state.mode == DebugMode::TRACE) && ((emu.cycles() & 0x1F) == 0)) ||
        state.should_print_state_header) {
      emu.print_state_header(stderr);
      state.should_print_state_header = false;
    }
    emu.print_state(stderr);
  }

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
        labels.emplace(regs.pc, "pc");

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

      } else if ((cmd == "b") || (cmd == "break")) {
        uint32_t addr = stoul(args, nullptr, 16);
        state.breakpoints.emplace(addr);
        fprintf(stderr, "added breakpoint at %08" PRIX32 "\n", addr);

      } else if ((cmd == "u") || (cmd == "unbreak")) {
        uint32_t addr = stoul(args, nullptr, 16);
        if (!state.breakpoints.erase(addr)) {
          fprintf(stderr, "no breakpoint existed at %08" PRIX32 "\n", addr);
        } else {
          fprintf(stderr, "deleted breakpoint at %08" PRIX32 "\n", addr);
        }

      } else if ((cmd == "sr") || (cmd == "setreg")) {
        auto tokens = split(args, ' ');
        regs.set_by_name(tokens.at(0), stoul(tokens.at(1), nullptr, 16));

      } else if ((cmd == "ss") || (cmd == "savestate")) {
        auto f = fopen_unique(args, "wb");
        emu.export_state(f.get());

      } else if ((cmd == "ls") || (cmd == "loadstate")) {
        auto f = fopen_unique(args, "rb");
        emu.import_state(f.get());
        emu.print_state_header(stderr);
        emu.print_state(stderr);

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
    } catch (const exception& e) {
      fprintf(stderr, "FAILED: %s\n", e.what());
    }
  }
}

enum class Architecture {
  M68K = 0,
  X86,
};

int main(int argc, char** argv) {
  Architecture arch = Architecture::M68K;
  bool audit = false;
  uint32_t pc = 0;
  const char* pe_filename = nullptr;
  vector<SegmentDefinition> segment_defs;
  X86Registers regs_x86;
  M68KRegisters regs_m68k;
  vector<uint32_t> values_to_push;
  const char* state_filename = nullptr;
  for (int x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "--mem=", 6)) {
      segment_defs.emplace_back(parse_segment_definition(&argv[x][6]));
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
      if (!succeeded) {
        throw invalid_argument("invalid register definition");
      }
    } else if (!strncmp(argv[x], "--state=", 8)) {
      state_filename = &argv[x][8];
    } else if (!strncmp(argv[x], "--breakpoint=", 13)) {
      state.breakpoints.emplace(stoul(&argv[x][13], nullptr, 16));
    } else if (!strcmp(argv[x], "--m68k")) {
      arch = Architecture::M68K;
    } else if (!strcmp(argv[x], "--x86")) {
      arch = Architecture::X86;
    } else if (!strcmp(argv[x], "--audit")) {
      audit = true;
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

  shared_ptr<MemoryContext> mem(new MemoryContext());
  X86Emulator emu_x86(mem);
  M68KEmulator emu_m68k(mem);
  if (state_filename) {
    auto f = fopen_unique(state_filename, "rb");
    if (arch == Architecture::X86) {
      emu_x86.import_state(f.get());
    } else if (arch == Architecture::M68K) {
      emu_m68k.import_state(f.get());
    } else {
      throw logic_error("invalid architecture");
    }
  }

  // Load executable if needed
  if (pe_filename) {
    PEFile pe(pe_filename);
    pe.load_into(mem);

    const auto& header = pe.unloaded_header();
    regs_x86.pc = header.entrypoint_rva + header.image_base;
    regs_m68k.pc = header.entrypoint_rva + header.image_base;
  }

  // Apply pc if needed
  if (pc) {
    regs_x86.pc = pc;
    regs_m68k.pc = pc;
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

  // If the stack pointer doesn't make sense, allocate a stack region and set A7
  uint32_t sp;
  if (arch == Architecture::X86) {
    sp = regs_x86.esp.u.load();
  } else if (arch == Architecture::M68K) {
    sp = regs_m68k.a[7];
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

  // Save the possibly-modified sp back to the regs struct
  regs_x86.esp.u = sp;
  regs_m68k.a[7] = sp;

  // In M68K land, implement basic Mac syscalls
  emu_m68k.set_syscall_handler([&](M68KEmulator&, M68KRegisters& regs, uint16_t syscall) -> bool {
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

    return true;
  });

  // Set up the debug interfaces
  emu_x86.set_debug_hook(debug_hook_generic<X86Emulator, X86Registers>);
  emu_m68k.set_debug_hook(debug_hook_generic<M68KEmulator, M68KRegisters>);

  // Run it
  if (arch == Architecture::X86) {
    emu_x86.set_audit(audit);
    emu_x86.execute(regs_x86);
    if (audit) {
      print_x86_audit_results(emu_x86);
    }

  } else {
    emu_m68k.execute(regs_m68k);
  }

  return 0;
}
