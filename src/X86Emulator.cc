#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <array>
#include <deque>
#include <forward_list>
#include <utility>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <unordered_map>

#include "X86Emulator.hh"

using namespace std;



string extend(const std::string& s, size_t len) {
  string ret = s;
  if (ret.size() < len) {
    ret.resize(len, ' ');
  }
  return ret;
}



uint8_t X86Emulator::DisassemblyState::standard_operand_size() const {
  if (this->opcode & 1) {
    return this->overrides.operand_size ? 16 : 32;
  } else {
    return 8;
  }
}

static uint32_t get_operand(StringReader& r, uint8_t operand_size) {
  if (operand_size == 8) {
    return r.get_u8();
  } else if (operand_size == 16) {
    return r.get_u16l();
  } else if (operand_size == 32) {
    return r.get_u32l();
  } else {
    throw logic_error("invalid operand size");
  }
}

static const char* const name_for_condition_code[0x10] = {
    "o", "no", "b", "ae", "e", "ne", "be", "a",
    "s", "ns", "pe", "po", "l", "ge", "le", "g"};



X86Registers::X86Registers() {
  for (size_t x = 0; x < 8; x++) {
    this->regs[x].u = 0;
  }
  // Default flags:
  // 0x00200000 (bit 21) = able to use cpuid instruction
  // 0x00003000 (bits 12 and 13) = I/O privilege level (3)
  // 0x00000200 (bit 9) = interrupts enabled
  // 0x00000002 (bit 1) = reserved, but apparently always set in EFLAGS
  this->eflags = 0x00203202;
  this->eip = 0;
}

void X86Registers::set_by_name(const std::string& reg_name, uint32_t value) {
  if ((reg_name == "eax") || (reg_name == "EAX")) {
    this->eax().u = value;
  } else if ((reg_name == "ecx") || (reg_name == "ECX")) {
    this->ecx().u = value;
  } else if ((reg_name == "edx") || (reg_name == "EDX")) {
    this->edx().u = value;
  } else if ((reg_name == "ebx") || (reg_name == "EBX")) {
    this->ebx().u = value;
  } else if ((reg_name == "esp") || (reg_name == "ESP")) {
    this->esp().u = value;
  } else if ((reg_name == "ebp") || (reg_name == "EBP")) {
    this->ebp().u = value;
  } else if ((reg_name == "esi") || (reg_name == "ESI")) {
    this->esi().u = value;
  } else if ((reg_name == "edi") || (reg_name == "EDI")) {
    this->edi().u = value;
  } else if ((reg_name == "eflags") || (reg_name == "EFLAGS")) {
    this->eflags = value;
  } else {
    throw invalid_argument("unknown x86 register");
  }
}

uint8_t& X86Registers::reg8(uint8_t which) {
  if (which & ~7) {
    throw logic_error("invalid register index");
  }
  if (which & 4) {
    return this->regs[which & 3].u8.h;
  } else {
    return this->regs[which].u8.l;
  }
}

le_uint16_t& X86Registers::reg16(uint8_t which) {
  if (which & ~7) {
    throw logic_error("invalid register index");
  }
  return this->regs[which].u16;
}

le_uint32_t& X86Registers::reg32(uint8_t which) {
  if (which & ~7) {
    throw logic_error("invalid register index");
  }
  return this->regs[which].u;
}

bool X86Registers::flag(uint32_t mask) const {
  return this->eflags & mask;
}

void X86Registers::replace_flag(uint32_t mask, bool value) {
  this->eflags = (this->eflags & ~mask) | (value ? mask : 0);
}

std::string X86Registers::flags_str(uint32_t flags) {
  string ret;
  ret += (flags & OF) ? 'o' : '-';
  ret += (flags & DF) ? 'd' : '-';
  ret += (flags & IF) ? 'i' : '-';
  ret += (flags & SF) ? 's' : '-';
  ret += (flags & ZF) ? 'z' : '-';
  ret += (flags & AF) ? 'a' : '-';
  ret += (flags & PF) ? 'p' : '-';
  ret += (flags & CF) ? 'c' : '-';
  return ret;
}

std::string X86Registers::flags_str() const {
  return this->flags_str(this->eflags);
}

bool X86Registers::check_condition(uint8_t cc) const {
  switch (cc) {
    case 0x00: // o
    case 0x01: // no
      return this->flag(OF) != (cc & 1);
    case 0x02: // b/nae/c
    case 0x03: // nb/ae/nc
      return this->flag(CF) != (cc & 1);
    case 0x04: // z/e
    case 0x05: // nz/ne
      return this->flag(ZF) != (cc & 1);
    case 0x06: // be/na
    case 0x07: // nbe/a
      return (this->flag(CF) || this->flag(ZF)) != (cc & 1);
    case 0x08: // s
    case 0x09: // ns
      return this->flag(SF) != (cc & 1);
    case 0x0A: // p/pe
    case 0x0B: // np/po
      return this->flag(PF) != (cc & 1);
    case 0x0C: // l/nge
    case 0x0D: // nl/ge
      return (this->flag(SF) != this->flag(OF)) != (cc & 1);
    case 0x0E: // le/ng
    case 0x0F: // nle/g
      return (this->flag(ZF) || (this->flag(SF) != this->flag(OF))) != (cc & 1);
    default:
      throw logic_error("invalid condition code");
  }
}

template <typename T>
void X86Registers::set_flags_integer_result(T res, uint32_t apply_mask) {
  if (apply_mask & SF) {
    // SF should be set if the result is negative
    this->replace_flag(SF, res & (1 << (bits_for_type<T> - 1)));
  }
  if (apply_mask & ZF) {
    // ZF should be set if the result is zero
    this->replace_flag(ZF, (res == 0));
  }
  if (apply_mask & PF) {
    // PF should be set if the number of ones is even (TODO: Or should it be set
    // if it's odd?). However, x86's PF apparently only applies to the
    // least-significant byte of the result (why??)
    bool pf = true;
    uint8_t v = res;
    for (size_t x = 0; x < 8; x++, v >>= 1) {
      pf ^= (v & 1);
    }
    this->replace_flag(PF, pf);
  }
}

template <typename T>
void X86Registers::set_flags_bitwise_result(T res, uint32_t apply_mask) {
  this->set_flags_integer_result(res, apply_mask);
  if (apply_mask & OF) {
    this->replace_flag(OF, false);
  }
  if (apply_mask & CF) {
    this->replace_flag(CF, false);
  }
  // The manuals say that AF is undefined for bitwise operations (so it MAY be
  // changed). We just leave it alone here.
}

template <typename T>
T X86Registers::set_flags_integer_add(T a, T b, uint32_t apply_mask) {
  T res = a + b;

  this->set_flags_integer_result(res);

  if (apply_mask & OF) {
    // OF should be set if a and b have the same sign and the result has the
    // opposite sign (that is, the signed result has overflowed)
    this->replace_flag(OF,
        ((a & (1 << (bits_for_type<T> - 1))) == (b & (1 << (bits_for_type<T> - 1)))) &&
        ((a & (1 << (bits_for_type<T> - 1))) != (res & (1 << (bits_for_type<T> - 1)))));
  }
  if (apply_mask & CF) {
    // CF should be set if any nonzero bits were carried out
    this->replace_flag(CF, (res < a) || (res < b));
  }
  if (apply_mask & AF) {
    // AF should be set if any nonzero bits were carried out of the lowest
    // nybble
    // TODO: Is this logic correct?
    this->replace_flag(AF, ((res & 0x0F) < (a & 0x0F)) || ((res & 0x0F) < (b & 0x0F)));
  }

  return res;
}

template <typename T>
T X86Registers::set_flags_integer_subtract(T a, T b, uint32_t apply_mask) {
  T res = a - b;

  this->set_flags_integer_result(res);

  if (apply_mask & OF) {
    // OF should be set if a and b have opposite signs and the result has the
    // opposite sign as the minuend (that is, the signed result has overflowed)
    this->replace_flag(OF,
        ((a & (1 << (bits_for_type<T> - 1))) != (b & (1 << (bits_for_type<T> - 1)))) &&
        ((a & (1 << (bits_for_type<T> - 1))) != (res & (1 << (bits_for_type<T> - 1)))));
  }
  if (apply_mask & CF) {
    // CF should be set if any nonzero bits were borrowed in. Equivalently, if
    // the unsigned result is larger than the original minuend, then an external
    // borrow occurred.
    this->replace_flag(CF, (res > a));
  }
  if (apply_mask & AF) {
    // AF should be set if any nonzero bits were borrowed into the lowest nybble
    // TODO: Is this logic correct?
    this->replace_flag(AF, ((res & 0x0F) > (a & 0x0F)));
  }

  return res;
}

void X86Registers::import_state(FILE* stream) {
  uint8_t version;
  freadx(stream, &version, sizeof(version));
  if (version != 0) {
    throw runtime_error("unknown format version");
  }

  for (size_t x = 0; x < 8; x++) {
    freadx(stream, &this->regs[x].u, sizeof(this->regs[x].u));
  }
  freadx(stream, &this->eflags, sizeof(this->eflags));
  freadx(stream, &this->eip, sizeof(this->eip));
}

void X86Registers::export_state(FILE* stream) const {
  uint8_t version = 0;
  fwritex(stream, &version, sizeof(version));

  for (size_t x = 0; x < 8; x++) {
    fwritex(stream, &this->regs[x].u, sizeof(this->regs[x].u));
  }
  fwritex(stream, &this->eflags, sizeof(this->eflags));
  fwritex(stream, &this->eip, sizeof(this->eip));
}



void X86Emulator::print_state_header(FILE* stream) {
  fprintf(stream, "\
-CYCLES-  --EAX--- --ECX--- --EDX--- --EBX--- --ESP--- --EBP--- --ESI--- --EDI---  \
-EFLAGS-(--BITS--) @ --EIP--- = CODE\n");
}

void X86Emulator::print_state(FILE* stream) {
  string flags_str = this->regs.flags_str();
  fprintf(stream, "\
%08" PRIX64 "  %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 "  \
%08" PRIX32 "(%s) @ %08" PRIX32 " = ",
      this->instructions_executed,
      this->regs.eax().u.load(),
      this->regs.ecx().u.load(),
      this->regs.edx().u.load(),
      this->regs.ebx().u.load(),
      this->regs.esp().u.load(),
      this->regs.ebp().u.load(),
      this->regs.esi().u.load(),
      this->regs.edi().u.load(),
      this->regs.eflags,
      flags_str.c_str(),
      this->regs.eip);

  string data;
  uint32_t addr = this->regs.eip;
  try {
    while (data.size() < 0x10) {
      data += this->mem->read_s8(addr++);
    }
  } catch (const out_of_range&) { }

  DisassemblyState s = {
    StringReader(data),
    this->regs.eip,
    0,
    this->overrides,
    {},
  };
  try {
    string disassembly = this->disassemble_one(s);
    fprintf(stream, "%s\n", disassembly.c_str());
  } catch (const exception& e) {
    fprintf(stream, "(failed: %s)\n", e.what());
  }
}



// TODO: eliminate code duplication between the two versions of this function
X86Emulator::DecodedRM X86Emulator::fetch_and_decode_rm(StringReader& r) {
  uint8_t rm = r.get_u8();
  uint8_t sib = 0;

  DecodedRM ret;
  ret.non_ea_reg = (rm >> 3) & 7;
  ret.ea_reg = rm & 7;
  ret.ea_index_reg = -1;
  ret.ea_index_scale = 0;
  ret.ea_disp = 0;

  uint8_t mode = (rm >> 6) & 3;
  if (mode == 3) {
    ret.ea_index_scale = -1; // ea_reg is a register ref, not a mem ref

  } else if (mode == 0 && ret.ea_reg == 5) {
    ret.ea_reg = -1;
    ret.ea_disp = r.get_s32l();

  } else {
    if (ret.ea_reg == 4) {
      sib = r.get_u8();
      ret.ea_reg = sib & 7;
      if ((ret.ea_reg == 5) && (mode == 0)) {
        ret.ea_reg = -1;
        ret.ea_disp = r.get_u32l();
      }
      ret.ea_index_reg = (sib >> 3) & 7;
      if (ret.ea_index_reg == 4) {
        ret.ea_index_reg = -1;
      } else {
        ret.ea_index_scale = 1 << ((sib >> 6) & 3);
      }
    }
    if (mode == 1) {
      ret.ea_disp = r.get_s8();
    } else if (mode == 2) {
      ret.ea_disp = r.get_s32l();
    }
  }

  return ret;
}

X86Emulator::DecodedRM X86Emulator::fetch_and_decode_rm() {
  uint8_t rm = this->fetch_instruction_byte();
  uint8_t sib = 0;


  DecodedRM ret;
  ret.non_ea_reg = (rm >> 3) & 7;
  ret.ea_reg = rm & 7;
  ret.ea_index_reg = -1;
  ret.ea_index_scale = 0;
  ret.ea_disp = 0;

  uint8_t mode = (rm >> 6) & 3;
  if (mode == 3) {
    ret.ea_index_scale = -1; // ea_reg is a register ref, not a mem ref

  } else if (mode == 0 && ret.ea_reg == 5) {
    ret.ea_reg = -1;
    ret.ea_disp = this->fetch_instruction_dword();

  } else {
    if (ret.ea_reg == 4) {
      sib = this->fetch_instruction_byte();
      ret.ea_reg = sib & 7;
      if ((ret.ea_reg == 5) && (mode == 0)) {
        ret.ea_reg = -1;
        ret.ea_disp = this->fetch_instruction_dword();
      }
      ret.ea_index_reg = (sib >> 3) & 7;
      if (ret.ea_index_reg == 4) {
        ret.ea_index_reg = -1;
      } else {
        ret.ea_index_scale = 1 << ((sib >> 6) & 3);
      }
    }
    if (mode == 1) {
      ret.ea_disp = static_cast<int8_t>(this->fetch_instruction_byte());
    } else if (mode == 2) {
      ret.ea_disp = this->fetch_instruction_dword();
    }
  }

  return ret;
}

static const char* name_for_reg(uint8_t reg, uint8_t operand_size) {
  if (reg & ~7) {
    throw logic_error("invalid register index");
  }
  if (operand_size == 8) {
    static const char* const reg_names[8] = {"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"};
    return reg_names[reg];
  } else if (operand_size == 16) {
    static const char* const reg_names[8] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
    return reg_names[reg];
  } else if (operand_size == 32) {
    static const char* const reg_names[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
    return reg_names[reg];
  } else {
    throw logic_error("invalid operand size");
  }
}

string X86Emulator::DecodedRM::ea_str(uint8_t operand_size) const {
  if (this->ea_index_scale == -1) {
    if (this->ea_reg & ~7) {
      throw logic_error("DecodedRM has reg ref but invalid ea_reg");
    }
    return name_for_reg(this->ea_reg, operand_size);

  } else {
    vector<string> tokens;
    if (this->ea_reg >= 0) {
      tokens.emplace_back(name_for_reg(this->ea_reg, 32));
    }
    if (this->ea_index_scale > 0) {
      if (!tokens.empty()) {
        tokens.emplace_back("+");
      }
      tokens.emplace_back(name_for_reg(this->ea_index_reg, 32));
      if (this->ea_index_scale > 1) {
        tokens.emplace_back("*");
        tokens.emplace_back(string_printf("%hhd", this->ea_index_scale));
      }
    }
    if (this->ea_disp) {
      if (tokens.empty()) {
        tokens.emplace_back(string_printf("%08" PRIX32, this->ea_disp));
      } else {
        if (this->ea_disp < 0) {
          tokens.emplace_back("-");
          tokens.emplace_back(string_printf("%08" PRIX32, -this->ea_disp));
        } else {
          tokens.emplace_back("+");
          tokens.emplace_back(string_printf("%08" PRIX32, this->ea_disp));
        }
      }
    }
    string size_str;
    if (operand_size == 8) {
      size_str = "byte";
    } else if (operand_size == 16) {
      size_str = "word";
    } else if (operand_size == 32) {
      size_str = "dword";
    } else {
      size_str = string_printf("(%02" PRIX8 ")", operand_size);
    }
    return size_str + " [" + join(tokens, " ") + "]";
  }
}

string X86Emulator::DecodedRM::non_ea_str(uint8_t operand_size) const {
  return name_for_reg(this->non_ea_reg, operand_size);
}

string X86Emulator::DecodedRM::str(uint8_t operand_size, bool ea_first) const {
  return this->str(operand_size, operand_size, ea_first);
}

string X86Emulator::DecodedRM::str(
    uint8_t ea_operand_size, uint8_t non_ea_operand_size, bool ea_first) const {
  string ea_str = this->ea_str(ea_operand_size);
  string non_ea_str = this->non_ea_str(non_ea_operand_size);
  if (ea_first) {
    return ea_str + ", " + non_ea_str;
  } else {
    return non_ea_str + ", " + ea_str;
  }
}

uint32_t X86Emulator::resolve_mem_ea(const DecodedRM& rm) {
  if (rm.ea_index_scale < 0) {
    throw logic_error("this should be handled outside of resolve_ea");
  }

  uint32_t base_component = ((rm.ea_reg >= 0) ? this->regs.reg32(rm.ea_reg).load() : 0);
  uint32_t index_component = ((rm.ea_index_scale > 0) ? (this->regs.reg32(rm.ea_index_reg) * rm.ea_index_scale) : 0);
  uint32_t disp_component = rm.ea_disp;
  return base_component + index_component + disp_component;
}

uint8_t& X86Emulator::resolve_non_ea_w8(const DecodedRM& rm) {
  return this->regs.reg8(rm.non_ea_reg);
}

le_uint16_t& X86Emulator::resolve_non_ea_w16(const DecodedRM& rm) {
  return this->regs.reg16(rm.non_ea_reg);
}

le_uint32_t& X86Emulator::resolve_non_ea_w32(const DecodedRM& rm) {
  return this->regs.reg32(rm.non_ea_reg);
}

const uint8_t& X86Emulator::resolve_non_ea_r8(const DecodedRM& rm) {
  return this->regs.reg8(rm.non_ea_reg);
}

const le_uint16_t& X86Emulator::resolve_non_ea_r16(const DecodedRM& rm) {
  return this->regs.reg16(rm.non_ea_reg);
}

const le_uint32_t& X86Emulator::resolve_non_ea_r32(const DecodedRM& rm) {
  return this->regs.reg32(rm.non_ea_reg);
}

uint8_t& X86Emulator::resolve_ea_w8(const DecodedRM& rm) {
  if (rm.ea_index_scale < 0) {
    return this->regs.reg8(rm.ea_reg);
  } else {
    uint32_t addr = this->resolve_mem_ea(rm);
    this->report_mem_access(addr, bits_for_type<uint8_t>, true);
    return *this->mem->at<uint8_t>(addr);
  }
}

le_uint16_t& X86Emulator::resolve_ea_w16(const DecodedRM& rm) {
  if (rm.ea_index_scale < 0) {
    return this->regs.reg16(rm.ea_reg);
  } else {
    uint32_t addr = this->resolve_mem_ea(rm);
    this->report_mem_access(addr, bits_for_type<le_uint16_t>, true);
    return *this->mem->at<le_uint16_t>(addr);
  }
}

le_uint32_t& X86Emulator::resolve_ea_w32(const DecodedRM& rm) {
  if (rm.ea_index_scale < 0) {
    return this->regs.reg32(rm.ea_reg);
  } else {
    uint32_t addr = this->resolve_mem_ea(rm);
    this->report_mem_access(addr, bits_for_type<le_uint32_t>, true);
    return *this->mem->at<le_uint32_t>(addr);
  }
}

const uint8_t& X86Emulator::resolve_ea_r8(const DecodedRM& rm) {
  if (rm.ea_index_scale < 0) {
    return this->regs.reg8(rm.ea_reg);
  } else {
    uint32_t addr = this->resolve_mem_ea(rm);
    this->report_mem_access(addr, bits_for_type<uint8_t>, false);
    return *this->mem->at<uint8_t>(addr);
  }
}

const le_uint16_t& X86Emulator::resolve_ea_r16(const DecodedRM& rm) {
  if (rm.ea_index_scale < 0) {
    return this->regs.reg16(rm.ea_reg);
  } else {
    uint32_t addr = this->resolve_mem_ea(rm);
    this->report_mem_access(addr, bits_for_type<le_uint16_t>, false);
    return *this->mem->at<le_uint16_t>(addr);
  }
}

const le_uint32_t& X86Emulator::resolve_ea_r32(const DecodedRM& rm) {
  if (rm.ea_index_scale < 0) {
    return this->regs.reg32(rm.ea_reg);
  } else {
    uint32_t addr = this->resolve_mem_ea(rm);
    this->report_mem_access(addr, bits_for_type<le_uint32_t>, false);
    return *this->mem->at<le_uint32_t>(addr);
  }
}



void X86Emulator::exec_0F_extensions(uint8_t) {
  uint8_t opcode = this->fetch_instruction_byte();
  auto fn = this->fns_0F[opcode].exec;
  if (fn) {
    (this->*fn)(opcode);
  } else {
    this->exec_0F_unimplemented(opcode);
  }
}

string X86Emulator::dasm_0F_extensions(DisassemblyState& s) {
  s.opcode = s.r.get_u8();
  auto fn = X86Emulator::fns_0F[s.opcode].dasm;
  return fn ? (*fn)(s) : X86Emulator::dasm_0F_unimplemented(s);
}

template <typename T>
void X86Emulator::exec_integer_math_inner(uint8_t what, T& dest, T src) {
  switch (what) {
    case 0: // add
      dest = this->regs.set_flags_integer_add<T>(dest, src);
      break;
    case 1: // or
      dest |= src;
      this->regs.set_flags_bitwise_result<T>(dest);
      break;
    case 2: { // adc
      bool cf = this->regs.flag(X86Registers::CF);
      dest = this->regs.set_flags_integer_add<T>(dest, src + cf);
      break;
    }
    case 3: { // sbb
      bool cf = this->regs.flag(X86Registers::CF);
      dest = this->regs.set_flags_integer_subtract<T>(dest, src + cf);
      break;
    }
    case 4: // and
      dest &= src;
      this->regs.set_flags_bitwise_result<T>(dest);
      break;
    case 5: // sub
      dest = this->regs.set_flags_integer_subtract<T>(dest, src);
      break;
    case 6: // xor
      dest ^= src;
      this->regs.set_flags_bitwise_result<T>(dest);
      break;
    case 7: // cmp
      this->regs.set_flags_integer_subtract<T>(dest, src);
      break;
    default:
      throw logic_error("invalid operation for low-opcode integer math");
  }
}

static const char* const integer_math_opcode_names[8] = {
    "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};

void X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math(uint8_t opcode) {
  uint8_t what = (opcode >> 3) & 7;
  DecodedRM rm = this->fetch_and_decode_rm();
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->exec_integer_math_inner<le_uint16_t>(what, this->resolve_ea_w16(rm), this->resolve_non_ea_r16(rm));
    } else {
      this->exec_integer_math_inner<le_uint32_t>(what, this->resolve_ea_w32(rm), this->resolve_non_ea_r32(rm));
    }
  } else {
    this->exec_integer_math_inner<uint8_t>(what, this->resolve_ea_w8(rm), this->resolve_non_ea_r8(rm));
  }
}

string X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math(DisassemblyState& s) {
  string opcode_name = extend(integer_math_opcode_names[(s.opcode >> 3) & 7], 10);
  DecodedRM rm = X86Emulator::fetch_and_decode_rm(s.r);
  return opcode_name + rm.str(s.standard_operand_size(), true);
}

void X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math(uint8_t opcode) {
  uint8_t what = (opcode >> 3) & 7;
  DecodedRM rm = this->fetch_and_decode_rm();
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->exec_integer_math_inner<le_uint16_t>(what, this->resolve_non_ea_w16(rm), this->resolve_ea_r16(rm));
    } else {
      this->exec_integer_math_inner<le_uint32_t>(what, this->resolve_non_ea_w32(rm), this->resolve_ea_r32(rm));
    }
  } else {
    this->exec_integer_math_inner<uint8_t>(what, this->resolve_non_ea_w8(rm), this->resolve_ea_r8(rm));
  }
}

string X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math(DisassemblyState& s) {
  string opcode_name = extend(integer_math_opcode_names[(s.opcode >> 3) & 7], 10);
  DecodedRM rm = X86Emulator::fetch_and_decode_rm(s.r);
  return opcode_name + rm.str(s.standard_operand_size(), false);
}

void X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math(uint8_t opcode) {
  uint8_t what = (opcode >> 3) & 7;
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->exec_integer_math_inner<le_uint16_t>(what, this->regs.eax().u16, this->fetch_instruction_word());
    } else {
      this->exec_integer_math_inner<le_uint32_t>(what, this->regs.eax().u, this->fetch_instruction_dword());
    }
  } else {
    this->exec_integer_math_inner<uint8_t>(what, this->regs.eax().u8.l, this->fetch_instruction_byte());
  }
}

string X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math(DisassemblyState& s) {
  string opcode_name = extend(integer_math_opcode_names[(s.opcode >> 3) & 7], 10);
  uint8_t operand_size = s.standard_operand_size();
  uint32_t imm = get_operand(s.r, operand_size);
  return opcode_name + string_printf("%s, %" PRIX32, name_for_reg(0, operand_size), imm);
}

void X86Emulator::exec_26_es(uint8_t) {
  this->overrides.should_clear = false;
  this->overrides.segment = Overrides::Segment::ES;
}

std::string X86Emulator::dasm_26_es(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.segment = Overrides::Segment::ES;
  return "";
}

void X86Emulator::exec_2E_cs(uint8_t) {
  this->overrides.should_clear = false;
  this->overrides.segment = Overrides::Segment::CS;
}

std::string X86Emulator::dasm_2E_cs(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.segment = Overrides::Segment::CS;
  return "";
}

void X86Emulator::exec_36_ss(uint8_t) {
  this->overrides.should_clear = false;
  this->overrides.segment = Overrides::Segment::SS;
}

std::string X86Emulator::dasm_36_ss(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.segment = Overrides::Segment::SS;
  return "";
}

void X86Emulator::exec_3E_ds(uint8_t) {
  this->overrides.should_clear = false;
  this->overrides.segment = Overrides::Segment::DS;
}

std::string X86Emulator::dasm_3E_ds(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.segment = Overrides::Segment::DS;
  return "";
}

void X86Emulator::exec_40_to_47_inc(uint8_t opcode) {
  auto& reg = this->regs.regs[opcode & 7];
  if (this->overrides.operand_size) {
    reg.u16 = this->regs.set_flags_integer_add<uint16_t>(reg.u16, 1, ~X86Registers::CF);
  } else {
    reg.u = this->regs.set_flags_integer_add<uint32_t>(reg.u, 1, ~X86Registers::CF);
  }
}

void X86Emulator::exec_48_to_4F_dec(uint8_t opcode) {
  auto& reg = this->regs.regs[opcode & 7];
  if (this->overrides.operand_size) {
    reg.u16 = this->regs.set_flags_integer_subtract<uint16_t>(reg.u16, 1, ~X86Registers::CF);
  } else {
    reg.u = this->regs.set_flags_integer_subtract<uint32_t>(reg.u, 1, ~X86Registers::CF);
  }
}

string X86Emulator::dasm_40_to_4F_inc_dec(DisassemblyState& s) {
  return string_printf("%s       %s",
      (s.opcode & 8) ? "dec" : "inc",
      name_for_reg(s.opcode & 7, s.overrides.operand_size ? 16 : 32));
}

void X86Emulator::exec_50_to_57_push(uint8_t opcode) {
  auto& reg = this->regs.regs[opcode & 7];
  if (this->overrides.operand_size) {
    this->push<le_uint16_t>(reg.u16);
  } else {
    this->push<le_uint32_t>(reg.u);
  }
}

void X86Emulator::exec_58_to_5F_pop(uint8_t opcode) {
  auto& reg = this->regs.regs[opcode & 7];
  if (this->overrides.operand_size) {
    reg.u16 = this->pop<le_uint16_t>();
  } else {
    reg.u = this->pop<le_uint32_t>();
  }
}

string X86Emulator::dasm_50_to_5F_push_pop(DisassemblyState& s) {
  return string_printf("%s      %s",
      (s.opcode & 8) ? "pop " : "push",
      name_for_reg(s.opcode & 7, s.overrides.operand_size ? 16 : 32));
}

void X86Emulator::exec_64_fs(uint8_t) {
  this->overrides.should_clear = false;
  this->overrides.segment = Overrides::Segment::FS;
}

std::string X86Emulator::dasm_64_fs(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.segment = Overrides::Segment::FS;
  return "";
}

void X86Emulator::exec_65_gs(uint8_t) {
  this->overrides.should_clear = false;
  this->overrides.segment = Overrides::Segment::GS;
}

std::string X86Emulator::dasm_65_gs(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.segment = Overrides::Segment::GS;
  return "";
}

void X86Emulator::exec_66_operand_size(uint8_t) {
  this->overrides.should_clear = false;
  this->overrides.operand_size = true;
}

string X86Emulator::dasm_66_operand_size(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.operand_size = true;
  return "";
}

void X86Emulator::exec_68_push(uint8_t) {
  if (this->overrides.operand_size) {
    this->push<le_uint16_t>(this->fetch_instruction_word());
  } else {
    this->push<le_uint32_t>(this->fetch_instruction_dword());
  }
}

string X86Emulator::dasm_68_push(DisassemblyState& s) {
  if (s.overrides.operand_size) {
    return string_printf("push      %04" PRIX16, s.r.get_u16l());
  } else {
    return string_printf("push      %08" PRIX32, s.r.get_u32l());
  }
}

void X86Emulator::exec_80_to_83_imm_math(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      uint16_t v = (opcode & 2)
          ? sign_extend<uint16_t, uint8_t>(this->fetch_instruction_byte())
          : this->fetch_instruction_word();
      this->exec_integer_math_inner<le_uint16_t>(rm.non_ea_reg, this->resolve_ea_w16(rm), v);

    } else {
      uint32_t v = (opcode & 2)
          ? sign_extend<uint32_t, uint8_t>(this->fetch_instruction_byte())
          : this->fetch_instruction_dword();
      this->exec_integer_math_inner<le_uint32_t>(rm.non_ea_reg, this->resolve_ea_w32(rm), v);
    }
  } else {
    // It looks like 82 is actually identical to 80. Is this true?
    uint8_t v = this->fetch_instruction_byte();
    this->exec_integer_math_inner<uint8_t>(rm.non_ea_reg, this->resolve_ea_w8(rm), v);
  }
}

string X86Emulator::dasm_80_to_83_imm_math(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  string opcode_name = extend(integer_math_opcode_names[rm.non_ea_reg], 10);

  if (s.opcode & 1) {
    if (s.overrides.operand_size) {
      uint16_t imm = (s.opcode & 2)
          ? sign_extend<uint16_t, uint8_t>(s.r.get_u8())
          : s.r.get_u16l();
      return opcode_name + rm.ea_str(16) + string_printf(", %" PRIX16, imm);

    } else {
      uint32_t imm = (s.opcode & 2)
          ? sign_extend<uint32_t, uint8_t>(s.r.get_u8())
          : s.r.get_u32l();
      return opcode_name + rm.ea_str(32) + string_printf(", %" PRIX32, imm);
    }
  } else {
    // It looks like 82 is actually identical to 80. Is this true?
    uint8_t imm = s.r.get_u8();
    return opcode_name + rm.ea_str(8) + string_printf(", %" PRIX8, imm);
  }
}

void X86Emulator::exec_84_85_test_rm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      uint16_t a = this->resolve_non_ea_r16(rm);
      uint16_t b = this->resolve_ea_r16(rm);
      this->regs.set_flags_bitwise_result<uint16_t>(a & b);
    } else {
      uint32_t a = this->resolve_non_ea_r32(rm);
      uint32_t b = this->resolve_ea_r32(rm);
      this->regs.set_flags_bitwise_result<uint32_t>(a & b);
    }
  } else {
    uint8_t a = this->resolve_non_ea_r8(rm);
    uint8_t b = this->resolve_ea_r8(rm);
    this->regs.set_flags_bitwise_result<uint8_t>(a & b);
  }
}

string X86Emulator::dasm_84_85_test_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return "test      " + rm.str(s.standard_operand_size(), true);
}

void X86Emulator::exec_86_87_xchg_rm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      le_uint16_t& a = this->resolve_non_ea_w16(rm);
      le_uint16_t& b = this->resolve_ea_w16(rm);
      uint16_t t = a;
      a = b;
      b = t;
    } else {
      le_uint32_t& a = this->resolve_non_ea_w32(rm);
      le_uint32_t& b = this->resolve_ea_w32(rm);
      uint32_t t = a;
      a = b;
      b = t;
    }
  } else {
    uint8_t& a = this->resolve_non_ea_w8(rm);
    uint8_t& b = this->resolve_ea_w8(rm);
    uint8_t t = a;
    a = b;
    b = t;
  }
}

string X86Emulator::dasm_86_87_xchg_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return "xchg      " + rm.str(s.standard_operand_size(), true);
}

void X86Emulator::exec_88_to_8B_mov_rm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      if (opcode & 2) {
        this->resolve_non_ea_w16(rm) = this->resolve_ea_r16(rm);
      } else {
        this->resolve_ea_w16(rm) = this->resolve_non_ea_r16(rm);
      }
    } else {
      if (opcode & 2) {
        this->resolve_non_ea_w32(rm) = this->resolve_ea_r32(rm);
      } else {
        this->resolve_ea_w32(rm) = this->resolve_non_ea_r32(rm);
      }
    }
  } else {
    if (opcode & 2) {
      this->resolve_non_ea_w8(rm) = this->resolve_ea_r8(rm);
    } else {
      this->resolve_ea_w8(rm) = this->resolve_non_ea_r8(rm);
    }
  }
}

string X86Emulator::dasm_88_to_8B_mov_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return "mov       " + rm.str(s.standard_operand_size(), !(s.opcode & 2));
}

void X86Emulator::exec_8D_lea(uint8_t) {
  // TODO: What's supposed to happen if either override is set?
  if (this->overrides.operand_size || this->overrides.address_size) {
    throw runtime_error("lea with size overrides is not implemented");
  }
  auto rm = this->fetch_and_decode_rm();
  if (rm.ea_index_scale < 0) {
    throw runtime_error("lea effective address is a register");
  }
  this->resolve_non_ea_w32(rm) = this->resolve_mem_ea(rm);
}

string X86Emulator::dasm_8D_lea(DisassemblyState& s) {
  if (s.overrides.operand_size || s.overrides.address_size) {
    return ".unknown  <<lea+override>> // unimplemented";
  }
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  if (rm.ea_index_scale < 0) {
    return ".invalid  <<lea with non-memory reference>>";
  }
  return "lea       " + rm.str(32, false);
}

void X86Emulator::exec_8F_pop_rm(uint8_t) {
  auto rm = this->fetch_and_decode_rm();
  if (rm.non_ea_reg) {
    throw runtime_error("invalid pop r/m with non_ea_reg != 0");
  }

  if (this->overrides.operand_size) {
    this->resolve_ea_w16(rm) = this->pop<le_uint16_t>();
  } else {
    this->resolve_ea_w32(rm) = this->pop<le_uint32_t>();
  }
}

string X86Emulator::dasm_8F_pop_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  if (rm.non_ea_reg) {
    return ".invalid  <<pop r/m with non_ea_reg != 0>>";
  }
  return "pop       " + rm.ea_str(s.overrides.operand_size ? 16 : 32);
}

void X86Emulator::exec_90_to_97_xchg(uint8_t opcode) {
  if (opcode == 0x90) {
    return; // nop
  }

  uint8_t reg = opcode & 7;
  if (this->overrides.operand_size) {
    auto& other = this->regs.reg16(reg);
    uint16_t t = this->regs.eax().u16;
    this->regs.eax().u16 = other;
    other = t;
  } else {
    auto& other = this->regs.reg32(reg);
    uint32_t t = this->regs.eax().u;
    this->regs.eax().u = other;
    other = t;
  }
}

string X86Emulator::dasm_90_to_97_xchg(DisassemblyState& s) {
  if (s.opcode == 0x90) {
    return "nop";
  }

  if (s.overrides.operand_size) {
    return string_printf("xchg      %s, ax", name_for_reg(s.opcode & 7, 16));
  } else {
    return string_printf("xchg      %s, eax", name_for_reg(s.opcode & 7, 32));
  }
}

void X86Emulator::exec_98_cbw_cwde(uint8_t) {
  if (this->overrides.operand_size) {
    this->regs.eax().u8.h = (this->regs.eax().u8.l & 0x80) ? 0xFF : 0x00;
  } else {
    if (this->regs.eax().u16 & 0x8000) {
      this->regs.eax().u |= 0xFFFF0000;
    } else {
      this->regs.eax().u &= 0x0000FFFF;
    }
  }
}

string X86Emulator::dasm_98_cbw_cwde(DisassemblyState& s) {
  return s.overrides.operand_size ? "cbw" : "cwde";
}

void X86Emulator::exec_99_cwd_cdq(uint8_t) {
  if (this->overrides.operand_size) {
    this->regs.edx().u16 = (this->regs.eax().u16 & 0x8000) ? 0xFFFF : 0x0000;
  } else {
    this->regs.edx().u = (this->regs.eax().u & 0x80000000) ? 0xFFFFFFFF : 0x00000000;
  }
}

string X86Emulator::dasm_99_cwd_cdq(DisassemblyState& s) {
  return s.overrides.operand_size ? "cwd" : "cdq";
}

void X86Emulator::exec_9C_pushf_pushfd(uint8_t) {
  if (this->overrides.operand_size) {
    this->push<le_uint16_t>(this->regs.eflags & 0xFFFF);
  } else {
    // Mask out the RF and VM bits
    this->push<le_uint32_t>(this->regs.eflags & 0x00FCFFFF);
  }
}

string X86Emulator::dasm_9C_pushf_pushfd(DisassemblyState& s) {
  return s.overrides.operand_size ? "pushf" : "pushfd";
}

void X86Emulator::exec_9D_popf_popfd(uint8_t) {
  if (this->overrides.operand_size) {
    static constexpr uint32_t mask = 0x00004DD5;
    this->regs.eflags = (this->regs.eflags & ~mask) | (this->pop<le_uint16_t>() & mask);
  } else {
    static constexpr uint32_t mask = 0x00044DD5;
    this->regs.eflags = (this->regs.eflags & ~mask) | (this->pop<le_uint32_t>() & mask);
  }
  this->regs.replace_flag(0x00010000, false); // clear RF
}

string X86Emulator::dasm_9D_popf_popfd(DisassemblyState& s) {
  return s.overrides.operand_size ? "popf" : "popfd";
}

void X86Emulator::exec_9F_lahf(uint8_t) {
  this->regs.eax().u8.h = this->regs.eflags & 0xFF;
}

string X86Emulator::dasm_9F_lahf(DisassemblyState&) {
  return "lahf";
}

template <typename T>
void X86Emulator::exec_movs_inner() {
  // Note: We ignore the segment registers here. Technically we should be
  // reading from ds:esi (ds may be overridden by another prefix) and writing to
  // es:edi (es may NOT be overridden). But on modern OSes, these segment
  // registers point to the same location in protected mode, so we ignore them.
  this->report_mem_access(this->regs.esi().u, bits_for_type<T>, false);
  this->report_mem_access(this->regs.edi().u, bits_for_type<T>, true);
  this->mem->write<T>(this->regs.edi().u, this->mem->read<T>(this->regs.esi().u));
  if (this->regs.flag(X86Registers::DF)) {
    this->regs.edi().u -= sizeof(T);
    this->regs.esi().u -= sizeof(T);
  } else {
    this->regs.edi().u += sizeof(T);
    this->regs.esi().u += sizeof(T);
  }
}

template <typename T>
void X86Emulator::exec_rep_movs_inner() {
  for (; this->regs.ecx().u; this->regs.ecx().u--) {
    this->exec_movs_inner<T>();
  }
}

void X86Emulator::exec_A4_A5_movs(uint8_t opcode) {
  if (this->overrides.address_size) {
    throw runtime_error("movs with overridden address size is not implemented");
  }

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      if (this->overrides.repeat_nz || this->overrides.repeat_z) {
        this->exec_rep_movs_inner<le_uint16_t>();
      } else {
        this->exec_movs_inner<le_uint16_t>();
      }
    } else {
      if (this->overrides.repeat_nz || this->overrides.repeat_z) {
        this->exec_rep_movs_inner<le_uint32_t>();
      } else {
        this->exec_movs_inner<le_uint32_t>();
      }
    }
  } else {
    if (this->overrides.repeat_nz || this->overrides.repeat_z) {
      this->exec_rep_movs_inner<uint8_t>();
    } else {
      this->exec_movs_inner<uint8_t>();
    }
  }
}

std::string X86Emulator::dasm_A4_A5_movs(DisassemblyState& s) {
  if (s.overrides.address_size) {
    return ".unknown  <<movs with overridden address size>> // unimplemented";
  }

  const char* src_segment_name = s.overrides.overridden_segment_name();
  if (!src_segment_name) {
    src_segment_name = "ds";
  }

  if (s.opcode & 1) {
    if (s.overrides.operand_size) {
      if (s.overrides.repeat_nz || s.overrides.repeat_z) {
        return string_printf("rep movs  word es:[edi], %s:[esi]", src_segment_name);
      } else {
        return string_printf("movs      word es:[edi], %s:[esi]", src_segment_name);
      }
    } else {
      if (s.overrides.repeat_nz || s.overrides.repeat_z) {
        return string_printf("rep movs  dword es:[edi], %s:[esi]", src_segment_name);
      } else {
        return string_printf("movs      dword es:[edi], %s:[esi]", src_segment_name);
      }
    }
  } else {
    if (s.overrides.repeat_nz || s.overrides.repeat_z) {
      return string_printf("rep movs  byte es:[edi], %s:[esi]", src_segment_name);
    } else {
      return string_printf("movs      byte es:[edi], %s:[esi]", src_segment_name);
    }
  }
}

void X86Emulator::exec_A8_A9_test_eax_imm(uint8_t opcode) {
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      uint16_t v = this->fetch_instruction_word();
      this->regs.set_flags_bitwise_result<uint16_t>(this->regs.eax().u16 & v);
    } else {
      uint32_t v = this->fetch_instruction_dword();
      this->regs.set_flags_bitwise_result<uint32_t>(this->regs.eax().u & v);
    }
  } else {
    uint8_t v = this->fetch_instruction_byte();
    this->regs.set_flags_bitwise_result<uint8_t>(this->regs.eax().u8.l & v);
  }
}

string X86Emulator::dasm_A8_A9_test_eax_imm(DisassemblyState& s) {
  if (s.opcode & 1) {
    if (s.overrides.operand_size) {
      return string_printf("test      ax, %04" PRIX16, s.r.get_u16l());
    } else {
      return string_printf("test      eax, %08" PRIX32, s.r.get_u32l());
    }
  } else {
    return string_printf("test      al, %02" PRIX8, s.r.get_u8());
  }
}

void X86Emulator::exec_B0_to_B7_mov_imm_8(uint8_t opcode) {
  this->regs.regs[opcode & 7].u8.l = this->fetch_instruction_byte();
}

void X86Emulator::exec_B8_to_BF_mov_imm_16_32(uint8_t opcode) {
  if (this->overrides.operand_size) {
    this->regs.regs[opcode & 7].u16 = this->fetch_instruction_word();
  } else {
    this->regs.regs[opcode & 7].u = this->fetch_instruction_dword();
  }
}

string X86Emulator::dasm_B0_to_BF_mov_imm(DisassemblyState& s) {
  if (s.opcode & 8) {
    if (s.overrides.operand_size) {
      return string_printf("mov       %s, %04" PRIX16,
          name_for_reg(s.opcode & 7, 16), s.r.get_u16l());
    } else {
      return string_printf("mov       %s, %08" PRIX32,
          name_for_reg(s.opcode & 7, 32), s.r.get_u32l());
    }
  } else {
    return string_printf("mov       %s, %02" PRIX8,
        name_for_reg(s.opcode & 7, 8), s.r.get_u8());
  }
}

template <typename T>
void X86Emulator::exec_bit_shifts_inner(
    uint8_t what, T& value, uint8_t distance) {
  switch (what) {
    case 0: // rol
      distance &= (bits_for_type<T> - 1);
      if (distance) {
        value = (value << distance) | (value >> (bits_for_type<T> - distance));
        this->regs.replace_flag(X86Registers::CF, value & 1);
        if (distance == 1) {
          this->regs.replace_flag(X86Registers::OF, !!(value & msb_for_type<T>) != (value & 1));
        }
      }
      break;
    case 1: // ror
      distance &= (bits_for_type<T> - 1);
      if (distance) {
        value = (value >> distance) | (value << (bits_for_type<T> - distance));
        this->regs.replace_flag(X86Registers::CF, value & msb_for_type<T>);
        if (distance == 1) {
          this->regs.replace_flag(X86Registers::OF, !(value & msb_for_type<T>) != !(value & (msb_for_type<T> >> 1)));
        }
      }
      break;
    case 2: { // rcl
      bool cf = this->regs.flag(X86Registers::CF);
      for (uint8_t c = (distance & 0x1F) % (bits_for_type<T> + 1); c; c--) {
        bool temp_cf = !!(value & msb_for_type<T>);
        value = (value << 1) | cf;
        cf = temp_cf;
      }
      this->regs.replace_flag(X86Registers::CF, cf);
      if ((distance & 0x1F) == 1) {
        this->regs.replace_flag(X86Registers::OF, (!!(value & msb_for_type<T>) != cf));
      }
      break;
    }
    case 3: { // rcr
      bool cf = this->regs.flag(X86Registers::CF);
      if ((distance & 0x1F) == 1) {
        this->regs.replace_flag(X86Registers::OF, (!!(value & msb_for_type<T>) != cf));
      }
      for (uint8_t c = (distance & 0x1F) % (bits_for_type<T> + 1); c; c--) {
        bool temp_cf = value & 1;
        value = (value >> 1) | (cf << (bits_for_type<T> - 1));
        cf = temp_cf;
      }
      this->regs.replace_flag(X86Registers::CF, cf);
      break;
    }
    case 4: // shl/sal
    case 5: // shr
    case 6: // sal/shl
    case 7: { // sar
      bool is_right_shift = (what & 1);
      bool is_signed = (what & 2);
      bool cf = this->regs.flag(X86Registers::CF);
      for (uint8_t c = distance & 0x1F; c; c--) {
        if (!is_right_shift) {
          cf = !!(value & msb_for_type<T>);
          value <<= 1;
        } else {
          cf = value & 1;
          value >>= 1;
          if (is_signed && (value & (msb_for_type<T> >> 1))) {
            value |= msb_for_type<T>;
          }
        }
      }
      this->regs.replace_flag(X86Registers::CF, cf);
      if ((distance & 0x1F) == 1) {
        if (!is_right_shift) {
          this->regs.replace_flag(X86Registers::OF, !!(value & msb_for_type<T>) != cf);
        } else {
          if (is_signed) {
            this->regs.replace_flag(X86Registers::OF, false);
          } else {
            this->regs.replace_flag(X86Registers::OF, !!(value & msb_for_type<T>));
          }
        }
      }
      break;
    }
    default:
      throw logic_error("non_ea_reg is not valid");
  }
}

static const char* const bit_shift_opcode_names[8] = {
    "rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar"};

void X86Emulator::exec_C0_C1_bit_shifts(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  uint8_t distance = this->fetch_instruction_byte();

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->exec_bit_shifts_inner(rm.non_ea_reg, this->resolve_ea_w16(rm), distance);
    } else {
      this->exec_bit_shifts_inner(rm.non_ea_reg, this->resolve_ea_w32(rm), distance);
    }
  } else {
    this->exec_bit_shifts_inner(rm.non_ea_reg, this->resolve_ea_w8(rm), distance);
  }
}

string X86Emulator::dasm_C0_C1_bit_shifts(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  uint8_t distance = s.r.get_u8();
  return extend(bit_shift_opcode_names[rm.non_ea_reg], 10)
      + rm.ea_str(s.standard_operand_size())
      + string_printf(", %02" PRIX8, distance);
}

void X86Emulator::exec_C2_C3_ret(uint8_t opcode) {
  uint32_t new_eip = this->pop<le_uint32_t>();
  if (!(opcode & 1)) {
    // TODO: Is this signed? It wouldn't make sense for it to be, but......
    this->regs.esp().u += this->fetch_instruction_word();
  }
  this->regs.eip = new_eip;
}

string X86Emulator::dasm_C2_C3_ret(DisassemblyState& s) {
  if (s.opcode & 1) {
    return "ret";
  } else {
    return string_printf("ret       %04" PRIX16, s.r.get_u16l());
  }
}

void X86Emulator::exec_C6_C7_mov_rm_imm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  if (rm.non_ea_reg != 0) {
    throw runtime_error("invalid mov r/m, imm with non_ea_reg != 0");
  }

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->resolve_ea_w16(rm) = this->fetch_instruction_word();
    } else {
      this->resolve_ea_w32(rm) = this->fetch_instruction_dword();
    }
  } else {
    this->resolve_ea_w8(rm) = this->fetch_instruction_byte();
  }
}

string X86Emulator::dasm_C6_C7_mov_rm_imm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  if (rm.non_ea_reg != 0) {
    return ".invalid  <<mov r/m, imm with non_ea_reg != 0>>";
  }

  uint8_t operand_size = s.standard_operand_size();
  return "mov       " + rm.ea_str(operand_size)
      + string_printf(", %" PRIX32, get_operand(s.r, operand_size));
}

void X86Emulator::exec_D0_to_D3_bit_shifts(uint8_t opcode) {
  uint8_t distance = (opcode & 2) ? this->regs.ecx().u8.l : 1;
  auto rm = this->fetch_and_decode_rm();

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->exec_bit_shifts_inner(rm.non_ea_reg, this->resolve_ea_w16(rm), distance);
    } else {
      this->exec_bit_shifts_inner(rm.non_ea_reg, this->resolve_ea_w32(rm), distance);
    }
  } else {
    this->exec_bit_shifts_inner(rm.non_ea_reg, this->resolve_ea_w8(rm), distance);
  }
}

string X86Emulator::dasm_D0_to_D3_bit_shifts(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return extend(bit_shift_opcode_names[rm.non_ea_reg], 10)
      + rm.ea_str(s.standard_operand_size())
      + ((s.opcode & 2) ? ", cl" : ", 1");
}

void X86Emulator::exec_E8_E9_call_jmp(uint8_t opcode) {
  uint32_t offset = this->overrides.operand_size
      ? sign_extend<uint32_t, uint16_t>(this->fetch_instruction_word())
      : this->fetch_instruction_dword();

  if (!(opcode & 1)) {
    this->push<le_uint32_t>(this->regs.eip);
  }
  this->regs.eip += offset;
}

string X86Emulator::dasm_E8_E9_call_jmp(DisassemblyState& s) {
  uint32_t offset = s.overrides.operand_size
      ? sign_extend<uint32_t, uint16_t>(s.r.get_u16l())
      : s.r.get_u32l();

  const char* opcode_name = (s.opcode & 1) ? "jmp " : "call";
  uint32_t dest = s.start_address + s.r.where() + offset;
  s.branch_target_addresses.emplace(dest, !(s.opcode & 1));
  return string_printf("%s      %08" PRIX32, opcode_name, dest);
}

void X86Emulator::exec_F2_F3_repz_repnz(uint8_t opcode) {
  this->overrides.should_clear = false;
  this->overrides.repeat_z = (opcode & 1);
  this->overrides.repeat_nz = !this->overrides.repeat_z;
}

string X86Emulator::dasm_F2_F3_repz_repnz(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.repeat_z = (s.opcode & 1);
  s.overrides.repeat_nz = !s.overrides.repeat_z;
  return "";
}

void X86Emulator::exec_F5_cmc(uint8_t) {
  this->regs.replace_flag(X86Registers::CF, !this->regs.flag(X86Registers::CF));
}

string X86Emulator::dasm_F5_cmc(DisassemblyState&) {
  return "cmc";
}

template <typename T>
void X86Emulator::exec_F6_F7_misc_math_inner(uint8_t what, T& value) {
  switch (what) {
    case 0: // test
    case 1: { // test (TODO: is this actually identical to case 0?)
      T imm = this->fetch_instruction_data<T>();
      this->regs.set_flags_bitwise_result<T>(value & imm);
      break;
    }
    case 2: // not
      // Note: Unlike all the other opcodes here, this one doesn't set any flags
      value = ~value;
      break;
    case 3: // neg
      // TODO: What is the correct way to set flags here? We assume that this
      // opcode is equivalent to `sub 0, value`. The manual describes a special
      // treatment for CF, which should be equivalent to just letting
      // set_flags_integer_subtract do its thing, but we implement it anyway.
      // Is this logic correct?
      value = this->regs.set_flags_integer_subtract<T>(0, value, ~X86Registers::CF);
      this->regs.replace_flag(X86Registers::CF, (value != 0));
      break;
    case 4: { // mul (to edx:eax)
      bool of_cf = false;
      if (bits_for_type<T> == 8) {
        this->regs.eax().u16 = this->regs.eax().u8.l * value;
        of_cf = (this->regs.eax().u8.h != 0);
      } else if (bits_for_type<T> == 16) {
        uint32_t result = this->regs.eax().u16 * value;
        this->regs.edx().u16 = result >> 16;
        this->regs.eax().u16 = result;
        of_cf = (this->regs.edx().u16 != 0);
      } else if (bits_for_type<T> == 32) {
        uint64_t result = static_cast<uint64_t>(this->regs.eax().u) * value;
        this->regs.edx().u = result >> 32;
        this->regs.eax().u = result;
        of_cf = (this->regs.edx().u != 0);
      } else {
        throw logic_error("invalid operand size");
      }
      this->regs.replace_flag(X86Registers::OF, of_cf);
      this->regs.replace_flag(X86Registers::CF, of_cf);
      break;
    }
    case 5: { // imul (to edx:eax)
      bool of_cf = false;
      if (bits_for_type<T> == 8) {
        this->regs.eax().s16 = this->regs.eax().s8.l * static_cast<int8_t>(value);
        of_cf = (this->regs.eax().u16 != sign_extend<uint16_t, uint8_t>(this->regs.eax().u8.l));
      } else if (bits_for_type<T> == 16) {
        int32_t result = this->regs.eax().s16 * static_cast<int16_t>(value);
        this->regs.edx().s16 = result >> 16;
        this->regs.eax().s16 = result;
        of_cf = (result != sign_extend<int32_t, uint16_t>(this->regs.eax().u16));
      } else if (bits_for_type<T> == 32) {
        int64_t result = static_cast<int64_t>(this->regs.eax().s) * static_cast<int32_t>(value);
        this->regs.edx().s = result >> 32;
        this->regs.eax().s = result;
        of_cf = (result != sign_extend<int64_t, uint32_t>(this->regs.eax().u));
      } else {
        throw logic_error("invalid operand size");
      }
      // NOTE: The other forms of imul may have different treatments for these
      // flags! Don't copy this implementation blindly.
      this->regs.replace_flag(X86Registers::OF, of_cf);
      this->regs.replace_flag(X86Registers::CF, of_cf);
      break;
    }
    case 6: // div (to edx:eax)
      if (value == 0) {
        throw runtime_error("division by zero");
      }
      if (bits_for_type<T> == 8) {
        uint16_t dividend = this->regs.eax().u16;
        uint16_t quotient = dividend / value;
        if (quotient > 0xFF) {
          throw runtime_error("quotient too large");
        }
        this->regs.eax().u8.l = quotient;
        this->regs.eax().u8.h = dividend % value;
      } else if (bits_for_type<T> == 16) {
        uint32_t dividend = (this->regs.edx().u16 << 16) | this->regs.eax().u16;
        uint32_t quotient = dividend / value;
        if (quotient > 0xFFFF) {
          throw runtime_error("quotient too large");
        }
        this->regs.eax().u16 = quotient;
        this->regs.edx().u16 = dividend % value;
      } else if (bits_for_type<T> == 32) {
        uint64_t dividend = (static_cast<uint64_t>(this->regs.edx().u) << 32) | this->regs.eax().u;
        uint64_t quotient = dividend / value;
        if (quotient > 0xFFFFFFFF) {
          throw runtime_error("quotient too large");
        }
        this->regs.eax().u = quotient;
        this->regs.edx().u = dividend % value;
      } else {
        throw logic_error("invalid operand size");
      }
      break;
    case 7: // idiv (to edx:eax)
      if (value == 0) {
        throw runtime_error("division by zero");
      }
      if (bits_for_type<T> == 8) {
        int16_t dividend = this->regs.eax().s16;
        int16_t quotient = dividend / static_cast<int8_t>(value);
        if (quotient < -0x80 || quotient > 0x7F) {
          throw runtime_error("quotient too large");
        }
        this->regs.eax().s8.l = quotient;
        this->regs.eax().s8.h = dividend % static_cast<int8_t>(value);
      } else if (bits_for_type<T> == 16) {
        int32_t dividend = (this->regs.edx().s16 << 16) | this->regs.eax().s16;
        int32_t quotient = dividend / static_cast<int16_t>(value);
        if (quotient < -0x8000 || quotient > 0x7FFF) {
          throw runtime_error("quotient too large");
        }
        this->regs.eax().s16 = quotient;
        this->regs.edx().s16 = dividend % static_cast<int16_t>(value);
      } else if (bits_for_type<T> == 32) {
        int64_t dividend = (static_cast<int64_t>(this->regs.edx().s) << 32) | this->regs.eax().s;
        int64_t quotient = dividend / static_cast<int32_t>(value);
        if (quotient < static_cast<int64_t>(-0x80000000) || quotient > static_cast<int64_t>(0x7FFFFFFF)) {
          throw runtime_error("quotient too large");
        }
        this->regs.eax().s = quotient;
        this->regs.edx().s = dividend % static_cast<int32_t>(value);
      } else {
        throw logic_error("invalid operand size");
      }
      break;
    default:
      throw logic_error("invalid misc math operation");
  }
}

void X86Emulator::exec_F6_F7_misc_math(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  // TODO: This is not always a write. Refactor the resolve calls appropriately.
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->exec_F6_F7_misc_math_inner(rm.non_ea_reg, this->resolve_ea_w16(rm));
    } else {
      this->exec_F6_F7_misc_math_inner(rm.non_ea_reg, this->resolve_ea_w32(rm));
    }
  } else {
    this->exec_F6_F7_misc_math_inner(rm.non_ea_reg, this->resolve_ea_w8(rm));
  }
}

string X86Emulator::dasm_F6_F7_misc_math(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  uint8_t operand_size = s.standard_operand_size();
  if (rm.non_ea_reg < 2) {
    return "test      " + rm.ea_str(operand_size) + string_printf(", %02" PRIX32, get_operand(s.r, operand_size));
  } else {
    const char* const opcode_names[8] = {
        "test", "test", "not", "neg", "mul", "imul", "div", "idiv"};
    string name = extend(opcode_names[rm.non_ea_reg], 10);
    return name + rm.ea_str(operand_size);
  }
}

void X86Emulator::exec_F8_clc(uint8_t) {
  this->regs.replace_flag(X86Registers::CF, false);
}

string X86Emulator::dasm_F8_clc(DisassemblyState&) {
  return "clc";
}

void X86Emulator::exec_F9_stc(uint8_t) {
  this->regs.replace_flag(X86Registers::CF, true);
}

string X86Emulator::dasm_F9_stc(DisassemblyState&) {
  return "stc";
}

void X86Emulator::exec_FA_cli(uint8_t) {
  this->regs.replace_flag(X86Registers::IF, false);
}

string X86Emulator::dasm_FA_cli(DisassemblyState&) {
  return "cli";
}

void X86Emulator::exec_FB_sti(uint8_t) {
  this->regs.replace_flag(X86Registers::IF, true);
}

string X86Emulator::dasm_FB_sti(DisassemblyState&) {
  return "sti";
}

void X86Emulator::exec_FC_cld(uint8_t) {
  this->regs.replace_flag(X86Registers::DF, false);
}

string X86Emulator::dasm_FC_cld(DisassemblyState&) {
  return "cld";
}

void X86Emulator::exec_FD_std(uint8_t) {
  this->regs.replace_flag(X86Registers::DF, true);
}

string X86Emulator::dasm_FD_std(DisassemblyState&) {
  return "std";
}

void X86Emulator::exec_FE_FF_inc_dec_misc(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  if (opcode & 1) {
    switch (rm.non_ea_reg) {
      case 0: // inc
        if (this->overrides.operand_size) {
          le_uint16_t& v = this->resolve_ea_w16(rm);
          v = this->regs.set_flags_integer_add<uint16_t>(v, 1, ~X86Registers::CF);
        } else {
          le_uint32_t& v = this->resolve_ea_w32(rm);
          v = this->regs.set_flags_integer_add<uint32_t>(v, 1, ~X86Registers::CF);
        }
        break;
      case 1: // dec
        if (this->overrides.operand_size) {
          le_uint16_t& v = this->resolve_ea_w16(rm);
          v = this->regs.set_flags_integer_subtract<uint16_t>(v, 1, ~X86Registers::CF);
        } else {
          le_uint32_t& v = this->resolve_ea_w32(rm);
          v = this->regs.set_flags_integer_subtract<uint32_t>(v, 1, ~X86Registers::CF);
        }
        break;
      case 2: // call
        this->push<le_uint32_t>(this->regs.eip);
        [[fallthrough]];
      case 4: // jmp
        this->regs.eip = this->overrides.operand_size
            ? sign_extend<uint32_t, uint16_t>(this->resolve_ea_r16(rm))
            : this->resolve_ea_r32(rm).load();
        break;
      case 3: // call (far)
      case 5: // jmp (far)
        throw runtime_error("far call/jmp is not implemented");
      case 6: // push
        if (this->overrides.operand_size) {
          this->push<le_uint16_t>(this->resolve_ea_r16(rm));
        } else {
          this->push<le_uint32_t>(this->resolve_ea_r32(rm));
        }
        break;
      case 7:
        throw runtime_error("invalid opcode");
      default:
        throw logic_error("invalid misc operation");
    }
  } else {
    if (rm.non_ea_reg > 1) {
      throw runtime_error("invalid opcode");
    }
    uint8_t& v = this->resolve_ea_w8(rm);
    if (!(rm.non_ea_reg & 1)) {
      v = this->regs.set_flags_integer_add<uint8_t>(v, 1, ~X86Registers::CF);
    } else {
      v = this->regs.set_flags_integer_subtract<uint8_t>(v, 1, ~X86Registers::CF);
    }
  }
}

string X86Emulator::dasm_FE_FF_inc_dec_misc(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  uint8_t operand_size = s.standard_operand_size();
  if (rm.non_ea_reg < 2) {
    return (rm.non_ea_reg ? "dec       " : "inc       ") + rm.ea_str(operand_size);
  }

  if (!(s.opcode & 1)) {
    return ".invalid  <<inc/dec/misc>>";
  }

  switch (rm.non_ea_reg) {
    case 2: // call
    case 4: // jmp
      return ((s.opcode == 2) ? "call      " : "jmp       ") + rm.ea_str(operand_size);
    case 3: // call (far)
    case 5: // jmp (far)
      return ".unknown  <<far call/jmp>> // unimplemented";
    case 6: // push
      return "push      " + rm.ea_str(operand_size);
    case 7:
      return ".invalid  <<misc/7>>";
    default:
      throw logic_error("invalid misc operation");
  }
}

void X86Emulator::exec_0F_31_rdtsc(uint8_t) {
  this->regs.edx().u = this->instructions_executed >> 32;
  this->regs.eax().u = this->instructions_executed;
}

string X86Emulator::dasm_0F_31_rdtsc(DisassemblyState&) {
  return "rdtsc";
}

void X86Emulator::exec_0F_40_to_4F_cmov_rm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  if (this->regs.check_condition(opcode & 0x0F)) {
    if (this->overrides.operand_size) {
      this->resolve_non_ea_w16(rm) = this->resolve_ea_r16(rm);
    } else {
      this->resolve_non_ea_w32(rm) = this->resolve_ea_r32(rm);
    }
  }
}

string X86Emulator::dasm_0F_40_to_4F_cmov_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  string opcode_name = "cmov";
  opcode_name += name_for_condition_code[s.opcode & 0x0F];
  opcode_name.resize(10, ' ');
  return opcode_name + rm.str(s.overrides.operand_size ? 16 : 32, false);
}

void X86Emulator::exec_0F_80_to_8F_jcc(uint8_t opcode) {
  // Always read the offset even if the condition is false, so we don't try to
  // execute the offset as code immediately after.
  uint32_t offset = this->overrides.operand_size
      ? sign_extend<uint32_t, uint16_t>(this->fetch_instruction_word())
      : this->fetch_instruction_dword();
  if (this->regs.check_condition(opcode & 0x0F)) {
    this->regs.eip += offset;
  }
}

string X86Emulator::dasm_0F_80_to_8F_jcc(DisassemblyState& s) {
  string opcode_name = "j";
  opcode_name += name_for_condition_code[s.opcode & 0x0F];
  opcode_name.resize(10, ' ');

  uint32_t offset = s.overrides.operand_size
      ? sign_extend<uint32_t, uint16_t>(s.r.get_u16l())
      : s.r.get_u32l();

  uint32_t dest = s.start_address + s.r.where() + offset;
  s.branch_target_addresses.emplace(dest, false);
  return opcode_name + string_printf("%08" PRIX32, dest);
}

void X86Emulator::exec_0F_90_to_9F_setcc_rm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  if (rm.non_ea_reg != 0) {
    throw runtime_error("invalid setcc with non_ea_reg != 0");
  }
  this->resolve_ea_w8(rm) = this->regs.check_condition(opcode & 0x0F) ? 1 : 0;
}

string X86Emulator::dasm_0F_90_to_9F_setcc_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  if (rm.non_ea_reg != 0) {
    return ".invalid  <<setcc with non_ea_reg != 0>>";
  }
  string opcode_name = "set";
  opcode_name += name_for_condition_code[s.opcode & 0x0F];
  opcode_name.resize(10, ' ');
  return opcode_name + rm.ea_str(8);
}

template <typename T>
void X86Emulator::exec_shld_shrd_inner(
    bool is_right_shift, T& dest_value, T incoming_value, uint8_t distance) {
  if ((distance & 0x1F) == 0) {
    return;
  }
  T orig_sign = dest_value & msb_for_type<T>;
  bool cf = this->regs.flag(X86Registers::CF);
  for (uint8_t c = distance & 0x1F; c; c--) {
    if (!is_right_shift) {
      cf = !!(dest_value & msb_for_type<T>);
      dest_value = (dest_value << 1) | ((incoming_value & msb_for_type<T>) ? 1 : 0);
      incoming_value <<= 1;
    } else {
      cf = dest_value & 1;
      dest_value = (dest_value >> 1) | ((incoming_value & 1) ? msb_for_type<T> : static_cast<T>(0));
      incoming_value >>= 1;
    }
  }
  this->regs.replace_flag(X86Registers::CF, cf);
  this->regs.replace_flag(X86Registers::OF, (orig_sign == (dest_value & msb_for_type<T>)));
}

void X86Emulator::exec_0F_A4_A5_AC_AD_shld_shrd(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  uint8_t distance = (opcode & 1)
      ? this->regs.ecx().u8.l : this->fetch_instruction_byte();

  if (this->overrides.operand_size) {
    this->exec_shld_shrd_inner<le_uint16_t>(opcode & 8, this->resolve_ea_w16(rm),
        this->resolve_non_ea_r16(rm), distance);
  } else {
    this->exec_shld_shrd_inner<le_uint32_t>(opcode & 8, this->resolve_ea_w32(rm),
        this->resolve_non_ea_r32(rm), distance);
  }
}

string X86Emulator::dasm_0F_A4_A5_AC_AD_shld_shrd(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  string opcode_name = extend((s.opcode & 8) ? "shrd" : "shld", 10);
  string distance_str = (s.opcode & 1) ? ", cl" : string_printf(", %02" PRIX8, s.r.get_u8());
  return opcode_name + rm.str(s.overrides.operand_size ? 16 : 32, true) + distance_str;
}

template <typename T>
void X86Emulator::exec_bit_test_ops(uint8_t what, T& v, uint8_t bit_number) {
  uint32_t mask = (1 << bit_number);
  this->regs.replace_flag(X86Registers::CF, v & mask);
  switch (what) {
    case 0: // bt (bit test)
      // Nothing to do (we already tested it above)
      break;
    case 1: // bts (bit test and set)
      v |= mask;
      break;
    case 2: // btr (bit test and reset)
      v &= ~mask;
      break;
    case 3: // btc (bit test and complement)
      v ^= mask;
      break;
    default:
      throw logic_error("invalid bit test operation");
  }
}

static const char* const bit_test_opcode_names[4] = {"bt", "bts", "btr", "btc"};

void X86Emulator::exec_0F_A3_AB_B3_BB_bit_tests(uint8_t opcode) {
  DecodedRM rm = this->fetch_and_decode_rm();
  uint8_t what = (opcode >> 3) & 3;

  // TODO: This is not always a write. Refactor the resolve calls appropriately.
  if (rm.ea_index_scale < 0) { // Bit field is in register
    // Note: We don't use resolve_non_ea_8 here because the register assignments
    // are different for registers 4-7, and this opcode actually does use
    // dil/sil (for example) if those are specified.
    if (this->overrides.operand_size) {
      uint8_t bit_number = this->resolve_non_ea_r16(rm) & 0x0F;
      this->exec_bit_test_ops<le_uint16_t>(what, this->resolve_ea_w16(rm), bit_number);
    } else {
      uint8_t bit_number = this->resolve_non_ea_r16(rm) & 0x1F;
      this->exec_bit_test_ops<le_uint32_t>(what, this->resolve_ea_w32(rm), bit_number);
    }

  } else {
    uint32_t bit_number = this->overrides.operand_size
        ? this->resolve_non_ea_r16(rm) : this->resolve_non_ea_r32(rm);
    uint32_t addr = this->resolve_mem_ea(rm) + (bit_number >> 3);
    this->report_mem_access(addr, bits_for_type<uint8_t>, false);
    this->exec_bit_test_ops<uint8_t>(what, *this->mem->at<uint8_t>(addr),
        (bit_number & 7));
  }
}

string X86Emulator::dasm_0F_A3_AB_B3_BB_bit_tests(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  string opcode_name = extend(bit_test_opcode_names[(s.opcode >> 3) & 3], 10);
  return opcode_name + rm.str(s.overrides.operand_size ? 16 : 32, true);
}

void X86Emulator::exec_0F_B6_B7_BE_BF_movzx_movsx(uint8_t opcode) {
  DecodedRM rm = this->fetch_and_decode_rm();

  uint32_t v = (opcode & 1) ? this->resolve_ea_r16(rm).load() : this->resolve_ea_r8(rm);
  if (opcode & 8) { // movsx
    v = (opcode & 1)
        ? sign_extend<uint32_t, uint16_t>(v)
        : sign_extend<uint32_t, uint8_t>(v);
  } else { // movzx
    v &= (opcode & 1) ? 0x0000FFFF : 0x000000FF;
  }

  if (this->overrides.operand_size) {
    this->resolve_non_ea_w16(rm) = v;
  } else {
    this->resolve_non_ea_w32(rm) = v;
  }
}

string X86Emulator::dasm_0F_B6_B7_BE_BF_movzx_movsx(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  string opcode_name = (s.opcode & 8) ? "movsx     " : "movzx     ";
  return opcode_name + rm.str((s.opcode & 1) ? 16 : 8, s.overrides.operand_size ? 16 : 32, false);
}

void X86Emulator::exec_0F_BA_bit_tests(uint8_t) {
  DecodedRM rm = this->fetch_and_decode_rm();
  if (!(rm.non_ea_reg & 4)) {
    throw runtime_error("invalid opcode 0F BA");
  }
  uint8_t what = rm.non_ea_reg & 3;
  uint8_t bit_number = this->fetch_instruction_byte();

  if (rm.ea_index_scale < 0) { // Bit field is in register
    // TODO: Docs seems to say that the mask is 7 (not 0x0F) for a 16-bit
    // operand, but that seems... wrong. Verify the correct behavior.
    if (this->overrides.operand_size) {
      this->exec_bit_test_ops<le_uint16_t>(what, this->resolve_ea_w16(rm),
          bit_number & 0x0F);
    } else {
      this->exec_bit_test_ops<le_uint32_t>(what, this->resolve_ea_w32(rm),
          bit_number & 0x1F);
    }

  } else {
    // TODO: Should we AND bit_number with something here? What's the effective
    // operand size when accessing memory with these opcodes?
    uint32_t addr = this->resolve_mem_ea(rm) + (bit_number >> 3);
    this->report_mem_access(addr, bits_for_type<uint8_t>, false);
    this->exec_bit_test_ops<uint8_t>(what, *this->mem->at<uint8_t>(addr),
        (bit_number & 7));
  }
}

string X86Emulator::dasm_0F_BA_bit_tests(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  if (!(rm.non_ea_reg & 4)) {
    return ".invalid  <<bit test with subopcode 0-3>>";
  }
  uint8_t bit_number = s.r.get_u8();
  string opcode_name = extend(bit_test_opcode_names[rm.non_ea_reg & 3], 10);
  return opcode_name + rm.ea_str(s.overrides.operand_size ? 16 : 32) + string_printf(", %02" PRIX8, bit_number);
}

void X86Emulator::exec_0F_BC_BD_bsf_bsr(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  uint32_t value = this->overrides.operand_size
      ? this->resolve_ea_r16(rm).load() : this->resolve_ea_r32(rm).load();

  if (value == 0) {
    this->regs.replace_flag(X86Registers::ZF, true);
  } else {
    this->regs.replace_flag(X86Registers::ZF, false);

    uint32_t result;
    if (opcode & 1) { // bsr
      result = 31;
      for (; !(value & 0x80000000); result--, value <<= 1) { }
    } else { // bsf
      result = 0;
      for (; !(value & 1); result++, value >>= 1) { }
    }

    if (this->overrides.operand_size) {
      this->resolve_non_ea_w16(rm) = result;
    } else {
      this->resolve_non_ea_w32(rm) = result;
    }
  }
}

string X86Emulator::dasm_0F_BC_BD_bsf_bsr(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return ((s.opcode & 1) ? "bsr       " : "bsf       ") + rm.str(s.overrides.operand_size ? 16 : 32, false);
}

void X86Emulator::exec_0F_C0_C1_xadd_rm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      le_uint16_t& a = this->resolve_non_ea_w16(rm);
      le_uint16_t& b = this->resolve_ea_w16(rm);
      uint16_t t = a;
      a = b;
      b = this->regs.set_flags_integer_add<uint16_t>(t, b);
    } else {
      le_uint32_t& a = this->resolve_non_ea_w32(rm);
      le_uint32_t& b = this->resolve_ea_w32(rm);
      uint32_t t = a;
      a = b;
      b = this->regs.set_flags_integer_add<uint32_t>(t, b);
    }
  } else {
    uint8_t& a = this->resolve_non_ea_w8(rm);
    uint8_t& b = this->resolve_ea_w8(rm);
    uint8_t t = a;
    a = b;
    b = this->regs.set_flags_integer_add<uint8_t>(t, b);
  }
}

string X86Emulator::dasm_0F_C0_C1_xadd_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return "xadd      " + rm.str(s.standard_operand_size(), true);
}

void X86Emulator::exec_0F_C8_to_CF_bswap(uint8_t opcode) {
  auto& reg = this->regs.regs[opcode & 7];
  if (this->overrides.operand_size) {
    reg.u16 = bswap16(reg.u16);
  } else {
    reg.u = bswap32(reg.u);
  }
}

string X86Emulator::dasm_0F_C8_to_CF_bswap(DisassemblyState& s) {
  return string_printf("bswap     %s",
      name_for_reg(s.opcode & 7, s.overrides.operand_size ? 16 : 32));
}

void X86Emulator::exec_unimplemented(uint8_t opcode) {
  throw runtime_error(string_printf("unimplemented opcode: %02hhX", opcode));
}

string X86Emulator::dasm_unimplemented(DisassemblyState& s) {
  return string_printf(".unknown  %02" PRIX8, s.opcode);
}

void X86Emulator::exec_0F_unimplemented(uint8_t opcode) {
  throw runtime_error(string_printf("unimplemented opcode: 0F %02hhX", opcode));
}

string X86Emulator::dasm_0F_unimplemented(DisassemblyState& s) {
  return string_printf(".unknown  0F%02" PRIX8, s.opcode);
}

X86Emulator::X86Emulator(shared_ptr<MemoryContext> mem)
  : EmulatorBase(mem), audit(false), current_audit_result(nullptr) { }

const X86Emulator::OpcodeImplementation X86Emulator::fns[0x100] = {
  // 00
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {},
  {},
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {},
  {&X86Emulator::exec_0F_extensions, &X86Emulator::dasm_0F_extensions},
  // 10
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {},
  {},
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {},
  {},
  // 20
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {&X86Emulator::exec_26_es, &X86Emulator::dasm_26_es},
  {},
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {&X86Emulator::exec_2E_cs, &X86Emulator::dasm_2E_cs},
  {},
  // 30
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {&X86Emulator::exec_36_ss, &X86Emulator::dasm_36_ss},
  {},
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
  {&X86Emulator::exec_3E_ds, &X86Emulator::dasm_3E_ds},
  {},
  // 40
  {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
  {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
  // 50
  {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
  {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
  // 60
  {},
  {},
  {},
  {},
  {&X86Emulator::exec_64_fs, &X86Emulator::dasm_64_fs},
  {&X86Emulator::exec_65_gs, &X86Emulator::dasm_65_gs},
  {&X86Emulator::exec_66_operand_size, &X86Emulator::dasm_66_operand_size},
  {},
  {&X86Emulator::exec_68_push, &X86Emulator::dasm_68_push},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  // 70
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  // 80
  {&X86Emulator::exec_80_to_83_imm_math, &X86Emulator::dasm_80_to_83_imm_math},
  {&X86Emulator::exec_80_to_83_imm_math, &X86Emulator::dasm_80_to_83_imm_math},
  {&X86Emulator::exec_80_to_83_imm_math, &X86Emulator::dasm_80_to_83_imm_math},
  {&X86Emulator::exec_80_to_83_imm_math, &X86Emulator::dasm_80_to_83_imm_math},
  {&X86Emulator::exec_84_85_test_rm, &X86Emulator::dasm_84_85_test_rm},
  {&X86Emulator::exec_84_85_test_rm, &X86Emulator::dasm_84_85_test_rm},
  {&X86Emulator::exec_86_87_xchg_rm, &X86Emulator::dasm_86_87_xchg_rm},
  {&X86Emulator::exec_86_87_xchg_rm, &X86Emulator::dasm_86_87_xchg_rm},
  {&X86Emulator::exec_88_to_8B_mov_rm, &X86Emulator::dasm_88_to_8B_mov_rm},
  {&X86Emulator::exec_88_to_8B_mov_rm, &X86Emulator::dasm_88_to_8B_mov_rm},
  {&X86Emulator::exec_88_to_8B_mov_rm, &X86Emulator::dasm_88_to_8B_mov_rm},
  {&X86Emulator::exec_88_to_8B_mov_rm, &X86Emulator::dasm_88_to_8B_mov_rm},
  {},
  {&X86Emulator::exec_8D_lea, &X86Emulator::dasm_8D_lea},
  {},
  {&X86Emulator::exec_8F_pop_rm, &X86Emulator::dasm_8F_pop_rm},
  // 90
  {&X86Emulator::exec_90_to_97_xchg, &X86Emulator::dasm_90_to_97_xchg},
  {&X86Emulator::exec_90_to_97_xchg, &X86Emulator::dasm_90_to_97_xchg},
  {&X86Emulator::exec_90_to_97_xchg, &X86Emulator::dasm_90_to_97_xchg},
  {&X86Emulator::exec_90_to_97_xchg, &X86Emulator::dasm_90_to_97_xchg},
  {&X86Emulator::exec_90_to_97_xchg, &X86Emulator::dasm_90_to_97_xchg},
  {&X86Emulator::exec_90_to_97_xchg, &X86Emulator::dasm_90_to_97_xchg},
  {&X86Emulator::exec_90_to_97_xchg, &X86Emulator::dasm_90_to_97_xchg},
  {&X86Emulator::exec_90_to_97_xchg, &X86Emulator::dasm_90_to_97_xchg},
  {&X86Emulator::exec_98_cbw_cwde, &X86Emulator::dasm_98_cbw_cwde},
  {&X86Emulator::exec_99_cwd_cdq, &X86Emulator::dasm_99_cwd_cdq},
  {},
  {},
  {&X86Emulator::exec_9C_pushf_pushfd, &X86Emulator::dasm_9C_pushf_pushfd},
  {&X86Emulator::exec_9D_popf_popfd, &X86Emulator::dasm_9D_popf_popfd},
  {},
  {&X86Emulator::exec_9F_lahf, &X86Emulator::dasm_9F_lahf},
  // A0
  {},
  {},
  {},
  {},
  {&X86Emulator::exec_A4_A5_movs, &X86Emulator::dasm_A4_A5_movs},
  {&X86Emulator::exec_A4_A5_movs, &X86Emulator::dasm_A4_A5_movs},
  {},
  {},
  {&X86Emulator::exec_A8_A9_test_eax_imm, &X86Emulator::dasm_A8_A9_test_eax_imm},
  {&X86Emulator::exec_A8_A9_test_eax_imm, &X86Emulator::dasm_A8_A9_test_eax_imm},
  {},
  {},
  {},
  {},
  {},
  {},
  // B0
  {&X86Emulator::exec_B0_to_B7_mov_imm_8, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_B7_mov_imm_8, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_B7_mov_imm_8, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_B7_mov_imm_8, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_B7_mov_imm_8, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_B7_mov_imm_8, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_B7_mov_imm_8, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_B7_mov_imm_8, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B8_to_BF_mov_imm_16_32, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B8_to_BF_mov_imm_16_32, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B8_to_BF_mov_imm_16_32, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B8_to_BF_mov_imm_16_32, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B8_to_BF_mov_imm_16_32, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B8_to_BF_mov_imm_16_32, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B8_to_BF_mov_imm_16_32, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B8_to_BF_mov_imm_16_32, &X86Emulator::dasm_B0_to_BF_mov_imm},
  // C0
  {&X86Emulator::exec_C0_C1_bit_shifts, &X86Emulator::dasm_C0_C1_bit_shifts},
  {&X86Emulator::exec_C0_C1_bit_shifts, &X86Emulator::dasm_C0_C1_bit_shifts},
  {&X86Emulator::exec_C2_C3_ret, &X86Emulator::dasm_C2_C3_ret},
  {&X86Emulator::exec_C2_C3_ret, &X86Emulator::dasm_C2_C3_ret},
  {},
  {},
  {&X86Emulator::exec_C6_C7_mov_rm_imm, &X86Emulator::dasm_C6_C7_mov_rm_imm},
  {&X86Emulator::exec_C6_C7_mov_rm_imm, &X86Emulator::dasm_C6_C7_mov_rm_imm},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  // D0
  {&X86Emulator::exec_D0_to_D3_bit_shifts, &X86Emulator::dasm_D0_to_D3_bit_shifts},
  {&X86Emulator::exec_D0_to_D3_bit_shifts, &X86Emulator::dasm_D0_to_D3_bit_shifts},
  {&X86Emulator::exec_D0_to_D3_bit_shifts, &X86Emulator::dasm_D0_to_D3_bit_shifts},
  {&X86Emulator::exec_D0_to_D3_bit_shifts, &X86Emulator::dasm_D0_to_D3_bit_shifts},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  // E0
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {&X86Emulator::exec_E8_E9_call_jmp, &X86Emulator::dasm_E8_E9_call_jmp},
  {&X86Emulator::exec_E8_E9_call_jmp, &X86Emulator::dasm_E8_E9_call_jmp},
  {},
  {},
  {},
  {},
  {},
  {},
  // F0
  {},
  {},
  {&X86Emulator::exec_F2_F3_repz_repnz, &X86Emulator::dasm_F2_F3_repz_repnz},
  {&X86Emulator::exec_F2_F3_repz_repnz, &X86Emulator::dasm_F2_F3_repz_repnz},
  {},
  {&X86Emulator::exec_F5_cmc, &X86Emulator::dasm_F5_cmc},
  {&X86Emulator::exec_F6_F7_misc_math, &X86Emulator::dasm_F6_F7_misc_math},
  {&X86Emulator::exec_F6_F7_misc_math, &X86Emulator::dasm_F6_F7_misc_math},
  {&X86Emulator::exec_F8_clc, &X86Emulator::dasm_F8_clc},
  {&X86Emulator::exec_F9_stc, &X86Emulator::dasm_F9_stc},
  {&X86Emulator::exec_FA_cli, &X86Emulator::dasm_FA_cli},
  {&X86Emulator::exec_FB_sti, &X86Emulator::dasm_FB_sti},
  {&X86Emulator::exec_FC_cld, &X86Emulator::dasm_FC_cld},
  {&X86Emulator::exec_FD_std, &X86Emulator::dasm_FD_std},
  {&X86Emulator::exec_FE_FF_inc_dec_misc, &X86Emulator::dasm_FE_FF_inc_dec_misc},
  {&X86Emulator::exec_FE_FF_inc_dec_misc, &X86Emulator::dasm_FE_FF_inc_dec_misc},
};

const X86Emulator::OpcodeImplementation X86Emulator::fns_0F[0x100] = {
  // 00
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  // 10
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  // 20
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  // 30
  {},
  {&X86Emulator::exec_0F_31_rdtsc, &X86Emulator::dasm_0F_31_rdtsc},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  // 40
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
  // 50
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  // 60
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  // 70
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  // 80
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
  // 90
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
  // A0
  {},
  {},
  {},
  {&X86Emulator::exec_0F_A3_AB_B3_BB_bit_tests, &X86Emulator::dasm_0F_A3_AB_B3_BB_bit_tests},
  {&X86Emulator::exec_0F_A4_A5_AC_AD_shld_shrd, &X86Emulator::dasm_0F_A4_A5_AC_AD_shld_shrd},
  {&X86Emulator::exec_0F_A4_A5_AC_AD_shld_shrd, &X86Emulator::dasm_0F_A4_A5_AC_AD_shld_shrd},
  {},
  {},
  {},
  {},
  {},
  {&X86Emulator::exec_0F_A3_AB_B3_BB_bit_tests, &X86Emulator::dasm_0F_A3_AB_B3_BB_bit_tests},
  {&X86Emulator::exec_0F_A4_A5_AC_AD_shld_shrd, &X86Emulator::dasm_0F_A4_A5_AC_AD_shld_shrd},
  {&X86Emulator::exec_0F_A4_A5_AC_AD_shld_shrd, &X86Emulator::dasm_0F_A4_A5_AC_AD_shld_shrd},
  {},
  {},
  // B0
  {},
  {},
  {},
  {&X86Emulator::exec_0F_A3_AB_B3_BB_bit_tests, &X86Emulator::dasm_0F_A3_AB_B3_BB_bit_tests},
  {},
  {},
  {&X86Emulator::exec_0F_B6_B7_BE_BF_movzx_movsx, &X86Emulator::dasm_0F_B6_B7_BE_BF_movzx_movsx},
  {&X86Emulator::exec_0F_B6_B7_BE_BF_movzx_movsx, &X86Emulator::dasm_0F_B6_B7_BE_BF_movzx_movsx},
  {},
  {},
  {&X86Emulator::exec_0F_BA_bit_tests, &X86Emulator::dasm_0F_BA_bit_tests},
  {&X86Emulator::exec_0F_A3_AB_B3_BB_bit_tests, &X86Emulator::dasm_0F_A3_AB_B3_BB_bit_tests},
  {&X86Emulator::exec_0F_BC_BD_bsf_bsr, &X86Emulator::dasm_0F_BC_BD_bsf_bsr},
  {&X86Emulator::exec_0F_BC_BD_bsf_bsr, &X86Emulator::dasm_0F_BC_BD_bsf_bsr},
  {&X86Emulator::exec_0F_B6_B7_BE_BF_movzx_movsx, &X86Emulator::dasm_0F_B6_B7_BE_BF_movzx_movsx},
  {&X86Emulator::exec_0F_B6_B7_BE_BF_movzx_movsx, &X86Emulator::dasm_0F_B6_B7_BE_BF_movzx_movsx},
  // C0
  {&X86Emulator::exec_0F_C0_C1_xadd_rm, &X86Emulator::dasm_0F_C0_C1_xadd_rm},
  {&X86Emulator::exec_0F_C0_C1_xadd_rm, &X86Emulator::dasm_0F_C0_C1_xadd_rm},
  {},
  {},
  {},
  {},
  {},
  {},
  {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
  {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
  {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
  {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
  {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
  {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
  {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
  {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
  // D0
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  // E0
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  // F0
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
  {},
};



X86Emulator::Overrides::Overrides() noexcept
  : should_clear(true),
    segment(Segment::NONE),
    operand_size(false),
    address_size(false),
    wait(false),
    lock(false),
    repeat_nz(false),
    repeat_z(false) { }

std::string X86Emulator::Overrides::str() const {
  vector<const char*> tokens;
  const char* segment_name = this->overridden_segment_name();
  if (segment_name) {
    tokens.emplace_back(segment_name);
  }
  if (this->operand_size) {
    tokens.emplace_back("operand_size");
  }
  if (this->address_size) {
    tokens.emplace_back("address_size");
  }
  if (this->wait) {
    tokens.emplace_back("wait");
  }
  if (this->lock) {
    tokens.emplace_back("lock");
  }
  if (this->repeat_nz) {
    tokens.emplace_back("repeat_nz");
  }
  if (this->repeat_z) {
    tokens.emplace_back("repeat_z");
  }
  if (tokens.empty()) {
    return "(none)";
  } else {
    return "(" + join(tokens, ",") + ")";
  }
}

void X86Emulator::Overrides::on_opcode_complete() {
  if (!this->should_clear) {
    this->should_clear = true;
  } else {
    this->segment = Segment::NONE;
    this->operand_size = false;
    this->address_size = false;
    this->wait = false;
    this->lock = false;
    this->repeat_nz = false;
    this->repeat_z = false;
  }
}

const char* X86Emulator::Overrides::overridden_segment_name() const {
  if (this->segment == Segment::CS) {
    return "cs";
  } else if (this->segment == Segment::DS) {
    return "ds";
  } else if (this->segment == Segment::ES) {
    return "es";
  } else if (this->segment == Segment::FS) {
    return "fs";
  } else if (this->segment == Segment::GS) {
    return "gs";
  } else if (this->segment == Segment::SS) {
    return "ss";
  }
  return nullptr;
}


void X86Emulator::execute() {
  for (;;) {
    // Call debug hook if present
    if (this->debug_hook) {
      try {
        this->debug_hook(*this, this->regs);
      } catch (const terminate_emulation&) {
        break;
      }
    }

    // Execute a cycle
    uint8_t opcode = this->fetch_instruction_byte();
    auto fn = this->fns[opcode].exec;
    if (this->audit) {
      if (opcode == 0x0F) {
        this->current_audit_result = &this->audit_results[*this->mem->at<uint8_t>(this->regs.eip) + 0x100].emplace_back();
      } else {
        this->current_audit_result = &this->audit_results[opcode].emplace_back();
      }
      this->current_audit_result->cycle_num = this->instructions_executed;
      this->current_audit_result->regs_before = this->regs;
      // Correct for the opcode byte, which was already fetched
      this->current_audit_result->regs_before.eip--;
      this->current_audit_result->overrides = this->overrides;
    }
    if (fn) {
      (this->*fn)(opcode);
    } else {
      this->exec_unimplemented(opcode);
    }
    this->overrides.on_opcode_complete();

    if (this->current_audit_result) {
      this->current_audit_result->regs_after = this->regs;
      uint32_t addr = this->current_audit_result->regs_before.eip;
      try {
        while (this->current_audit_result->opcode.size() < 0x20) {
          this->current_audit_result->opcode += this->mem->read_s8(addr++);
        }
      } catch (const out_of_range&) { }

      DisassemblyState s = {
        StringReader(this->current_audit_result->opcode),
        this->current_audit_result->regs_before.eip,
        0,
        this->current_audit_result->overrides,
        {},
      };
      this->current_audit_result->disassembly = this->disassemble_one(s);
      this->current_audit_result = nullptr;
    }

    this->instructions_executed++;
  }
}



std::string X86Emulator::disassemble_one(DisassemblyState& s) {
  size_t start_offset = s.r.where();

  s.opcode = s.r.get_u8();
  auto dasm_fn = X86Emulator::fns[s.opcode].dasm;
  string dasm = dasm_fn ? dasm_fn(s) : X86Emulator::dasm_unimplemented(s);

  size_t num_bytes = s.r.where() - start_offset;
  string data_str = format_data_string(s.r.preadx(start_offset, num_bytes));
  data_str.resize(max<size_t>(data_str.size() + 3, 19), ' ');
  return data_str + dasm;
}

std::string X86Emulator::disassemble(
    const void* vdata,
    size_t size,
    uint32_t start_address,
    const std::multimap<uint32_t, std::string>* labels) {
  static const multimap<uint32_t, string> empty_labels_map = {};
  if (!labels) {
    labels = &empty_labels_map;
  }

  DisassemblyState s = {
    StringReader(vdata, size),
    start_address,
    0,
    Overrides(),
    {},
  };

  // Generate disassembly lines for each opcode
  map<uint32_t, pair<string, uint32_t>> lines; // {pc: (line, next_pc)}
  while (!s.r.eof()) {
    uint32_t pc = s.start_address + s.r.where();
    string line = string_printf("%08" PRIX32 " ", pc)
        + X86Emulator::disassemble_one(s) + "\n";
    uint32_t next_pc = s.start_address + s.r.where();
    lines.emplace(pc, make_pair(move(line), next_pc));
    s.overrides.on_opcode_complete();
  }

  // TODO: Implement backups like we do in M68KEmulator::disassemble

  // Generate output lines, including passed-in labels and branch target labels
  size_t ret_bytes = 0;
  deque<string> ret_lines;
  auto branch_target_it = s.branch_target_addresses.lower_bound(start_address);
  auto label_it = labels->lower_bound(start_address);

  for (auto line_it = lines.begin();
       line_it != lines.end();
       line_it = lines.find(line_it->second.second)) {
    uint32_t pc = line_it->first;
    string& line = line_it->second.first;

    // TODO: Deduplicate this functionality (label iteration + line assembly)
    // across the various emulator implementations
    for (; label_it != labels->end() && label_it->first <= pc; label_it++) {
      string label;
      if (label_it->first != pc) {
        label = string_printf("%s: // at %08" PRIX32 " (misaligned)\n",
              label_it->second.c_str(), label_it->first);
      } else {
        label = string_printf("%s:\n", label_it->second.c_str());
      }
      ret_bytes += label.size();
      ret_lines.emplace_back(move(label));
    }
    for (; (branch_target_it != s.branch_target_addresses.end()) &&
           (branch_target_it->first <= pc);
         branch_target_it++) {
      string label;
      const char* label_type = branch_target_it->second ? "fn" : "label";
      if (branch_target_it->first != pc) {
        label = string_printf("%s%08" PRIX32 ": // (misaligned)\n",
            label_type, branch_target_it->first);
      } else {
        label = string_printf("%s%08" PRIX32 ":\n",
            label_type, branch_target_it->first);
      }
      ret_bytes += label.size();
      ret_lines.emplace_back(move(label));
    }

    ret_bytes += line.size();
    // TODO: we can eliminate this copy by making ret_lines instead keep
    // references into the lines map. We can't just move the line contents into
    // ret_lines here because disassembly lines may appear multiple times in
    // the output. (Technically this should not be true, but I'm too lazy to
    // verify as such right now.)
    ret_lines.emplace_back(line);
  }

  // Phase 4: assemble the output lines into a single string and return it
  string ret;
  ret.reserve(ret_bytes);
  for (const auto& line : ret_lines) {
    ret += line;
  }
  return ret;
}



const vector<vector<X86Emulator::AuditResult>>& X86Emulator::get_audit_results() const {
  return this->audit_results;
}



void X86Emulator::import_state(FILE* stream) {
  uint8_t version;
  freadx(stream, &version, sizeof(version));
  if (version != 0) {
    throw runtime_error("unknown format version");
  }

  this->regs.import_state(stream);
  this->mem->import_state(stream);
}

void X86Emulator::export_state(FILE* stream) const {
  uint8_t version = 0;
  fwritex(stream, &version, sizeof(version));

  this->regs.export_state(stream);
  this->mem->export_state(stream);
}
