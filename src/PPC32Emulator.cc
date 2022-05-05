#include <inttypes.h>
#include <string.h>
#include <stdio.h>

#include <forward_list>
#include <set>
#include <string>
#include <deque>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>

#include "PPC32Emulator.hh"


using namespace std;

static inline uint8_t op_get_op(uint32_t op) {
  return ((op >> 26) & 0x0000003F);
}

static inline uint8_t op_get_crf1(uint32_t op) {
  return ((op >> 23) & 0x00000007);
}

static inline uint8_t op_get_crf2(uint32_t op){
  return ((op >> 18) & 0x00000007);
};

static inline uint8_t op_get_reg1(uint32_t op) {
  return ((op >> 21) & 0x0000001F);
}

static inline uint8_t op_get_reg2(uint32_t op) {
  return ((op >> 16) & 0x0000001F);
}

static inline uint8_t op_get_reg3(uint32_t op) {
  return ((op >> 11) & 0x0000001F);
}

static inline uint8_t op_get_reg4(uint32_t op) {
  return ((op >> 6) & 0x0000001F);
}

static inline uint8_t op_get_reg5(uint32_t op) {
  return ((op >> 1) & 0x0000001F);
}

static inline uint8_t op_get_bi(uint32_t op) {
  return ((op >> 16) & 0x0000001F);
}

static inline bool op_get_b_abs(uint32_t op) {
  return ((op >> 1) & 0x00000001);
}

static inline bool op_get_b_link(uint32_t op) {
  return (op & 0x00000001);
}

static inline uint16_t op_get_spr(uint32_t op) {
  return ((op >> 16) & 0x1F) | ((op >> 6) & 0x3E0);
}

static inline bool op_get_u(uint32_t op) {
  return ((op >> 26) & 0x00000001);
}

static inline bool op_get_rec4(uint32_t op) {
  return ((op >> 26) & 0x00000001);
}

static inline uint16_t op_get_subopcode(uint32_t op) {
  return ((op >> 1) & 0x000003FF);
}

static inline uint8_t op_get_short_subopcode(uint32_t op) {
  return ((op >> 1) & 0x0000001F);
}

static inline bool op_get_o(uint32_t op) {
  return ((op >> 10) & 1);
}

static inline bool op_get_rec(uint32_t op) {
  return (op & 0x00000001);
}

static inline uint16_t op_get_imm(uint32_t op) {
  return (op & 0x0000FFFF);
}

static inline int32_t op_get_imm_ext(uint32_t op) {
  uint32_t ret = op_get_imm(op);
  if (ret & 0x00008000) {
    return ret | 0xFFFF0000;
  } else {
    return ret;
  }
}

static inline int32_t op_get_b_target(uint32_t op) {
  int32_t target = (op & 0x03FFFFFC);
  if (target & 0x02000000) {
    return (target | 0xFC000000);
  }
  return target;
}

struct BranchBOField {
  uint8_t u;

  inline bool skip_condition() const {
    return (u >> 4) & 0x10;
  }
  inline bool branch_condition_value() const {
    return (u >> 3) & 0x01;
  }
  inline bool skip_ctr() const {
    return (u >> 2) & 1;
  }
  inline bool branch_if_ctr_zero() const {
    return (u >> 1) & 1;
  }
  inline bool branch_likely() const {
    return u & 1;
  }
};

static BranchBOField op_get_bo(uint32_t op) {
  return {.u = static_cast<uint8_t>((op >> 21) & 0x1F)};
}

const char* mnemonic_for_bc(uint8_t bo, uint8_t bi) {
  uint16_t as = ((bo & 0x1E) << 5) | (bi & 3);
  if (as & 0x0080) {
    as &= 0x03BF;
  }
  if (as & 0x0200) {
    as &= 0x02FF;
  }

  switch (as) {
    case 0x0000:
      return "dnzf";
    case 0x0001:
      return "dnzf";
    case 0x0080:
      return "ge";
    case 0x0081:
      return "le";
    case 0x0082:
      return "ne";
    case 0x0083:
      return "ns";
    case 0x0103:
      return "dnzt";
    case 0x0140:
      return "dzt";
    case 0x0141:
      return "dzt";
    case 0x0180:
      return "lt";
    case 0x0181:
      return "gt";
    case 0x0182:
      return "eq";
    case 0x0183:
      return "so";
    case 0x0200:
      return "dnz";
    case 0x0243:
      return "dz";
    case 0x0280:
      return "";
    default:
      return nullptr;
  }
}

const char* name_for_spr(uint16_t spr) {
  switch (spr) {
    case 1:
      return "xer";
    case 8:
      return "lr";
    case 9:
      return "ctr";
    case 18:
      return "dsisr";
    case 19:
      return "dar";
    case 22:
      return "dec";
    case 25:
      return "sdr1";
    case 26:
      return "srr0";
    case 27:
      return "srr1";
    case 272:
      return "sprg0";
    case 273:
      return "sprg1";
    case 274:
      return "sprg2";
    case 275:
      return "sprg3";
    case 282:
      return "ear";
    case 287:
      return "pvr";
    case 528:
      return "ibat0u";
    case 529:
      return "ibat0l";
    case 530:
      return "ibat1u";
    case 531:
      return "ibat1l";
    case 532:
      return "ibat2u";
    case 533:
      return "ibat2l";
    case 534:
      return "ibat3u";
    case 535:
      return "ibat3l";
    case 536:
      return "dbat0u";
    case 537:
      return "dbat0l";
    case 538:
      return "dbat1u";
    case 539:
      return "dbat1l";
    case 540:
      return "dbat2u";
    case 541:
      return "dbat2l";
    case 542:
      return "dbat3u";
    case 543:
      return "dbat3l";
    case 1013:
      return "dabr";
    default:
      return nullptr;
  }
}



bool PPC32Emulator::should_branch(uint32_t op) {
  BranchBOField bo = op_get_bo(op);
  if (!bo.skip_ctr()) {
    this->regs.ctr--;
  }
  bool ctr_ok = bo.skip_ctr() | ((this->regs.ctr == 0) == bo.branch_if_ctr_zero());
  bool cond_ok = bo.skip_condition() |
      (((this->regs.cr.u >> (31 - op_get_bi(op))) & 1) == bo.branch_condition_value());
  return ctr_ok && cond_ok;
}

void PPC32Emulator::set_cr_bits_int(uint8_t crf, int32_t value) {
  uint8_t cr_bits = ((value < 0) << 3) | ((value > 0) << 2) | ((value == 0) << 1) | this->regs.xer.get_so();
  this->regs.cr.replace_field(crf, cr_bits);
}



void PPC32Emulator::exec_unimplemented(uint32_t op) {
  string dasm = this->disassemble_one(this->regs.pc, op);
  throw runtime_error(string_printf("unimplemented opcode: %08X %s", op, dasm.c_str()));
}

string PPC32Emulator::dasm_unimplemented(uint32_t, uint32_t, map<uint32_t, bool>&) {
  return "<<unimplemented>>";
}

void PPC32Emulator::exec_invalid(uint32_t) {
  // TODO: this should trigger an interrupt probably
  throw runtime_error("invalid opcode");
}

string PPC32Emulator::dasm_invalid(uint32_t, uint32_t, map<uint32_t, bool>&) {
  return ".invalid";
}



void PPC32Emulator::exec_0C_twi(uint32_t op) {
  this->exec_unimplemented(op); // 000011 TTTTT AAAAA IIIIIIIIIIIIIIII
}

string PPC32Emulator::dasm_0C_twi(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t to = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm_ext(op);
  return string_printf("twi       %hhu, r%hhu, %hd", to, ra, imm);
}



void PPC32Emulator::exec_1C_mulli(uint32_t op) {
  // 000111 DDDDD AAAAA IIIIIIIIIIIIIIII
  this->regs.r[op_get_reg1(op)].s =
      this->regs.r[op_get_reg2(op)].s * op_get_imm_ext(op);
}

string PPC32Emulator::dasm_1C_mulli(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm_ext(op);
  return string_printf("mulli     r%hhu, r%hhu, %hd", rd, ra, imm);
}



void PPC32Emulator::exec_20_subfic(uint32_t op) {
  // 001000 DDDDD AAAAA IIIIIIIIIIIIIIII
  this->regs.r[op_get_reg1(op)].s =
      op_get_imm_ext(op) - this->regs.r[op_get_reg2(op)].s;
  this->exec_unimplemented(op); // TODO: set XER[CA]
}

string PPC32Emulator::dasm_20_subfic(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm_ext(op);
  return string_printf("subfic    r%hhu, r%hhu, %hd", rd, ra, imm);
}



void PPC32Emulator::exec_28_cmpli(uint32_t op) {
  // 001010 CCC 0 L AAAAA IIIIIIIIIIIIIIII
  if (op & 0x00600000) {
    throw runtime_error("invalid 28 (cmpli) opcode");
  }
  uint8_t a_reg = op_get_reg2(op);
  uint32_t imm = op_get_imm(op);
  uint8_t crf_num = op_get_crf1(op);
  uint8_t crf_res = this->regs.xer.get_so() ? 1 : 0;
  if (this->regs.r[a_reg].u < imm) {
    crf_res |= 8;
  } else if (this->regs.r[a_reg].u > imm) {
    crf_res |= 4;
  } else {
    crf_res |= 2;
  }
  this->regs.cr.replace_field(crf_num, crf_res);
}

string PPC32Emulator::dasm_28_cmpli(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  if (op & 0x00600000) {
    return ".invalid  cmpli";
  }
  uint8_t crf = op_get_crf1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm(op);
  if (crf) {
    return string_printf("cmplwi    cr%hhu, r%hhu, %hd", crf, ra, imm);
  } else {
    return string_printf("cmplwi    r%hhu, %hd", ra, imm);
  }
}



void PPC32Emulator::exec_2C_cmpi(uint32_t op) {
  // 001011 CCC 0 L AAAAA IIIIIIIIIIIIIIII
  if (op & 0x00600000) {
    throw runtime_error("invalid 2C (cmpi) opcode");
  }
  uint8_t a_reg = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  uint8_t crf_num = op_get_crf1(op);
  uint8_t crf_res = this->regs.xer.get_so() ? 1 : 0;
  if (this->regs.r[a_reg].s < imm) {
    crf_res |= 8;
  } else if (this->regs.r[a_reg].s > imm) {
    crf_res |= 4;
  } else {
    crf_res |= 2;
  }
  this->regs.cr.replace_field(crf_num, crf_res);
}

string PPC32Emulator::dasm_2C_cmpi(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  if (op & 0x00600000) {
    return ".invalid  cmpi";
  }
  uint8_t crf = op_get_crf1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm(op);
  if (crf) {
    return string_printf("cmpwi     cr%hhu, r%hhu, %hd", crf, ra, imm);
  } else {
    return string_printf("cmpwi     r%hhu, %hd", ra, imm);
  }
}



void PPC32Emulator::exec_30_34_addic(uint32_t op) {
  // 00110 R DDDDD AAAAA IIIIIIIIIIIIIIII
  uint8_t rd = op_get_reg1(op);
  this->regs.r[rd].s = this->regs.r[op_get_reg2(op)].s + op_get_imm_ext(op);
  this->exec_unimplemented(op); // TODO: set XER[CA]
  if (op_get_rec4(op)) {
    this->set_cr_bits_int(0, this->regs.r[rd].s);
  }
}

string PPC32Emulator::dasm_30_34_addic(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  bool rec = op_get_rec4(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (imm < 0) {
    return string_printf("subic%c    r%hhu, r%hhu, %d", rec ? '.' : ' ', rd, ra, -imm);
  } else {
    return string_printf("addic%c    r%hhu, r%hhu, %d", rec ? '.' : ' ', rd, ra, imm);
  }
}



void PPC32Emulator::exec_38_addi(uint32_t op) {
  // 001110 DDDDD AAAAA IIIIIIIIIIIIIIII
  uint8_t a_reg = op_get_reg2(op);
  uint8_t d_reg = op_get_reg1(op);
  if (a_reg == 0) {
    this->regs.r[d_reg].s = op_get_imm_ext(op);
  } else {
    this->regs.r[d_reg].s = this->regs.r[a_reg].s + op_get_imm_ext(op);
  }
}

string PPC32Emulator::dasm_38_addi(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (ra == 0) {
    return string_printf("li        r%hhu, 0x%04" PRIX32, rd, imm);
  } else {
    if (imm < 0) {
      return string_printf("subi      r%hhu, r%hhu, 0x%04X", rd, ra, -imm);
    } else {
      return string_printf("addi      r%hhu, r%hhu, 0x%04" PRIX32, rd, ra, imm);
    }
  }
}



void PPC32Emulator::exec_3C_addis(uint32_t op) {
  // 001111 DDDDD AAAAA IIIIIIIIIIIIIIII
  uint8_t a_reg = op_get_reg2(op);
  uint8_t d_reg = op_get_reg1(op);
  if (a_reg == 0) {
    this->regs.r[d_reg].s = op_get_imm(op) << 16;
  } else {
    this->regs.r[d_reg].s = this->regs.r[a_reg].s + (op_get_imm(op) << 16);
  }
}

string PPC32Emulator::dasm_3C_addis(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm(op);
  if (ra == 0) {
    return string_printf("lis       r%hhu, 0x%04hX", rd, imm);
  } else {
    if (imm < 0) {
      return string_printf("subis     r%hhu, r%hhu, 0x%04X", rd, ra, -imm);
    } else {
      return string_printf("addis     r%hhu, r%hhu, 0x%04hX", rd, ra, imm);
    }
  }
}



void PPC32Emulator::exec_40_bc(uint32_t op) {
  // 010000 OOOOO IIIII DDDDDDDDDDDDDD A L

  // TODO: The manual appears to show that this happens even if the branch isn't
  // taken, so it should be ok to do it first. Is this actually true?
  if (op_get_b_link(op)) {
    this->regs.lr = this->regs.pc + 4;
  }

  auto bo = op_get_bo(op);
  if (!bo.skip_ctr()) {
    this->regs.ctr--;
  }
  bool ctr_ok = bo.skip_ctr()
      || ((this->regs.ctr == 0) == bo.branch_if_ctr_zero());
  bool cond_ok = bo.skip_condition()
      || (((this->regs.cr.u >> (31 - op_get_bi(op))) & 1) == bo.branch_condition_value());
  // Note: we subtract 4 here to correct for the fact that we always add 4 after
  // every opcode, even if it overwrote pc
  if (ctr_ok && cond_ok) {
    if (op_get_b_abs(op)) {
      this->regs.pc = (op_get_imm_ext(op) & (~3)) - 4;
    } else {
      this->regs.pc = (this->regs.pc + (op_get_imm_ext(op) & (~3))) - 4;
    }
  }
}

string PPC32Emulator::dasm_40_bc(uint32_t pc, uint32_t op, map<uint32_t, bool>& branch_target_addresses) {
  BranchBOField bo = op_get_bo(op);
  uint8_t bi = op_get_bi(op);
  bool absolute = op_get_b_abs(op);
  bool link = op_get_b_link(op);
  int32_t offset = op_get_imm_ext(op) & 0xFFFFFFFC;
  uint32_t target_addr = (absolute ? 0 : pc) + offset;

  if (link) {
    branch_target_addresses[target_addr] = true;
  } else {
    branch_target_addresses.emplace(target_addr, false);
  }

  const char* suffix;
  if (absolute && link) {
    suffix = "la";
  } else if (absolute) {
    suffix = "a ";
  } else if (link) {
    suffix = "l ";
  } else {
    suffix = "  ";
  }

  uint16_t as = ((bo.u & 0x1E) << 5) | (bi & 3);
  if (as & 0x0080) {
    as &= 0x03BF;
  }
  if (as & 0x0200) {
    as &= 0x02FF;
  }

  const char* mnemonic = mnemonic_for_bc(bo.u, bi);
  string ret = "b";
  if (mnemonic) {
    ret += mnemonic;
    ret += suffix;
    ret.resize(10, ' ');
    if (bi & 0x1C) {
      ret += string_printf("cr%d, ", (bi >> 2) & 7);
    }
  } else {
    ret = string_printf("bc%s     %u, %d, ", suffix, bo.u, bi);
  }

  if (absolute) {
    ret += string_printf("0x%08X", target_addr);
  } else {
    if (offset < 0) {
      ret += string_printf("-0x%08X /* %08X */", -offset, target_addr);
    } else {
      ret += string_printf("+0x%08X /* %08X */", offset, target_addr);
    }
  }

  return ret;
}



void PPC32Emulator::exec_44_sc(uint32_t op) {
  // 010001 00000000000000000000000010
  if (this->syscall_handler) {
    this->syscall_handler(*this);
  } else {
    this->exec_unimplemented(op);
  }
}

string PPC32Emulator::dasm_44_sc(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  if (op == 0x44000002) {
    return "sc";
  }
  return ".invalid  sc";
}



void PPC32Emulator::exec_48_b(uint32_t op) {
  // 010010 TTTTTTTTTTTTTTTTTTTTTTTT A L

  if (op_get_b_link(op)) {
    this->regs.lr = this->regs.pc + 4;
  }

  // Note: we subtract 4 here to correct for the fact that we always add 4 after
  // every opcode, even if it overwrote pc
  if (op_get_b_abs(op)) {
    this->regs.pc = op_get_b_target(op) - 4;
  } else {
    this->regs.pc = this->regs.pc + op_get_b_target(op) - 4;
  }
}

string PPC32Emulator::dasm_48_b(uint32_t pc, uint32_t op, map<uint32_t, bool>& branch_target_addresses) {
  bool absolute = op_get_b_abs(op);
  bool link = op_get_b_link(op);
  int32_t offset = op_get_b_target(op);
  uint32_t target_addr = (absolute ? 0 : pc) + offset;
  if (link) {
    branch_target_addresses[target_addr] = true;
  } else {
    branch_target_addresses.emplace(target_addr, false);
  }

  const char* suffix;
  if (absolute && link) {
    suffix = "la";
  } else if (absolute) {
    suffix = "a ";
  } else if (link) {
    suffix = "l ";
  } else {
    suffix = "  ";
  }

  if (absolute) {
    return string_printf("b%s       0x%08X /* ", suffix, target_addr);
  } else {
    if (offset < 0) {
      return string_printf("b%s       -0x%08X /* %08X */", suffix, -offset, target_addr);
    } else {
      return string_printf("b%s       +0x%08X /* %08X */", suffix, offset, target_addr);
    }
  }
}



void PPC32Emulator::exec_4C(uint32_t op) {
  switch (op_get_subopcode(op)) {
    case 0x000:
      this->exec_4C_000_mcrf(op);
      break;
    case 0x010:
      this->exec_4C_010_bclr(op);
      break;
    case 0x021:
      this->exec_4C_021_crnor(op);
      break;
    case 0x032:
      this->exec_4C_032_rfi(op);
      break;
    case 0x081:
      this->exec_4C_081_crandc(op);
      break;
    case 0x096:
      this->exec_4C_096_isync(op);
      break;
    case 0x0C1:
      this->exec_4C_0C1_crxor(op);
      break;
    case 0x0E1:
      this->exec_4C_0E1_crnand(op);
      break;
    case 0x101:
      this->exec_4C_101_crand(op);
      break;
    case 0x121:
      this->exec_4C_121_creqv(op);
      break;
    case 0x1A1:
      this->exec_4C_1A1_crorc(op);
      break;
    case 0x1C1:
      this->exec_4C_1C1_cror(op);
      break;
    case 0x210:
      this->exec_4C_210_bcctr(op);
      break;
    default:
      throw runtime_error("invalid 4C subopcode");
  }
}

string PPC32Emulator::dasm_4C(uint32_t pc, uint32_t op, map<uint32_t, bool>& branch_target_addresses) {
  switch (op_get_subopcode(op)) {
    case 0x000:
      return PPC32Emulator::dasm_4C_000_mcrf(pc, op, branch_target_addresses);
    case 0x010:
      return PPC32Emulator::dasm_4C_010_bclr(pc, op, branch_target_addresses);
    case 0x021:
      return PPC32Emulator::dasm_4C_021_crnor(pc, op, branch_target_addresses);
    case 0x032:
      return PPC32Emulator::dasm_4C_032_rfi(pc, op, branch_target_addresses);
    case 0x081:
      return PPC32Emulator::dasm_4C_081_crandc(pc, op, branch_target_addresses);
    case 0x096:
      return PPC32Emulator::dasm_4C_096_isync(pc, op, branch_target_addresses);
    case 0x0C1:
      return PPC32Emulator::dasm_4C_0C1_crxor(pc, op, branch_target_addresses);
    case 0x0E1:
      return PPC32Emulator::dasm_4C_0E1_crnand(pc, op, branch_target_addresses);
    case 0x101:
      return PPC32Emulator::dasm_4C_101_crand(pc, op, branch_target_addresses);
    case 0x121:
      return PPC32Emulator::dasm_4C_121_creqv(pc, op, branch_target_addresses);
    case 0x1A1:
      return PPC32Emulator::dasm_4C_1A1_crorc(pc, op, branch_target_addresses);
    case 0x1C1:
      return PPC32Emulator::dasm_4C_1C1_cror(pc, op, branch_target_addresses);
    case 0x210:
      return PPC32Emulator::dasm_4C_210_bcctr(pc, op, branch_target_addresses);
    default:
      return ".invalid  4C";
  }
}



void PPC32Emulator::exec_4C_000_mcrf(uint32_t op) {
  this->exec_unimplemented(op); // 010011 DDD 00 SSS 0000000 0000000000 0
}

string PPC32Emulator::dasm_4C_000_mcrf(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return string_printf("mcrf      cr%hhu, cr%hhu", op_get_crf1(op),
      op_get_crf2(op));
}



void PPC32Emulator::exec_4C_010_bclr(uint32_t op) {
  // 010011 OOOOO IIIII 00000 0000010000 L
  if (op_get_b_link(op)) {
    this->regs.lr = this->regs.pc + 4;
  }
  if (this->should_branch(op)) {
    this->regs.pc = (this->regs.lr & 0xFFFFFFFC) - 4;
  }
}

string PPC32Emulator::dasm_4C_010_bclr(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  BranchBOField bo = op_get_bo(op);
  uint8_t bi = op_get_bi(op);
  bool l = op_get_b_link(op);

  const char* mnemonic = mnemonic_for_bc(bo.u, bi);
  string ret = "b";
  if (mnemonic) {
    ret += mnemonic;
    ret += "lr";
    if (l) {
      ret += 'l';
    }
    if (bi & 0x1C) {
      ret.resize(10, ' ');
      ret += string_printf("cr%d", (bi >> 2) & 7);
    }
  } else {
    ret = string_printf("bclr%c     %u, %d", l ? 'l' : ' ', bo.u, bi);
  }
  return ret;
}



void PPC32Emulator::exec_4C_021_crnor(uint32_t op) {
  this->exec_unimplemented(op); // 010011 DDDDD AAAAA BBBBB 0000100001 0
}

string PPC32Emulator::dasm_4C_021_crnor(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  return string_printf("crnor     crb%hhu, crb%hhu, crb%hhu", d, a, b);
}



void PPC32Emulator::exec_4C_032_rfi(uint32_t op) {
  this->exec_unimplemented(op); // 010011 00000 00000 00000 0000110010 0
}

string PPC32Emulator::dasm_4C_032_rfi(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  if (op == 0x4C000064) {
    return "rfi";
  }
  return ".invalid  rfi";
}



void PPC32Emulator::exec_4C_081_crandc(uint32_t op) {
  this->exec_unimplemented(op); // 010011 DDDDD AAAAA BBBBB 0010000001 0
}

string PPC32Emulator::dasm_4C_081_crandc(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  return string_printf("crandc    crb%hhu, crb%hhu, crb%hhu", d, a, b);
}



void PPC32Emulator::exec_4C_096_isync(uint32_t op) {
  this->exec_unimplemented(op); // 010011 00000 00000 00000 0010010110 0
}

string PPC32Emulator::dasm_4C_096_isync(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  if (op == 0x4C00012C) {
    return "isync";
  }
  return ".invalid  isync";
}



void PPC32Emulator::exec_4C_0C1_crxor(uint32_t op) {
  this->exec_unimplemented(op); // 010011 DDDDD AAAAA BBBBB 0011000001 0
}

string PPC32Emulator::dasm_4C_0C1_crxor(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  return string_printf("crxor     crb%hhu, crb%hhu, crb%hhu", d, a, b);
}



void PPC32Emulator::exec_4C_0E1_crnand(uint32_t op) {
  this->exec_unimplemented(op); // 010011 DDDDD AAAAA BBBBB 0011100001 0
}

string PPC32Emulator::dasm_4C_0E1_crnand(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  return string_printf("crnand    crb%hhu, crb%hhu, crb%hhu", d, a, b);
}



void PPC32Emulator::exec_4C_101_crand(uint32_t op) {
  this->exec_unimplemented(op); // 010011 DDDDD AAAAA BBBBB 0100000001 0
}

string PPC32Emulator::dasm_4C_101_crand(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  return string_printf("crand     crb%hhu, crb%hhu, crb%hhu", d, a, b);
}



void PPC32Emulator::exec_4C_121_creqv(uint32_t op) {
  this->exec_unimplemented(op); // 010011 DDDDD AAAAA BBBBB 0101000001 0
}

string PPC32Emulator::dasm_4C_121_creqv(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  return string_printf("creqv     crb%hhu, crb%hhu, crb%hhu", d, a, b);
}



void PPC32Emulator::exec_4C_1A1_crorc(uint32_t op) {
  this->exec_unimplemented(op); // 010011 DDDDD AAAAA BBBBB 0110100001 0
}

string PPC32Emulator::dasm_4C_1A1_crorc(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  return string_printf("crorc     crb%hhu, crb%hhu, crb%hhu", d, a, b);
}



void PPC32Emulator::exec_4C_1C1_cror(uint32_t op) {
  this->exec_unimplemented(op); // 010011 DDDDD AAAAA BBBBB 0111000001 0
}

string PPC32Emulator::dasm_4C_1C1_cror(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  return string_printf("cror      crb%hhu, crb%hhu, crb%hhu", d, a, b);
}



void PPC32Emulator::exec_4C_210_bcctr(uint32_t op) {
  // 010011 OOOOO IIIII 00000 1000010000 L
  if (op_get_b_link(op)) {
    this->regs.lr = this->regs.pc + 4;
  }
  if (this->should_branch(op)) {
    this->regs.pc = (this->regs.ctr & 0xFFFFFFFC) - 4;
  }
}

string PPC32Emulator::dasm_4C_210_bcctr(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  BranchBOField bo = op_get_bo(op);
  uint8_t bi = op_get_bi(op);
  bool l = op_get_b_link(op);

  const char* mnemonic = mnemonic_for_bc(bo.u, bi);
  string ret = "b";
  if (mnemonic) {
    ret += mnemonic;
    ret += "ctr";
    if (l) {
      ret += 'l';
    }
    if (bi & 0x1C) {
      ret.resize(10, ' ');
      ret += string_printf("cr%d", (bi >> 2) & 7);
    }
  } else {
    ret = string_printf("bcctr%c    %u, %d, ", l ? 'l' : ' ', bo.u, bi);
  }
  return ret;
}



void PPC32Emulator::exec_50_rlwimi(uint32_t op) {
  this->exec_unimplemented(op); // 010100 SSSSS AAAAA <<<<< MMMMM NNNNN R
}

string PPC32Emulator::dasm_50_rlwimi(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t sh = op_get_reg3(op);
  uint8_t ms = op_get_reg4(op);
  uint8_t me = op_get_reg5(op);
  bool rec = op_get_rec(op);
  return string_printf("rlwimi%c   r%hhu, r%hhu, %hhu, %hhu, %hhu",
      rec ? '.' : ' ', ra, rs, sh, ms, me);
}



void PPC32Emulator::exec_54_rlwinm(uint32_t op) {
  // 010101 SSSSS AAAAA <<<<< MMMMM NNNNN R
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t sh = op_get_reg3(op);
  uint8_t ms = op_get_reg4(op);
  uint8_t me = op_get_reg5(op);
  bool rec = op_get_rec(op);

  uint32_t v = (this->regs.r[rs].u << sh) | (this->regs.r[rs].u >> (32 - sh));
  uint32_t mask = (0xFFFFFFFF >> ms) & (0xFFFFFFFF << (31 - me));
  this->regs.r[ra].u = v & mask;
  if (rec) {
    this->set_cr_bits_int(0, this->regs.r[ra].s);
  }
}

string PPC32Emulator::dasm_54_rlwinm(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t sh = op_get_reg3(op);
  uint8_t ms = op_get_reg4(op);
  uint8_t me = op_get_reg5(op);
  bool rec = op_get_rec(op);
  return string_printf("rlwinm%c   r%hhu, r%hhu, %hhu, %hhu, %hhu",
      rec ? '.' : ' ', ra, rs, sh, ms, me);
}



void PPC32Emulator::exec_5C_rlwnm(uint32_t op) {
  this->exec_unimplemented(op); // 010111 SSSSS AAAAA BBBBB MMMMM NNNNN R
}

string PPC32Emulator::dasm_5C_rlwnm(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  uint8_t ms = op_get_reg4(op);
  uint8_t me = op_get_reg5(op);
  bool rec = op_get_rec(op);
  return string_printf("rlwnm%c    r%hhu, r%hhu, r%hhu, %hhu, %hhu",
      rec ? '.' : ' ', ra, rs, rb, ms, me);
}



void PPC32Emulator::exec_60_ori(uint32_t op) {
  // 011000 SSSSS AAAAA IIIIIIIIIIIIIIII
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint16_t imm = op_get_imm(op);
  this->regs.r[ra].u = this->regs.r[rs].u | imm;
}

string PPC32Emulator::dasm_60_ori(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm(op);
  if (imm == 0 && rs == ra) {
    if (rs == 0) {
      return "nop";
    } else {
      return string_printf("nop       r%hhu", rs);
    }
  } else {
    return string_printf("ori       r%hhu, r%hhu, 0x%04hX", ra, rs, imm);
  }
}



void PPC32Emulator::exec_64_oris(uint32_t op) {
  // 011001 SSSSS AAAAA IIIIIIIIIIIIIIII
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint32_t imm = op_get_imm(op);
  this->regs.r[ra].u = this->regs.r[rs].u | (imm << 16);
}

string PPC32Emulator::dasm_64_oris(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm(op);
  return string_printf("oris      r%hhu, r%hhu, 0x%04hX", ra, rs, imm);
}



void PPC32Emulator::exec_68_xori(uint32_t op) {
  this->exec_unimplemented(op); // 011010 SSSSS AAAAA IIIIIIIIIIIIIIII
}

string PPC32Emulator::dasm_68_xori(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm(op);
  return string_printf("xori      r%hhu, r%hhu, 0x%04hX", ra, rs, imm);
}



void PPC32Emulator::exec_6C_xoris(uint32_t op) {
  this->exec_unimplemented(op); // 011011 SSSSS AAAAA IIIIIIIIIIIIIIII
}

string PPC32Emulator::dasm_6C_xoris(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm(op);
  return string_printf("xoris     r%hhu, r%hhu, 0x%04hX", ra, rs, imm);
}



void PPC32Emulator::exec_70_andi_rec(uint32_t op) {
  this->exec_unimplemented(op); // 011100 SSSSS AAAAA IIIIIIIIIIIIIIII
}

string PPC32Emulator::dasm_70_andi_rec(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm(op);
  return string_printf("andi.     r%hhu, r%hhu, 0x%04hX", ra, rs, imm);
}



void PPC32Emulator::exec_74_andis_rec(uint32_t op) {
  this->exec_unimplemented(op); // 011101 SSSSS AAAAA IIIIIIIIIIIIIIII
}

string PPC32Emulator::dasm_74_andis_rec(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm(op);
  return string_printf("andis.    r%hhu, r%hhu, 0x%04hX", ra, rs, imm);
}



void PPC32Emulator::exec_7C(uint32_t op) {
  switch (op_get_subopcode(op)) {
    case 0x000:
      this->exec_7C_000_cmp(op);
      break;
    case 0x004:
      this->exec_7C_004_tw(op);
      break;
    case 0x008:
      this->exec_7C_008_208_subfc(op);
      break;
    case 0x00A:
      this->exec_7C_00A_20A_addc(op);
      break;
    case 0x00B:
      this->exec_7C_00B_mulhwu(op);
      break;
    case 0x013:
      this->exec_7C_013_mfcr(op);
      break;
    case 0x014:
      this->exec_7C_014_lwarx(op);
      break;
    case 0x017:
      this->exec_7C_017_lwzx(op);
      break;
    case 0x018:
      this->exec_7C_018_slw(op);
      break;
    case 0x01A:
      this->exec_7C_01A_cntlzw(op);
      break;
    case 0x01C:
      this->exec_7C_01C_and(op);
      break;
    case 0x020:
      this->exec_7C_020_cmpl(op);
      break;
    case 0x028:
      this->exec_7C_028_228_subf(op);
      break;
    case 0x036:
      this->exec_7C_036_dcbst(op);
      break;
    case 0x037:
      this->exec_7C_037_lwzux(op);
      break;
    case 0x03C:
      this->exec_7C_03C_andc(op);
      break;
    case 0x04B:
      this->exec_7C_04B_mulhw(op);
      break;
    case 0x053:
      this->exec_7C_053_mfmsr(op);
      break;
    case 0x056:
      this->exec_7C_056_dcbf(op);
      break;
    case 0x057:
      this->exec_7C_057_lbzx(op);
      break;
    case 0x068:
    case 0x268:
      this->exec_7C_068_268_neg(op);
      break;
    case 0x077:
      this->exec_7C_077_lbzux(op);
      break;
    case 0x07C:
      this->exec_7C_07C_nor(op);
      break;
    case 0x088:
    case 0x288:
      this->exec_7C_088_288_subfe(op);
      break;
    case 0x08A:
    case 0x28A:
      this->exec_7C_08A_28A_adde(op);
      break;
    case 0x090:
      this->exec_7C_090_mtcrf(op);
      break;
    case 0x092:
      this->exec_7C_092_mtmsr(op);
      break;
    case 0x096:
      this->exec_7C_096_stwcx_rec(op);
      break;
    case 0x097:
      this->exec_7C_097_stwx(op);
      break;
    case 0x0B7:
      this->exec_7C_0B7_stwux(op);
      break;
    case 0x0C8:
    case 0x2C8:
      this->exec_7C_0C8_2C8_subfze(op);
      break;
    case 0x0CA:
    case 0x2CA:
      this->exec_7C_0CA_2CA_addze(op);
      break;
    case 0x0D2:
      this->exec_7C_0D2_mtsr(op);
      break;
    case 0x0D7:
      this->exec_7C_0D7_stbx(op);
      break;
    case 0x0E8:
    case 0x2E8:
      this->exec_7C_0E8_2E8_subfme(op);
      break;
    case 0x0EA:
    case 0x2EA:
      this->exec_7C_0EA_2EA_addme(op);
      break;
    case 0x0EB:
    case 0x2EB:
      this->exec_7C_0EB_2EB_mullw(op);
      break;
    case 0x0F2:
      this->exec_7C_0F2_mtsrin(op);
      break;
    case 0x0F6:
      this->exec_7C_0F6_dcbtst(op);
      break;
    case 0x0F7:
      this->exec_7C_0F7_stbux(op);
      break;
    case 0x10A:
    case 0x30A:
      this->exec_7C_10A_30A_add(op);
      break;
    case 0x116:
      this->exec_7C_116_dcbt(op);
      break;
    case 0x117:
      this->exec_7C_117_lhzx(op);
      break;
    case 0x11C:
      this->exec_7C_11C_eqv(op);
      break;
    case 0x132:
      this->exec_7C_132_tlbie(op);
      break;
    case 0x136:
      this->exec_7C_136_eciwx(op);
      break;
    case 0x137:
      this->exec_7C_137_lhzux(op);
      break;
    case 0x13C:
      this->exec_7C_13C_xor(op);
      break;
    case 0x153:
      this->exec_7C_153_mfspr(op);
      break;
    case 0x157:
      this->exec_7C_157_lhax(op);
      break;
    case 0x172:
      this->exec_7C_172_tlbia(op);
      break;
    case 0x173:
      this->exec_7C_173_mftb(op);
      break;
    case 0x177:
      this->exec_7C_177_lhaux(op);
      break;
    case 0x197:
      this->exec_7C_197_sthx(op);
      break;
    case 0x19C:
      this->exec_7C_19C_orc(op);
      break;
    case 0x1B6:
      this->exec_7C_1B6_ecowx(op);
      break;
    case 0x1B7:
      this->exec_7C_1B7_sthux(op);
      break;
    case 0x1BC:
      this->exec_7C_1BC_or(op);
      break;
    case 0x1CB:
    case 0x3CB:
      this->exec_7C_1CB_3CB_divwu(op);
      break;
    case 0x1D3:
      this->exec_7C_1D3_mtspr(op);
      break;
    case 0x1D6:
      this->exec_7C_1D6_dcbi(op);
      break;
    case 0x1DC:
      this->exec_7C_1DC_nand(op);
      break;
    case 0x1EB:
    case 0x3EB:
      this->exec_7C_1EB_3EB_divw(op);
      break;
    case 0x200:
      this->exec_7C_200_mcrxr(op);
      break;
    case 0x215:
      this->exec_7C_215_lswx(op);
      break;
    case 0x216:
      this->exec_7C_216_lwbrx(op);
      break;
    case 0x217:
      this->exec_7C_217_lfsx(op);
      break;
    case 0x218:
      this->exec_7C_218_srw(op);
      break;
    case 0x236:
      this->exec_7C_236_tlbsync(op);
      break;
    case 0x237:
      this->exec_7C_237_lfsux(op);
      break;
    case 0x253:
      this->exec_7C_253_mfsr(op);
      break;
    case 0x255:
      this->exec_7C_255_lswi(op);
      break;
    case 0x256:
      this->exec_7C_256_sync(op);
      break;
    case 0x257:
      this->exec_7C_257_lfdx(op);
      break;
    case 0x277:
      this->exec_7C_277_lfdux(op);
      break;
    case 0x293:
      this->exec_7C_293_mfsrin(op);
      break;
    case 0x295:
      this->exec_7C_295_stswx(op);
      break;
    case 0x296:
      this->exec_7C_296_stwbrx(op);
      break;
    case 0x297:
      this->exec_7C_297_stfsx(op);
      break;
    case 0x2B7:
      this->exec_7C_2B7_stfsux(op);
      break;
    case 0x2E5:
      this->exec_7C_2E5_stswi(op);
      break;
    case 0x2E7:
      this->exec_7C_2E7_stfdx(op);
      break;
    case 0x2F6:
      this->exec_7C_2F6_dcba(op);
      break;
    case 0x2F7:
      this->exec_7C_2F7_stfdux(op);
      break;
    case 0x316:
      this->exec_7C_316_lhbrx(op);
      break;
    case 0x318:
      this->exec_7C_318_sraw(op);
      break;
    case 0x338:
      this->exec_7C_338_srawi(op);
      break;
    case 0x356:
      this->exec_7C_356_eieio(op);
      break;
    case 0x396:
      this->exec_7C_396_sthbrx(op);
      break;
    case 0x39A:
      this->exec_7C_39A_extsh(op);
      break;
    case 0x3BA:
      this->exec_7C_3BA_extsb(op);
      break;
    case 0x3D6:
      this->exec_7C_3D6_icbi(op);
      break;
    case 0x3D7:
      this->exec_7C_3D7_stfiwx(op);
      break;
    case 0x3F6:
      this->exec_7C_3F6_dcbz(op);
      break;
    default:
      throw runtime_error("invalid 7C subopcode");
  }
}

string PPC32Emulator::dasm_7C(uint32_t pc, uint32_t op, map<uint32_t, bool>& branch_target_addresses) {
  switch (op_get_subopcode(op)) {
    case 0x000:
      return PPC32Emulator::dasm_7C_000_cmp(pc, op, branch_target_addresses);
    case 0x004:
      return PPC32Emulator::dasm_7C_004_tw(pc, op, branch_target_addresses);
    case 0x008:
      return PPC32Emulator::dasm_7C_008_208_subfc(pc, op, branch_target_addresses);
    case 0x00A:
      return PPC32Emulator::dasm_7C_00A_20A_addc(pc, op, branch_target_addresses);
    case 0x00B:
      return PPC32Emulator::dasm_7C_00B_mulhwu(pc, op, branch_target_addresses);
    case 0x013:
      return PPC32Emulator::dasm_7C_013_mfcr(pc, op, branch_target_addresses);
    case 0x014:
      return PPC32Emulator::dasm_7C_014_lwarx(pc, op, branch_target_addresses);
    case 0x017:
      return PPC32Emulator::dasm_7C_017_lwzx(pc, op, branch_target_addresses);
    case 0x018:
      return PPC32Emulator::dasm_7C_018_slw(pc, op, branch_target_addresses);
    case 0x01A:
      return PPC32Emulator::dasm_7C_01A_cntlzw(pc, op, branch_target_addresses);
    case 0x01C:
      return PPC32Emulator::dasm_7C_01C_and(pc, op, branch_target_addresses);
    case 0x020:
      return PPC32Emulator::dasm_7C_020_cmpl(pc, op, branch_target_addresses);
    case 0x028:
      return PPC32Emulator::dasm_7C_028_228_subf(pc, op, branch_target_addresses);
    case 0x036:
      return PPC32Emulator::dasm_7C_036_dcbst(pc, op, branch_target_addresses);
    case 0x037:
      return PPC32Emulator::dasm_7C_037_lwzux(pc, op, branch_target_addresses);
    case 0x03C:
      return PPC32Emulator::dasm_7C_03C_andc(pc, op, branch_target_addresses);
    case 0x04B:
      return PPC32Emulator::dasm_7C_04B_mulhw(pc, op, branch_target_addresses);
    case 0x053:
      return PPC32Emulator::dasm_7C_053_mfmsr(pc, op, branch_target_addresses);
    case 0x056:
      return PPC32Emulator::dasm_7C_056_dcbf(pc, op, branch_target_addresses);
    case 0x057:
      return PPC32Emulator::dasm_7C_057_lbzx(pc, op, branch_target_addresses);
    case 0x068:
    case 0x268:
      return PPC32Emulator::dasm_7C_068_268_neg(pc, op, branch_target_addresses);
    case 0x077:
      return PPC32Emulator::dasm_7C_077_lbzux(pc, op, branch_target_addresses);
    case 0x07C:
      return PPC32Emulator::dasm_7C_07C_nor(pc, op, branch_target_addresses);
    case 0x088:
    case 0x288:
      return PPC32Emulator::dasm_7C_088_288_subfe(pc, op, branch_target_addresses);
    case 0x08A:
    case 0x28A:
      return PPC32Emulator::dasm_7C_08A_28A_adde(pc, op, branch_target_addresses);
    case 0x090:
      return PPC32Emulator::dasm_7C_090_mtcrf(pc, op, branch_target_addresses);
    case 0x092:
      return PPC32Emulator::dasm_7C_092_mtmsr(pc, op, branch_target_addresses);
    case 0x096:
      return PPC32Emulator::dasm_7C_096_stwcx_rec(pc, op, branch_target_addresses);
    case 0x097:
      return PPC32Emulator::dasm_7C_097_stwx(pc, op, branch_target_addresses);
    case 0x0B7:
      return PPC32Emulator::dasm_7C_0B7_stwux(pc, op, branch_target_addresses);
    case 0x0C8:
    case 0x2C8:
      return PPC32Emulator::dasm_7C_0C8_2C8_subfze(pc, op, branch_target_addresses);
    case 0x0CA:
    case 0x2CA:
      return PPC32Emulator::dasm_7C_0CA_2CA_addze(pc, op, branch_target_addresses);
    case 0x0D2:
      return PPC32Emulator::dasm_7C_0D2_mtsr(pc, op, branch_target_addresses);
    case 0x0D7:
      return PPC32Emulator::dasm_7C_0D7_stbx(pc, op, branch_target_addresses);
    case 0x0E8:
    case 0x2E8:
      return PPC32Emulator::dasm_7C_0E8_2E8_subfme(pc, op, branch_target_addresses);
    case 0x0EA:
    case 0x2EA:
      return PPC32Emulator::dasm_7C_0EA_2EA_addme(pc, op, branch_target_addresses);
    case 0x0EB:
    case 0x2EB:
      return PPC32Emulator::dasm_7C_0EB_2EB_mullw(pc, op, branch_target_addresses);
    case 0x0F2:
      return PPC32Emulator::dasm_7C_0F2_mtsrin(pc, op, branch_target_addresses);
    case 0x0F6:
      return PPC32Emulator::dasm_7C_0F6_dcbtst(pc, op, branch_target_addresses);
    case 0x0F7:
      return PPC32Emulator::dasm_7C_0F7_stbux(pc, op, branch_target_addresses);
    case 0x10A:
    case 0x30A:
      return PPC32Emulator::dasm_7C_10A_30A_add(pc, op, branch_target_addresses);
    case 0x116:
      return PPC32Emulator::dasm_7C_116_dcbt(pc, op, branch_target_addresses);
    case 0x117:
      return PPC32Emulator::dasm_7C_117_lhzx(pc, op, branch_target_addresses);
    case 0x11C:
      return PPC32Emulator::dasm_7C_11C_eqv(pc, op, branch_target_addresses);
    case 0x132:
      return PPC32Emulator::dasm_7C_132_tlbie(pc, op, branch_target_addresses);
    case 0x136:
      return PPC32Emulator::dasm_7C_136_eciwx(pc, op, branch_target_addresses);
    case 0x137:
      return PPC32Emulator::dasm_7C_137_lhzux(pc, op, branch_target_addresses);
    case 0x13C:
      return PPC32Emulator::dasm_7C_13C_xor(pc, op, branch_target_addresses);
    case 0x153:
      return PPC32Emulator::dasm_7C_153_mfspr(pc, op, branch_target_addresses);
    case 0x157:
      return PPC32Emulator::dasm_7C_157_lhax(pc, op, branch_target_addresses);
    case 0x172:
      return PPC32Emulator::dasm_7C_172_tlbia(pc, op, branch_target_addresses);
    case 0x173:
      return PPC32Emulator::dasm_7C_173_mftb(pc, op, branch_target_addresses);
    case 0x177:
      return PPC32Emulator::dasm_7C_177_lhaux(pc, op, branch_target_addresses);
    case 0x197:
      return PPC32Emulator::dasm_7C_197_sthx(pc, op, branch_target_addresses);
    case 0x19C:
      return PPC32Emulator::dasm_7C_19C_orc(pc, op, branch_target_addresses);
    case 0x1B6:
      return PPC32Emulator::dasm_7C_1B6_ecowx(pc, op, branch_target_addresses);
    case 0x1B7:
      return PPC32Emulator::dasm_7C_1B7_sthux(pc, op, branch_target_addresses);
    case 0x1BC:
      return PPC32Emulator::dasm_7C_1BC_or(pc, op, branch_target_addresses);
    case 0x1CB:
    case 0x3CB:
      return PPC32Emulator::dasm_7C_1CB_3CB_divwu(pc, op, branch_target_addresses);
    case 0x1D3:
      return PPC32Emulator::dasm_7C_1D3_mtspr(pc, op, branch_target_addresses);
    case 0x1D6:
      return PPC32Emulator::dasm_7C_1D6_dcbi(pc, op, branch_target_addresses);
    case 0x1DC:
      return PPC32Emulator::dasm_7C_1DC_nand(pc, op, branch_target_addresses);
    case 0x1EB:
    case 0x3EB:
      return PPC32Emulator::dasm_7C_1EB_3EB_divw(pc, op, branch_target_addresses);
    case 0x200:
      return PPC32Emulator::dasm_7C_200_mcrxr(pc, op, branch_target_addresses);
    case 0x215:
      return PPC32Emulator::dasm_7C_215_lswx(pc, op, branch_target_addresses);
    case 0x216:
      return PPC32Emulator::dasm_7C_216_lwbrx(pc, op, branch_target_addresses);
    case 0x217:
      return PPC32Emulator::dasm_7C_217_lfsx(pc, op, branch_target_addresses);
    case 0x218:
      return PPC32Emulator::dasm_7C_218_srw(pc, op, branch_target_addresses);
    case 0x236:
      return PPC32Emulator::dasm_7C_236_tlbsync(pc, op, branch_target_addresses);
    case 0x237:
      return PPC32Emulator::dasm_7C_237_lfsux(pc, op, branch_target_addresses);
    case 0x253:
      return PPC32Emulator::dasm_7C_253_mfsr(pc, op, branch_target_addresses);
    case 0x255:
      return PPC32Emulator::dasm_7C_255_lswi(pc, op, branch_target_addresses);
    case 0x256:
      return PPC32Emulator::dasm_7C_256_sync(pc, op, branch_target_addresses);
    case 0x257:
      return PPC32Emulator::dasm_7C_257_lfdx(pc, op, branch_target_addresses);
    case 0x277:
      return PPC32Emulator::dasm_7C_277_lfdux(pc, op, branch_target_addresses);
    case 0x293:
      return PPC32Emulator::dasm_7C_293_mfsrin(pc, op, branch_target_addresses);
    case 0x295:
      return PPC32Emulator::dasm_7C_295_stswx(pc, op, branch_target_addresses);
    case 0x296:
      return PPC32Emulator::dasm_7C_296_stwbrx(pc, op, branch_target_addresses);
    case 0x297:
      return PPC32Emulator::dasm_7C_297_stfsx(pc, op, branch_target_addresses);
    case 0x2B7:
      return PPC32Emulator::dasm_7C_2B7_stfsux(pc, op, branch_target_addresses);
    case 0x2E5:
      return PPC32Emulator::dasm_7C_2E5_stswi(pc, op, branch_target_addresses);
    case 0x2E7:
      return PPC32Emulator::dasm_7C_2E7_stfdx(pc, op, branch_target_addresses);
    case 0x2F6:
      return PPC32Emulator::dasm_7C_2F6_dcba(pc, op, branch_target_addresses);
    case 0x2F7:
      return PPC32Emulator::dasm_7C_2F7_stfdux(pc, op, branch_target_addresses);
    case 0x316:
      return PPC32Emulator::dasm_7C_316_lhbrx(pc, op, branch_target_addresses);
    case 0x318:
      return PPC32Emulator::dasm_7C_318_sraw(pc, op, branch_target_addresses);
    case 0x338:
      return PPC32Emulator::dasm_7C_338_srawi(pc, op, branch_target_addresses);
    case 0x356:
      return PPC32Emulator::dasm_7C_356_eieio(pc, op, branch_target_addresses);
    case 0x396:
      return PPC32Emulator::dasm_7C_396_sthbrx(pc, op, branch_target_addresses);
    case 0x39A:
      return PPC32Emulator::dasm_7C_39A_extsh(pc, op, branch_target_addresses);
    case 0x3BA:
      return PPC32Emulator::dasm_7C_3BA_extsb(pc, op, branch_target_addresses);
    case 0x3D6:
      return PPC32Emulator::dasm_7C_3D6_icbi(pc, op, branch_target_addresses);
    case 0x3D7:
      return PPC32Emulator::dasm_7C_3D7_stfiwx(pc, op, branch_target_addresses);
    case 0x3F6:
      return PPC32Emulator::dasm_7C_3F6_dcbz(pc, op, branch_target_addresses);
    default:
      return ".invalid  7C";
  }
}

string PPC32Emulator::dasm_7C_a_b(uint32_t op, const char* base_name) {
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  string ret = base_name;
  ret.resize(10, ' ');
  return ret + string_printf("r%hhu, r%hhu", ra, rb);
}

string PPC32Emulator::dasm_7C_d_a_b(uint32_t op, const char* base_name) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  string ret = base_name;
  ret.resize(10, ' ');
  return ret + string_printf("r%hhu, r%hhu, r%hhu", rd, ra, rb);
}

string PPC32Emulator::dasm_7C_d_a_b_r(uint32_t op, const char* base_name) {
  bool rec = op_get_rec(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  string ret = base_name;
  if (rec) {
    ret += '.';
  }
  ret.resize(10, ' ');
  return ret + string_printf("r%hhu, r%hhu, r%hhu", rd, ra, rb);
}

string PPC32Emulator::dasm_7C_s_a_b(uint32_t op, const char* base_name) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  string ret = base_name;
  ret.resize(10, ' ');
  return ret + string_printf("r%hhu, r%hhu, r%hhu", ra, rs, rb);
}

string PPC32Emulator::dasm_7C_s_a_r(uint32_t op, const char* base_name) {
  bool rec = op_get_rec(op);
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  string ret = base_name;
  if (rec) {
    ret += '.';
  }
  ret.resize(10, ' ');
  return ret + string_printf("r%hhu, r%hhu", ra, rs);
}

string PPC32Emulator::dasm_7C_s_a_b_r(uint32_t op, const char* base_name) {
  bool rec = op_get_rec(op);
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  string ret = base_name;
  if (rec) {
    ret += '.';
  }
  ret.resize(10, ' ');
  return ret + string_printf("r%hhu, r%hhu, r%hhu", ra, rs, rb);
}

string PPC32Emulator::dasm_7C_d_a_o_r(uint32_t op, const char* base_name) {
  bool rec = op_get_rec(op);
  bool o = op_get_o(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  string ret = base_name;
  if (o) {
    ret += 'o';
  }
  if (rec) {
    ret += '.';
  }
  ret.resize(10, ' ');
  return ret + string_printf("r%hhu, r%hhu", rd, ra);
}

string PPC32Emulator::dasm_7C_d_a_b_o_r(uint32_t op, const char* base_name) {
  bool rec = op_get_rec(op);
  bool o = op_get_o(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  string ret = base_name;
  if (o) {
    ret += 'o';
  }
  if (rec) {
    ret += '.';
  }
  ret.resize(10, ' ');
  return ret + string_printf("r%hhu, r%hhu, r%hhu", rd, ra, rb);
}



void PPC32Emulator::exec_7C_000_cmp(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDD 0 L AAAAA BBBBB 0000000000 0
}

string PPC32Emulator::dasm_7C_000_cmp(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  if (op & 0x00600000) {
    return ".invalid  cmp";
  }
  uint8_t crf = op_get_crf1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (crf) {
    return string_printf("cmp       cr%hhu, r%hhu, r%hhu", crf, ra, rb);
  } else {
    return string_printf("cmp       r%hhu, r%hhu", ra, rb);
  }
}



void PPC32Emulator::exec_7C_004_tw(uint32_t op) {
  this->exec_unimplemented(op); // 011111 TTTTT AAAAA BBBBB 0000000100
}

string PPC32Emulator::dasm_7C_004_tw(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t to = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  return string_printf("tw        %hhu, r%hhu, r%hhu", to, ra, rb);
}



void PPC32Emulator::exec_7C_008_208_subfc(uint32_t op) {
  // 011111 DDDDD AAAAA BBBBB O 000001000 R
  if (op_get_o(op)) {
    throw runtime_error("overflow bits not implemented");
  }

  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.r[rd].s = this->regs.r[rb].s - this->regs.r[ra].s;
  if (this->regs.r[rd].s < 0) {
    this->regs.xer.u |= 0x20000000; // xer[ca] = 1
  } else {
    this->regs.xer.u &= ~0x20000000; // xer[ca] = 0
  }
  if (op_get_rec(op)) {
    this->set_cr_bits_int(0, this->regs.r[rd].s);
  }
}

string PPC32Emulator::dasm_7C_008_208_subfc(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "subfc");
}



void PPC32Emulator::exec_7C_00A_20A_addc(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB O 000001010 R
}

string PPC32Emulator::dasm_7C_00A_20A_addc(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "addc");
}



void PPC32Emulator::exec_7C_00B_mulhwu(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 0000001011 R
}

string PPC32Emulator::dasm_7C_00B_mulhwu(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b_r(op, "mulhwu");
}



void PPC32Emulator::exec_7C_013_mfcr(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD 00000 00000 0000010011 0
}

string PPC32Emulator::dasm_7C_013_mfcr(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rd = op_get_reg1(op);
  return string_printf("mfcr      r%hhu", rd);
}



void PPC32Emulator::exec_7C_014_lwarx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 0000010100 0
}

string PPC32Emulator::dasm_7C_014_lwarx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lwarx");
}



void PPC32Emulator::exec_7C_017_lwzx(uint32_t op) {
  // 011111 DDDDD AAAAA BBBBB 0000010111 0
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->regs.r[rd].u = this->mem->read<be_uint32_t>(this->regs.debug.addr);
}

string PPC32Emulator::dasm_7C_017_lwzx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lwzx");
}



void PPC32Emulator::exec_7C_018_slw(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0000011000 R
}

string PPC32Emulator::dasm_7C_018_slw(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "slw");
}



void PPC32Emulator::exec_7C_01A_cntlzw(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA 00000 0000011010 R
}

string PPC32Emulator::dasm_7C_01A_cntlzw(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  bool rec = op_get_rec(op);
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  return string_printf("cntlzw%c   r%hhu, r%hhu", rec ? '.' : ' ', ra, rs);
}



void PPC32Emulator::exec_7C_01C_and(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0000011100 R
}

string PPC32Emulator::dasm_7C_01C_and(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "and");
}



void PPC32Emulator::exec_7C_020_cmpl(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDD 0 L AAAAA BBBBB 0000100000 0
}

string PPC32Emulator::dasm_7C_020_cmpl(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  if (op & 0x00600000) {
    return ".invalid  cmpl";
  }
  uint8_t crf = op_get_crf1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (crf) {
    return string_printf("cmpl      cr%hhu, r%hhu, r%hhu", crf, ra, rb);
  } else {
    return string_printf("cmpl      r%hhu, r%hhu", ra, rb);
  }
}



void PPC32Emulator::exec_7C_028_228_subf(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB O 000101000 R
}

string PPC32Emulator::dasm_7C_028_228_subf(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "subf");
}



void PPC32Emulator::exec_7C_036_dcbst(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 0000110110 0
}

string PPC32Emulator::dasm_7C_036_dcbst(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcbst");
}



void PPC32Emulator::exec_7C_037_lwzux(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 0000110111 0
}

string PPC32Emulator::dasm_7C_037_lwzux(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lwzux");
}



void PPC32Emulator::exec_7C_03C_andc(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0000111100 R
}

string PPC32Emulator::dasm_7C_03C_andc(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "andc");
}



void PPC32Emulator::exec_7C_04B_mulhw(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 0001001011 R
}

string PPC32Emulator::dasm_7C_04B_mulhw(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b_r(op, "mulhw");
}



void PPC32Emulator::exec_7C_053_mfmsr(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD 00000 00000 0001010011 0
}

string PPC32Emulator::dasm_7C_053_mfmsr(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rd = op_get_reg1(op);
  return string_printf("mfmsr     r%hhu", rd);
}



void PPC32Emulator::exec_7C_056_dcbf(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 0001010110 0
}

string PPC32Emulator::dasm_7C_056_dcbf(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcbf");
}



void PPC32Emulator::exec_7C_057_lbzx(uint32_t op) {
  // 011111 DDDDD AAAAA BBBBB 0001010111 0
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->regs.r[rd].u = static_cast<uint32_t>(this->mem->read<uint8_t>(this->regs.debug.addr));
}

string PPC32Emulator::dasm_7C_057_lbzx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lbzx");
}



void PPC32Emulator::exec_7C_068_268_neg(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA 00000 O 001101000 R
}

string PPC32Emulator::dasm_7C_068_268_neg(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_o_r(op, "neg");
}



void PPC32Emulator::exec_7C_077_lbzux(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 0001110111 0
}

string PPC32Emulator::dasm_7C_077_lbzux(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lbzux");
}



void PPC32Emulator::exec_7C_07C_nor(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0001111100 R
}

string PPC32Emulator::dasm_7C_07C_nor(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "nor");
}



void PPC32Emulator::exec_7C_088_288_subfe(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB O 010001000 R
}

string PPC32Emulator::dasm_7C_088_288_subfe(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "subfe");
}



void PPC32Emulator::exec_7C_08A_28A_adde(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB O 010001010 R
}

string PPC32Emulator::dasm_7C_08A_28A_adde(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "adde");
}



void PPC32Emulator::exec_7C_090_mtcrf(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS 0 CCCCCCCC 0 0010010000 0
}

string PPC32Emulator::dasm_7C_090_mtcrf(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t crm = (op >> 12) & 0xFF;
  if (crm == 0xFF) {
    return string_printf("mtcr      r%hhu", rs);
  } else {
    return string_printf("mtcrf     0x%02hhX, r%hhu", crm, rs);
  }
}



void PPC32Emulator::exec_7C_092_mtmsr(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS 00000 00000 0010010010 0
}

string PPC32Emulator::dasm_7C_092_mtmsr(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  return string_printf("mtmsr     r%hhu", rs);
}



void PPC32Emulator::exec_7C_096_stwcx_rec(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0010010110 1
}

string PPC32Emulator::dasm_7C_096_stwcx_rec(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "stwcx.");
}



void PPC32Emulator::exec_7C_097_stwx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0010010111 0
}

string PPC32Emulator::dasm_7C_097_stwx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "stwx");
}



void PPC32Emulator::exec_7C_0B7_stwux(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0010110111 0
}

string PPC32Emulator::dasm_7C_0B7_stwux(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "stwux");
}



void PPC32Emulator::exec_7C_0C8_2C8_subfze(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA 00000 O 011001000 R
}

string PPC32Emulator::dasm_7C_0C8_2C8_subfze(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_o_r(op, "subfze");
}



void PPC32Emulator::exec_7C_0CA_2CA_addze(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA 00000 O 011001010 R
}

string PPC32Emulator::dasm_7C_0CA_2CA_addze(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_o_r(op, "addze");
}



void PPC32Emulator::exec_7C_0D2_mtsr(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS 0 RRRR 00000 0011010010 0
}

string PPC32Emulator::dasm_7C_0D2_mtsr(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t sr = op_get_reg2(op) & 0x0F;
  return string_printf("mtsr      %hhu, r%hhu", sr, rs);
}



void PPC32Emulator::exec_7C_0D7_stbx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0011010111 0
}

string PPC32Emulator::dasm_7C_0D7_stbx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "stbx");
}



void PPC32Emulator::exec_7C_0E8_2E8_subfme(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA 00000 O 011101000 R
}

string PPC32Emulator::dasm_7C_0E8_2E8_subfme(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_o_r(op, "subfme");
}



void PPC32Emulator::exec_7C_0EA_2EA_addme(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA 00000 O 011101010 R
}

string PPC32Emulator::dasm_7C_0EA_2EA_addme(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_o_r(op, "addme");
}



void PPC32Emulator::exec_7C_0EB_2EB_mullw(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB O 011101011 R
}

string PPC32Emulator::dasm_7C_0EB_2EB_mullw(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "mullw");
}



void PPC32Emulator::exec_7C_0F2_mtsrin(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS 00000 BBBBB 0011110010 0
}

string PPC32Emulator::dasm_7C_0F2_mtsrin(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t rb = op_get_reg2(op);
  return string_printf("mtsr      r%hhu, r%hhu", rb, rs);
}



void PPC32Emulator::exec_7C_0F6_dcbtst(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 0011110110 0
}

string PPC32Emulator::dasm_7C_0F6_dcbtst(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcbtst");
}



void PPC32Emulator::exec_7C_0F7_stbux(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0011110111 0
}

string PPC32Emulator::dasm_7C_0F7_stbux(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "stbux");
}



void PPC32Emulator::exec_7C_10A_30A_add(uint32_t op) {
  // 011111 DDDDD AAAAA BBBBB O 100001010 R
  if (op_get_o(op)) {
    throw runtime_error("overflow bits not implemented");
  }

  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.r[rd].s = this->regs.r[ra].s + this->regs.r[rb].s;
  if (op_get_rec(op)) {
    this->set_cr_bits_int(0, this->regs.r[rd].s);
  }
}

string PPC32Emulator::dasm_7C_10A_30A_add(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "add");
}



void PPC32Emulator::exec_7C_116_dcbt(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 0100010110 0
}

string PPC32Emulator::dasm_7C_116_dcbt(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcbt");
}



void PPC32Emulator::exec_7C_117_lhzx(uint32_t op) {
  // 011111 DDDDD AAAAA BBBBB 0100010111 0
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->regs.r[rd].u = static_cast<uint32_t>(this->mem->read<be_uint16_t>(this->regs.debug.addr));
}

string PPC32Emulator::dasm_7C_117_lhzx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lhzx");
}



void PPC32Emulator::exec_7C_11C_eqv(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0100011100 R
}

string PPC32Emulator::dasm_7C_11C_eqv(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "eqv");
}



void PPC32Emulator::exec_7C_132_tlbie(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 00000 BBBBB 0100110010 0
}

string PPC32Emulator::dasm_7C_132_tlbie(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rb = op_get_reg1(op);
  return string_printf("tlbie     r%hhu", rb);
}



void PPC32Emulator::exec_7C_136_eciwx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 0100110110 0
}

string PPC32Emulator::dasm_7C_136_eciwx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "eciwx");
}



void PPC32Emulator::exec_7C_137_lhzux(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 0100110111 0
}

string PPC32Emulator::dasm_7C_137_lhzux(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lhzux");
}



void PPC32Emulator::exec_7C_13C_xor(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0100111100 R
}

string PPC32Emulator::dasm_7C_13C_xor(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "xor");
}



void PPC32Emulator::exec_7C_153_mfspr(uint32_t op) {
  // 011111 DDDDD RRRRRRRRRR 0101010011 0
  uint8_t rd = op_get_reg1(op);
  uint16_t spr = op_get_spr(op);
  if (spr == 8) {
    this->regs.r[rd].u = this->regs.lr;
  } else if (spr == 9) {
    this->regs.r[rd].u = this->regs.ctr;
  } else if (spr == 1) {
    this->regs.r[rd].u = this->regs.xer.u;
  } else {
    this->exec_unimplemented(op);
  }
}

string PPC32Emulator::dasm_7C_153_mfspr(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rd = op_get_reg1(op);
  uint16_t spr = op_get_spr(op);
  const char* name = name_for_spr(spr);
  if (name) {
    string ret = "mf";
    ret += name;
    ret.resize(10, ' ');
    ret += string_printf("r%hhu", rd);
    return ret;
  } else {
    return string_printf("mfspr     r%hhu, %hu", rd, spr);
  }
}



void PPC32Emulator::exec_7C_157_lhax(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 0101010111 0
}

string PPC32Emulator::dasm_7C_157_lhax(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lhax");
}



void PPC32Emulator::exec_7C_172_tlbia(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 00000 00000 0101110010 0
}

string PPC32Emulator::dasm_7C_172_tlbia(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  if (op == 0x7C0002E4) {
    return "tlbia";
  }
  return ".invalid  tlbia";
}



void PPC32Emulator::exec_7C_173_mftb(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD RRRRRRRRRR 0101110011 0
}

string PPC32Emulator::dasm_7C_173_mftb(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rd = op_get_reg1(op);
  uint16_t tbr = op_get_spr(op);
  if (tbr == 268) {
    return string_printf("mftb      r%hhu", rd);
  } else if (tbr == 269) {
    return string_printf("mftbu     r%hhu", rd);
  } else {
    return string_printf("mftb      r%hhu, tbr%hu", rd, tbr);
  }
}



void PPC32Emulator::exec_7C_177_lhaux(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 0101110111 0
}

string PPC32Emulator::dasm_7C_177_lhaux(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lhaux");
}



void PPC32Emulator::exec_7C_197_sthx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0110010111 0
}

string PPC32Emulator::dasm_7C_197_sthx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "sthx");
}



void PPC32Emulator::exec_7C_19C_orc(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0110011100 R
}

string PPC32Emulator::dasm_7C_19C_orc(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b_r(op, "orc");
}



void PPC32Emulator::exec_7C_1B6_ecowx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0110110110 0
}

string PPC32Emulator::dasm_7C_1B6_ecowx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "ecowx");
}



void PPC32Emulator::exec_7C_1B7_sthux(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0110110111 0
}

string PPC32Emulator::dasm_7C_1B7_sthux(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "sthux");
}



void PPC32Emulator::exec_7C_1BC_or(uint32_t op) {
  // 011111 SSSSS AAAAA BBBBB 0110111100 R
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.r[ra].u = this->regs.r[rs].u | this->regs.r[rb].u;
  if (op_get_rec(op)) {
    this->set_cr_bits_int(0, this->regs.r[ra].s);
  }
}

string PPC32Emulator::dasm_7C_1BC_or(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (rs == rb) {
    return string_printf("mr%c       r%hhu, r%hhu", op_get_rec(op) ? '.' : ' ',
        ra, rs);
  } else {
    return PPC32Emulator::dasm_7C_s_a_b_r(op, "or");
  }
}



void PPC32Emulator::exec_7C_1CB_3CB_divwu(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB O 111001011 R
}

string PPC32Emulator::dasm_7C_1CB_3CB_divwu(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "divwu");
}



void PPC32Emulator::exec_7C_1D3_mtspr(uint32_t op) {
  // 011111 SSSSS RRRRRRRRRR 0111010011 0
  uint8_t rs = op_get_reg1(op);
  uint16_t spr = op_get_spr(op);
  if (spr == 8) {
    this->regs.lr = this->regs.r[rs].u;
  } else if (spr == 9) {
    this->regs.ctr = this->regs.r[rs].u;
  } else if (spr == 1) {
    this->regs.xer.u = this->regs.r[rs].u;
  } else {
    this->exec_unimplemented(op);
  }
}

string PPC32Emulator::dasm_7C_1D3_mtspr(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint16_t spr = op_get_spr(op);
  const char* name = name_for_spr(spr);
  if (name) {
    string ret = "mt";
    ret += name;
    ret.resize(10, ' ');
    ret += string_printf("r%hhu", rs);
    return ret;
  } else {
    return string_printf("mtspr     %hu, r%hhu", spr, rs);
  }
}



void PPC32Emulator::exec_7C_1D6_dcbi(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 0111010110 0
}

string PPC32Emulator::dasm_7C_1D6_dcbi(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcbi");
}



void PPC32Emulator::exec_7C_1DC_nand(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0111011100 R
}

string PPC32Emulator::dasm_7C_1DC_nand(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "nand");
}



void PPC32Emulator::exec_7C_1EB_3EB_divw(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB O 111101011 R
}

string PPC32Emulator::dasm_7C_1EB_3EB_divw(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "divw");
}



void PPC32Emulator::exec_7C_200_mcrxr(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDD 00 00000 00000 1000000000 0
}

string PPC32Emulator::dasm_7C_200_mcrxr(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t crf = op_get_crf1(op);
  return string_printf("mcrxr     cr%hhu", crf);
}



void PPC32Emulator::exec_7C_215_lswx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 1000010101 0
}

string PPC32Emulator::dasm_7C_215_lswx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lswx");
}



void PPC32Emulator::exec_7C_216_lwbrx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 1000010110 0
}

string PPC32Emulator::dasm_7C_216_lwbrx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lwbrx");
}



void PPC32Emulator::exec_7C_217_lfsx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 1000010111 0
}

string PPC32Emulator::dasm_7C_217_lfsx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lfsx");
}



void PPC32Emulator::exec_7C_218_srw(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 1000011000 R
}

string PPC32Emulator::dasm_7C_218_srw(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "srw");
}



void PPC32Emulator::exec_7C_236_tlbsync(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 00000 00000 1000110110 0
}

string PPC32Emulator::dasm_7C_236_tlbsync(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  if (op == 0x7C00046C) {
    return "tlbsync";
  }
  return ".invalid  tlbsync";
}



void PPC32Emulator::exec_7C_237_lfsux(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 1000110111 0
}

string PPC32Emulator::dasm_7C_237_lfsux(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lfsux");
}



void PPC32Emulator::exec_7C_253_mfsr(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD 0 RRRR 00000 1001010011 0
}

string PPC32Emulator::dasm_7C_253_mfsr(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rd = op_get_reg1(op);
  uint8_t sr = op_get_reg2(op) & 0x0F;
  return string_printf("mfsr      r%hhu, %hhu", rd, sr);
}



void PPC32Emulator::exec_7C_255_lswi(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA NNNNN 1001010101 0
}

string PPC32Emulator::dasm_7C_255_lswi(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t n = op_get_reg3(op);
  if (n == 0) {
    n = 32;
  }
  return string_printf("lswi      r%hhu, r%hhu, %hhu", rd, ra, n);
}



void PPC32Emulator::exec_7C_256_sync(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 00000 00000 1001010110 0
}

string PPC32Emulator::dasm_7C_256_sync(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  if (op == 0x7C0004AC) {
    return "sync";
  }
  return ".invalid  sync";
}



void PPC32Emulator::exec_7C_257_lfdx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 1001010111 0
}

string PPC32Emulator::dasm_7C_257_lfdx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lfdx");
}



void PPC32Emulator::exec_7C_277_lfdux(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 1001110111 0
}

string PPC32Emulator::dasm_7C_277_lfdux(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lfdux");
}



void PPC32Emulator::exec_7C_293_mfsrin(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD 00000 BBBBB 1010010011 0
}

string PPC32Emulator::dasm_7C_293_mfsrin(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rd = op_get_reg1(op);
  uint8_t rb = op_get_reg2(op);
  return string_printf("mfsrin    r%hhu, r%hhu", rd, rb);
}



void PPC32Emulator::exec_7C_295_stswx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 1010010101 0
}

string PPC32Emulator::dasm_7C_295_stswx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "stswx");
}



void PPC32Emulator::exec_7C_296_stwbrx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 1010010110 0
}

string PPC32Emulator::dasm_7C_296_stwbrx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "stwbrx");
}



void PPC32Emulator::exec_7C_297_stfsx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 1010010111 0
}

string PPC32Emulator::dasm_7C_297_stfsx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "stfsx");
}



void PPC32Emulator::exec_7C_2B7_stfsux(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 1010110111 0
}

string PPC32Emulator::dasm_7C_2B7_stfsux(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "stfsux");
}



void PPC32Emulator::exec_7C_2E5_stswi(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA NNNNN 1011010101 0
}

string PPC32Emulator::dasm_7C_2E5_stswi(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t n = op_get_reg3(op);
  if (n == 0) {
    n = 32;
  }
  return string_printf("stswi     r%hhu, r%hhu, %hhu", ra, rs, n);
}



void PPC32Emulator::exec_7C_2E7_stfdx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 1011010111 0
}

string PPC32Emulator::dasm_7C_2E7_stfdx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "stfdx");
}



void PPC32Emulator::exec_7C_2F6_dcba(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 1011110110 0
}

string PPC32Emulator::dasm_7C_2F6_dcba(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcba");
}



void PPC32Emulator::exec_7C_2F7_stfdux(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 1011110111 0
}

string PPC32Emulator::dasm_7C_2F7_stfdux(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "stfdux");
}



void PPC32Emulator::exec_7C_316_lhbrx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 1100010110 0
}

string PPC32Emulator::dasm_7C_316_lhbrx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "lhbrx");
}



void PPC32Emulator::exec_7C_318_sraw(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 1100011000 R
}

string PPC32Emulator::dasm_7C_318_sraw(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "sraw");
}



void PPC32Emulator::exec_7C_338_srawi(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA <<<<< 1100111000 R
}

string PPC32Emulator::dasm_7C_338_srawi(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t sh = op_get_reg3(op);
  return string_printf("srawi     r%hhu, r%hhu, %hhu", ra, rs, sh);
}



void PPC32Emulator::exec_7C_356_eieio(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 00000 00000 1101010110 0
}

string PPC32Emulator::dasm_7C_356_eieio(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  if (op == 0x7C0006AC) {
    return "eieio";
  }
  return ".invalid  eieio";
}



void PPC32Emulator::exec_7C_396_sthbrx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 1110010110 0
}

string PPC32Emulator::dasm_7C_396_sthbrx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "sthbrx");
}



void PPC32Emulator::exec_7C_39A_extsh(uint32_t op) {
  // 011111 SSSSS AAAAA 00000 1110011010 R
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  this->regs.r[ra].u = this->regs.r[rs].u & 0xFFFF;
  if (this->regs.r[ra].u & 0x8000) {
    this->regs.r[ra].u |= 0xFFFF0000;
  } else {
    this->regs.r[ra].u &= 0x0000FFFF;
  }
  if (op_get_rec(op)) {
    this->set_cr_bits_int(0, this->regs.r[ra].u);
  }
}

string PPC32Emulator::dasm_7C_39A_extsh(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_r(op, "extsh");
}



void PPC32Emulator::exec_7C_3BA_extsb(uint32_t op) {
  // 011111 SSSSS AAAAA 00000 1110111010 R
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  this->regs.r[ra].u = this->regs.r[rs].u & 0xFF;
  if (this->regs.r[ra].u & 0x80) {
    this->regs.r[ra].u |= 0xFFFFFF00;
  } else {
    this->regs.r[ra].u &= 0x000000FF;
  }
  if (op_get_rec(op)) {
    this->set_cr_bits_int(0, this->regs.r[ra].u);
  }
}

string PPC32Emulator::dasm_7C_3BA_extsb(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_r(op, "extsb");
}



void PPC32Emulator::exec_7C_3D6_icbi(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 1111010110 0
}

string PPC32Emulator::dasm_7C_3D6_icbi(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_a_b(op, "icbi");
}



void PPC32Emulator::exec_7C_3D7_stfiwx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 1111010111 0
}

string PPC32Emulator::dasm_7C_3D7_stfiwx(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "stfiwx");
}



void PPC32Emulator::exec_7C_3F6_dcbz(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 1111110110 0
}

string PPC32Emulator::dasm_7C_3F6_dcbz(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcbz");
}



string PPC32Emulator::dasm_load_store_imm_u(
    uint32_t op,
    const char* base_name,
    bool is_store,
    bool data_reg_is_f) {
  bool u = op_get_u(op);
  uint8_t rsd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm(op);

  string ret = base_name;
  if (u) {
    ret += 'u';
  }
  ret.resize(10, ' ');

  char rsd_type = data_reg_is_f ? 'f' : 'r';
  if (is_store) {
    if (imm < 0) {
      return ret + string_printf("[r%hhu - 0x%04X], %c%hhu", ra, -imm, rsd_type, rsd);
    } else if (imm > 0) {
      return ret + string_printf("[r%hhu + 0x%04X], %c%hhu", ra, imm, rsd_type, rsd);
    } else {
      return ret + string_printf("[r%hhu], %c%hhu", ra, rsd_type, rsd);
    }
  } else {
    if (imm < 0) {
      return ret + string_printf("%c%hhu, [r%hhu - 0x%04X]", rsd_type, rsd, ra, -imm);
    } else if (imm > 0) {
      return ret + string_printf("%c%hhu, [r%hhu + 0x%04X]", rsd_type, rsd, ra, imm);
    } else {
      return ret + string_printf("%c%hhu, [r%hhu]", rsd_type, rsd, ra);
    }
  }
}

string PPC32Emulator::dasm_load_store_imm(uint32_t op, const char* base_name, bool is_store) {
  uint8_t rsd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm(op);

  string ret = base_name;
  ret.resize(10, ' ');
  if (is_store) {
    if (imm < 0) {
      return ret + string_printf("[r%hhu - 0x%04X], r%hhu", ra, -imm, rsd);
    } else if (imm > 0) {
      return ret + string_printf("[r%hhu + 0x%04X], r%hhu", ra, imm, rsd);
    } else {
      return ret + string_printf("[r%hhu], r%hhu", ra, rsd);
    }
  } else {
    if (imm < 0) {
      return ret + string_printf("r%hhu, [r%hhu - 0x%04X]", rsd, ra, -imm);
    } else if (imm > 0) {
      return ret + string_printf("r%hhu, [r%hhu + 0x%04X]", rsd, ra, imm);
    } else {
      return ret + string_printf("r%hhu, [r%hhu]", rsd, ra);
    }
  }
}



void PPC32Emulator::exec_80_84_lwz_lwzu(uint32_t op) {
  // 10000 U DDDDD AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if ((u && (ra == 0)) || (ra == rd)) {
    throw runtime_error("invalid opcode: lwz(u) [r0 + X], rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->regs.r[rd].u = this->mem->read<be_uint32_t>(this->regs.debug.addr);
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

string PPC32Emulator::dasm_80_84_lwz_lwzu(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_load_store_imm_u(op, "lwz", false, false);
}



void PPC32Emulator::exec_88_8C_lbz_lbzu(uint32_t op) {
  // 10001 U DDDDD AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (u && ((ra == 0) || (ra == rd))) {
    throw runtime_error("invalid opcode: lhau rX, [r0 + Z] or rX == rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->regs.r[rd].u = static_cast<uint32_t>(this->mem->read<uint8_t>(this->regs.debug.addr));
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

string PPC32Emulator::dasm_88_8C_lbz_lbzu(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_load_store_imm_u(op, "lbz", false, false);
}



void PPC32Emulator::exec_90_94_stw_stwu(uint32_t op) {
  // 10010 U SSSSS AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (u && (ra == 0)) {
    throw runtime_error("invalid opcode: stwu [r0 + X], rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->mem->write<be_uint32_t>(this->regs.debug.addr, this->regs.r[rs].u);
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

string PPC32Emulator::dasm_90_94_stw_stwu(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_load_store_imm_u(op, "stw", true, false);
}



void PPC32Emulator::exec_98_9C_stb_stbu(uint32_t op) {
  // 10011 U SSSSS AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (u && (ra == 0)) {
    throw runtime_error("invalid opcode: stbu [r0 + X], rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->mem->write<uint8_t>(this->regs.debug.addr, this->regs.r[rs].u & 0xFF);
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

string PPC32Emulator::dasm_98_9C_stb_stbu(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_load_store_imm_u(op, "stb", true, false);
}



void PPC32Emulator::exec_A0_A4_lhz_lhzu(uint32_t op) {
  // 10100 U DDDDD AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (u && ((ra == 0) || (ra == rd))) {
    throw runtime_error("invalid opcode: lhzu rX, [r0 + Z] or rX == rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->regs.r[rd].u = static_cast<uint32_t>(this->mem->read<be_uint16_t>(this->regs.debug.addr));
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

string PPC32Emulator::dasm_A0_A4_lhz_lhzu(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_load_store_imm_u(op, "lhz", false, false);
}



void PPC32Emulator::exec_A8_AC_lha_lhau(uint32_t op) {
  // 10101 U DDDDD AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (u && ((ra == 0) || (ra == rd))) {
    throw runtime_error("invalid opcode: lhau rX, [r0 + Z] or rX == rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->regs.r[rd].s = static_cast<int32_t>(this->mem->read<be_int16_t>(this->regs.debug.addr));
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

string PPC32Emulator::dasm_A8_AC_lha_lhau(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_load_store_imm_u(op, "lha", false, false);
}



void PPC32Emulator::exec_B0_B4_sth_sthu(uint32_t op) {
  // 10110 U SSSSS AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (u && (ra == 0)) {
    throw runtime_error("invalid opcode: sthu [r0 + X], rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->mem->write<be_uint16_t>(this->regs.debug.addr, this->regs.r[rs].u & 0xFFFF);
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

string PPC32Emulator::dasm_B0_B4_sth_sthu(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_load_store_imm_u(op, "sth", true, false);
}



void PPC32Emulator::exec_B8_lmw(uint32_t op) {
  // 101110 DDDDD AAAAA dddddddddddddddd
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (ra >= rd) {
    throw runtime_error("invalid lmw opcode");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  for (uint32_t addr = this->regs.debug.addr; rd < 32; rd++, addr += 4) {
    this->regs.r[rd].u = this->mem->read<uint32_t>(addr);
  }
}

string PPC32Emulator::dasm_B8_lmw(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_load_store_imm(op, "lmw", false);
}



void PPC32Emulator::exec_BC_stmw(uint32_t op) {
  // 101111 SSSSS AAAAA dddddddddddddddd
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  for (uint32_t addr = this->regs.debug.addr; rs < 32; rs++, addr += 4) {
    this->mem->write<be_uint32_t>(addr, this->regs.r[rs].u);
  }
}

string PPC32Emulator::dasm_BC_stmw(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_load_store_imm(op, "stmw", true);
}



void PPC32Emulator::exec_C0_C4_lfs_lfsu(uint32_t op) {
  this->exec_unimplemented(op); // 11000 U DDDDD AAAAA dddddddddddddddd
}

string PPC32Emulator::dasm_C0_C4_lfs_lfsu(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_load_store_imm_u(op, "lfs", false, true);
}



void PPC32Emulator::exec_C8_CC_lfd_lfdu(uint32_t op) {
  this->exec_unimplemented(op); // 11001 U DDDDD AAAAA dddddddddddddddd
}

string PPC32Emulator::dasm_C8_CC_lfd_lfdu(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_load_store_imm_u(op, "lfd", false, true);
}



void PPC32Emulator::exec_D0_D4_stfs_stfsu(uint32_t op) {
  this->exec_unimplemented(op); // 11010 U DDDDD AAAAA dddddddddddddddd
}

string PPC32Emulator::dasm_D0_D4_stfs_stfsu(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_load_store_imm_u(op, "stfs", true, true);
}



void PPC32Emulator::exec_D8_DC_stfd_stfdu(uint32_t op) {
  this->exec_unimplemented(op); // 11011 U DDDDD AAAAA dddddddddddddddd
}

string PPC32Emulator::dasm_D8_DC_stfd_stfdu(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_load_store_imm_u(op, "stfd", true, true);
}



void PPC32Emulator::exec_EC(uint32_t op) {
  switch (op_get_short_subopcode(op)) {
    case 0x12:
      this->exec_EC_12_fdivs(op);
      break;
    case 0x14:
      this->exec_EC_14_fsubs(op);
      break;
    case 0x15:
      this->exec_EC_15_fadds(op);
      break;
    case 0x16:
      this->exec_EC_16_fsqrts(op);
      break;
    case 0x18:
      this->exec_EC_18_fres(op);
      break;
    case 0x19:
      this->exec_EC_19_fmuls(op);
      break;
    case 0x1C:
      this->exec_EC_1C_fmsubs(op);
      break;
    case 0x1D:
      this->exec_EC_1D_fmadds(op);
      break;
    case 0x1E:
      this->exec_EC_1E_fnmsubs(op);
      break;
    case 0x1F:
      this->exec_EC_1F_fnmadds(op);
      break;
    default:
      throw runtime_error("invalid EC subopcode");
  }
}

string PPC32Emulator::dasm_EC(uint32_t pc, uint32_t op, map<uint32_t, bool>& branch_target_addresses) {
  switch (op_get_short_subopcode(op)) {
    case 0x12:
      return PPC32Emulator::dasm_EC_12_fdivs(pc, op, branch_target_addresses);
    case 0x14:
      return PPC32Emulator::dasm_EC_14_fsubs(pc, op, branch_target_addresses);
    case 0x15:
      return PPC32Emulator::dasm_EC_15_fadds(pc, op, branch_target_addresses);
    case 0x16:
      return PPC32Emulator::dasm_EC_16_fsqrts(pc, op, branch_target_addresses);
    case 0x18:
      return PPC32Emulator::dasm_EC_18_fres(pc, op, branch_target_addresses);
    case 0x19:
      return PPC32Emulator::dasm_EC_19_fmuls(pc, op, branch_target_addresses);
    case 0x1C:
      return PPC32Emulator::dasm_EC_1C_fmsubs(pc, op, branch_target_addresses);
    case 0x1D:
      return PPC32Emulator::dasm_EC_1D_fmadds(pc, op, branch_target_addresses);
    case 0x1E:
      return PPC32Emulator::dasm_EC_1E_fnmsubs(pc, op, branch_target_addresses);
    case 0x1F:
      return PPC32Emulator::dasm_EC_1F_fnmadds(pc, op, branch_target_addresses);
    default:
      return ".invalid  EC";
  }
}



string PPC32Emulator::dasm_EC_FC_d_b_r(uint32_t op, const char* base_name) {
  bool r = op_get_rec(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t rb = op_get_reg3(op);
  string ret = base_name;
  if (r) {
    ret += '.';
  }
  ret.resize(10, ' ');
  return ret + string_printf("f%hhu, f%hhu", rd, rb);
}

string PPC32Emulator::dasm_EC_FC_d_a_b_r(uint32_t op, const char* base_name) {
  bool r = op_get_rec(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  string ret = base_name;
  if (r) {
    ret += '.';
  }
  ret.resize(10, ' ');
  return ret + string_printf("f%hhu, f%hhu, f%hhu", rd, ra, rb);
}

string PPC32Emulator::dasm_EC_FC_d_a_c_r(uint32_t op, const char* base_name) {
  bool r = op_get_rec(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rc = op_get_reg4(op);
  string ret = base_name;
  if (r) {
    ret += '.';
  }
  ret.resize(10, ' ');
  return ret + string_printf("f%hhu, f%hhu, f%hhu", rd, ra, rc);
}

string PPC32Emulator::dasm_EC_FC_d_a_b_c_r(uint32_t op, const char* base_name) {
  bool r = op_get_rec(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  uint8_t rc = op_get_reg4(op);
  string ret = base_name;
  if (r) {
    ret += '.';
  }
  ret.resize(10, ' ');
  return ret + string_printf("f%hhu, f%hhu, f%hhu, f%hhu", rd, ra, rb, rc);
}

void PPC32Emulator::exec_EC_12_fdivs(uint32_t op) {
  this->exec_unimplemented(op); // 111011 DDDDD AAAAA BBBBB 00000 10010 R
}

string PPC32Emulator::dasm_EC_12_fdivs(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_r(op, "fdivs");
}



void PPC32Emulator::exec_EC_14_fsubs(uint32_t op) {
  this->exec_unimplemented(op); // 111011 DDDDD AAAAA BBBBB 00000 10100 R
}

string PPC32Emulator::dasm_EC_14_fsubs(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_r(op, "fsubs");
}



void PPC32Emulator::exec_EC_15_fadds(uint32_t op) {
  this->exec_unimplemented(op); // 111011 DDDDD AAAAA BBBBB 00000 10101 R
}

string PPC32Emulator::dasm_EC_15_fadds(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_r(op, "fadds");
}



void PPC32Emulator::exec_EC_16_fsqrts(uint32_t op) {
  this->exec_unimplemented(op); // 111011 DDDDD 00000 BBBBB 00000 10110 R
}

string PPC32Emulator::dasm_EC_16_fsqrts(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fsqrts");
}



void PPC32Emulator::exec_EC_18_fres(uint32_t op) {
  this->exec_unimplemented(op); // 111011 DDDDD 00000 BBBBB 00000 11000 R
}

string PPC32Emulator::dasm_EC_18_fres(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fres");
}



void PPC32Emulator::exec_EC_19_fmuls(uint32_t op) {
  this->exec_unimplemented(op); // 111011 DDDDD AAAAA 00000 CCCCC 11001 R
}

string PPC32Emulator::dasm_EC_19_fmuls(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_c_r(op, "fmuls");
}



void PPC32Emulator::exec_EC_1C_fmsubs(uint32_t op) {
  this->exec_unimplemented(op); // 111011 DDDDD AAAAA BBBBB CCCCC 11100 R
}

string PPC32Emulator::dasm_EC_1C_fmsubs(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fmsubs");
}



void PPC32Emulator::exec_EC_1D_fmadds(uint32_t op) {
  this->exec_unimplemented(op); // 111011 DDDDD AAAAA BBBBB CCCCC 11101 R
}

string PPC32Emulator::dasm_EC_1D_fmadds(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fmadds");
}



void PPC32Emulator::exec_EC_1E_fnmsubs(uint32_t op) {
  this->exec_unimplemented(op); // 111011 DDDDD AAAAA BBBBB CCCCC 11110 R
}

string PPC32Emulator::dasm_EC_1E_fnmsubs(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fnmsubs");
}



void PPC32Emulator::exec_EC_1F_fnmadds(uint32_t op) {
  this->exec_unimplemented(op); // 111011 DDDDD AAAAA BBBBB CCCCC 11111 R
}

string PPC32Emulator::dasm_EC_1F_fnmadds(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fnmadds");
}



void PPC32Emulator::exec_FC(uint32_t op) {
  uint8_t short_sub = op_get_short_subopcode(op);
  if (short_sub & 0x10) {
    switch (short_sub) {
      case 0x12:
        this->exec_FC_12_fdiv(op);
        break;
      case 0x14:
        this->exec_FC_14_fsub(op);
        break;
      case 0x15:
        this->exec_FC_15_fadd(op);
        break;
      case 0x16:
        this->exec_FC_16_fsqrt(op);
        break;
      case 0x17:
        this->exec_FC_17_fsel(op);
        break;
      case 0x19:
        this->exec_FC_19_fmul(op);
        break;
      case 0x1A:
        this->exec_FC_1A_frsqrte(op);
        break;
      case 0x1C:
        this->exec_FC_1C_fmsub(op);
        break;
      case 0x1D:
        this->exec_FC_1D_fmadd(op);
        break;
      case 0x1E:
        this->exec_FC_1E_fnmsub(op);
        break;
      case 0x1F:
        this->exec_FC_1F_fnmadd(op);
        break;
      default:
        throw runtime_error("invalid FC subopcode");
    }
  } else {
    switch (op_get_subopcode(op)) {
      case 0x000:
        this->exec_FC_000_fcmpu(op);
        break;
      case 0x00C:
        this->exec_FC_00C_frsp(op);
        break;
      case 0x00E:
        this->exec_FC_00E_fctiw(op);
        break;
      case 0x00F:
        this->exec_FC_00F_fctiwz(op);
        break;
      case 0x020:
        this->exec_FC_020_fcmpo(op);
        break;
      case 0x026:
        this->exec_FC_026_mtfsb1(op);
        break;
      case 0x028:
        this->exec_FC_028_fneg(op);
        break;
      case 0x040:
        this->exec_FC_040_mcrfs(op);
        break;
      case 0x046:
        this->exec_FC_046_mtfsb0(op);
        break;
      case 0x048:
        this->exec_FC_048_fmr(op);
        break;
      case 0x086:
        this->exec_FC_086_mtfsfi(op);
        break;
      case 0x088:
        this->exec_FC_088_fnabs(op);
        break;
      case 0x108:
        this->exec_FC_108_fabs(op);
        break;
      case 0x247:
        this->exec_FC_247_mffs(op);
        break;
      case 0x2C7:
        this->exec_FC_2C7_mtfsf(op);
        break;
      default:
        throw runtime_error("invalid FC subopcode");
    }
  }
}

string PPC32Emulator::dasm_FC(uint32_t pc, uint32_t op, map<uint32_t, bool>& branch_target_addresses) {
  uint8_t short_sub = op_get_short_subopcode(op);
  if (short_sub & 0x10) {
    switch (short_sub) {
      case 0x12:
        return PPC32Emulator::dasm_FC_12_fdiv(pc, op, branch_target_addresses);
      case 0x14:
        return PPC32Emulator::dasm_FC_14_fsub(pc, op, branch_target_addresses);
      case 0x15:
        return PPC32Emulator::dasm_FC_15_fadd(pc, op, branch_target_addresses);
      case 0x16:
        return PPC32Emulator::dasm_FC_16_fsqrt(pc, op, branch_target_addresses);
      case 0x17:
        return PPC32Emulator::dasm_FC_17_fsel(pc, op, branch_target_addresses);
      case 0x19:
        return PPC32Emulator::dasm_FC_19_fmul(pc, op, branch_target_addresses);
      case 0x1A:
        return PPC32Emulator::dasm_FC_1A_frsqrte(pc, op, branch_target_addresses);
      case 0x1C:
        return PPC32Emulator::dasm_FC_1C_fmsub(pc, op, branch_target_addresses);
      case 0x1D:
        return PPC32Emulator::dasm_FC_1D_fmadd(pc, op, branch_target_addresses);
      case 0x1E:
        return PPC32Emulator::dasm_FC_1E_fnmsub(pc, op, branch_target_addresses);
      case 0x1F:
        return PPC32Emulator::dasm_FC_1F_fnmadd(pc, op, branch_target_addresses);
      default:
        return ".invalid  FC, 1";
    }
  } else {
    switch (op_get_subopcode(op)) {
      case 0x000:
        return PPC32Emulator::dasm_FC_000_fcmpu(pc, op, branch_target_addresses);
      case 0x00C:
        return PPC32Emulator::dasm_FC_00C_frsp(pc, op, branch_target_addresses);
      case 0x00E:
        return PPC32Emulator::dasm_FC_00E_fctiw(pc, op, branch_target_addresses);
      case 0x00F:
        return PPC32Emulator::dasm_FC_00F_fctiwz(pc, op, branch_target_addresses);
      case 0x020:
        return PPC32Emulator::dasm_FC_020_fcmpo(pc, op, branch_target_addresses);
      case 0x026:
        return PPC32Emulator::dasm_FC_026_mtfsb1(pc, op, branch_target_addresses);
      case 0x028:
        return PPC32Emulator::dasm_FC_028_fneg(pc, op, branch_target_addresses);
      case 0x040:
        return PPC32Emulator::dasm_FC_040_mcrfs(pc, op, branch_target_addresses);
      case 0x046:
        return PPC32Emulator::dasm_FC_046_mtfsb0(pc, op, branch_target_addresses);
      case 0x048:
        return PPC32Emulator::dasm_FC_048_fmr(pc, op, branch_target_addresses);
      case 0x086:
        return PPC32Emulator::dasm_FC_086_mtfsfi(pc, op, branch_target_addresses);
      case 0x088:
        return PPC32Emulator::dasm_FC_088_fnabs(pc, op, branch_target_addresses);
      case 0x108:
        return PPC32Emulator::dasm_FC_108_fabs(pc, op, branch_target_addresses);
      case 0x247:
        return PPC32Emulator::dasm_FC_247_mffs(pc, op, branch_target_addresses);
      case 0x2C7:
        return PPC32Emulator::dasm_FC_2C7_mtfsf(pc, op, branch_target_addresses);
      default:
        return ".invalid  FC, 0";
    }
  }
}



void PPC32Emulator::exec_FC_12_fdiv(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD AAAAA BBBBB 00000 10010 R
}

string PPC32Emulator::dasm_FC_12_fdiv(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_r(op, "fdiv");
}



void PPC32Emulator::exec_FC_14_fsub(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD AAAAA BBBBB 00000 10100 R
}

string PPC32Emulator::dasm_FC_14_fsub(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_r(op, "fsub");
}



void PPC32Emulator::exec_FC_15_fadd(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD AAAAA BBBBB 00000 10101 R
}

string PPC32Emulator::dasm_FC_15_fadd(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_r(op, "fadd");
}



void PPC32Emulator::exec_FC_16_fsqrt(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD 00000 BBBBB 00000 10110 R
}

string PPC32Emulator::dasm_FC_16_fsqrt(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fsqrt");
}



void PPC32Emulator::exec_FC_17_fsel(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD AAAAA BBBBB CCCCC 10111 R
}

string PPC32Emulator::dasm_FC_17_fsel(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fsel");
}



void PPC32Emulator::exec_FC_19_fmul(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD AAAAA 00000 CCCCC 11001 R
}

string PPC32Emulator::dasm_FC_19_fmul(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_c_r(op, "fmul");
}



void PPC32Emulator::exec_FC_1A_frsqrte(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD 00000 BBBBB 00000 11010 R
}

string PPC32Emulator::dasm_FC_1A_frsqrte(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "frsqrte");
}



void PPC32Emulator::exec_FC_1C_fmsub(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD AAAAA BBBBB CCCCC 11100 R
}

string PPC32Emulator::dasm_FC_1C_fmsub(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fmsub");
}



void PPC32Emulator::exec_FC_1D_fmadd(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD AAAAA BBBBB CCCCC 11101 R
}

string PPC32Emulator::dasm_FC_1D_fmadd(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fmadd");
}



void PPC32Emulator::exec_FC_1E_fnmsub(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD AAAAA BBBBB CCCCC 11110 R
}

string PPC32Emulator::dasm_FC_1E_fnmsub(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fnmsub");
}



void PPC32Emulator::exec_FC_1F_fnmadd(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD AAAAA BBBBB CCCCC 11111 R
}

string PPC32Emulator::dasm_FC_1F_fnmadd(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fnmadd");
}



void PPC32Emulator::exec_FC_000_fcmpu(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDD 00 AAAAA BBBBB 0000000000 0
}

string PPC32Emulator::dasm_FC_000_fcmpu(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t crf = op_get_crf1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (crf) {
    return string_printf("fcmpu     cr%hhu, f%hhu, f%hhu", crf, ra, rb);
  } else {
    return string_printf("fcmpu     f%hhu, f%hhu", ra, rb);
  }
}



void PPC32Emulator::exec_FC_00C_frsp(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD 00000 BBBBB 0000001100 R
}

string PPC32Emulator::dasm_FC_00C_frsp(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "frsp");
}



void PPC32Emulator::exec_FC_00E_fctiw(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD 00000 BBBBB 0000001110 R
}

string PPC32Emulator::dasm_FC_00E_fctiw(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fctiw");
}



void PPC32Emulator::exec_FC_00F_fctiwz(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD 00000 BBBBB 0000001111 R
}

string PPC32Emulator::dasm_FC_00F_fctiwz(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fctiwz");
}



void PPC32Emulator::exec_FC_020_fcmpo(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDD 00 AAAAA BBBBB 0000100000 0
}

string PPC32Emulator::dasm_FC_020_fcmpo(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t crf = op_get_crf1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (crf) {
    return string_printf("fcmpo     cr%hhu, f%hhu, f%hhu", crf, ra, rb);
  } else {
    return string_printf("fcmpo     f%hhu, f%hhu", ra, rb);
  }
}



void PPC32Emulator::exec_FC_026_mtfsb1(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD 00000 00000 0000100110 R
}

string PPC32Emulator::dasm_FC_026_mtfsb1(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  bool rec = op_get_rec(op);
  uint8_t crb = op_get_reg1(op);
  return string_printf("mtfsb1%c   crb%hhu", rec ? '.' : ' ', crb);
}



void PPC32Emulator::exec_FC_028_fneg(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD 00000 BBBBB 0000101000 R
}

string PPC32Emulator::dasm_FC_028_fneg(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fneg");
}



void PPC32Emulator::exec_FC_040_mcrfs(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDD 00 SSS 00 00000 0001000000 0
}

string PPC32Emulator::dasm_FC_040_mcrfs(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  uint8_t crf = op_get_crf1(op);
  uint8_t fpscrf = op_get_crf2(op);
  return string_printf("mcrfs     cr%hhu, cr%hhu", crf, fpscrf);
}



void PPC32Emulator::exec_FC_046_mtfsb0(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD 00000 00000 0001000110 R
}

string PPC32Emulator::dasm_FC_046_mtfsb0(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  bool rec = op_get_rec(op);
  uint8_t crb = op_get_reg1(op);
  return string_printf("mtfsb0%c   crb%hhu", rec ? '.' : ' ', crb);
}



void PPC32Emulator::exec_FC_048_fmr(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD 00000 BBBBB 0001001000 R
}

string PPC32Emulator::dasm_FC_048_fmr(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fmr");
}



void PPC32Emulator::exec_FC_086_mtfsfi(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDD 00 00000 IIII 0 0010000110 R
}

string PPC32Emulator::dasm_FC_086_mtfsfi(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  bool rec = op_get_rec(op);
  uint8_t crf = op_get_crf1(op);
  uint8_t imm = (op >> 12) & 0x0F;
  return string_printf("mtfsfi%c   cr%hhu, 0x%hhX", rec ? '.' : ' ', crf, imm);
}



void PPC32Emulator::exec_FC_088_fnabs(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD 00000 BBBBB 0010001000 R
}

string PPC32Emulator::dasm_FC_088_fnabs(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fnabs");
}



void PPC32Emulator::exec_FC_108_fabs(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD 00000 BBBBB 0100001000 R
}

string PPC32Emulator::dasm_FC_108_fabs(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fabs");
}



void PPC32Emulator::exec_FC_247_mffs(uint32_t op) {
  this->exec_unimplemented(op); // 111111 DDDDD 00000 00000 1001000111 R
}

string PPC32Emulator::dasm_FC_247_mffs(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  bool rec = op_get_rec(op);
  uint8_t rd = op_get_reg1(op);
  return string_printf("mffs%c     f%hhu", rec ? '.' : ' ', rd);
}



void PPC32Emulator::exec_FC_2C7_mtfsf(uint32_t op) {
  this->exec_unimplemented(op); // 111111 0 FFFFFFFF 0 BBBBB 1011000111 R
}

string PPC32Emulator::dasm_FC_2C7_mtfsf(uint32_t, uint32_t op, map<uint32_t, bool>&) {
  bool rec = op_get_rec(op);
  uint8_t rb = op_get_reg3(op);
  uint8_t fm = (op >> 17) & 0xFF;
  return string_printf("mtfsf%c    0x%02hhX, f%hhu", rec ? '.' : ' ', fm, rb);
}



const PPC32Emulator::OpcodeImplementation PPC32Emulator::fns[0x40] = {
  /* 00 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* 04 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* 08 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* 0C */ {&PPC32Emulator::exec_0C_twi, &PPC32Emulator::dasm_0C_twi},
  /* 10 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* 14 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* 18 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* 1C */ {&PPC32Emulator::exec_1C_mulli, &PPC32Emulator::dasm_1C_mulli},
  /* 20 */ {&PPC32Emulator::exec_20_subfic, &PPC32Emulator::dasm_20_subfic},
  /* 24 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* 28 */ {&PPC32Emulator::exec_28_cmpli, &PPC32Emulator::dasm_28_cmpli},
  /* 2C */ {&PPC32Emulator::exec_2C_cmpi, &PPC32Emulator::dasm_2C_cmpi},
  /* 30 */ {&PPC32Emulator::exec_30_34_addic, &PPC32Emulator::dasm_30_34_addic},
  /* 34 */ {&PPC32Emulator::exec_30_34_addic, &PPC32Emulator::dasm_30_34_addic},
  /* 38 */ {&PPC32Emulator::exec_38_addi, &PPC32Emulator::dasm_38_addi},
  /* 3C */ {&PPC32Emulator::exec_3C_addis, &PPC32Emulator::dasm_3C_addis},
  /* 40 */ {&PPC32Emulator::exec_40_bc, &PPC32Emulator::dasm_40_bc},
  /* 44 */ {&PPC32Emulator::exec_44_sc, &PPC32Emulator::dasm_44_sc},
  /* 48 */ {&PPC32Emulator::exec_48_b, &PPC32Emulator::dasm_48_b},
  /* 4C */ {&PPC32Emulator::exec_4C, &PPC32Emulator::dasm_4C},
  /* 50 */ {&PPC32Emulator::exec_50_rlwimi, &PPC32Emulator::dasm_50_rlwimi},
  /* 54 */ {&PPC32Emulator::exec_54_rlwinm, &PPC32Emulator::dasm_54_rlwinm},
  /* 58 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* 5C */ {&PPC32Emulator::exec_5C_rlwnm, &PPC32Emulator::dasm_5C_rlwnm},
  /* 60 */ {&PPC32Emulator::exec_60_ori, &PPC32Emulator::dasm_60_ori},
  /* 64 */ {&PPC32Emulator::exec_64_oris, &PPC32Emulator::dasm_64_oris},
  /* 68 */ {&PPC32Emulator::exec_68_xori, &PPC32Emulator::dasm_68_xori},
  /* 6C */ {&PPC32Emulator::exec_6C_xoris, &PPC32Emulator::dasm_6C_xoris},
  /* 70 */ {&PPC32Emulator::exec_70_andi_rec, &PPC32Emulator::dasm_70_andi_rec},
  /* 74 */ {&PPC32Emulator::exec_74_andis_rec, &PPC32Emulator::dasm_74_andis_rec},
  /* 78 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* 7C */ {&PPC32Emulator::exec_7C, &PPC32Emulator::dasm_7C},
  /* 80 */ {&PPC32Emulator::exec_80_84_lwz_lwzu, &PPC32Emulator::dasm_80_84_lwz_lwzu},
  /* 84 */ {&PPC32Emulator::exec_80_84_lwz_lwzu, &PPC32Emulator::dasm_80_84_lwz_lwzu},
  /* 88 */ {&PPC32Emulator::exec_88_8C_lbz_lbzu, &PPC32Emulator::dasm_88_8C_lbz_lbzu},
  /* 8C */ {&PPC32Emulator::exec_88_8C_lbz_lbzu, &PPC32Emulator::dasm_88_8C_lbz_lbzu},
  /* 90 */ {&PPC32Emulator::exec_90_94_stw_stwu, &PPC32Emulator::dasm_90_94_stw_stwu},
  /* 94 */ {&PPC32Emulator::exec_90_94_stw_stwu, &PPC32Emulator::dasm_90_94_stw_stwu},
  /* 98 */ {&PPC32Emulator::exec_98_9C_stb_stbu, &PPC32Emulator::dasm_98_9C_stb_stbu},
  /* 9C */ {&PPC32Emulator::exec_98_9C_stb_stbu, &PPC32Emulator::dasm_98_9C_stb_stbu},
  /* A0 */ {&PPC32Emulator::exec_A0_A4_lhz_lhzu, &PPC32Emulator::dasm_A0_A4_lhz_lhzu},
  /* A4 */ {&PPC32Emulator::exec_A0_A4_lhz_lhzu, &PPC32Emulator::dasm_A0_A4_lhz_lhzu},
  /* A8 */ {&PPC32Emulator::exec_A8_AC_lha_lhau, &PPC32Emulator::dasm_A8_AC_lha_lhau},
  /* AC */ {&PPC32Emulator::exec_A8_AC_lha_lhau, &PPC32Emulator::dasm_A8_AC_lha_lhau},
  /* B0 */ {&PPC32Emulator::exec_B0_B4_sth_sthu, &PPC32Emulator::dasm_B0_B4_sth_sthu},
  /* B4 */ {&PPC32Emulator::exec_B0_B4_sth_sthu, &PPC32Emulator::dasm_B0_B4_sth_sthu},
  /* B8 */ {&PPC32Emulator::exec_B8_lmw, &PPC32Emulator::dasm_B8_lmw},
  /* BC */ {&PPC32Emulator::exec_BC_stmw, &PPC32Emulator::dasm_BC_stmw},
  /* C0 */ {&PPC32Emulator::exec_C0_C4_lfs_lfsu, &PPC32Emulator::dasm_C0_C4_lfs_lfsu},
  /* C4 */ {&PPC32Emulator::exec_C0_C4_lfs_lfsu, &PPC32Emulator::dasm_C0_C4_lfs_lfsu},
  /* C8 */ {&PPC32Emulator::exec_C8_CC_lfd_lfdu, &PPC32Emulator::dasm_C8_CC_lfd_lfdu},
  /* CC */ {&PPC32Emulator::exec_C8_CC_lfd_lfdu, &PPC32Emulator::dasm_C8_CC_lfd_lfdu},
  /* D0 */ {&PPC32Emulator::exec_D0_D4_stfs_stfsu, &PPC32Emulator::dasm_D0_D4_stfs_stfsu},
  /* D4 */ {&PPC32Emulator::exec_D0_D4_stfs_stfsu, &PPC32Emulator::dasm_D0_D4_stfs_stfsu},
  /* D8 */ {&PPC32Emulator::exec_D8_DC_stfd_stfdu, &PPC32Emulator::dasm_D8_DC_stfd_stfdu},
  /* DC */ {&PPC32Emulator::exec_D8_DC_stfd_stfdu, &PPC32Emulator::dasm_D8_DC_stfd_stfdu},
  /* E0 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* E4 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* E8 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* EC */ {&PPC32Emulator::exec_EC, &PPC32Emulator::dasm_EC},
  /* F0 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* F4 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* F8 */ {&PPC32Emulator::exec_invalid, &PPC32Emulator::dasm_invalid},
  /* FC */ {&PPC32Emulator::exec_FC, &PPC32Emulator::dasm_FC},
};

PPC32Registers::PPC32Registers() {
  memset(this, 0, sizeof(*this));
  this->tbr_ticks_per_cycle = 1;
}

void PPC32Registers::print_header(FILE* stream) {
  fprintf(stream, "---r0---/---r1---/---r2---/---r3---/---r4---/---r5---/"
      "---r6---/---r7---/---r8---/---r9---/--r10---/--r11---/--r12---/"
      "--r13---/--r14---/--r15---/--r16---/--r17---/--r18---/--r19---/"
      "--r20---/--r21---/--r22---/--r23---/--r24---/--r25---/--r26---/"
      "--r27---/--r28---/--r29---/--r30---/--r31--- ---CR--- ---LR--- --CTR--- ---PC---");
}

void PPC32Registers::print(FILE* stream) const {
  for (size_t x = 0; x < 32; x++) {
    if (x != 0) {
      fputc('/', stream);
    }
    fprintf(stream, "%08X", this->r[x].u);
  }

  // Uncomment to add floats (not very useful for debugging currently)
  // fprintf(stream, "%lg", this->f[0].f);
  // for (size_t x = 1; x < 32; x++) {
  //   fprintf(stream, "/%lg", this->f[x].f);
  // }

  fprintf(stream, " %08" PRIX32, this->cr.u);
  // fprintf(stream, " fpscr/%08" PRIX32, this->fpscr);
  // fprintf(stream, " xer/%08" PRIX32, this->xer.u);
  fprintf(stream, " %08" PRIX32, this->lr);
  fprintf(stream, " %08" PRIX32, this->ctr);
  // fprintf(stream, " tbr/%016" PRIX64, this->tbr);
  fprintf(stream, " %08" PRIX32, this->pc);
  // fprintf(stream, " addr/%08" PRIX32, this->debug.addr);
}

PPC32Emulator::PPC32Emulator(shared_ptr<MemoryContext> mem)
  : EmulatorBase(mem) { }

void PPC32Emulator::import_state(FILE*) {
  throw runtime_error("PPC32Emulator::import_state is not implemented");
}

void PPC32Emulator::export_state(FILE*) const {
  throw runtime_error("PPC32Emulator::export_state is not implemented");
}

void PPC32Emulator::print_state_header(FILE* stream) {
  this->regs.print_header(stream);
}

void PPC32Emulator::print_state(FILE* stream) {
  this->regs.print(stream);
}



void PPC32Emulator::execute() {
  if (!this->interrupt_manager.get()) {
    this->interrupt_manager.reset(new InterruptManager());
  }

  for (;;) {
    try {
      if (this->debug_hook) {
        this->debug_hook(*this);
      }

      this->interrupt_manager->on_cycle_start();

      uint32_t full_op = this->mem->read<be_uint32_t>(this->regs.pc);
      uint8_t op = op_get_op(full_op);
      auto fn = this->fns[op].exec;
      (this->*fn)(full_op);
      this->regs.pc += 4;
      this->regs.tbr += this->regs.tbr_ticks_per_cycle;

    } catch (const terminate_emulation&) {
      break;
    }
  }
}

string PPC32Emulator::disassemble_one(uint32_t pc, uint32_t opcode, map<uint32_t, bool>& branch_target_addresses) {
  return PPC32Emulator::fns[op_get_op(opcode)].dasm(pc, opcode, branch_target_addresses);
}

string PPC32Emulator::disassemble_one(uint32_t pc, uint32_t opcode) {
  map<uint32_t, bool> branch_target_addresses;
  return PPC32Emulator::disassemble_one(pc, opcode, branch_target_addresses);
}

string PPC32Emulator::disassemble(const void* data, size_t size, uint32_t pc,
    const multimap<uint32_t, string>* labels) {
  static const multimap<uint32_t, string> empty_labels_map = {};
  if (!labels) {
    labels = &empty_labels_map;
  }

  const be_uint32_t* opcodes = reinterpret_cast<const be_uint32_t*>(data);

  // Phase 1: generate the disassembly for each opcode, and collect branch
  // target addresses
  uint32_t start_pc = pc;
  size_t line_count = size / 4;
  map<uint32_t, bool> branch_target_addresses;
  forward_list<string> lines;
  auto add_line_it = lines.before_begin();
  for (size_t x = 0; x < line_count; x++, pc += 4) {
    uint32_t opcode = opcodes[x];
    string line = string_printf("%08X  %08X  ", pc, opcode);
    line += PPC32Emulator::disassemble_one(pc, opcode, branch_target_addresses);
    line += '\n';
    add_line_it = lines.emplace_after(add_line_it, move(line));
  }

  // Phase 2: add labels from the passed-in labels dict and from disassembled
  // branch opcodes; while doing so, count the number of bytes in the output.
  pc = start_pc;
  size_t ret_bytes = 0;
  auto branch_target_addresses_it = branch_target_addresses.lower_bound(start_pc);
  auto label_it = labels->lower_bound(start_pc);
  for (auto prev_line_it = lines.before_begin(), line_it = lines.begin();
       line_it != lines.end();
       prev_line_it = line_it++, pc += 4) {
    for (; label_it != labels->end() && label_it->first <= pc; label_it++) {
      string label;
      if (label_it->first != pc) {
        label = string_printf("%s: // at %08" PRIX32 " (misaligned)\n",
              label_it->second.c_str(), label_it->first);
      } else {
        label = string_printf("%s:\n", label_it->second.c_str());
      }
      ret_bytes += label.size();
      prev_line_it = lines.emplace_after(prev_line_it, move(label));
    }
    for (; branch_target_addresses_it != branch_target_addresses.end() &&
           branch_target_addresses_it->first <= pc;
         branch_target_addresses_it++) {
      string label;
    const char* label_type = branch_target_addresses_it->second ? "fn" : "label";
      if (branch_target_addresses_it->first != pc) {
        label = string_printf("%s%08" PRIX32 ": // (misaligned)\n",
            label_type, branch_target_addresses_it->first);
      } else {
        label = string_printf("%s%08" PRIX32 ":\n",
            label_type, branch_target_addresses_it->first);
      }
      ret_bytes += label.size();
      prev_line_it = lines.emplace_after(prev_line_it, move(label));
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
