#include <inttypes.h>
#include <string.h>

#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

#include "M68KEmulator.hh"

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

struct RegisterDefinition {
  bool a;
  uint8_t reg;
  uint32_t value;

  RegisterDefinition() : a(false), reg(0), value(0) { }
};

RegisterDefinition parse_register_definition(const char* def_str) {
  if ((def_str[0] != 'a') && (def_str[0] != 'A') &&
      (def_str[0] != 'd') && (def_str[0] != 'D')) {
    throw invalid_argument("invalid register type");
  }
  if ((def_str[1] < '0') || (def_str[1] > '7')) {
    throw invalid_argument("invalid register number");
  }
  if (def_str[2] != ':') {
    throw invalid_argument("invalid register specification");
  }

  RegisterDefinition def;
  def.a = (def_str[0] == 'A') || (def_str[0] == 'a');
  def.reg = def_str[1] - '0';
  def.value = strtoul(&def_str[3], nullptr, 16);
  return def;
}



enum class DebugMode {
  NONE,
  TRACE,
  STEP,
};

void print_usage() {
  fprintf(stderr, "\
Usage: m68kexec <options>\n\
\n\
For this program to be useful, at least one --mem option should be given. --pc\n\
probably should be given as well.\n\
\n\
All numbers are specified in hexadecimal.\n\
\n\
Options:\n\
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
  --push=VALUE\n\
      Push the given 32-bit value on the stack immediately before starting\n\
      execution. If this option is given multiple times, the values are pushed\n\
      in the order they are specified (that is, the last one specified ends up\n\
      at the lowest address on the stack, with A7 pointing to it).\n\
  --pc=ADDR\n\
      Start emulation at ADDR.\n\
  --reg=REG:VALUE\n\
      Set the given register\'s value before starting emulation. REG may be\n\
      D0-D7 or A0-A7. If A7 is not explicitly set using this option, a stack\n\
      region will be created automatically and A7 will point to the end of that\n\
      region.\n\
  --breakpoint=ADDR\n\
      Switch to single-step (shell) mode when execution reaches this address.\n\
  --trace\n\
      Start emulation in trace mode (show state after each cycle).\n\
  --step\n\
      Start emulation in single-step mode.\n\
");
}

int main(int argc, char** argv) {
  DebugMode debug_mode = DebugMode::NONE;
  uint32_t pc = 0;
  set<uint32_t> breakpoints;
  vector<SegmentDefinition> segment_defs;
  vector<RegisterDefinition> register_defs;
  vector<uint32_t> values_to_push;
  for (int x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "--mem=", 6)) {
      segment_defs.emplace_back(parse_segment_definition(&argv[x][6]));
    } else if (!strncmp(argv[x], "--push=", 7)) {
      values_to_push.emplace_back(strtoul(&argv[x][7], nullptr, 16));
    } else if (!strncmp(argv[x], "--pc=", 5)) {
      pc = stoul(&argv[x][5], nullptr, 16);
    } else if (!strncmp(argv[x], "--reg=", 6)) {
      register_defs.emplace_back(parse_register_definition(&argv[x][6]));
    } else if (!strncmp(argv[x], "--breakpoint=", 13)) {
      breakpoints.emplace(stoul(&argv[x][13], nullptr, 16));
    } else if (!strcmp(argv[x], "--trace")) {
      debug_mode = DebugMode::TRACE;
    } else if (!strcmp(argv[x], "--step")) {
      debug_mode = DebugMode::STEP;
    } else {
      throw invalid_argument("unknown argument: " + string(argv[x]));
    }
  }

  if (segment_defs.empty()) {
    print_usage();
    return 1;
  }

  // Apply register definitions
  M68KRegisters regs;
  regs.pc = pc;
  for (const auto& def : register_defs) {
    if (def.a) {
      regs.a[def.reg] = def.value;
    } else {
      regs.d[def.reg].u = def.value;
    }
  }

  // Apply memory definitions
  shared_ptr<MemoryContext> mem(new MemoryContext());
  for (const auto& def : segment_defs) {
    uint32_t addr = mem->allocate_at(def.addr, def.size);
    if (addr != def.addr) {
      throw runtime_error(string_printf(
          "cannot allocate block at %08" PRIX32, def.addr));
    }

    void* segment_data = mem->at(addr);
    if (def.size <= def.data.size()) {
      memcpy(segment_data, def.data.data(), def.size);
    } else {
      memcpy(segment_data, def.data.data(), def.data.size());
      void* zero_data = reinterpret_cast<uint8_t*>(segment_data) + def.data.size();
      memset(zero_data, 0, def.size - def.data.size());
    }
  }

  // If the stack pointer doesn't make sense, allocate a stack region and set A7
  try {
    mem->at(regs.a[7]);
  } catch (const out_of_range&) {
    static const size_t stack_size = 0x10000;
    uint32_t stack_addr = mem->allocate(stack_size);
    regs.a[7] = stack_addr + stack_size;
    fprintf(stderr, "note: automatically creating stack region at %08" PRIX32 ":%zX with A7=%08" PRIX32 "\n",
        stack_addr, stack_size, regs.a[7]);
  }

  // Push the requested values to the stack
  for (uint32_t value : values_to_push) {
    regs.a[7] -= 4;
    mem->write_u32(regs.a[7], value);
  }

  // Implement basic syscalls
  M68KEmulator emu(mem);
  emu.set_syscall_handler([&](M68KEmulator&, M68KRegisters& regs, uint16_t syscall) -> bool {
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

      if (debug_mode != DebugMode::NONE) {
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
      mem->write_u32(addr, addr + 4);

      if (debug_mode != DebugMode::NONE) {
        fprintf(stderr, "[syscall_handler] NewHandle size=%08" PRIX32 " => %08" PRIX32 "\n",
            regs.d[0].u, regs.a[0]);
      }
      regs.d[0].u = 0; // Result code (success)

    } else if (trap_number == 0x0025) { // GetHandleSize
      // A0 = handle, D0 = returned size or error code (if <0)
      try {
        regs.d[0].u = mem->get_block_size(mem->read_u32(regs.a[0]));
      } catch (const out_of_range&) {
        regs.d[0].s = -111; // memWZErr
      }

      if (debug_mode != DebugMode::NONE) {
        fprintf(stderr, "[syscall_handler] GetHandleSize handle=%08" PRIX32 " => %08" PRIX32 "\n",
            regs.a[0], regs.d[0].s);
      }

    } else if ((trap_number == 0x0029) || (trap_number == 0x002A)) { // HLock/HUnlock
      // A0 = handle
      // We ignore this; blocks are never moved in our emulated system.
      if (debug_mode != DebugMode::NONE) {
        fprintf(stderr, "[syscall_handler] %s handle=%08" PRIX32 "\n",
            (trap_number == 0x0029) ? "HLock" : "HUnlock", regs.a[0]);
      }
      regs.d[0].u = 0; // Result code (success)

    } else if (trap_number == 0x002E) { // BlockMove
      // A0 = src, A1 = dst, D0 = size
      const void* src = mem->at(regs.a[0], regs.d[0].u);
      void* dst = mem->at(regs.a[1], regs.d[0].u);
      memcpy(dst, src, regs.d[0].u);

      if (debug_mode != DebugMode::NONE) {
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

  // Set up the debug interface
  bool should_print_state_header = true;
  emu.set_debug_hook([&](M68KEmulator& emu, M68KRegisters& regs) {
    if (breakpoints.count(regs.pc)) {
      fprintf(stderr, "reached breakpoint at %08" PRIX32 "\n", regs.pc);
      debug_mode = DebugMode::STEP;
    }
    if (debug_mode != DebugMode::NONE) {
      if (debug_mode == DebugMode::STEP || should_print_state_header) {
        emu.print_state_header(stderr);
        should_print_state_header = false;
      }
      emu.print_state(stderr);
    }
    bool should_continue = false;
    while ((debug_mode == DebugMode::STEP) && !should_continue) {
      fprintf(stderr, "pc=%08" PRIX32 "> ", regs.pc);
      fflush(stderr);
      string cmd(0x400, '\0');
      if (!fgets(cmd.data(), cmd.size(), stdin)) {
        fprintf(stderr, "stdin was closed; stopping emulation\n");
        return false;
      }
      cmd.resize(strlen(cmd.c_str()));
      if (ends_with(cmd, "\n")) {
        cmd.resize(cmd.size() - 1);
      }

      try {
        auto tokens = split(cmd, ' ');
        if (tokens.empty()) {
          fprintf(stderr, "no command; try \'h\'\n");
        } else if (tokens.at(0) == "h") {
          fprintf(stderr, "\
commands:\n\
  r ADDR SIZE - read memory\n\
  r ADDR SIZE FILENAME - save memory contents to file\n\
  d ADDR SIZE - disassemble memory\n\
  d ADDR SIZE FILENAME - disassemble memory and save to file\n\
  w ADDR DATA - write memory\n\
  a ADDR SIZE - allocate memory at address\n\
  a SIZE - allocate memory anywhere\n\
  b ADDR - set a breakpoint at ADDR\n\
  u ADDR - delete the breakpoint at ADDR\n\
  j ADDR - jump to addr\n\
  sr REG VALUE - set reg value\n\
  ss FILENAME - save state\n\
  ls FILENAME - load state\n\
  s - step\n\
  c - continue\n\
  ct - continue with trace)\n\
  q - quit\n\
");
        } else if (tokens.at(0) == "r") {
          uint32_t addr = stoul(tokens.at(1), nullptr, 16);
          uint32_t size = stoul(tokens.at(2), nullptr, 16);
          const void* data = mem->at(addr, size);
          try {
            auto f = fopen_unique(tokens.at(3), "wb");
            fwritex(f.get(), data, size);
          } catch (const out_of_range&) {
            print_data(stderr, data, size, addr);
          }

        } else if (tokens.at(0) == "d") {
          uint32_t addr, size;
          if (tokens.size() == 1) {
            addr = regs.pc;
            size = 0x20;
          } else {
            addr = stoul(tokens.at(1), nullptr, 16);
            size = stoul(tokens.at(2), nullptr, 16);
          }
          const void* data = mem->at(addr, size);

          multimap<uint32_t, string> labels;
          for (const auto& symbol_it : mem->all_symbols()) {
            if (symbol_it.second >= addr && symbol_it.second < addr + size) {
              labels.emplace(symbol_it.second, symbol_it.first);
            }
          }
          labels.emplace(regs.pc, "pc");

          string disassembly = M68KEmulator::disassemble(data, size, addr, &labels);
          try {
            save_file(tokens.at(3), disassembly);
          } catch (const out_of_range&) {
            fwritex(stderr, disassembly);
          }

        } else if (tokens.at(0) == "w") {
          uint32_t addr = stoul(tokens.at(1), nullptr, 16);
          string data_str;
          for (size_t x = 2; x < tokens.size(); x++) {
            data_str += tokens[x];
          }
          string data = parse_data_string(data_str);
          void* dest = mem->at(addr, data.size());
          memcpy(dest, data.data(), data.size());

        } else if (tokens.at(0) == "a") {
          if (tokens.size() < 3) {
            uint32_t size = stoul(tokens.at(1), nullptr, 16);
            uint32_t addr = mem->allocate(size);
            fprintf(stderr, "allocated memory at %08" PRIX32 ":%" PRIX32 "\n",
                addr, size);
          } else {
            uint32_t addr = stoul(tokens.at(1), nullptr, 16);
            uint32_t size = stoul(tokens.at(2), nullptr, 16);
            if (mem->allocate_at(addr, size) != addr) {
              throw runtime_error("cannot allocate memory at address");
            }
            fprintf(stderr, "allocated memory at %08" PRIX32 ":%" PRIX32 "\n",
                addr, size);
          }

        } else if (tokens.at(0) == "j") {
          regs.pc = stoul(tokens.at(1), nullptr, 16);

        } else if (tokens.at(0) == "b") {
          uint32_t addr = stoul(tokens.at(1), nullptr, 16);
          breakpoints.emplace(addr);
          fprintf(stderr, "added breakpoint at %08" PRIX32 "\n", addr);

        } else if (tokens.at(0) == "u") {
          uint32_t addr = stoul(tokens.at(1), nullptr, 16);
          if (!breakpoints.erase(addr)) {
            fprintf(stderr, "no breakpoint existed at %08" PRIX32 "\n", addr);
          } else {
            fprintf(stderr, "deleted breakpoint at %08" PRIX32 "\n", addr);
          }

        } else if (tokens.at(0) == "sr") {
          const string& reg_str = tokens.at(1);
          if (reg_str.size() < 2) {
            throw invalid_argument("invalid register name");
          }
          uint8_t reg_num = strtoul(reg_str.data() + 1, nullptr, 10);
          uint32_t value = stoul(tokens.at(2), nullptr, 16);
          if (reg_str.at(0) == 'a' || reg_str.at(0) == 'A') {
            regs.a[reg_num] = value;
          } else if (reg_str.at(0) == 'd' || reg_str.at(0) == 'D') {
            regs.d[reg_num].u = value;
          } else {
            throw invalid_argument("invalid register name");
          }

        } else if (tokens.at(0) == "ss") {
          auto f = fopen_unique(tokens.at(1), "wb");
          emu.export_state(f.get());

        } else if (tokens.at(0) == "ls") {
          auto f = fopen_unique(tokens.at(1), "rb");
          emu.import_state(f.get());
          emu.print_state_header(stderr);
          emu.print_state(stderr);

        } else if (tokens.at(0) == "s") {
          should_continue = true;

        } else if (tokens.at(0) == "c") {
          debug_mode = DebugMode::NONE;

        } else if (tokens.at(0) == "ct") {
          debug_mode = DebugMode::TRACE;
          should_print_state_header = true;

        } else if (tokens.at(0) == "q") {
          return false;

        } else {
          fprintf(stderr, "invalid command\n");
        }
      } catch (const exception& e) {
        fprintf(stderr, "FAILED: %s\n", e.what());
      }
    }
    return true;
  });

  // Run it
  emu.execute(regs);

  return 0;
}
