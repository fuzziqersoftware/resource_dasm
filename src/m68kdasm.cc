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
#include <phosg/Strings.hh>
#include <unordered_map>
#include <vector>

#include "Emulators/M68KEmulator.hh"
#include "Emulators/PPC32Emulator.hh"
#include "Emulators/X86Emulator.hh"
#include "ExecutableFormats/DOLFile.hh"
#include "ExecutableFormats/ELFFile.hh"
#include "ExecutableFormats/PEFFFile.hh"
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
  --peff\n\
      Disassemble the input as a PEFF (Mac OS PowerPC executable).\n\
  --pe\n\
      Disassemble the input as a PE (Windows executable).\n\
  --elf\n\
      Disassemble the input as an ELF file.\n\
  --dol\n\
      Disassemble the input as a DOL (Nintendo GameCube executable).\n\
  --rel (Nintendo GameCube relocatable library)\n\
      Disassemble the input as a REL (Nintendo GameCube relocatable library).\n\
  --start-address=ADDR\n\
      When disassembling code with one of the above options, use ADDR as the\n\
      start address (instead of zero).\n\
  --label=ADDR[:NAME]\n\
      Add this label into the disassembly output. If NAME is not given, use\n\
      \"label<ADDR>\" as the label name. May be given multiple times.\n\
  --hex-view-for-code\n\
      Show all sections in hex view, even if they are also disassembled.\n\
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
    DISASSEMBLE_PEFF,
    DISASSEMBLE_DOL,
    DISASSEMBLE_REL,
    DISASSEMBLE_PE,
    DISASSEMBLE_ELF,
  };

  string in_filename;
  string out_filename;
  Behavior behavior = Behavior::DISASSEMBLE_M68K;
  bool parse_data = false;
  bool print_hex_view_for_code = false;
  uint32_t start_address = 0;
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
      } else if (!strcmp(argv[x], "--peff")) {
        behavior = Behavior::DISASSEMBLE_PEFF;
      } else if (!strcmp(argv[x], "--dol")) {
        behavior = Behavior::DISASSEMBLE_DOL;
      } else if (!strcmp(argv[x], "--rel")) {
        behavior = Behavior::DISASSEMBLE_REL;
      } else if (!strcmp(argv[x], "--pe")) {
        behavior = Behavior::DISASSEMBLE_PE;
      } else if (!strcmp(argv[x], "--elf")) {
        behavior = Behavior::DISASSEMBLE_ELF;

      } else if (!strcmp(argv[x], "--assemble-ppc")) {
        behavior = Behavior::ASSEMBLE_PPC;

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

  } else if (behavior == Behavior::DISASSEMBLE_PEFF) {
    disassemble_executable<PEFFFile>(
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
