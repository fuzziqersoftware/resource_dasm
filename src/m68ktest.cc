#include <inttypes.h>
#include <string.h>

#include <filesystem>
#include <phosg/Arguments.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Random.hh>
#include <phosg/Strings.hh>
#include <phosg/Tools.hh>
#include <random>
#include <stdexcept>

#include "Emulators/M68KEmulator.hh"
#include "Emulators/PPC32Emulator.hh"
#include "Emulators/SH4Emulator.hh"
#include "Emulators/X86Emulator.hh"

bool run_68k_emulator_test(uint32_t start_opcode = 0, uint32_t end_opcode = 0x10000) {
  std::string mem_data;
  {
    mem_data.reserve(0x20060);
    std::mt19937 gen(0x11213380);
    while (mem_data.size() < 0x20060) {
      uint32_t v = gen();
      mem_data.push_back(v >> 24);
      mem_data.push_back(v >> 16);
      mem_data.push_back(v >> 8);
      mem_data.push_back(v);
    }
  }
  phosg::StringReader mem_data_r(mem_data);

  // Set up base memory
  uint32_t mem_base = 0x11213380;
  uint32_t code_base = 0xA0000000;
  auto mem = std::make_shared<ResourceDASM::MemoryContext>();
  mem->allocate_at(mem_base, mem_data.size());
  mem->memcpy(mem_base, mem_data.data(), mem_data.size());
  mem->allocate_at(code_base, 0x20);

  ResourceDASM::M68KEmulator::Regs base_regs;
  for (size_t z = 0; z < 8; z++) {
    base_regs.a[z] = mem_base + 0x10000 + (z * 4);
    base_regs.d[z].u = mem_base + 0x10040 + (z * 4);
    base_regs.pc = code_base;
    base_regs.sr.u = 0;
  }

  std::vector<phosg::be_uint16_t> opcode{start_opcode};
  ResourceDASM::M68KEmulator emu(mem);
  while (!opcode.empty()) {
    for (uint8_t ccr_set = 0; ccr_set < 2; ccr_set++) {
      mem->memcpy(code_base, opcode.data(), opcode.size() * 2);
      mem->memset(code_base + (opcode.size() * 2), 0, 0x20 - (opcode.size() * 2));

      auto disassembly = ResourceDASM::M68KEmulator::disassemble_one(opcode.data(), opcode.size() * 2);
      if (disassembly.contains(".incomplete")) {
        phosg::fwrite_fmt(stdout, "{} ...", disassembly);
        opcode.emplace_back(0x0000);
        continue;
      }

      auto& emu_regs = emu.registers();
      base_regs.sr.u = ccr_set ? 0x1F : 0x00;
      emu_regs = base_regs;

      emu.execute_one();

      phosg::fwrite_fmt(stdout, "{} =>", disassembly);
      for (size_t z = 0; z < 8; z++) {
        if (base_regs.a[z] != emu_regs.a[z]) {
          phosg::fwrite_fmt(stdout, " A{}:{:08X}->{:08X}", z, base_regs.a[z], emu_regs.a[z]);
        }
      }
      for (size_t z = 0; z < 8; z++) {
        if (base_regs.d[z].u != emu_regs.d[z].u) {
          phosg::fwrite_fmt(stdout, " D{}:{:08X}->{:08X}", z, base_regs.d[z].u, emu_regs.d[z].u);
        }
      }
      if (base_regs.sr.u != emu_regs.sr.u) {
        phosg::fwrite_fmt(stdout, " SR:{:04X}->{:04X}", base_regs.sr.u, emu_regs.sr.u);
      }
      for (size_t z = 0; z < mem_data.size(); z += 4) {
        uint32_t base_val = mem_data_r.pget_u32b(z);
        uint32_t mem_val = mem->read_u32b(mem_base + z);
        if (base_val != mem_val) {
          phosg::fwrite_fmt(stdout, " {:08X}:{:08X}->{:08X}", mem_base + z, base_val, mem_val);
          mem->write_u32b(mem_base + z, base_val);
        }
      }
      fputc('\n', stdout);
    }

    // Advance to the next opcode
    while (opcode.back() == 0xFFFF) {
      opcode.pop_back();
    }
    if (!opcode.empty()) {
      opcode.back()++;
    }
    if ((opcode.size() == 1) && (opcode[0] >= end_opcode)) {
      break;
    }
  }

  return true;
}

int main(int argc, char** argv) {
  phosg::Arguments args(argv + 1, argc - 1);

  if (args.get<bool>("test-expression")) {
    auto expr = ResourceDASM::Expression::Node::parse(args.get<std::string>(0));
    phosg::fwrite_fmt(stderr, "Expression: {}\n", expr->str());
    phosg::fwrite_fmt(stderr, "Result: {} ({})\n", expr->evaluate().str(), expr->evaluate().str(true));
    return 0;

  } else if (args.get<bool>("test-assemble-ppc32")) {
    return !ResourceDASM::PPC32Emulator::test_assembler(
        args.get<size_t>("threads", 0),
        args.get<uint32_t>("start-opcode", 0, phosg::Arguments::IntFormat::HEX),
        args.get<bool>("stop-on-failure"),
        args.get<bool>("verbose"));

  } else if (args.get<bool>("test-assemble-sh4")) {
    return !ResourceDASM::SH4Emulator::test_assembler(args.get<bool>("stop-on-failure"), args.get<bool>("verbose"));

  } else if (args.get<bool>("test-assemble-x86")) {
    return !ResourceDASM::X86Emulator::test_assembler(
        phosg::parse_data_string(args.get<std::string>("start-opcode", false)),
        args.get<bool>("stop-on-failure"),
        args.get<bool>("verbose"));

  } else if (args.get<bool>("test-exec-68k")) {
    return !run_68k_emulator_test(
        args.get<uint32_t>("start-opcode", 0, phosg::Arguments::IntFormat::HEX),
        args.get<uint32_t>("end-opcode", 0x10000, phosg::Arguments::IntFormat::HEX));
  }

  return 0;
}
