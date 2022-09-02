#pragma once

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>

#include <vector>
#include <phosg/Strings.hh>
#include <phosg/Filesystem.hh>
#include <set>
#include <string>
#include <stdexcept>

#include "MemoryContext.hh"



template <typename T>
constexpr uint8_t bits_for_type = sizeof(T) << 3;

template <typename T>
constexpr T msb_for_type = (1 << (bits_for_type<T> - 1));



class EmulatorBase {
public:
  explicit EmulatorBase(std::shared_ptr<MemoryContext> mem);
  virtual ~EmulatorBase() = default;

  virtual void import_state(FILE* stream) = 0;
  virtual void export_state(FILE* stream) const = 0;

  inline std::shared_ptr<MemoryContext> memory() {
    return this->mem;
  }

  inline uint64_t cycles() const {
    return this->instructions_executed;
  }

  virtual void print_state_header(FILE* stream) const = 0;
  virtual void print_state(FILE* stream) const = 0;

  // The syscall handler or debug hook can throw this to terminate emulation
  // cleanly (and cause .execute() to return). Throwing any other type of
  // exception will cause emulation to terminate uncleanly and the exception
  // will propagate out of .execute().
  class terminate_emulation : public std::runtime_error {
  public:
    terminate_emulation() : runtime_error("terminate emulation") { }
    ~terminate_emulation() = default;
  };

  virtual void set_behavior_by_name(const std::string& name);

  virtual void set_time_base(uint64_t time_base);
  virtual void set_time_base(const std::vector<uint64_t>& time_overrides);

  inline void set_log_memory_access(bool log_memory_access) {
    this->log_memory_access = log_memory_access;
    if (!this->log_memory_access) {
      this->memory_access_log.clear();
    }
  }
  inline bool get_log_memory_access() const {
    return this->log_memory_access;
  }

  struct MemoryAccess {
    uint32_t addr;
    uint8_t size;
    bool is_write;
  };

  std::vector<MemoryAccess> get_and_clear_memory_access_log();

  virtual void print_source_trace(FILE* stream, const std::string& what, size_t max_depth = 0) const = 0;

  virtual void execute() = 0;

protected:
  std::shared_ptr<MemoryContext> mem;
  uint64_t instructions_executed;

  bool log_memory_access;
  std::vector<MemoryAccess> memory_access_log;

  void report_mem_access(uint32_t addr, uint8_t size, bool is_write);
};



enum class DebuggerMode {
  NONE,
  PERIODIC_TRACE,
  TRACE,
  STEP,
};

struct EmulatorDebuggerState {
  std::set<uint32_t> breakpoints;
  std::set<uint64_t> cycle_breakpoints;
  uint64_t max_cycles;
  DebuggerMode mode;
  uint64_t trace_period;
  bool print_state_headers;
  bool print_memory_accesses;

  EmulatorDebuggerState();
};

template <typename EmuT>
class EmulatorDebugger {
public:
  EmuT* bound_emu;
  EmulatorDebuggerState state;

  EmulatorDebugger() : bound_emu(nullptr), should_print_state_header(true) { }

  void bind(EmuT& emu) {
    this->bound_emu = &emu;
    this->bound_emu->set_debug_hook(std::bind(&EmulatorDebugger::debug_hook, this, std::placeholders::_1));
  }
  void unbind() {
    if (this->bound_emu) {
      this->bound_emu->set_debug_hook(nullptr);
      this->bound_emu = nullptr;
    }
  }

private:
  bool should_print_state_header;

  void print_state_header(const EmuT& emu) {
    if (this->state.print_state_headers) {
      emu.print_state_header(stderr);
    }
  }

  void debug_hook(EmuT& emu) {
    auto mem = emu.memory();
    auto& regs = emu.registers();

    if (this->state.max_cycles && emu.cycles() >= this->state.max_cycles) {
      fprintf(stderr, "reached maximum cycle count\n");
      throw typename EmuT::terminate_emulation();
    }

    if (this->state.cycle_breakpoints.erase(emu.cycles())) {
      fprintf(stderr, "reached cycle breakpoint at %08" PRIX64 "\n", emu.cycles());
      this->state.mode = DebuggerMode::STEP;
    } else if (this->state.breakpoints.count(regs.pc)) {
      fprintf(stderr, "reached execution breakpoint at %08" PRIX32 "\n", regs.pc);
      this->state.mode = DebuggerMode::STEP;
    }
    if (this->state.mode != DebuggerMode::NONE &&
        (this->state.mode != DebuggerMode::PERIODIC_TRACE || ((emu.cycles() % this->state.trace_period) == 0))) {
      if ((this->state.mode == DebuggerMode::STEP) ||
          ((this->state.mode == DebuggerMode::TRACE) && ((emu.cycles() & 0x1F) == 0)) ||
          ((this->state.mode == DebuggerMode::PERIODIC_TRACE) && (((emu.cycles() / this->state.trace_period) % 32) == 0)) ||
          this->should_print_state_header) {
        this->print_state_header(emu);
        this->should_print_state_header = false;
      }
      auto accesses = emu.get_and_clear_memory_access_log();
      if (this->state.print_memory_accesses) {
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
      }
      emu.print_state(stderr);
    }

    // If in trace or step mode, log all memory accesses (so they can be printed
    // before the current paused state, above)
    emu.set_log_memory_access(this->state.mode != DebuggerMode::NONE && this->state.mode != DebuggerMode::PERIODIC_TRACE);

    bool should_continue = false;
    while ((this->state.mode == DebuggerMode::STEP) && !should_continue) {
      fprintf(stderr, "pc=%08" PRIX32 "> ", regs.pc);
      fflush(stderr);
      std::string input_line(0x400, '\0');
      if (!fgets(input_line.data(), input_line.size(), stdin)) {
        fprintf(stderr, "stdin was closed; stopping emulation\n");
        throw typename EmuT::terminate_emulation();
      }
      strip_trailing_zeroes(input_line);
      strip_trailing_whitespace(input_line);

      try {
        auto input_tokens = split(input_line, ' ', 1);
        const std::string& cmd = input_tokens.at(0);
        const std::string& args = input_tokens.size() == 2 ? input_tokens.at(1) : "";
        if (cmd.empty()) {
          fprintf(stderr, "no command; try \'h\'\n");

        } else if ((cmd == "h") || (cmd == "help")) {
          fprintf(stderr, "\
  Commands:\n\
    s\n\
    step\n\
      Execute a single opcode, then prompt for commands again.\n\
    t\n\
    trace\n\
      Resume execution with tracing state. This will run emulation until the\n\
      next breakpoint, or until emulation terminates cleanly or encounters an\n\
      error. The debugger prints the register state and disassembly for each\n\
      opcode executed.\n\
    pt [N]\n\
    periodic-trace [N]\n\
      Like the trace command, but only prints state every N cycles. The default\n\
      value for N is 0x100.\n\
    c\n\
    continue\n\
      Resume execution without tracing state. Like the trace command above, but\n\
      does not print anything for each opcode.\n\
    q\n\
    quit\n\
      Stop emulation and exit.\n\
    r ADDR SIZE [FILENAME]\n\
    read ADDR SIZE [FILENAME]\n\
      Read memory. If FILENAME is given, save the raw data to the file;\n\
      otherwise, display it in the terminal in a hex/ASCII view.\n\
    d ADDR SIZE [FILENAME]\n\
    disas ADDR SIZE [FILENAME]\n\
      Disassemble memory. If FILENAME is given, save the disassembly text to\n\
      the file; otherwise, display it in the terminal.\n\
    w ADDR DATA\n\
    write ADDR DATA\n\
      Write memory. Data is given in parse_data_string format (hex strings,\n\
      quoted ASCII strings, etc.).\n\
    cp DSTADDR SRCADDR SIZE\n\
    copy DSTADDR SRCADDR SIZE\n\
      Copy SIZE bytes from SRCADDR to DESTADDR.\n\
    a [ADDR] SIZE\n\
    alloc [ADDR] SIZE\n\
      Allocate memory. If ADDR is given, allocate it at a specific address.\n\
    g\n\
    regions\n\
      List all allocated regions in emulated memory.\n\
    f DATA\n\
    find DATA\n\
      Search for DATA in all allocated memory.\n\
    b ADDR\n\
    break ADDR\n\
      Set an execution breakpoint at ADDR. When the emulator's PC register\n\
      reaches this address, the emulator switches to single-step mode.\n\
    bc CYCLE\n\
    break-cycles CYCLE\n\
      Set an execution breakpoint at cycle CYCLE. When given number of opcodes\n\
      have been executed, the emulator switches to single-step mode. CYCLE is\n\
      measured from the beginning of emulation, not from the current cycle.\n\
    u ADDR\n\
    unbreak ADDR\n\
      Delete the execution breakpoint at ADDR.\n\
    uc CYCLE\n\
    unbreak-cycles CYCLE\n\
      Delete the cycle breakpoint at ADDR. Cycle breakpoints are automatically\n\
      deleted when they are reached, but can be manually deleted before then\n\
      with this command.\n\
    j ADDR\n\
    jump ADDR\n\
      Jump to ADDR. This only changes PC; emulation is not resumed.\n\
    sr REG VALUE\n\
    setreg REG VALUE\n\
      Set the value of a register. REG is specified by name; for M68K this can\n\
      be A0, D3, etc.; for PPC32 this can be r0, r1, r2, etc.; for X86 this\n\
      can be a register name like eax, cl, sp, etc. VALUE is specified in hex.\n\
    ss FILENAME\n\
    savestate FILENAME\n\
      Save memory and emulation state to a file.\n\
    ls FILENAME\n\
    loadstate FILENAME\n\
      Load memory and emulation state from a file.\n\
    st WHAT [MAXDEPTH]\n\
    source-trace WHAT [MAXDEPTH]\n\
      Show where data came from. WHAT may be a register name or memory address.\n\
      This command only works if data source tracing has been enabled since\n\
      emulation began, and is currently only implemented for x86 emulation.\n\
");

        } else if ((cmd == "r") || (cmd == "read")) {
          auto tokens = split(args, ' ', 2);
          uint32_t addr = stoul(tokens.at(0), nullptr, 16);
          uint32_t size = stoul(tokens.at(1), nullptr, 16);
          const void* data = mem->template at<void>(addr, size);
          try {
            auto f = fopen_unique(tokens.at(2), "wb");
            fwritex(f.get(), data, size);
          } catch (const std::out_of_range&) {
            print_data(stderr, data, size, addr, nullptr, PrintDataFlags::PRINT_ASCII | PrintDataFlags::OFFSET_32_BITS);
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

          std::multimap<uint32_t, std::string> labels;
          for (const auto& symbol_it : mem->all_symbols()) {
            if (symbol_it.second >= addr && symbol_it.second < addr + size) {
              labels.emplace(symbol_it.second, symbol_it.first);
            }
          }
          uint32_t pc = regs.pc;
          labels.emplace(pc, "pc");

          std::string disassembly = EmuT::disassemble(data, size, addr, &labels);
          try {
            save_file(tokens.at(2), disassembly);
          } catch (const std::out_of_range&) {
            fwritex(stderr, disassembly);
          }

        } else if ((cmd == "w") || (cmd == "write")) {
          auto tokens = split(args, ' ', 1);
          uint32_t addr = stoul(tokens.at(0), nullptr, 16);
          std::string data = parse_data_string(tokens.at(1));
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

        } else if ((cmd == "g") || (cmd == "regions")) {
          for (const auto& it : mem->allocated_blocks()) {
            std::string size_str = format_size(it.second);
            fprintf(stderr, "region: %08" PRIX32 "-%08" PRIX32 " (%s)\n",
                it.first, it.first + it.second, size_str.c_str());
          }

        } else if ((cmd == "f") || (cmd == "find")) {
          std::string search_data = parse_data_string(args);
          for (const auto& it : mem->allocated_blocks()) {
            if (it.second < search_data.size()) {
              continue;
            }
            auto* mem_data = mem->template at<const char>(it.first, it.second);
            for (size_t z = 0; z <= it.second - search_data.size(); z++) {
              if (!memcmp(&mem_data[z], search_data.data(), search_data.size())) {
                fprintf(stderr, "found at %08" PRIX32 "\n",
                    static_cast<uint32_t>(it.first + z));
              }
            }
          }

        } else if ((cmd == "j") || (cmd == "jump")) {
          regs.pc = stoul(args, nullptr, 16);
          this->print_state_header(emu);
          emu.print_state(stderr);

        } else if ((cmd == "b") || (cmd == "break")) {
          uint32_t addr = stoul(args, nullptr, 16);
          this->state.breakpoints.emplace(addr);
          fprintf(stderr, "added breakpoint at %08" PRIX32 "\n", addr);

        } else if ((cmd == "bc") || (cmd == "break-cycles")) {
          uint64_t count = stoull(args, nullptr, 16);
          if (count <= emu.cycles()) {
            fprintf(stderr, "cannot add cycle breakpoint at or before current cycle count\n");
          } else {
            this->state.cycle_breakpoints.emplace(count);
            fprintf(stderr, "added cycle breakpoint at %08" PRIX64 "\n", count);
          }

        } else if ((cmd == "u") || (cmd == "unbreak")) {
          uint32_t addr = args.empty() ? regs.pc : stoul(args, nullptr, 16);
          if (!this->state.breakpoints.erase(addr)) {
            fprintf(stderr, "no breakpoint existed at %08" PRIX32 "\n", addr);
          } else {
            fprintf(stderr, "deleted breakpoint at %08" PRIX32 "\n", addr);
          }

        } else if ((cmd == "uc") || (cmd == "unbreak-cycles")) {
          uint64_t count = stoull(args, nullptr, 16);
          if (!this->state.cycle_breakpoints.erase(count)) {
            fprintf(stderr, "no cycle breakpoint existed at %08" PRIX64 "\n", count);
          } else {
            fprintf(stderr, "deleted cycle breakpoint at %08" PRIX64 "\n", count);
          }

        } else if ((cmd == "sr") || (cmd == "setreg")) {
          auto tokens = split(args, ' ');
          regs.set_by_name(tokens.at(0), stoul(tokens.at(1), nullptr, 16));
          this->print_state_header(emu);
          emu.print_state(stderr);

        } else if ((cmd == "ss") || (cmd == "savestate")) {
          auto f = fopen_unique(args, "wb");
          emu.export_state(f.get());

        } else if ((cmd == "ls") || (cmd == "loadstate")) {
          auto f = fopen_unique(args, "rb");
          emu.import_state(f.get());
          this->print_state_header(emu);
          emu.print_state(stderr);

        } else if ((cmd == "st") || (cmd == "source-trace")) {
          auto tokens = split(args, ' ');
          size_t max_depth = 0;
          try {
            max_depth = stoull(tokens.at(1), nullptr, 0);
          } catch (const std::out_of_range& e) { }
          emu.print_source_trace(stderr, tokens.at(0), max_depth);

        } else if ((cmd == "s") || (cmd == "step")) {
          should_continue = true;

        } else if ((cmd == "c") || (cmd == "continue")) {
          this->state.mode = DebuggerMode::NONE;

        } else if ((cmd == "t") || (cmd == "trace")) {
          this->state.mode = DebuggerMode::TRACE;
          this->should_print_state_header = true;

        } else if ((cmd == "pt") || (cmd == "periodic-trace")) {
          this->state.mode = DebuggerMode::PERIODIC_TRACE;
          if (!args.empty()) {
            this->state.trace_period = stoull(args, nullptr, 16);
          }
          this->should_print_state_header = true;

        } else if ((cmd == "q") || (cmd == "quit")) {
          throw typename EmuT::terminate_emulation();

        } else {
          fprintf(stderr, "invalid command\n");
        }
      } catch (const typename EmuT::terminate_emulation&) {
        throw;
      } catch (const std::exception& e) {
        fprintf(stderr, "FAILED: %s\n", e.what());
      }
    }
  }
};
