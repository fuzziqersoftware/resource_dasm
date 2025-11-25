#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <deque>
#include <filesystem>
#include <forward_list>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <set>
#include <string>

#include "SH4Emulator.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {

SH4Emulator::Regs::Regs() {
  for (size_t z = 0; z < 16; z++) {
    this->r[z].u = 0;
    this->d[z] = 0.0;
  }
  this->sr = 0;
  this->ssr = 0;
  this->gbr = 0;
  this->mac = 0;
  this->pr = 0;
  this->pc = 0;
  this->spc = 0;
  this->sgr = 0;
  this->vbr = 0;
  this->fpul_i = 0;
  this->fpscr = 0;
  this->dbr = 0;
  this->pending_branch_type = PendingBranchType::NONE;
  this->pending_branch_target = 0;
  this->instructions_until_branch = 0;
}

void SH4Emulator::Regs::set_by_name(const std::string& name, uint32_t value) {
  if ((name == "sr") || (name == "SR")) {
    this->sr = value;
  } else if ((name == "ssr") || (name == "SSR")) {
    this->ssr = value;
  } else if ((name == "gbr") || (name == "GBR")) {
    this->gbr = value;
  } else if ((name == "mach") || (name == "MACH")) {
    this->mac = (this->mac & 0x00000000FFFFFFFF) | (static_cast<uint64_t>(value) << 32);
  } else if ((name == "macl") || (name == "MACL")) {
    this->mac = (this->mac & 0xFFFFFFFF00000000) | static_cast<uint64_t>(value);
  } else if ((name == "pr") || (name == "PR")) {
    this->pr = value;
  } else if ((name == "pc") || (name == "PC")) {
    this->pc = value;
  } else if ((name == "spc") || (name == "SPC")) {
    this->spc = value;
  } else if ((name == "sgr") || (name == "SGR")) {
    this->sgr = value;
  } else if ((name == "vbr") || (name == "VBR")) {
    this->vbr = value;
  } else if ((name == "fpscr") || (name == "FPSCR")) {
    this->fpscr = value;
  } else if ((name == "dbr") || (name == "DBR")) {
    this->dbr = value;
  } else if ((name == "fpul") || (name == "FPUL")) {
    this->fpul_i = value;
  } else if (name.starts_with("r") || name.starts_with("R")) {
    size_t reg_num = stoul(name.substr(1), nullptr, 10);
    if (reg_num >= 16) {
      throw invalid_argument("invalid register number");
    }
    this->r[reg_num].u = value;
  } else if (name.starts_with("f") || name.starts_with("F")) {
    size_t reg_num = stoul(name.substr(1), nullptr, 10);
    if (reg_num >= 32) {
      throw invalid_argument("invalid register number");
    }
    *reinterpret_cast<le_uint32_t*>(&this->f[reg_num]) = value;
  } else {
    throw invalid_argument("invalid register name");
  }
}

void SH4Emulator::Regs::assert_no_branch_pending() const {
  if (this->pending_branch_type != PendingBranchType::NONE) {
    throw std::runtime_error("invalid instruction in delay slot");
  }
}

void SH4Emulator::Regs::enqueue_branch(PendingBranchType type, uint32_t target, size_t instructions_until_branch) {
  this->assert_no_branch_pending();
  this->pending_branch_type = type;
  this->pending_branch_target = target;
  this->instructions_until_branch = instructions_until_branch;
}

template <typename T>
void check_range_t(T value, T min, T max) {
  if (value < min) {
    throw invalid_argument("value before beginning of range");
  }
  if (value > max) {
    throw invalid_argument("value beyond end of range");
  }
}

static constexpr uint8_t op_get_op(uint16_t op) {
  return (op >> 12) & 0x0F;
}
static constexpr uint8_t op_get_r1(uint16_t op) {
  return (op >> 8) & 0x0F;
}
static constexpr uint8_t op_get_r2(uint16_t op) {
  return (op >> 4) & 0x0F;
}
static constexpr uint8_t op_get_r3(uint16_t op) {
  return op & 0x0F;
}

static constexpr uint32_t op_get_uimm4(uint16_t op) {
  return op & 0x000F;
}
static constexpr uint32_t op_get_uimm8(uint16_t op) {
  return op & 0xFF;
}
static constexpr int32_t op_get_simm8(uint16_t op) {
  int32_t ret = op & 0x00FF;
  return (ret & 0x80) ? (ret | 0xFFFFFF00) : ret;
}
static constexpr int32_t op_get_simm12(uint16_t op) {
  int32_t ret = op & 0x0FFF;
  return (ret & 0x800) ? (ret | 0xFFFFF000) : ret;
}

static bool is_reg_name(const string& s) {
  if (s.size() < 2) {
    return false;
  }
  if (s[0] != 'r') {
    return false;
  }
  if (s[1] == '1') {
    return (s.size() == 2) || ((s.size() == 3) && (s[2] >= '0') && (s[2] <= '5'));
  } else if ((s[1] == '0') || ((s[1] > '1') && (s[1] <= '9'))) {
    return s.size() == 2;
  }
  return false;
}

SH4Emulator::SH4Emulator(shared_ptr<MemoryContext> mem) : EmulatorBase(mem) {}

void SH4Emulator::import_state(FILE* stream) {
  uint8_t version = freadx<uint8_t>(stream);
  if (version != 0) {
    throw runtime_error("unknown format version");
  }
  this->regs = freadx<Regs>(stream);
  this->mem->import_state(stream);
}

void SH4Emulator::export_state(FILE* stream) const {
  fwritex<uint8_t>(stream, 0); // version
  fwritex(stream, &this->regs, sizeof(this->regs));
  this->mem->export_state(stream);
}

void SH4Emulator::print_state_header(FILE* stream) const {
  fwrite_fmt(stream, "\
---R0--- ---R1--- ---R2--- ---R3--- ---R4--- ---R5--- ---R6--- ---R7--- \
---R8--- ---R9--- ---R10-- ---R11-- ---R12-- ---R13-- ---R14-- -R15-SP- \
T ---GBR-- -------MAC------ ---PR--- ---PC--- BT = INSTRUCTION\n");
}

void SH4Emulator::print_state(FILE* stream) const {
  string disassembly;
  uint16_t opcode = 0;
  try {
    this->assert_aligned(this->regs.pc, 2);
    opcode = this->mem->read_u16l(this->regs.pc);
    disassembly = this->disassemble_one(this->regs.pc, opcode, false, this->mem);
  } catch (const exception& e) {
    disassembly = std::format(" (failed: {})", e.what());
  }

  char branch_type_ch = '?';
  switch (this->regs.pending_branch_type) {
    case Regs::PendingBranchType::NONE:
      branch_type_ch = '-';
      break;
    case Regs::PendingBranchType::BRANCH:
      branch_type_ch = 'b';
      break;
    case Regs::PendingBranchType::CALL:
      branch_type_ch = 'c';
      break;
    case Regs::PendingBranchType::RETURN:
      branch_type_ch = 'r';
      break;
  }

  fwrite_fmt(stream, "{:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {} {:08X} {:016X} {:08X} {:08X} {}{} = {:04X} {}\n",
      this->regs.r[0].u, this->regs.r[1].u, this->regs.r[2].u, this->regs.r[3].u,
      this->regs.r[4].u, this->regs.r[5].u, this->regs.r[6].u, this->regs.r[7].u,
      this->regs.r[8].u, this->regs.r[9].u, this->regs.r[10].u, this->regs.r[11].u,
      this->regs.r[12].u, this->regs.r[13].u, this->regs.r[14].u, this->regs.r[15].u,
      this->regs.t() ? '1' : '0', this->regs.gbr, this->regs.mac, this->regs.pr, this->regs.pc,
      this->regs.instructions_until_branch, branch_type_ch, opcode, disassembly);
}

void SH4Emulator::execute_one_0(uint16_t op) {
  switch (op_get_r3(op)) {
    case 0x2:
      if (op_get_r2(op) & 0x8) { // 0000nnnn1mmm0010 stc    rn, rmb
        // TODO
        throw runtime_error("banked registers are not supported");
      }
      switch (op_get_r2(op)) {
        case 0x0: // 0000nnnn00000010 stc    rn, sr
          this->regs.r[op_get_r1(op)].u = this->regs.sr;
          break;
        case 0x1: // 0000nnnn00010010 stc    rn, gbr
          this->regs.r[op_get_r1(op)].u = this->regs.gbr;
          break;
        case 0x2: // 0000nnnn00100010 stc    rn, vbr
          this->regs.r[op_get_r1(op)].u = this->regs.vbr;
          break;
        case 0x3: // 0000nnnn00110010 stc    rn, ssr
          this->regs.r[op_get_r1(op)].u = this->regs.ssr;
          break;
        case 0x4: // 0000nnnn01000010 stc    rn, spc
          this->regs.r[op_get_r1(op)].u = this->regs.spc;
          break;
        default:
          throw runtime_error("invalid opcode");
      }
      break;
    case 0x3:
      switch (op_get_r2(op)) {
        case 0x0: // 0000nnnn00000011 calls  (pc + 4 + rn)
        case 0x2: // 0000nnnn00100011 bs     (pc + 4 + rn)
          this->regs.enqueue_branch(
              (op & 0x0020) ? Regs::PendingBranchType::CALL : Regs::PendingBranchType::BRANCH,
              this->regs.pc + this->regs.r[op_get_r1(op)].u + 4,
              1);
          break;
        case 0x8: // 0000nnnn10000011 pref   [rn]  # prefetch
        case 0x9: // 0000nnnn10010011 ocbi   [rn]  # dcbi
        case 0xA: // 0000nnnn10100011 ocbp   [rn]  # dcbf
        case 0xB: // 0000nnnn10110011 ocbwb  [rn]  # dcbst?
          // We don't emulate any caches, so just check that the address is valid
          if (!this->mem->exists(this->regs.r[op_get_r1(op)].u)) {
            throw runtime_error("invalid memory access");
          }
          break;
        case 0xC: // 0000nnnn11000011 movca.l [rn], r0
          // TODO
          throw runtime_error("unimplemented movca.l opcode");
      }
      break;

    case 0x4: // 0000nnnnmmmm0100 mov.b  [r0 + rn], rm
      this->mem->write_u8(this->regs.r[0].u + this->regs.r[op_get_r1(op)].u, this->regs.r[op_get_r2(op)].u);
      break;
    case 0x5: { // 0000nnnnmmmm0101 mov.w  [r0 + rn], rm
      uint32_t addr = this->regs.r[0].u + this->regs.r[op_get_r1(op)].u;
      this->assert_aligned(addr, 2);
      this->mem->write_u16l(addr, this->regs.r[op_get_r2(op)].u);
      break;
    }
    case 0x6: { // 0000nnnnmmmm0110 mov.l  [r0 + rn], rm
      uint32_t addr = this->regs.r[0].u + this->regs.r[op_get_r1(op)].u;
      this->assert_aligned(addr, 4);
      this->mem->write_u32l(addr, this->regs.r[op_get_r2(op)].u);
      break;
    }

    case 0x8:
      if (op_get_r1(op) != 0) {
        throw runtime_error("invalid opcode");
      }
      switch (op_get_r2(op)) {
        case 0x0: // 0000000000001000 clrt
          this->regs.replace_t(false);
          break;
        case 0x1: // 0000000000011000 sett
          this->regs.replace_t(true);
          break;
        case 0x2: // 0000000000101000 clrmac
          this->regs.mac = 0;
          break;
        case 0x3: // 0000000000111000 ldtlb
          // TODO
          throw runtime_error("TLB is not implemented");
        case 0x4: // 0000000001001000 clrs
          this->regs.replace_s(false);
          break;
        case 0x5: // 0000000001011000 sets
          this->regs.replace_s(true);
          break;
        default:
          throw runtime_error("invalid opcode");
      }
      break;

    case 0x9:
      switch (op_get_r2(op)) {
        case 0x0: // 0000000000001001 nop
          if (op_get_r1(op) != 0) {
            throw runtime_error("invalid opcode");
          }
          break;
        case 0x1: // 0000000000011001 div0u
          if (op_get_r1(op) != 0) {
            throw runtime_error("invalid opcode");
          }
          this->regs.replace_mqt(false, false, false);
          break;
        case 0x2: // 0000nnnn00101001 movt   rn, t
          this->regs.r[op_get_r1(op)].u = this->regs.t();
          break;
        default:
          throw runtime_error("invalid opcode");
      }
      break;

    case 0xA:
      switch (op_get_r2(op)) {
        case 0x0: // 0000nnnn00001010 sts    rn, mach
          this->regs.r[op_get_r1(op)].u = this->regs.mac >> 32;
          break;
        case 0x1: // 0000nnnn00011010 sts    rn, macl
          this->regs.r[op_get_r1(op)].u = this->regs.mac;
          break;
        case 0x2: // 0000nnnn00101010 sts    rn, pr
          this->regs.r[op_get_r1(op)].u = this->regs.pr;
          break;
        case 0x3: // 0000nnnn00111010 stc    rn, sgr
          this->regs.r[op_get_r1(op)].u = this->regs.sgr;
          break;
        case 0x5: // 0000nnnn01011010 sts    rn, fpul
          this->regs.r[op_get_r1(op)].u = this->regs.fpul_i;
          break;
        case 0x6: // 0000nnnn01101010 sts    rn, fpscr
          this->regs.r[op_get_r1(op)].u = this->regs.fpscr;
          break;
        case 0xF: // 0000nnnn11111010 stc    rn, dbr
          this->regs.r[op_get_r1(op)].u = this->regs.dbr;
          break;
        default:
          throw runtime_error("invalid opcode");
      }
      break;

    case 0xB:
      if (op_get_r1(op) != 0) {
        throw runtime_error("invalid opcode");
      }
      switch (op_get_r2(op)) {
        case 0x0: // 0000000000001011 rets
          this->regs.enqueue_branch(Regs::PendingBranchType::RETURN, 0, 1);
          break;
        case 0x1: // 0000000000011011 sleep
          throw terminate_emulation();
        case 0x2: // 0000000000101011 rte
          // TODO
          throw runtime_error("exceptions are not supported");
        default:
          throw runtime_error("invalid opcode");
      }
      break;

    case 0xC: // 0000nnnnmmmm1100 mov.b  rn, [r0 + rm]  # sign-ext
      this->regs.r[op_get_r1(op)].u = sign_extend<uint32_t, uint8_t>(this->mem->read_u8(
          this->regs.r[0].u + this->regs.r[op_get_r2(op)].u));
      break;

    case 0xD: { // 0000nnnnmmmm1101 mov.w  rn, [r0 + rm]  # sign-ext
      uint32_t addr = this->regs.r[0].u + this->regs.r[op_get_r2(op)].u;
      this->assert_aligned(addr, 2);
      this->regs.r[op_get_r1(op)].u = sign_extend<uint32_t, uint16_t>(this->mem->read_u16l(addr));
      break;
    }

    case 0xE: { // 0000nnnnmmmm1110 mov.l  rn, [r0 + rm]
      uint32_t addr = this->regs.r[0].u + this->regs.r[op_get_r2(op)].u;
      this->assert_aligned(addr, 4);
      this->regs.r[op_get_r1(op)].u = this->mem->read_u32l(addr);
      break;
    }

    case 0xF: { // 0000nnnnmmmm1111 mac.l  [rn]+, [rm]+  # mac = [rn] * [rm] + mac
      uint32_t rn_addr = this->regs.r[op_get_r1(op)].u;
      uint32_t rm_addr = this->regs.r[op_get_r2(op)].u;
      this->assert_aligned(rn_addr, 4);
      this->assert_aligned(rm_addr, 4);
      this->regs.mac +=
          static_cast<int64_t>(this->mem->read_s32l(rn_addr)) *
          static_cast<int64_t>(this->mem->read_s32l(rm_addr));
      break;
    }

    default:
      throw runtime_error("invalid opcode");
  }
}

void SH4Emulator::execute_one_1(uint16_t op) {
  // 0001nnnnmmmmdddd mov.l  [rn + 4 * d], rm
  uint32_t addr = this->regs.r[op_get_r1(op)].u + (op_get_r3(op) * 4);
  this->assert_aligned(addr, 4);
  this->mem->write_u32l(addr, this->regs.r[op_get_r2(op)].u);
}

void SH4Emulator::execute_one_2(uint16_t op) {
  auto& rn = this->regs.r[op_get_r1(op)];
  auto& rm = this->regs.r[op_get_r2(op)];
  switch (op_get_r3(op)) {
    case 0x0: // 0010nnnnmmmm0000 mov.b  [rn], rm
      this->mem->write_u8(rn.u, rm.u);
      break;
    case 0x1: // 0010nnnnmmmm0001 mov.w  [rn], rm
      this->assert_aligned(rn.u, 2);
      this->mem->write_u16l(rn.u, rm.u);
      break;
    case 0x2: // 0010nnnnmmmm0010 mov.l  [rn], rm
      this->assert_aligned(rn.u, 4);
      this->mem->write_u32l(rn.u, rm.u);
      break;
    case 0x4: // 0010nnnnmmmm0100 mov.b  -[rn], rm
      rn.u--;
      this->mem->write_u8(rn.u, rm.u);
      break;
    case 0x5: // 0010nnnnmmmm0101 mov.w  -[rn], rm
      this->assert_aligned(rn.u, 2);
      rn.u -= 2;
      this->mem->write_u16l(rn.u, rm.u);
      break;
    case 0x6: // 0010nnnnmmmm0110 mov.l  -[rn], rm
      this->assert_aligned(rn.u, 4);
      rn.u -= 4;
      this->mem->write_u32l(rn.u, rm.u);
      break;
    case 0x7: { // 0010nnnnmmmm0111 div0s  rn, rm
      bool q = (rn.s < 0);
      bool m = (rm.s < 0);
      this->regs.replace_mqt(m, q, (m != q));
      break;
    }
    case 0x8: // 0010nnnnmmmm1000 test   rn, rm
      this->regs.replace_t((rn.u & rm.u) == 0);
      break;
    case 0x9: // 0010nnnnmmmm1001 and    rn, rm
      rn.u &= rm.u;
      break;
    case 0xA: // 0010nnnnmmmm1010 xor    rn, rm
      rn.u ^= rm.u;
      break;
    case 0xB: // 0010nnnnmmmm1011 or     rn, rm
      rn.u |= rm.u;
      break;
    case 0xC: { // 0010nnnnmmmm1100 cmpstr rn, rm  # any bytes are equal
      uint32_t v = rn.u ^ rm.u;
      this->regs.replace_t(!(v & 0xFF000000) || !(v & 0x00FF0000) || !(v & 0x0000FF00) || !(v & 0x000000FF));
      break;
    }
    case 0xD: // 0010nnnnmmmm1101 xtrct  rn, rm  # rm.rn middle 32 bits -> rn
      rn.u = ((rm.u << 16) & 0xFFFF0000) | ((rn.u >> 16) & 0x0000FFFF);
      break;
    case 0xE: { // 0010nnnnmmmm1110 mulu.w rn, rm  # macl = rn * rm
      uint32_t v = (rn.u & 0xFFFF) * (rm.u & 0xFFFF);
      this->regs.mac = (this->regs.mac & 0xFFFFFFFF00000000) | v;
      break;
    }
    case 0xF: { // 0010nnnnmmmm1111 muls.w rn, rm  # macl = rn * rm
      int32_t v = sign_extend<int32_t, int16_t>(rn.s & 0xFFFF) * sign_extend<int32_t, int16_t>(rm.s & 0xFFFF);
      this->regs.mac = (this->regs.mac & 0xFFFFFFFF00000000) | static_cast<uint32_t>(v);
      break;
    }
    default:
      throw runtime_error("invalid opcode");
  }
}

void SH4Emulator::execute_one_3(uint16_t op) {
  auto& rn = this->regs.r[op_get_r1(op)];
  auto& rm = this->regs.r[op_get_r2(op)];
  switch (op_get_r3(op)) {
    case 0x0: // 0011nnnnmmmm0000 cmpeq  rn, rm
      this->regs.replace_t(rn.u == rm.u);
      break;
    case 0x2: // 0011nnnnmmmm0010 cmpae  rn, rm
      this->regs.replace_t(rn.u >= rm.u);
      break;
    case 0x3: // 0011nnnnmmmm0011 cmpge  rn, rm
      this->regs.replace_t(rn.s >= rm.s);
      break;
    case 0x4: { // 0011nnnnmmmm0100 div1   rn, rm
      bool old_q = this->regs.q();
      this->regs.replace_q(rn.s < 0);
      rn.u = (rn.u << 1) | this->regs.t();

      uint32_t tmp0 = rn.u;
      if (!old_q) {
        if (!this->regs.m()) {
          rn.u -= rm.u;
          uint8_t tmp1 = (rn.u > tmp0);
          this->regs.replace_q(this->regs.q() ? (tmp1 == 0) : tmp1);
        } else {
          rn.u += rm.u;
          uint8_t tmp1 = (rn.u < tmp0);
          this->regs.replace_q(this->regs.q() ? tmp1 : (tmp1 == 0));
        }
      } else {
        if (!this->regs.m()) {
          rn.u += rm.u;
          uint8_t tmp1 = (rn.u < tmp0);
          this->regs.replace_q(this->regs.q() ? (tmp1 == 0) : tmp1);
        } else {
          rn.u -= rm.u;
          uint8_t tmp1 = (rn.u > tmp0);
          this->regs.replace_q(this->regs.q() ? tmp1 : (tmp1 == 0));
        }
      }
      break;
    }
    case 0x5: // 0011nnnnmmmm0101 dmulu.l rn, rm
      this->regs.mac =
          static_cast<uint64_t>(this->regs.r[op_get_r1(op)].u) *
          static_cast<uint64_t>(this->regs.r[op_get_r2(op)].u);
      break;
    case 0x6: // 0011nnnnmmmm0110 cmpa   rn, rm
      this->regs.replace_t(rn.u > rm.u);
      break;
    case 0x7: // 0011nnnnmmmm0111 cmpgt  rn, rm
      this->regs.replace_t(rn.s > rm.s);
      break;
    case 0x8: // 0011nnnnmmmm1000 sub    rn, rm
      rn.u -= rm.u;
      break;
    case 0xA: { // 0011nnnnmmmm1010 subc   rn, rm
      uint32_t tmp1 = rn.u - rm.u;
      uint32_t tmp0 = rn.u;
      rn.u = tmp1 - this->regs.t();
      this->regs.replace_t((tmp0 < tmp1) || (tmp1 < rn.u));
      break;
    }
    case 0xB: { // 0011nnnnmmmm1011 subv   rn, rm
      int32_t dest = (rn.s >= 0) ? 0 : 1;
      int32_t src = ((rm.s >= 0) ? 0 : 1) + dest;
      rn.s -= rm.s;
      int32_t ans = ((rn.s >= 0) ? 0 : 1) + dest;
      this->regs.replace_t((src == 1) && (ans == 1));
      break;
    }
    case 0xC: // 0011nnnnmmmm1100 add    rn, rm
      rn.u += rm.u;
      break;
    case 0xD: // 0011nnnnmmmm1101 dmuls.l rn, rm
      this->regs.mac =
          static_cast<int64_t>(this->regs.r[op_get_r1(op)].s) *
          static_cast<int64_t>(this->regs.r[op_get_r2(op)].s);
      break;
    case 0xE: { // 0011nnnnmmmm1110 addc   rn, rm
      uint32_t tmp1 = rn.u + rm.u;
      uint32_t tmp0 = rn.u;
      rn.u = tmp1 + this->regs.t();
      this->regs.replace_t((tmp0 > tmp1) || (tmp1 > rn.u));
      break;
    }
    case 0xF: { // 0011nnnnmmmm1111 addv   rn, rm
      int32_t dest = (rn.s >= 0) ? 0 : 1;
      int32_t src = ((rm.s >= 0) ? 0 : 1) + dest;
      rn.s += rm.s;
      int32_t ans = ((rn.s >= 0) ? 0 : 1) + dest;
      this->regs.replace_t(((src == 0) || (src == 2)) && (ans == 1));
      break;
    }
    default:
      throw runtime_error("invalid opcode");
  }
}

void SH4Emulator::execute_one_4(uint16_t op) {
  auto& r = this->regs.r[op_get_r1(op)];
  switch (op_get_r3(op)) {
    case 0x0:
      switch (op_get_r2(op)) {
        case 0x0: // 0100nnnn00000000 shl    rn
        case 0x2: // 0100nnnn00100000 shal   rn
          this->regs.replace_t(r.s < 0);
          r.u <<= 1;
          break;
        case 0x1: // 0100nnnn00010000 dec    rn ("dt" in manual)
          r.u--;
          this->regs.replace_t(r.u == 0);
          break;
        default:
          throw runtime_error("invalid opcode");
      }
      break;
    case 0x1:
      switch (op_get_r2(op)) {
        case 0x0: // 0100nnnn00000001 shr    rn
          this->regs.replace_t(r.u & 1);
          r.u >>= 1;
          break;
        case 0x1: // 0100nnnn00010001 cmpge  rn, 0
          this->regs.replace_t(r.s >= 0);
          break;
        case 0x2: // 0100nnnn00100001 shar   rn
          this->regs.replace_t(r.u & 1);
          r.s >>= 1;
          break;
        default:
          throw runtime_error("invalid opcode");
      }
      break;
    case 0x2:
      switch (op_get_r2(op)) {
        case 0x0: // 0100nnnn00000010 sts.l  -[rn], mach
          this->assert_aligned(r.u, 4);
          r.u -= 4;
          this->mem->write_u32l(r.u, this->regs.mac >> 32);
          break;
        case 0x1: // 0100nnnn00010010 sts.l  -[rn], macl
          this->assert_aligned(r.u, 4);
          r.u -= 4;
          this->mem->write_u32l(r.u, this->regs.mac);
          break;
        case 0x2: // 0100nnnn00100010 sts.l  -[rn], pr
          this->assert_aligned(r.u, 4);
          r.u -= 4;
          this->mem->write_u32l(r.u, this->regs.pr);
          break;
        case 0x3: // 0100nnnn00110010 stc.l  -[rn], sgr
          this->assert_aligned(r.u, 4);
          r.u -= 4;
          this->mem->write_u32l(r.u, this->regs.sgr);
          break;
        case 0x5: // 0100nnnn01010010 sts.l  -[rn], fpul
          this->assert_aligned(r.u, 4);
          r.u -= 4;
          this->mem->write_u32l(r.u, this->regs.fpul_i);
          break;
        case 0x6: // 0100nnnn01100010 sts.l  -[rn], fpscr
          this->assert_aligned(r.u, 4);
          r.u -= 4;
          this->mem->write_u32l(r.u, this->regs.fpscr);
          break;
        case 0xF: // 0100nnnn11110010 stc.l  -[rn], dbr
          this->assert_aligned(r.u, 4);
          r.u -= 4;
          this->mem->write_u32l(r.u, this->regs.dbr);
          break;
        default:
          throw runtime_error("invalid opcode");
      }
      break;
    case 0x3:
      if (op_get_r2(op) & 0x8) { // 0100nnnn1mmm0011 stc.l  -[rn], rmb
        throw runtime_error("banked registers are not supported");
      }
      switch (op_get_r2(op)) {
        case 0x0: // 0100nnnn00000011 stc.l  -[rn], sr
          this->assert_aligned(r.u, 4);
          r.u -= 4;
          this->mem->write_u32l(r.u, this->regs.sr);
          break;
        case 0x1: // 0100nnnn00010011 stc.l  -[rn], gbr
          this->assert_aligned(r.u, 4);
          r.u -= 4;
          this->mem->write_u32l(r.u, this->regs.gbr);
          break;
        case 0x2: // 0100nnnn00100011 stc.l  -[rn], vbr
          this->assert_aligned(r.u, 4);
          r.u -= 4;
          this->mem->write_u32l(r.u, this->regs.vbr);
          break;
        case 0x3: // 0100nnnn00110011 stc.l  -[rn], ssr
          this->assert_aligned(r.u, 4);
          r.u -= 4;
          this->mem->write_u32l(r.u, this->regs.ssr);
          break;
        case 0x4: // 0100nnnn01000011 stc.l  -[rn], spc
          this->assert_aligned(r.u, 4);
          r.u -= 4;
          this->mem->write_u32l(r.u, this->regs.spc);
          break;
        default:
          throw runtime_error("invalid opcode");
      }
      break;
    case 0x4:
      switch (op_get_r2(op)) {
        case 0x0: // 0100nnnn00000100 rol    rn
          this->regs.replace_t(r.s < 0);
          r.u = (r.u << 1) | this->regs.t();
          break;
        case 0x2: { // 0100nnnn00100100 rcl    rn
          bool old_t = this->regs.t();
          this->regs.replace_t(r.s < 0);
          r.u = (r.u << 1) | old_t;
          break;
        }
        default:
          throw runtime_error("invalid opcode");
      }
      break;
    case 0x5:
      switch (op_get_r2(op)) {
        case 0x0: // 0100nnnn00000101 ror    rn
          this->regs.replace_t(r.u & 1);
          r.u = (r.u >> 1) | (this->regs.t() ? 0x80000000 : 0);
          break;
        case 0x1: // 0100nnnn00010101 cmpgt  rn, 0
          this->regs.replace_t(r.s > 0);
          break;
        case 0x2: { // 0100nnnn00100101 rcr    rn
          bool old_t = this->regs.t();
          this->regs.replace_t(r.u & 1);
          r.u = (r.u >> 1) | (old_t ? 0x80000000 : 0);
          break;
        }
        default:
          throw runtime_error("invalid opcode");
      }
      break;
    case 0x6:
      switch (op_get_r2(op)) {
        case 0x0: // 0100mmmm00000110 lds    mach, [rm]+
          this->assert_aligned(r.u, 4);
          this->regs.mac = (this->regs.mac & 0x00000000FFFFFFFF) | (static_cast<uint64_t>(this->mem->read_u32l(r.u)) << 32);
          r.u += 4;
          break;
        case 0x1: // 0100mmmm00010110 lds    macl, [rm]+
          this->assert_aligned(r.u, 4);
          this->regs.mac = (this->regs.mac & 0xFFFFFFFF00000000) | static_cast<uint64_t>(this->mem->read_u32l(r.u));
          r.u += 4;
          break;
        case 0x2: // 0100mmmm00100110 lds    pr, [rm]+
          this->assert_aligned(r.u, 4);
          this->regs.pr = this->mem->read_u32l(r.u);
          r.u += 4;
          break;
        case 0x5: // 0100mmmm01010110 lds.l  fpul, [rm]+
          this->assert_aligned(r.u, 4);
          this->regs.fpul_i = this->mem->read_u32l(r.u);
          r.u += 4;
          break;
        case 0x6: // 0100mmmm01100110 lds.l  fpscr, [rm]+
          this->assert_aligned(r.u, 4);
          this->regs.fpscr = this->mem->read_u32l(r.u);
          r.u += 4;
          break;
        case 0xF: // 0100mmmm11110110 ldc.l  dbr, [rm]+
          this->assert_aligned(r.u, 4);
          this->regs.dbr = this->mem->read_u32l(r.u);
          r.u += 4;
          break;
        default:
          throw runtime_error("invalid opcode");
      }
      break;
    case 0x7:
      if (op_get_r2(op) & 0x8) { // 0100mmmm1nnn0111 ldc.l  rnb, [rm]+
        throw runtime_error("banked registers are not supported");
      }
      switch (op_get_r2(op)) {
        case 0x0: // 0100mmmm00000111 ldc.l  sr, [rm]+
          this->regs.assert_no_branch_pending();
          this->assert_aligned(r.u, 4);
          this->regs.sr = this->mem->read_u32l(r.u);
          r.u += 4;
          break;
        case 0x1: // 0100mmmm00010111 ldc.l  gbr, [rm]+
          this->assert_aligned(r.u, 4);
          this->regs.gbr = this->mem->read_u32l(r.u);
          r.u += 4;
          break;
        case 0x2: // 0100mmmm00100111 ldc.l  vbr, [rm]+
          this->assert_aligned(r.u, 4);
          this->regs.vbr = this->mem->read_u32l(r.u);
          r.u += 4;
          break;
        case 0x3: // 0100mmmm00110111 ldc.l  ssr, [rm]+
          this->assert_aligned(r.u, 4);
          this->regs.ssr = this->mem->read_u32l(r.u);
          r.u += 4;
          break;
        case 0x4: // 0100mmmm01000111 ldc.l  spc, [rm]+
          this->assert_aligned(r.u, 4);
          this->regs.spc = this->mem->read_u32l(r.u);
          r.u += 4;
          break;
      }
      break;
    case 0x8:
    case 0x9: {
      static const uint8_t amounts[3] = {2, 8, 16};

      uint8_t which = op_get_r2(op);
      if (which > 2) {
        throw runtime_error("invalid opcode");
      }

      if (op_get_r3(op) & 1) {
        // 0100nnnn00001001 shr    rn, 2
        // 0100nnnn00011001 shr    rn, 8
        // 0100nnnn00101001 shr    rn, 16
        r.u >>= amounts[which];
      } else {
        // 0100nnnn00001000 shl    rn, 2
        // 0100nnnn00011000 shl    rn, 8
        // 0100nnnn00101000 shl    rn, 16
        r.u <<= amounts[which];
      }
      break;
    }
    case 0xA:
      switch (op_get_r2(op)) {
        case 0x0: // 0100mmmm00001010 lds    mach, rm
          this->regs.mac = (this->regs.mac & 0x00000000FFFFFFFF) | (static_cast<uint64_t>(r.u) << 32);
          break;
        case 0x1: // 0100mmmm00011010 lds    macl, rm
          this->regs.mac = (this->regs.mac & 0xFFFFFFFF00000000) | static_cast<uint64_t>(r.u);
          break;
        case 0x2: // 0100mmmm00101010 lds    pr, rm
          this->regs.pr = r.u;
          break;
        case 0x5: // 0100mmmm01011010 lds    fpul, rm
          this->regs.fpul_i = r.u;
          break;
        case 0x6: // 0100mmmm01101010 lds    fpscr, rm
          this->regs.fpscr = r.u;
          break;
        case 0xF: // 0100mmmm11111010 ldc    dbr, rm
          this->regs.dbr = r.u;
          break;
        default:
          throw runtime_error("invalid opcode");
      }
      break;
    case 0xB:
      switch (op_get_r2(op)) {
        case 0x0: // 0100nnnn00001011 calls  [rn]
          this->regs.enqueue_branch(Regs::PendingBranchType::CALL, r.u, 1);
          break;
        case 0x1: { // 0100nnnn00011011 tas.b  [rn]
          uint8_t v = this->mem->read_u8(r.u);
          this->regs.replace_t(v == 0);
          this->mem->write_u8(r.u, v | 0x80);
          break;
        }
        case 0x2: // 0100nnnn00101011 bs     [rn]
          this->regs.enqueue_branch(Regs::PendingBranchType::BRANCH, r.u, 1);
          break;
        default:
          throw runtime_error("invalid opcode");
      }
      break;
    case 0xC: // 0100nnnnmmmm1100 shad   rn, rm
    case 0xD: { // 0100nnnnmmmm1101 shld   rn, rm
      bool is_l = op_get_r3(op) & 1;
      const auto& rm = this->regs.r[op_get_r2(op)];
      if (rm.s >= 0) {
        r.u <<= (rm.u & 0x1F);
      } else if ((rm.s & 0x1F) == 0) {
        r.s = (is_l || (r.s >= 0)) ? 0 : -1;
      } else if (is_l) {
        r.u >>= (((~rm.u) & 0x1F) + 1);
      } else {
        r.s >>= (((~rm.u) & 0x1F) + 1);
      }
      break;
    }
    case 0xE:
      if (op_get_r2(op) & 0x8) { // 0100mmmm1nnn1110 ldc    rnb, rm
        throw runtime_error("banked registers are not supported");
      }
      switch (op_get_r2(op)) {
        case 0x0: // 0100mmmm00001110 ldc    sr, rm
          this->regs.assert_no_branch_pending();
          this->regs.sr = r.u;
          break;
        case 0x1: // 0100mmmm00011110 ldc    gbr, rm
          this->regs.gbr = r.u;
          break;
        case 0x2: // 0100mmmm00101110 ldc    vbr, rm
          this->regs.vbr = r.u;
          break;
        case 0x3: // 0100mmmm00111110 ldc    ssr, rm
          this->regs.ssr = r.u;
          break;
        case 0x4: // 0100mmmm01001110 ldc    spc, rm
          this->regs.spc = r.u;
          break;
      }
      break;
    default:
      throw runtime_error("invalid opcode");
  }
}

void SH4Emulator::execute_one_5(uint16_t op) {
  // 0101nnnnmmmmdddd mov.l  rn, [rm + 4 * d]
  uint32_t addr = this->regs.r[op_get_r2(op)].u + 4 * op_get_r3(op);
  this->assert_aligned(addr, 4);
  this->regs.r[op_get_r1(op)].u = this->mem->read_u32l(addr);
}

void SH4Emulator::execute_one_6(uint16_t op) {
  auto& rn = this->regs.r[op_get_r1(op)];
  auto& rm = this->regs.r[op_get_r2(op)];
  switch (op_get_r3(op)) {
    case 0x0: // 0110nnnnmmmm0000 mov.b  rn, [rm]  # sign-ext
      rn.u = sign_extend<uint32_t, uint8_t>(this->mem->read_u8(rm.u));
      break;
    case 0x1: // 0110nnnnmmmm0001 mov.w  rn, [rm]  # sign-ext
      this->assert_aligned(rm.u, 2);
      rn.u = sign_extend<uint32_t, uint16_t>(this->mem->read_u16l(rm.u));
      break;
    case 0x2: // 0110nnnnmmmm0010 mov.l  rn, [rm]
      this->assert_aligned(rm.u, 4);
      rn.u = this->mem->read_u32l(rm.u);
      break;
    case 0x3: // 0110nnnnmmmm0011 mov    rn, rm
      rn.u = rm.u;
      break;
    case 0x4: // 0110nnnnmmmm0100 mov.b  rn, [rm]+  # sign-ext
      rn.u = sign_extend<uint32_t, uint8_t>(this->mem->read_u8(rm.u));
      rm.u++;
      break;
    case 0x5: // 0110nnnnmmmm0101 mov.w  rn, [rm]+  # sign-ext
      this->assert_aligned(rm.u, 2);
      rn.u = sign_extend<uint32_t, uint16_t>(this->mem->read_u16l(rm.u));
      rm.u += 2;
      break;
    case 0x6: // 0110nnnnmmmm0110 mov.l  rn, [rm]+
      this->assert_aligned(rm.u, 4);
      rn.u = this->mem->read_u32l(rm.u);
      rm.u += 4;
      break;
    case 0x7: // 0110nnnnmmmm0111 not    rn, rm
      rn.u = ~rm.u;
      break;
    case 0x8: // 0110nnnnmmmm1000 swap.b rn, rm  # swap lower 2 bytes
      rn.u = (rm.u & 0xFFFF0000) | ((rm.u >> 8) & 0x000000FF) | ((rm.u << 8) & 0x0000FF00);
      break;
    case 0x9: // 0110nnnnmmmm1001 swap.w rn, rm  # swap words
      rn.u = ((rm.u >> 16) & 0x0000FFFF) | ((rm.u << 16) & 0xFFFF0000);
      break;
    case 0xA: { // 0110nnnnmmmm1010 negc   rn, rm
      uint32_t temp = 0 - rm.u;
      rn.u = temp - this->regs.t();
      this->regs.replace_t((0 < temp) || (temp < rn.u));
      break;
    }
    case 0xB: // 0110nnnnmmmm1011 neg    rn, rm
      rn.s = -rm.s;
      break;
    case 0xC: // 0110nnnnmmmm1100 extu.b rn, rm
      rn.u = rm.u & 0x000000FF;
      break;
    case 0xD: // 0110nnnnmmmm1101 extu.w rn, rm
      rn.u = rm.u & 0x0000FFFF;
      break;
    case 0xE: // 0110nnnnmmmm1110 exts.b rn, rm
      rn.u = sign_extend<uint32_t, uint8_t>(rm.u);
      break;
    case 0xF: // 0110nnnnmmmm1111 exts.w rn, rm
      rn.u = sign_extend<uint32_t, uint16_t>(rm.u);
      break;
    default:
      throw runtime_error("invalid opcode");
  }
}

void SH4Emulator::execute_one_7(uint16_t op) {
  // 0111nnnniiiiiiii add    rn, imm
  this->regs.r[op_get_r1(op)].u += op_get_simm8(op);
}

void SH4Emulator::execute_one_8(uint16_t op) {
  switch (op_get_r1(op)) {
    case 0x0: // 10000000nnnndddd mov.b  [rn + d], r0
      this->mem->write_u8(this->regs.r[op_get_r2(op)].u + op_get_uimm4(op), this->regs.r[0].u);
      break;
    case 0x1: { // 10000001nnnndddd mov.w  [rn + 2 * d], r0
      uint32_t addr = this->regs.r[op_get_r2(op)].u + 2 * op_get_uimm4(op);
      this->assert_aligned(addr, 2);
      this->mem->write_u16l(addr, this->regs.r[0].u);
      break;
    }
    case 0x4: // 10000100mmmmdddd mov.b  r0, [rm + d]  # sign-ext
      this->regs.r[0].u = sign_extend<uint32_t, uint8_t>(this->mem->read_u8(this->regs.r[op_get_r2(op)].u + op_get_uimm4(op)));
      break;
    case 0x5: { // 10000101mmmmdddd mov.w  r0, [rm + 2 * d]  # sign-ext
      uint32_t addr = this->regs.r[op_get_r2(op)].u + 2 * op_get_uimm4(op);
      this->assert_aligned(addr, 2);
      this->regs.r[0].u = sign_extend<uint32_t, uint16_t>(this->mem->read_u16l(addr));
      break;
    }
    case 0x8: // 10001000iiiiiiii cmpeq  r0, imm
      this->regs.replace_t(this->regs.r[0].s == op_get_simm8(op));
      break;
    case 0x9: // 10001001dddddddd bt     (pc + 4 + 2 * d)  # branch if T = 1
    case 0xB: // 10001011dddddddd bf     (pc + 4 + 2 * d)  # branch if T = 0
    case 0xD: // 10001101dddddddd bts    (pc + 4 + 2 * d)  # branch after next ins if T = 1
    case 0xF: { // 10001111dddddddd bfs    (pc + 4 + 2 * d)  # branch after next ins if T = 0
      bool is_f = op_get_r1(op) & 2;
      bool is_s = op_get_r1(op) & 4;
      if (this->regs.t() != is_f) {
        this->regs.enqueue_branch(
            Regs::PendingBranchType::BRANCH,
            this->regs.pc + 4 + 2 * op_get_simm8(op),
            is_s ? 1 : 0);
      } else {
        // It looks like this opcode is always invalid in a delay slot even if
        // the branch isn't taken, so we assert that here.
        this->regs.assert_no_branch_pending();
      }
      break;
    }
    default:
      throw runtime_error("invalid opcode");
  }
}

void SH4Emulator::execute_one_9(uint16_t op) {
  // 1001nnnndddddddd mov.w  rn, [pc + 4 + d * 2]
  this->regs.assert_no_branch_pending();
  uint32_t addr = this->regs.pc + 4 + 2 * op_get_simm8(op);
  this->assert_aligned(addr, 2);
  this->regs.r[op_get_r1(op)].u = sign_extend<uint32_t, uint16_t>(this->mem->read_u16l(addr));
}

void SH4Emulator::execute_one_A_B(uint16_t op) {
  // 1010dddddddddddd bs     (pc + 4 + 2 * d)
  // 1011dddddddddddd calls  (pc + 4 + 2 * d)
  this->regs.enqueue_branch(
      (op_get_op(op) & 1) ? Regs::PendingBranchType::CALL : Regs::PendingBranchType::BRANCH,
      this->regs.pc + 4 + 2 * op_get_simm12(op),
      1);
}

void SH4Emulator::execute_one_C(uint16_t op) {
  switch (op_get_r1(op)) {
    case 0x0: // 11000000dddddddd mov.b  [gbr + d], r0
      this->mem->write_u8(this->regs.gbr + op_get_uimm8(op), this->regs.r[0].u);
      break;
    case 0x1: { // 11000001dddddddd mov.w  [gbr + 2 * d], r0
      uint32_t addr = this->regs.gbr + 2 * op_get_uimm8(op);
      this->assert_aligned(addr, 2);
      this->mem->write_u16l(addr, this->regs.r[0].u);
      break;
    }
    case 0x2: { // 11000010dddddddd mov.l  [gbr + 4 * d], r0
      uint32_t addr = this->regs.gbr + 2 * op_get_uimm8(op);
      this->assert_aligned(addr, 4);
      this->mem->write_u32l(this->regs.gbr + 4 * op_get_uimm8(op), this->regs.r[0].u);
      break;
    }
    case 0x3: // 11000011iiiiiiii trapa  imm
      this->regs.assert_no_branch_pending();
      throw runtime_error(std::format("unhandled trap {:02X}", op_get_uimm8(op)));
    case 0x4: // 11000100dddddddd mov.b  r0, [gbr + d]  # sign-ext
      this->regs.r[0].u = sign_extend<uint32_t, uint8_t>(this->mem->read_u8(this->regs.gbr + op_get_uimm8(op)));
      break;
    case 0x5: { // 11000101dddddddd mov.w  r0, [gbr + 2 * d]  # sign-ext
      uint32_t addr = this->regs.gbr + 2 * op_get_uimm8(op);
      this->assert_aligned(addr, 2);
      this->regs.r[0].u = sign_extend<uint32_t, uint16_t>(this->mem->read_u16l(addr));
      break;
    }
    case 0x6: { // 11000110dddddddd mov.l  r0, [gbr + 4 * d]
      uint32_t addr = this->regs.gbr + 4 * op_get_uimm8(op);
      this->assert_aligned(addr, 4);
      this->regs.r[0].u = this->mem->read_u32l(this->regs.gbr + 4 * op_get_uimm8(op));
      break;
    }
    case 0x7: // 11000111dddddddd mova   r0, [(pc & ~3) + 4 + d * 4]
      this->regs.assert_no_branch_pending();
      this->regs.r[0].u = (this->regs.pc & (~3)) + 4 + 4 * op_get_uimm8(op);
      break;
    case 0x8: // 11001000iiiiiiii test   r0, imm
      this->regs.replace_t(this->regs.r[0].u == op_get_uimm8(op));
      break;
    case 0x9: // 11001001iiiiiiii and    r0, imm
      this->regs.r[0].u &= op_get_uimm8(op);
      break;
    case 0xA: // 11001010iiiiiiii xor    r0, imm
      this->regs.r[0].u ^= op_get_uimm8(op);
      break;
    case 0xB: // 11001011iiiiiiii or     r0, imm
      this->regs.r[0].u |= op_get_uimm8(op);
      break;
    case 0xC: // 11001100iiiiiiii test.b [r0 + gbr], imm
      this->regs.replace_t(this->mem->read_u8(this->regs.gbr + this->regs.r[0].u) == op_get_uimm8(op));
      break;
    case 0xD: { // 11001101iiiiiiii and.b  [r0 + gbr], imm
      uint32_t addr = this->regs.gbr + this->regs.r[0].u;
      this->mem->write_u8(addr, this->mem->read_u8(addr) & op_get_uimm8(op));
      break;
    }
    case 0xE: { // 11001110iiiiiiii xor.b  [r0 + gbr], imm
      uint32_t addr = this->regs.gbr + this->regs.r[0].u;
      this->mem->write_u8(addr, this->mem->read_u8(addr) ^ op_get_uimm8(op));
      break;
    }
    case 0xF: { // 11001111iiiiiiii or.b   [r0 + gbr], imm
      uint32_t addr = this->regs.gbr + this->regs.r[0].u;
      this->mem->write_u8(addr, this->mem->read_u8(addr) | op_get_uimm8(op));
      break;
    }
    default:
      throw logic_error("unhandled C/X case");
  }
}

void SH4Emulator::execute_one_D(uint16_t op) {
  // 1101nnnndddddddd mov.l  rn, [(pc & ~3) + 4 + d * 4]
  this->regs.assert_no_branch_pending();
  uint32_t addr = (this->regs.pc & (~3)) + 4 + 4 * op_get_uimm8(op);
  this->assert_aligned(addr, 4);
  this->regs.r[op_get_r1(op)].u = this->mem->read_u32l(addr);
}

void SH4Emulator::execute_one_E(uint16_t op) {
  // 1110nnnniiiiiiii mov    rn, imm
  this->regs.r[op_get_r1(op)].s = op_get_simm8(op);
}

void SH4Emulator::execute_one_F(uint16_t op) {
  // TODO: Use fpscr_fr here? When is it needed?
  float& frn = (op & 0x0100) ? this->regs.f[(op_get_r1(op) >> 1) + 16] : this->regs.f[op_get_r1(op) >> 1];
  double& drn = (op & 0x0100) ? this->regs.d[(op_get_r1(op) >> 1) + 8] : this->regs.d[op_get_r1(op) >> 1];
  float& frm = (op & 0x0010) ? this->regs.f[(op_get_r2(op) >> 1) + 16] : this->regs.f[op_get_r2(op) >> 1];
  double& drm = (op & 0x0010) ? this->regs.d[(op_get_r2(op) >> 1) + 8] : this->regs.d[op_get_r2(op) >> 1];
  auto& rn = this->regs.r[op_get_r1(op)];
  auto& rm = this->regs.r[op_get_r2(op)];
  switch (op_get_r3(op)) {
    case 0x0:
      if (this->regs.fpscr_pr()) { // 1111nnn0mmm00000 fadd   drn, drm
        if (op & 0x0110) {
          throw runtime_error("invalid opcode");
        }
        drn += drm;
      } else { // 1111nnnnmmmm0000 fadd   frn, frm
        frn += frm;
      }
      break;
    case 0x1:
      if (this->regs.fpscr_pr()) { // 1111nnn0mmm00001 fsub   drn, drm
        if (op & 0x0110) {
          throw runtime_error("invalid opcode");
        }
        drn -= drm;
      } else { // 1111nnnnmmmm0001 fsub   frn, frm
        frn -= frm;
      }
      break;
    case 0x2:
      if (this->regs.fpscr_pr()) { // 1111nnn0mmm00010 fmul   drn, drm
        if (op & 0x0110) {
          throw runtime_error("invalid opcode");
        }
        drn *= drm;
      } else { // 1111nnnnmmmm0010 fmul   frn, frm
        frn *= frm;
      }
      break;
    case 0x3:
      if (this->regs.fpscr_pr()) { // 1111nnn0mmm00011 fdiv   drn, drm
        if (op & 0x0110) {
          throw runtime_error("invalid opcode");
        }
        drn /= drm;
      } else { // 1111nnnnmmmm0011 fdiv   frn, frm
        frn /= frm;
      }
      break;
    case 0x4:
      if (this->regs.fpscr_pr()) { // 1111nnn0mmm00100 fcmpeq drn, drm
        if (op & 0x0110) {
          throw runtime_error("invalid opcode");
        }
        this->regs.replace_t(drn == drm);
      } else { // 1111nnnnmmmm0100 fcmpeq frn, frm
        this->regs.replace_t(frn == frm);
      }
      break;
    case 0x5:
      if (this->regs.fpscr_pr()) { // 1111nnn0mmm00101 fcmpgt drn, drm
        if (op & 0x0110) {
          throw runtime_error("invalid opcode");
        }
        this->regs.replace_t(drn > drm);
      } else { // 1111nnnnmmmm0101 fcmpgt frn, frm
        this->regs.replace_t(frn > frm);
      }
      break;
    case 0x6: {
      uint32_t addr = this->regs.r[0].u + rm.u;
      if (this->regs.fpscr_sz()) {
        // 1111nnn0mmmm0110 fmov   drn, [r0 + rm]
        // 1111nnn1mmmm0110 fmov   xdn, [r0 + rm]
        this->assert_aligned(addr, 8);
        drn = this->mem->read_f64l(addr);
      } else { // 1111nnnnmmmm0110 fmov.s frn, [r0 + rm]
        this->assert_aligned(addr, 4);
        frn = this->mem->read_f32l(addr);
      }
      break;
    }
    case 0x7: {
      uint32_t addr = this->regs.r[0].u + rn.u;
      if (this->regs.fpscr_sz()) {
        // 1111nnnnmmm00111 fmov   [r0 + rn], drm
        // 1111nnnnmmm10111 fmov   [r0 + rn], xdm
        this->assert_aligned(addr, 8);
        this->mem->write_f64l(addr, drn);
      } else { // 1111nnnnmmmm0111 fmov.s [r0 + rn], frm
        this->assert_aligned(addr, 4);
        this->mem->write_f32l(addr, frn);
      }
      break;
    }
    case 0x8:
      if (this->regs.fpscr_sz()) {
        // 1111nnn0mmmm1000 fmov   drn, [rm]
        // 1111nnn1mmmm1000 fmov   xdn, [rm]
        this->assert_aligned(rm.u, 8);
        drn = this->mem->read_f64l(rm.u);
      } else { // 1111nnnnmmmm1000 fmov.s frn, [rm]
        this->assert_aligned(rm.u, 4);
        frn = this->mem->read_f32l(rm.u);
      }
      break;
    case 0x9:
      if (this->regs.fpscr_sz()) {
        // 1111nnn0mmmm1001 fmov   drn, [rm]+
        // 1111nnn1mmmm1001 fmov   xdn, [rm]+
        this->assert_aligned(rm.u, 8);
        drn = this->mem->read_f64l(rm.u);
        rm.u += 8;
      } else { // 1111nnnnmmmm1001 fmov.s frn, [rm]+
        this->assert_aligned(rm.u, 4);
        frn = this->mem->read_f32l(rm.u);
        rm.u += 4;
      }
      break;
    case 0xA:
      if (this->regs.fpscr_sz()) {
        // 1111nnnnmmm01010 fmov   [rn], drm
        // 1111nnnnmmm11010 fmov   [rn], xdm
        this->assert_aligned(rn.u, 8);
        this->mem->write_f64l(rn.u, drm);
      } else { // 1111nnnnmmmm1010 fmov.s [rn], frm
        this->assert_aligned(rn.u, 4);
        this->mem->write_f32l(rn.u, frm);
      }
      break;
    case 0xB:
      if (this->regs.fpscr_sz()) {
        // 1111nnnnmmm01011 fmov   -[rn], drm
        // 1111nnnnmmm11011 fmov   -[rn], xdm
        this->assert_aligned(rn.u, 8);
        rm.u -= 8;
        this->mem->write_f64l(rn.u, drm);
      } else { // 1111nnnnmmmm1011 fmov.s -[rn], frm
        this->assert_aligned(rn.u, 4);
        rm.u -= 4;
        this->mem->write_f32l(rn.u, frm);
      }
      break;
    case 0xC:
      if (this->regs.fpscr_sz()) {
        // 1111nnn0mmm01100 fmov   drn, drm
        // 1111nnn0mmm11100 fmov   drn, xdm
        // 1111nnn1mmm01100 fmov   xdn, drm
        // 1111nnn1mmm11100 fmov   xdn, xdm
        drn = drm;
      } else { // 1111nnnnmmmm1100 fmov   frn, frm
        frn = frm;
      }
      break;
    case 0xD:
      switch (op_get_r2(op)) {
        case 0x0: // 1111nnnn00001101 fsts   frm, fpul
          frn = this->regs.fpul_f;
          break;
        case 0x1: // 1111mmmm00011101 flds   fpul, frm
          this->regs.fpul_f = frn;
          break;
        case 0x2:
          if (this->regs.fpscr_pr()) { // 1111nnn000101101 float  drn, fpul
            drn = this->regs.fpul_i;
          } else { // 1111nnnn00101101 float  frn, fpul
            frn = this->regs.fpul_i;
          }
          break;
        case 0x3:
          if (this->regs.fpscr_pr()) { // 1111mmm000111101 ftrc   fpul, drn
            this->regs.fpul_i = drn;
          } else { // 1111mmmm00111101 ftrc   fpul, frm
            this->regs.fpul_i = frn;
          }
          break;
        case 0x4:
          if (this->regs.fpscr_pr()) { // 1111nnn001001101 fneg   drn
            drn = -drn;
          } else { // 1111nnnn01001101 fneg   frn
            frn = -frn;
          }
          break;
        case 0x5:
          if (this->regs.fpscr_pr()) { // 1111nnn001011101 fabs   drn
            drn = abs(drn);
          } else { // 1111nnnn01011101 fabs   frn
            frn = fabs(frn);
          }
          break;
        case 0x6:
          if (this->regs.fpscr_pr()) { // 1111nnn001101101 fsqrt  drn
            drn = sqrt(drn);
          } else { // 1111nnnn01101101 fsqrt  frn
            frn = sqrtf(frn);
          }
          break;
        case 0x8: // 1111nnnn10001101 fldi0  frn
          frn = 0.0f;
          break;
        case 0x9: // 1111nnnn10011101 fldi1  frn
          frn = 1.0f;
          break;
        case 0xA: // 1111nnn010101101 fcnvsd drn, fpul
          if (op & 0x0100) {
            throw runtime_error("invalid opcode");
          }
          drn = this->regs.fpul_f;
          break;
        case 0xB: // 1111mmm010111101 fcnvds fpul, drm
          if (op & 0x0100) {
            throw runtime_error("invalid opcode");
          }
          this->regs.fpul_f = drn;
          break;
        case 0xE: // 1111nnmm11101101 fipr   fvn, fvm  # fs(n+3) = dot(fvn, fvm)
          // TODO
          throw runtime_error("floating-point vector opcodes not yet implemented");
        case 0xF:
          if ((op & 0x0300) == 0x0100) {
            // 1111nn0111111101 ftrv   fvn, xmtrx
            // TODO
            throw runtime_error("floating-point vector opcodes not yet implemented");
          } else if ((op & 0x0300) == 0x0300) {
            // 1111001111111101 fschg
            // 1111101111111101 frchg
            throw runtime_error("floating-point control bit changes not yet implemented");
          } else {
            throw runtime_error("invalid opcode");
          }
          break;
        default:
          throw runtime_error("invalid opcode");
      }
      break;
    case 0xE: // 1111nnnnmmmm1110 fmac   frn, frm  # frn += fs0 * frm
      throw runtime_error("fmac opcode not yet implemented");
    default:
      throw runtime_error("invalid opcode");
  }
}

void SH4Emulator::execute_one(uint16_t op) {
  switch (op_get_op(op)) {
    case 0x0:
      this->execute_one_0(op);
      break;
    case 0x1:
      this->execute_one_1(op);
      break;
    case 0x2:
      this->execute_one_2(op);
      break;
    case 0x3:
      this->execute_one_3(op);
      break;
    case 0x4:
      this->execute_one_4(op);
      break;
    case 0x5:
      this->execute_one_5(op);
      break;
    case 0x6:
      this->execute_one_6(op);
      break;
    case 0x7:
      this->execute_one_7(op);
      break;
    case 0x8:
      this->execute_one_8(op);
      break;
    case 0x9:
      this->execute_one_9(op);
      break;
    case 0xA:
    case 0xB:
      this->execute_one_A_B(op);
      break;
    case 0xC:
      this->execute_one_C(op);
      break;
    case 0xD:
      this->execute_one_D(op);
      break;
    case 0xE:
      this->execute_one_E(op);
      break;
    case 0xF:
      this->execute_one_F(op);
      break;
  }
}

void SH4Emulator::execute() {
  for (;;) {
    try {
      if (this->debug_hook) {
        try {
          this->debug_hook(*this);
        } catch (const terminate_emulation&) {
          break;
        }
      }
      this->assert_aligned(this->regs.pc, 2);
      this->execute_one(this->mem->read_u16l(this->regs.pc));
      this->instructions_executed++;

      switch (this->regs.instructions_until_branch ? Regs::PendingBranchType::NONE : this->regs.pending_branch_type) {
        case Regs::PendingBranchType::NONE:
          this->regs.pc += 2;
          break;
        case Regs::PendingBranchType::CALL:
          this->regs.pr = this->regs.pc + 2;
          [[fallthrough]];
        case Regs::PendingBranchType::BRANCH:
          this->regs.pc = this->regs.pending_branch_target;
          this->regs.pending_branch_type = Regs::PendingBranchType::NONE;
          break;
        case Regs::PendingBranchType::RETURN:
          this->regs.pc = this->regs.pr;
          this->regs.pending_branch_type = Regs::PendingBranchType::NONE;
          break;
        default:
          throw logic_error("unimplemented branch type");
      }
      if (this->regs.instructions_until_branch) {
        this->regs.instructions_until_branch--;
      }
    } catch (const terminate_emulation&) {
      break;
    }
  }
}

SH4Emulator::Assembler::Argument::Argument(const string& text, bool raw)
    : type(Type::UNKNOWN),
      reg_num(0),
      value(0) {
  if (text.empty()) {
    throw runtime_error("argument text is blank");
  }
  if (raw) {
    this->type = Type::RAW;
    this->label_name = text;
    return;
  }

  if (text[0] == 'r') {
    try {
      bool is_banked = text.back() == 'b';
      this->reg_num = stoul(text.substr(1, text.size() - (is_banked ? 2 : 1)));
      this->type = is_banked ? Type::BANK_INT_REGISTER : Type::INT_REGISTER;
      check_range_t<uint8_t>(this->reg_num, 0, 15);
      return;
    } catch (const invalid_argument&) {
    }
  }

  if (text.size() >= 3) {
    if (text.starts_with("fr")) {
      try {
        this->reg_num = stoul(text.substr(2));
        this->type = Type::FR_REGISTER;
        check_range_t<uint8_t>(this->reg_num, 0, 15);
        return;
      } catch (const invalid_argument&) {
      }
    } else if (text.starts_with("dr")) {
      try {
        this->reg_num = stoul(text.substr(2));
        this->type = Type::DR_REGISTER;
        check_range_t<uint8_t>(this->reg_num, 0, 15);
        if (this->reg_num & 1) {
          throw runtime_error("invalid double-precision float register number");
        }
        return;
      } catch (const invalid_argument&) {
      }
    } else if (text.starts_with("xd")) {
      try {
        this->reg_num = stoul(text.substr(2));
        this->type = Type::XD_REGISTER;
        check_range_t<uint8_t>(this->reg_num, 0, 15);
        if (this->reg_num & 1) {
          throw runtime_error("invalid extended float register number");
        }
        return;
      } catch (const invalid_argument&) {
      }
    } else if (text.starts_with("fv")) {
      try {
        this->reg_num = stoul(text.substr(2));
        this->type = Type::FV_REGISTER;
        check_range_t<uint8_t>(this->reg_num, 0, 15);
        if (this->reg_num & 3) {
          throw runtime_error("invalid vector register number");
        }
        return;
      } catch (const invalid_argument&) {
      }
    }
  }

  if (text == "xmtrx") {
    this->type = Type::XMTRX;
    return;
  } else if (text == "sr") {
    this->type = Type::SR;
    return;
  } else if (text == "mach") {
    this->type = Type::MACH;
    return;
  } else if (text == "macl") {
    this->type = Type::MACL;
    return;
  } else if (text == "gbr") {
    this->type = Type::GBR;
    return;
  } else if (text == "vbr") {
    this->type = Type::VBR;
    return;
  } else if (text == "dbr") {
    this->type = Type::DBR;
    return;
  } else if (text == "pr") {
    this->type = Type::PR;
    return;
  } else if (text == "ssr") {
    this->type = Type::SSR;
    return;
  } else if (text == "sgr") {
    this->type = Type::SGR;
    return;
  } else if (text == "spc") {
    this->type = Type::SPC;
    return;
  } else if (text == "fpul") {
    this->type = Type::FPUL;
    return;
  } else if (text == "fpscr") {
    this->type = Type::FPSCR;
    return;
  } else if (text == "t") {
    this->type = Type::T;
    return;
  }

  if (text.starts_with("-[r") && text.ends_with("]")) {
    try {
      this->reg_num = stoul(text.substr(3, text.size() - 4));
      this->type = Type::PREDEC_MEMORY_REFERENCE;
      check_range_t<uint8_t>(this->reg_num, 0, 15);
      return;
    } catch (const invalid_argument&) {
    }
  } else if (text.starts_with("[r") && text.ends_with("]+")) {
    try {
      this->reg_num = stoul(text.substr(2, text.size() - 4));
      this->type = Type::POSTINC_MEMORY_REFERENCE;
      check_range_t<uint8_t>(this->reg_num, 0, 15);
      return;
    } catch (const invalid_argument&) {
    }
  } else if (text.starts_with("[") && text.ends_with("]")) {
    string inner_text = text.substr(1, text.size() - 2);
    strip_whitespace(inner_text);

    string expr1, expr2;
    bool is_subtract = 0;
    size_t arithmetic_operator_pos = inner_text.find_first_of("+-");
    if (arithmetic_operator_pos != string::npos) {
      is_subtract = (inner_text[arithmetic_operator_pos] == '-');
      expr1 = inner_text.substr(0, arithmetic_operator_pos);
      expr2 = inner_text.substr(arithmetic_operator_pos + 1);
      strip_whitespace(expr1);
      strip_whitespace(expr2);
    } else {
      expr1 = std::move(inner_text);
    }

    // All memory references have two exprs except the [rN] and [label] forms
    if (arithmetic_operator_pos == string::npos) {
      if (expr1.starts_with("r")) {
        try {
          this->reg_num = stoul(expr1.substr(1));
          this->type = Type::MEMORY_REFERENCE;
          check_range_t<uint8_t>(this->reg_num, 0, 15);
          return;
        } catch (const invalid_argument&) {
        }
      }
      if (expr1 == "gbr") {
        this->value = 0;
        this->type = Type::GBR_DISP_MEMORY_REFERENCE;
        return;
      }
      if (expr1.starts_with("0x")) {
        size_t end_pos;
        this->value = stoul(expr1, &end_pos, 0);
        if (end_pos != expr1.size()) {
          throw runtime_error("invalid absolute memory reference");
        }
      } else {
        if (expr1.empty()) {
          throw runtime_error("address expression is empty");
        }
        this->label_name = expr1;
      }
      this->type = Type::PC_MEMORY_REFERENCE;
      return;
    }

    // Figure out which token is the base and which is the index
    string base_expr, index_expr;
    // One token must be of the form rN or gbr
    bool expr1_is_reg = (is_reg_name(expr1) || (expr1 == "gbr"));
    bool expr2_is_reg = (is_reg_name(expr2) || (expr2 == "gbr"));
    // If both are regs, the one that isn't r0 is the base register
    if (expr1_is_reg && expr2_is_reg) {
      if (is_subtract) {
        throw runtime_error("invalid memory reference");
      }
      if (expr1 == "r0") {
        base_expr = std::move(expr2);
        index_expr = std::move(expr1);
      } else {
        base_expr = std::move(expr1);
        index_expr = std::move(expr2);
      }
    } else if (expr1_is_reg) {
      base_expr = std::move(expr1);
      index_expr = std::move(expr2);
    } else if (expr2_is_reg) {
      if (is_subtract) {
        throw runtime_error("invalid memory reference");
      }
      base_expr = std::move(expr2);
      index_expr = std::move(expr1);
    } else {
      throw runtime_error("invalid indexed memory reference");
    }

    // Parse the base reg
    bool base_is_gbr = (base_expr == "gbr");
    if (!base_is_gbr) {
      this->reg_num = stoul(base_expr.substr(1));
      check_range_t<uint8_t>(this->reg_num, 0, 15);
    }

    // Parse the index expr
    if (index_expr == "r0") {
      if (is_subtract) {
        throw runtime_error("invalid memory reference");
      }
      this->type = base_is_gbr ? Type::GBR_R0_MEMORY_REFERENCE : Type::REG_R0_MEMORY_REFERENCE;
    } else {
      if (isdigit(index_expr[0])) {
        this->type = base_is_gbr ? Type::GBR_DISP_MEMORY_REFERENCE : Type::REG_DISP_MEMORY_REFERENCE;
        this->value = (is_subtract ? (-1) : 1) * stoll(index_expr, nullptr, 0);
      } else {
        if (is_subtract || base_is_gbr) {
          throw runtime_error("invalid memory reference");
        }
        this->type = Type::PC_INDEX_MEMORY_REFERENCE;
        this->label_name = std::move(index_expr);
      }
    }
    return;
  }

  // Check for PC-relative offsets (NOT memory references)
  // These are of the form "<label> + rN"
  size_t plus_offset = text.find('+');
  if (plus_offset != string::npos) {
    string expr1 = text.substr(0, plus_offset);
    string expr2 = text.substr(plus_offset + 1);
    strip_whitespace(expr1);
    strip_whitespace(expr2);
    bool expr1_is_reg = is_reg_name(expr1) && (expr2 == "npc");
    bool expr2_is_reg = is_reg_name(expr2) && (expr1 == "npc");
    if (expr1_is_reg != expr2_is_reg) {
      this->type = Type::PC_REG_OFFSET;
      this->reg_num = stoll((expr1_is_reg ? expr1 : expr2).substr(1));
      return;
    }
  }

  // Check for immediate values
  try {
    size_t end_pos = 0;
    this->value = stoll(text, &end_pos, 0);
    if ((end_pos == text.size()) && !text.empty()) {
      this->reg_num = ((text[0] == '-') || (text[0] == '+')) ? 1 : 0;
      this->type = Type::IMMEDIATE;
      return;
    } else {
      this->value = 0;
    }
  } catch (const invalid_argument&) {
  }

  // If we really can't figure out what it is, assume it's a branch target
  this->label_name = text;
  this->type = Type::BRANCH_TARGET;
}

const char* SH4Emulator::Assembler::Argument::name_for_argument_type(Type type) {
  switch (type) {
    case ArgType::UNKNOWN:
      return "UNKNOWN";
    case ArgType::INT_REGISTER:
      return "INT_REGISTER";
    case ArgType::BANK_INT_REGISTER:
      return "BANK_INT_REGISTER";
    case ArgType::MEMORY_REFERENCE:
      return "MEMORY_REFERENCE";
    case ArgType::PREDEC_MEMORY_REFERENCE:
      return "PREDEC_MEMORY_REFERENCE";
    case ArgType::POSTINC_MEMORY_REFERENCE:
      return "POSTINC_MEMORY_REFERENCE";
    case ArgType::REG_R0_MEMORY_REFERENCE:
      return "REG_R0_MEMORY_REFERENCE";
    case ArgType::GBR_R0_MEMORY_REFERENCE:
      return "GBR_R0_MEMORY_REFERENCE";
    case ArgType::REG_DISP_MEMORY_REFERENCE:
      return "REG_DISP_MEMORY_REFERENCE";
    case ArgType::GBR_DISP_MEMORY_REFERENCE:
      return "GBR_DISP_MEMORY_REFERENCE";
    case ArgType::PC_MEMORY_REFERENCE:
      return "PC_MEMORY_REFERENCE";
    case ArgType::PC_INDEX_MEMORY_REFERENCE:
      return "PC_INDEX_MEMORY_REFERENCE";
    case ArgType::FR_DR_REGISTER:
      return "FR_DR_REGISTER";
    case ArgType::DR_XD_REGISTER:
      return "DR_XD_REGISTER";
    case ArgType::FR_DR_XD_REGISTER:
      return "FR_DR_XD_REGISTER";
    case ArgType::FR_REGISTER:
      return "FR_REGISTER";
    case ArgType::DR_REGISTER:
      return "DR_REGISTER";
    case ArgType::FV_REGISTER:
      return "FV_REGISTER";
    case ArgType::XD_REGISTER:
      return "XD_REGISTER";
    case ArgType::XMTRX:
      return "XMTRX";
    case ArgType::IMMEDIATE:
      return "IMMEDIATE";
    case ArgType::SR:
      return "SR";
    case ArgType::MACH:
      return "MACH";
    case ArgType::MACL:
      return "MACL";
    case ArgType::GBR:
      return "GBR";
    case ArgType::VBR:
      return "VBR";
    case ArgType::DBR:
      return "DBR";
    case ArgType::PR:
      return "PR";
    case ArgType::SSR:
      return "SSR";
    case ArgType::SGR:
      return "SGR";
    case ArgType::SPC:
      return "SPC";
    case ArgType::FPUL:
      return "FPUL";
    case ArgType::FPSCR:
      return "FPSCR";
    case ArgType::T:
      return "T";
    case ArgType::BRANCH_TARGET:
      return "BRANCH_TARGET";
    default:
      return "__UNKNOWN__";
  }
}

void SH4Emulator::Assembler::StreamItem::check_arg_types(std::initializer_list<ArgType> types) const {
  if (this->args.size() < types.size()) {
    throw runtime_error("not enough arguments to opcode");
  }
  if (this->args.size() > types.size()) {
    throw runtime_error("too many arguments to opcode");
  }
  size_t z = 0;
  for (auto et : types) {
    auto at = this->args[z].type;
    if ((at == et) ||
        (at == ArgType::IMMEDIATE && et == ArgType::BRANCH_TARGET) ||
        (at == ArgType::FR_REGISTER && et == ArgType::FR_DR_REGISTER) ||
        (at == ArgType::DR_REGISTER && et == ArgType::FR_DR_REGISTER) ||
        (at == ArgType::DR_REGISTER && et == ArgType::DR_XD_REGISTER) ||
        (at == ArgType::XD_REGISTER && et == ArgType::DR_XD_REGISTER) ||
        (at == ArgType::FR_REGISTER && et == ArgType::FR_DR_XD_REGISTER) ||
        (at == ArgType::DR_REGISTER && et == ArgType::FR_DR_XD_REGISTER) ||
        (at == ArgType::XD_REGISTER && et == ArgType::FR_DR_XD_REGISTER)) {
      z++;
      continue;
    }
    throw runtime_error(std::format("incorrect type for argument {} (expected {}, received {})",
        z, Argument::name_for_argument_type(et), Argument::name_for_argument_type(at)));
  }
}

bool SH4Emulator::Assembler::StreamItem::check_2_same_float_regs() const {
  try {
    this->check_arg_types({ArgType::FR_REGISTER, ArgType::FR_REGISTER});
    return false;
  } catch (const runtime_error&) {
    this->check_arg_types({ArgType::DR_REGISTER, ArgType::DR_REGISTER});
    return true;
  }
}

[[nodiscard]] bool SH4Emulator::Assembler::StreamItem::arg_types_match(std::initializer_list<Argument::Type> types) const {
  try {
    this->check_arg_types(types);
    return true;
  } catch (const runtime_error&) {
    return false;
  }
}

[[noreturn]] void SH4Emulator::Assembler::StreamItem::throw_invalid_arguments() const {
  string message = "invalid arguments (types: ";
  for (const auto& arg : this->args) {
    message += Argument::name_for_argument_type(arg.type);
    message += ", ";
  }
  if (message.ends_with(", ")) {
    message.resize(message.size() - 2);
  }
  message.push_back(')');
  throw runtime_error(message);
}

static std::string dasm_disp(uint8_t base_reg_num, int32_t disp) {
  if (disp == 0) {
    // TODO: Remove the + 0 here.
    return std::format("[r{} + 0]", base_reg_num);
  } else if (disp > 0) {
    return std::format("[r{} + 0x{:X}]", base_reg_num, disp);
  } else {
    return std::format("[r{} - 0x{:X}]", base_reg_num, -disp);
  }
}

static std::string dasm_disp_gbr(int32_t disp) {
  if (disp == 0) {
    return "[gbr]";
  } else if (disp > 0) {
    return std::format("[gbr + 0x{:X}]", disp);
  } else {
    return std::format("[gbr - 0x{:X}]", -disp);
  }
}

static std::string dasm_b_target(uint32_t pc, int32_t disp) {
  disp += 4;
  if (disp == 0) {
    return std::format("+0x0 // {:08X}", pc + disp);
  } else if (disp > 0) {
    return std::format("+0x{:X} // {:08X}", disp, pc + disp);
  } else {
    return std::format("-0x{:X} // {:08X}", -disp, pc + disp);
  }
}

static std::string dasm_imm(int32_t value) {
  if (value < 0) {
    return std::format("-0x{:X}", -value);
  } else {
    return std::format("0x{:X}", value);
  }
}

std::string SH4Emulator::disassemble_one(DisassemblyState& s, uint16_t op) {
  switch (op_get_op(op)) {
    case 0x0:
      switch (op_get_r3(op)) {
        case 0x2: {
          static const array<const char*, 5> reg_names = {"sr", "gbr", "vbr", "ssr", "spc"};
          uint8_t reg1 = op_get_r1(op);
          uint8_t reg2 = op_get_r2(op);
          if (reg2 < reg_names.size()) {
            // 0000nnnn00000010 stc    rn, sr
            // 0000nnnn00010010 stc    rn, gbr
            // 0000nnnn00100010 stc    rn, vbr
            // 0000nnnn00110010 stc    rn, ssr
            // 0000nnnn01000010 stc    rn, spc
            return std::format("stc     r{}, {}", reg1, reg_names[reg2]);
          } else if (reg2 & 8) {
            // 0000nnnn1mmm0010 stc    rn, rmb
            return std::format("stc     r{}, r{}b", reg1, static_cast<uint8_t>(reg2 & 7));
          }
          break;
        }
        case 0x3:
          switch (op_get_r2(op)) {
            case 0x0: // 0000nnnn00000011 calls  (pc + 4 + rn)
              return std::format("calls   npc + r{} // 0x{:08X} + r{}", op_get_r1(op), s.pc + 4, op_get_r1(op));
            case 0x2: // 0000nnnn00100011 bs     (pc + 4 + rn)
              return std::format("bs      npc + r{} // 0x{:08X} + r{}", op_get_r1(op), s.pc + 4, op_get_r1(op));
            case 0x8: // 0000nnnn10000011 pref   [rn]  # prefetch
              return std::format("pref    [r{}]", op_get_r1(op));
            case 0x9: // 0000nnnn10010011 ocbi   [rn]  # dcbi
              return std::format("ocbi    [r{}]", op_get_r1(op));
            case 0xA: // 0000nnnn10100011 ocbp   [rn]  # dcbf
              return std::format("ocbp    [r{}]", op_get_r1(op));
            case 0xB: // 0000nnnn10110011 ocbwb  [rn]  # dcbst?
              return std::format("ocbwb   [r{}]", op_get_r1(op));
            case 0xC: // 0000nnnn11000011 movca.l [rn], r0
              return std::format("movca.l [r{}], r0", op_get_r1(op));
          }
          break;
        case 0x4: // 0000nnnnmmmm0100 mov.b  [r0 + rn], rm
          return std::format("mov.b   [r{} + r0], r{}", op_get_r1(op), op_get_r2(op));
        case 0x5: // 0000nnnnmmmm0101 mov.w  [r0 + rn], rm
          return std::format("mov.w   [r{} + r0], r{}", op_get_r1(op), op_get_r2(op));
        case 0x6: // 0000nnnnmmmm0110 mov.l  [r0 + rn], rm
          return std::format("mov.l   [r{} + r0], r{}", op_get_r1(op), op_get_r2(op));
        case 0x7: // 0000nnnnmmmm0111 mul.l  rn, rm // macl = rn * rm
          return std::format("mul.l   r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0x8:
          if (op_get_r1(op) == 0) {
            switch (op_get_r2(op)) {
              case 0x0: // 0000000000001000 clrt
                return "clrt";
              case 0x1: // 0000000000011000 sett
                return "sett";
              case 0x2: // 0000000000101000 clrmac
                return "clrmac";
              case 0x3: // 0000000000111000 ldtlb
                return "ldtlb";
              case 0x4: // 0000000001001000 clrs
                return "clrs";
              case 0x5: // 0000000001011000 sets
                return "sets";
            }
          }
          break;
        case 0x9:
          switch (op_get_r2(op)) {
            case 0x0: // 0000000000001001 nop
              if (op_get_r1(op) == 0) {
                return "nop";
              }
              break;
            case 0x1: // 0000000000011001 div0u
              if (op_get_r1(op) == 0) {
                return "div0u";
              }
              break;
            case 0x2: // 0000nnnn00101001 movt   rn, t
              return std::format("movt    r{}, t", op_get_r1(op));
          }
          break;
        case 0xA:
          switch (op_get_r2(op)) {
            case 0x0: // 0000nnnn00001010 sts    rn, mach
              return std::format("sts     r{}, mach", op_get_r1(op));
            case 0x1: // 0000nnnn00011010 sts    rn, macl
              return std::format("sts     r{}, macl", op_get_r1(op));
            case 0x2: // 0000nnnn00101010 sts    rn, pr
              return std::format("sts     r{}, pr", op_get_r1(op));
            case 0x3: // 0000nnnn00111010 stc    rn, sgr
              return std::format("stc     r{}, sgr", op_get_r1(op));
            case 0x5: // 0000nnnn01011010 sts    rn, fpul
              return std::format("sts     r{}, fpul", op_get_r1(op));
            case 0x6: // 0000nnnn01101010 sts    rn, fpscr
              return std::format("sts     r{}, fpscr", op_get_r1(op));
            case 0xF: // 0000nnnn11111010 stc    rn, dbr
              return std::format("stc     r{}, dbr", op_get_r1(op));
          }
          break;
        case 0xB:
          switch (op) {
            case 0x000B: // 0000000000001011 ret
              return "rets";
            case 0x001B: // 0000000000011011 sleep
              return "sleep";
            case 0x002B: // 0000000000101011 rte
              return "rte";
          }
          break;
        case 0xC: // 0000nnnnmmmm1100 mov.b  rn, [r0 + rm]  # sign-ext
          return std::format("mov.b   r{}, [r{} + r0]", op_get_r1(op), op_get_r2(op));
        case 0xD: // 0000nnnnmmmm1101 mov.w  rn, [r0 + rm]  # sign-ext
          return std::format("mov.w   r{}, [r{} + r0]", op_get_r1(op), op_get_r2(op));
        case 0xE: // 0000nnnnmmmm1110 mov.l  rn, [r0 + rm]
          return std::format("mov.l   r{}, [r{} + r0]", op_get_r1(op), op_get_r2(op));
        case 0xF: // 0000nnnnmmmm1111 mac.l  [rn]+, [rm]+  # mac = [rn] * [rm] + mac
          return std::format("mac.l   [r{}]+, [r{}]+", op_get_r1(op), op_get_r2(op));
      }
      break;

    case 0x1: { // 0001nnnnmmmmdddd mov.l  [rn + 4 * d], rm
      auto ref_str = dasm_disp(op_get_r1(op), op_get_uimm4(op) * 4);
      return std::format("mov.l   {}, r{}", ref_str, op_get_r2(op));
    }

    case 0x2:
      switch (op_get_r3(op)) {
        case 0x0: // 0010nnnnmmmm0000 mov.b  [rn], rm
          return std::format("mov.b   [r{}], r{}", op_get_r1(op), op_get_r2(op));
        case 0x1: // 0010nnnnmmmm0001 mov.w  [rn], rm
          return std::format("mov.w   [r{}], r{}", op_get_r1(op), op_get_r2(op));
        case 0x2: // 0010nnnnmmmm0010 mov.l  [rn], rm
          return std::format("mov.l   [r{}], r{}", op_get_r1(op), op_get_r2(op));
        case 0x4: // 0010nnnnmmmm0100 mov.b  -[rn], rm
          return std::format("mov.b   -[r{}], r{}", op_get_r1(op), op_get_r2(op));
        case 0x5: // 0010nnnnmmmm0101 mov.w  -[rn], rm
          return std::format("mov.w   -[r{}], r{}", op_get_r1(op), op_get_r2(op));
        case 0x6: // 0010nnnnmmmm0110 mov.l  -[rn], rm
          return std::format("mov.l   -[r{}], r{}", op_get_r1(op), op_get_r2(op));
        case 0x7: // 0010nnnnmmmm0111 div0s  rn, rm
          return std::format("div0s   r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0x8: // 0010nnnnmmmm1000 test   rn, rm
          return std::format("test    r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0x9: // 0010nnnnmmmm1001 and    rn, rm
          return std::format("and     r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0xA: // 0010nnnnmmmm1010 xor    rn, rm
          return std::format("xor     r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0xB: // 0010nnnnmmmm1011 or     rn, rm
          return std::format("or      r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0xC: // 0010nnnnmmmm1100 cmpstr rn, rm  # any bytes are equal
          return std::format("cmpstr  r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0xD: // 0010nnnnmmmm1101 xtrct  rn, rm  # rm.rn middle 32 bits -> rn
          return std::format("xtrct   r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0xE: // 0010nnnnmmmm1110 mulu.w rn, rm // macl = rn * rm
          return std::format("mulu.w  r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0xF: // 0010nnnnmmmm1111 muls.w rn, rm // macl = rn * rm
          return std::format("muls.w  r{}, r{}", op_get_r1(op), op_get_r2(op));
      }
      break;

    case 0x3: {
      // 0011nnnnmmmm0000 cmpeq  rn, rm
      // 0011nnnnmmmm0010 cmpae  rn, rm
      // 0011nnnnmmmm0011 cmpge  rn, rm
      // 0011nnnnmmmm0100 div1   rn, rm
      // 0011nnnnmmmm0101 dmulu.l rn, rm
      // 0011nnnnmmmm0110 cmpa   rn, rm
      // 0011nnnnmmmm0111 cmpgt  rn, rm
      // 0011nnnnmmmm1000 sub    rn, rm
      // 0011nnnnmmmm1010 subc   rn, rm
      // 0011nnnnmmmm1011 subv   rn, rm
      // 0011nnnnmmmm1100 add    rn, rm
      // 0011nnnnmmmm1101 dmuls.l rn, rm
      // 0011nnnnmmmm1110 addc   rn, rm
      // 0011nnnnmmmm1111 addv   rn, rm
      static const array<const char*, 0x10> names = {
          "cmpeq", nullptr, "cmpae", "cmpge", "div1", "dmulu.l", "cmpa", "cmpgt",
          "sub", nullptr, "subc", "subv", "add", "dmuls.l", "addc", "addv"};
      const char* name = names[op_get_r3(op)];
      if (name) {
        string ret = name;
        ret.resize(8, ' ');
        ret += std::format("r{}, r{}", op_get_r1(op), op_get_r2(op));
        return ret;
      }
      break;
    }

    case 0x4:
      switch (op_get_r3(op)) {
        case 0x0:
          switch (op_get_r2(op)) {
            case 0x0: // 0100nnnn00000000 shl    rn
              return std::format("shl     r{}", op_get_r1(op));
            case 0x1: // 0100nnnn00010000 dec    rn
              return std::format("dec     r{}", op_get_r1(op));
            case 0x2: // 0100nnnn00100000 shal   rn
              return std::format("shal    r{}", op_get_r1(op));
          }
          break;
        case 0x1:
          switch (op_get_r2(op)) {
            case 0x0: // 0100nnnn00000001 shr    rn
              return std::format("shr     r{}", op_get_r1(op));
            case 0x1: // 0100nnnn00010001 cmpge  rn, 0
              return std::format("cmpge   r{}, 0", op_get_r1(op));
            case 0x2: // 0100nnnn00100001 shar   rn
              return std::format("shar    r{}", op_get_r1(op));
          }
          break;
        case 0x2: {
          // 0100nnnn00000010 sts.l  -[rn], mach
          // 0100nnnn00010010 sts.l  -[rn], macl
          // 0100nnnn00100010 sts.l  -[rn], pr
          // 0100nnnn00110010 stc.l  -[rn], sgr
          // 0100nnnn01010010 sts.l  -[rn], fpul
          // 0100nnnn01100010 sts.l  -[rn], fpscr
          // 0100nnnn11110010 stc.l  -[rn], dbr
          static const array<const char*, 0x10> reg_names = {
              "mach", "macl", "pr", "sgr", nullptr, "fpul", "fpscr", nullptr,
              nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "dbr"};
          uint8_t reg2 = op_get_r2(op);
          const char* reg_name = reg_names[reg2];
          if (reg_name) {
            return std::format("st{}.l   -[r{}], {}", ((reg2 & 3) == 3) ? 'c' : 's', op_get_r1(op), reg_name);
          }
          break;
        }
        case 0x3: {
          static const array<const char*, 5> reg_names = {"sr", "gbr", "vbr", "ssr", "spc"};
          uint8_t reg2 = op_get_r2(op);
          if (reg2 < reg_names.size()) {
            // 0100nnnn00000011 stc.l  -[rn], sr
            // 0100nnnn00010011 stc.l  -[rn], gbr
            // 0100nnnn00100011 stc.l  -[rn], vbr
            // 0100nnnn00110011 stc.l  -[rn], ssr
            // 0100nnnn01000011 stc.l  -[rn], spc
            return std::format("stc.l   -[r{}], {}", op_get_r1(op), reg_names[reg2]);
          } else if (reg2 & 8) {
            // 0100nnnn1mmm0011 stc.l  -[rn], rmb
            return std::format("stc.l   -[r{}], r{}b", op_get_r1(op), static_cast<uint8_t>(reg2 & 7));
          }
          break;
        }
        case 0x4:
          if (!(op_get_r2(op) & 0xD)) {
            // 0100nnnn00000100 rol    rn
            // 0100nnnn00100100 rcl    rn
            return std::format("r{}l     r{}", op_get_r2(op) ? 'c' : 'o', op_get_r1(op));
          }
          break;
        case 0x5:
          switch (op_get_r2(op)) {
            case 0x0: // 0100nnnn00000101 ror    rn
              return std::format("ror     r{}", op_get_r1(op));
            case 0x1: // 0100nnnn00010101 cmpgt  rn, 0
              return std::format("cmpgt   r{}, 0", op_get_r1(op));
            case 0x2: // 0100nnnn00100101 rcr    rn
              return std::format("rcr     r{}", op_get_r1(op));
          }
          break;
        case 0x6: {
          static const array<const char*, 0x10> reg_names = {
              "mach", "macl", "pr", nullptr, nullptr, "fpul", "fpscr", nullptr,
              nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "dbr"};
          uint8_t reg2 = op_get_r2(op);
          const char* reg_name = reg_names[reg2];
          if (reg_name) {
            // 0100mmmm00000110 lds    mach, [rm]+
            // 0100mmmm00010110 lds    macl, [rm]+
            // 0100mmmm00100110 lds    pr, [rm]+
            // 0100mmmm01010110 lds.l  fpul, [rm]+
            // 0100mmmm01100110 lds.l  fpscr, [rm]+
            // 0100mmmm11110110 ldc.l  dbr, [rm]+
            return std::format("ld{}{}   {}, [r{}]+", (reg2 & 8) ? 'c' : 's', (reg2 & 4) ? ".l" : "  ", reg_name, op_get_r1(op));
          }
          break;
        }
        case 0x7: {
          static const array<const char*, 5> reg_names = {"sr", "gbr", "vbr", "ssr", "spc"};
          uint8_t reg2 = op_get_r2(op);
          if (reg2 < reg_names.size()) {
            // 0100mmmm00000111 ldc.l  sr, [rm]+
            // 0100mmmm00010111 ldc.l  gbr, [rm]+
            // 0100mmmm00100111 ldc.l  vbr, [rm]+
            // 0100mmmm00110111 ldc.l  ssr, [rm]+
            // 0100mmmm01000111 ldc.l  spc, [rm]+
            return std::format("ldc.l   {}, [r{}]+", reg_names[reg2], op_get_r1(op));
          } else if (reg2 & 8) {
            // 0100mmmm1nnn0111 ldc.l  rnb, [rm]+
            return std::format("ldc.l   r{}b, [r{}]+", static_cast<uint8_t>(reg2 & 7), op_get_r1(op));
          }
          break;
        }
        case 0x8:
        case 0x9: {
          static const array<uint8_t, 3> shifts = {2, 8, 16};
          uint8_t reg2 = op_get_r2(op);
          if (reg2 < shifts.size()) {
            // 0100nnnn00001000 shl    rn, 2
            // 0100nnnn00011000 shl    rn, 8
            // 0100nnnn00101000 shl    rn, 16
            // 0100nnnn00001001 shr    rn, 2
            // 0100nnnn00011001 shr    rn, 8
            // 0100nnnn00101001 shr    rn, 16
            bool is_shr = op_get_r3(op) & 1;
            return std::format("sh{}     r{}, {}", is_shr ? 'r' : 'l', op_get_r1(op), shifts[reg2]);
          }
          break;
        }
        case 0xA: {
          static const array<const char*, 0x10> reg_names = {
              "mach", "macl", "pr", nullptr, nullptr, "fpul", "fpscr", nullptr,
              nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "dbr"};
          uint8_t reg2 = op_get_r2(op);
          const char* reg_name = reg_names[reg2];
          if (reg_name) {
            // 0100mmmm00001010 lds    mach, rm
            // 0100mmmm00011010 lds    macl, rm
            // 0100mmmm00101010 lds    pr, rm
            // 0100mmmm01011010 lds    fpul, rm
            // 0100mmmm01101010 lds    fpscr, rm
            // 0100mmmm11111010 ldc    dbr, rm
            return std::format("ld{}     {}, r{}", (reg2 & 8) ? 'c' : 's', reg_name, op_get_r1(op));
          }
          break;
        }
        case 0xB: {
          static const array<const char*, 3> names = {"calls", "tas.b", "bs   "};
          uint8_t reg2 = op_get_r2(op);
          if (reg2 < names.size()) {
            // 0100nnnn00001011 calls  [rn]
            // 0100nnnn00011011 tas.b  [rn]
            // 0100nnnn00101011 bs     [rn]
            return std::format("{}   [r{}]", names[reg2], op_get_r1(op));
          }
          break;
        }
        case 0xC: // 0100nnnnmmmm1100 shad   rn, rm
        case 0xD: // 0100nnnnmmmm1101 shld   rn, rm
          return std::format("sh{}d    r{}, r{}", (op_get_r3(op) & 1) ? 'l' : 'a', op_get_r1(op), op_get_r2(op));
        case 0xE: {
          static const array<const char*, 5> reg_names = {"sr", "gbr", "vbr", "ssr", "spc"};
          uint8_t reg2 = op_get_r2(op);
          if (reg2 < reg_names.size()) {
            // 0100mmmm00001110 ldc    sr, rm
            // 0100mmmm00011110 ldc    gbr, rm
            // 0100mmmm00101110 ldc    vbr, rm
            // 0100mmmm00111110 ldc    ssr, rm
            // 0100mmmm01001110 ldc    spc, rm
            return std::format("ldc     {}, r{}", reg_names[reg2], op_get_r1(op));
          } else if (reg2 & 8) {
            // 0100mmmm1nnn1110 ldc    rnb, rm
            return std::format("ldc     r{}b, r{}", static_cast<uint8_t>(reg2 & 7), op_get_r1(op));
          }
          break;
        }
        case 0xF: // 0100nnnnmmmm1111 mac.w  [rn]+, [rm]+ // mac = [rn] * [rm] + mac
          return std::format("mac.w   [r{}]+, [r{}]+", op_get_r1(op), op_get_r2(op));
      }
      break;

    case 0x5: { // 0101nnnnmmmmdddd mov.l  rn, [rm + 4 * d]
      return std::format("mov.l   r{}, ", op_get_r1(op)) + dasm_disp(op_get_r2(op), op_get_uimm4(op) * 4);
    }

    case 0x6:
      switch (op_get_r3(op)) {
        case 0x0: // 0110nnnnmmmm0000 mov.b  rn, [rm]  # sign-ext
          return std::format("mov.b   r{}, [r{}]", op_get_r1(op), op_get_r2(op));
        case 0x1: // 0110nnnnmmmm0001 mov.w  rn, [rm]  # sign-ext
          return std::format("mov.w   r{}, [r{}]", op_get_r1(op), op_get_r2(op));
        case 0x2: // 0110nnnnmmmm0010 mov.l  rn, [rm]
          return std::format("mov.l   r{}, [r{}]", op_get_r1(op), op_get_r2(op));
        case 0x3: // 0110nnnnmmmm0011 mov    rn, rm
          return std::format("mov     r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0x4: // 0110nnnnmmmm0100 mov.b  rn, [rm]+  # sign-ext
          return std::format("mov.b   r{}, [r{}]+", op_get_r1(op), op_get_r2(op));
        case 0x5: // 0110nnnnmmmm0101 mov.w  rn, [rm]+  # sign-ext
          return std::format("mov.w   r{}, [r{}]+", op_get_r1(op), op_get_r2(op));
        case 0x6: // 0110nnnnmmmm0110 mov.l  rn, [rm]+
          return std::format("mov.l   r{}, [r{}]+", op_get_r1(op), op_get_r2(op));
        case 0x7: // 0110nnnnmmmm0111 not    rn, rm
          return std::format("not     r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0x8: // 0110nnnnmmmm1000 swap.b rn, rm  # swap lower 2 bytes
          return std::format("swap.b  r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0x9: // 0110nnnnmmmm1001 swap.w rn, rm  # swap words
          return std::format("swap.w  r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0xA: // 0110nnnnmmmm1010 negc   rn, rm
          return std::format("negc    r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0xB: // 0110nnnnmmmm1011 neg    rn, rm
          return std::format("neg     r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0xC: // 0110nnnnmmmm1100 extu.b rn, rm
          return std::format("extu.b  r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0xD: // 0110nnnnmmmm1101 extu.w rn, rm
          return std::format("extu.w  r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0xE: // 0110nnnnmmmm1110 exts.b rn, rm
          return std::format("exts.b  r{}, r{}", op_get_r1(op), op_get_r2(op));
        case 0xF: // 0110nnnnmmmm1111 exts.w rn, rm
          return std::format("exts.w  r{}, r{}", op_get_r1(op), op_get_r2(op));
      }
      break;

    case 0x7: // 0111nnnniiiiiiii add    rn, imm
      return std::format("add     r{}, ", op_get_r1(op)) + dasm_imm(op_get_simm8(op));

    case 0x8:
      switch (op_get_r1(op)) {
        case 0x0: // 10000000nnnndddd mov.b  [rn + d], r0
          return "mov.b   " + dasm_disp(op_get_r2(op), op_get_uimm4(op)) + ", r0";
        case 0x1: // 10000001nnnndddd mov.w  [rn + 2 * d], r0
          return "mov.w   " + dasm_disp(op_get_r2(op), 2 * op_get_uimm4(op)) + ", r0";
        case 0x4: // 10000100mmmmdddd mov.b  r0, [rm + d]  # sign-ext
          return "mov.b   r0, " + dasm_disp(op_get_r2(op), op_get_uimm4(op));
        case 0x5: // 10000101mmmmdddd mov.w  r0, [rm + 2 * d]  # sign-ext
          return "mov.w   r0, " + dasm_disp(op_get_r2(op), 2 * op_get_uimm4(op));
        case 0x8: // 10001000iiiiiiii cmpeq  r0, imm
          return "cmpeq   r0, " + dasm_imm(op_get_simm8(op));
        case 0x9: // 10001001dddddddd bt     (pc + 4 + 2 * d)  # branch if T = 1
        case 0xB: // 10001011dddddddd bf     (pc + 4 + 2 * d)  # branch if T = 0
        case 0xD: // 10001101dddddddd bts    (pc + 4 + 2 * d)  # branch after next ins if T = 1
        case 0xF: { // 10001111dddddddd bfs    (pc + 4 + 2 * d)  # branch after next ins if T = 0
          static const array<string, 4> names = {"bt ", "bf ", "bts", "bfs"};
          int32_t disp = 2 * op_get_simm8(op);
          s.branch_target_addresses.emplace(s.pc + 4 + disp, false);
          return names[(op >> 9) & 3] + "     " + dasm_b_target(s.pc, disp);
        }
      }
      break;

    case 0x9: { // 1001nnnndddddddd mov.w  rn, [pc + 4 + d * 2]
      uint32_t referenced_pc = s.pc + 4 + 2 * op_get_uimm8(op);
      string value_suffix;
      try {
        if (s.mem) {
          value_suffix = std::format(" /* 0x{:04X} */", s.mem->read_u16l(referenced_pc));
        } else {
          value_suffix = std::format(" /* 0x{:04X} */", s.r.pget_u16l(referenced_pc - s.start_pc));
        }
      } catch (const out_of_range&) {
        value_suffix = " /* reference out of range */";
      }
      return std::format("mov.w   r{}, [0x{:08X}]{}", op_get_r1(op), referenced_pc, value_suffix);
    }

    case 0xA: // 1010dddddddddddd bs     (pc + 4 + 2 * d)
    case 0xB: { // 1011dddddddddddd calls  (pc + 4 + 2 * d)
      int32_t disp = 2 * op_get_simm12(op);
      bool is_call = (op_get_op(op) & 1);
      s.branch_target_addresses.emplace(s.pc + 4 + disp, is_call);
      return (is_call ? "calls   " : "bs      ") + dasm_b_target(s.pc, disp);
    }

    case 0xC:
      switch (op_get_r1(op)) {
        case 0x0: // 11000000dddddddd mov.b  [gbr + d], r0
          return "mov.b   " + dasm_disp_gbr(op_get_uimm8(op)) + ", r0";
        case 0x1: // 11000001dddddddd mov.w  [gbr + 2 * d], r0
          return "mov.w   " + dasm_disp_gbr(2 * op_get_uimm8(op)) + ", r0";
        case 0x2: // 11000010dddddddd mov.l  [gbr + 4 * d], r0
          return "mov.l   " + dasm_disp_gbr(4 * op_get_uimm8(op)) + ", r0";
        case 0x3: // 11000011iiiiiiii trapa  imm
          return "trapa   " + dasm_imm(op_get_uimm8(op));
        case 0x4: // 11000100dddddddd mov.b  r0, [gbr + d]  # sign-ext
          return "mov.b   r0, " + dasm_disp_gbr(op_get_uimm8(op));
        case 0x5: // 11000101dddddddd mov.w  r0, [gbr + 2 * d]  # sign-ext
          return "mov.w   r0, " + dasm_disp_gbr(2 * op_get_uimm8(op));
        case 0x6: // 11000110dddddddd mov.l  r0, [gbr + 4 * d]
          return "mov.l   r0, " + dasm_disp_gbr(4 * op_get_uimm8(op));
        case 0x7: // 11000111dddddddd mova   r0, [(pc & ~3) + 4 + disp * 4]
          return std::format("mova    r0, [0x{:08X}]", static_cast<uint32_t>(s.pc & (~3)) + 4 + 4 * op_get_uimm8(op));
        case 0x8: // 11001000iiiiiiii test   r0, imm
        case 0x9: // 11001001iiiiiiii and    r0, imm
        case 0xA: // 11001010iiiiiiii xor    r0, imm
        case 0xB: { // 11001011iiiiiiii or     r0, imm
          static const array<const char*, 4> names = {"test", "and ", "xor ", "or  "};
          return std::format("{}    r0, ", names[op_get_r1(op) & 3]) + dasm_imm(op_get_uimm8(op));
        }
        case 0xC: // 11001100iiiiiiii test.b [r0 + gbr], imm
        case 0xD: // 11001101iiiiiiii and.b  [r0 + gbr], imm
        case 0xE: // 11001110iiiiiiii xor.b  [r0 + gbr], imm
        case 0xF: { // 11001111iiiiiiii or.b   [r0 + gbr], imm
          static const array<const char*, 4> names = {"test.b", "and.b ", "xor.b ", "or.b  "};
          return std::format("{}  [gbr + r0], ", names[op_get_r1(op) & 3]) + dasm_imm(op_get_uimm8(op));
        }
      }
      break;

    case 0xD: { // 1101nnnndddddddd mov.l  rn, [(pc & ~3) + 4 + d * 4]
      uint32_t referenced_pc = (s.pc & (~3)) + 4 + 4 * op_get_uimm8(op);
      string value_suffix;
      try {
        if (s.mem) {
          value_suffix = std::format(" /* 0x{:08X} */", s.mem->read_u32l(referenced_pc));
        } else {
          value_suffix = std::format(" /* 0x{:08X} */", s.r.pget_u32l(referenced_pc - s.start_pc));
        }
      } catch (const out_of_range&) {
        value_suffix = " /* reference out of range */";
      }
      return std::format("mov.l   r{}, [0x{:08X}]{}", op_get_r1(op), referenced_pc, value_suffix);
    }

    case 0xE: // 1110nnnniiiiiiii mov    rn, imm
      return std::format("mov     r{}, ", op_get_r1(op)) + dasm_imm(op_get_simm8(op));

    case 0xF: {
      char size_ch = s.double_precision ? 'd' : 'f';
      switch (op_get_r3(op)) {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3:
        case 0x4:
        case 0x5: {
          // 1111nnn0mmm00000 fadd   drn, drm
          // 1111nnnnmmmm0000 fadd   frn, frm
          // 1111nnn0mmm00001 fsub   drn, drm
          // 1111nnnnmmmm0001 fsub   frn, frm
          // 1111nnn0mmm00010 fmul   drn, drm
          // 1111nnnnmmmm0010 fmul   frn, frm
          // 1111nnn0mmm00011 fdiv   drn, drm
          // 1111nnnnmmmm0011 fdiv   frn, frm
          // 1111nnn0mmm00100 fcmpeq drn, drm
          // 1111nnnnmmmm0100 fcmpeq frn, frm
          // 1111nnn0mmm00101 fcmpgt drn, drm
          // 1111nnnnmmmm0101 fcmpgt frn, frm
          if (s.double_precision && (op & 0x0110)) {
            break;
          }
          static const array<const char*, 6> names = {"fadd  ", "fsub  ", "fmul  ", "fdiv  ", "fcmpeq", "fcmpgt"};
          return std::format("{}  {}r{}, {}r{}",
              names[op_get_r3(op)], size_ch, op_get_r1(op), size_ch, op_get_r2(op));
        }
        case 0x6:
        case 0x8: {
          // 1111nnn0mmmm0110 fmov   drn, [r0 + rm]
          // 1111nnn1mmmm0110 fmov   xdn, [r0 + rm]
          // 1111nnnnmmmm0110 fmov.s frn, [r0 + rm]
          // 1111nnn0mmmm1000 fmov   drn, [rm]
          // 1111nnn1mmmm1000 fmov   xdn, [rm]
          // 1111nnnnmmmm1000 fmov.s frn, [rm]
          const char* suffix = (op_get_r3(op) == 8) ? "" : " + r0";
          if (s.double_precision) {
            if (op & 0x0100) {
              return std::format("fmov    xd{}, [r{}{}]", static_cast<uint8_t>(op_get_r1(op) & 0xE), op_get_r2(op), suffix);
            } else {
              return std::format("fmov    dr{}, [r{}{}]", op_get_r1(op), op_get_r2(op), suffix);
            }
          } else {
            return std::format("fmov.s  fr{}, [r{}{}]", op_get_r1(op), op_get_r2(op), suffix);
          }
          break;
        }
        case 0x7:
        case 0xA: {
          // 1111nnnnmmm00111 fmov   [r0 + rn], drm
          // 1111nnnnmmm10111 fmov   [r0 + rn], xdm
          // 1111nnnnmmmm0111 fmov.s [r0 + rn], frm
          // 1111nnnnmmm01010 fmov   [rn], drm
          // 1111nnnnmmm11010 fmov   [rn], xdm
          // 1111nnnnmmmm1010 fmov.s [rn], frm
          const char* suffix = (op_get_r3(op) == 0xA) ? "" : " + r0";
          if (s.double_precision) {
            if (op & 0x0010) {
              return std::format("fmov    [r{}{}], xd{}", op_get_r1(op), suffix, static_cast<uint8_t>(op_get_r2(op) & 0xE));
            } else {
              return std::format("fmov    [r{}{}], dr{}", op_get_r1(op), suffix, op_get_r2(op));
            }
          } else {
            return std::format("fmov.s  [r{}{}], fr{}", op_get_r1(op), suffix, op_get_r2(op));
          }
          break;
        }
        case 0x9:
          // 1111nnn0mmmm1001 fmov   drn, [rm]+
          // 1111nnn1mmmm1001 fmov   xdn, [rm]+
          // 1111nnnnmmmm1001 fmov.s frn, [rm]+
          if (s.double_precision) {
            if (op & 0x0100) {
              return std::format("fmov    xd{}, [r{}]+", static_cast<uint8_t>(op_get_r1(op) & 0xE), op_get_r2(op));
            } else {
              return std::format("fmov    dr{}, [r{}]+", op_get_r1(op), op_get_r2(op));
            }
          } else {
            return std::format("fmov.s  fr{}, [r{}]+", op_get_r1(op), op_get_r2(op));
          }
          break;
        case 0xB:
          // 1111nnnnmmm01011 fmov   -[rn], drm
          // 1111nnnnmmm11011 fmov   -[rn], xdm
          // 1111nnnnmmmm1011 fmov.s -[rn], frm
          if (s.double_precision) {
            if (op & 0x0010) {
              return std::format("fmov    -[r{}], xd{}", op_get_r1(op), static_cast<uint8_t>(op_get_r2(op) & 0xE));
            } else {
              return std::format("fmov    -[r{}], dr{}", op_get_r1(op), op_get_r2(op));
            }
          } else {
            return std::format("fmov.s  -[r{}], fr{}", op_get_r1(op), op_get_r2(op));
          }
          break;
        case 0xC:
          if (s.double_precision) {
            // 1111nnn0mmm01100 fmov   drn, drm
            // 1111nnn0mmm11100 fmov   drn, xdm
            // 1111nnn1mmm01100 fmov   xdn, drm
            // 1111nnn1mmm11100 fmov   xdn, xdm
            uint8_t reg1 = op_get_r1(op);
            uint8_t reg2 = op_get_r2(op);
            return std::format("fmov    {}{}, {}{}",
                (reg1 & 1) ? "xd" : "dr", static_cast<uint8_t>(reg1 & 0xE),
                (reg2 & 1) ? "xd" : "dr", static_cast<uint8_t>(reg2 & 0xE));
          } else {
            // 1111nnnnmmmm1100 fmov   frn, frm
            return std::format("fmov    fr{}, fr{}", op_get_r1(op), op_get_r2(op));
          }
          break;
        case 0xD:
          switch (op_get_r2(op)) {
            case 0x0: // 1111nnnn00001101 fsts   frm, fpul
              return std::format("fsts    fr{}, fpul", op_get_r1(op));
            case 0x1: // 1111mmmm00011101 flds   fpul, frm
              return std::format("flds    fpul, fr{}", op_get_r1(op));
            case 0x2:
              // 1111nnn000101101 float  drn, fpul
              // 1111nnnn00101101 float  frn, fpul
              if (s.double_precision && (op & 0x0100)) {
                break;
              }
              return std::format("float   {}r{}, fpul", s.double_precision ? 'd' : 'f', op_get_r1(op));
            case 0x3:
              // 1111mmm000111101 ftrc   fpul, drn
              // 1111mmmm00111101 ftrc   fpul, frm
              if (s.double_precision && (op & 0x0100)) {
                break;
              }
              return std::format("ftrc    fpul, {}r{}", s.double_precision ? 'd' : 'f', op_get_r1(op));
            case 0x4:
              // 1111nnn001001101 fneg   drn
              // 1111nnnn01001101 fneg   frn
              if (s.double_precision && (op & 0x0100)) {
                break;
              }
              return std::format("fneg    {}r{}", s.double_precision ? 'd' : 'f', op_get_r1(op));
            case 0x5:
              // 1111nnn001011101 fabs   drn
              // 1111nnnn01011101 fabs   frn
              if (s.double_precision && (op & 0x0100)) {
                break;
              }
              return std::format("fabs    {}r{}", s.double_precision ? 'd' : 'f', op_get_r1(op));
            case 0x6:
              // 1111nnn001101101 fsqrt  drn
              // 1111nnnn01101101 fsqrt  frn
              if (s.double_precision && (op & 0x0100)) {
                break;
              }
              return std::format("fsqrt   {}r{}", s.double_precision ? 'd' : 'f', op_get_r1(op));
            case 0x8: // 1111nnnn10001101 fldi0  frn
              return std::format("fldi0   fr{}", op_get_r1(op));
            case 0x9: // 1111nnnn10011101 fldi1  frn
              return std::format("fldi1   fr{}", op_get_r1(op));
            case 0xA: // 1111nnn010101101 fcnvsd drn, fpul
              if (!s.double_precision || (op & 0x0100)) {
                break;
              }
              return std::format("fcnvsd  dr{}, fpul", op_get_r1(op));
            case 0xB: // 1111mmm010111101 fcnvds fpul, drm
              if (!s.double_precision || (op & 0x0100)) {
                break;
              }
              return std::format("fcnvds  fpul, dr{}", op_get_r1(op));
            case 0xE: // 1111nnmm11101101 fipr   fvn, fvm  # fs(n+3) = dot(fvn, fvm)
              return std::format("fipr    fv{}, fv{}", static_cast<uint8_t>(op_get_r1(op) & 0xC), static_cast<uint8_t>((op_get_r1(op) << 2) & 0xC));
            case 0xF: {
              uint8_t reg1 = op_get_r1(op);
              if ((reg1 & 0x3) == 0x1) {
                // 1111nn0111111101 ftrv   fvn, xmtrx
                return std::format("ftrv    fv{}, xmtrx", static_cast<uint8_t>(reg1 & 0xC));
              } else if (reg1 == 0x3) {
                // 1111001111111101 fschg
                return "fschg";
              } else if (reg1 == 0xB) {
                // 1111101111111101 frchg
                return "frchg";
              }
            }
          }
          break;
        case 0xE:
          // 1111nnnnmmmm1110 fmac   frn, frm  # frn += fs0 * frm
          return std::format("fmac    fr{}, fr{}", op_get_r1(op), op_get_r2(op));
      }
      break;
    }

    default:
      throw logic_error("invalid op field");
  }

  return ".invalid";
}

static constexpr uint16_t asm_op_imm12(uint8_t op, uint16_t imm) {
  return (op << 12) | (imm & 0xFFF);
}
static constexpr uint16_t asm_op_r1_imm8(uint8_t op, uint8_t r1, uint8_t imm) {
  return (op << 12) | ((r1 & 0x0F) << 8) | (imm & 0xFF);
}
static constexpr uint16_t asm_op_r1_r2_r3(uint8_t op, uint8_t r1, uint8_t r2, uint8_t r3) {
  return (op << 12) | ((r1 & 0x0F) << 8) | ((r2 & 0x0F) << 4) | (r3 & 0x0F);
}

uint16_t SH4Emulator::Assembler::asm_add_addc_addv_sub_subc_subv(const StreamItem& si) const {
  bool is_add = si.op_name.starts_with("add");
  bool is_sub = si.op_name.starts_with("sub");
  if ((!is_add && !is_sub) || (si.op_name.size() > 4)) {
    throw logic_error("add/sub called for incorrect opcode");
  }
  char suffix = (si.op_name.size() == 4) ? si.op_name[3] : 0;

  if (is_add && si.arg_types_match({ArgType::INT_REGISTER, ArgType::IMMEDIATE})) {
    // 0111nnnniiiiiiii add    rn, imm
    check_range_t(si.args[1].value, -0x80, 0x7F);
    return asm_op_r1_imm8(0x7, si.args[0].reg_num, si.args[1].value);
  }
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  if (suffix == 0) {
    // 0011nnnnmmmm1000 sub    rn, rm
    // 0011nnnnmmmm1100 add    rn, rm
    return asm_op_r1_r2_r3(0x3, si.args[0].reg_num, si.args[1].reg_num, is_sub ? 0x8 : 0xC);
  }
  if (suffix == 'c') {
    // 0011nnnnmmmm1010 subc   rn, rm
    // 0011nnnnmmmm1110 addc   rn, rm
    return asm_op_r1_r2_r3(0x3, si.args[0].reg_num, si.args[1].reg_num, is_sub ? 0xA : 0xE);
  }
  if (suffix == 'v') {
    // 0011nnnnmmmm1011 subv   rn, rm
    // 0011nnnnmmmm1111 addv   rn, rm
    return asm_op_r1_r2_r3(0x3, si.args[0].reg_num, si.args[1].reg_num, is_sub ? 0xB : 0xF);
  }
  throw logic_error("unhandled add/sub case");
}

uint16_t SH4Emulator::Assembler::asm_and_or(const StreamItem& si) const {
  if (si.arg_types_match({ArgType::INT_REGISTER, ArgType::INT_REGISTER})) {
    // 0010nnnnmmmm1001 and    rn, rm
    // 0010nnnnmmmm1011 or     rn, rm
    return asm_op_r1_r2_r3(0x2, si.args[0].reg_num, si.args[1].reg_num, (si.op_name == "or") ? 0xB : 0x9);
  }
  if (si.arg_types_match({ArgType::INT_REGISTER, ArgType::IMMEDIATE}) && si.args[0].reg_num == 0) {
    // 11001001iiiiiiii and    r0, imm
    // 11001011iiiiiiii or     r0, imm
    check_range_t(si.args[1].value, 0x00, 0xFF);
    return asm_op_r1_imm8(0xC, (si.op_name == "or") ? 0xB : 0x9, si.args[1].value);
  }
  si.throw_invalid_arguments();
}

uint16_t SH4Emulator::Assembler::asm_and_b_or_b(const StreamItem& si) const {
  // 11001101iiiiiiii and.b  [r0 + gbr], imm
  // 11001111iiiiiiii or.b   [r0 + gbr], imm
  si.check_arg_types({ArgType::GBR_R0_MEMORY_REFERENCE, ArgType::IMMEDIATE});
  check_range_t(si.args[1].value, 0x00, 0xFF);
  return asm_op_r1_imm8(0xC, (si.op_name == "or.b") ? 0xF : 0xD, si.args[1].value);
}

uint16_t SH4Emulator::Assembler::asm_bs_calls(const StreamItem& si) const {
  bool is_calls = (si.op_name == "calls");
  if (si.arg_types_match({ArgType::BRANCH_TARGET})) {
    // 1010dddddddddddd bs     (pc + 4 + 2 * d)
    // 1011dddddddddddd calls  (pc + 4 + 2 * d)
    uint32_t dest_offset = (si.args[0].type == ArgType::BRANCH_TARGET)
        ? this->label_offsets.at(si.args[0].label_name)
        : si.args[0].reg_num
        ? (si.offset + si.args[0].value)
        : si.args[0].value;
    int32_t delta = dest_offset - (si.offset + 4);
    if ((delta & 0xFFFFF001) != 0 && (delta & 0xFFFFF001) != 0xFFFFF000) {
      throw runtime_error("invalid branch target");
    }
    return asm_op_imm12(is_calls ? 0xB : 0xA, delta >> 1);

  } else if (si.arg_types_match({ArgType::PC_REG_OFFSET})) {
    // 0000nnnn00100011 bs     (pc + 4 + rn)
    // 0000nnnn00000011 calls  (pc + 4 + rn)
    return asm_op_r1_r2_r3(0x0, si.args[0].reg_num, is_calls ? 0x0 : 0x2, 0x3);

  } else if (si.arg_types_match({ArgType::MEMORY_REFERENCE})) {
    // 0100nnnn00101011 bs     [rn]
    // 0100nnnn00001011 calls  [rn]
    return asm_op_r1_r2_r3(0x4, si.args[0].reg_num, is_calls ? 0x0 : 0x2, 0xB);
  }

  si.throw_invalid_arguments();
}

uint16_t SH4Emulator::Assembler::asm_bt_bf_bts_bfs(const StreamItem& si) const {
  // 10001001dddddddd bt     (pc + 4 + 2 * d)  # branch if T = 1
  // 10001011dddddddd bf     (pc + 4 + 2 * d)  # branch if T = 0
  // 10001101dddddddd bts    (pc + 4 + 2 * d)  # branch after next ins if T = 1
  // 10001111dddddddd bfs    (pc + 4 + 2 * d)  # branch after next ins if T = 0
  si.check_arg_types({ArgType::BRANCH_TARGET});
  bool is_f = si.op_name[1] == 'f';
  bool is_s = si.op_name.size() == 3;
  uint32_t dest_offset = (si.args[0].type == ArgType::BRANCH_TARGET)
      ? this->label_offsets.at(si.args[0].label_name)
      : si.args[0].reg_num
      ? (si.offset + si.args[0].value)
      : si.args[0].value;
  int32_t delta = dest_offset - (si.offset + 4);
  if ((delta & 0xFFFFFF01) != 0 && (delta & 0xFFFFFF01) != 0xFFFFFF00) {
    throw runtime_error("invalid branch target");
  }
  return asm_op_r1_imm8(0x8, 0x9 | (is_s ? 0x4 : 0x0) | (is_f ? 0x2 : 0x0), delta >> 1);
}

uint16_t SH4Emulator::Assembler::asm_clrt(const StreamItem& si) const {
  // 0000000000001000 clrt
  si.check_arg_types({});
  return 0x0008;
}

uint16_t SH4Emulator::Assembler::asm_sett(const StreamItem& si) const {
  // 0000000000011000 sett
  si.check_arg_types({});
  return 0x0018;
}

uint16_t SH4Emulator::Assembler::asm_clrmac(const StreamItem& si) const {
  // 0000000000101000 clrmac
  si.check_arg_types({});
  return 0x0028;
}

uint16_t SH4Emulator::Assembler::asm_clrs(const StreamItem& si) const {
  // 0000000001001000 clrs
  si.check_arg_types({});
  return 0x0048;
}

uint16_t SH4Emulator::Assembler::asm_sets(const StreamItem& si) const {
  // 0000000001011000 sets
  si.check_arg_types({});
  return 0x0058;
}

uint16_t SH4Emulator::Assembler::asm_cmp_mnemonics(const StreamItem& si) const {
  if (si.arg_types_match({ArgType::INT_REGISTER, ArgType::INT_REGISTER})) {
    // 0011nnnnmmmm0110 cmpa   rn, rm
    // 0011nnnnmmmm0010 cmpae  rn, rm
    // 0011nnnnmmmm0000 cmpeq  rn, rm
    // 0011nnnnmmmm0011 cmpge  rn, rm
    // 0011nnnnmmmm0111 cmpgt  rn, rm
    // 0010nnnnmmmm1100 cmpstr rn, rm  # any bytes are equal
    if (si.op_name == "cmpa") {
      return asm_op_r1_r2_r3(0x3, si.args[0].reg_num, si.args[1].reg_num, 0x6);
    } else if (si.op_name == "cmpae") {
      return asm_op_r1_r2_r3(0x3, si.args[0].reg_num, si.args[1].reg_num, 0x2);
    } else if (si.op_name == "cmpe" || si.op_name == "cmpeq") {
      return asm_op_r1_r2_r3(0x3, si.args[0].reg_num, si.args[1].reg_num, 0x0);
    } else if (si.op_name == "cmpge") {
      return asm_op_r1_r2_r3(0x3, si.args[0].reg_num, si.args[1].reg_num, 0x3);
    } else if (si.op_name == "cmpgt") {
      return asm_op_r1_r2_r3(0x3, si.args[0].reg_num, si.args[1].reg_num, 0x7);
    } else if (si.op_name == "cmpstr") {
      return asm_op_r1_r2_r3(0x2, si.args[0].reg_num, si.args[1].reg_num, 0xC);
    } else {
      throw runtime_error("invalid cmp mnemonic");
    }
  }

  si.check_arg_types({ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  if (((si.op_name == "cmpeq") || (si.op_name == "cmpe")) && (si.args[0].reg_num == 0)) {
    // 10001000iiiiiiii cmpeq  r0, imm
    check_range_t(si.args[1].value, -0x80, 0x7F);
    return asm_op_r1_imm8(0x8, 0x8, si.args[1].value);
  }
  if (((si.op_name == "cmpgt") || (si.op_name == "cmpge")) && (si.args[1].value == 0)) {
    // 0100nnnn00010001 cmpge  rn, 0
    // 0100nnnn00010101 cmpgt  rn, 0
    return asm_op_r1_r2_r3(0x4, si.args[0].reg_num, 0x1, (si.op_name[4] == 't') ? 0x5 : 0x1);
  }

  si.throw_invalid_arguments();
}

uint16_t SH4Emulator::Assembler::asm_dec(const StreamItem& si) const {
  // 0100nnnn00010000 dec    rn
  si.check_arg_types({ArgType::INT_REGISTER});
  return asm_op_r1_imm8(0x4, si.args[0].reg_num, 0x10);
}

uint16_t SH4Emulator::Assembler::asm_div0s(const StreamItem& si) const {
  // 0010nnnnmmmm0111 div0s  rn, rm
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return asm_op_r1_r2_r3(0x2, si.args[0].reg_num, si.args[1].reg_num, 0x7);
}

uint16_t SH4Emulator::Assembler::asm_div0u(const StreamItem& si) const {
  // 0000000000011001 div0u
  si.check_arg_types({});
  return 0x0019;
}

uint16_t SH4Emulator::Assembler::asm_div1(const StreamItem& si) const {
  // 0011nnnnmmmm0100 div1   rn, rm
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return asm_op_r1_r2_r3(0x3, si.args[0].reg_num, si.args[1].reg_num, 0x4);
}

uint16_t SH4Emulator::Assembler::asm_dmuls_dmulu(const StreamItem& si) const {
  // 0011nnnnmmmm1101 dmuls.l rn, rm
  // 0011nnnnmmmm0101 dmulu.l rn, rm
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return asm_op_r1_r2_r3(0x3, si.args[0].reg_num, si.args[1].reg_num, (si.op_name.at(4) == 's') ? 0xD : 0x5);
}

uint16_t SH4Emulator::Assembler::asm_exts_extu(const StreamItem& si) const {
  // 0110nnnnmmmm1100 extu.b rn, rm
  // 0110nnnnmmmm1101 extu.w rn, rm
  // 0110nnnnmmmm1110 exts.b rn, rm
  // 0110nnnnmmmm1111 exts.w rn, rm
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return asm_op_r1_r2_r3(0x6, si.args[0].reg_num, si.args[1].reg_num,
      0xC | ((si.op_name.at(3) == 's') ? 0x2 : 0x0) | ((si.op_name.at(5) == 'w') ? 0x1 : 0x0));
}

uint16_t SH4Emulator::Assembler::asm_fabs(const StreamItem& si) const {
  // 1111nnn001011101 fabs   drn
  // 1111nnnn01011101 fabs   frn
  si.check_arg_types({ArgType::FR_DR_REGISTER});
  return asm_op_r1_imm8(0xF, si.args[0].reg_num, 0x5D);
}

uint16_t SH4Emulator::Assembler::asm_fadd(const StreamItem& si) const {
  // 1111nnn0mmm00000 fadd   drn, drm
  // 1111nnnnmmmm0000 fadd   frn, frm
  si.check_2_same_float_regs();
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num, si.args[1].reg_num, 0x0);
}

uint16_t SH4Emulator::Assembler::asm_fcmp_mnemonics(const StreamItem& si) const {
  // 1111nnn0mmm00100 fcmpeq drn, drm
  // 1111nnnnmmmm0100 fcmpeq frn, frm
  // 1111nnn0mmm00101 fcmpgt drn, drm
  // 1111nnnnmmmm0101 fcmpgt frn, frm
  si.check_2_same_float_regs();
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num, si.args[1].reg_num, (si.op_name.at(4) == 'g') ? 0x5 : 0x4);
}

uint16_t SH4Emulator::Assembler::asm_fcnvds(const StreamItem& si) const {
  // 1111mmm010111101 fcnvds fpul, drm
  si.check_arg_types({ArgType::FPUL, ArgType::DR_REGISTER});
  return asm_op_r1_r2_r3(0xF, si.args[1].reg_num, 0xB, 0xD);
}

uint16_t SH4Emulator::Assembler::asm_fcnvsd(const StreamItem& si) const {
  // 1111nnn010101101 fcnvsd drn, fpul
  si.check_arg_types({ArgType::DR_REGISTER, ArgType::FPUL});
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num, 0xA, 0xD);
}

uint16_t SH4Emulator::Assembler::asm_fdiv(const StreamItem& si) const {
  // 1111nnn0mmm00011 fdiv   drn, drm
  // 1111nnnnmmmm0011 fdiv   frn, frm
  si.check_2_same_float_regs();
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num, si.args[1].reg_num, 0x3);
}

uint16_t SH4Emulator::Assembler::asm_fipr(const StreamItem& si) const {
  // 1111nnmm11101101 fipr   fvn, fvm  # fs(n+3) = dot(fvn, fvm)
  si.check_arg_types({ArgType::FV_REGISTER, ArgType::FV_REGISTER});
  if ((si.args[0].reg_num & 0xF3) || (si.args[1].reg_num & 0xF3)) {
    throw runtime_error("invalid fv register number");
  }
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num | (si.args[1].reg_num >> 2), 0xE, 0xD);
}

uint16_t SH4Emulator::Assembler::asm_fldi0_fldi1(const StreamItem& si) const {
  // 1111nnnn10001101 fldi0  frn
  // 1111nnnn10011101 fldi1  frn
  si.check_arg_types({ArgType::FR_REGISTER});
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num, si.op_name.at(4) == '1' ? 0x9 : 0x8, 0xD);
}

uint16_t SH4Emulator::Assembler::asm_flds(const StreamItem& si) const {
  // 1111mmmm00011101 flds   fpul, frm
  si.check_arg_types({ArgType::FPUL, ArgType::FR_REGISTER});
  return asm_op_r1_r2_r3(0xF, si.args[1].reg_num, 0x1, 0xD);
}

uint16_t SH4Emulator::Assembler::asm_fsts(const StreamItem& si) const {
  // 1111nnnn00001101 fsts   frm, fpul
  si.check_arg_types({ArgType::FR_REGISTER, ArgType::FPUL});
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num, 0x0, 0xD);
}

uint16_t SH4Emulator::Assembler::asm_float(const StreamItem& si) const {
  // 1111nnn000101101 float  drn, fpul
  // 1111nnnn00101101 float  frn, fpul
  si.check_arg_types({ArgType::FR_DR_REGISTER, ArgType::FPUL});
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num, 0x2, 0xD);
}

uint16_t SH4Emulator::Assembler::asm_fmac(const StreamItem& si) const {
  // 1111nnnnmmmm1110 fmac   frn, frm  # frn += fs0 * frm
  si.check_arg_types({ArgType::FR_REGISTER, ArgType::FR_REGISTER});
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num, si.args[1].reg_num, 0xE);
}

uint16_t SH4Emulator::Assembler::asm_fmov_fmov_s(const StreamItem& si) const {
  if (si.args.size() < 2) {
    throw runtime_error("not enough arguments");
  }
  uint8_t reg1 = si.args[0].reg_num | ((si.args[0].type == ArgType::XD_REGISTER) ? 1 : 0);
  uint8_t reg2 = si.args[1].reg_num | ((si.args[1].type == ArgType::XD_REGISTER) ? 1 : 0);

  uint8_t subopcode = 0;
  if (si.arg_types_match({ArgType::PREDEC_MEMORY_REFERENCE, ArgType::FR_DR_XD_REGISTER})) {
    // 1111nnnnmmm01011 fmov   -[rn], drm
    // 1111nnnnmmm11011 fmov   -[rn], xdm
    // 1111nnnnmmmm1011 fmov.s -[rn], frm
    subopcode = 0xB;

  } else if (si.arg_types_match({ArgType::REG_R0_MEMORY_REFERENCE, ArgType::FR_DR_XD_REGISTER})) {
    // 1111nnnnmmm00111 fmov   [r0 + rn], drm
    // 1111nnnnmmm10111 fmov   [r0 + rn], xdm
    // 1111nnnnmmmm0111 fmov.s [r0 + rn], frm
    subopcode = 0x7;

  } else if (si.arg_types_match({ArgType::MEMORY_REFERENCE, ArgType::FR_DR_XD_REGISTER})) {
    // 1111nnnnmmm01010 fmov   [rn], drm
    // 1111nnnnmmm11010 fmov   [rn], xdm
    // 1111nnnnmmmm1010 fmov.s [rn], frm
    subopcode = 0xA;

  } else if (si.arg_types_match({ArgType::FR_DR_XD_REGISTER, ArgType::REG_R0_MEMORY_REFERENCE})) {
    // 1111nnn0mmmm0110 fmov   drn, [r0 + rm]
    // 1111nnn1mmmm0110 fmov   xdn, [r0 + rm]
    // 1111nnnnmmmm0110 fmov.s frn, [r0 + rm]
    subopcode = 0x6;

  } else if (si.arg_types_match({ArgType::FR_DR_XD_REGISTER, ArgType::MEMORY_REFERENCE})) {
    // 1111nnn0mmmm1000 fmov   drn, [rm]
    // 1111nnn1mmmm1000 fmov   xdn, [rm]
    // 1111nnnnmmmm1000 fmov.s frn, [rm]
    subopcode = 0x8;

  } else if (si.arg_types_match({ArgType::FR_DR_XD_REGISTER, ArgType::POSTINC_MEMORY_REFERENCE})) {
    // 1111nnn0mmmm1001 fmov   drn, [rm]+
    // 1111nnn1mmmm1001 fmov   xdn, [rm]+
    // 1111nnnnmmmm1001 fmov.s frn, [rm]+
    subopcode = 0x9;

  } else if (si.arg_types_match({ArgType::DR_XD_REGISTER, ArgType::DR_XD_REGISTER}) ||
      si.arg_types_match({ArgType::FR_REGISTER, ArgType::FR_REGISTER})) {
    // 1111nnn0mmm01100 fmov   drn, drm
    // 1111nnn0mmm11100 fmov   drn, xdm
    // 1111nnn1mmm01100 fmov   xdn, drm
    // 1111nnn1mmm11100 fmov   xdn, xdm
    // 1111nnnnmmmm1100 fmov   frn, frm
    subopcode = 0xC;
  }

  if (subopcode == 0) {
    throw runtime_error("incorrect argument types");
  }
  return asm_op_r1_r2_r3(0xF, reg1, reg2, subopcode);
}

uint16_t SH4Emulator::Assembler::asm_fmul(const StreamItem& si) const {
  // 1111nnn0mmm00010 fmul   drn, drm
  // 1111nnnnmmmm0010 fmul   frn, frm
  si.check_2_same_float_regs();
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num, si.args[1].reg_num, 0x2);
}

uint16_t SH4Emulator::Assembler::asm_fneg(const StreamItem& si) const {
  // 1111nnn001001101 fneg   drn
  // 1111nnnn01001101 fneg   frn
  si.check_arg_types({ArgType::FR_DR_REGISTER});
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num, 0x4, 0xD);
}

uint16_t SH4Emulator::Assembler::asm_frchg_fschg(const StreamItem& si) const {
  // 1111001111111101 fschg
  // 1111101111111101 frchg
  si.check_arg_types({});
  return (si.op_name.at(1) == 'r') ? 0xFBFD : 0xF3FD;
}

uint16_t SH4Emulator::Assembler::asm_fsqrt(const StreamItem& si) const {
  // 1111nnn001101101 fsqrt  drn
  // 1111nnnn01101101 fsqrt  frn
  si.check_arg_types({ArgType::FR_DR_REGISTER});
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num, 0x6, 0xD);
}

uint16_t SH4Emulator::Assembler::asm_fsub(const StreamItem& si) const {
  // 1111nnn0mmm00001 fsub   drn, drm
  // 1111nnnnmmmm0001 fsub   frn, frm
  si.check_2_same_float_regs();
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num, si.args[1].reg_num, 0x1);
}

uint16_t SH4Emulator::Assembler::asm_ftrc(const StreamItem& si) const {
  // 1111mmm000111101 ftrc   fpul, drn
  // 1111mmmm00111101 ftrc   fpul, frm
  si.check_arg_types({ArgType::FPUL, ArgType::FR_DR_REGISTER});
  return asm_op_r1_r2_r3(0xF, si.args[1].reg_num, 0x3, 0xD);
}

uint16_t SH4Emulator::Assembler::asm_ftrv(const StreamItem& si) const {
  // 1111nn0111111101 ftrv   fvn, xmtrx
  si.check_arg_types({ArgType::FV_REGISTER, ArgType::XMTRX});
  if (si.args[0].reg_num & 0xF3) {
    throw runtime_error("invalid fv register number");
  }
  return asm_op_r1_r2_r3(0xF, si.args[0].reg_num | 1, 0xF, 0xD);
}

uint16_t SH4Emulator::Assembler::asm_ldc_ldc_l(const StreamItem& si) const {
  if (si.args.size() != 2) {
    throw runtime_error("incorrect number of arguments");
  }

  if (si.args[0].type == ArgType::BANK_INT_REGISTER) {
    if (si.args[1].type == ArgType::INT_REGISTER) {
      // 0100mmmm1nnn1110 ldc    rnb, rm
      return asm_op_r1_r2_r3(0x4, si.args[1].reg_num, si.args[0].reg_num | 0x8, 0xE);
    } else if (si.args[1].type == ArgType::POSTINC_MEMORY_REFERENCE) {
      // 0100mmmm1nnn0111 ldc.l  rnb, [rm]+
      return asm_op_r1_r2_r3(0x4, si.args[1].reg_num, si.args[0].reg_num | 0x8, 0x7);
    } else {
      si.throw_invalid_arguments();
    }
  }

  bool is_postinc = si.op_name.ends_with(".l");
  if (si.args[1].type != (is_postinc ? ArgType::POSTINC_MEMORY_REFERENCE : ArgType::INT_REGISTER)) {
    si.throw_invalid_arguments();
  }

  if (si.args[0].type == ArgType::DBR) {
    // 0100mmmm11111010 ldc    dbr, rm
    // 0100mmmm11110110 ldc.l  dbr, [rm]+
    return asm_op_r1_r2_r3(0x4, si.args[1].reg_num, 0xF, is_postinc ? 0x6 : 0xA);
  } else if (si.args[0].type == ArgType::GBR) {
    // 0100mmmm00011110 ldc    gbr, rm
    // 0100mmmm00010111 ldc.l  gbr, [rm]+
    return asm_op_r1_r2_r3(0x4, si.args[1].reg_num, 0x1, is_postinc ? 0x7 : 0xE);
  } else if (si.args[0].type == ArgType::SPC) {
    // 0100mmmm01001110 ldc    spc, rm
    // 0100mmmm01000111 ldc.l  spc, [rm]+
    return asm_op_r1_r2_r3(0x4, si.args[1].reg_num, 0x4, is_postinc ? 0x7 : 0xE);
  } else if (si.args[0].type == ArgType::SR) {
    // 0100mmmm00001110 ldc    sr, rm
    // 0100mmmm00000111 ldc.l  sr, [rm]+
    return asm_op_r1_r2_r3(0x4, si.args[1].reg_num, 0x0, is_postinc ? 0x7 : 0xE);
  } else if (si.args[0].type == ArgType::SSR) {
    // 0100mmmm00111110 ldc    ssr, rm
    // 0100mmmm00110111 ldc.l  ssr, [rm]+
    return asm_op_r1_r2_r3(0x4, si.args[1].reg_num, 0x3, is_postinc ? 0x7 : 0xE);
  } else if (si.args[0].type == ArgType::VBR) {
    // 0100mmmm00101110 ldc    vbr, rm
    // 0100mmmm00100111 ldc.l  vbr, [rm]+
    return asm_op_r1_r2_r3(0x4, si.args[1].reg_num, 0x2, is_postinc ? 0x7 : 0xE);
  } else {
    si.throw_invalid_arguments();
  }
}

uint16_t SH4Emulator::Assembler::asm_lds_lds_l(const StreamItem& si) const {
  if (si.args.size() != 2) {
    throw runtime_error("incorrect number of arguments");
  }

  bool is_postinc = (si.args[1].type == ArgType::POSTINC_MEMORY_REFERENCE);
  if (!is_postinc && (si.args[1].type != ArgType::INT_REGISTER)) {
    si.throw_invalid_arguments();
  }

  if (si.args[0].type == ArgType::FPSCR) {
    // 0100mmmm01101010 lds    fpscr, rm
    // 0100mmmm01100110 lds.l  fpscr, [rm]+
    return asm_op_r1_r2_r3(0x4, si.args[1].reg_num, 0x6, is_postinc ? 0x6 : 0xA);

  } else if (si.args[0].type == ArgType::FPUL) {
    // 0100mmmm01011010 lds    fpul, rm
    // 0100mmmm01010110 lds.l  fpul, [rm]+
    return asm_op_r1_r2_r3(0x4, si.args[1].reg_num, 0x5, is_postinc ? 0x6 : 0xA);

  } else if (si.args[0].type == ArgType::MACH) {
    // 0100mmmm00001010 lds    mach, rm
    // 0100mmmm00000110 lds    mach, [rm]+
    return asm_op_r1_r2_r3(0x4, si.args[1].reg_num, 0x0, is_postinc ? 0x6 : 0xA);

  } else if (si.args[0].type == ArgType::MACL) {
    // 0100mmmm00011010 lds    macl, rm
    // 0100mmmm00010110 lds    macl, [rm]+
    return asm_op_r1_r2_r3(0x4, si.args[1].reg_num, 0x1, is_postinc ? 0x6 : 0xA);

  } else if (si.args[0].type == ArgType::PR) {
    // 0100mmmm00101010 lds    pr, rm
    // 0100mmmm00100110 lds    pr, [rm]+
    return asm_op_r1_r2_r3(0x4, si.args[1].reg_num, 0x2, is_postinc ? 0x6 : 0xA);

  } else {
    si.throw_invalid_arguments();
  }
}

uint16_t SH4Emulator::Assembler::asm_ldtlb(const StreamItem& si) const {
  // 0000000000111000 ldtlb
  si.check_arg_types({});
  return 0x0038;
}

uint16_t SH4Emulator::Assembler::asm_mac_w_mac_l(const StreamItem& si) const {
  // 0000nnnnmmmm1111 mac.l  [rn]+, [rm]+  # mac = [rn] * [rm] + mac
  // 0100nnnnmmmm1111 mac.w  [rn]+, [rm]+ // mac = [rn] * [rm] + mac
  si.check_arg_types({ArgType::POSTINC_MEMORY_REFERENCE, ArgType::POSTINC_MEMORY_REFERENCE});
  return asm_op_r1_r2_r3(si.op_name.at(4) == 'w' ? 0x4 : 0x0, si.args[0].reg_num, si.args[1].reg_num, 0xF);
}

uint16_t SH4Emulator::Assembler::asm_mov(const StreamItem& si) const {
  if (si.arg_types_match({ArgType::INT_REGISTER, ArgType::IMMEDIATE})) {
    // 1110nnnniiiiiiii mov    rn, imm
    check_range_t(si.args[1].value, -0x80, 0x7F);
    return asm_op_r1_imm8(0xE, si.args[0].reg_num, si.args[1].value);
  }
  // 0110nnnnmmmm0011 mov    rn, rm
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return asm_op_r1_r2_r3(0x6, si.args[0].reg_num, si.args[1].reg_num, 0x3);
}

uint16_t SH4Emulator::Assembler::asm_mov_b_w_l(const StreamItem& si) const {
  uint8_t size = 0xFF;
  if (si.op_name.at(4) == 'b') {
    size = 0;
  } else if (si.op_name.at(4) == 'w') {
    size = 1;
  } else if (si.op_name.at(4) == 'l') {
    size = 2;
  } else {
    throw runtime_error("invalid operand size");
  }

  if (si.arg_types_match({ArgType::MEMORY_REFERENCE, ArgType::INT_REGISTER})) {
    // 0010nnnnmmmm0000 mov.b  [rn], rm
    // 0010nnnnmmmm0001 mov.w  [rn], rm
    // 0010nnnnmmmm0010 mov.l  [rn], rm
    return asm_op_r1_r2_r3(0x2, si.args[0].reg_num, si.args[1].reg_num, size);

  } else if (si.arg_types_match({ArgType::PREDEC_MEMORY_REFERENCE, ArgType::INT_REGISTER})) {
    // 0010nnnnmmmm0100 mov.b  -[rn], rm
    // 0010nnnnmmmm0101 mov.w  -[rn], rm
    // 0010nnnnmmmm0110 mov.l  -[rn], rm
    return asm_op_r1_r2_r3(0x2, si.args[0].reg_num, si.args[1].reg_num, 0x4 | size);

  } else if (si.arg_types_match({ArgType::REG_R0_MEMORY_REFERENCE, ArgType::INT_REGISTER})) {
    // 0000nnnnmmmm0100 mov.b  [r0 + rn], rm
    // 0000nnnnmmmm0101 mov.w  [r0 + rn], rm
    // 0000nnnnmmmm0110 mov.l  [r0 + rn], rm
    return asm_op_r1_r2_r3(0x0, si.args[0].reg_num, si.args[1].reg_num, 0x4 | size);

  } else if (si.arg_types_match({ArgType::REG_DISP_MEMORY_REFERENCE, ArgType::INT_REGISTER})) {
    check_range_t(si.args[0].value, 0x00, 0x0F * (1 << size));
    if (si.args[0].value & ((1 << size) - 1)) {
      throw runtime_error("offset is not aligned");
    }

    if (size == 2) {
      // 0001nnnnmmmmdddd mov.l  [rn + 4 * d], rm
      return asm_op_r1_r2_r3(0x1, si.args[0].reg_num, si.args[1].reg_num, si.args[0].value >> size);
    }
    // 10000000nnnndddd mov.b  [rn + d], r0
    // 10000001nnnndddd mov.w  [rn + 2 * d], r0
    if (si.args[1].reg_num != 0) {
      throw runtime_error("invalid source register");
    }
    return asm_op_r1_r2_r3(0x8, size, si.args[0].reg_num, si.args[0].value >> size);

  } else if (si.arg_types_match({ArgType::GBR_DISP_MEMORY_REFERENCE, ArgType::INT_REGISTER})) {
    // 11000000dddddddd mov.b  [gbr + d], r0
    // 11000001dddddddd mov.w  [gbr + 2 * d], r0
    // 11000010dddddddd mov.l  [gbr + 4 * d], r0
    check_range_t(si.args[0].value, 0x00, 0x0F * (1 << size));
    if (si.args[0].value & ((1 << size) - 1)) {
      throw runtime_error("offset is not aligned");
    }
    if (si.args[1].reg_num != 0) {
      throw runtime_error("invalid source register");
    }
    return asm_op_r1_imm8(0xC, size, si.args[0].value >> size);

  } else if (si.arg_types_match({ArgType::INT_REGISTER, ArgType::MEMORY_REFERENCE})) {
    // 0110nnnnmmmm0000 mov.b  rn, [rm]  # sign-ext
    // 0110nnnnmmmm0001 mov.w  rn, [rm]  # sign-ext
    // 0110nnnnmmmm0010 mov.l  rn, [rm]
    return asm_op_r1_r2_r3(0x6, si.args[0].reg_num, si.args[1].reg_num, size);

  } else if (si.arg_types_match({ArgType::INT_REGISTER, ArgType::POSTINC_MEMORY_REFERENCE})) {
    // 0110nnnnmmmm0100 mov.b  rn, [rm]+  # sign-ext
    // 0110nnnnmmmm0101 mov.w  rn, [rm]+  # sign-ext
    // 0110nnnnmmmm0110 mov.l  rn, [rm]+
    return asm_op_r1_r2_r3(0x6, si.args[0].reg_num, si.args[1].reg_num, 0x4 | size);

  } else if (si.arg_types_match({ArgType::INT_REGISTER, ArgType::REG_R0_MEMORY_REFERENCE})) {
    // 0000nnnnmmmm1100 mov.b  rn, [r0 + rm]  # sign-ext
    // 0000nnnnmmmm1101 mov.w  rn, [r0 + rm]  # sign-ext
    // 0000nnnnmmmm1110 mov.l  rn, [r0 + rm]
    return asm_op_r1_r2_r3(0x0, si.args[0].reg_num, si.args[1].reg_num, 0xC | size);

  } else if (si.arg_types_match({ArgType::INT_REGISTER, ArgType::REG_DISP_MEMORY_REFERENCE})) {
    check_range_t(si.args[1].value, 0x00, 0x0F * (1 << size));
    if (si.args[1].value & ((1 << size) - 1)) {
      throw runtime_error("offset is not aligned");
    }

    if (size == 2) {
      // 0101nnnnmmmmdddd mov.l  rn, [rm + 4 * d]
      return asm_op_r1_r2_r3(0x5, si.args[0].reg_num, si.args[1].reg_num, si.args[1].value >> size);
    }
    // 10000100mmmmdddd mov.b  r0, [rm + d]  # sign-ext
    // 10000101mmmmdddd mov.w  r0, [rm + 2 * d]  # sign-ext
    if (si.args[0].reg_num != 0) {
      throw runtime_error("invalid destination register");
    }
    return asm_op_r1_r2_r3(0x8, 4 | size, si.args[1].reg_num, si.args[1].value >> size);

  } else if (si.arg_types_match({ArgType::INT_REGISTER, ArgType::GBR_DISP_MEMORY_REFERENCE})) {
    // 11000100dddddddd mov.b  r0, [gbr + d]  # sign-ext
    // 11000101dddddddd mov.w  r0, [gbr + 2 * d]  # sign-ext
    // 11000110dddddddd mov.l  r0, [gbr + 4 * d]
    check_range_t(si.args[1].value, 0x00, 0x0F * (1 << size));
    if (si.args[1].value & ((1 << size) - 1)) {
      throw runtime_error("offset is not aligned");
    }
    if (si.args[0].reg_num != 0) {
      throw runtime_error("invalid destination register");
    }
    return asm_op_r1_imm8(0xC, 4 | size, si.args[1].value >> size);

  } else if (si.arg_types_match({ArgType::INT_REGISTER, ArgType::PC_MEMORY_REFERENCE})) {
    // 1001nnnndddddddd mov.w  rn, [pc + 4 + d * 2]
    // 1101nnnndddddddd mov.l  rn, [(pc & ~3) + 4 + d * 4]
    uint32_t dest_offset = si.args[1].label_name.empty()
        ? si.args[1].value
        : this->label_offsets.at(si.args[1].label_name);
    int32_t delta;
    if (size == 1) {
      delta = dest_offset - (si.offset + 4);
    } else if (size == 2) {
      delta = dest_offset - ((si.offset & (~3)) + 4);
    } else {
      throw runtime_error("invalid operand size");
    }
    if (delta & ((1 << size) - 1)) {
      throw runtime_error("misaligned read offset");
    }
    return asm_op_r1_imm8(0x9 | ((size == 2) ? 0x4 : 0x0), si.args[0].reg_num, delta >> size);
  }

  si.throw_invalid_arguments();
}

uint16_t SH4Emulator::Assembler::asm_movca_l(const StreamItem& si) const {
  // 0000nnnn11000011 movca.l [rn], r0
  si.check_arg_types({ArgType::MEMORY_REFERENCE, ArgType::INT_REGISTER});
  if (si.args[1].reg_num != 0) {
    throw runtime_error("movca.l source operand must be r0");
  }
  return asm_op_r1_r2_r3(0x0, si.args[0].reg_num, 0xC, 0x3);
}

uint16_t SH4Emulator::Assembler::asm_mova(const StreamItem& si) const {
  // 11000111dddddddd mova   r0, [(pc & ~3) + 4 + d * 4]
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::PC_MEMORY_REFERENCE});
  if (si.args[0].reg_num != 0) {
    throw runtime_error("mova dest operand must be r0");
  }
  uint32_t target = si.args[1].label_name.empty()
      ? si.args[1].value
      : this->label_offsets.at(si.args[1].label_name);
  int32_t delta = target - ((si.offset & (~3)) + 4);
  check_range_t(delta, -0x80 * 4, 0x7F * 4);
  return asm_op_r1_imm8(0xC, 0x7, delta >> 2);
}

uint16_t SH4Emulator::Assembler::asm_movt(const StreamItem& si) const {
  // 0000nnnn00101001 movt   rn, t
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::T});
  return asm_op_r1_imm8(0x0, si.args[0].reg_num, 0x29);
}

uint16_t SH4Emulator::Assembler::asm_mul_l(const StreamItem& si) const {
  // 0000nnnnmmmm0111 mul.l  rn, rm // macl = rn * rm
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return asm_op_r1_r2_r3(0x0, si.args[0].reg_num, si.args[1].reg_num, 0x7);
}

uint16_t SH4Emulator::Assembler::asm_muls_w_mulu_w(const StreamItem& si) const {
  // 0010nnnnmmmm1110 mulu.w rn, rm  # macl = rn * rm
  // 0010nnnnmmmm1111 muls.w rn, rm  # macl = rn * rm
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return asm_op_r1_r2_r3(0x2, si.args[0].reg_num, si.args[1].reg_num, (si.op_name.at(3) == 's') ? 0xF : 0xE);
}

uint16_t SH4Emulator::Assembler::asm_neg_negc(const StreamItem& si) const {
  // 0110nnnnmmmm1010 negc   rn, rm
  // 0110nnnnmmmm1011 neg    rn, rm
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return asm_op_r1_r2_r3(0x6, si.args[0].reg_num, si.args[1].reg_num, (si.op_name == "negc") ? 0xA : 0xB);
}

uint16_t SH4Emulator::Assembler::asm_not(const StreamItem& si) const {
  // 0110nnnnmmmm0111 not    rn, rm
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return asm_op_r1_r2_r3(0x6, si.args[0].reg_num, si.args[1].reg_num, 0x7);
}

uint16_t SH4Emulator::Assembler::asm_nop(const StreamItem& si) const {
  // 0000000000001001 nop
  si.check_arg_types({});
  return 0x0009;
}

uint16_t SH4Emulator::Assembler::asm_ocbi_ocbp_ocbwb_pref(const StreamItem& si) const {
  // 0000nnnn10000011 pref   [rn]  # prefetch
  // 0000nnnn10010011 ocbi   [rn]  # dcbi
  // 0000nnnn10100011 ocbp   [rn]  # dcbf
  // 0000nnnn10110011 ocbwb  [rn]  # dcbst?
  si.check_arg_types({ArgType::MEMORY_REFERENCE});
  uint8_t subtype;
  if (si.op_name == "pref") {
    subtype = 0x8;
  } else if (si.op_name == "ocbi") {
    subtype = 0x9;
  } else if (si.op_name == "ocbp") {
    subtype = 0xA;
  } else if (si.op_name == "ocbwb") {
    subtype = 0xB;
  } else {
    throw logic_error("invalid cache opcode subtype");
  }
  return asm_op_r1_r2_r3(0x0, si.args[0].reg_num, subtype, 0x3);
}

uint16_t SH4Emulator::Assembler::asm_rcl_rcr_rol_ror(const StreamItem& si) const {
  // 0100nnnn00000100 rol    rn
  // 0100nnnn00000101 ror    rn
  // 0100nnnn00100100 rcl    rn
  // 0100nnnn00100101 rcr    rn
  si.check_arg_types({ArgType::INT_REGISTER});
  bool is_c = si.op_name.at(1) == 'c';
  bool is_r = si.op_name.at(2) == 'r';
  return asm_op_r1_r2_r3(0x4, si.args[0].reg_num, is_c ? 0x2 : 0x0, is_r ? 0x5 : 0x4);
}

uint16_t SH4Emulator::Assembler::asm_rets(const StreamItem& si) const {
  // 0000000000001011 ret
  si.check_arg_types({});
  return 0x000B;
}

uint16_t SH4Emulator::Assembler::asm_sleep(const StreamItem& si) const {
  // 0000000000011011 sleep
  si.check_arg_types({});
  return 0x001B;
}

uint16_t SH4Emulator::Assembler::asm_rte(const StreamItem& si) const {
  // 0000000000101011 rte
  si.check_arg_types({});
  return 0x002B;
}

uint16_t SH4Emulator::Assembler::asm_shad_shld(const StreamItem& si) const {
  // 0100nnnnmmmm1100 shad   rn, rm
  // 0100nnnnmmmm1101 shld   rn, rm
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  bool is_l = si.op_name.at(2) == 'l';
  return asm_op_r1_r2_r3(0x4, si.args[0].reg_num, si.args[1].reg_num, is_l ? 0xD : 0xC);
}

uint16_t SH4Emulator::Assembler::asm_shal_shar(const StreamItem& si) const {
  // 0100nnnn00100000 shal   rn
  // 0100nnnn00100001 shar   rn
  si.check_arg_types({ArgType::INT_REGISTER});
  bool is_r = si.op_name.at(3) == 'r';
  return asm_op_r1_r2_r3(0x4, si.args[0].reg_num, 0x2, is_r ? 0x1 : 0x0);
}

uint16_t SH4Emulator::Assembler::asm_shl_shr(const StreamItem& si) const {
  uint8_t shift_spec;
  if (si.arg_types_match({ArgType::INT_REGISTER})) {
    // 0100nnnn00000000 shl    rn
    // 0100nnnn00000001 shr    rn
    shift_spec = 0x00;
  } else {
    si.check_arg_types({ArgType::INT_REGISTER, ArgType::IMMEDIATE});
    if (si.args[1].value == 1) {
      // 0100nnnn00000000 shl    rn  # alias
      // 0100nnnn00000001 shr    rn  # alias
      shift_spec = 0x00;
    } else if (si.args[1].value == 2) {
      // 0100nnnn00001000 shl    rn, 2
      // 0100nnnn00001001 shr    rn, 2
      shift_spec = 0x08;
    } else if (si.args[1].value == 8) {
      // 0100nnnn00011000 shl    rn, 8
      // 0100nnnn00011001 shr    rn, 8
      shift_spec = 0x18;
    } else if (si.args[1].value == 16) {
      // 0100nnnn00101000 shl    rn, 16
      // 0100nnnn00101001 shr    rn, 16
      shift_spec = 0x28;
    } else {
      throw runtime_error("shift distance must be 1, 2, 8, or 16");
    }
  }
  return asm_op_r1_imm8(0x4, si.args[0].reg_num, shift_spec | ((si.op_name.at(2) == 'r') ? 1 : 0));
}

uint16_t SH4Emulator::Assembler::asm_stc_stc_l(const StreamItem& si) const {
  if (si.args.size() != 2) {
    throw runtime_error("incorrect number of arguments");
  }

  bool is_predec = si.op_name.ends_with(".l");
  if (si.args[0].type != (is_predec ? ArgType::PREDEC_MEMORY_REFERENCE : ArgType::INT_REGISTER)) {
    si.throw_invalid_arguments();
  }

  uint8_t op = is_predec ? 0x4 : 0x0;
  if (si.args[1].type == ArgType::SR) {
    // 0000nnnn00000010 stc    rn, sr
    // 0100nnnn00000011 stc.l  -[rn], sr
    return asm_op_r1_imm8(op, si.args[0].reg_num, is_predec ? 0x03 : 0x02);
  } else if (si.args[1].type == ArgType::GBR) {
    // 0000nnnn00010010 stc    rn, gbr
    // 0100nnnn00010011 stc.l  -[rn], gbr
    return asm_op_r1_imm8(op, si.args[0].reg_num, is_predec ? 0x13 : 0x12);
  } else if (si.args[1].type == ArgType::VBR) {
    // 0000nnnn00100010 stc    rn, vbr
    // 0100nnnn00100011 stc.l  -[rn], vbr
    return asm_op_r1_imm8(op, si.args[0].reg_num, is_predec ? 0x23 : 0x22);
  } else if (si.args[1].type == ArgType::SSR) {
    // 0000nnnn00110010 stc    rn, ssr
    // 0100nnnn00110011 stc.l  -[rn], ssr
    return asm_op_r1_imm8(op, si.args[0].reg_num, is_predec ? 0x33 : 0x32);
  } else if (si.args[1].type == ArgType::SGR) {
    // 0000nnnn00111010 stc    rn, sgr
    // 0100nnnn00110010 stc.l  -[rn], sgr
    return asm_op_r1_imm8(op, si.args[0].reg_num, is_predec ? 0x32 : 0x3A);
  } else if (si.args[1].type == ArgType::SPC) {
    // 0000nnnn01000010 stc    rn, spc
    // 0100nnnn01000011 stc.l  -[rn], spc
    return asm_op_r1_imm8(op, si.args[0].reg_num, is_predec ? 0x43 : 0x42);
  } else if (si.args[1].type == ArgType::DBR) {
    // 0000nnnn11111010 stc    rn, dbr
    // 0100nnnn11110010 stc.l  -[rn], dbr
    return asm_op_r1_imm8(op, si.args[0].reg_num, is_predec ? 0xF2 : 0xFA);
  } else if (si.args[1].type == ArgType::BANK_INT_REGISTER) {
    // 0000nnnn1mmm0010 stc    rn, rmb
    // 0100nnnn1mmm0011 stc.l  -[rn], rmb
    return asm_op_r1_r2_r3(op, si.args[0].reg_num, 8 | si.args[1].reg_num, is_predec ? 0x3 : 0x2);
  } else {
    si.throw_invalid_arguments();
  }
}

uint16_t SH4Emulator::Assembler::asm_sts_sts_l(const StreamItem& si) const {
  if (si.args.size() != 2) {
    throw runtime_error("incorrect number of arguments");
  }

  bool is_predec = si.op_name.ends_with(".l");
  if (si.args[0].type != (is_predec ? ArgType::PREDEC_MEMORY_REFERENCE : ArgType::INT_REGISTER)) {
    si.throw_invalid_arguments();
  }

  uint8_t op = is_predec ? 0x4 : 0x0;
  if (si.args[1].type == ArgType::MACH) {
    // 0000nnnn00001010 sts    rn, mach
    // 0100nnnn00000010 sts.l  -[rn], mach
    return asm_op_r1_imm8(op, si.args[0].reg_num, is_predec ? 0x02 : 0x0A);
  } else if (si.args[1].type == ArgType::MACL) {
    // 0000nnnn00011010 sts    rn, macl
    // 0100nnnn00010010 sts.l  -[rn], macl
    return asm_op_r1_imm8(op, si.args[0].reg_num, is_predec ? 0x12 : 0x1A);
  } else if (si.args[1].type == ArgType::PR) {
    // 0000nnnn00101010 sts    rn, pr
    // 0100nnnn00100010 sts.l  -[rn], pr
    return asm_op_r1_imm8(op, si.args[0].reg_num, is_predec ? 0x22 : 0x2A);
  } else if (si.args[1].type == ArgType::FPUL) {
    // 0000nnnn01011010 sts    rn, fpul
    // 0100nnnn01010010 sts.l  -[rn], fpul
    return asm_op_r1_imm8(op, si.args[0].reg_num, is_predec ? 0x52 : 0x5A);
  } else if (si.args[1].type == ArgType::FPSCR) {
    // 0000nnnn01101010 sts    rn, fpscr
    // 0100nnnn01100010 sts.l  -[rn], fpscr
    return asm_op_r1_imm8(op, si.args[0].reg_num, is_predec ? 0x62 : 0x6A);
  } else {
    si.throw_invalid_arguments();
  }
}

uint16_t SH4Emulator::Assembler::asm_swap_b_w(const StreamItem& si) const {
  // 0110nnnnmmmm1000 swap.b rn, rm  # swap lower 2 bytes
  // 0110nnnnmmmm1001 swap.w rn, rm  # swap words
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return asm_op_r1_r2_r3(0x6, si.args[0].reg_num, si.args[1].reg_num, (si.op_name.at(5) == 'w') ? 0x9 : 0x8);
}

uint16_t SH4Emulator::Assembler::asm_tas_b(const StreamItem& si) const {
  // 0100nnnn00011011 tas.b  [rn]
  si.check_arg_types({ArgType::MEMORY_REFERENCE});
  return asm_op_r1_imm8(0x4, si.args[0].reg_num, 0x1B);
}

uint16_t SH4Emulator::Assembler::asm_test_xor(const StreamItem& si) const {
  uint8_t subopcode = (si.op_name == "xor") ? 0xA : 0x8;
  if (si.arg_types_match({ArgType::INT_REGISTER, ArgType::INT_REGISTER})) {
    // 0010nnnnmmmm1000 test   rn, rm
    // 0010nnnnmmmm1010 xor    rn, rm
    return asm_op_r1_r2_r3(0x2, si.args[0].reg_num, si.args[1].reg_num, subopcode);
  }
  // 11001000iiiiiiii test   r0, imm
  // 11001010iiiiiiii xor    r0, imm
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  if (si.args[0].reg_num != 0) {
    throw runtime_error("register must be r0 for test/xor with imm");
  }
  check_range_t(si.args[1].value, 0x00, 0xFF);
  return asm_op_r1_imm8(0xC, subopcode, si.args[1].value);
}

uint16_t SH4Emulator::Assembler::asm_test_b_xor_b(const StreamItem& si) const {
  // 11001100iiiiiiii test.b [r0 + gbr], imm
  // 11001110iiiiiiii xor.b  [r0 + gbr], imm
  uint8_t subopcode = si.op_name == "xor.b" ? 0xE : 0xC;
  si.check_arg_types({ArgType::GBR_R0_MEMORY_REFERENCE, ArgType::IMMEDIATE});
  check_range_t(si.args[1].value, 0x00, 0xFF);
  return asm_op_r1_imm8(0xC, subopcode, si.args[1].value);
}

uint16_t SH4Emulator::Assembler::asm_trapa(const StreamItem& si) const {
  // 11000011iiiiiiii trapa  imm
  si.check_arg_types({ArgType::IMMEDIATE});
  check_range_t(si.args[0].value, 0x00, 0xFF);
  return asm_op_r1_imm8(0xC, 0x3, si.args[0].value);
}

uint16_t SH4Emulator::Assembler::asm_xtrct(const StreamItem& si) const {
  // 0010nnnnmmmm1101 xtrct  rn, rm  # rm.rn middle 32 bits -> rn
  si.check_arg_types({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return asm_op_r1_r2_r3(0x2, si.args[0].reg_num, si.args[1].reg_num, 0xD);
}

const unordered_map<string, SH4Emulator::Assembler::AssembleFunction>
    SH4Emulator::Assembler::assemble_functions = {
        {"add", &SH4Emulator::Assembler::asm_add_addc_addv_sub_subc_subv},
        {"addc", &SH4Emulator::Assembler::asm_add_addc_addv_sub_subc_subv},
        {"addv", &SH4Emulator::Assembler::asm_add_addc_addv_sub_subc_subv},
        {"sub", &SH4Emulator::Assembler::asm_add_addc_addv_sub_subc_subv},
        {"subc", &SH4Emulator::Assembler::asm_add_addc_addv_sub_subc_subv},
        {"subv", &SH4Emulator::Assembler::asm_add_addc_addv_sub_subc_subv},
        {"and", &SH4Emulator::Assembler::asm_and_or},
        {"or", &SH4Emulator::Assembler::asm_and_or},
        {"and.b", &SH4Emulator::Assembler::asm_and_b_or_b},
        {"or.b", &SH4Emulator::Assembler::asm_and_b_or_b},
        {"bs", &SH4Emulator::Assembler::asm_bs_calls},
        {"calls", &SH4Emulator::Assembler::asm_bs_calls},
        {"bt", &SH4Emulator::Assembler::asm_bt_bf_bts_bfs},
        {"bf", &SH4Emulator::Assembler::asm_bt_bf_bts_bfs},
        {"bts", &SH4Emulator::Assembler::asm_bt_bf_bts_bfs},
        {"bfs", &SH4Emulator::Assembler::asm_bt_bf_bts_bfs},
        {"clrt", &SH4Emulator::Assembler::asm_clrt},
        {"sett", &SH4Emulator::Assembler::asm_sett},
        {"clrmac", &SH4Emulator::Assembler::asm_clrmac},
        {"clrs", &SH4Emulator::Assembler::asm_clrs},
        {"sets", &SH4Emulator::Assembler::asm_sets},
        {"cmpa", &SH4Emulator::Assembler::asm_cmp_mnemonics},
        {"cmpae", &SH4Emulator::Assembler::asm_cmp_mnemonics},
        {"cmpe", &SH4Emulator::Assembler::asm_cmp_mnemonics},
        {"cmpeq", &SH4Emulator::Assembler::asm_cmp_mnemonics},
        {"cmpge", &SH4Emulator::Assembler::asm_cmp_mnemonics},
        {"cmpgt", &SH4Emulator::Assembler::asm_cmp_mnemonics},
        {"cmpstr", &SH4Emulator::Assembler::asm_cmp_mnemonics},
        {"dec", &SH4Emulator::Assembler::asm_dec},
        {"div0s", &SH4Emulator::Assembler::asm_div0s},
        {"div0u", &SH4Emulator::Assembler::asm_div0u},
        {"div1", &SH4Emulator::Assembler::asm_div1},
        {"dmuls.l", &SH4Emulator::Assembler::asm_dmuls_dmulu},
        {"dmulu.l", &SH4Emulator::Assembler::asm_dmuls_dmulu},
        {"exts.b", &SH4Emulator::Assembler::asm_exts_extu},
        {"exts.w", &SH4Emulator::Assembler::asm_exts_extu},
        {"extu.b", &SH4Emulator::Assembler::asm_exts_extu},
        {"extu.w", &SH4Emulator::Assembler::asm_exts_extu},
        {"fabs", &SH4Emulator::Assembler::asm_fabs},
        {"fadd", &SH4Emulator::Assembler::asm_fadd},
        {"fcmpe", &SH4Emulator::Assembler::asm_fcmp_mnemonics},
        {"fcmpeq", &SH4Emulator::Assembler::asm_fcmp_mnemonics},
        {"fcmpgt", &SH4Emulator::Assembler::asm_fcmp_mnemonics},
        {"fcnvds", &SH4Emulator::Assembler::asm_fcnvds},
        {"fcnvsd", &SH4Emulator::Assembler::asm_fcnvsd},
        {"fdiv", &SH4Emulator::Assembler::asm_fdiv},
        {"fipr", &SH4Emulator::Assembler::asm_fipr},
        {"fldi0", &SH4Emulator::Assembler::asm_fldi0_fldi1},
        {"fldi1", &SH4Emulator::Assembler::asm_fldi0_fldi1},
        {"flds", &SH4Emulator::Assembler::asm_flds},
        {"fsts", &SH4Emulator::Assembler::asm_fsts},
        {"float", &SH4Emulator::Assembler::asm_float},
        {"fmac", &SH4Emulator::Assembler::asm_fmac},
        {"fmov", &SH4Emulator::Assembler::asm_fmov_fmov_s},
        {"fmov.s", &SH4Emulator::Assembler::asm_fmov_fmov_s},
        {"fmul", &SH4Emulator::Assembler::asm_fmul},
        {"fneg", &SH4Emulator::Assembler::asm_fneg},
        {"frchg", &SH4Emulator::Assembler::asm_frchg_fschg},
        {"fschg", &SH4Emulator::Assembler::asm_frchg_fschg},
        {"fsqrt", &SH4Emulator::Assembler::asm_fsqrt},
        {"fsub", &SH4Emulator::Assembler::asm_fsub},
        {"ftrc", &SH4Emulator::Assembler::asm_ftrc},
        {"ftrv", &SH4Emulator::Assembler::asm_ftrv},
        {"ldc", &SH4Emulator::Assembler::asm_ldc_ldc_l},
        {"ldc.l", &SH4Emulator::Assembler::asm_ldc_ldc_l},
        {"lds", &SH4Emulator::Assembler::asm_lds_lds_l},
        {"lds.l", &SH4Emulator::Assembler::asm_lds_lds_l},
        {"ldtlb", &SH4Emulator::Assembler::asm_ldtlb},
        {"mac.w", &SH4Emulator::Assembler::asm_mac_w_mac_l},
        {"mac.l", &SH4Emulator::Assembler::asm_mac_w_mac_l},
        {"mov", &SH4Emulator::Assembler::asm_mov},
        {"mov.b", &SH4Emulator::Assembler::asm_mov_b_w_l},
        {"mov.w", &SH4Emulator::Assembler::asm_mov_b_w_l},
        {"mov.l", &SH4Emulator::Assembler::asm_mov_b_w_l},
        {"movca.l", &SH4Emulator::Assembler::asm_movca_l},
        {"mova", &SH4Emulator::Assembler::asm_mova},
        {"movt", &SH4Emulator::Assembler::asm_movt},
        {"mul.l", &SH4Emulator::Assembler::asm_mul_l},
        {"muls.w", &SH4Emulator::Assembler::asm_muls_w_mulu_w},
        {"mulu.w", &SH4Emulator::Assembler::asm_muls_w_mulu_w},
        {"neg", &SH4Emulator::Assembler::asm_neg_negc},
        {"negc", &SH4Emulator::Assembler::asm_neg_negc},
        {"not", &SH4Emulator::Assembler::asm_not},
        {"nop", &SH4Emulator::Assembler::asm_nop},
        {"ocbi", &SH4Emulator::Assembler::asm_ocbi_ocbp_ocbwb_pref},
        {"ocbp", &SH4Emulator::Assembler::asm_ocbi_ocbp_ocbwb_pref},
        {"ocbwb", &SH4Emulator::Assembler::asm_ocbi_ocbp_ocbwb_pref},
        {"pref", &SH4Emulator::Assembler::asm_ocbi_ocbp_ocbwb_pref},
        {"rcl", &SH4Emulator::Assembler::asm_rcl_rcr_rol_ror},
        {"rcr", &SH4Emulator::Assembler::asm_rcl_rcr_rol_ror},
        {"rol", &SH4Emulator::Assembler::asm_rcl_rcr_rol_ror},
        {"ror", &SH4Emulator::Assembler::asm_rcl_rcr_rol_ror},
        {"rets", &SH4Emulator::Assembler::asm_rets},
        {"sleep", &SH4Emulator::Assembler::asm_sleep},
        {"rte", &SH4Emulator::Assembler::asm_rte},
        {"shad", &SH4Emulator::Assembler::asm_shad_shld},
        {"shld", &SH4Emulator::Assembler::asm_shad_shld},
        {"shal", &SH4Emulator::Assembler::asm_shal_shar},
        {"shar", &SH4Emulator::Assembler::asm_shal_shar},
        {"shl", &SH4Emulator::Assembler::asm_shl_shr},
        {"shr", &SH4Emulator::Assembler::asm_shl_shr},
        {"stc", &SH4Emulator::Assembler::asm_stc_stc_l},
        {"stc.l", &SH4Emulator::Assembler::asm_stc_stc_l},
        {"sts", &SH4Emulator::Assembler::asm_sts_sts_l},
        {"sts.l", &SH4Emulator::Assembler::asm_sts_sts_l},
        {"swap.b", &SH4Emulator::Assembler::asm_swap_b_w},
        {"swap.w", &SH4Emulator::Assembler::asm_swap_b_w},
        {"tas.b", &SH4Emulator::Assembler::asm_tas_b},
        {"test", &SH4Emulator::Assembler::asm_test_xor},
        {"xor", &SH4Emulator::Assembler::asm_test_xor},
        {"test.b", &SH4Emulator::Assembler::asm_test_b_xor_b},
        {"xor.b", &SH4Emulator::Assembler::asm_test_b_xor_b},
        {"trapa", &SH4Emulator::Assembler::asm_trapa},
        {"xtrct", &SH4Emulator::Assembler::asm_xtrct},
};

string SH4Emulator::disassemble_one(uint32_t pc, uint16_t op, bool double_precision, std::shared_ptr<const MemoryContext> mem) {
  le_uint16_t str = op;
  DisassemblyState s = {
      .pc = pc,
      .start_pc = pc,
      .double_precision = double_precision,
      .labels = nullptr,
      .branch_target_addresses = {},
      .r = StringReader(&str, sizeof(str)),
      .mem = mem,
  };
  return SH4Emulator::disassemble_one(s, op);
}

string SH4Emulator::disassemble(
    const void* data,
    size_t size,
    uint32_t start_pc,
    const multimap<uint32_t, string>* in_labels,
    bool double_precision,
    std::shared_ptr<const MemoryContext> mem) {
  static const multimap<uint32_t, string> empty_labels_map = {};

  DisassemblyState s = {
      .pc = start_pc,
      .start_pc = start_pc,
      .double_precision = double_precision,
      .labels = (in_labels ? in_labels : &empty_labels_map),
      .branch_target_addresses = {},
      .r = StringReader(data, size),
      .mem = mem,
  };

  const le_uint16_t* opcodes = reinterpret_cast<const le_uint16_t*>(data);

  // Phase 1: generate the disassembly for each opcode, and collect branch
  // target addresses
  size_t line_count = size / 2;
  forward_list<string> lines;
  auto add_line_it = lines.before_begin();
  for (size_t x = 0; x < line_count; x++, s.pc += 2) {
    uint32_t opcode = opcodes[x];
    string line = std::format("{:08X}  {:04X}  ", s.pc, opcode);
    line += SH4Emulator::disassemble_one(s, opcode);
    line += '\n';
    add_line_it = lines.emplace_after(add_line_it, std::move(line));
  }

  // Phase 2: add labels from the passed-in labels dict and from disassembled
  // branch opcodes; while doing so, count the number of bytes in the output.
  s.pc = start_pc;
  size_t ret_bytes = 0;
  auto branch_target_addresses_it = s.branch_target_addresses.lower_bound(start_pc);
  auto label_it = s.labels->lower_bound(start_pc);
  for (auto prev_line_it = lines.before_begin(), line_it = lines.begin();
      line_it != lines.end();
      prev_line_it = line_it++, s.pc += 2) {
    for (; label_it != s.labels->end() && label_it->first <= s.pc + 1; label_it++) {
      string label;
      if (label_it->first != s.pc) {
        label = std::format("{}: // at {:08X} (misaligned)\n",
            label_it->second, label_it->first);
      } else {
        label = std::format("{}:\n", label_it->second);
      }
      ret_bytes += label.size();
      prev_line_it = lines.emplace_after(prev_line_it, std::move(label));
    }
    for (; branch_target_addresses_it != s.branch_target_addresses.end() &&
        branch_target_addresses_it->first <= s.pc;
        branch_target_addresses_it++) {
      string label;
      const char* label_type = branch_target_addresses_it->second ? "fn" : "label";
      if (branch_target_addresses_it->first != s.pc) {
        label = std::format("{}{:08X}: // (misaligned)\n",
            label_type, branch_target_addresses_it->first);
      } else {
        label = std::format("{}{:08X}:\n",
            label_type, branch_target_addresses_it->first);
      }
      ret_bytes += label.size();
      prev_line_it = lines.emplace_after(prev_line_it, std::move(label));
    }

    ret_bytes += line_it->size();
  }

  // Phase 3: assemble the output lines into a single string and return it
  string ret;
  ret.reserve(ret_bytes);
  for (const auto& line : lines) {
    ret += line;
  }
  return ret;
}

EmulatorBase::AssembleResult SH4Emulator::assemble(
    const string& text,
    function<string(const string&)> get_include,
    uint32_t start_address) {
  Assembler a;
  a.start_address = start_address;
  a.assemble(text, get_include);

  EmulatorBase::AssembleResult res;
  res.code = std::move(a.code.str());
  res.label_offsets = std::move(a.label_offsets);
  res.metadata_keys = std::move(a.metadata_keys);
  return res;
}

EmulatorBase::AssembleResult SH4Emulator::assemble(
    const string& text,
    const vector<string>& include_dirs,
    uint32_t start_address) {
  if (include_dirs.empty()) {
    return SH4Emulator::assemble(text, nullptr, start_address);

  } else {
    unordered_set<string> get_include_stack;
    function<string(const string&)> get_include = [&](const string& name) -> string {
      for (const auto& dir : include_dirs) {
        string filename = dir + "/" + name + ".inc.s";
        if (std::filesystem::is_regular_file(filename)) {
          if (!get_include_stack.emplace(name).second) {
            throw runtime_error("mutual recursion between includes: " + name);
          }
          const auto& ret = SH4Emulator::assemble(load_file(filename), get_include, start_address).code;
          get_include_stack.erase(name);
          return ret;
        }
        filename = dir + "/" + name + ".inc.bin";
        if (std::filesystem::is_regular_file(filename)) {
          return load_file(filename);
        }
      }
      throw runtime_error("data not found for include: " + name);
    };
    return SH4Emulator::assemble(text, get_include, start_address);
  }
}

void SH4Emulator::Assembler::assemble(const string& text, function<string(const string&)> get_include) {
  string effective_text = text;
  strip_comments_inplace(effective_text);

  // First pass: generate args and labels and collect metadata
  StringReader r(effective_text);
  size_t line_num = 0;
  size_t stream_offset = 0;
  while (!r.eof()) {
    string line = r.get_line();
    line_num++;

    try {
      // Strip comments and whitespace
      size_t comment_pos = min<size_t>(min<size_t>(line.find("//"), line.find('#')), line.find(';'));
      if (comment_pos != string::npos) {
        line = line.substr(0, comment_pos);
      }
      strip_trailing_whitespace(line);
      strip_leading_whitespace(line);

      // If the line is blank, skip it
      if (line.empty()) {
        continue;

        // If the line ends with :, it's a label
      } else if (line.ends_with(":")) {
        line.pop_back();
        strip_trailing_whitespace(line);
        if (!label_offsets.emplace(line, stream_offset).second) {
          throw runtime_error(std::format("duplicate label: {}", line));
        }

      } else {
        // Get the opcode name and arguments
        vector<string> tokens = split(line, ' ', 1);
        if (tokens.size() == 0) {
          throw logic_error("no tokens in non-empty line");
        }
        const string& op_name = tokens[0];

        vector<Argument> args;
        if (tokens.size() == 2) {
          string& args_str = tokens[1];
          strip_leading_whitespace(args_str);
          if (op_name == ".meta") {
            size_t equals_pos = args_str.find('=');
            if (equals_pos == string::npos) {
              this->metadata_keys.emplace(args_str, "");
            } else {
              this->metadata_keys.emplace(args_str.substr(0, equals_pos), parse_data_string(args_str.substr(equals_pos + 1)));
            }
            continue;
          } else if (op_name == ".binary") {
            args.emplace_back(args_str, true);
          } else {
            vector<string> arg_strs = split(args_str, ',');
            for (auto& arg_str : arg_strs) {
              strip_leading_whitespace(arg_str);
              strip_trailing_whitespace(arg_str);
              args.emplace_back(arg_str);
            }
          }
        }

        const StreamItem& si = this->stream.emplace_back(StreamItem{stream_offset, line_num, op_name, std::move(args)});
        if (si.op_name == ".include") {
          si.check_arg_types({ArgType::BRANCH_TARGET});
          const string& inc_name = si.args[0].label_name;
          if (!get_include) {
            throw runtime_error("includes are not available");
          }
          string contents;
          try {
            const string& contents = this->includes_cache.at(inc_name);
            stream_offset += (contents.size() + 1) & (~1);
          } catch (const out_of_range&) {
            try {
              contents = get_include(inc_name);
            } catch (const exception& e) {
              throw runtime_error(std::format("failed to get include data: {}", e.what()));
            }
            stream_offset += (contents.size() + 1) & (~1);
            this->includes_cache.emplace(inc_name, std::move(contents));
          }

        } else if ((si.op_name == ".align")) {
          si.check_arg_types({ArgType::IMMEDIATE});
          uint32_t alignment = si.args[0].value;
          if (alignment & (alignment - 1)) {
            throw runtime_error(".align argument must be a power of two");
          }
          alignment--;
          stream_offset = (stream_offset + alignment) & (~alignment);

        } else if (si.op_name == ".data") {
          si.check_arg_types({ArgType::IMMEDIATE});
          stream_offset += 4;

        } else if (si.op_name == ".offsetof") {
          si.check_arg_types({ArgType::BRANCH_TARGET});
          stream_offset += 4;

        } else if (si.op_name == ".deltaof") {
          si.check_arg_types({ArgType::BRANCH_TARGET, ArgType::BRANCH_TARGET});
          stream_offset += 4;

        } else if ((si.op_name == ".binary") && !si.args.empty()) {
          si.check_arg_types({ArgType::RAW});
          // TODO: It's not great that we call parse_data_string here just to get
          // the length of the result data. Find a way to not have to do this.
          string data = parse_data_string(si.args[0].label_name);
          stream_offset += (data.size() + 1) & (~1);

        } else {
          stream_offset += 2;
        }
      }
    } catch (const exception& e) {
      throw runtime_error(std::format("(line {}) {}", line_num, e.what()));
    }
  }

  // Second pass: generate opcodes
  for (const auto& si : this->stream) {
    try {
      if (si.op_name == ".include") {
        si.check_arg_types({ArgType::BRANCH_TARGET});
        try {
          const string& include_contents = this->includes_cache.at(si.args[0].label_name);
          this->code.write(include_contents);
          while (this->code.size() & 1) {
            this->code.put_u8(0);
          }
        } catch (const out_of_range&) {
          throw logic_error("include data missing from cache");
        }

      } else if (si.op_name == ".align") {
        si.check_arg_types({ArgType::IMMEDIATE});
        size_t mask = si.args[0].value - 1;
        this->code.extend_to((this->code.size() + mask) & (~mask));

      } else if (si.op_name == ".data") {
        si.check_arg_types({ArgType::IMMEDIATE});
        this->code.put_u32l(si.args[0].value);

      } else if (si.op_name == ".offsetof") {
        si.check_arg_types({ArgType::BRANCH_TARGET});
        this->code.put_u32l(this->label_offsets.at(si.args[0].label_name));

      } else if (si.op_name == ".deltaof") {
        si.check_arg_types({ArgType::BRANCH_TARGET, ArgType::BRANCH_TARGET});
        this->code.put_u32l(this->label_offsets.at(si.args[1].label_name) - this->label_offsets.at(si.args[0].label_name));

      } else if (si.op_name == ".binary") {
        si.check_arg_types({ArgType::RAW});
        string data = parse_data_string(si.args[0].label_name);
        data.resize((data.size() + 1) & (~1), '\0');
        this->code.write(data);

      } else {
        auto fn = this->assemble_functions.at(si.op_name);
        this->code.put_u16l((this->*fn)(si));
      }
    } catch (const exception& e) {
      throw runtime_error(std::format("(line {}) {}", si.line_num, e.what()));
    }
  }
}

} // namespace ResourceDASM
