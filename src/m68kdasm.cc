#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/JSON.hh>
#include <phosg/Process.hh>
#include <phosg/Random.hh>
#include <phosg/Strings.hh>
#include <phosg/Tools.hh>
#include <unordered_map>
#include <vector>

#include "Emulators/M68KEmulator.hh"
#include "Emulators/PPC32Emulator.hh"
#include "Emulators/X86Emulator.hh"
#include "ExecutableFormats/DOLFile.hh"
#include "ExecutableFormats/ELFFile.hh"
#include "ExecutableFormats/PEFFile.hh"
#include "ExecutableFormats/PEFile.hh"
#include "ExecutableFormats/RELFile.hh"

using namespace std;



template <typename ExecT>
void disassemble_executable(
    FILE* out_stream,
    const std::string& filename,
    const std::string& data,
    const multimap<uint32_t, string>* labels,
    bool print_hex_view_for_code) {
  ExecT f(filename.c_str(), data);
  f.print(out_stream, labels, print_hex_view_for_code);
}



void print_usage() {
  fputs("\
Usage: m68kdasm [options] [input_filename] [output_filename]\n\
\n\
If input_filename is not given or is '-', reads from stdin.\n\
If output_directory is not given or is '-', writes to stdout.\n\
\n\
Options:\n\
  --68k\n\
      Disassemble the input as raw 68K code (default). Note that some classic\n\
      Mac OS code resources (like CODE, dcmp, and DRVR) have headers before the\n\
      actual code; to disassemble an exported resource like this, use\n\
      resource_dasm with the --decode-single-resource option instead.\n\
  --ppc32\n\
      Disassemble the input as raw PowerPC code.\n\
  --x86\n\
      Disassemble the input as raw x86 code.\n\
  --pef\n\
      Disassemble the input as a PEF (Mac OS PowerPC executable).\n\
  --pe\n\
      Disassemble the input as a PE (Windows executable).\n\
  --elf\n\
      Disassemble the input as an ELF file.\n\
  --dol\n\
      Disassemble the input as a DOL (Nintendo GameCube executable).\n\
  --rel\n\
      Disassemble the input as a REL (Nintendo GameCube relocatable library).\n\
  --start-address=ADDR\n\
      When disassembling raw code with one of the above options, use ADDR as\n\
      the start address (instead of zero). No effect when disassembling an\n\
      executable file.\n\
  --label=ADDR[:NAME]\n\
      Add this label into the disassembly output. If NAME is not given, use\n\
      \"label<ADDR>\" as the label name. May be given multiple times.\n\
  --hex-view-for-code\n\
      Show all sections in hex views, even if they are also disassembled.\n\
  --parse-data\n\
      Treat the input data as a hexadecimal string instead of raw (binary)\n\
      machine code. This is useful when pasting data into a terminal from a hex\n\
      dump or editor.\n\
  --assemble-ppc32\n\
      Assemble the input text (from a file or from stdin) into PowerPC machine\n\
      code. Note that m68kdasm expects a nonstandard syntax for memory\n\
      references, which matches the syntax produced in code disassembled by\n\
      m68kdasm. The raw assembled code is written to stdout or to the output\n\
      file. If no output filename is given and stdout is a terminal, a\n\
      hex/ASCII view of the assembled code is written to the terminal instead\n\
      of raw binary.\n\
\n", stderr);
}

int main(int argc, char* argv[]) {
  signal(SIGPIPE, SIG_IGN);

  enum class Behavior {
    DISASSEMBLE_M68K,
    DISASSEMBLE_PPC,
    ASSEMBLE_PPC,
    DISASSEMBLE_X86,
    DISASSEMBLE_PEF,
    DISASSEMBLE_DOL,
    DISASSEMBLE_REL,
    DISASSEMBLE_PE,
    DISASSEMBLE_ELF,
    TEST_PPC_ASSEMBLER,
  };

  string in_filename;
  string out_filename;
  Behavior behavior = Behavior::DISASSEMBLE_M68K;
  bool parse_data = false;
  bool print_hex_view_for_code = false;
  bool verbose = false;
  uint32_t start_address = 0;
  uint64_t start_opcode = 0;
  size_t test_random_count = 0;
  size_t test_num_threads = 0;
  multimap<uint32_t, string> labels;
  for (int x = 1; x < argc; x++) {
    if (argv[x][0] == '-' && argv[x][1] != '\0') {
      if (!strcmp(argv[x], "--help")) {
        print_usage();
        return 0;
      } else if (!strcmp(argv[x], "--68k")) {
        behavior = Behavior::DISASSEMBLE_M68K;
      } else if (!strcmp(argv[x], "--ppc32")) {
        behavior = Behavior::DISASSEMBLE_PPC;
      } else if (!strcmp(argv[x], "--x86")) {
        behavior = Behavior::DISASSEMBLE_X86;
      } else if (!strcmp(argv[x], "--pef")) {
        behavior = Behavior::DISASSEMBLE_PEF;
      } else if (!strcmp(argv[x], "--dol")) {
        behavior = Behavior::DISASSEMBLE_DOL;
      } else if (!strcmp(argv[x], "--rel")) {
        behavior = Behavior::DISASSEMBLE_REL;
      } else if (!strcmp(argv[x], "--pe")) {
        behavior = Behavior::DISASSEMBLE_PE;
      } else if (!strcmp(argv[x], "--elf")) {
        behavior = Behavior::DISASSEMBLE_ELF;

      } else if (!strcmp(argv[x], "--assemble-ppc32")) {
        behavior = Behavior::ASSEMBLE_PPC;

      } else if (!strncmp(argv[x], "--test-assemble-ppc32", 21)) {
        behavior = Behavior::TEST_PPC_ASSEMBLER;
        if (argv[x][21] == '=') {
          start_opcode = strtoull(&argv[x][22], nullptr, 16);
        }
      } else if (!strncmp(argv[x], "--test-random-count=", 20)) {
        test_random_count = strtoull(&argv[x][20], nullptr, 0);
      } else if (!strncmp(argv[x], "--test-thread-count=", 20)) {
        test_num_threads = strtoull(&argv[x][20], nullptr, 0);
      } else if (!strcmp(argv[x], "--verbose")) {
        verbose = true;

      } else if (!strncmp(argv[x], "--start-address=", 16)) {
        start_address = strtoul(&argv[x][16], nullptr, 16);

      } else if (!strncmp(argv[x], "--label=", 8)) {
        string arg(&argv[x][8]);
        string addr_str, name_str;
        size_t colon_pos = arg.find(':');
        if (colon_pos == string::npos) {
          addr_str = arg;
        } else {
          addr_str = arg.substr(0, colon_pos);
          name_str = arg.substr(colon_pos + 1);
        }
        uint32_t addr = stoul(addr_str, nullptr, 16);
        if (name_str.empty()) {
          name_str = string_printf("label%08" PRIX32, addr);
        }
        labels.emplace(addr, name_str);

      } else if (!strcmp(argv[x], "--hex-view-for-code")) {
        print_hex_view_for_code = true;

      } else if (!strcmp(argv[x], "--parse-data")) {
        parse_data = true;

      } else {
        fprintf(stderr, "unknown option: %s\n", argv[x]);
        return 1;
      }
    } else {
      if (in_filename.empty()) {
        in_filename = argv[x];
      } else if (out_filename.empty()) {
        out_filename = argv[x];
      } else {
        print_usage();
        return 1;
      }
    }
  }

  if (behavior == Behavior::TEST_PPC_ASSEMBLER) {
    auto check_opcode = [&](uint64_t opcode, size_t) -> bool {
      string disassembly = PPC32Emulator::disassemble_one(0, opcode);
      if (starts_with(disassembly, ".invalid")) {
        if (verbose) {
          fprintf(stderr, "[%08" PRIX64 "] \"%s\" (skipping)\n",
              opcode, disassembly.c_str());
        }
        return false;
      }
      string assembled;
      try {
        assembled = PPC32Emulator::assemble(disassembly).code;
      } catch (const exception& e) {
        fprintf(stderr, "[%08" PRIX64 "] \"%s\" (assembly failed: %s)\n",
              opcode, disassembly.c_str(), e.what());
        return true;
      }
      if (assembled.size() != 4) {
        if (verbose) {
          fprintf(stderr, "[%08" PRIX64 "] \"%s\" (assembly produced incorrect data size)\n",
              opcode, disassembly.c_str());
          print_data(stderr, assembled);
        }
        return true;
      }
      StringReader r(assembled);
      uint32_t assembled_opcode = r.get_u32b();
      if (assembled_opcode != opcode) {
        if (verbose) {
          fprintf(stderr, "[%08" PRIX64 "] \"%s\" (assembly produced incorrect opcode %08" PRIX32 ")\n",
              opcode, disassembly.c_str(), assembled_opcode);
        }
        return true;
      }
      fprintf(stderr, "[%08" PRIX64 "] \"%s\" (correct)\n",
          opcode, disassembly.c_str());
      return false;
    };
    uint64_t failed_opcode;
    if (test_random_count) {
      vector<uint32_t> opcodes(test_random_count, 0);
      random_data(opcodes.data(), opcodes.size() * sizeof(opcodes[0]));
      auto check_opcode_from_vector = [&](uint64_t index, size_t thread_num) -> bool {
        return check_opcode(opcodes[index], thread_num);
      };
      failed_opcode = parallel_range<uint64_t, true>(
          check_opcode_from_vector, start_opcode, 0x100000000, test_num_threads);
      failed_opcode = (failed_opcode < opcodes.size()) ? opcodes[failed_opcode] : 0x100000000;
    } else {
      failed_opcode = parallel_range<uint64_t, true>(
          check_opcode, start_opcode, 0x100000000, test_num_threads);
    }
    if (failed_opcode < 0x100000000) {
      string disassembly = PPC32Emulator::disassemble_one(0, failed_opcode);
      fprintf(stderr, "Failed on %08" PRIX64 ": %s\n",
          failed_opcode, disassembly.c_str());
      auto assembled = PPC32Emulator::assemble(disassembly);
      print_data(stderr, assembled.code);
      if (assembled.code.size() == 4) {
        fprintf(stderr, "Failure: resulting data does not match original opcode\n");
      } else {
        fprintf(stderr, "Failure: resulting data size is not 4 bytes\n");
      }

      return 4;
    } else {
      fprintf(stderr, "All tests passed\n");
      return 0;
    }
  }

  string data;
  if (in_filename.empty() || in_filename == "-") {
    in_filename = "<stdin>";
    data = read_all(stdin);
  } else {
    data = load_file(in_filename);
  }

  if (parse_data) {
    data = parse_data_string(data);
  }

  FILE* out_stream;
  if (out_filename.empty() || out_filename == "-") {
    out_stream = stdout;
  } else {
    out_stream = fopen(out_filename.c_str(), "wb");
  }

  if (behavior == Behavior::ASSEMBLE_PPC) {
    auto res = PPC32Emulator::assemble(data);

    // If writing to stdout and it's a terminal, don't write raw binary
    if (out_stream == stdout && isatty(fileno(stdout))) {
      print_data(stdout, res.code);
    } else {
      fwritex(out_stream, res.code);
    }

  } else if (behavior == Behavior::DISASSEMBLE_PEF) {
    disassemble_executable<PEFFile>(
        out_stream, in_filename, data, &labels, print_hex_view_for_code);
  } else if (behavior == Behavior::DISASSEMBLE_DOL) {
    disassemble_executable<DOLFile>(
        out_stream, in_filename, data, &labels, print_hex_view_for_code);
  } else if (behavior == Behavior::DISASSEMBLE_REL) {
    disassemble_executable<RELFile>(
        out_stream, in_filename, data, &labels, print_hex_view_for_code);
  } else if (behavior == Behavior::DISASSEMBLE_PE) {
    disassemble_executable<PEFile>(
        out_stream, in_filename, data, &labels, print_hex_view_for_code);
  } else if (behavior == Behavior::DISASSEMBLE_ELF) {
    disassemble_executable<ELFFile>(
        out_stream, in_filename, data, &labels, print_hex_view_for_code);

  } else {
    string disassembly;
    if (behavior == Behavior::DISASSEMBLE_M68K) {
      disassembly = M68KEmulator::disassemble(
          data.data(), data.size(), start_address, &labels);
    } else if (behavior == Behavior::DISASSEMBLE_PPC) {
      disassembly = PPC32Emulator::disassemble(
          data.data(), data.size(), start_address, &labels);
    } else if (behavior == Behavior::DISASSEMBLE_X86) {
      disassembly = X86Emulator::disassemble(
          data.data(), data.size(), start_address, &labels);
    } else {
      throw logic_error("invalid behavior");
    }
    fwritex(out_stream, disassembly);
  }

  if (out_stream != stdout) {
    fclose(out_stream);
  } else {
    fflush(out_stream);
  }

  return 0;
}
