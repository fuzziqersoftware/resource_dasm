#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Process.hh>
#include <phosg/Random.hh>
#include <phosg/Strings.hh>
#include <phosg/Tools.hh>
#include <unordered_map>
#include <vector>

#include "Emulators/M68KEmulator.hh"
#include "Emulators/PPC32Emulator.hh"
#include "Emulators/SH4Emulator.hh"
#include "Emulators/X86Emulator.hh"
#include "ExecutableFormats/DOLFile.hh"
#include "ExecutableFormats/ELFFile.hh"
#include "ExecutableFormats/PEFFile.hh"
#include "ExecutableFormats/PEFile.hh"
#include "ExecutableFormats/RELFile.hh"
#include "ExecutableFormats/XBEFile.hh"

template <typename ExecT>
void disassemble_executable(
    FILE* out_stream,
    const std::string& filename,
    const std::string& data,
    const std::multimap<uint32_t, std::string>* labels,
    bool print_hex_view_for_code,
    bool all_sections_as_code) {
  ExecT f(filename.c_str(), data);
  f.print(out_stream, labels, print_hex_view_for_code, all_sections_as_code);
}

void print_usage() {
  fputs("\
Usage: m68kdasm [options] [input_filename] [output_filename]\n\
\n\
If input_filename is not given or is '-', reads from stdin.\n\
If output_filename is not given or is '-', writes to stdout.\n\
If no input type options are given, m68kdasm will figure out the executable\n\
type from the input data. If the input data is raw code, you must give one of\n\
the --68k, --ppc32, --x86, or --sh4 options.\n\
\n\
Type options:\n\
  --68k\n\
      Disassemble the input as raw 68K code. Note that some classic Mac OS code\n\
      resources (like CODE, dcmp, and DRVR) have headers before the actual\n\
      code; to disassemble an exported resource like this, use resource_dasm\n\
      with the --decode-single-resource option instead.\n\
  --ppc32\n\
      Disassemble the input as raw PowerPC code.\n\
  --x86\n\
      Disassemble the input as raw x86 code.\n\
  --sh4\n\
      Disassemble the input as raw SH-4 code.\n\
  --pef\n\
      Disassemble the input as a PEF (Mac OS PowerPC executable).\n\
  --pe\n\
      Disassemble the input as a PE (Windows executable / EXE).\n\
  --elf\n\
      Disassemble the input as an ELF file.\n\
  --dol\n\
      Disassemble the input as a DOL (Nintendo GameCube executable).\n\
  --rel\n\
      Disassemble the input as a REL (Nintendo GameCube relocatable library).\n\
  --xbe\n\
      Disassemble the input as an XBE (Microsoft Xbox executable).\n\
  --assemble-ppc32\n\
      Assemble the input text into PowerPC machine code. Note that m68kdasm\n\
      expects a nonstandard syntax for memory references, which matches the\n\
      syntax that it produces when disassembling PowerPC code. If no output\n\
      filename is given and stdout is a terminal, a hex/ASCII view of the\n\
      assembled code is written to the terminal instead of raw binary. If\n\
      --ppc32 is also given, the input text is assembled, then disassembled\n\
      immediately. This can be useful for making Action Replay codes.\n\
  --assemble-x86\n\
      Assemble the input text (from a file or from stdin) into x86 machine\n\
      code. As with the other assembly options, --x86 may also be given.\n\
  --assemble-sh4\n\
      Assemble the input text (from a file or from stdin) into SH-4 machine\n\
      code. As with the other assembly options, --sh4 may also be given. Note\n\
      that m68kdasm\'s SH-4 syntax is nonstandard as well, like its PPC syntax.\n\
\n\
Disassembly options:\n\
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
      machine code. This is enabled by default if stdin is a terminal, unless\n\
      one of the --assemble-X options is used.\n\
  --raw-data\n\
      Treat the input data as a hexadecimal string instead of raw (binary)\n\
      machine code. This is the opposite of --parse-data.\n\
  --data=HEX\n\
      Disassemble the given data instead of reading from stdin or a file.\n\
\n\
Assembly options:\n\
  --include-directory=DIRECTORY\n\
      Enable the .include directive in the assembler, and search this directory\n\
      for included files. This option may be given multiple times, and the\n\
      directories are searched in the order they are specified. Include files\n\
      should end in the extension .inc.s (for code) or .inc.bin (for data).\n\
      Labels in the included files are not copied into the calling file, so\n\
      including the same file multiple times does not cause problems.\n\
\n",
      stderr);
}

enum class ParseDataBehavior {
  UNSPECIFIED = 0,
  PARSE_DATA,
  RAW_DATA,
};

int main(int argc, char** argv) {
  enum class Behavior {
    DISASSEMBLE_M68K,
    DISASSEMBLE_PPC,
    ASSEMBLE_PPC,
    ASSEMBLE_AND_DISASSEMBLE_PPC,
    DISASSEMBLE_X86,
    ASSEMBLE_X86,
    ASSEMBLE_AND_DISASSEMBLE_X86,
    DISASSEMBLE_SH4,
    ASSEMBLE_SH4,
    ASSEMBLE_AND_DISASSEMBLE_SH4,
    DISASSEMBLE_UNSPECIFIED_EXECUTABLE,
    DISASSEMBLE_PEF,
    DISASSEMBLE_DOL,
    DISASSEMBLE_REL,
    DISASSEMBLE_PE,
    DISASSEMBLE_ELF,
    DISASSEMBLE_XBE,
    TEST_EXPRESSION,
    TEST_PPC_ASSEMBLER,
    TEST_SH4_ASSEMBLER,
    TEST_X86_ASSEMBLER,
  };

  std::string in_filename;
  std::string out_filename;
  Behavior behavior = Behavior::DISASSEMBLE_UNSPECIFIED_EXECUTABLE;
  ParseDataBehavior parse_data_behavior = ParseDataBehavior::UNSPECIFIED;
  bool in_filename_is_data = false;
  bool print_hex_view_for_code = false;
  bool all_sections_as_code = false;
  bool verbose = false;
  uint32_t start_address = 0;
  uint64_t start_opcode = 0;
  std::string start_opcode_str;
  size_t test_num_threads = 0;
  bool test_stop_on_failure = false;
  std::string test_expr_str;
  std::multimap<uint32_t, std::string> labels;
  std::vector<std::string> include_directories;
  for (int x = 1; x < argc; x++) {
    if (argv[x][0] == '-' && argv[x][1] != '\0') {
      if (!strcmp(argv[x], "--help")) {
        print_usage();
        return 0;
      } else if (!strcmp(argv[x], "--68k")) {
        behavior = Behavior::DISASSEMBLE_M68K;
      } else if (!strcmp(argv[x], "--ppc32")) {
        if (behavior == Behavior::ASSEMBLE_PPC) {
          behavior = Behavior::ASSEMBLE_AND_DISASSEMBLE_PPC;
        } else {
          behavior = Behavior::DISASSEMBLE_PPC;
        }
      } else if (!strcmp(argv[x], "--sh4")) {
        if (behavior == Behavior::ASSEMBLE_SH4) {
          behavior = Behavior::ASSEMBLE_AND_DISASSEMBLE_SH4;
        } else {
          behavior = Behavior::DISASSEMBLE_SH4;
        }
      } else if (!strcmp(argv[x], "--x86")) {
        if (behavior == Behavior::ASSEMBLE_X86) {
          behavior = Behavior::ASSEMBLE_AND_DISASSEMBLE_X86;
        } else {
          behavior = Behavior::DISASSEMBLE_X86;
        }
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
      } else if (!strcmp(argv[x], "--xbe")) {
        behavior = Behavior::DISASSEMBLE_XBE;

      } else if (!strcmp(argv[x], "--assemble-ppc32")) {
        parse_data_behavior = ParseDataBehavior::RAW_DATA;
        if (behavior == Behavior::DISASSEMBLE_PPC) {
          behavior = Behavior::ASSEMBLE_AND_DISASSEMBLE_PPC;
        } else {
          behavior = Behavior::ASSEMBLE_PPC;
        }
      } else if (!strcmp(argv[x], "--assemble-sh4")) {
        parse_data_behavior = ParseDataBehavior::RAW_DATA;
        if (behavior == Behavior::DISASSEMBLE_SH4) {
          behavior = Behavior::ASSEMBLE_AND_DISASSEMBLE_SH4;
        } else {
          behavior = Behavior::ASSEMBLE_SH4;
        }
      } else if (!strcmp(argv[x], "--assemble-x86")) {
        parse_data_behavior = ParseDataBehavior::RAW_DATA;
        if (behavior == Behavior::DISASSEMBLE_X86) {
          behavior = Behavior::ASSEMBLE_AND_DISASSEMBLE_X86;
        } else {
          behavior = Behavior::ASSEMBLE_X86;
        }
      } else if (!strncmp(argv[x], "--include-directory=", 20)) {
        include_directories.emplace_back(&argv[x][20]);

      } else if (!strncmp(argv[x], "--test-expression=", 18)) {
        behavior = Behavior::TEST_EXPRESSION;
        test_expr_str = &argv[x][18];
      } else if (!strncmp(argv[x], "--test-assemble-ppc32", 21)) {
        behavior = Behavior::TEST_PPC_ASSEMBLER;
        if (argv[x][21] == '=') {
          start_opcode = strtoull(&argv[x][22], nullptr, 16);
        }
      } else if (!strncmp(argv[x], "--test-assemble-sh4", 19)) {
        behavior = Behavior::TEST_SH4_ASSEMBLER;
      } else if (!strncmp(argv[x], "--test-assemble-x86", 19)) {
        behavior = Behavior::TEST_X86_ASSEMBLER;
        if (argv[x][19] == '=') {
          start_opcode_str = phosg::parse_data_string(&argv[x][20]);
        }
      } else if (!strncmp(argv[x], "--test-thread-count=", 20)) {
        test_num_threads = strtoull(&argv[x][20], nullptr, 0);
      } else if (!strcmp(argv[x], "--test-stop-on-failure")) {
        test_stop_on_failure = true;
      } else if (!strcmp(argv[x], "--verbose")) {
        verbose = true;

      } else if (!strncmp(argv[x], "--start-address=", 16)) {
        start_address = strtoul(&argv[x][16], nullptr, 16);

      } else if (!strncmp(argv[x], "--label=", 8)) {
        std::string arg(&argv[x][8]);
        std::string addr_str, name_str;
        size_t colon_pos = arg.find(':');
        if (colon_pos == std::string::npos) {
          addr_str = arg;
        } else {
          addr_str = arg.substr(0, colon_pos);
          name_str = arg.substr(colon_pos + 1);
        }
        uint32_t addr = stoul(addr_str, nullptr, 16);
        if (name_str.empty()) {
          name_str = std::format("label{:08X}", addr);
        }
        labels.emplace(addr, name_str);

      } else if (!strcmp(argv[x], "--hex-view-for-code")) {
        print_hex_view_for_code = true;
      } else if (!strcmp(argv[x], "--all-sections-as-code")) {
        all_sections_as_code = true;

      } else if (!strcmp(argv[x], "--parse-data")) {
        parse_data_behavior = ParseDataBehavior::PARSE_DATA;
      } else if (!strcmp(argv[x], "--raw-data")) {
        parse_data_behavior = ParseDataBehavior::RAW_DATA;

      } else if (!strncmp(argv[x], "--data=", 7)) {
        in_filename = &argv[x][7];
        in_filename_is_data = true;
        parse_data_behavior = ParseDataBehavior::PARSE_DATA;

      } else {
        phosg::fwrite_fmt(stderr, "unknown option: {}\n", argv[x]);
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

  if (behavior == Behavior::TEST_EXPRESSION) {
    auto expr = ResourceDASM::Expression::Node::parse(test_expr_str);
    phosg::fwrite_fmt(stderr, "Expression: {}\n", expr->str());
    phosg::fwrite_fmt(stderr, "Result: {} ({})\n", expr->evaluate().str(), expr->evaluate().str(true));
    return 0;
  } else if (behavior == Behavior::TEST_PPC_ASSEMBLER) {
    return ResourceDASM::PPC32Emulator::test_assembler(test_num_threads, start_opcode, test_stop_on_failure, verbose) ? 0 : 4;
  } else if (behavior == Behavior::TEST_SH4_ASSEMBLER) {
    return ResourceDASM::SH4Emulator::test_assembler(test_stop_on_failure, verbose) ? 0 : 4;
  } else if (behavior == Behavior::TEST_X86_ASSEMBLER) {
    return ResourceDASM::X86Emulator::test_assembler(start_opcode_str, test_stop_on_failure, verbose) ? 0 : 4;
  }

  std::string data;
  if (in_filename_is_data) {
    data = in_filename;
    parse_data_behavior = ParseDataBehavior::PARSE_DATA;
  } else if (in_filename.empty() || in_filename == "-") {
    in_filename = "<stdin>";
    data = phosg::read_all(stdin);
    if (parse_data_behavior == ParseDataBehavior::UNSPECIFIED) {
      parse_data_behavior = isatty(fileno(stdin)) ? ParseDataBehavior::PARSE_DATA : ParseDataBehavior::RAW_DATA;
    }
  } else {
    data = phosg::load_file(in_filename);
    if (parse_data_behavior == ParseDataBehavior::UNSPECIFIED) {
      parse_data_behavior = ParseDataBehavior::RAW_DATA;
    }
  }

  if (parse_data_behavior == ParseDataBehavior::PARSE_DATA) {
    data = phosg::parse_data_string(data);
  }

  FILE* out_stream;
  if (out_filename.empty() || out_filename == "-") {
    out_stream = stdout;
  } else {
    out_stream = fopen(out_filename.c_str(), "wb");
  }

  if ((behavior == Behavior::ASSEMBLE_PPC) ||
      (behavior == Behavior::ASSEMBLE_AND_DISASSEMBLE_PPC)) {
    auto res = ResourceDASM::PPC32Emulator::assemble(data, include_directories, start_address);

    if (behavior == Behavior::ASSEMBLE_AND_DISASSEMBLE_PPC) {
      std::multimap<uint32_t, std::string> dasm_labels;
      for (const auto& it : res.label_offsets) {
        dasm_labels.emplace(it.second + start_address, it.first);
      }
      for (const auto& it : labels) {
        dasm_labels.emplace(it.first, it.second);
      }
      auto disassembly = ResourceDASM::PPC32Emulator::disassemble(
          res.code.data(), res.code.size(), start_address, &dasm_labels);
      phosg::fwritex(out_stream, disassembly);
    } else {
      // If writing to stdout and it's a terminal, don't write raw binary
      if (out_stream == stdout && isatty(fileno(stdout))) {
        phosg::print_data(stdout, res.code);
      } else {
        phosg::fwritex(out_stream, res.code);
      }
    }

  } else if ((behavior == Behavior::ASSEMBLE_X86) || (behavior == Behavior::ASSEMBLE_AND_DISASSEMBLE_X86)) {
    auto res = ResourceDASM::X86Emulator::assemble(data, include_directories, start_address);

    if (behavior == Behavior::ASSEMBLE_AND_DISASSEMBLE_X86) {
      std::multimap<uint32_t, std::string> dasm_labels;
      for (const auto& it : res.label_offsets) {
        dasm_labels.emplace(it.second + start_address, it.first);
      }
      for (const auto& it : labels) {
        dasm_labels.emplace(it.first, it.second);
      }
      auto disassembly = ResourceDASM::X86Emulator::disassemble(
          res.code.data(), res.code.size(), start_address, &dasm_labels);
      phosg::fwritex(out_stream, disassembly);
    } else {
      // If writing to stdout and it's a terminal, don't write raw binary
      if (out_stream == stdout && isatty(fileno(stdout))) {
        phosg::print_data(stdout, res.code);
      } else {
        phosg::fwritex(out_stream, res.code);
      }
    }

  } else if ((behavior == Behavior::ASSEMBLE_SH4) || (behavior == Behavior::ASSEMBLE_AND_DISASSEMBLE_SH4)) {
    auto res = ResourceDASM::SH4Emulator::assemble(data, include_directories, start_address);

    if (behavior == Behavior::ASSEMBLE_AND_DISASSEMBLE_SH4) {
      std::multimap<uint32_t, std::string> dasm_labels;
      for (const auto& it : res.label_offsets) {
        dasm_labels.emplace(it.second + start_address, it.first);
      }
      for (const auto& it : labels) {
        dasm_labels.emplace(it.first, it.second);
      }
      auto disassembly = ResourceDASM::SH4Emulator::disassemble(
          res.code.data(), res.code.size(), start_address, &dasm_labels);
      phosg::fwritex(out_stream, disassembly);
    } else {
      // If writing to stdout and it's a terminal, don't write raw binary
      if (out_stream == stdout && isatty(fileno(stdout))) {
        phosg::print_data(stdout, res.code);
      } else {
        phosg::fwritex(out_stream, res.code);
      }
    }

  } else if (behavior == Behavior::DISASSEMBLE_UNSPECIFIED_EXECUTABLE) {
    using DasmFnT = void (*)(FILE*, const std::string&, const std::string&, const std::multimap<uint32_t, std::string>*, bool, bool);
    static const std::vector<std::pair<const char*, DasmFnT>> fns({
        {"Preferred Executable Format (PEF)", disassemble_executable<ResourceDASM::PEFFile>},
        {"Portable Executable (PE)", disassemble_executable<ResourceDASM::PEFile>},
        {"Executable and Linkable Format (ELF)", disassemble_executable<ResourceDASM::ELFFile>},
        {"Nintendo GameCube executable (DOL)", disassemble_executable<ResourceDASM::DOLFile>},
        {"Nintendo GameCube library (REL)", disassemble_executable<ResourceDASM::RELFile>},
        {"Microsoft Xbox executable (XBE)", disassemble_executable<ResourceDASM::XBEFile>},
    });
    std::vector<const char*> succeeded_format_names;
    for (const auto& it : fns) {
      const char* name = it.first;
      auto fn = it.second;
      try {
        fn(out_stream, in_filename, data, &labels, print_hex_view_for_code, all_sections_as_code);
        succeeded_format_names.emplace_back(name);
      } catch (const std::exception& e) {
      }
    }
    if (succeeded_format_names.empty()) {
      throw std::runtime_error("input is not in a recognized format");
    } else if (succeeded_format_names.size() > 1) {
      phosg::fwrite_fmt(stderr, "Warning: multiple disassemblers succeeded; the output will contain multiple representations of the input\n");
      for (size_t z = 0; z < succeeded_format_names.size(); z++) {
        phosg::fwrite_fmt(stderr, "  ({}) {}\n", z + 1, succeeded_format_names[z]);
      }
    }

  } else if (behavior == Behavior::DISASSEMBLE_PEF) {
    disassemble_executable<ResourceDASM::PEFFile>(
        out_stream, in_filename, data, &labels, print_hex_view_for_code, all_sections_as_code);
  } else if (behavior == Behavior::DISASSEMBLE_DOL) {
    disassemble_executable<ResourceDASM::DOLFile>(
        out_stream, in_filename, data, &labels, print_hex_view_for_code, all_sections_as_code);
  } else if (behavior == Behavior::DISASSEMBLE_REL) {
    disassemble_executable<ResourceDASM::RELFile>(
        out_stream, in_filename, data, &labels, print_hex_view_for_code, all_sections_as_code);
  } else if (behavior == Behavior::DISASSEMBLE_PE) {
    disassemble_executable<ResourceDASM::PEFile>(
        out_stream, in_filename, data, &labels, print_hex_view_for_code, all_sections_as_code);
  } else if (behavior == Behavior::DISASSEMBLE_ELF) {
    disassemble_executable<ResourceDASM::ELFFile>(
        out_stream, in_filename, data, &labels, print_hex_view_for_code, all_sections_as_code);
  } else if (behavior == Behavior::DISASSEMBLE_XBE) {
    disassemble_executable<ResourceDASM::XBEFile>(
        out_stream, in_filename, data, &labels, print_hex_view_for_code, all_sections_as_code);

  } else {
    std::string disassembly;
    if (behavior == Behavior::DISASSEMBLE_M68K) {
      disassembly = ResourceDASM::M68KEmulator::disassemble(data.data(), data.size(), start_address, &labels);
    } else if (behavior == Behavior::DISASSEMBLE_PPC) {
      disassembly = ResourceDASM::PPC32Emulator::disassemble(data.data(), data.size(), start_address, &labels);
    } else if (behavior == Behavior::DISASSEMBLE_X86) {
      disassembly = ResourceDASM::X86Emulator::disassemble(data.data(), data.size(), start_address, &labels);
    } else if (behavior == Behavior::DISASSEMBLE_SH4) {
      disassembly = ResourceDASM::SH4Emulator::disassemble(data.data(), data.size(), start_address, &labels);
    } else {
      throw std::logic_error("invalid behavior");
    }
    phosg::fwritex(out_stream, disassembly);
  }

  if (out_stream != stdout) {
    fclose(out_stream);
  } else {
    fflush(out_stream);
  }

  return 0;
}

// TODO; // Run PPC and SH4 comparison tests on stashed and unstashed repo
// TODO; // Write x86 comparison test maybe? (WOuld have to deal with variable width obviously)
