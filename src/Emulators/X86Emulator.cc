#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <array>
#include <deque>
#include <forward_list>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <unordered_map>
#include <utility>

#include "X86Emulator.hh"

using namespace std;

// TODO: Some opcodes do not use resolve_mem_ea to compute memory addresses.
// Those that don't need to handle the case where the override segment is set to
// FS, since (on Windows at least) that segment is not the same as the others.

static bool can_encode_as_int8(uint64_t value) {
  uint64_t masked = (value & 0xFFFFFFFFFFFFFF80);
  return (masked == 0xFFFFFFFFFFFFFF80) || (masked == 0);
}

static bool can_encode_as_int32(uint64_t value) {
  uint64_t masked = (value & 0xFFFFFFFF80000000);
  return (masked == 0xFFFFFFFF80000000) || (masked == 0);
}

string extend(const string& s, size_t len) {
  string ret = s;
  if (ret.size() < len) {
    ret.resize(len, ' ');
  }
  return ret;
}

const char* X86Emulator::name_for_segment(Segment segment) {
  switch (segment) {
    case Segment::NONE:
      return nullptr;
    case Segment::CS:
      return "cs";
    case Segment::DS:
      return "ds";
    case Segment::ES:
      return "es";
    case Segment::FS:
      return "fs";
    case Segment::GS:
      return "gs";
    case Segment::SS:
      return "ss";
    default:
      throw logic_error("invalid segment");
  }
}

uint8_t X86Emulator::DisassemblyState::standard_operand_size() const {
  if (this->opcode & 1) {
    return this->overrides.operand_size ? 16 : 32;
  } else {
    return 8;
  }
}

string X86Emulator::DisassemblyState::annotation_for_rm_ea(
    const DecodedRM& rm, int64_t operand_size, uint8_t flags) const {
  if (this->emu && rm.has_mem_ref()) {
    uint32_t addr = this->emu->resolve_mem_ea_untraced(rm);

    vector<string> tokens;
    if (!(flags & RMF::SUPPRESS_ADDRESS_TOKEN)) {
      if (operand_size > 0) {
        string value_str;
        try {
          if (operand_size == 8) {
            value_str = string_printf("0x%02hhX", this->emu->mem->read_u8(addr));
          } else if (operand_size == 16) {
            value_str = string_printf("0x%04hX", this->emu->mem->read_u16l(addr));
          } else if (operand_size == 32) {
            value_str = string_printf("0x%08" PRIX32, this->emu->mem->read_u32l(addr));
          } else if (operand_size == 64) {
            value_str = string_printf("0x%016" PRIX64, this->emu->mem->read_u64l(addr));
          } else {
            value_str = "DATA:" + format_data_string(this->emu->mem->read(addr, operand_size >> 8), nullptr, FormatDataFlags::HEX_ONLY);
          }
        } catch (const exception& e) {
          value_str = string_printf("(unreadable: %s)", e.what());
        }
        tokens.emplace_back(string_printf(
                                "[0x%08" PRIX32 "]=", addr) +
            value_str);
      } else if (operand_size == 0) {
        tokens.emplace_back(string_printf("[0x%08" PRIX32 "]", addr));
      }
    }

    if (this->labels) {
      for (auto label_its = labels->equal_range(addr);
           label_its.first != label_its.second;
           label_its.first++) {
        tokens.emplace_back("label " + label_its.first->second);
      }
    }

    if (!tokens.empty()) {
      return " /* " + join(tokens, ", ") + " */";
    } else {
      return "";
    }

  } else {
    return "";
  }
}

string X86Emulator::DisassemblyState::rm_ea_str(
    const DecodedRM& rm, uint8_t operand_size, uint8_t flags) const {
  return rm.ea_str(operand_size, flags, this->overrides.segment) + this->annotation_for_rm_ea(rm, operand_size, flags);
}

string X86Emulator::DisassemblyState::rm_non_ea_str(
    const DecodedRM& rm, uint8_t operand_size, uint8_t flags) const {
  return rm.non_ea_str(operand_size, flags);
}

string X86Emulator::DisassemblyState::rm_str(
    const DecodedRM& rm, uint8_t operand_size, uint8_t flags) const {
  return this->rm_str(rm, operand_size, operand_size, flags);
}

string X86Emulator::DisassemblyState::rm_str(
    const DecodedRM& rm,
    uint8_t ea_operand_size,
    uint8_t non_ea_operand_size,
    uint8_t flags) const {
  string ea_str = this->rm_ea_str(rm, ea_operand_size, flags);
  string non_ea_str = this->rm_non_ea_str(rm, non_ea_operand_size, flags);
  if (flags & RMF::EA_FIRST) {
    return ea_str + ", " + non_ea_str;
  } else {
    return non_ea_str + ", " + ea_str;
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
    throw logic_error("invalid operand size in get_operand");
  }
}

static const std::array<const char* const, 0x10> name_for_condition_code = {
    "o", "no", "b", "ae", "e", "ne", "be", "a",
    "s", "ns", "pe", "po", "l", "ge", "le", "g"};

X86Emulator::Regs::XMMReg::XMMReg() {
  this->u64[0] = 0;
  this->u64[1] = 0;
}

X86Emulator::Regs::XMMReg::XMMReg(uint32_t v) {
  this->u32[0] = v;
  this->u32[1] = 0;
  this->u32[2] = 0;
  this->u32[3] = 0;
}

X86Emulator::Regs::XMMReg::XMMReg(uint64_t v) {
  this->u64[0] = v;
  this->u64[1] = 0;
}

X86Emulator::Regs::XMMReg& X86Emulator::Regs::XMMReg::operator=(uint32_t v) {
  this->u32[0] = v;
  this->u32[1] = 0;
  this->u32[2] = 0;
  this->u32[3] = 0;
  return *this;
}

X86Emulator::Regs::XMMReg& X86Emulator::Regs::XMMReg::operator=(uint64_t v) {
  this->u64[0] = v;
  this->u64[1] = 0;
  return *this;
}

X86Emulator::Regs::XMMReg::operator uint32_t() const {
  return this->u32[0];
}

X86Emulator::Regs::XMMReg::operator uint64_t() const {
  return this->u64[0];
}

X86Emulator::Regs::Regs() {
  for (size_t x = 0; x < 8; x++) {
    this->regs[x].u = 0;
  }
  for (size_t x = 0; x < 8; x++) {
    this->xmm[x].u64[0] = 0;
    this->xmm[x].u64[1] = 0;
  }
  // Default flags:
  // 0x00200000 (bit 21) = able to use cpuid instruction
  // 0x00000200 (bit 9) = interrupts enabled
  // 0x00000002 (bit 1) = reserved, but apparently always set in EFLAGS
  this->eflags = 0x00200202;
  this->eip = 0;
}

void X86Emulator::Regs::set_by_name(const string& reg_name, uint32_t value) {
  string lower_name = tolower(reg_name);
  if (lower_name == "al") {
    this->w_al(value);
  } else if (lower_name == "cl") {
    this->w_cl(value);
  } else if (lower_name == "dl") {
    this->w_dl(value);
  } else if (lower_name == "bl") {
    this->w_bl(value);
  } else if (lower_name == "ah") {
    this->w_ah(value);
  } else if (lower_name == "ch") {
    this->w_ch(value);
  } else if (lower_name == "dh") {
    this->w_dh(value);
  } else if (lower_name == "bh") {
    this->w_bh(value);

  } else if (lower_name == "ax") {
    this->w_ax(value);
  } else if (lower_name == "cx") {
    this->w_cx(value);
  } else if (lower_name == "dx") {
    this->w_dx(value);
  } else if (lower_name == "bx") {
    this->w_bx(value);
  } else if (lower_name == "sp") {
    this->w_sp(value);
  } else if (lower_name == "bp") {
    this->w_bp(value);
  } else if (lower_name == "si") {
    this->w_si(value);
  } else if (lower_name == "di") {
    this->w_di(value);

  } else if (lower_name == "eax") {
    this->w_eax(value);
  } else if (lower_name == "ecx") {
    this->w_ecx(value);
  } else if (lower_name == "edx") {
    this->w_edx(value);
  } else if (lower_name == "ebx") {
    this->w_ebx(value);
  } else if (lower_name == "esp") {
    this->w_esp(value);
  } else if (lower_name == "ebp") {
    this->w_ebp(value);
  } else if (lower_name == "esi") {
    this->w_esi(value);
  } else if (lower_name == "edi") {
    this->w_edi(value);

  } else if (lower_name == "eflags") {
    this->eflags = value;
  } else {
    throw invalid_argument("unknown x86 register");
  }
}

uint8_t& X86Emulator::Regs::reg_unreported8(uint8_t which) {
  if (which & ~7) {
    throw logic_error("invalid register index");
  }
  if (which & 4) {
    return this->regs[which & 3].u8.h;
  } else {
    return this->regs[which].u8.l;
  }
}

le_uint16_t& X86Emulator::Regs::reg_unreported16(uint8_t which) {
  if (which & ~7) {
    throw logic_error("invalid register index");
  }
  return this->regs[which].u16;
}

le_uint32_t& X86Emulator::Regs::reg_unreported32(uint8_t which) {
  if (which & ~7) {
    throw logic_error("invalid register index");
  }
  return this->regs[which].u;
}

const uint8_t& X86Emulator::Regs::reg_unreported8(uint8_t which) const {
  return const_cast<Regs*>(this)->reg_unreported8(which);
}
const le_uint16_t& X86Emulator::Regs::reg_unreported16(uint8_t which) const {
  return const_cast<Regs*>(this)->reg_unreported16(which);
}
const le_uint32_t& X86Emulator::Regs::reg_unreported32(uint8_t which) const {
  return const_cast<Regs*>(this)->reg_unreported32(which);
}

le_uint32_t& X86Emulator::Regs::xmm_unreported32(uint8_t which) {
  if (which & ~7) {
    throw logic_error("invalid register index");
  }
  return this->xmm[which].u32[0];
}

le_uint64_t& X86Emulator::Regs::xmm_unreported64(uint8_t which) {
  if (which & ~7) {
    throw logic_error("invalid register index");
  }
  return this->xmm[which].u64[0];
}

X86Emulator::Regs::XMMReg& X86Emulator::Regs::xmm_unreported128(uint8_t which) {
  if (which & ~7) {
    throw logic_error("invalid register index");
  }
  return this->xmm[which];
}

const le_uint32_t& X86Emulator::Regs::xmm_unreported32(uint8_t which) const {
  return const_cast<Regs*>(this)->xmm_unreported32(which);
}
const le_uint64_t& X86Emulator::Regs::xmm_unreported64(uint8_t which) const {
  return const_cast<Regs*>(this)->xmm_unreported64(which);
}
const X86Emulator::Regs::XMMReg& X86Emulator::Regs::xmm_unreported128(uint8_t which) const {
  return const_cast<Regs*>(this)->xmm_unreported128(which);
}

uint32_t X86Emulator::Regs::read_unreported(uint8_t which, uint8_t size) const {
  if (size == 8) {
    return this->reg_unreported8(which);
  } else if (size == 16) {
    return this->reg_unreported16(which);
  } else if (size == 32) {
    return this->reg_unreported32(which);
  } else {
    throw logic_error("invalid operand size");
  }
}

X86Emulator::Regs::XMMReg X86Emulator::Regs::read_xmm_unreported(uint8_t which, uint8_t size) const {
  XMMReg ret = this->xmm_unreported128(which);
  if (size == 32) {
    ret.u64[1] = 0;
    ret.u64[0] &= 0xFFFFFFFF;
  } else if (size == 64) {
    ret.u64[1] = 0;
  } else if (size != 128) {
    throw logic_error("invalid xmm access size");
  }
  return ret;
}

bool X86Emulator::Regs::read_flag(uint32_t mask) {
  this->mark_flags_read(mask);
  return this->eflags & mask;
}

void X86Emulator::Regs::replace_flag(uint32_t mask, bool value) {
  this->mark_flags_written(mask);
  this->eflags = (this->eflags & ~mask) | (value ? mask : 0);
}

string X86Emulator::Regs::flags_str(uint32_t flags) {
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

string X86Emulator::Regs::flags_str() const {
  return this->flags_str(this->eflags);
}

void X86Emulator::Regs::mark_flags_read(uint32_t mask) const {
  this->flags_read |= mask;
}

void X86Emulator::Regs::mark_flags_written(uint32_t mask) const {
  this->flags_written |= mask;
}

static void mark_reg(array<uint32_t, 8>& regs, uint8_t which, uint8_t size) {
  if (size == 8) {
    if (which & 4) {
      regs.at(which & 3) |= 0x0000FF00;
    } else {
      regs.at(which & 3) |= 0x000000FF;
    }
  } else if (size == 16) {
    regs.at(which) |= 0x0000FFFF;
  } else if (size == 32) {
    regs.at(which) = 0xFFFFFFFF;
  } else {
    throw logic_error("invalid operand size");
  }
}

void X86Emulator::Regs::mark_read(uint8_t which, uint8_t size) const {
  mark_reg(this->regs_read, which, size);
}

void X86Emulator::Regs::mark_written(uint8_t which, uint8_t size) const {
  mark_reg(this->regs_written, which, size);
}

static void mark_xmm(array<X86Emulator::Regs::XMMReg, 8>& regs, uint8_t which, uint8_t size) {
  if (size == 32) {
    regs.at(which).u32[0] = 0xFFFFFFFF;
  } else if (size == 64) {
    regs.at(which).u64[0] = 0xFFFFFFFFFFFFFFFF;
  } else if (size == 128) {
    regs.at(which).u64[0] = 0xFFFFFFFFFFFFFFFF;
    regs.at(which).u64[1] = 0xFFFFFFFFFFFFFFFF;
  } else {
    throw logic_error("invalid operand size");
  }
}

void X86Emulator::Regs::mark_xmm_read(uint8_t which, uint8_t size) const {
  mark_xmm(this->xmm_regs_read, which, size);
}

void X86Emulator::Regs::mark_xmm_written(uint8_t which, uint8_t size) const {
  mark_xmm(this->xmm_regs_written, which, size);
}

static bool is_reg_marked(const array<uint32_t, 8>& regs, uint8_t which, uint8_t size) {
  if (size == 8) {
    if (which & 4) {
      return regs.at(which & 3) == 0x0000FF00;
    } else {
      return regs.at(which & 3) == 0x000000FF;
    }
  } else if (size == 16) {
    return regs.at(which) == 0x0000FFFF;
  } else if (size == 32) {
    return regs.at(which) == 0xFFFFFFFF;
  } else {
    throw logic_error("invalid operand size");
  }
}

bool X86Emulator::Regs::was_read(uint8_t which, uint8_t size) const {
  return is_reg_marked(this->regs_read, which, size);
}

bool X86Emulator::Regs::was_written(uint8_t which, uint8_t size) const {
  return is_reg_marked(this->regs_written, which, size);
}

static bool is_xmm_marked(const array<X86Emulator::Regs::XMMReg, 8>& regs, uint8_t which, uint8_t size) {
  const auto& reg = regs.at(which);
  if (size == 32) {
    return reg.u64[1] == 0x0000000000000000 && reg.u64[0] == 0x00000000FFFFFFFF;
  } else if (size == 64) {
    return reg.u64[1] == 0x0000000000000000 && reg.u64[0] == 0x00000000FFFFFFFF;
  } else if (size == 128) {
    return reg.u64[1] == 0xFFFFFFFFFFFFFFFF && reg.u64[0] == 0xFFFFFFFFFFFFFFFF;
  } else {
    throw logic_error("invalid operand size");
  }
}

bool X86Emulator::Regs::xmm_was_read(uint8_t which, uint8_t size) const {
  return is_xmm_marked(this->xmm_regs_read, which, size);
}

bool X86Emulator::Regs::xmm_was_written(uint8_t which, uint8_t size) const {
  return is_xmm_marked(this->xmm_regs_written, which, size);
}

uint32_t X86Emulator::Regs::get_read_flags() const {
  return this->flags_read;
}

uint32_t X86Emulator::Regs::get_written_flags() const {
  return this->flags_written;
}

void X86Emulator::Regs::reset_access_flags() const {
  for (auto& it : this->regs_read) {
    it = 0;
  }
  for (auto& it : this->regs_written) {
    it = 0;
  }
  for (auto& it : this->xmm_regs_read) {
    it.u64[0] = 0;
    it.u64[1] = 0;
  }
  for (auto& it : this->xmm_regs_written) {
    it.u64[0] = 0;
    it.u64[1] = 0;
  }
  this->flags_read = 0;
  this->flags_written = 0;
}

bool X86Emulator::Regs::check_condition(uint8_t cc) {
  switch (cc) {
    case 0x00: // o
    case 0x01: // no
      return this->read_flag(OF) != (cc & 1);
    case 0x02: // b/nae/c
    case 0x03: // nb/ae/nc
      return this->read_flag(CF) != (cc & 1);
    case 0x04: // z/e
    case 0x05: // nz/ne
      return this->read_flag(ZF) != (cc & 1);
    case 0x06: // be/na
    case 0x07: // nbe/a
      return (this->read_flag(CF) || this->read_flag(ZF)) != (cc & 1);
    case 0x08: // s
    case 0x09: // ns
      return this->read_flag(SF) != (cc & 1);
    case 0x0A: // p/pe
    case 0x0B: // np/po
      return this->read_flag(PF) != (cc & 1);
    case 0x0C: // l/nge
    case 0x0D: // nl/ge
      return (this->read_flag(SF) != this->read_flag(OF)) != (cc & 1);
    case 0x0E: // le/ng
    case 0x0F: // nle/g
      return (this->read_flag(ZF) || (this->read_flag(SF) != this->read_flag(OF))) != (cc & 1);
    default:
      throw logic_error("invalid condition code");
  }
}

template <typename T, enable_if_t<is_unsigned<T>::value, bool>>
void X86Emulator::Regs::set_flags_integer_result(T res, uint32_t apply_mask) {
  if (apply_mask & SF) {
    // SF should be set if the result is negative
    this->replace_flag(SF, res & msb_for_type<T>);
  }
  if (apply_mask & ZF) {
    // ZF should be set if the result is zero
    this->replace_flag(ZF, (res == 0));
  }
  if (apply_mask & PF) {
    // PF should be set if the number of ones is even. However, x86's PF
    // apparently only applies to the least-significant byte of the result.
    bool pf = true;
    for (uint8_t v = res; v != 0; v >>= 1) {
      pf ^= (v & 1);
    }
    this->replace_flag(PF, pf);
  }
}

template <typename T, enable_if_t<is_unsigned<T>::value, bool>>
void X86Emulator::Regs::set_flags_bitwise_result(T res, uint32_t apply_mask) {
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

template <typename T, enable_if_t<is_unsigned<T>::value, bool>>
T X86Emulator::Regs::set_flags_integer_add(T a, T b, uint32_t apply_mask) {
  T res = a + b;

  this->set_flags_integer_result(res, apply_mask);

  if (apply_mask & OF) {
    // OF should be set if the result overflows the destination location, as if
    // the operation was signed. Equivalently, OF should be set if a and b have
    // the same sign and the result has the opposite sign (that is, the signed
    // result has overflowed).
    this->replace_flag(OF,
        ((a & msb_for_type<T>) == (b & msb_for_type<T>)) &&
            ((a & msb_for_type<T>) != (res & msb_for_type<T>)));
  }
  if (apply_mask & CF) {
    // CF should be set if any nonzero bits were carried out, as if the
    // operation was unsigned. This is equivalent to the condition that the
    // result is less than either input operand, because a full wrap-around
    // cannot occur: the maximum value that can be added to any other value is
    // one less than would result in a full wrap-around.
    this->replace_flag(CF, (res < a) || (res < b));
  }
  if (apply_mask & AF) {
    // AF should be set if any nonzero bits were carried out of the lowest
    // nybble. The logic here is similar to the CF logic, but applies only to
    // the lowest 4 bytes.
    this->replace_flag(AF, ((res & 0x0F) < (a & 0x0F)) || ((res & 0x0F) < (b & 0x0F)));
  }

  return res;
}

template <typename T, enable_if_t<is_unsigned<T>::value, bool>>
T X86Emulator::Regs::set_flags_integer_add_with_carry(T a, T b, uint32_t apply_mask) {
  // If CF is not set, this operation is the same as a normal add. The rest of
  // this function will assume CF was set.
  if (!this->read_flag(Regs::CF)) {
    return this->set_flags_integer_add(a, b, apply_mask);
  }

  T res = a + b + 1;

  this->set_flags_integer_result(res, apply_mask);

  if (apply_mask & OF) {
    // The same rules as for add-without-carry apply here. The edge cases that
    // seem like they should require special treatment actually do not, because
    // adding 1 moves the result away from any critical values, as shown below.
    // So, we can use the same rule - OF = ((a and b have same sign) and (res
    // has opposite sign as a and b)).
    // a  b  c r  OF
    // 00 00 1 01 0 (0    + 0    + 1 == 1)
    // 00 7F 1 80 1 (0    + 127  + 1 != -128)
    // 00 80 1 81 0 (0    + -128 + 1 == -127)
    // 00 FF 1 00 0 (0    + -1   + 1 == 0)
    // 7F 7F 1 FF 1 (127  + 127  + 1 != -1)
    // 7F 80 1 00 0 (127  + -128 + 1 == 0)
    // 7F FF 1 7F 0 (127  + -1   + 1 == 127)
    // 80 80 1 01 1 (-128 + -128 + 1 != 1)
    // 80 FF 1 80 0 (-128 + -1   + 1 == -128)
    // FF FF 1 FF 0 (-1   + -1   + 1 == -1)
    this->replace_flag(OF,
        ((a & msb_for_type<T>) == (b & msb_for_type<T>)) &&
            ((a & msb_for_type<T>) != (res & msb_for_type<T>)));
  }
  if (apply_mask & CF) {
    // CF should be set if any nonzero bits were carried out, as if the
    // operation was unsigned. This is equivalent to the condition that the
    // result is less than or equal to either input operand, because at most
    // exactly one full wrap-around can occur, and the result must be greater
    // than at least one of the input operands because CF was set.
    this->replace_flag(CF, (res <= a) || (res <= b));
  }
  if (apply_mask & AF) {
    // AF should be set if any nonzero bits were carried out of the lowest
    // nybble. Similar reasoning as for CF applies here (about why we use <=).
    this->replace_flag(AF, ((res & 0x0F) <= (a & 0x0F)) || ((res & 0x0F) <= (b & 0x0F)));
  }

  return res;
}

template <typename T, enable_if_t<is_unsigned<T>::value, bool>>
T X86Emulator::Regs::set_flags_integer_subtract(T a, T b, uint32_t apply_mask) {
  T res = a - b;

  this->set_flags_integer_result(res, apply_mask);

  if (apply_mask & OF) {
    // OF should be set if the result overflows the destination location, as if
    // the operation was signed. Subtraction overflow logic is harder to
    // understand than for addition, but the resulting rule is just as simple.
    // The following observations apply:
    // - If the operands are the same sign, overflow cannot occur, because there
    //   is no way to get a result far enough away from the minuend.
    // - If the operands are different signs and the result is the opposite sign
    //   as the minuend, then overflow has occurred. (If the minuend is
    //   positive, then it should have increased; if it was negative, it should
    //   have decreased.)
    // The edge cases are described in the following table:
    // a  b  r  OF
    // 00 00 00 0 (0    - 0    == 0)     ++ + 0
    // 00 7F 81 0 (0    - 127  == -127)  ++ - 0
    // 00 80 80 1 (0    - -128 != -128)  +- - 1
    // 00 FF 01 0 (0    - -1   == 1)     +- + 0
    // 7F 00 7F 0 (127  - 0    == 127)   ++ + 0
    // 7F 7F 00 0 (127  - 127  == 0)     ++ + 0
    // 7F 80 FF 1 (127  - -128 != -1)    +- - 1
    // 7F FF 80 1 (127  - -1   != -128)  +- - 1
    // 80 00 80 0 (-128 - 0    == -128)  -+ - 0
    // 80 7F 01 1 (-128 - 127  != 1)     -+ + 1
    // 80 80 00 0 (-128 - -128 == 0)     -- + 0
    // 80 FF 81 0 (-128 - -1   == -127)  -- - 0
    // FF 00 FF 0 (-1   - 0    == -1)    -+ - 0
    // FF 7F 80 0 (-1   - 127  == -128)  -+ - 0
    // FF 80 7F 0 (-1   - -128 == 127)   -- + 0
    // FF FF 00 0 (-1   - -1   == 0)     -- + 0
    this->replace_flag(OF,
        ((a & msb_for_type<T>) != (b & msb_for_type<T>)) &&
            ((a & msb_for_type<T>) != (res & msb_for_type<T>)));
  }
  if (apply_mask & CF) {
    // CF should be set if any nonzero bits were borrowed in, as if the
    // operation was unsigned. This is equivalent to the condition that the
    // result is greater than the input minuend operand, because a full
    // wrap-around cannot occur: the maximum value that can be subtracted from
    // any other value is one less than would result in a full wrap-around.
    this->replace_flag(CF, (res > a));
  }
  if (apply_mask & AF) {
    // AF should be set if any nonzero bits were borrowed into the lowest
    // nybble. The logic here is similar to the CF logic, but applies only to
    // the lowest 4 bytes.
    this->replace_flag(AF, ((res & 0x0F) > (a & 0x0F)));
  }

  return res;
}

template <typename T, enable_if_t<is_unsigned<T>::value, bool>>
T X86Emulator::Regs::set_flags_integer_subtract_with_borrow(T a, T b, uint32_t apply_mask) {
  // If CF is not set, this operation is the same as a normal subtract. The rest
  // of this function will assume CF was set.
  if (!this->read_flag(Regs::CF)) {
    return this->set_flags_integer_subtract(a, b, apply_mask);
  }

  T res = a - b - 1;

  this->set_flags_integer_result(res, apply_mask);

  if (apply_mask & OF) {
    // Perhaps surprisingly, the overflow logic is the same in the borrow case
    // as in the non-borrow case. This table summarizes the edge cases:
    // a  b  c r  OF
    // 00 00 1 FF 0 (0    - 0    - 1 == -1)    ++ - 0
    // 00 7F 1 80 0 (0    - 127  - 1 == -128)  ++ - 0
    // 00 80 1 7F 0 (0    - -128 - 1 == 127)   +- + 0
    // 00 FF 1 00 0 (0    - -1   - 1 == 0)     +- + 0
    // 7F 00 1 7E 0 (127  - 0    - 1 == 126)   ++ + 0
    // 7F 7F 1 FF 0 (127  - 127  - 1 == -1)    ++ - 0
    // 7F 80 1 FE 1 (127  - -128 - 1 != -2)    +- - 1
    // 7F FF 1 7F 0 (127  - -1   - 1 == 127)   +- + 0
    // 80 00 1 7F 1 (-128 - 0    - 1 != 127)   -+ + 1
    // 80 7F 1 00 1 (-128 - 127  - 1 != 0)     -+ + 1
    // 80 80 1 FF 0 (-128 - -128 - 1 == -1)    -- - 0
    // 80 FF 1 80 0 (-128 - -1   - 1 == -128)  -- - 0
    // FF 00 1 FE 0 (-1   - 0    - 1 == -2)    -+ - 0
    // FF 7E 1 80 0 (-1   - 126  - 1 == -128)  -+ - 0
    // FF 7F 1 7F 1 (-1   - 127  - 1 != 127)   -+ + 1
    // FF 80 1 7E 0 (-1   - -128 - 1 != 126)   -- + 0
    // FF 81 1 7D 0 (-1   - -127 - 1 != 125)   -- + 0
    // FF FF 1 FF 0 (-1   - -1   - 1 == -1)    -- - 0
    this->replace_flag(OF,
        ((a & msb_for_type<T>) != (b & msb_for_type<T>)) &&
            ((a & msb_for_type<T>) != (res & msb_for_type<T>)));
  }
  if (apply_mask & CF) {
    // Analogously to adding with carry, we use the same condition as in the
    // non-borrow case, but use >= instead of >. This is because the result
    // cannot logically be equal to the minuend: CF was set, so we must have
    // subtracted at least 1.
    this->replace_flag(CF, (res >= a));
  }
  if (apply_mask & AF) {
    // Again, this is analogous to the AF condition in the non-borrow case.
    this->replace_flag(AF, ((res & 0x0F) >= (a & 0x0F)));
  }

  return res;
}

void X86Emulator::Regs::import_state(FILE* stream) {
  uint8_t version = freadx<uint8_t>(stream);
  if (version > 2) {
    throw runtime_error("unknown format version");
  }

  for (size_t x = 0; x < 8; x++) {
    this->regs[x].u = freadx<le_uint32_t>(stream);
  }
  this->eflags = freadx<le_uint32_t>(stream);
  this->eip = freadx<le_uint32_t>(stream);
  if (version >= 1) {
    for (size_t x = 0; x < 8; x++) {
      this->xmm[x].u64[0] = freadx<le_uint64_t>(stream);
      this->xmm[x].u64[1] = freadx<le_uint64_t>(stream);
    }
  } else {
    for (size_t x = 0; x < 8; x++) {
      this->xmm[x].u64[0] = 0;
      this->xmm[x].u64[1] = 0;
    }
  }
}

void X86Emulator::Regs::export_state(FILE* stream) const {
  fwritex<uint8_t>(stream, 1); // version

  for (size_t x = 0; x < 8; x++) {
    fwritex<le_uint32_t>(stream, this->regs[x].u);
  }
  fwritex<le_uint32_t>(stream, this->eflags);
  fwritex<le_uint32_t>(stream, this->eip);
  for (size_t x = 0; x < 8; x++) {
    fwritex<le_uint64_t>(stream, this->xmm[x].u64[0]);
    fwritex<le_uint64_t>(stream, this->xmm[x].u64[1]);
  }
}

void X86Emulator::print_state_header(FILE* stream) const {
  fprintf(stream, "\
-CYCLES-  --EAX--- --ECX--- --EDX--- --EBX--- --ESP--- --EBP--- --ESI--- --EDI---  \
-EFLAGS-(--BITS--) <XMM> @ --EIP--- = CODE\n");
}

void X86Emulator::print_state(FILE* stream) const {
  string xmm_str;
  for (size_t x = 0; x < 8; x++) {
    const auto& xmm = this->regs.xmm_unreported128(x);
    if ((xmm.u64[0] | xmm.u64[1]) == 0) {
      continue;
    }
    if (!xmm_str.empty()) {
      xmm_str += ", ";
    }
    xmm_str += string_printf("xmm%zu=%016" PRIX64 "%016" PRIX64,
        x, xmm.u64[1].load(), xmm.u64[0].load());
  }
  if (!xmm_str.empty()) {
    xmm_str += ' ';
  }

  string flags_str = this->regs.flags_str();
  fprintf(stream, "\
%08" PRIX64 "  %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 "  \
%08" PRIX32 "(%s) %s@ %08" PRIX32 " = ",
      this->instructions_executed,
      this->regs.reg_unreported32(0).load(),
      this->regs.reg_unreported32(1).load(),
      this->regs.reg_unreported32(2).load(),
      this->regs.reg_unreported32(3).load(),
      this->regs.reg_unreported32(4).load(),
      this->regs.reg_unreported32(5).load(),
      this->regs.reg_unreported32(6).load(),
      this->regs.reg_unreported32(7).load(),
      this->regs.read_eflags_unreported(),
      flags_str.c_str(),
      xmm_str.c_str(),
      this->regs.eip);

  string data;
  uint32_t addr = this->regs.eip;
  try {
    while (data.size() < 0x10) {
      data += this->mem->read_s8(addr++);
    }
  } catch (const out_of_range&) {
  }

  this->compute_execution_labels();

  DisassemblyState s = {
      StringReader(data),
      this->regs.eip,
      0,
      this->overrides,
      {},
      &this->execution_labels,
      this,
  };
  try {
    string disassembly = this->disassemble_one(s);
    fprintf(stream, "%s\n", disassembly.c_str());
  } catch (const exception& e) {
    fprintf(stream, "(failed: %s)\n", e.what());
  }
}

void X86Emulator::set_behavior_by_name(const string& name) {
  if (name == "specification") {
    this->behavior = Behavior::SPECIFICATION;
  } else if (name == "windows-arm-emu") {
    this->behavior = Behavior::WINDOWS_ARM_EMULATOR;
  } else {
    throw runtime_error("invalid x86 behavior name");
  }
}

void X86Emulator::set_time_base(uint64_t time_base) {
  this->tsc_offset = time_base - this->instructions_executed;
}

void X86Emulator::set_time_base(const vector<uint64_t>& tsc_overrides) {
  this->tsc_overrides.clear();
  this->tsc_overrides.insert(
      this->tsc_overrides.end(), tsc_overrides.begin(), tsc_overrides.end());
}

// TODO: Eliminate code duplication between the two versions of this function
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

static const char* name_for_st_reg(uint8_t reg) {
  if (reg & ~7) {
    throw logic_error("invalid register index");
  }
  static const char* const reg_names[8] = {"st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7"};
  return reg_names[reg];
}

static const char* name_for_xmm_reg(uint8_t reg) {
  if (reg & ~7) {
    throw logic_error("invalid register index");
  }
  static const char* const reg_names[8] = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
  return reg_names[reg];
}

X86Emulator::DecodedRM::DecodedRM(int8_t ea_reg, int32_t ea_disp)
    : non_ea_reg(0),
      ea_reg(ea_reg),
      ea_index_reg(-1),
      ea_index_scale(0),
      ea_disp(ea_disp) {}

bool X86Emulator::DecodedRM::has_mem_ref() const {
  return (this->ea_index_scale != -1);
}

string X86Emulator::DecodedRM::ea_str(
    uint8_t operand_size,
    uint8_t flags,
    Segment override_segment) const {
  if (this->ea_index_scale == -1) {
    if (this->ea_reg & ~7) {
      throw logic_error("DecodedRM has reg ref but invalid ea_reg");
    }
    if (flags & EA_XMM) {
      return name_for_xmm_reg(this->ea_reg);
    } else if (flags & EA_ST) {
      return name_for_st_reg(this->ea_reg);
    } else {
      return name_for_reg(this->ea_reg, operand_size);
    }

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
    // If there are no other tokens, this is likely an absolute reference, even
    // if it is zero. Some programs do this with non-default segment overrides,
    // or these opcodes can appear when the actual offset is to be filled in
    // later (e.g. by a relocation adjustment).
    if (this->ea_disp || tokens.empty()) {
      if (tokens.empty()) {
        tokens.emplace_back(string_printf("0x%08" PRIX32, this->ea_disp));
      } else {
        if (this->ea_disp < 0) {
          tokens.emplace_back("-");
          tokens.emplace_back(string_printf("0x%08" PRIX32, -this->ea_disp));
        } else {
          tokens.emplace_back("+");
          tokens.emplace_back(string_printf("0x%08" PRIX32, this->ea_disp));
        }
      }
    }
    string ret;
    if (!(flags & RMF::SUPPRESS_OPERAND_SIZE)) {
      if (operand_size == 8) {
        ret = "byte ";
      } else if (operand_size == 16) {
        ret = "word ";
      } else if (operand_size == 32) {
        ret = "dword ";
      } else if (operand_size == 64) {
        ret = "qword ";
      } else if (operand_size == 80) {
        ret = "tbyte ";
      } else if (operand_size == 128) {
        ret = "oword ";
      } else {
        ret = string_printf("(0x%02" PRIX8 ") ", operand_size);
      }
    }
    if (override_segment != Segment::NONE) {
      ret += name_for_segment(override_segment);
      ret += ':';
    }
    ret += '[';
    ret += join(tokens, " ");
    ret += ']';
    return ret;
  }
}

string X86Emulator::DecodedRM::non_ea_str(uint8_t operand_size, uint8_t flags) const {
  if (flags & NON_EA_XMM) {
    return name_for_xmm_reg(this->non_ea_reg);
  } else if (flags & NON_EA_ST) {
    return name_for_st_reg(this->non_ea_reg);
  } else {
    return name_for_reg(this->non_ea_reg, operand_size);
  }
}

uint32_t X86Emulator::get_segment_offset() const {
  if (this->overrides.segment == Segment::FS) {
    try {
      return this->mem->get_symbol_addr("fs");
    } catch (const out_of_range&) {
      throw runtime_error("fs symbol not set");
    }
  }
  return 0;
}

uint32_t X86Emulator::resolve_mem_ea(
    const DecodedRM& rm, bool always_trace_sources) {
  if (rm.ea_index_scale < 0) {
    throw logic_error("resolve_mem_ea called on non-memory reference");
  }

  if (!always_trace_sources && !this->trace_data_source_addrs) {
    return this->resolve_mem_ea_untraced(rm);
  }

  uint32_t segment_offset = this->get_segment_offset();
  uint32_t base_component = 0;
  uint32_t index_component = 0;
  uint32_t disp_component = rm.ea_disp;
  if (rm.ea_reg >= 0) {
    base_component = this->regs.read32(rm.ea_reg);
  }
  if (rm.ea_index_scale > 0) {
    index_component = rm.ea_index_scale * this->regs.read32(rm.ea_index_reg);
  }
  return segment_offset + base_component + index_component + disp_component;
}

// TODO: Deduplicate this function with resolve_mem_ea somehow. It's kind of
// important that this one be const though
uint32_t X86Emulator::resolve_mem_ea_untraced(const DecodedRM& rm) const {
  if (rm.ea_index_scale < 0) {
    throw logic_error("resolve_mem_ea_untraced called on non-memory reference");
  }

  uint32_t segment_offset = this->get_segment_offset();
  uint32_t base_component = 0;
  uint32_t index_component = 0;
  uint32_t disp_component = rm.ea_disp;
  if (rm.ea_reg >= 0) {
    base_component = this->regs.reg_unreported32(rm.ea_reg);
  }
  if (rm.ea_index_scale > 0) {
    index_component = rm.ea_index_scale * this->regs.reg_unreported32(rm.ea_index_reg);
  }
  return segment_offset + base_component + index_component + disp_component;
}

string X86Emulator::DataAccess::str() const {
  string loc_str;
  if (this->is_reg) {
    if (this->addr == 8) {
      loc_str = "eflags";
    } else {
      loc_str = name_for_reg(this->addr, this->size);
    }
  } else if (this->is_xmm_reg) {
    loc_str = string_printf("xmm%" PRIu32, this->addr);
  } else {
    loc_str = string_printf("[0x%08" PRIX32 "]", this->addr);
  }

  string val_str;
  if (this->size == 8) {
    val_str = string_printf("0x%02" PRIX64, this->value_low & 0xFF);
  } else if (this->size == 16) {
    val_str = string_printf("0x%04" PRIX64, this->value_low & 0xFFFF);
  } else if (this->size == 32) {
    val_str = string_printf("0x%08" PRIX64, this->value_low & 0xFFFFFFFF);
  } else if (this->size == 64) {
    val_str = string_printf("0x%016" PRIX64, this->value_low);
  } else if (this->size == 128) {
    val_str = string_printf("0x%016" PRIX64 "%016" PRIX64, this->value_high, this->value_low);
  } else {
    throw logic_error("invalid operand size");
  }

  return string_printf("%08" PRIX64 ": %s %s %s",
      this->cycle_num,
      loc_str.c_str(),
      this->is_write ? "<=" : "=>",
      val_str.c_str());
}

void X86Emulator::report_access(shared_ptr<DataAccess> acc) {
  if (this->trace_data_sources) {
    if (acc->is_write) {
      this->current_writes.emplace(acc);
    } else {
      this->current_reads.emplace(acc);
    }
  }
}

void X86Emulator::report_access(uint32_t addr, uint8_t size, bool is_write, bool is_reg, bool is_xmm_reg, uint64_t value_low, uint64_t value_high) {
  auto acc = make_shared<DataAccess>(DataAccess{this->instructions_executed, addr, size, is_write, is_reg, is_xmm_reg, value_low, value_high, {}});
  this->report_access(acc);
}

void X86Emulator::report_mem_access(uint32_t addr, uint8_t size, bool is_write, uint64_t value_low, uint64_t value_high) {
  this->EmulatorBase::report_mem_access(addr, size, is_write);
  this->report_access(addr, size, is_write, false, false, value_low, value_high);
}

void X86Emulator::link_current_accesses() {
  if (!this->trace_data_sources) {
    this->current_reads.clear();
    this->current_writes.clear();
    this->regs.reset_access_flags();
    return;
  }

  // Convert all accessed registers into DataAccess objects
  static const array<uint8_t, 3> sizes = {8, 16, 32};
  static const array<uint8_t, 3> xmm_sizes = {32, 64, 128};
  for (uint8_t which = 0; which < 8; which++) {
    for (uint8_t size : sizes) {
      if (this->regs.was_read(which, size)) {
        this->report_access(which, size, false, true, false,
            this->prev_regs.read_unreported(which, size), 0);
      }
      if (this->regs.was_written(which, size)) {
        this->report_access(which, size, true, true, false,
            this->regs.read_unreported(which, size), 0);
      }
    }
    for (uint8_t size : xmm_sizes) {
      if (this->regs.xmm_was_read(which, size)) {
        auto val = this->prev_regs.read_xmm_unreported(which, size);
        this->report_access(which, size, false, false, true, val.u64[0], val.u64[1]);
      }
      if (this->regs.xmm_was_written(which, size)) {
        auto val = this->regs.read_xmm_unreported(which, size);
        this->report_access(which, size, true, false, true, val.u64[0], val.u64[1]);
      }
    }
  }
  if (this->regs.get_read_flags()) {
    this->report_access(8, 32, false, true, false,
        this->prev_regs.read_eflags_unreported(), 0);
  }
  if (this->regs.get_written_flags()) {
    this->report_access(8, 32, true, true, false,
        this->regs.read_eflags_unreported(), 0);
  }
  this->regs.reset_access_flags();

  // Find the original sources for the reads, if any
  for (auto& acc : this->current_reads) {
    if (acc->is_reg) {
      if (acc->size == 32) {
        auto sources = this->current_reg_sources.at(acc->addr);
        acc->sources.emplace(sources.source32);
        acc->sources.emplace(sources.source16);
        acc->sources.emplace(sources.source8h);
        acc->sources.emplace(sources.source8l);
      } else if (acc->size == 16) {
        auto sources = this->current_reg_sources.at(acc->addr);
        acc->sources.emplace(sources.source16);
        acc->sources.emplace(sources.source8h);
        acc->sources.emplace(sources.source8l);
      } else if (acc->size == 8) {
        auto sources = this->current_reg_sources.at(acc->addr & 3);
        if (acc->addr & 4) {
          acc->sources.emplace(sources.source8h);
        } else {
          acc->sources.emplace(sources.source8l);
        }
      } else {
        throw logic_error("invalid register access size");
      }

    } else if (acc->is_xmm_reg) {
      auto sources = this->current_xmm_reg_sources.at(acc->addr);
      if (acc->size == 128) {
        acc->sources.emplace(sources.source128);
        acc->sources.emplace(sources.source64);
        acc->sources.emplace(sources.source32);
      } else if (acc->size == 64) {
        acc->sources.emplace(sources.source64);
        acc->sources.emplace(sources.source32);
      } else if (acc->size == 32) {
        acc->sources.emplace(sources.source32);
      } else {
        throw logic_error("invalid register access size");
      }

    } else { // Memory read
      size_t bytes = (acc->size >> 3);
      for (size_t x = 0; x < bytes; x++) {
        try {
          acc->sources.emplace(this->memory_data_sources.at(acc->addr + x));
        } catch (const out_of_range&) {
        }
      }
    }
  }

  // Assume that all writes done by the current opcode are dependent on all
  // reads done by the opcode (which is almost always true)
  for (auto& acc : this->current_writes) {
    acc->sources = this->current_reads;
  }

  // Update the sources for the written locations
  for (auto& acc : this->current_writes) {
    if (acc->is_reg) {
      size_t index = (acc->size == 8) ? (acc->addr & 3) : acc->addr;
      RegSources& sources = this->current_reg_sources.at(index);
      if (acc->size == 32) {
        sources.source32 = acc;
        sources.source16 = acc;
        sources.source8h = acc;
        sources.source8l = acc;
      } else if (acc->size == 16) {
        sources.source16 = acc;
        sources.source8h = acc;
        sources.source8l = acc;
      } else if (acc->size == 8) {
        if (acc->addr & 4) {
          sources.source8h = acc;
        } else {
          sources.source8l = acc;
        }
      } else {
        throw logic_error("invalid register access size");
      }

    } else if (acc->is_xmm_reg) {
      XMMRegSources& sources = this->current_xmm_reg_sources.at(acc->addr);
      if (acc->size == 128) {
        sources.source128 = acc;
        sources.source64 = acc;
        sources.source32 = acc;
      } else if (acc->size == 16) {
        sources.source64 = acc;
        sources.source32 = acc;
      } else if (acc->size == 8) {
        sources.source32 = acc;
      } else {
        throw logic_error("invalid xmm register access size");
      }

    } else { // Memory write
      size_t bytes = (acc->size >> 3);
      for (size_t x = 0; x < bytes; x++) {
        this->memory_data_sources[acc->addr + x] = acc;
      }
    }
  }

  // Clear state for the next cycle
  this->current_reads.clear();
  this->current_writes.clear();
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
T X86Emulator::exec_integer_math_logic(uint8_t what, T dest, T src) {
  switch (what) {
    case 0: // add
      return this->regs.set_flags_integer_add<T>(dest, src);
    case 1: // or
      dest |= src;
      this->regs.set_flags_bitwise_result<T>(dest);
      return dest;
    case 2: // adc
      return this->regs.set_flags_integer_add_with_carry<T>(dest, src);
    case 3: // sbb
      return this->regs.set_flags_integer_subtract_with_borrow<T>(dest, src);
    case 4: // and
      dest &= src;
      this->regs.set_flags_bitwise_result<T>(dest);
      return dest;
    case 5: // sub
      return this->regs.set_flags_integer_subtract<T>(dest, src);
    case 6: // xor
      dest ^= src;
      this->regs.set_flags_bitwise_result<T>(dest);
      return dest;
    case 7: // cmp
      this->regs.set_flags_integer_subtract<T>(dest, src);
      return dest;
    default:
      throw logic_error("invalid operation for low-opcode integer math");
  }
}

static const std::array<const char* const, 8> integer_math_opcode_names = {
    "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};

void X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math(uint8_t opcode) {
  uint8_t what = (opcode >> 3) & 7;
  DecodedRM rm = this->fetch_and_decode_rm();
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->w_ea16(rm, this->exec_integer_math_logic<uint16_t>(what, this->r_ea16(rm), this->r_non_ea16(rm)));
    } else {
      this->w_ea32(rm, this->exec_integer_math_logic<uint32_t>(what, this->r_ea32(rm), this->r_non_ea32(rm)));
    }
  } else {
    this->w_ea8(rm, this->exec_integer_math_logic<uint8_t>(what, this->r_ea8(rm), this->r_non_ea8(rm)));
  }
}

string X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math(DisassemblyState& s) {
  string opcode_name = extend(integer_math_opcode_names[(s.opcode >> 3) & 7], 10);
  DecodedRM rm = X86Emulator::fetch_and_decode_rm(s.r);
  return opcode_name + s.rm_str(rm, s.standard_operand_size(), RMF::EA_FIRST);
}

void X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math(uint8_t opcode) {
  uint8_t what = (opcode >> 3) & 7;
  DecodedRM rm = this->fetch_and_decode_rm();
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->w_non_ea16(rm, this->exec_integer_math_logic<uint16_t>(what, this->r_non_ea16(rm), this->r_ea16(rm)));
    } else {
      this->w_non_ea32(rm, this->exec_integer_math_logic<uint32_t>(what, this->r_non_ea32(rm), this->r_ea32(rm)));
    }
  } else {
    this->w_non_ea8(rm, this->exec_integer_math_logic<uint8_t>(what, this->r_non_ea8(rm), this->r_ea8(rm)));
  }
}

string X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math(DisassemblyState& s) {
  string opcode_name = extend(integer_math_opcode_names[(s.opcode >> 3) & 7], 10);
  DecodedRM rm = X86Emulator::fetch_and_decode_rm(s.r);
  return opcode_name + s.rm_str(rm, s.standard_operand_size(), 0);
}

void X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math(uint8_t opcode) {
  uint8_t what = (opcode >> 3) & 7;
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->regs.w_ax(this->exec_integer_math_logic<uint16_t>(
          what, this->regs.r_ax(), this->fetch_instruction_word()));
    } else {
      this->regs.w_eax(this->exec_integer_math_logic<uint32_t>(
          what, this->regs.r_eax(), this->fetch_instruction_dword()));
    }
  } else {
    this->regs.w_al(this->exec_integer_math_logic<uint8_t>(
        what, this->regs.r_al(), this->fetch_instruction_byte()));
  }
}

string X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math(DisassemblyState& s) {
  string opcode_name = extend(integer_math_opcode_names[(s.opcode >> 3) & 7], 10);
  uint8_t operand_size = s.standard_operand_size();
  uint32_t imm = get_operand(s.r, operand_size);
  return opcode_name + string_printf("%s, 0x%" PRIX32, name_for_reg(0, operand_size), imm);
}

void X86Emulator::exec_06_0E_16_1E_0FA0_0FA8_push_segment_reg(uint8_t) {
  throw runtime_error("segment registers are not implemented");
}

string X86Emulator::dasm_06_0E_16_1E_0FA0_0FA8_push_segment_reg(DisassemblyState& s) {
  switch (s.opcode) {
    case 0x06:
      return "push      es";
    case 0x0E:
      return "push      cs";
    case 0x16:
      return "push      ss";
    case 0x1E:
      return "push      ds";
    case 0xA0:
      return "push      fs";
    case 0xA8:
      return "push      gs";
    default:
      throw logic_error("incorrect push segment register opcode");
  }
}

void X86Emulator::exec_07_17_1F_0FA1_0FA9_pop_segment_reg(uint8_t) {
  throw runtime_error("segment registers are not implemented");
}

string X86Emulator::dasm_07_17_1F_0FA1_0FA9_pop_segment_reg(DisassemblyState& s) {
  switch (s.opcode) {
    case 0x07:
      return "pop       es";
    case 0x17:
      return "pop       ss";
    case 0x1F:
      return "pop       ds";
    case 0xA1:
      return "pop       fs";
    case 0xA9:
      return "pop       gs";
    default:
      throw logic_error("incorrect push segment register opcode");
  }
}

void X86Emulator::exec_26_es(uint8_t) {
  this->overrides.should_clear = false;
  this->overrides.segment = Segment::ES;
}

string X86Emulator::dasm_26_es(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.segment = Segment::ES;
  return "";
}

void X86Emulator::exec_27_daa(uint8_t) {
  uint8_t orig_al = this->regs.r_al();
  bool orig_cf = this->regs.read_flag(Regs::CF);

  // Note: The x86 manual says CF is written during this phase as well, but it's
  // also written in both branches of the below section, so we skip the writes
  // here.
  if (this->regs.read_flag(Regs::AF) || ((orig_al & 0x0F) > 9)) {
    uint8_t new_al = this->regs.r_al() + 6;
    this->regs.w_al(new_al);
    this->regs.replace_flag(Regs::AF, 1);
  } else {
    this->regs.replace_flag(Regs::AF, 0);
  }

  if (orig_cf || (orig_al > 0x99)) {
    uint8_t new_al = this->regs.r_al() + 0x60;
    this->regs.w_al(new_al);
    this->regs.replace_flag(Regs::CF, 1);
  } else {
    this->regs.replace_flag(Regs::CF, 0);
  }
}

string X86Emulator::dasm_27_daa(DisassemblyState&) {
  return "daa";
}

void X86Emulator::exec_2E_cs(uint8_t) {
  this->overrides.should_clear = false;
  this->overrides.segment = Segment::CS;
}

string X86Emulator::dasm_2E_cs(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.segment = Segment::CS;
  return "";
}

void X86Emulator::exec_36_ss(uint8_t) {
  this->overrides.should_clear = false;
  this->overrides.segment = Segment::SS;
}

string X86Emulator::dasm_36_ss(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.segment = Segment::SS;
  return "";
}

void X86Emulator::exec_37_aaa(uint8_t) {
  if (this->regs.read_flag(Regs::AF) || ((this->regs.r_al() & 0x0F) > 9)) {
    this->regs.w_al(this->regs.r_al() + 0x06);
    this->regs.w_ah(this->regs.r_ah() + 0x01);
    this->regs.replace_flag(Regs::AF, true);
    this->regs.replace_flag(Regs::CF, true);
  } else {
    this->regs.replace_flag(Regs::AF, false);
    this->regs.replace_flag(Regs::CF, false);
  }
  this->regs.w_al(this->regs.r_al() & 0x0F);
}

string X86Emulator::dasm_37_aaa(DisassemblyState&) {
  return "aaa";
}

void X86Emulator::exec_3E_ds(uint8_t) {
  this->overrides.should_clear = false;
  this->overrides.segment = Segment::DS;
}

string X86Emulator::dasm_3E_ds(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.segment = Segment::DS;
  return "";
}

void X86Emulator::exec_40_to_47_inc(uint8_t opcode) {
  uint8_t which = opcode & 7;
  if (this->overrides.operand_size) {
    this->regs.write16(which, this->regs.set_flags_integer_add<uint16_t>(this->regs.read16(which), 1, ~Regs::CF));
  } else {
    this->regs.write32(which, this->regs.set_flags_integer_add<uint32_t>(this->regs.read32(which), 1, ~Regs::CF));
  }
}

void X86Emulator::exec_48_to_4F_dec(uint8_t opcode) {
  uint8_t which = opcode & 7;
  if (this->overrides.operand_size) {
    this->regs.write16(which, this->regs.set_flags_integer_subtract<uint16_t>(this->regs.read16(which), 1, ~Regs::CF));
  } else {
    this->regs.write32(which, this->regs.set_flags_integer_subtract<uint32_t>(this->regs.read32(which), 1, ~Regs::CF));
  }
}

string X86Emulator::dasm_40_to_4F_inc_dec(DisassemblyState& s) {
  return string_printf("%s       %s",
      (s.opcode & 8) ? "dec" : "inc",
      name_for_reg(s.opcode & 7, s.overrides.operand_size ? 16 : 32));
}

void X86Emulator::exec_50_to_57_push(uint8_t opcode) {
  uint8_t which = opcode & 7;
  if (this->overrides.operand_size) {
    this->push<le_uint16_t>(this->regs.read16(which));
  } else {
    this->push<le_uint32_t>(this->regs.read32(which));
  }
}

void X86Emulator::exec_58_to_5F_pop(uint8_t opcode) {
  uint8_t which = opcode & 7;
  if (this->overrides.operand_size) {
    this->regs.write16(which, this->pop<le_uint16_t>());
  } else {
    this->regs.write32(which, this->pop<le_uint32_t>());
  }
}

string X86Emulator::dasm_50_to_5F_push_pop(DisassemblyState& s) {
  return string_printf("%s      %s",
      (s.opcode & 8) ? "pop " : "push",
      name_for_reg(s.opcode & 7, s.overrides.operand_size ? 16 : 32));
}

void X86Emulator::exec_60_pusha(uint8_t) {
  uint32_t original_esp = this->regs.r_esp();
  if (this->overrides.operand_size) {
    this->push<le_uint16_t>(this->regs.r_ax());
    this->push<le_uint16_t>(this->regs.r_cx());
    this->push<le_uint16_t>(this->regs.r_dx());
    this->push<le_uint16_t>(this->regs.r_bx());
    this->push<le_uint16_t>(original_esp & 0xFFFF);
    this->push<le_uint16_t>(this->regs.r_bp());
    this->push<le_uint16_t>(this->regs.r_si());
    this->push<le_uint16_t>(this->regs.r_di());
  } else {
    this->push<le_uint32_t>(this->regs.r_eax());
    this->push<le_uint32_t>(this->regs.r_ecx());
    this->push<le_uint32_t>(this->regs.r_edx());
    this->push<le_uint32_t>(this->regs.r_ebx());
    this->push<le_uint32_t>(original_esp);
    this->push<le_uint32_t>(this->regs.r_ebp());
    this->push<le_uint32_t>(this->regs.r_esi());
    this->push<le_uint32_t>(this->regs.r_edi());
  }
}

string X86Emulator::dasm_60_pusha(DisassemblyState& s) {
  uint32_t operand_size = s.overrides.operand_size ? 0x80 : 0x100;
  return (s.overrides.operand_size ? "pusha" : "pushad") +
      s.annotation_for_rm_ea(DecodedRM(4, -operand_size), operand_size);
}

void X86Emulator::exec_61_popa(uint8_t) {
  if (this->overrides.operand_size) {
    this->regs.w_ax(this->pop<le_uint16_t>());
    this->regs.w_cx(this->pop<le_uint16_t>());
    this->regs.w_dx(this->pop<le_uint16_t>());
    this->regs.w_bx(this->pop<le_uint16_t>());
    this->regs.w_sp(this->regs.r_sp() + 2);
    this->regs.w_bp(this->pop<le_uint16_t>());
    this->regs.w_si(this->pop<le_uint16_t>());
    this->regs.w_di(this->pop<le_uint16_t>());
  } else {
    this->regs.w_eax(this->pop<le_uint32_t>());
    this->regs.w_ecx(this->pop<le_uint32_t>());
    this->regs.w_edx(this->pop<le_uint32_t>());
    this->regs.w_ebx(this->pop<le_uint32_t>());
    this->regs.w_esp(this->regs.r_esp() + 4);
    this->regs.w_ebp(this->pop<le_uint32_t>());
    this->regs.w_esi(this->pop<le_uint32_t>());
    this->regs.w_edi(this->pop<le_uint32_t>());
  }
}

string X86Emulator::dasm_61_popa(DisassemblyState& s) {
  uint32_t operand_size = s.overrides.operand_size ? 0x80 : 0x100;
  return (s.overrides.operand_size ? "popa" : "popad") +
      s.annotation_for_rm_ea(DecodedRM(4, 0), operand_size);
}

void X86Emulator::exec_64_fs(uint8_t) {
  this->overrides.should_clear = false;
  this->overrides.segment = Segment::FS;
}

string X86Emulator::dasm_64_fs(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.segment = Segment::FS;
  return "";
}

void X86Emulator::exec_65_gs(uint8_t) {
  this->overrides.should_clear = false;
  this->overrides.segment = Segment::GS;
}

string X86Emulator::dasm_65_gs(DisassemblyState& s) {
  s.overrides.should_clear = false;
  s.overrides.segment = Segment::GS;
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

void X86Emulator::exec_68_6A_push(uint8_t opcode) {
  // Unlike most opcodes, these are switched - the higher code is the 8-bit one
  if (opcode & 2) {
    this->push<le_uint32_t>(sign_extend<uint32_t, uint8_t>(this->fetch_instruction_byte()));
  } else if (this->overrides.operand_size) {
    this->push<le_uint16_t>(this->fetch_instruction_word());
  } else {
    this->push<le_uint32_t>(this->fetch_instruction_dword());
  }
}

string X86Emulator::dasm_68_6A_push(DisassemblyState& s) {
  string annotation;
  if (s.opcode & 2) {
    return string_printf("push      0x%02" PRIX32, sign_extend<uint32_t, uint8_t>(s.r.get_u8()));
  } else if (s.overrides.operand_size) {
    return string_printf("push      0x%04" PRIX32, sign_extend<uint32_t, uint8_t>(s.r.get_u16l()));
  } else {
    return string_printf("push      0x%08" PRIX32, s.r.get_u32l()) + s.annotation_for_rm_ea(DecodedRM(4, -4), 32);
  }
}

void X86Emulator::exec_69_6B_imul(uint8_t) {
  this->fetch_and_decode_rm();
  throw runtime_error("unimplemented opcode: imul r16/32, r/m16/32, imm");
}

string X86Emulator::dasm_69_6B_imul(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  uint32_t imm;
  if (s.opcode & 2) {
    imm = s.r.get_u8();
  } else if (s.overrides.operand_size) {
    imm = s.r.get_u16l();
  } else {
    imm = s.r.get_u32l();
  }

  uint8_t operand_size = s.overrides.operand_size ? 16 : 32;
  return "imul      " + s.rm_str(rm, operand_size, 0) + string_printf(", 0x%" PRIX32, imm);
}

void X86Emulator::exec_70_to_7F_jcc(uint8_t opcode) {
  // Always read the offset even if the condition is false, so we don't try to
  // execute the offset as code immediately after.
  uint32_t offset = sign_extend<uint32_t, uint8_t>(this->fetch_instruction_byte());
  if (this->regs.check_condition(opcode & 0x0F)) {
    this->regs.eip += offset;
  }
}

string X86Emulator::dasm_70_to_7F_jcc(DisassemblyState& s) {
  string opcode_name = "j";
  opcode_name += name_for_condition_code[s.opcode & 0x0F];
  opcode_name.resize(10, ' ');

  uint32_t offset = sign_extend<uint32_t, uint8_t>(s.r.get_u8());
  uint32_t dest = s.start_address + s.r.where() + offset;
  s.branch_target_addresses.emplace(dest, false);
  return opcode_name + string_printf("0x%08" PRIX32, dest) + s.annotation_for_rm_ea(DecodedRM(-1, dest), -1);
}

void X86Emulator::exec_80_to_83_imm_math(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      uint16_t v = (opcode & 2)
          ? sign_extend<uint16_t, uint8_t>(this->fetch_instruction_byte())
          : this->fetch_instruction_word();
      this->w_ea16(rm, this->exec_integer_math_logic<uint16_t>(rm.non_ea_reg, this->r_ea16(rm), v));

    } else {
      uint32_t v = (opcode & 2)
          ? sign_extend<uint32_t, uint8_t>(this->fetch_instruction_byte())
          : this->fetch_instruction_dword();
      this->w_ea32(rm, this->exec_integer_math_logic<uint32_t>(rm.non_ea_reg, this->r_ea32(rm), v));
    }
  } else {
    // It looks like 82 is actually identical to 80. Is this true?
    uint8_t v = this->fetch_instruction_byte();
    this->w_ea8(rm, this->exec_integer_math_logic<uint8_t>(rm.non_ea_reg, this->r_ea8(rm), v));
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
      return opcode_name + s.rm_ea_str(rm, 16, 0) + string_printf(", 0x%" PRIX16, imm);

    } else {
      uint32_t imm = (s.opcode & 2)
          ? sign_extend<uint32_t, uint8_t>(s.r.get_u8())
          : s.r.get_u32l();
      return opcode_name + s.rm_ea_str(rm, 32, 0) + string_printf(", 0x%" PRIX32, imm);
    }
  } else {
    // It looks like 82 is actually identical to 80. Is this true?
    uint8_t imm = s.r.get_u8();
    return opcode_name + s.rm_ea_str(rm, 8, 0) + string_printf(", 0x%" PRIX8, imm);
  }
}

void X86Emulator::exec_84_85_test_rm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->regs.set_flags_bitwise_result<uint16_t>(
          this->r_non_ea16(rm) & this->r_ea16(rm));
    } else {
      this->regs.set_flags_bitwise_result<uint32_t>(
          this->r_non_ea32(rm) & this->r_ea32(rm));
    }
  } else {
    this->regs.set_flags_bitwise_result<uint8_t>(
        this->r_non_ea8(rm) & this->r_ea8(rm));
  }
}

string X86Emulator::dasm_84_85_test_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return "test      " + s.rm_str(rm, s.standard_operand_size(), RMF::EA_FIRST);
}

void X86Emulator::exec_86_87_xchg_rm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      uint16_t a = this->r_non_ea16(rm);
      this->w_non_ea16(rm, this->r_ea16(rm));
      this->w_ea16(rm, a);
    } else {
      uint32_t a = this->r_non_ea32(rm);
      this->w_non_ea32(rm, this->r_ea32(rm));
      this->w_ea32(rm, a);
    }
  } else {
    uint8_t a = this->r_non_ea8(rm);
    this->w_non_ea8(rm, this->r_ea8(rm));
    this->w_ea8(rm, a);
  }
}

string X86Emulator::dasm_86_87_xchg_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return "xchg      " + s.rm_str(rm, s.standard_operand_size(), RMF::EA_FIRST);
}

void X86Emulator::exec_88_to_8B_mov_rm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      if (opcode & 2) {
        this->w_non_ea16(rm, this->r_ea16(rm));
      } else {
        this->w_ea16(rm, this->r_non_ea16(rm));
      }
    } else {
      if (opcode & 2) {
        this->w_non_ea32(rm, this->r_ea32(rm));
      } else {
        this->w_ea32(rm, this->r_non_ea32(rm));
      }
    }
  } else {
    if (opcode & 2) {
      this->w_non_ea8(rm, this->r_ea8(rm));
    } else {
      this->w_ea8(rm, this->r_non_ea8(rm));
    }
  }
}

string X86Emulator::dasm_88_to_8B_mov_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return "mov       " + s.rm_str(rm, s.standard_operand_size(), (s.opcode & 2) ? 0 : RMF::EA_FIRST);
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
  this->w_non_ea32(rm, this->resolve_mem_ea(rm, true));
}

string X86Emulator::dasm_8D_lea(DisassemblyState& s) {
  if (s.overrides.operand_size || s.overrides.address_size) {
    return ".unknown  <<lea+override>> // unimplemented";
  }
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  if (rm.ea_index_scale < 0) {
    return ".invalid  <<lea with non-memory reference>>";
  }
  return "lea       " + s.rm_str(rm, 32, RMF::SUPPRESS_OPERAND_SIZE | RMF::SUPPRESS_ADDRESS_TOKEN);
}

void X86Emulator::exec_8F_pop_rm(uint8_t) {
  // TODO: pop [esp] and pop [esp+...] may have special considerations here,
  // e.g. the EA should be computed after esp has been incremented. Check the
  // docs and implement these behaviors correctly.
  auto rm = this->fetch_and_decode_rm();
  if (rm.non_ea_reg) {
    throw runtime_error("invalid pop r/m with non_ea_reg != 0");
  }

  if (this->overrides.operand_size) {
    this->w_ea16(rm, this->pop<le_uint16_t>());
  } else {
    this->w_ea32(rm, this->pop<le_uint32_t>());
  }
}

string X86Emulator::dasm_8F_pop_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  if (rm.non_ea_reg) {
    return ".invalid  <<pop r/m with non_ea_reg != 0>>";
  }
  uint8_t operand_size = s.overrides.operand_size ? 16 : 32;
  return "pop       " + s.rm_ea_str(rm, operand_size, 0) +
      s.annotation_for_rm_ea(DecodedRM(4, 0), operand_size);
}

void X86Emulator::exec_90_to_97_xchg_eax(uint8_t opcode) {
  if (opcode == 0x90) {
    return; // nop
  }

  uint8_t which = opcode & 7;
  if (this->overrides.operand_size) {
    uint16_t a = this->regs.r_ax();
    this->regs.w_ax(this->regs.read16(which));
    this->regs.write16(which, a);
  } else {
    uint32_t a = this->regs.r_eax();
    this->regs.w_eax(this->regs.read32(which));
    this->regs.write32(which, a);
  }
}

string X86Emulator::dasm_90_to_97_xchg_eax(DisassemblyState& s) {
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
    this->regs.w_ah((this->regs.r_al() & 0x80) ? 0xFF : 0x00);
  } else {
    uint32_t a = this->regs.r_eax();
    if (a & 0x00008000) {
      this->regs.w_eax(a | 0xFFFF0000);
    } else {
      this->regs.w_eax(a & 0x0000FFFF);
    }
  }
}

string X86Emulator::dasm_98_cbw_cwde(DisassemblyState& s) {
  return s.overrides.operand_size ? "cbw" : "cwde";
}

void X86Emulator::exec_99_cwd_cdq(uint8_t) {
  if (this->overrides.operand_size) {
    this->regs.w_dx((this->regs.r_ax() & 0x8000) ? 0xFFFF : 0x0000);
  } else {
    this->regs.w_edx((this->regs.r_eax() & 0x80000000) ? 0xFFFFFFFF : 0x00000000);
  }
}

string X86Emulator::dasm_99_cwd_cdq(DisassemblyState& s) {
  return s.overrides.operand_size ? "cwd" : "cdq";
}

void X86Emulator::exec_9C_pushf_pushfd(uint8_t) {
  if (this->overrides.operand_size) {
    this->push<le_uint16_t>(this->regs.read_eflags() & 0xFFFF);
  } else {
    // Mask out the RF and VM bits
    this->push<le_uint32_t>(this->regs.read_eflags() & 0x00FCFFFF);
  }
}

string X86Emulator::dasm_9C_pushf_pushfd(DisassemblyState& s) {
  uint8_t operand_size = s.overrides.operand_size ? 16 : 32;
  return (s.overrides.operand_size ? "pushf    " : "pushfd   ") +
      s.annotation_for_rm_ea(DecodedRM(4, -operand_size), operand_size);
}

void X86Emulator::exec_9D_popf_popfd(uint8_t) {
  if (this->overrides.operand_size) {
    static constexpr uint32_t mask = 0x00004DD5;
    this->regs.write_eflags((this->regs.read_eflags() & ~mask) | (this->pop<le_uint16_t>() & mask));
  } else {
    static constexpr uint32_t mask = 0x00244DD5;
    this->regs.write_eflags((this->regs.read_eflags() & ~mask) | (this->pop<le_uint32_t>() & mask));
  }
  this->regs.replace_flag(0x00010000, false); // clear RF
}

string X86Emulator::dasm_9D_popf_popfd(DisassemblyState& s) {
  uint8_t operand_size = s.overrides.operand_size ? 16 : 32;
  return (s.overrides.operand_size ? "popf     " : "popfd    ") +
      s.annotation_for_rm_ea(DecodedRM(4, 0), operand_size);
}

void X86Emulator::exec_9F_lahf(uint8_t) {
  // Mask out bits that are always 0 in FLAGS, and set the reserved bit (2)
  this->regs.w_ah((this->regs.read_eflags() & 0xD5) | 2);
}

string X86Emulator::dasm_9F_lahf(DisassemblyState&) {
  return "lahf";
}

void X86Emulator::exec_A0_A1_A2_A3_mov_eax_memabs(uint8_t opcode) {
  uint32_t addr = this->fetch_instruction_dword();
  if (!(opcode & 1)) {
    if (opcode & 2) {
      this->w_mem<uint8_t>(addr, this->regs.r_al());
    } else {
      this->regs.w_al(this->r_mem<uint8_t>(addr));
    }
  } else if (this->overrides.operand_size) {
    if (opcode & 2) {
      this->w_mem<le_uint16_t>(addr, this->regs.r_ax());
    } else {
      this->regs.w_ax(this->r_mem<le_uint16_t>(addr));
    }
  } else {
    if (opcode & 2) {
      this->w_mem<le_uint32_t>(addr, this->regs.r_eax());
    } else {
      this->regs.w_eax(this->r_mem<le_uint32_t>(addr));
    }
  }
}

string X86Emulator::dasm_A0_A1_A2_A3_mov_eax_memabs(DisassemblyState& s) {
  uint32_t addr = s.r.get_u32l();
  const char* seg_name = s.overrides.overridden_segment_name();
  string mem_str;
  if (seg_name) {
    mem_str = string_printf("%s:[0x%08" PRIX32 "]", seg_name, addr);
  } else {
    mem_str = string_printf("[0x%08" PRIX32 "]", addr);
  }

  string reg_str;
  uint8_t operand_size;
  if (!(s.opcode & 1)) {
    reg_str = "al";
    operand_size = 8;
  } else if (s.overrides.operand_size) {
    reg_str = "ax";
    operand_size = 16;
  } else {
    reg_str = "eax";
    operand_size = 32;
  }

  if (s.opcode & 2) {
    return "mov       " + mem_str + ", " + reg_str + s.annotation_for_rm_ea(DecodedRM(-1, addr), operand_size);
  } else {
    return "mov       " + reg_str + ", " + mem_str + s.annotation_for_rm_ea(DecodedRM(-1, addr), operand_size);
  }
}

template <typename T, typename LET>
void X86Emulator::exec_string_op_logic(uint8_t opcode) {
  // Note: We ignore the segment registers here. Technically we should be
  // reading from ds:esi (ds may be overridden by another prefix) and writing to
  // es:edi (es may NOT be overridden). But on modern OSes, these segment
  // registers point to the same location in protected mode, so we ignore them.
  // TODO: Properly handle the case where the override segment is FS (this is
  // probably extremely rare)

  // BYTES = OPCODE = [EDI] = [ESI] = EQUIVALENT INSTRUCTION
  // A4/A5 = movs   = write = read  = mov es:[edi], ds:[esi]
  // A6/A7 = cmps   = read  = read  = cmp ds:[esi], es:[edi]
  // AA/AB = stos   = write =       = mov es:[edi], al/ax/eax
  // AC/AD = lods   =       = read  = mov al/ax/eax, ds:[esi]
  // AE/AF = scas   = read  =       = cmp al/ax/eax, es:[edi] (yes, edi)

  uint32_t edi_delta = this->regs.read_flag(Regs::DF) ? static_cast<uint32_t>(-sizeof(T)) : sizeof(T);
  uint32_t esi_delta = edi_delta;

  uint8_t what = (opcode & 0x0E);
  switch (what) {
    case 0x04: // movs
      this->w_mem<LET>(this->regs.r_edi(), this->r_mem<LET>(this->regs.r_esi()));
      break;
    case 0x06: // cmps
      this->regs.set_flags_integer_subtract<T>(
          this->r_mem<LET>(this->regs.r_esi()),
          this->r_mem<LET>(this->regs.r_edi()));
      break;
    case 0x0A: // stos
      this->w_mem<LET>(this->regs.r_edi(), this->regs.r_eax());
      esi_delta = 0;
      break;
    case 0x0C: { // lods
      uint64_t mask = (1ULL << bits_for_type<T>)-1;
      uint64_t prev_eax = this->regs.r_eax();
      uint64_t value = this->r_mem<LET>(this->regs.r_esi());
      this->regs.w_eax((prev_eax & (~mask)) | (value & mask));
      edi_delta = 0;
      break;
    }
    case 0x0E: { // scas
      uint64_t mask = (1ULL << bits_for_type<T>)-1;
      uint64_t eax = this->regs.r_eax();
      uint64_t value = this->r_mem<LET>(this->regs.r_edi());
      this->regs.set_flags_integer_subtract<T>(eax & mask, value & mask);
      esi_delta = 0;
      break;
    }
    default:
      throw logic_error("unhandled string opcode");
  }

  if (edi_delta) {
    this->regs.w_edi(this->regs.r_edi() + edi_delta);
  }
  if (esi_delta) {
    this->regs.w_esi(this->regs.r_esi() + esi_delta);
  }
}

template <typename T, typename LET>
void X86Emulator::exec_rep_string_op_logic(uint8_t opcode) {
  if ((opcode & 0x06) == 6) { // cmps or scas
    bool expected_zf = this->overrides.repeat_z ? true : false;
    // Note: We don't need to explicitly report the flags access here because
    // exec_string_op_inner accesses DF and reports flags access there
    for (;
         this->regs.r_ecx() && this->regs.read_flag(Regs::ZF) == expected_zf;
         this->regs.w_ecx(this->regs.r_ecx() - 1)) {
      this->exec_string_op_logic<T, LET>(opcode);
      // Note: We manually link accesses during this opcode's execution because
      // we could be copying a large amount of data, and it would be incorrect
      // to link each source byte to all destination bytes.
      this->link_current_accesses();
    }
  } else {
    if (this->overrides.repeat_nz) {
      throw runtime_error("invalid repne prefix on string operation");
    }
    for (; this->regs.r_ecx(); this->regs.w_ecx(this->regs.r_ecx() - 1)) {
      this->exec_string_op_logic<T, LET>(opcode);
      this->link_current_accesses();
    }
  }
}

void X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops(uint8_t opcode) {
  if (this->overrides.address_size) {
    throw runtime_error("string op with overridden address size is not implemented");
  }

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      if (this->overrides.repeat_nz || this->overrides.repeat_z) {
        this->exec_rep_string_op_logic<uint16_t>(opcode);
      } else {
        this->exec_string_op_logic<uint16_t>(opcode);
      }
    } else {
      if (this->overrides.repeat_nz || this->overrides.repeat_z) {
        this->exec_rep_string_op_logic<uint32_t>(opcode);
      } else {
        this->exec_string_op_logic<uint32_t>(opcode);
      }
    }
  } else {
    if (this->overrides.repeat_nz || this->overrides.repeat_z) {
      this->exec_rep_string_op_logic<uint8_t>(opcode);
    } else {
      this->exec_string_op_logic<uint8_t>(opcode);
    }
  }
}

string X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops(DisassemblyState& s) {
  if (s.overrides.address_size) {
    return ".unknown  <<movs with overridden address size>> // unimplemented";
  }

  const char* src_segment_name = s.overrides.overridden_segment_name();
  if (!src_segment_name) {
    src_segment_name = "ds";
  }

  string prefix;
  if ((s.opcode & 6) == 6) { // cmps or scas
    if (s.overrides.repeat_z) {
      prefix += "repz ";
    } else if (s.overrides.repeat_nz) {
      prefix += "repnz ";
    }
  } else {
    if (s.overrides.repeat_z || s.overrides.repeat_nz) {
      prefix += "rep ";
    }
  }

  static const char* opcode_names[8] = {
      nullptr, nullptr, "movs", "cmps", nullptr, "stos", "lods", "scas"};
  prefix += opcode_names[(s.opcode >> 1) & 7];
  prefix.resize(10, ' ');
  if (prefix[prefix.size() - 1] != ' ') {
    prefix += ' ';
  }

  const char* a_reg_name;
  uint8_t operand_size;
  if (!(s.opcode & 1)) {
    prefix += "byte ";
    a_reg_name = "al";
    operand_size = 8;
  } else if (s.overrides.operand_size) {
    prefix += "word ";
    a_reg_name = "ax";
    operand_size = 16;
  } else {
    prefix += "dword ";
    a_reg_name = "eax";
    operand_size = 32;
  }

  switch ((s.opcode >> 1) & 7) {
    case 2: // movs
      return prefix + string_printf("es:[edi], %s:[esi]", src_segment_name) +
          s.annotation_for_rm_ea(DecodedRM(7, 0), operand_size) +
          s.annotation_for_rm_ea(DecodedRM(6, 0), operand_size);
    case 3: // cmps
      return prefix + string_printf("%s:[esi], es:[edi]", src_segment_name) +
          s.annotation_for_rm_ea(DecodedRM(6, 0), operand_size) +
          s.annotation_for_rm_ea(DecodedRM(7, 0), operand_size);
    case 5: // stos
      return prefix + string_printf("es:[edi], %s", a_reg_name) +
          s.annotation_for_rm_ea(DecodedRM(7, 0), operand_size);
    case 6: // lods
      return prefix + string_printf("%s, %s:[esi]", a_reg_name, src_segment_name) +
          s.annotation_for_rm_ea(DecodedRM(6, 0), operand_size);
    case 7: // scas
      return prefix + string_printf("%s, es:[edi]", a_reg_name) +
          s.annotation_for_rm_ea(DecodedRM(7, 0), operand_size);
    default:
      throw logic_error("string op disassembler called for non-string op");
  }
}

void X86Emulator::exec_A8_A9_test_eax_imm(uint8_t opcode) {
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      uint16_t v = this->fetch_instruction_word();
      this->regs.set_flags_bitwise_result<uint16_t>(this->regs.r_ax() & v);
    } else {
      uint32_t v = this->fetch_instruction_dword();
      this->regs.set_flags_bitwise_result<uint32_t>(this->regs.r_eax() & v);
    }
  } else {
    uint8_t v = this->fetch_instruction_byte();
    this->regs.set_flags_bitwise_result<uint8_t>(this->regs.r_al() & v);
  }
}

string X86Emulator::dasm_A8_A9_test_eax_imm(DisassemblyState& s) {
  if (s.opcode & 1) {
    if (s.overrides.operand_size) {
      return string_printf("test      ax, 0x%04" PRIX16, s.r.get_u16l());
    } else {
      return string_printf("test      eax, 0x%08" PRIX32, s.r.get_u32l());
    }
  } else {
    return string_printf("test      al, 0x%02" PRIX8, s.r.get_u8());
  }
}

void X86Emulator::exec_B0_to_BF_mov_imm(uint8_t opcode) {
  uint8_t which = opcode & 7;
  if (opcode & 8) {
    if (this->overrides.operand_size) {
      this->regs.write16(which, this->fetch_instruction_word());
    } else {
      this->regs.write32(which, this->fetch_instruction_dword());
    }
  } else {
    this->regs.write8(which, this->fetch_instruction_byte());
  }
}

string X86Emulator::dasm_B0_to_BF_mov_imm(DisassemblyState& s) {
  if (s.opcode & 8) {
    if (s.overrides.operand_size) {
      return string_printf("mov       %s, 0x%04" PRIX16,
          name_for_reg(s.opcode & 7, 16), s.r.get_u16l());
    } else {
      return string_printf("mov       %s, 0x%08" PRIX32,
          name_for_reg(s.opcode & 7, 32), s.r.get_u32l());
    }
  } else {
    return string_printf("mov       %s, 0x%02" PRIX8,
        name_for_reg(s.opcode & 7, 8), s.r.get_u8());
  }
}

template <typename T>
T X86Emulator::exec_bit_shifts_logic(
    uint8_t what,
    T value,
    uint8_t distance,
    bool distance_is_cl) {
  switch (what) {
    case 0: // rol
    case 1: // ror
      // Note: The x86 manual says if size=8 or size=16, then the distance is
      // ANDed with 0x1F, then MOD'ed by 8 or 16. Even though this is logically
      // the same as ANDing with a smaller mask, the AND result is used for
      // checking if a shift needs to be done at all (and flags should be
      // modified), and then the MOD result is used to actually do the shift.
      // This means that, for example, when rotating a 16-bit register by 16
      // bits, the register's value is unchanged but CF SHOULD be overwritten
      // (and maybe OF too, depending on whcih undefined behavior we're doing).
      distance &= 0x1F;
      if (distance) {
        uint8_t shift_distance = distance & (bits_for_type<T> - 1);
        value = what
            ? (value >> shift_distance) | (value << (bits_for_type<T> - shift_distance))
            : (value << shift_distance) | (value >> (bits_for_type<T> - shift_distance));
        // The Windows ARM emulator has some odd behavior with the CF and OF
        // flags here which doesn't seem to conform to the manual. Specifically,
        // it doesn't set CF if the distance is immediate (not from cl) and the
        // shift distance is zero (which can happen when e.g. shifting a 16-bit
        // register by 0x10).
        if (this->behavior != Behavior::WINDOWS_ARM_EMULATOR || distance_is_cl || (shift_distance != 0)) {
          this->regs.replace_flag(Regs::CF,
              what ? (!!(value & msb_for_type<T>)) : (value & 1));
        }
        if ((shift_distance == 1) ||
            (distance != 0 && this->behavior == Behavior::WINDOWS_ARM_EMULATOR && distance_is_cl)) {
          this->regs.replace_flag(Regs::OF, what ? (!!((value ^ (value << 1)) & msb_for_type<T>)) : (((value >> (bits_for_type<T> - 1)) ^ value) & 1));
        }
      }
      break;
    case 2: // rcl
    case 3: { // rcr
      bool is_rcr = (what & 1);
      bool cf = this->regs.read_flag(Regs::CF);
      distance &= 0x1F;
      uint8_t shift_distance = distance % (bits_for_type<T> + 1);
      if (is_rcr && ((shift_distance == 1) || (distance != 0 && this->behavior == Behavior::WINDOWS_ARM_EMULATOR && distance_is_cl))) {
        this->regs.replace_flag(Regs::OF, (!!(value & msb_for_type<T>) != cf));
      }
      for (uint8_t c = shift_distance; c; c--) {
        bool temp_cf = is_rcr ? (value & 1) : (!!(value & msb_for_type<T>));
        value = is_rcr
            ? ((value >> 1) | (cf << (bits_for_type<T> - 1)))
            : ((value << 1) | cf);
        cf = temp_cf;
      }
      this->regs.replace_flag(Regs::CF, cf);
      if (!is_rcr &&
          ((shift_distance == 1) ||
              (distance != 0 && this->behavior == Behavior::WINDOWS_ARM_EMULATOR && distance_is_cl))) {
        this->regs.replace_flag(Regs::OF, (!!(value & msb_for_type<T>) != cf));
      }
      break;
    }
    case 4: // shl/sal
    case 5: // shr
    case 6: // sal/shl
    case 7: { // sar
      bool is_right_shift = (what & 1);
      bool is_signed = (what & 2);
      bool cf = this->regs.read_flag(Regs::CF);
      T orig_value = value;
      uint8_t shift_distance = distance & 0x1F;
      for (uint8_t c = shift_distance; c; c--) {
        if (!is_right_shift) {
          cf = !!(value & msb_for_type<T>);
          value <<= 1;
        } else {
          cf = value & 1;
          value >>= 1;
          if (is_signed && (value & ((msb_for_type<T>) >> 1))) {
            value |= msb_for_type<T>;
          }
        }
      }
      this->regs.replace_flag(Regs::CF, cf);
      // If the distance came from cl, the Windows ARM emulator writes OF if the
      // distance is nonzero. But if the distance didn't come from cl, it writes
      // different values (below).
      if ((shift_distance == 1) ||
          (shift_distance != 0 && this->behavior == Behavior::WINDOWS_ARM_EMULATOR && distance_is_cl)) {
        if (!is_right_shift) {
          this->regs.replace_flag(Regs::OF, !!(value & msb_for_type<T>) != cf);
        } else {
          if (is_signed) {
            this->regs.replace_flag(Regs::OF, false);
          } else {
            this->regs.replace_flag(Regs::OF, !!(orig_value & msb_for_type<T>));
          }
        }
      } else if (shift_distance != 0 && this->behavior == Behavior::WINDOWS_ARM_EMULATOR && !distance_is_cl) {
        if (!is_right_shift) {
          this->regs.replace_flag(Regs::OF, !!(value & msb_for_type<T>) != cf);
        } else {
          this->regs.replace_flag(Regs::OF, false);
        }
      }
      if (distance & 0x1F) {
        this->regs.set_flags_integer_result<T>(value);
      }
      // Technically AF is undefined here. We just leave it alone.
      break;
    }
    default:
      throw logic_error("non_ea_reg is not valid");
  }
  return value;
}

static const std::array<const char* const, 8> bit_shift_opcode_names = {
    "rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar"};

void X86Emulator::exec_C0_C1_bit_shifts(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  uint8_t distance = this->fetch_instruction_byte();

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->w_ea16(rm, this->exec_bit_shifts_logic<uint16_t>(rm.non_ea_reg, this->r_ea16(rm), distance, false));
    } else {
      this->w_ea32(rm, this->exec_bit_shifts_logic<uint32_t>(rm.non_ea_reg, this->r_ea32(rm), distance, false));
    }
  } else {
    this->w_ea8(rm, this->exec_bit_shifts_logic<uint8_t>(rm.non_ea_reg, this->r_ea8(rm), distance, false));
  }
}

string X86Emulator::dasm_C0_C1_bit_shifts(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  uint8_t distance = s.r.get_u8();
  return extend(bit_shift_opcode_names[rm.non_ea_reg], 10) + s.rm_ea_str(rm, s.standard_operand_size(), 0) + string_printf(", 0x%02" PRIX8, distance);
}

void X86Emulator::exec_C2_C3_CA_CB_ret(uint8_t opcode) {
  if (opcode & 8) {
    throw runtime_error("far return is not implemented");
  }
  uint32_t new_eip = this->pop<le_uint32_t>();
  if (!(opcode & 1)) {
    this->regs.w_esp(this->regs.r_esp() + this->fetch_instruction_word());
  }
  this->regs.eip = new_eip;
}

string X86Emulator::dasm_C2_C3_CA_CB_ret(DisassemblyState& s) {
  char far_ch = (s.opcode & 8) ? 'f' : ' ';
  if (s.opcode & 1) {
    return string_printf("ret%c      ", far_ch) + s.annotation_for_rm_ea(DecodedRM(4, 0), 32);
  } else {
    return string_printf("ret%c      0x%04" PRIX16, far_ch, s.r.get_u16l()) +
        s.annotation_for_rm_ea(DecodedRM(4, 0), 32);
  }
}

void X86Emulator::exec_C6_C7_mov_rm_imm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  if (rm.non_ea_reg != 0) {
    throw runtime_error("invalid mov r/m, imm with non_ea_reg != 0");
  }

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->w_ea16(rm, this->fetch_instruction_word());
    } else {
      this->w_ea32(rm, this->fetch_instruction_dword());
    }
  } else {
    this->w_ea8(rm, this->fetch_instruction_byte());
  }
}

string X86Emulator::dasm_C6_C7_mov_rm_imm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  if (rm.non_ea_reg != 0) {
    return ".invalid  <<mov r/m, imm with non_ea_reg != 0>>";
  }

  uint8_t operand_size = s.standard_operand_size();
  return "mov       " + s.rm_ea_str(rm, operand_size, 0) + string_printf(", 0x%" PRIX32, get_operand(s.r, operand_size));
}

void X86Emulator::exec_C8_enter(uint8_t) {
  uint16_t size = this->fetch_instruction_word();
  uint8_t nest_level = this->fetch_instruction_byte();
  // TODO: Be unlazy and write this opcode
  throw runtime_error(string_printf("unimplemented opcode: enter 0x%04hX 0x%02hhX", size, nest_level));
}

string X86Emulator::dasm_C8_enter(DisassemblyState& s) {
  uint16_t size = s.r.get_u16l();
  uint8_t nest_level = s.r.get_u8();
  return string_printf("enter     0x%04hX, 0x%02hhX", size, nest_level);
}

void X86Emulator::exec_C9_leave(uint8_t) {
  this->regs.w_esp(this->regs.r_ebp());
  this->regs.w_ebp(this->overrides.operand_size
          ? this->pop<le_uint16_t>()
          : this->pop<le_uint32_t>());
}

string X86Emulator::dasm_C9_leave(DisassemblyState&) {
  // TODO: Add annotations for ESP reads here
  return "leave";
}

void X86Emulator::exec_CC_CD_int(uint8_t opcode) {
  uint8_t int_num = (opcode & 1) ? this->fetch_instruction_byte() : 3;
  if (this->syscall_handler) {
    this->syscall_handler(*this, int_num);
  } else {
    this->exec_unimplemented(opcode);
  }
}

string X86Emulator::dasm_CC_CD_int(DisassemblyState& s) {
  if (!(s.opcode & 1)) {
    return "int       03";
  } else {
    uint8_t int_num = s.r.get_u8();
    if (int_num == 3) {
      // The manual says that this form has some behavior differences from
      // opcode CC, so we comment on it if we see it. These differences don't
      // seem relevant for this emulator's purposes, though.
      return "int       03 // explicit two-byte form";
    } else {
      return string_printf("int       0x%02hhX", int_num);
    }
  }
}

void X86Emulator::exec_CE_into(uint8_t) {
  throw runtime_error("into opcode is not implemented");
}

string X86Emulator::dasm_CE_into(DisassemblyState&) {
  return "into";
}

void X86Emulator::exec_CF_iret(uint8_t) {
  throw runtime_error("iret opcode is not implemented");
}

string X86Emulator::dasm_CF_iret(DisassemblyState&) {
  return "iret";
}

void X86Emulator::exec_D0_to_D3_bit_shifts(uint8_t opcode) {
  bool distance_is_cl = (opcode & 2);
  uint8_t distance = distance_is_cl ? this->regs.r_cl() : 1;
  auto rm = this->fetch_and_decode_rm();

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->w_ea16(rm, this->exec_bit_shifts_logic<uint16_t>(rm.non_ea_reg, this->r_ea16(rm), distance, distance_is_cl));
    } else {
      this->w_ea32(rm, this->exec_bit_shifts_logic<uint32_t>(rm.non_ea_reg, this->r_ea32(rm), distance, distance_is_cl));
    }
  } else {
    this->w_ea8(rm, this->exec_bit_shifts_logic<uint8_t>(rm.non_ea_reg, this->r_ea8(rm), distance, distance_is_cl));
  }
}

string X86Emulator::dasm_D0_to_D3_bit_shifts(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return extend(bit_shift_opcode_names[rm.non_ea_reg], 10) + s.rm_ea_str(rm, s.standard_operand_size(), 0) + ((s.opcode & 2) ? ", cl" : ", 1");
}

void X86Emulator::exec_D4_amx_aam(uint8_t) {
  uint8_t base = this->fetch_instruction_byte();
  this->regs.w_ah(this->regs.r_al() / base);
  this->regs.w_al(this->regs.r_al() % base);
  this->regs.set_flags_integer_result<uint8_t>(this->regs.r_al());
}

string X86Emulator::dasm_D4_amx_aam(DisassemblyState& s) {
  uint8_t base = s.r.get_u8();
  if (base == 10) {
    return "aam";
  } else {
    return string_printf("amx       0x%02hhX // unofficial mnemonic (aam with non-10 base)", base);
  }
}

void X86Emulator::exec_D5_adx_aad(uint8_t) {
  uint8_t base = this->fetch_instruction_byte();
  this->regs.w_al(this->regs.r_al() + (this->regs.r_ah() * base));
  this->regs.w_ah(0);
  this->regs.set_flags_integer_result<uint8_t>(this->regs.r_al());
}

string X86Emulator::dasm_D5_adx_aad(DisassemblyState& s) {
  uint8_t base = s.r.get_u8();
  if (base == 10) {
    return "aad";
  } else {
    return string_printf("adx       0x%02hhX // unofficial mnemonic (aad with non-10 base)", base);
  }
}

void X86Emulator::exec_D8_DC_float_basic_math(uint8_t) {
  throw runtime_error("floating-point opcodes are not implemented");
}

string X86Emulator::dasm_D8_DC_float_basic_math(DisassemblyState& s) {
  bool is_DC = (s.opcode == 0xDC);
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  switch (rm.non_ea_reg) {
    case 0:
    case 1: {
      string name = extend(rm.non_ea_reg == 1 ? "fmul" : "fadd", 10);
      if (!is_DC || rm.has_mem_ref()) {
        uint8_t operand_size = is_DC ? 64 : 32;
        return name + "st, " + s.rm_ea_str(rm, operand_size, RMF::EA_ST);
      } else {
        return name + s.rm_ea_str(rm, 80, RMF::EA_ST) + ", st";
      }
    }
    case 2:
    case 3: {
      string name = extend(rm.non_ea_reg == 3 ? "fcomp" : "fcom", 10);
      uint8_t operand_size = is_DC ? 64 : 32;
      return name + "st, " + s.rm_ea_str(rm, operand_size, RMF::EA_ST);
    }
    case 4:
    case 5:
    case 6:
    case 7: {
      bool is_r = (rm.has_mem_ref() ? 0 : is_DC) ^ (rm.non_ea_reg & 1);
      string name = extend(string_printf("f%s%c", ((rm.non_ea_reg & 2) ? "div" : "sub"), (is_r ? 'r' : ' ')), 10);
      if (!is_DC || rm.has_mem_ref()) {
        uint8_t operand_size = is_DC ? 64 : 32;
        return name + "st, " + s.rm_ea_str(rm, operand_size, RMF::EA_ST);
      } else {
        return name + s.rm_ea_str(rm, 80, RMF::EA_ST) + ", st";
      }
    }
    default:
      throw logic_error("invalid subopcode number");
  }
}

void X86Emulator::exec_D9_DD_float_moves_and_analytical_math(uint8_t) {
  throw runtime_error("floating-point opcodes are not implemented");
}

string X86Emulator::dasm_D9_DD_float_moves_and_analytical_math(DisassemblyState& s) {
  bool is_DD = (s.opcode == 0xDD);
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  switch (rm.non_ea_reg) {
    case 0: {
      if (!is_DD || rm.has_mem_ref()) {
        uint8_t operand_size = is_DD ? 64 : 32;
        return "fld       st, " + s.rm_ea_str(rm, operand_size, RMF::EA_ST);
      } else {
        return "ffree     " + s.rm_ea_str(rm, 80, RMF::EA_ST);
      }
    }
    case 1: {
      if (!rm.has_mem_ref()) {
        return "fxch      st, " + s.rm_ea_str(rm, 80, RMF::EA_ST);
      } else {
        return "fisttp    " + s.rm_ea_str(rm, 64, 0) + ", st";
      }
    }
    case 2: {
      if (!is_DD || rm.has_mem_ref()) {
        uint8_t operand_size = is_DD ? 64 : 32;
        return "fst       " + s.rm_ea_str(rm, operand_size, RMF::EA_ST) + ", st";
      } else if (rm.ea_reg != 0) {
        return "fst       st, " + s.rm_ea_str(rm, 80, RMF::EA_ST);
      } else {
        return "fnop";
      }
    }
    case 3: {
      if (!is_DD || rm.has_mem_ref()) {
        uint8_t operand_size = is_DD ? 64 : 32;
        return "fstp      " + s.rm_ea_str(rm, operand_size, RMF::EA_ST) + ", st";
      } else {
        return "fstp      st, " + s.rm_ea_str(rm, 80, RMF::EA_ST);
      }
    }
    case 4: {
      if (is_DD) {
        if (rm.has_mem_ref()) {
          return "frstor    " + s.rm_ea_str(rm, 0, RMF::SUPPRESS_OPERAND_SIZE);
        } else {
          return "fucom     st, " + s.rm_ea_str(rm, 0, RMF::EA_ST);
        }
      } else {
        if (rm.has_mem_ref()) {
          return "fldenv    " + s.rm_ea_str(rm, 0, RMF::SUPPRESS_OPERAND_SIZE);
        } else if (rm.ea_reg == 0) {
          return "fchs      st";
        } else if (rm.ea_reg == 1) {
          return "fabs      st";
        } else if (rm.ea_reg == 4) {
          return "ftst      st";
        } else if (rm.ea_reg == 5) {
          return "fxam      st";
        } else {
          return ".invalid  <<fldenv meta variants>>";
        }
      }
    }
    case 5: {
      if (is_DD) {
        if (rm.has_mem_ref()) {
          return ".invalid  <<fucomp with memory reference>>";
        } else {
          return "fucomp    st, " + s.rm_ea_str(rm, 0, RMF::EA_ST);
        }
      } else {
        if (rm.has_mem_ref()) {
          return "fldcw     " + s.rm_ea_str(rm, 16, 0);
        } else {
          static const char* names[8] = {
              "fld1      st",
              "fldl2t    st",
              "fldl2e    st",
              "fldpi     st",
              "fldlg2    st",
              "fldln2    st",
              "fldz      st",
              ".invalid  <<load float constant>>",
          };
          return names[rm.ea_reg];
        }
      }
    }
    case 6: {
      if (is_DD) {
        if (rm.has_mem_ref()) {
          return "fnsave    " + s.rm_ea_str(rm, 0, RMF::SUPPRESS_OPERAND_SIZE);
        } else {
          return ".invalid  <<fnsave with register reference>>";
        }
      } else {
        if (rm.has_mem_ref()) {
          return "fnstenv   " + s.rm_ea_str(rm, 0, RMF::SUPPRESS_OPERAND_SIZE);
        } else {
          static const char* names[8] = {
              "f2xm1     st",
              "fyl2x     st1, st",
              "fptan     st",
              "fpatan    st1, st",
              "fxtract   st",
              "fprem1    st1, st",
              "fdecstp",
              "fincstp",
          };
          return names[rm.ea_reg];
        }
      }
    }
    case 7: {
      if (is_DD) {
        if (rm.has_mem_ref()) {
          return "fnstsw    " + s.rm_ea_str(rm, 16, 0);
        } else {
          return ".invalid  <<fnsave with register reference>>";
        }
      } else {
        if (rm.has_mem_ref()) {
          return "fnstcw    " + s.rm_ea_str(rm, 16, 0);
        } else {
          static const char* names[8] = {
              "fprem     st, st1",
              "fyl2xp1   st1, st",
              "fsqrt     st",
              "fsincos   st",
              "frndint   st",
              "fscale    st, st1",
              "fsin      st",
              "fcos      st",
          };
          return names[rm.ea_reg];
        }
      }
    }
    default:
      throw logic_error("invalid subopcode number");
  }
}

void X86Emulator::exec_DA_DB_float_cmov_and_int_math(uint8_t) {
  throw runtime_error("floating-point opcodes are not implemented");
}

string X86Emulator::dasm_DA_DB_float_cmov_and_int_math(DisassemblyState& s) {
  bool is_DB = (s.opcode & 1);
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  switch (rm.non_ea_reg) {
    case 0:
      if (rm.has_mem_ref()) {
        return string(is_DB ? "fild      " : "fiadd     ") + "st, " + s.rm_ea_str(rm, 32, 0);
      } else {
        return string(is_DB ? "fcmovnb   " : "fcmovb    ") + "st, " + s.rm_ea_str(rm, 32, RMF::EA_ST);
      }
    case 1:
      if (rm.has_mem_ref()) {
        if (is_DB) {
          return "fisttp    " + s.rm_ea_str(rm, 32, 0) + ", st";
        } else {
          return "fimul     st, " + s.rm_ea_str(rm, 32, 0);
        }
      } else {
        return string(is_DB ? "fcmovne   " : "fcmove    ") + "st, " + s.rm_ea_str(rm, 32, RMF::EA_ST);
      }
    case 2:
    case 3: {
      bool is_3 = (rm.non_ea_reg & 1);
      if (rm.has_mem_ref()) {
        if (is_DB) {
          return (is_3 ? "fistp     " : "fist      ") + s.rm_ea_str(rm, 32, 0) + ", st";
        } else {
          return (is_3 ? "ficomp    st, " : "ficom     st, ") + s.rm_ea_str(rm, 32, 0);
        }
      } else {
        const char* name = is_3
            ? (is_DB ? "fcmovnu   st, " : "fcmovu    st, ")
            : (is_DB ? "fcmovnbe  st, " : "fcmovbe   st, ");
        return name + s.rm_ea_str(rm, 32, RMF::EA_ST);
      }
    }
    case 4:
    case 5: {
      bool is_5 = (rm.non_ea_reg & 1);
      if (is_DB) {
        if (is_5) {
          return (rm.has_mem_ref() ? "fld       st, " : "fucomi    st, ") + s.rm_ea_str(rm, 80, RMF::EA_ST);
        } else if (rm.has_mem_ref()) {
          return ".invalid  <<fneni variant with memory reference>>";
        } else {
          static const char* names[8] = {
              "fneni",
              "fndisi",
              "fnclex",
              "fninit",
              "fnsetpm",
              "frstpm",
              ".invalid  <<fneni variant 6>>",
              ".invalid  <<fneni variant 7>>",
          };
          return names[rm.ea_reg];
        }
      } else {
        if (rm.has_mem_ref()) {
          return (is_5 ? "fsubr     st, " : "fsub      st, ") + s.rm_ea_str(rm, 32, 0);
        } else if (rm.ea_reg == 1) {
          return "fucompp   st, st1";
        } else {
          return ".invalid  <<fsubr/fucompp variant>>";
        }
      }
    }
    case 6:
    case 7: {
      bool is_7 = (rm.non_ea_reg & 1);
      if (is_DB) {
        if (is_7) {
          if (!rm.has_mem_ref()) {
            return ".invalid  <<fstp with register reference>>";
          } else {
            return "fstp      " + s.rm_ea_str(rm, 80, 0) + ", st";
          }
        } else {
          if (rm.has_mem_ref()) {
            return ".invalid  <<fcomi with memory reference>>";
          } else {
            return "fcomi     st, " + s.rm_ea_str(rm, 80, RMF::EA_ST);
          }
        }
      } else {
        if (!rm.has_mem_ref()) {
          return ".invalid  <<fidiv/fidivr with register reference>>";
        } else {
          return (is_7 ? "fidivr    st, " : "fidiv     st, ") + s.rm_ea_str(rm, 32, 0);
        }
      }
    }
    default:
      throw logic_error("invalid subopcode number");
  }
}

void X86Emulator::exec_DE_float_misc1(uint8_t) {
  throw runtime_error("floating-point opcodes are not implemented");
}

string X86Emulator::dasm_DE_float_misc1(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  switch (rm.non_ea_reg) {
    case 0:
    case 1: {
      string op = (rm.non_ea_reg & 1) ? "mul" : "add";
      if (rm.has_mem_ref()) {
        return "fi" + op + "     st, " + s.rm_ea_str(rm, 16, 0);
      } else {
        return "f" + op + "p     " + s.rm_ea_str(rm, 16, RMF::EA_ST) + ", st";
      }
    }
    case 2:
    case 3: {
      if (rm.has_mem_ref()) {
        string op = (rm.non_ea_reg & 1) ? "p" : " ";
        return "ficom" + op + "    st, " + s.rm_ea_str(rm, 16, 0);
      } else if ((rm.non_ea_reg == 3) && (rm.ea_reg == 1)) {
        return "fcompp    st, st1";
      } else {
        return ".invalid  <<ficom/fcompp variant>>";
      }
    }
    case 4:
    case 5:
    case 6:
    case 7:
      if (rm.has_mem_ref()) {
        static const char* names[4] = {"fisub     st, ", "fisubr    st, ", "fidiv     st, ", "fidivr    st, "};
        return names[rm.non_ea_reg - 4] + s.rm_ea_str(rm, 16, 0);
      } else {
        static const char* names[4] = {"fsubrp    ", "fsubp     ", "fdivrp    ", "fdivp     "};
        return names[rm.non_ea_reg - 4] + s.rm_ea_str(rm, 16, RMF::EA_ST) + ", st";
      }
    default:
      throw logic_error("invalid subopcode number");
  }
}

void X86Emulator::exec_DF_float_misc2(uint8_t) {
  throw runtime_error("floating-point opcodes are not implemented");
}

string X86Emulator::dasm_DF_float_misc2(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  switch (rm.non_ea_reg) {
    case 0:
      if (rm.has_mem_ref()) {
        return "fild      " + s.rm_ea_str(rm, 16, 0);
      } else {
        return "ffreep    " + s.rm_ea_str(rm, 0, RMF::EA_ST);
      }
    case 1:
      if (rm.has_mem_ref()) {
        return "fisttp    " + s.rm_ea_str(rm, 16, 0) + ", st";
      } else {
        return "fxch7     st, " + s.rm_ea_str(rm, 0, RMF::EA_ST);
      }
    case 2:
    case 3:
      if (rm.has_mem_ref()) {
        return ((rm.non_ea_reg & 1) ? "fistp     " : "fist      ") + s.rm_ea_str(rm, 16, 0) + ", st";
      } else {
        return ".invalid  <<fist/fistp with register reference>>";
      }
    case 4:
      if (rm.has_mem_ref()) {
        return "fbld      st, " + s.rm_ea_str(rm, 80, 0);
      } else if (rm.ea_reg == 0) {
        return "fnstsw    ax";
      } else {
        return ".invalid  <<fist/fistp with register reference>>";
      }
    case 5:
      if (rm.has_mem_ref()) {
        return "fild      st, " + s.rm_ea_str(rm, 64, 0);
      } else {
        return "fucomip   st, " + s.rm_ea_str(rm, 80, RMF::EA_ST);
      }
    case 6:
      if (rm.has_mem_ref()) {
        return "fbstp     " + s.rm_ea_str(rm, 80, 0) + ", st";
      } else {
        return "fcomip    st, " + s.rm_ea_str(rm, 80, RMF::EA_ST);
      }
    case 7:
      if (rm.has_mem_ref()) {
        return "fistp     " + s.rm_ea_str(rm, 64, 0) + ", st";
      } else {
        return ".invalid  <<fistp with register reference>>";
      }
    default:
      throw logic_error("invalid subopcode number");
  }
}

void X86Emulator::exec_E4_E5_EC_ED_in(uint8_t) {
  throw runtime_error("port I/O not implemented");
}

string X86Emulator::dasm_E4_E5_EC_ED_in(DisassemblyState& s) {
  uint8_t operand_size = s.standard_operand_size();
  if (s.opcode & 8) {
    return string_printf("in        %s, dx", name_for_reg(0, operand_size));
  } else {
    uint8_t port = s.r.get_u8();
    return string_printf("in        %s, 0x%02hhX", name_for_reg(0, operand_size), port);
  }
}

void X86Emulator::exec_E6_E7_EE_EF_out(uint8_t) {
  throw runtime_error("port I/O not implemented");
}

string X86Emulator::dasm_E6_E7_EE_EF_out(DisassemblyState& s) {
  uint8_t operand_size = s.standard_operand_size();
  if (s.opcode & 8) {
    return string_printf("out       dx, %s", name_for_reg(0, operand_size));
  } else {
    uint8_t port = s.r.get_u8();
    return string_printf("in        0x%02hhX, %s", port, name_for_reg(0, operand_size));
  }
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
  return string_printf("%s      0x%08" PRIX32, opcode_name, dest) + s.annotation_for_rm_ea(DecodedRM(-1, dest), -1);
}

void X86Emulator::exec_EB_jmp(uint8_t) {
  this->regs.eip += sign_extend<uint32_t, uint8_t>(this->fetch_instruction_byte());
}

string X86Emulator::dasm_EB_jmp(DisassemblyState& s) {
  uint32_t offset = sign_extend<uint32_t, uint8_t>(s.r.get_u8());
  uint32_t dest = s.start_address + s.r.where() + offset;
  s.branch_target_addresses.emplace(dest, false);
  return string_printf("jmp       0x%08" PRIX32, dest) + s.annotation_for_rm_ea(DecodedRM(-1, dest), -1);
}

void X86Emulator::exec_F2_F3_repz_repnz(uint8_t opcode) {
  if (this->overrides.repeat_nz || this->overrides.repeat_z) {
    throw runtime_error("multiple repeat prefixes on opcode");
  }
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
  this->regs.replace_flag(Regs::CF, !this->regs.read_flag(Regs::CF));
}

string X86Emulator::dasm_F5_cmc(DisassemblyState&) {
  return "cmc";
}

template <typename T, typename LET>
T X86Emulator::exec_F6_F7_misc_math_logic(uint8_t what, T value) {
  switch (what) {
    case 0: // test
    case 1: { // test (this case is documented by AMD but not Intel)
      T imm = this->fetch_instruction_data<LET>();
      this->regs.set_flags_bitwise_result<T>(value & imm);
      break;
    }
    case 2: // not
      // Note: Unlike all the other opcodes here, this one doesn't set any flags
      value = ~value;
      break;
    case 3: // neg
      // TODO: We assume that this opcode is equivalent to `sub 0, value`. Is
      // this the correct treatment for the resulting flags?
      value = this->regs.set_flags_integer_subtract<T>(0, value, ~Regs::CF);
      this->regs.replace_flag(Regs::CF, (value != 0));
      break;
    case 4: { // mul (to edx:eax)
      bool of_cf = false;
      // TODO: This is kind of bad. Use templates appropriately here.
      if (bits_for_type<T> == 8) {
        uint16_t res = this->regs.r_al() * value;
        this->regs.w_ax(res);
        of_cf = ((res & 0xFF00) != 0);
      } else if (bits_for_type<T> == 16) {
        uint32_t res = this->regs.r_ax() * value;
        this->regs.w_dx(res >> 16);
        this->regs.w_ax(res);
        of_cf = ((res & 0xFFFF0000) != 0);
      } else if (bits_for_type<T> == 32) {
        uint64_t res = static_cast<uint64_t>(this->regs.r_eax()) * value;
        this->regs.w_edx(res >> 32);
        this->regs.w_eax(res);
        of_cf = ((res & 0xFFFFFFFF00000000) != 0);
      } else {
        throw logic_error("invalid operand size");
      }
      this->regs.replace_flag(Regs::OF, of_cf);
      this->regs.replace_flag(Regs::CF, of_cf);
      break;
    }
    case 5: { // imul (to edx:eax)
      bool of_cf = false;
      // TODO: This is kind of bad. Use templates appropriately here.
      if (bits_for_type<T> == 8) {
        int16_t res = static_cast<int8_t>(this->regs.r_al()) * static_cast<int8_t>(value);
        this->regs.w_ax(res);
        of_cf = (res != sign_extend<int16_t, uint8_t>(res & 0x00FF));
      } else if (bits_for_type<T> == 16) {
        int32_t res = static_cast<int16_t>(this->regs.r_ax()) * static_cast<int16_t>(value);
        this->regs.w_dx(res >> 16);
        this->regs.w_ax(res);
        of_cf = (res != sign_extend<int32_t, uint16_t>(res & 0x0000FFFF));
      } else if (bits_for_type<T> == 32) {
        int64_t res = static_cast<int64_t>(static_cast<int32_t>(this->regs.r_eax())) * static_cast<int32_t>(value);
        this->regs.w_edx(res >> 32);
        this->regs.w_eax(res);
        of_cf = (res != sign_extend<int64_t, uint32_t>(res & 0x00000000FFFFFFFF));
      } else {
        throw logic_error("invalid operand size");
      }
      // NOTE: The other forms of imul may have different treatments for these
      // flags! Don't copy this implementation blindly.
      this->regs.replace_flag(Regs::OF, of_cf);
      this->regs.replace_flag(Regs::CF, of_cf);
      break;
    }
    case 6: // div (to edx:eax)
      if (value == 0) {
        throw runtime_error("division by zero");
      }
      if (bits_for_type<T> == 8) {
        uint16_t dividend = this->regs.r_ax();
        uint16_t quotient = dividend / value;
        if (quotient > 0xFF) {
          throw runtime_error("quotient too large");
        }
        this->regs.w_al(quotient);
        this->regs.w_ah(dividend % value);
      } else if (bits_for_type<T> == 16) {
        uint32_t dividend = (this->regs.r_dx() << 16) | this->regs.r_ax();
        uint32_t quotient = dividend / value;
        if (quotient > 0xFFFF) {
          throw runtime_error("quotient too large");
        }
        this->regs.w_ax(quotient);
        this->regs.w_dx(dividend % value);
      } else if (bits_for_type<T> == 32) {
        uint64_t dividend = (static_cast<uint64_t>(this->regs.r_edx()) << 32) | this->regs.r_eax();
        uint64_t quotient = dividend / value;
        if (quotient > 0xFFFFFFFFULL) {
          throw runtime_error("quotient too large");
        }
        this->regs.w_eax(quotient);
        this->regs.w_edx(dividend % value);
      } else {
        throw logic_error("invalid operand size");
      }
      // Note: this operation sets a bunch of flags, but they're all undefined,
      // so we just don't modify any of them.
      break;
    case 7: // idiv (to edx:eax)
      if (value == 0) {
        throw runtime_error("division by zero");
      }
      if (bits_for_type<T> == 8) {
        int16_t dividend = static_cast<int16_t>(this->regs.r_ax());
        int16_t quotient = dividend / static_cast<int8_t>(value);
        if (quotient < -0x80 || quotient > 0x7F) {
          throw runtime_error("quotient too large");
        }
        this->regs.w_al(quotient);
        this->regs.w_ah(dividend % static_cast<int8_t>(value));
      } else if (bits_for_type<T> == 16) {
        int32_t dividend = (this->regs.r_dx() << 16) | this->regs.r_ax();
        int32_t quotient = dividend / static_cast<int16_t>(value);
        if (quotient < -0x8000 || quotient > 0x7FFF) {
          throw runtime_error("quotient too large");
        }
        this->regs.w_ax(quotient);
        this->regs.w_dx(dividend % static_cast<int16_t>(value));
      } else if (bits_for_type<T> == 32) {
        int64_t dividend = (static_cast<int64_t>(this->regs.r_edx()) << 32) | this->regs.r_eax();
        int64_t quotient = dividend / static_cast<int32_t>(value);
        if (quotient < -0x80000000LL || quotient > 0x7FFFFFFFLL) {
          throw runtime_error("quotient too large");
        }
        this->regs.w_eax(quotient);
        this->regs.w_edx(dividend % static_cast<int32_t>(value));
      } else {
        throw logic_error("invalid operand size");
      }
      // Note: this operation sets a bunch of flags, but they're all undefined,
      // so we just don't modify any of them.
      break;
    default:
      throw logic_error("invalid misc math operation");
  }
  return value;
}

void X86Emulator::exec_F6_F7_misc_math(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  // Note: only 2 and 3 (not and neg) write to ea; the others don't
  if ((rm.non_ea_reg & 6) == 2) {
    if (opcode & 1) {
      if (this->overrides.operand_size) {
        this->w_ea16(rm, this->exec_F6_F7_misc_math_logic<uint16_t>(rm.non_ea_reg, this->r_ea16(rm)));
      } else {
        this->w_ea32(rm, this->exec_F6_F7_misc_math_logic<uint32_t>(rm.non_ea_reg, this->r_ea32(rm)));
      }
    } else {
      this->w_ea8(rm, this->exec_F6_F7_misc_math_logic<uint8_t>(rm.non_ea_reg, this->r_ea8(rm)));
    }
  } else {
    if (opcode & 1) {
      if (this->overrides.operand_size) {
        this->exec_F6_F7_misc_math_logic<uint16_t>(rm.non_ea_reg, this->r_ea16(rm));
      } else {
        this->exec_F6_F7_misc_math_logic<uint32_t>(rm.non_ea_reg, this->r_ea32(rm));
      }
    } else {
      this->exec_F6_F7_misc_math_logic<uint8_t>(rm.non_ea_reg, this->r_ea8(rm));
    }
  }
}

string X86Emulator::dasm_F6_F7_misc_math(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  uint8_t operand_size = s.standard_operand_size();
  if (rm.non_ea_reg < 2) {
    return "test      " + s.rm_ea_str(rm, operand_size, 0) + string_printf(", 0x%02" PRIX32, get_operand(s.r, operand_size));
  } else {
    const char* const opcode_names[8] = {
        "test", "test", "not", "neg", "mul", "imul", "div", "idiv"};
    string name = extend(opcode_names[rm.non_ea_reg], 10);
    return name + s.rm_ea_str(rm, operand_size, 0);
  }
}

void X86Emulator::exec_F8_clc(uint8_t) {
  this->regs.replace_flag(Regs::CF, false);
}

string X86Emulator::dasm_F8_clc(DisassemblyState&) {
  return "clc";
}

void X86Emulator::exec_F9_stc(uint8_t) {
  this->regs.replace_flag(Regs::CF, true);
}

string X86Emulator::dasm_F9_stc(DisassemblyState&) {
  return "stc";
}

void X86Emulator::exec_FA_cli(uint8_t) {
  throw runtime_error("IF cannot be modified within the emulator");
}

string X86Emulator::dasm_FA_cli(DisassemblyState&) {
  return "cli";
}

void X86Emulator::exec_FB_sti(uint8_t) {
  throw runtime_error("IF cannot be modified within the emulator");
}

string X86Emulator::dasm_FB_sti(DisassemblyState&) {
  return "sti";
}

void X86Emulator::exec_FC_cld(uint8_t) {
  this->regs.replace_flag(Regs::DF, false);
}

string X86Emulator::dasm_FC_cld(DisassemblyState&) {
  return "cld";
}

void X86Emulator::exec_FD_std(uint8_t) {
  this->regs.replace_flag(Regs::DF, true);
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
          this->w_ea16(rm, this->regs.set_flags_integer_add<uint16_t>(this->r_ea16(rm), 1, ~Regs::CF));
        } else {
          this->w_ea32(rm, this->regs.set_flags_integer_add<uint32_t>(this->r_ea32(rm), 1, ~Regs::CF));
        }
        break;
      case 1: // dec
        if (this->overrides.operand_size) {
          this->w_ea16(rm, this->regs.set_flags_integer_subtract<uint16_t>(this->r_ea16(rm), 1, ~Regs::CF));
        } else {
          this->w_ea32(rm, this->regs.set_flags_integer_subtract<uint32_t>(this->r_ea32(rm), 1, ~Regs::CF));
        }
        break;
      case 2: // call
        this->push<le_uint32_t>(this->regs.eip);
        [[fallthrough]];
      case 4: // jmp
        this->regs.eip = this->overrides.operand_size
            ? sign_extend<uint32_t, uint16_t>(this->r_ea16(rm))
            : this->r_ea32(rm);
        break;
      case 3: // call (far)
      case 5: // jmp (far)
        throw runtime_error("far call/jmp is not implemented");
      case 6: // push
        if (this->overrides.operand_size) {
          this->push<le_uint16_t>(this->r_ea16(rm));
        } else {
          this->push<le_uint32_t>(this->r_ea32(rm));
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
    if (!(rm.non_ea_reg & 1)) {
      this->w_ea8(rm, this->regs.set_flags_integer_add<uint8_t>(this->r_ea8(rm), 1, ~Regs::CF));
    } else {
      this->w_ea8(rm, this->regs.set_flags_integer_subtract<uint8_t>(this->r_ea8(rm), 1, ~Regs::CF));
    }
  }
}

string X86Emulator::dasm_FE_FF_inc_dec_misc(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  uint8_t operand_size = s.standard_operand_size();
  if (rm.non_ea_reg < 2) {
    return (rm.non_ea_reg ? "dec       " : "inc       ") + s.rm_ea_str(rm, operand_size, 0);
  }

  if (!(s.opcode & 1)) {
    return ".invalid  <<inc/dec/misc>>";
  }

  switch (rm.non_ea_reg) {
    case 2: // call
    case 4: // jmp
      return ((rm.non_ea_reg == 2) ? "call      " : "jmp       ") + s.rm_ea_str(rm, operand_size, 0);
    case 3: // call (far)
    case 5: // jmp (far)
      return ".unknown  <<far call/jmp>> // unimplemented";
    case 6: // push
      return "push      " + s.rm_ea_str(rm, operand_size, 0);
    case 7:
      return ".invalid  <<misc/7>>";
    default:
      throw logic_error("invalid misc operation");
  }
}

void X86Emulator::exec_0F_10_11_mov_xmm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  if (opcode & 1) { // xmm/mem <- xmm
    if (this->overrides.repeat_z) { // movss
      this->w_ea_xmm32(rm, this->r_non_ea_xmm32(rm));
    } else if (this->overrides.repeat_nz) { // movsd
      this->w_ea_xmm64(rm, this->r_non_ea_xmm64(rm));
    } else { // movups/movupd (TODO: Why are these different opcodes?)
      this->w_ea_xmm128(rm, this->r_non_ea_xmm128(rm));
    }
  } else { // xmm <- xmm/mem
    if (rm.has_mem_ref()) {
      this->w_non_ea_xmm128(rm, Regs::XMMReg());
    }
    if (this->overrides.repeat_z) { // movss
      this->w_non_ea_xmm32(rm, this->r_ea_xmm32(rm));
    } else if (this->overrides.repeat_nz) { // movsd
      this->w_non_ea_xmm64(rm, this->r_ea_xmm64(rm));
    } else { // movups/movupd (TODO: Why are these different opcodes?)
      this->w_non_ea_xmm128(rm, this->r_ea_xmm128(rm));
    }
  }
}

string X86Emulator::dasm_0F_10_11_mov_xmm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  string opcode_name;
  uint8_t operand_size;
  if (s.overrides.repeat_z) {
    opcode_name = "movss";
    operand_size = 32;
  } else if (s.overrides.repeat_nz) {
    opcode_name = "movsd";
    operand_size = 64;
  } else if (s.overrides.operand_size) {
    opcode_name = "movupd";
    operand_size = 128;
  } else {
    opcode_name = "movups";
    operand_size = 128;
  }
  opcode_name.resize(10, ' ');

  return opcode_name + s.rm_str(rm, operand_size, ((s.opcode & 1) ? RMF::EA_FIRST : 0) | RMF::EA_XMM | RMF::NON_EA_XMM);
}

void X86Emulator::exec_0F_18_to_1F_prefetch_or_nop(uint8_t) {
  this->fetch_and_decode_rm();
  // Technically we should do a read cycle here in case of the prefetch opcodes,
  // but I'm lazy
}

string X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  string opcode_name = "nop       ";
  if (s.opcode == 0x18) {
    if (rm.non_ea_reg == 0) {
      opcode_name = "prefetchnta ";
    } else if (rm.non_ea_reg == 1) {
      opcode_name = "prefetcht0 ";
    } else if (rm.non_ea_reg == 2) {
      opcode_name = "prefetcht1 ";
    } else if (rm.non_ea_reg == 3) {
      opcode_name = "prefetcht2 ";
    }
  }
  return opcode_name + s.rm_ea_str(rm, 8, 0);
}

void X86Emulator::exec_0F_31_rdtsc(uint8_t) {
  uint64_t res;
  if (this->tsc_overrides.empty()) {
    res = this->instructions_executed + this->tsc_offset;
  } else {
    res = this->tsc_overrides.front();
    this->tsc_overrides.pop_front();
  }
  this->regs.w_edx(res >> 32);
  this->regs.w_eax(res);
}

string X86Emulator::dasm_0F_31_rdtsc(DisassemblyState&) {
  return "rdtsc";
}

void X86Emulator::exec_0F_40_to_4F_cmov_rm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  // Technically there should be a read cycle even if the condition is false. We
  // don't do that because it could cause annoying fake connections in the data
  // dependency graph. This emulator isn't cycle-accurate anyway.
  if (this->regs.check_condition(opcode & 0x0F)) {
    if (this->overrides.operand_size) {
      this->w_non_ea16(rm, this->r_ea16(rm));
    } else {
      this->w_non_ea32(rm, this->r_ea32(rm));
    }
  }
}

string X86Emulator::dasm_0F_40_to_4F_cmov_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  string opcode_name = "cmov";
  opcode_name += name_for_condition_code[s.opcode & 0x0F];
  opcode_name.resize(10, ' ');
  return opcode_name + s.rm_str(rm, s.overrides.operand_size ? 16 : 32, 0);
}

void X86Emulator::exec_0F_7E_7F_mov_xmm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  if (this->overrides.repeat_nz) {
    throw runtime_error("invalid 0F 7E/7F with repeat_nz");
  }

  if (opcode & 1) { // all xmm/mem <- xmm
    if (this->overrides.repeat_z || this->overrides.operand_size) { // movdqu/movdqa
      this->w_ea_xmm128(rm, this->r_non_ea_xmm128(rm));
    } else { // movq
      throw runtime_error("mm registers are not supported");
    }
  } else { // all xmm/mem <- xmm EXCEPT for movq, which is the opposite (why?!)
    this->regs.xmm_unreported128(rm.non_ea_reg).clear();
    if (this->overrides.repeat_z) { // movq
      this->w_non_ea_xmm64(rm, this->r_ea_xmm64(rm));
    } else { // movd
      this->w_non_ea_xmm32(rm, this->r_ea_xmm32(rm));
    }
  }
}

string X86Emulator::dasm_0F_7E_7F_mov_xmm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  string opcode_name;
  uint8_t operand_size;
  if (s.opcode & 1) {
    if (s.overrides.operand_size) {
      opcode_name = "movdqa";
      operand_size = 128;
    } else if (s.overrides.repeat_z) {
      opcode_name = "movdqu";
      operand_size = 128;
    } else {
      throw runtime_error("mm registers are not supported");
    }
  } else {
    if (s.overrides.repeat_z) {
      opcode_name = "movq";
      operand_size = 64;
    } else {
      opcode_name = "movd";
      operand_size = 32;
    }
  }
  opcode_name.resize(10, ' ');

  return opcode_name + s.rm_str(rm, operand_size, (((s.opcode & 1) || !s.overrides.repeat_z) ? RMF::EA_FIRST : 0) | RMF::EA_XMM | RMF::NON_EA_XMM);
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
  return opcode_name + string_printf("0x%08" PRIX32, dest) + s.annotation_for_rm_ea(DecodedRM(-1, dest), -1);
}

void X86Emulator::exec_0F_90_to_9F_setcc_rm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  if (rm.non_ea_reg != 0) {
    throw runtime_error("invalid setcc with non_ea_reg != 0");
  }
  this->w_ea8(rm, this->regs.check_condition(opcode & 0x0F) ? 1 : 0);
}

string X86Emulator::dasm_0F_90_to_9F_setcc_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  if (rm.non_ea_reg != 0) {
    return ".invalid  <<setcc with non_ea_reg != 0>>";
  }
  string opcode_name = "set";
  opcode_name += name_for_condition_code[s.opcode & 0x0F];
  opcode_name.resize(10, ' ');
  return opcode_name + s.rm_ea_str(rm, 8, 0);
}

template <typename T>
T X86Emulator::exec_shld_shrd_logic(
    bool is_right_shift,
    T dest_value,
    T incoming_value,
    uint8_t distance,
    bool distance_is_cl) {
  if ((distance & 0x1F) == 0) {
    return dest_value;
  }

  // There appears to be a special case here in the Windows ARM emulator. If
  // distance masks to 0x10 above, then shift_distance is 0x10, even for 16-bit
  // operands.
  uint8_t shift_distance = (this->behavior == Behavior::WINDOWS_ARM_EMULATOR && ((distance & 0x1F) == 0x10))
      ? 0x10
      : (distance & (bits_for_type<T> - 1));

  T orig_sign = dest_value & msb_for_type<T>;
  bool cf = (this->behavior == Behavior::WINDOWS_ARM_EMULATOR)
      ? false
      : this->regs.read_flag(Regs::CF);
  for (uint8_t c = shift_distance; c; c--) {
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

  this->regs.set_flags_integer_result<T>(dest_value);
  this->regs.replace_flag(Regs::CF, cf);

  if (shift_distance == 1) {
    this->regs.replace_flag(Regs::OF, (orig_sign != (dest_value & msb_for_type<T>)));
  } else if (distance != 0 && this->behavior == Behavior::WINDOWS_ARM_EMULATOR) {
    if (distance_is_cl) {
      this->regs.replace_flag(Regs::OF, (orig_sign != (dest_value & msb_for_type<T>)));
    } else {
      this->regs.replace_flag(Regs::OF, false);
    }
  }
  return dest_value;
}

void X86Emulator::exec_0F_A2_cpuid(uint8_t) {
  // TODO: There are a lot of possible branches here; we probably should
  // implement behavior like a real CPU here instead of just guessing at what
  // reasonable constants would be here
  switch (this->regs.r_eax()) {
    case 0:
      this->regs.w_eax(4);
      this->regs.w_ecx(0x6C65746E);
      this->regs.w_edx(0x49656E69);
      this->regs.w_ebx(0x756E6547);
      break;
    case 1:
      if (this->behavior == Behavior::WINDOWS_ARM_EMULATOR) {
        this->regs.w_eax(0x00000F4A);
        this->regs.w_ecx(0x02880203);
        this->regs.w_edx(0x17808111);
        this->regs.w_ebx(0x00040000);
      } else {
        this->regs.w_eax(0x000005F0); // Intel Xeon 5100
        this->regs.w_ecx(0x00000000); // nothing
        this->regs.w_edx(0x06808001); // SSE, SSE2, MMX, cmov, x87
        this->regs.w_ebx(0x00000000);
      }
      break;
    default:
      throw runtime_error("unsupported cpuid request");
  }
}

string X86Emulator::dasm_0F_A2_cpuid(DisassemblyState&) {
  return "cpuid";
}

void X86Emulator::exec_0F_A4_A5_AC_AD_shld_shrd(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  bool distance_is_cl = (opcode & 1);
  uint8_t distance = distance_is_cl
      ? this->regs.r_cl()
      : this->fetch_instruction_byte();

  if (this->overrides.operand_size) {
    this->w_ea16(rm, this->exec_shld_shrd_logic<uint16_t>(opcode & 8, this->r_ea16(rm), this->r_non_ea16(rm), distance, distance_is_cl));
  } else {
    this->w_ea32(rm, this->exec_shld_shrd_logic<uint32_t>(opcode & 8, this->r_ea32(rm), this->r_non_ea32(rm), distance, distance_is_cl));
  }
}

string X86Emulator::dasm_0F_A4_A5_AC_AD_shld_shrd(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  string opcode_name = extend((s.opcode & 8) ? "shrd" : "shld", 10);
  string distance_str = (s.opcode & 1) ? ", cl" : string_printf(", 0x%02" PRIX8, s.r.get_u8());
  return opcode_name + s.rm_str(rm, s.overrides.operand_size ? 16 : 32, RMF::EA_FIRST) + distance_str;
}

void X86Emulator::exec_0F_AF_imul(uint8_t) {
  this->fetch_and_decode_rm();
  throw runtime_error("unimplemented opcode: imul r16/32, r/m16/32");
}

string X86Emulator::dasm_0F_AF_imul(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return "imul      " + s.rm_str(rm, s.overrides.operand_size ? 16 : 32, 0);
}

template <typename T>
T X86Emulator::exec_bit_test_ops_logic(uint8_t what, T v, uint8_t bit_number) {
  uint32_t mask = (1 << bit_number);
  this->regs.replace_flag(Regs::CF, v & mask);
  switch (what) {
    case 0: // bt (bit test)
      return v; // Nothing to do (we already tested it above)
    case 1: // bts (bit test and set)
      return v | mask;
    case 2: // btr (bit test and reset)
      return v & ~mask;
    case 3: // btc (bit test and complement)
      return v ^ mask;
    default:
      throw logic_error("invalid bit test operation");
  }
}

static const std::array<const char* const, 4> bit_test_opcode_names = {"bt", "bts", "btr", "btc"};

void X86Emulator::exec_0F_A3_AB_B3_BB_bit_tests(uint8_t opcode) {
  DecodedRM rm = this->fetch_and_decode_rm();
  uint8_t what = (opcode >> 3) & 3;

  if (rm.ea_index_scale < 0) { // Bit field is in register
    if (this->overrides.operand_size) {
      uint8_t bit_number = this->r_non_ea16(rm) & 0x0F;
      uint16_t v = this->exec_bit_test_ops_logic<uint16_t>(
          what, this->r_ea16(rm), bit_number);
      if (what != 0) {
        this->w_ea16(rm, v);
      }
    } else {
      uint8_t bit_number = this->r_non_ea16(rm) & 0x1F;
      uint32_t v = this->exec_bit_test_ops_logic<uint32_t>(
          what, this->r_ea32(rm), bit_number);
      if (what != 0) {
        this->w_ea32(rm, v);
      }
    }

  } else { // Bit field is in memory
    int32_t bit_number = this->overrides.operand_size
        ? sign_extend<int32_t, uint16_t>(this->r_non_ea16(rm))
        : static_cast<int32_t>(this->r_non_ea32(rm));
    uint32_t addr = this->resolve_mem_ea(rm) + (bit_number >> 8);
    uint8_t v = this->exec_bit_test_ops_logic<uint8_t>(
        what, this->r_mem<uint8_t>(addr), (bit_number & 7));
    if (what != 0) {
      this->w_mem<uint8_t>(addr, v);
    }
  }
}

string X86Emulator::dasm_0F_A3_AB_B3_BB_bit_tests(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  string opcode_name = extend(bit_test_opcode_names[(s.opcode >> 3) & 3], 10);
  return opcode_name + s.rm_str(rm, s.overrides.operand_size ? 16 : 32, RMF::EA_FIRST);
}

void X86Emulator::exec_0F_B6_B7_BE_BF_movzx_movsx(uint8_t opcode) {
  DecodedRM rm = this->fetch_and_decode_rm();

  uint32_t v = (opcode & 1) ? this->r_ea16(rm) : this->r_ea8(rm);
  if (opcode & 8) { // movsx
    v = (opcode & 1)
        ? sign_extend<uint32_t, uint16_t>(v)
        : sign_extend<uint32_t, uint8_t>(v);
  } else { // movzx
    v &= (opcode & 1) ? 0x0000FFFF : 0x000000FF;
  }

  if (this->overrides.operand_size) {
    // Intel's docs imply that the operand size prefix is simply ignored in this
    // case (but don't explicitly state this).
    if (opcode & 1) {
      throw runtime_error("operand size prefix on movsx/movzx r32 r/m16");
    }
    this->w_non_ea16(rm, v);
  } else {
    this->w_non_ea32(rm, v);
  }
}

string X86Emulator::dasm_0F_B6_B7_BE_BF_movzx_movsx(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  string opcode_name = (s.opcode & 8) ? "movsx     " : "movzx     ";
  return opcode_name + s.rm_str(rm, (s.opcode & 1) ? 16 : 8, s.overrides.operand_size ? 16 : 32, 0);
}

void X86Emulator::exec_0F_BA_bit_tests(uint8_t) {
  DecodedRM rm = this->fetch_and_decode_rm();
  if (!(rm.non_ea_reg & 4)) {
    throw runtime_error("invalid opcode 0F BA");
  }
  uint8_t what = rm.non_ea_reg & 3;
  // TODO: Is this supposed to be signed? The manual doesn't specify :(
  int8_t bit_number = static_cast<int8_t>(this->fetch_instruction_byte());

  if (rm.ea_index_scale < 0) { // Bit field is in register
    // TODO: Docs seem to say that the mask is 7 (not 0x0F) for a 16-bit
    // operand, but that seems... wrong. Verify the correct behavior.
    if (this->overrides.operand_size) {
      uint16_t v = this->exec_bit_test_ops_logic<uint16_t>(
          what, this->r_ea16(rm), bit_number & 0x0F);
      if (what != 0) {
        this->w_ea16(rm, v);
      }
    } else {
      uint32_t v = this->exec_bit_test_ops_logic<uint32_t>(
          what, this->r_ea32(rm), bit_number & 0x1F);
      if (what != 0) {
        this->w_ea32(rm, v);
      }
    }

  } else {
    uint32_t addr = this->resolve_mem_ea(rm) + (bit_number >> 3);
    uint8_t v = this->exec_bit_test_ops_logic<uint8_t>(
        what, this->r_mem<uint8_t>(addr), (bit_number & 7));
    if (what != 0) {
      this->w_mem<uint8_t>(addr, v);
    }
  }
}

string X86Emulator::dasm_0F_BA_bit_tests(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  if (!(rm.non_ea_reg & 4)) {
    return ".invalid  <<bit test with subopcode 0-3>>";
  }
  uint8_t bit_number = s.r.get_u8();
  string opcode_name = extend(bit_test_opcode_names[rm.non_ea_reg & 3], 10);
  return opcode_name + s.rm_ea_str(rm, s.overrides.operand_size ? 16 : 32, 0) + string_printf(", 0x%02" PRIX8, bit_number);
}

void X86Emulator::exec_0F_BC_BD_bsf_bsr(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  uint32_t value = this->overrides.operand_size
      ? this->r_ea16(rm)
      : this->r_ea32(rm);
  uint32_t orig_value = value;

  if (value == 0) {
    this->regs.replace_flag(Regs::ZF, true);
  } else {
    this->regs.replace_flag(Regs::ZF, false);

    uint32_t result;
    if (opcode & 1) { // bsr
      result = 31;
      for (; !(value & 0x80000000); result--, value <<= 1) {
      }
    } else { // bsf
      result = 0;
      for (; !(value & 1); result++, value >>= 1) {
      }
    }

    if (this->overrides.operand_size) {
      this->w_non_ea16(rm, result);
    } else {
      this->w_non_ea32(rm, result);
    }
  }

  if (this->behavior == Behavior::WINDOWS_ARM_EMULATOR) {
    this->regs.replace_flag(Regs::OF, false);
    this->regs.replace_flag(Regs::SF, !this->overrides.operand_size && (orig_value & 0x80000000));
    this->regs.replace_flag(Regs::CF, true);
  }
}

string X86Emulator::dasm_0F_BC_BD_bsf_bsr(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return ((s.opcode & 1) ? "bsr       " : "bsf       ") + s.rm_str(rm, s.overrides.operand_size ? 16 : 32, 0);
}

void X86Emulator::exec_0F_C0_C1_xadd_rm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      uint16_t a = this->r_non_ea16(rm);
      uint16_t b = this->r_ea16(rm);
      this->w_non_ea16(rm, b);
      this->w_ea16(rm, this->regs.set_flags_integer_add<uint16_t>(a, b));
    } else {
      uint32_t a = this->r_non_ea32(rm);
      uint32_t b = this->r_ea32(rm);
      this->w_non_ea32(rm, b);
      this->w_ea32(rm, this->regs.set_flags_integer_add<uint32_t>(a, b));
    }
  } else {
    uint8_t a = this->r_non_ea8(rm);
    uint8_t b = this->r_ea8(rm);
    this->w_non_ea8(rm, b);
    this->w_ea8(rm, this->regs.set_flags_integer_add<uint8_t>(a, b));
  }
}

string X86Emulator::dasm_0F_C0_C1_xadd_rm(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return "xadd      " + s.rm_str(rm, s.standard_operand_size(), RMF::EA_FIRST);
}

void X86Emulator::exec_0F_C8_to_CF_bswap(uint8_t opcode) {
  uint8_t which = opcode & 7;
  if (this->overrides.operand_size) {
    // If the bswap instruction references a 16-bit register, the result is
    // undefined. According to the manual, you're supposed to use something like
    // xchg ah, al to byteswap 16-bit values instead. We implement reasonable
    // behavior here, but the Windows emulator seems to zero the register
    // instead. (That might be what real CPUs do as well.)
    if (this->behavior == Behavior::WINDOWS_ARM_EMULATOR) {
      this->regs.write16(which, 0);
    } else {
      this->regs.write16(which, bswap16(this->regs.read16(which)));
    }
  } else {
    this->regs.write32(which, bswap32(this->regs.read32(which)));
  }
}

string X86Emulator::dasm_0F_C8_to_CF_bswap(DisassemblyState& s) {
  return string_printf("bswap     %s",
      name_for_reg(s.opcode & 7, s.overrides.operand_size ? 16 : 32));
}

void X86Emulator::exec_0F_D6_movq_variants(uint8_t) {
  auto rm = this->fetch_and_decode_rm();

  if (!this->overrides.operand_size || this->overrides.repeat_z || this->overrides.repeat_nz) {
    throw runtime_error("mm registers are not supported");
  }

  if (!rm.has_mem_ref()) {
    this->w_ea_xmm128(rm, Regs::XMMReg());
  }
  this->w_ea_xmm64(rm, this->r_non_ea_xmm64(rm));
}

string X86Emulator::dasm_0F_D6_movq_variants(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  if (!s.overrides.operand_size || s.overrides.repeat_z || s.overrides.repeat_nz) {
    throw runtime_error("mm registers are not supported");
  }

  return "movq      " + s.rm_str(rm, 64, RMF::EA_FIRST | RMF::EA_XMM | RMF::NON_EA_XMM);
}

void X86Emulator::exec_unimplemented(uint8_t opcode) {
  throw runtime_error(string_printf("unimplemented opcode: %02hhX", opcode));
}

string X86Emulator::dasm_unimplemented(DisassemblyState& s) {
  return string_printf(".unknown  0x%02" PRIX8, s.opcode);
}

void X86Emulator::exec_0F_unimplemented(uint8_t opcode) {
  throw runtime_error(string_printf("unimplemented opcode: 0F %02hhX", opcode));
}

string X86Emulator::dasm_0F_unimplemented(DisassemblyState& s) {
  return string_printf(".unknown  0x0F%02" PRIX8, s.opcode);
}

X86Emulator::X86Emulator(shared_ptr<MemoryContext> mem)
    : EmulatorBase(mem),
      behavior(Behavior::SPECIFICATION),
      tsc_offset(0),
      execution_labels_computed(false),
      trace_data_sources(false),
      trace_data_source_addrs(false) {}

const X86Emulator::OpcodeImplementation X86Emulator::fns[0x100] = {
    /* 00 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 01 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 02 */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 03 */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 04 */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 05 */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 06 */ {&X86Emulator::exec_06_0E_16_1E_0FA0_0FA8_push_segment_reg, &X86Emulator::dasm_06_0E_16_1E_0FA0_0FA8_push_segment_reg},
    /* 07 */ {&X86Emulator::exec_07_17_1F_0FA1_0FA9_pop_segment_reg, &X86Emulator::dasm_07_17_1F_0FA1_0FA9_pop_segment_reg},
    /* 08 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 09 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 0A */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 0B */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 0C */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 0D */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 0E */ {&X86Emulator::exec_06_0E_16_1E_0FA0_0FA8_push_segment_reg, &X86Emulator::dasm_06_0E_16_1E_0FA0_0FA8_push_segment_reg},
    /* 0F */ {&X86Emulator::exec_0F_extensions, &X86Emulator::dasm_0F_extensions},
    /* 10 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 11 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 12 */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 13 */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 14 */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 15 */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 16 */ {&X86Emulator::exec_06_0E_16_1E_0FA0_0FA8_push_segment_reg, &X86Emulator::dasm_06_0E_16_1E_0FA0_0FA8_push_segment_reg},
    /* 17 */ {&X86Emulator::exec_07_17_1F_0FA1_0FA9_pop_segment_reg, &X86Emulator::dasm_07_17_1F_0FA1_0FA9_pop_segment_reg},
    /* 18 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 19 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 1A */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 1B */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 1C */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 1D */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 1E */ {&X86Emulator::exec_06_0E_16_1E_0FA0_0FA8_push_segment_reg, &X86Emulator::dasm_06_0E_16_1E_0FA0_0FA8_push_segment_reg},
    /* 1F */ {&X86Emulator::exec_07_17_1F_0FA1_0FA9_pop_segment_reg, &X86Emulator::dasm_07_17_1F_0FA1_0FA9_pop_segment_reg},
    /* 20 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 21 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 22 */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 23 */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 24 */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 25 */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 26 */ {&X86Emulator::exec_26_es, &X86Emulator::dasm_26_es},
    /* 27 */ {&X86Emulator::exec_27_daa, &X86Emulator::dasm_27_daa},
    /* 28 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 29 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 2A */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 2B */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 2C */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 2D */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 2E */ {&X86Emulator::exec_2E_cs, &X86Emulator::dasm_2E_cs},
    /* 2F */ {},
    /* 30 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 31 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 32 */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 33 */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 34 */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 35 */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 36 */ {&X86Emulator::exec_36_ss, &X86Emulator::dasm_36_ss},
    /* 37 */ {&X86Emulator::exec_37_aaa, &X86Emulator::dasm_37_aaa},
    /* 38 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 39 */ {&X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math, &X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math},
    /* 3A */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 3B */ {&X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math, &X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math},
    /* 3C */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 3D */ {&X86Emulator::exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math, &X86Emulator::dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math},
    /* 3E */ {&X86Emulator::exec_3E_ds, &X86Emulator::dasm_3E_ds},
    /* 3F */ {},
    /* 40 */ {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 41 */ {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 42 */ {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 43 */ {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 44 */ {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 45 */ {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 46 */ {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 47 */ {&X86Emulator::exec_40_to_47_inc, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 48 */ {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 49 */ {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 4A */ {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 4B */ {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 4C */ {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 4D */ {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 4E */ {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 4F */ {&X86Emulator::exec_48_to_4F_dec, &X86Emulator::dasm_40_to_4F_inc_dec},
    /* 50 */ {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 51 */ {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 52 */ {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 53 */ {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 54 */ {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 55 */ {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 56 */ {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 57 */ {&X86Emulator::exec_50_to_57_push, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 58 */ {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 59 */ {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 5A */ {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 5B */ {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 5C */ {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 5D */ {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 5E */ {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 5F */ {&X86Emulator::exec_58_to_5F_pop, &X86Emulator::dasm_50_to_5F_push_pop},
    /* 60 */ {&X86Emulator::exec_60_pusha, &X86Emulator::dasm_60_pusha},
    /* 61 */ {&X86Emulator::exec_61_popa, &X86Emulator::dasm_61_popa},
    /* 62 */ {},
    /* 63 */ {},
    /* 64 */ {&X86Emulator::exec_64_fs, &X86Emulator::dasm_64_fs},
    /* 65 */ {&X86Emulator::exec_65_gs, &X86Emulator::dasm_65_gs},
    /* 66 */ {&X86Emulator::exec_66_operand_size, &X86Emulator::dasm_66_operand_size},
    /* 67 */ {},
    /* 68 */ {&X86Emulator::exec_68_6A_push, &X86Emulator::dasm_68_6A_push},
    /* 69 */ {&X86Emulator::exec_69_6B_imul, &X86Emulator::dasm_69_6B_imul},
    /* 6A */ {&X86Emulator::exec_68_6A_push, &X86Emulator::dasm_68_6A_push},
    /* 6B */ {&X86Emulator::exec_69_6B_imul, &X86Emulator::dasm_69_6B_imul},
    /* 6C */ {},
    /* 6D */ {},
    /* 6E */ {},
    /* 6F */ {},
    /* 70 */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 71 */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 72 */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 73 */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 74 */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 75 */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 76 */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 77 */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 78 */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 79 */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 7A */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 7B */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 7C */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 7D */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 7E */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 7F */ {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
    /* 80 */ {&X86Emulator::exec_80_to_83_imm_math, &X86Emulator::dasm_80_to_83_imm_math},
    /* 81 */ {&X86Emulator::exec_80_to_83_imm_math, &X86Emulator::dasm_80_to_83_imm_math},
    /* 82 */ {&X86Emulator::exec_80_to_83_imm_math, &X86Emulator::dasm_80_to_83_imm_math},
    /* 83 */ {&X86Emulator::exec_80_to_83_imm_math, &X86Emulator::dasm_80_to_83_imm_math},
    /* 84 */ {&X86Emulator::exec_84_85_test_rm, &X86Emulator::dasm_84_85_test_rm},
    /* 85 */ {&X86Emulator::exec_84_85_test_rm, &X86Emulator::dasm_84_85_test_rm},
    /* 86 */ {&X86Emulator::exec_86_87_xchg_rm, &X86Emulator::dasm_86_87_xchg_rm},
    /* 87 */ {&X86Emulator::exec_86_87_xchg_rm, &X86Emulator::dasm_86_87_xchg_rm},
    /* 88 */ {&X86Emulator::exec_88_to_8B_mov_rm, &X86Emulator::dasm_88_to_8B_mov_rm},
    /* 89 */ {&X86Emulator::exec_88_to_8B_mov_rm, &X86Emulator::dasm_88_to_8B_mov_rm},
    /* 8A */ {&X86Emulator::exec_88_to_8B_mov_rm, &X86Emulator::dasm_88_to_8B_mov_rm},
    /* 8B */ {&X86Emulator::exec_88_to_8B_mov_rm, &X86Emulator::dasm_88_to_8B_mov_rm},
    /* 8C */ {},
    /* 8D */ {&X86Emulator::exec_8D_lea, &X86Emulator::dasm_8D_lea},
    /* 8E */ {},
    /* 8F */ {&X86Emulator::exec_8F_pop_rm, &X86Emulator::dasm_8F_pop_rm},
    /* 90 */ {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
    /* 91 */ {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
    /* 92 */ {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
    /* 93 */ {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
    /* 94 */ {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
    /* 95 */ {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
    /* 96 */ {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
    /* 97 */ {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
    /* 98 */ {&X86Emulator::exec_98_cbw_cwde, &X86Emulator::dasm_98_cbw_cwde},
    /* 99 */ {&X86Emulator::exec_99_cwd_cdq, &X86Emulator::dasm_99_cwd_cdq},
    /* 9A */ {},
    /* 9B */ {},
    /* 9C */ {&X86Emulator::exec_9C_pushf_pushfd, &X86Emulator::dasm_9C_pushf_pushfd},
    /* 9D */ {&X86Emulator::exec_9D_popf_popfd, &X86Emulator::dasm_9D_popf_popfd},
    /* 9E */ {},
    /* 9F */ {&X86Emulator::exec_9F_lahf, &X86Emulator::dasm_9F_lahf},
    /* A0 */ {&X86Emulator::exec_A0_A1_A2_A3_mov_eax_memabs, &X86Emulator::dasm_A0_A1_A2_A3_mov_eax_memabs},
    /* A1 */ {&X86Emulator::exec_A0_A1_A2_A3_mov_eax_memabs, &X86Emulator::dasm_A0_A1_A2_A3_mov_eax_memabs},
    /* A2 */ {&X86Emulator::exec_A0_A1_A2_A3_mov_eax_memabs, &X86Emulator::dasm_A0_A1_A2_A3_mov_eax_memabs},
    /* A3 */ {&X86Emulator::exec_A0_A1_A2_A3_mov_eax_memabs, &X86Emulator::dasm_A0_A1_A2_A3_mov_eax_memabs},
    /* A4 */ {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
    /* A5 */ {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
    /* A6 */ {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
    /* A7 */ {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
    /* A8 */ {&X86Emulator::exec_A8_A9_test_eax_imm, &X86Emulator::dasm_A8_A9_test_eax_imm},
    /* A9 */ {&X86Emulator::exec_A8_A9_test_eax_imm, &X86Emulator::dasm_A8_A9_test_eax_imm},
    /* AA */ {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
    /* AB */ {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
    /* AC */ {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
    /* AD */ {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
    /* AE */ {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
    /* AF */ {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
    /* B0 */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* B1 */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* B2 */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* B3 */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* B4 */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* B5 */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* B6 */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* B7 */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* B8 */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* B9 */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* BA */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* BB */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* BC */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* BD */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* BE */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* BF */ {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
    /* C0 */ {&X86Emulator::exec_C0_C1_bit_shifts, &X86Emulator::dasm_C0_C1_bit_shifts},
    /* C1 */ {&X86Emulator::exec_C0_C1_bit_shifts, &X86Emulator::dasm_C0_C1_bit_shifts},
    /* C2 */ {&X86Emulator::exec_C2_C3_CA_CB_ret, &X86Emulator::dasm_C2_C3_CA_CB_ret},
    /* C3 */ {&X86Emulator::exec_C2_C3_CA_CB_ret, &X86Emulator::dasm_C2_C3_CA_CB_ret},
    /* C4 */ {},
    /* C5 */ {},
    /* C6 */ {&X86Emulator::exec_C6_C7_mov_rm_imm, &X86Emulator::dasm_C6_C7_mov_rm_imm},
    /* C7 */ {&X86Emulator::exec_C6_C7_mov_rm_imm, &X86Emulator::dasm_C6_C7_mov_rm_imm},
    /* C8 */ {&X86Emulator::exec_C8_enter, &X86Emulator::dasm_C8_enter},
    /* C9 */ {&X86Emulator::exec_C9_leave, &X86Emulator::dasm_C9_leave},
    /* CA */ {&X86Emulator::exec_C2_C3_CA_CB_ret, &X86Emulator::dasm_C2_C3_CA_CB_ret},
    /* CB */ {&X86Emulator::exec_C2_C3_CA_CB_ret, &X86Emulator::dasm_C2_C3_CA_CB_ret},
    /* CC */ {&X86Emulator::exec_CC_CD_int, &X86Emulator::dasm_CC_CD_int},
    /* CD */ {&X86Emulator::exec_CC_CD_int, &X86Emulator::dasm_CC_CD_int},
    /* CE */ {&X86Emulator::exec_CE_into, &X86Emulator::dasm_CE_into},
    /* CF */ {&X86Emulator::exec_CF_iret, &X86Emulator::dasm_CF_iret},
    /* D0 */ {&X86Emulator::exec_D0_to_D3_bit_shifts, &X86Emulator::dasm_D0_to_D3_bit_shifts},
    /* D1 */ {&X86Emulator::exec_D0_to_D3_bit_shifts, &X86Emulator::dasm_D0_to_D3_bit_shifts},
    /* D2 */ {&X86Emulator::exec_D0_to_D3_bit_shifts, &X86Emulator::dasm_D0_to_D3_bit_shifts},
    /* D3 */ {&X86Emulator::exec_D0_to_D3_bit_shifts, &X86Emulator::dasm_D0_to_D3_bit_shifts},
    /* D4 */ {&X86Emulator::exec_D4_amx_aam, &X86Emulator::dasm_D4_amx_aam},
    /* D5 */ {&X86Emulator::exec_D5_adx_aad, &X86Emulator::dasm_D5_adx_aad},
    /* D6 */ {},
    /* D7 */ {},
    /* D8 */ {&X86Emulator::exec_D8_DC_float_basic_math, &X86Emulator::dasm_D8_DC_float_basic_math},
    /* D9 */ {&X86Emulator::exec_D9_DD_float_moves_and_analytical_math, &X86Emulator::dasm_D9_DD_float_moves_and_analytical_math},
    /* DA */ {&X86Emulator::exec_DA_DB_float_cmov_and_int_math, &X86Emulator::dasm_DA_DB_float_cmov_and_int_math},
    /* DB */ {&X86Emulator::exec_DA_DB_float_cmov_and_int_math, &X86Emulator::dasm_DA_DB_float_cmov_and_int_math},
    /* DC */ {&X86Emulator::exec_D8_DC_float_basic_math, &X86Emulator::dasm_D8_DC_float_basic_math},
    /* DD */ {&X86Emulator::exec_D9_DD_float_moves_and_analytical_math, &X86Emulator::dasm_D9_DD_float_moves_and_analytical_math},
    /* DE */ {&X86Emulator::exec_DE_float_misc1, &X86Emulator::dasm_DE_float_misc1},
    /* DF */ {&X86Emulator::exec_DF_float_misc2, &X86Emulator::dasm_DF_float_misc2},
    /* E0 */ {},
    /* E1 */ {},
    /* E2 */ {},
    /* E3 */ {},
    /* E4 */ {&X86Emulator::exec_E4_E5_EC_ED_in, &X86Emulator::dasm_E4_E5_EC_ED_in},
    /* E5 */ {&X86Emulator::exec_E4_E5_EC_ED_in, &X86Emulator::dasm_E4_E5_EC_ED_in},
    /* E6 */ {&X86Emulator::exec_E6_E7_EE_EF_out, &X86Emulator::dasm_E6_E7_EE_EF_out},
    /* E7 */ {&X86Emulator::exec_E6_E7_EE_EF_out, &X86Emulator::dasm_E6_E7_EE_EF_out},
    /* E8 */ {&X86Emulator::exec_E8_E9_call_jmp, &X86Emulator::dasm_E8_E9_call_jmp},
    /* E9 */ {&X86Emulator::exec_E8_E9_call_jmp, &X86Emulator::dasm_E8_E9_call_jmp},
    /* EA */ {},
    /* EB */ {&X86Emulator::exec_EB_jmp, &X86Emulator::dasm_EB_jmp},
    /* EC */ {&X86Emulator::exec_E4_E5_EC_ED_in, &X86Emulator::dasm_E4_E5_EC_ED_in},
    /* ED */ {&X86Emulator::exec_E4_E5_EC_ED_in, &X86Emulator::dasm_E4_E5_EC_ED_in},
    /* EE */ {&X86Emulator::exec_E6_E7_EE_EF_out, &X86Emulator::dasm_E6_E7_EE_EF_out},
    /* EF */ {&X86Emulator::exec_E6_E7_EE_EF_out, &X86Emulator::dasm_E6_E7_EE_EF_out},
    /* F0 */ {},
    /* F1 */ {},
    /* F2 */ {&X86Emulator::exec_F2_F3_repz_repnz, &X86Emulator::dasm_F2_F3_repz_repnz},
    /* F3 */ {&X86Emulator::exec_F2_F3_repz_repnz, &X86Emulator::dasm_F2_F3_repz_repnz},
    /* F4 */ {},
    /* F5 */ {&X86Emulator::exec_F5_cmc, &X86Emulator::dasm_F5_cmc},
    /* F6 */ {&X86Emulator::exec_F6_F7_misc_math, &X86Emulator::dasm_F6_F7_misc_math},
    /* F7 */ {&X86Emulator::exec_F6_F7_misc_math, &X86Emulator::dasm_F6_F7_misc_math},
    /* F8 */ {&X86Emulator::exec_F8_clc, &X86Emulator::dasm_F8_clc},
    /* F9 */ {&X86Emulator::exec_F9_stc, &X86Emulator::dasm_F9_stc},
    /* FA */ {&X86Emulator::exec_FA_cli, &X86Emulator::dasm_FA_cli},
    /* FB */ {&X86Emulator::exec_FB_sti, &X86Emulator::dasm_FB_sti},
    /* FC */ {&X86Emulator::exec_FC_cld, &X86Emulator::dasm_FC_cld},
    /* FD */ {&X86Emulator::exec_FD_std, &X86Emulator::dasm_FD_std},
    /* FE */ {&X86Emulator::exec_FE_FF_inc_dec_misc, &X86Emulator::dasm_FE_FF_inc_dec_misc},
    /* FF */ {&X86Emulator::exec_FE_FF_inc_dec_misc, &X86Emulator::dasm_FE_FF_inc_dec_misc},
};

const X86Emulator::OpcodeImplementation X86Emulator::fns_0F[0x100] = {
    /* 0F00 */ {},
    /* 0F01 */ {},
    /* 0F02 */ {},
    /* 0F03 */ {},
    /* 0F04 */ {},
    /* 0F05 */ {},
    /* 0F06 */ {},
    /* 0F07 */ {},
    /* 0F08 */ {},
    /* 0F09 */ {},
    /* 0F0A */ {},
    /* 0F0B */ {},
    /* 0F0C */ {},
    /* 0F0D */ {},
    /* 0F0E */ {},
    /* 0F0F */ {},
    /* 0F10 */ {&X86Emulator::exec_0F_10_11_mov_xmm, &X86Emulator::dasm_0F_10_11_mov_xmm},
    /* 0F11 */ {&X86Emulator::exec_0F_10_11_mov_xmm, &X86Emulator::dasm_0F_10_11_mov_xmm},
    /* 0F12 */ {},
    /* 0F13 */ {},
    /* 0F14 */ {},
    /* 0F15 */ {},
    /* 0F16 */ {},
    /* 0F17 */ {},
    /* 0F18 */ {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
    /* 0F19 */ {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
    /* 0F1A */ {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
    /* 0F1B */ {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
    /* 0F1C */ {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
    /* 0F1D */ {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
    /* 0F1E */ {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
    /* 0F1F */ {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
    /* 0F20 */ {},
    /* 0F21 */ {},
    /* 0F22 */ {},
    /* 0F23 */ {},
    /* 0F24 */ {},
    /* 0F25 */ {},
    /* 0F26 */ {},
    /* 0F27 */ {},
    /* 0F28 */ {},
    /* 0F29 */ {},
    /* 0F2A */ {},
    /* 0F2B */ {},
    /* 0F2C */ {},
    /* 0F2D */ {},
    /* 0F2E */ {},
    /* 0F2F */ {},
    /* 0F30 */ {},
    /* 0F31 */ {&X86Emulator::exec_0F_31_rdtsc, &X86Emulator::dasm_0F_31_rdtsc},
    /* 0F32 */ {},
    /* 0F33 */ {},
    /* 0F34 */ {},
    /* 0F35 */ {},
    /* 0F36 */ {},
    /* 0F37 */ {},
    /* 0F38 */ {},
    /* 0F39 */ {},
    /* 0F3A */ {},
    /* 0F3B */ {},
    /* 0F3C */ {},
    /* 0F3D */ {},
    /* 0F3E */ {},
    /* 0F3F */ {},
    /* 0F40 */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F41 */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F42 */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F43 */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F44 */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F45 */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F46 */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F47 */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F48 */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F49 */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F4A */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F4B */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F4C */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F4D */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F4E */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F4F */ {&X86Emulator::exec_0F_40_to_4F_cmov_rm, &X86Emulator::dasm_0F_40_to_4F_cmov_rm},
    /* 0F50 */ {},
    /* 0F51 */ {},
    /* 0F52 */ {},
    /* 0F53 */ {},
    /* 0F54 */ {},
    /* 0F55 */ {},
    /* 0F56 */ {},
    /* 0F57 */ {},
    /* 0F58 */ {},
    /* 0F59 */ {},
    /* 0F5A */ {},
    /* 0F5B */ {},
    /* 0F5C */ {},
    /* 0F5D */ {},
    /* 0F5E */ {},
    /* 0F5F */ {},
    /* 0F60 */ {},
    /* 0F61 */ {},
    /* 0F62 */ {},
    /* 0F63 */ {},
    /* 0F64 */ {},
    /* 0F65 */ {},
    /* 0F66 */ {},
    /* 0F67 */ {},
    /* 0F68 */ {},
    /* 0F69 */ {},
    /* 0F6A */ {},
    /* 0F6B */ {},
    /* 0F6C */ {},
    /* 0F6D */ {},
    /* 0F6E */ {},
    /* 0F6F */ {},
    /* 0F70 */ {},
    /* 0F71 */ {},
    /* 0F72 */ {},
    /* 0F73 */ {},
    /* 0F74 */ {},
    /* 0F75 */ {},
    /* 0F76 */ {},
    /* 0F77 */ {},
    /* 0F78 */ {},
    /* 0F79 */ {},
    /* 0F7A */ {},
    /* 0F7B */ {},
    /* 0F7C */ {},
    /* 0F7D */ {},
    /* 0F7E */ {&X86Emulator::exec_0F_7E_7F_mov_xmm, &X86Emulator::dasm_0F_7E_7F_mov_xmm},
    /* 0F7F */ {&X86Emulator::exec_0F_7E_7F_mov_xmm, &X86Emulator::dasm_0F_7E_7F_mov_xmm},
    /* 0F80 */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F81 */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F82 */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F83 */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F84 */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F85 */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F86 */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F87 */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F88 */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F89 */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F8A */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F8B */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F8C */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F8D */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F8E */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F8F */ {&X86Emulator::exec_0F_80_to_8F_jcc, &X86Emulator::dasm_0F_80_to_8F_jcc},
    /* 0F90 */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F91 */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F92 */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F93 */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F94 */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F95 */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F96 */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F97 */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F98 */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F99 */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F9A */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F9B */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F9C */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F9D */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F9E */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0F9F */ {&X86Emulator::exec_0F_90_to_9F_setcc_rm, &X86Emulator::dasm_0F_90_to_9F_setcc_rm},
    /* 0FA0 */ {&X86Emulator::exec_06_0E_16_1E_0FA0_0FA8_push_segment_reg, &X86Emulator::dasm_06_0E_16_1E_0FA0_0FA8_push_segment_reg},
    /* 0FA1 */ {&X86Emulator::exec_07_17_1F_0FA1_0FA9_pop_segment_reg, &X86Emulator::dasm_07_17_1F_0FA1_0FA9_pop_segment_reg},
    /* 0FA2 */ {&X86Emulator::exec_0F_A2_cpuid, &X86Emulator::dasm_0F_A2_cpuid},
    /* 0FA3 */ {&X86Emulator::exec_0F_A3_AB_B3_BB_bit_tests, &X86Emulator::dasm_0F_A3_AB_B3_BB_bit_tests},
    /* 0FA4 */ {&X86Emulator::exec_0F_A4_A5_AC_AD_shld_shrd, &X86Emulator::dasm_0F_A4_A5_AC_AD_shld_shrd},
    /* 0FA5 */ {&X86Emulator::exec_0F_A4_A5_AC_AD_shld_shrd, &X86Emulator::dasm_0F_A4_A5_AC_AD_shld_shrd},
    /* 0FA6 */ {},
    /* 0FA7 */ {},
    /* 0FA8 */ {&X86Emulator::exec_06_0E_16_1E_0FA0_0FA8_push_segment_reg, &X86Emulator::dasm_06_0E_16_1E_0FA0_0FA8_push_segment_reg},
    /* 0FA9 */ {&X86Emulator::exec_07_17_1F_0FA1_0FA9_pop_segment_reg, &X86Emulator::dasm_07_17_1F_0FA1_0FA9_pop_segment_reg},
    /* 0FAA */ {},
    /* 0FAB */ {&X86Emulator::exec_0F_A3_AB_B3_BB_bit_tests, &X86Emulator::dasm_0F_A3_AB_B3_BB_bit_tests},
    /* 0FAC */ {&X86Emulator::exec_0F_A4_A5_AC_AD_shld_shrd, &X86Emulator::dasm_0F_A4_A5_AC_AD_shld_shrd},
    /* 0FAD */ {&X86Emulator::exec_0F_A4_A5_AC_AD_shld_shrd, &X86Emulator::dasm_0F_A4_A5_AC_AD_shld_shrd},
    /* 0FAE */ {},
    /* 0FAF */ {&X86Emulator::exec_0F_AF_imul, &X86Emulator::dasm_0F_AF_imul},
    /* 0FB0 */ {},
    /* 0FB1 */ {},
    /* 0FB2 */ {},
    /* 0FB3 */ {&X86Emulator::exec_0F_A3_AB_B3_BB_bit_tests, &X86Emulator::dasm_0F_A3_AB_B3_BB_bit_tests},
    /* 0FB4 */ {},
    /* 0FB5 */ {},
    /* 0FB6 */ {&X86Emulator::exec_0F_B6_B7_BE_BF_movzx_movsx, &X86Emulator::dasm_0F_B6_B7_BE_BF_movzx_movsx},
    /* 0FB7 */ {&X86Emulator::exec_0F_B6_B7_BE_BF_movzx_movsx, &X86Emulator::dasm_0F_B6_B7_BE_BF_movzx_movsx},
    /* 0FB8 */ {},
    /* 0FB9 */ {},
    /* 0FBA */ {&X86Emulator::exec_0F_BA_bit_tests, &X86Emulator::dasm_0F_BA_bit_tests},
    /* 0FBB */ {&X86Emulator::exec_0F_A3_AB_B3_BB_bit_tests, &X86Emulator::dasm_0F_A3_AB_B3_BB_bit_tests},
    /* 0FBC */ {&X86Emulator::exec_0F_BC_BD_bsf_bsr, &X86Emulator::dasm_0F_BC_BD_bsf_bsr},
    /* 0FBD */ {&X86Emulator::exec_0F_BC_BD_bsf_bsr, &X86Emulator::dasm_0F_BC_BD_bsf_bsr},
    /* 0FBE */ {&X86Emulator::exec_0F_B6_B7_BE_BF_movzx_movsx, &X86Emulator::dasm_0F_B6_B7_BE_BF_movzx_movsx},
    /* 0FBF */ {&X86Emulator::exec_0F_B6_B7_BE_BF_movzx_movsx, &X86Emulator::dasm_0F_B6_B7_BE_BF_movzx_movsx},
    /* 0FC0 */ {&X86Emulator::exec_0F_C0_C1_xadd_rm, &X86Emulator::dasm_0F_C0_C1_xadd_rm},
    /* 0FC1 */ {&X86Emulator::exec_0F_C0_C1_xadd_rm, &X86Emulator::dasm_0F_C0_C1_xadd_rm},
    /* 0FC2 */ {},
    /* 0FC3 */ {},
    /* 0FC4 */ {},
    /* 0FC5 */ {},
    /* 0FC6 */ {},
    /* 0FC7 */ {},
    /* 0FC8 */ {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
    /* 0FC9 */ {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
    /* 0FCA */ {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
    /* 0FCB */ {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
    /* 0FCC */ {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
    /* 0FCD */ {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
    /* 0FCE */ {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
    /* 0FCF */ {&X86Emulator::exec_0F_C8_to_CF_bswap, &X86Emulator::dasm_0F_C8_to_CF_bswap},
    /* 0FD0 */ {},
    /* 0FD1 */ {},
    /* 0FD2 */ {},
    /* 0FD3 */ {},
    /* 0FD4 */ {},
    /* 0FD5 */ {},
    /* 0FD6 */ {&X86Emulator::exec_0F_D6_movq_variants, &X86Emulator::dasm_0F_D6_movq_variants},
    /* 0FD7 */ {},
    /* 0FD8 */ {},
    /* 0FD9 */ {},
    /* 0FDA */ {},
    /* 0FDB */ {},
    /* 0FDC */ {},
    /* 0FDD */ {},
    /* 0FDE */ {},
    /* 0FDF */ {},
    /* 0FE0 */ {},
    /* 0FE1 */ {},
    /* 0FE2 */ {},
    /* 0FE3 */ {},
    /* 0FE4 */ {},
    /* 0FE5 */ {},
    /* 0FE6 */ {},
    /* 0FE7 */ {},
    /* 0FE8 */ {},
    /* 0FE9 */ {},
    /* 0FEA */ {},
    /* 0FEB */ {},
    /* 0FEC */ {},
    /* 0FED */ {},
    /* 0FEE */ {},
    /* 0FEF */ {},
    /* 0FF0 */ {},
    /* 0FF1 */ {},
    /* 0FF2 */ {},
    /* 0FF3 */ {},
    /* 0FF4 */ {},
    /* 0FF5 */ {},
    /* 0FF6 */ {},
    /* 0FF7 */ {},
    /* 0FF8 */ {},
    /* 0FF9 */ {},
    /* 0FFA */ {},
    /* 0FFB */ {},
    /* 0FFC */ {},
    /* 0FFD */ {},
    /* 0FFE */ {},
    /* 0FFF */ {},
};

X86Emulator::Overrides::Overrides() noexcept
    : should_clear(true),
      segment(Segment::NONE),
      operand_size(false),
      address_size(false),
      wait(false),
      lock(false),
      repeat_nz(false),
      repeat_z(false) {}

string X86Emulator::Overrides::str() const {
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
  return name_for_segment(this->segment);
}

void X86Emulator::execute() {
  this->execution_labels_computed = false;
  for (;;) {
    // Call debug hook if present
    if (this->debug_hook) {
      try {
        this->debug_hook(*this);
        // The debug hook can modify registers, and we don't want to erroneously
        // assign these changes to the next opcode.
        this->regs.reset_access_flags();
      } catch (const terminate_emulation&) {
        break;
      }
    }

    // Execute a cycle. This is a loop because prefix bytes are implemented as
    // separate opcodes, so we want to call the prefix handler and the opcode
    // handler as if they were a single opcode.
    for (bool should_execute_again = true; should_execute_again;) {
      uint8_t opcode = this->fetch_instruction_byte();
      auto fn = this->fns[opcode].exec;
      if (this->trace_data_sources) {
        this->prev_regs = this->regs;
        this->prev_regs.reset_access_flags();
      }
      if (fn) {
        (this->*fn)(opcode);
      } else {
        this->exec_unimplemented(opcode);
      }
      this->link_current_accesses();
      should_execute_again = !this->overrides.should_clear;
      this->overrides.on_opcode_complete();
    }

    this->instructions_executed++;
  }
  this->execution_labels.clear();
}

void X86Emulator::compute_execution_labels() const {
  if (!this->execution_labels_computed) {
    this->execution_labels.clear();
    for (const auto& symbol_it : this->mem->all_symbols()) {
      this->execution_labels.emplace(symbol_it.second, symbol_it.first);
    }
    this->execution_labels_computed = true;
  }
}

string X86Emulator::disassemble_one(DisassemblyState& s) {
  size_t start_offset = s.r.where();

  string dasm;
  while (dasm.empty()) {
    try {
      s.opcode = s.r.get_u8();
      auto dasm_fn = X86Emulator::fns[s.opcode].dasm;
      dasm = dasm_fn ? dasm_fn(s) : X86Emulator::dasm_unimplemented(s);
    } catch (const out_of_range&) {
      dasm = ".incomplete";
    } catch (const exception& e) {
      dasm = string_printf(".failed   (%s)", e.what());
    }
    s.overrides.on_opcode_complete();
  }

  size_t num_bytes = s.r.where() - start_offset;
  string data_str = format_data_string(s.r.preadx(start_offset, num_bytes), nullptr, FormatDataFlags::HEX_ONLY);
  data_str.resize(max<size_t>(data_str.size() + 3, 23), ' ');
  return data_str + dasm;
}

string X86Emulator::disassemble(
    const void* vdata,
    size_t size,
    uint32_t start_address,
    const multimap<uint32_t, string>* labels) {
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
      labels,
      nullptr,
  };

  // Generate disassembly lines for each opcode
  map<uint32_t, pair<string, uint32_t>> lines; // {pc: (line, next_pc)}
  while (!s.r.eof()) {
    uint32_t pc = s.start_address + s.r.where();
    string line = string_printf("%08" PRIX32 " ", pc);
    line += X86Emulator::disassemble_one(s) + "\n";
    uint32_t next_pc = s.start_address + s.r.where();
    lines.emplace(pc, make_pair(std::move(line), next_pc));
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
      ret_lines.emplace_back(std::move(label));
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
      ret_lines.emplace_back(std::move(label));
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

void X86Emulator::print_source_trace(FILE* stream, const string& what, size_t max_depth) const {
  if (!this->trace_data_sources) {
    fprintf(stream, "source tracing is disabled\n");
    return;
  }

  unordered_set<shared_ptr<DataAccess>> sources;
  auto add_reg_sources16 = [&](uint8_t which) {
    auto s = this->current_reg_sources.at(which);
    sources.emplace(s.source16);
    sources.emplace(s.source8h);
    sources.emplace(s.source8l);
  };
  auto add_reg_sources32 = [&](uint8_t which) {
    sources.emplace(this->current_reg_sources.at(which).source32);
    add_reg_sources16(which);
  };

  string lower_what = tolower(what);
  if (lower_what == "al") {
    sources.emplace(this->current_reg_sources.at(0).source8l);
  } else if (lower_what == "cl") {
    sources.emplace(this->current_reg_sources.at(1).source8l);
  } else if (lower_what == "dl") {
    sources.emplace(this->current_reg_sources.at(2).source8l);
  } else if (lower_what == "bl") {
    sources.emplace(this->current_reg_sources.at(3).source8l);
  } else if (lower_what == "ah") {
    sources.emplace(this->current_reg_sources.at(0).source8h);
  } else if (lower_what == "ch") {
    sources.emplace(this->current_reg_sources.at(1).source8h);
  } else if (lower_what == "dh") {
    sources.emplace(this->current_reg_sources.at(2).source8h);
  } else if (lower_what == "bh") {
    sources.emplace(this->current_reg_sources.at(3).source8h);

  } else if (lower_what == "ax") {
    add_reg_sources16(0);
  } else if (lower_what == "cx") {
    add_reg_sources16(1);
  } else if (lower_what == "dx") {
    add_reg_sources16(2);
  } else if (lower_what == "bx") {
    add_reg_sources16(3);
  } else if (lower_what == "sp") {
    add_reg_sources16(4);
  } else if (lower_what == "bp") {
    add_reg_sources16(5);
  } else if (lower_what == "si") {
    add_reg_sources16(6);
  } else if (lower_what == "di") {
    add_reg_sources16(7);

  } else if (lower_what == "eax") {
    add_reg_sources32(0);
  } else if (lower_what == "ecx") {
    add_reg_sources32(1);
  } else if (lower_what == "edx") {
    add_reg_sources32(2);
  } else if (lower_what == "ebx") {
    add_reg_sources32(3);
  } else if (lower_what == "esp") {
    add_reg_sources32(4);
  } else if (lower_what == "ebp") {
    add_reg_sources32(5);
  } else if (lower_what == "esi") {
    add_reg_sources32(6);
  } else if (lower_what == "edi") {
    add_reg_sources32(7);

    // TODO: support xmm regs here

  } else {
    try {
      sources.emplace(this->memory_data_sources.at(stoul(what, nullptr, 16)));
    } catch (const out_of_range&) {
      fprintf(stream, "no source info\n");
      return;
    }
  }

  function<void(shared_ptr<DataAccess>, size_t)> print_source = [&](shared_ptr<DataAccess> acc, size_t depth) {
    if (!acc.get()) {
      return;
    }

    for (size_t z = 0; z < depth; z++) {
      fputs("| ", stream);
    }
    fputs("+-", stream);
    if (max_depth && depth >= max_depth) {
      fprintf(stream, "(maximum depth reached)\n");
    } else {
      fwritex(stream, acc->str());
      fputc('\n', stream);
      for (const auto& from_acc : acc->sources) {
        print_source(from_acc, depth + 1);
      }
    }
  };

  for (const auto& from_acc : sources) {
    print_source(from_acc, 0);
  }
}

void X86Emulator::import_state(FILE* stream) {
  uint8_t version = freadx<uint8_t>(stream);
  if (version > 2) {
    throw runtime_error("unknown format version");
  }
  if (version >= 1) {
    this->behavior = freadx<Behavior>(stream);
    this->tsc_offset = freadx<le_uint64_t>(stream);
    uint64_t num_tsc_overrides = freadx<le_uint64_t>(stream);
    this->tsc_overrides.clear();
    while (this->tsc_overrides.size() < num_tsc_overrides) {
      this->tsc_overrides.emplace_back(freadx<le_uint64_t>(stream));
    }
  } else {
    this->behavior = Behavior::SPECIFICATION;
    this->tsc_offset = 0;
    this->tsc_overrides.clear();
  }

  this->regs.import_state(stream);
  this->mem->import_state(stream);

  for (auto& it : this->current_reg_sources) {
    it.source32.reset();
    it.source16.reset();
    it.source8h.reset();
    it.source8l.reset();
  }
  for (auto& it : this->current_xmm_reg_sources) {
    it.source128.reset();
    it.source64.reset();
    it.source32.reset();
  }
  this->memory_data_sources.clear();
}

void X86Emulator::export_state(FILE* stream) const {
  fwritex<uint8_t>(stream, 1); // version

  fwritex<Behavior>(stream, this->behavior);
  fwritex<le_uint64_t>(stream, this->tsc_offset);
  fwritex<le_uint64_t>(stream, this->tsc_overrides.size());
  for (uint64_t tsc_override : this->tsc_overrides) {
    fwritex<le_uint64_t>(stream, tsc_override);
  }

  this->regs.export_state(stream);
  this->mem->export_state(stream);
}

// Returns (reg_num, operand_size) or (0xFF, 0xFF) if no match
static pair<uint8_t, uint8_t> int_register_num_for_name(const string& name) {
  static const std::array<const char*, 8> reg_names_8 = {{"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"}};
  static const std::array<const char*, 8> reg_names_16 = {{"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"}};
  static const std::array<const char*, 8> reg_names_32 = {{"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"}};
  for (size_t z = 0; z < 8; z++) {
    if (name == reg_names_8[z]) {
      return make_pair(z, 1);
    }
    if (name == reg_names_16[z]) {
      return make_pair(z, 2);
    }
    if (name == reg_names_32[z]) {
      return make_pair(z, 4);
    }
  }
  return make_pair(0xFF, 0xFF);
}

static uint8_t float_register_num_for_name(const string& name) {
  static const std::array<const char*, 8> reg_names_float = {{"st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7"}};
  for (size_t z = 0; z < 8; z++) {
    if (name == reg_names_float[z]) {
      return z;
    }
  }
  return 0xFF;
}

static uint8_t xmm_register_num_for_name(const string& name) {
  static const std::array<const char*, 8> reg_names_xmm = {{"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"}};
  for (size_t z = 0; z < 8; z++) {
    if (name == reg_names_xmm[z]) {
      return z;
    }
  }
  return 0xFF;
}

X86Emulator::Assembler::Argument::Argument(const std::string& input_text, bool raw) {
  if (raw) {
    this->label_name = input_text;
    this->type = Type::RAW;
    return;
  }

  string text = tolower(input_text);
  strip_leading_whitespace(text);
  strip_trailing_whitespace(text);

  // Check for register names
  for (size_t z = 0; z < 8; z++) {
    auto int_reg_match = int_register_num_for_name(text);
    if (int_reg_match.first != 0xFF) {
      this->reg_num = int_reg_match.first;
      this->operand_size = int_reg_match.second;
      this->type = Type::INT_REGISTER;
      return;
    }
    auto float_reg_match = float_register_num_for_name(text);
    if (float_reg_match != 0xFF) {
      this->reg_num = float_reg_match;
      this->operand_size = 4;
      this->type = Type::FLOAT_REGISTER;
      return;
    }
    auto xmm_reg_match = xmm_register_num_for_name(text);
    if (xmm_reg_match != 0xFF) {
      this->reg_num = xmm_reg_match;
      this->operand_size = 8;
      this->type = Type::XMM_REGISTER;
      return;
    }
  }

  // Check for memory references
  this->operand_size = 0;
  if (text.starts_with("byte") && (text[4] == ' ' || text[4] == '[')) {
    this->operand_size = 1;
    text = text.substr(4);
    strip_leading_whitespace(text);
  } else if (text.starts_with("word") && (text[4] == ' ' || text[4] == '[')) {
    this->operand_size = 2;
    text = text.substr(4);
    strip_leading_whitespace(text);
  } else if (text.starts_with("dword") && (text[5] == ' ' || text[5] == '[')) {
    this->operand_size = 4;
    text = text.substr(5);
    strip_leading_whitespace(text);
  } else if (text.starts_with("qword") && (text[5] == ' ' || text[5] == '[')) {
    this->operand_size = 8;
    text = text.substr(5);
    strip_leading_whitespace(text);
  }
  if (this->operand_size && text.starts_with("ptr") && (text[3] == ' ' || text[3] == '[')) {
    text = text.substr(3);
    strip_leading_whitespace(text);
  }
  if (text.starts_with("[") && text.ends_with("]")) {
    if (!text.ends_with("]")) {
      throw invalid_argument("unterminated memory reference");
    }
    text = text.substr(1, text.size() - 2);

    vector<string> tokens;
    bool token_is_operator = false;
    tokens.emplace_back();
    for (char ch : text) {
      if (ch == ' ') {
        continue;
      }
      bool ch_is_operator = (ch == '+') || (ch == '-');
      if (ch_is_operator != token_is_operator) {
        tokens.emplace_back();
        token_is_operator = ch_is_operator;
      }
      tokens.back().push_back(ch);
    }

    this->reg_num = 0xFF;
    this->reg_num2 = 0xFF;
    this->scale = 0;
    this->value = 0;

    bool last_token_is_operator = false;
    bool operator_is_subtract = false;
    for (const auto& token : tokens) {
      // Check for operators
      if (token == "+") {
        operator_is_subtract = false;
        last_token_is_operator = true;
        continue;
      } else if (token == "-") {
        operator_is_subtract = true;
        last_token_is_operator = true;
        continue;
      } else {
        last_token_is_operator = false;
      }

      // Check for reg names first
      auto int_reg_match = int_register_num_for_name(token);
      if (int_reg_match.first != 0xFF) {
        if (operator_is_subtract) {
          throw invalid_argument("registers cannot be negated in memory references");
        }
        if (int_reg_match.second != 4) {
          throw invalid_argument("address register is not a 32-bit register");
        }
        if (this->reg_num == 0xFF) {
          this->reg_num = int_reg_match.first;
        } else if (this->reg_num2 == 0xFF) {
          this->reg_num2 = int_reg_match.first;
          this->scale = 1;
        } else {
          throw invalid_argument("too many registers specified in memory reference");
        }
        continue;
      }

      // If the token has a * in it, it must be 1*REG, 2*REG, 4*REG, or 8*REG,
      // or a reversal thereof
      size_t multiply_pos = token.find('*');
      if (multiply_pos != string::npos) {
        if (operator_is_subtract) {
          throw invalid_argument("scaled registers cannot be negated in memory references");
        }
        string before = token.substr(0, multiply_pos);
        string after = token.substr(multiply_pos + 1);
        auto before_reg_match = int_register_num_for_name(before);
        auto after_reg_match = int_register_num_for_name(after);
        if ((before_reg_match.first == 0xFF) == (after_reg_match.first == 0xFF)) {
          throw invalid_argument("incorrect scaled index register form in memory reference");
        }
        if (this->reg_num2 != 0xFF) {
          throw runtime_error("too many index registers specified");
        }
        const auto& reg_match = (before_reg_match.first == 0xFF) ? after_reg_match : before_reg_match;
        uint64_t scale64 = stoull((before_reg_match.first == 0xFF) ? before : after, nullptr, 0);
        if ((scale64 != 1) && (scale64 != 2) && (scale64 != 4) && (scale64 != 8)) {
          throw invalid_argument("indexed register scale must be 1, 2, 4, or 8");
        }
        this->scale = scale64;
        this->reg_num2 = reg_match.first;
        continue;
      }

      // If we get here, it must be a displacement
      int32_t value32 = stol(token, nullptr, 0);
      if (operator_is_subtract) {
        value32 = -value32;
      }
      this->value += sign_extend<uint64_t, int32_t>(value32);
    }
    if (last_token_is_operator) {
      throw invalid_argument("trailing operator in memory reference");
    }
    this->type = Type::MEMORY_REFERENCE;
    return;

  } else if (this->operand_size) {
    // An operand size is not required on a memory reference, but if an operand
    // size is given, a memory reference must follow it
    throw invalid_argument("size specification not followed by memory reference");
  }

  try {
    size_t endpos = 0;
    this->value = stoull(text, &endpos, 0);
    if (endpos != text.size()) {
      throw invalid_argument("not a valid immediate value");
    }
    this->scale = ((text[0] == '-') || (text[0] == '+'));
    this->type = Type::IMMEDIATE;
    return;
  } catch (const invalid_argument&) {
  }

  this->label_name = text;
  this->type = Type::BRANCH_TARGET;
}

string X86Emulator::Assembler::Argument::str() const {
  string type_str;
  if (this->type & T::INT_REGISTER) {
    type_str += "INT_REGISTER | ";
  }
  if (this->type & T::FLOAT_REGISTER) {
    type_str += "FLOAT_REGISTER | ";
  }
  if (this->type & T::XMM_REGISTER) {
    type_str += "XMM_REGISTER | ";
  }
  if (this->type & T::IMMEDIATE) {
    type_str += "IMMEDIATE | ";
  }
  if (this->type & T::MEMORY_REFERENCE) {
    type_str += "MEMORY_REFERENCE | ";
  }
  if (this->type & T::BRANCH_TARGET) {
    type_str += "BRANCH_TARGET | ";
  }
  if (this->type & T::RAW) {
    type_str += "RAW | ";
  }
  if (type_str.size() >= 3) {
    type_str.resize(type_str.size() - 3);
  } else {
    type_str = "__MISSING__";
  }

  string label_name_str = format_data_string(this->label_name);

  return string_printf("Argument(type=%s, operand_size=%hhu, reg_num=%hhu, reg_num2=%hhu, scale=%hhu, value=%" PRIX64 ", label_name=%s)",
      type_str.c_str(),
      this->operand_size,
      this->reg_num,
      this->reg_num2,
      this->scale,
      this->value,
      label_name_str.c_str());
}

bool X86Emulator::Assembler::Argument::is_reg_ref() const {
  return ((this->type == Type::INT_REGISTER) ||
      (this->type == Type::FLOAT_REGISTER) ||
      (this->type == Type::XMM_REGISTER));
}

X86Emulator::AssembleResult X86Emulator::Assembler::assemble(const string& text, function<string(const string&)> get_include) {
  string effective_text = text;
  strip_multiline_comments(effective_text);

  vector<string> lines = split(effective_text, '\n');

  unordered_set<string> current_line_labels;
  for (size_t line_index = 0; line_index < lines.size(); line_index++) {
    auto& line = lines[line_index];
    size_t line_num = line_index + 1;

    // Strip comments and whitespace
    size_t comment_pos = min<size_t>(min<size_t>(line.find("//"), line.find('#')), line.find(';'));
    if (comment_pos != string::npos) {
      line = line.substr(0, comment_pos);
    }
    strip_leading_whitespace(line);
    strip_trailing_whitespace(line);

    if (line.empty()) {
      continue;
    }
    if (line.ends_with(":")) {
      current_line_labels.emplace(line.substr(0, line.size() - 1));
      continue;
    }

    try {
      auto& si = this->stream.emplace_back();
      si.index = this->stream.size() - 1;
      si.line_num = line_num;
      si.label_names.swap(current_line_labels);
      for (const auto& label_name : si.label_names) {
        if (!this->label_si_indexes.emplace(label_name, this->stream.size() - 1).second) {
          throw runtime_error("duplicate label name: " + label_name);
        }
      }
      size_t space_pos = line.find(' ');
      if (space_pos == string::npos) {
        si.op_name = line;
      } else {
        si.op_name = line.substr(0, space_pos);
        line = line.substr(space_pos + 1);
        strip_leading_whitespace(line);
        if (si.op_name == ".meta") {
          size_t equals_pos = line.find('=');
          if (equals_pos == string::npos) {
            this->metadata_keys.emplace(line, "");
          } else {
            this->metadata_keys.emplace(line.substr(0, equals_pos), parse_data_string(line.substr(equals_pos + 1)));
          }
          si.op_name.clear();
        } else if (si.op_name == ".binary") {
          si.assembled_data = parse_data_string(line);
          si.op_name.clear();
        } else if (si.op_name == ".data") {
          StringWriter w;
          w.put_u32l(stoul(line, nullptr, 0));
          si.assembled_data = std::move(w.str());
          si.op_name.clear();
        } else if (si.op_name == ".include") {
          si.args.emplace_back(line, true);
        } else {
          for (const auto& arg : split(line, ',')) {
            si.args.emplace_back(arg);
          }
        }
      }

      if (si.op_name == ".include") {
        si.check_arg_types({T::RAW});
        const string& inc_name = si.args[0].label_name;
        if (!get_include) {
          throw runtime_error("includes are not available");
        }
        string contents;
        try {
          si.assembled_data = this->includes_cache.at(inc_name);
        } catch (const out_of_range&) {
          try {
            si.assembled_data = get_include(inc_name);
          } catch (const exception& e) {
            throw runtime_error(string_printf("failed to get include data for %s: %s", inc_name.c_str(), e.what()));
          }
          this->includes_cache.emplace(inc_name, si.assembled_data);
        }
        si.op_name.clear();

      } else if ((si.op_name == ".zero") && !si.args.empty()) {
        si.check_arg_types({T::IMMEDIATE});
        si.assembled_data = string(si.args[0].value, '\0');
        si.op_name.clear();

      } else if ((si.op_name == ".binary") && !si.args.empty()) {
        si.check_arg_types({T::RAW});
        si.assembled_data = parse_data_string(si.args[0].label_name);
        si.op_name.clear();
      }
    } catch (const exception& e) {
      throw runtime_error(string_printf("(line %zu) parser failed: %s", line_num, e.what()));
    }
  }

  // If there are any labels at the very end, create a blank stream item so they
  // can be referenced
  if (!current_line_labels.empty()) {
    auto& si = this->stream.emplace_back();
    si.index = this->stream.size() - 1;
    si.line_num = lines.size() + 1;
    si.label_names.swap(current_line_labels);
    for (const auto& label_name : si.label_names) {
      if (!this->label_si_indexes.emplace(label_name, this->stream.size() - 1).second) {
        throw runtime_error("duplicate label name: " + label_name);
      }
    }
  }

  // Assemble the stream once without the labels ready, to get a baseline for
  // the assembled code if all branches use the largest opcode sizes
  size_t offset = 0;
  for (auto& si : this->stream) {
    si.offset = offset;
    if (!si.op_name.empty()) {
      try {
        auto fn = this->assemble_functions.at(si.op_name);
        StringWriter w;
        (this->*fn)(w, si);
        si.assembled_data = std::move(w.str());
        if (si.assembled_data.size() == 0) {
          throw runtime_error("assembler produced no output");
        }
      } catch (const exception& e) {
        throw runtime_error(string_printf("(line %zu) %s", si.line_num, e.what()));
      }
    }
    offset += si.assembled_data.size();
  }

  // Revisit any stream items that have code deltas and may need to change size
  // based on the initial assembly. We do this repeatedly until nothing changes
  // size - this gives the smallest possible result, and cannot enter an
  // infinite loop because the code can never expand during this process.
  bool any_opcode_changed_size = true;
  while (any_opcode_changed_size) {
    offset = 0;
    any_opcode_changed_size = false;
    for (auto& si : this->stream) {
      si.offset = offset;
      if (si.has_code_delta) {
        if (si.op_name.empty()) {
          throw logic_error("blank or directive stream item has code delta");
        }
        try {
          auto fn = this->assemble_functions.at(si.op_name);
          StringWriter w;
          (this->*fn)(w, si);
          if (w.size() == 0) {
            throw runtime_error("assembler produced no output");
          }
          if (w.size() > si.assembled_data.size()) {
            throw runtime_error("assembler produced longer output on second pass");
          } else if (w.size() < si.assembled_data.size()) {
            any_opcode_changed_size = true;
          }
          si.assembled_data = std::move(w.str());
        } catch (const exception& e) {
          throw runtime_error(string_printf("(line %zu) %s", si.line_num, e.what()));
        }
      }
      offset += si.assembled_data.size();
    }
  }

  // Generate the assembled code
  AssembleResult ret;
  for (const auto& si : this->stream) {
    ret.code += si.assembled_data;
  }
  for (const auto& it : this->label_si_indexes) {
    ret.label_offsets.emplace(it.first, this->stream.at(it.second).offset);
  }
  ret.metadata_keys = std::move(this->metadata_keys);
  return ret;
}

string X86Emulator::Assembler::StreamItem::str() const {
  deque<string> lines;
  string label_names_str;
  for (const auto& label_name : this->label_names) {
    label_names_str += format_data_string(label_name);
    label_names_str += ",";
  }
  if (!label_names_str.empty()) {
    label_names_str.pop_back();
  }
  string op_name_str = format_data_string(this->op_name);
  string assembled_data_str = format_data_string(this->assembled_data);
  lines.emplace_back(string_printf("StreamItem(offset=%zu, index=%zu, line_num=%zu, op_name=%s, assembled_data=%s, has_code_delta=%s, label_names=[%s])",
      this->offset,
      this->index,
      this->line_num,
      op_name_str.c_str(),
      assembled_data_str.c_str(),
      this->has_code_delta ? "true" : "false",
      label_names_str.c_str()));
  for (const auto& arg : this->args) {
    lines.emplace_back("  " + arg.str());
  }
  return join(lines, "\n");
}

[[nodiscard]] bool X86Emulator::Assembler::StreamItem::arg_types_match(
    std::initializer_list<Argument::Type> types) const {
  try {
    this->check_arg_types(types);
    return true;
  } catch (const invalid_argument&) {
    return false;
  }
}

void X86Emulator::Assembler::StreamItem::check_arg_types(
    std::initializer_list<Argument::Type> types) const {
  if (types.size() < this->args.size()) {
    throw invalid_argument("not enough arguments");
  } else if (types.size() > this->args.size()) {
    throw invalid_argument("too many arguments");
  }
  size_t z = 0;
  for (Argument::Type type : types) {
    if (!(this->args[z].type & type)) {
      throw invalid_argument(string_printf("incorrect type for argument %zu", z));
    }
    z++;
  }
}

void X86Emulator::Assembler::StreamItem::check_arg_operand_sizes(
    std::initializer_list<uint8_t> sizes) const {
  if (sizes.size() < this->args.size()) {
    throw invalid_argument("not enough arguments");
  } else if (sizes.size() > this->args.size()) {
    throw invalid_argument("too many arguments");
  }
  size_t z = 0;
  for (uint8_t size : sizes) {
    if ((this->args[z].operand_size != 0) && (this->args[z].operand_size != size)) {
      throw invalid_argument(string_printf("incorrect operand size for argument %zu", z));
    }
    z++;
  }
}

void X86Emulator::Assembler::StreamItem::check_arg_fixed_registers(
    std::initializer_list<uint8_t> reg_nums) const {
  if (reg_nums.size() < this->args.size()) {
    throw invalid_argument("not enough arguments");
  } else if (reg_nums.size() > this->args.size()) {
    throw invalid_argument("too many arguments");
  }
  size_t z = 0;
  for (uint8_t reg_num : reg_nums) {
    if (reg_num != 0xFF) {
      if (this->args[z].type != T::INT_REGISTER) {
        throw invalid_argument(string_printf("argument %zu must be a register", z));
      }
      if (this->args[z].reg_num != reg_num) {
        throw invalid_argument(string_printf("incorrect register for argument %zu", z));
      }
    }
    z++;
  }
}

uint8_t X86Emulator::Assembler::StreamItem::require_16_or_32(StringWriter& w, size_t max_args) const {
  uint8_t operand_size = this->resolve_operand_size(w, max_args);
  if (operand_size == 2) {
    w.put_u8(0x66);
  } else if (operand_size != 4) {
    throw runtime_error("invalid operand size");
  }
  return operand_size;
}

uint8_t X86Emulator::Assembler::StreamItem::resolve_operand_size(StringWriter& w, size_t max_args) const {
  uint8_t operand_size = 0;
  size_t num_args = max_args ? min<size_t>(max_args, this->args.size()) : this->args.size();
  for (size_t z = 0; z < num_args; z++) {
    const auto& arg = this->args[z];
    if (arg.operand_size != 0) {
      if (operand_size == 0) {
        operand_size = arg.operand_size;
      } else if (operand_size != arg.operand_size) {
        throw runtime_error(string_printf("conflicting operand sizes in argument %zu (arg: %hhu, pre: %hhu)", z, arg.operand_size, operand_size));
      }
    }
  }
  if (operand_size == 0) {
    throw runtime_error("cannot determine operand size");
  }
  if (operand_size == 2) {
    w.put_u8(0x66);
  }
  return operand_size;
}

uint8_t X86Emulator::Assembler::StreamItem::get_size_mnemonic_suffix(const string& base_name) const {
  if (!this->op_name.starts_with(base_name)) {
    throw runtime_error("invalid opcode name");
  }
  if (this->op_name == base_name) {
    return 0;
  }
  if (this->op_name.size() != base_name.size() + 1) {
    throw runtime_error("invalid opcode suffix");
  }
  if (this->op_name[base_name.size()] == 'b') {
    return 1;
  } else if (this->op_name[base_name.size()] == 'w') {
    return 2;
  } else if (this->op_name[base_name.size()] == 'd') {
    return 4;
  } else {
    throw runtime_error("invalid opcode suffix");
  }
}

uint8_t X86Emulator::Assembler::StreamItem::require_size_mnemonic_suffix(
    StringWriter& w, const string& base_name) const {
  if (!this->op_name.starts_with(base_name)) {
    throw runtime_error("invalid opcode name");
  }
  if (this->op_name == base_name) {
    throw runtime_error(base_name + " should not be used directly; use b/w/d suffix to specify size");
  }
  if (this->op_name.size() != base_name.size() + 1) {
    throw runtime_error("invalid opcode suffix");
  }
  if (this->op_name[base_name.size()] == 'b') {
    return 1;
  } else if (this->op_name[base_name.size()] == 'w') {
    w.put_u8(0x66);
    return 2;
  } else if (this->op_name[base_name.size()] == 'd') {
    return 4;
  } else {
    throw runtime_error("invalid opcode suffix");
  }
}

void X86Emulator::Assembler::encode_imm(StringWriter& w, uint64_t value, uint8_t operand_size) const {
  switch (operand_size) {
    case 1:
      w.put_u8(value);
      break;
    case 2:
      w.put_u16l(value);
      break;
    case 4:
      w.put_u32l(value);
      break;
    case 8:
      w.put_u64l(value);
      break;
    default:
      throw runtime_error("invalid operand size");
  }
}

void X86Emulator::Assembler::encode_rm(
    StringWriter& w, const Argument& mem_ref, const Argument& reg_ref) const {
  if (!reg_ref.is_reg_ref()) {
    throw runtime_error("invalid r/m register field");
  }
  this->encode_rm(w, mem_ref, reg_ref.reg_num);
}

void X86Emulator::Assembler::encode_rm(StringWriter& w, const Argument& arg, uint8_t op_type) const {
  if (!(arg.type & T::MEM_OR_REG)) {
    throw runtime_error("invalid r/m memory reference field");
  }
  // The r/m byte is like TTNNNBBB, where:
  //   T = type
  //   N = non-reference register or opcode type
  //   B = base register

  uint8_t param = ((op_type << 3) & 0x38);

  // If T == 11, then EA is a register, not memory, with no special cases
  if (arg.is_reg_ref()) {
    w.put_u8(0xC0 | param | (arg.reg_num & 0x07)); // rm

  } else if (arg.type == T::MEMORY_REFERENCE) {
    uint8_t disp_type;
    if (arg.value == 0) {
      disp_type = 0x00;
    } else if (can_encode_as_int8(arg.value)) {
      disp_type = 0x40;
    } else if (can_encode_as_int32(arg.value)) {
      disp_type = 0x80;
    } else {
      throw invalid_argument("displacement cannot be encoded as a 32-bit signed integer");
    }

    if ((arg.scale == 0) && (arg.reg_num == 0xFF)) {
      // Just [DISP] - always disp32
      w.put_u8(0x05 | param); // rm
      disp_type = 0x80;

    } else if (!arg.scale) {
      // [REG] or [REG + DISP]
      if (arg.reg_num == 4) {
        // [esp] or [esp + DISP] - need scaled index byte
        w.put_u8(disp_type | param | 0x04); // rm
        w.put_u8(0x24); // sib (esp, no index reg)
      } else {
        // Force a disp8 byte if reg_num is 5 (ebp) since there's no encoding
        // for just [ebp]
        if (arg.reg_num == 5 && disp_type == 0x00) {
          disp_type = 0x40;
        }
        w.put_u8(disp_type | param | (arg.reg_num & 0x07)); // rm
      }

    } else {
      // ESP can't be used as an index register, but we can switch it for the
      // base register if scale is 1
      uint8_t base_reg = arg.reg_num;
      uint8_t index_reg = arg.reg_num2;
      if (index_reg == 4) {
        if ((base_reg != 4) && (arg.scale == 1)) {
          uint8_t t = base_reg;
          base_reg = index_reg;
          index_reg = t;
        } else {
          throw runtime_error("esp cannot be used as a scaled index register");
        }
      }

      uint8_t scale_type;
      switch (arg.scale) {
        case 1:
          scale_type = 0x00;
          break;
        case 2:
          scale_type = 0x40;
          break;
        case 4:
          scale_type = 0x80;
          break;
        case 8:
          scale_type = 0xC0;
          break;
        default:
          throw runtime_error("invalid scale size");
      }

      // Force a disp8 byte if reg_num is 5 (ebp) since there's no encoding for
      // just [ebp]
      if (base_reg == 5 && disp_type == 0x00) {
        disp_type = 0x40;
      }

      w.put_u8(disp_type | param | 0x04); // rm
      w.put_u8(scale_type | ((index_reg << 3) & 0x38) | base_reg); // sib
    }

    if (disp_type == 0x40) {
      w.put_u8(arg.value); // disp8
    } else if (disp_type == 0x80) {
      w.put_u32(arg.value); // disp32
    }

  } else {
    throw runtime_error("invalid argument type");
  }
}

uint32_t X86Emulator::Assembler::compute_branch_delta(size_t from_index, size_t to_index) const {
  bool is_reverse = (from_index > to_index);
  size_t start_index = is_reverse ? to_index : from_index;
  size_t end_index = is_reverse ? from_index : to_index;
  if (end_index > this->stream.size()) {
    throw runtime_error("branch beyond end of stream");
  }

  uint32_t distance = 0;
  for (size_t z = start_index; z < end_index; z++) {
    distance += this->stream[z].assembled_data.size();
  }

  return is_reverse ? (-distance) : distance;
}

void X86Emulator::Assembler::asm_aaa_aas_aad_aam(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  if (si.op_name == "aaa") {
    w.put_u8(0x37);
  } else if (si.op_name == "aas") {
    w.put_u8(0x3F);
  } else if (si.op_name == "aam") {
    w.put_u8(0xD4);
    w.put_u8(0x0A);
  } else if (si.op_name == "aad") {
    w.put_u8(0xD5);
    w.put_u8(0x0A);
  } else {
    throw logic_error("invalid opcode name");
  }
}

template <size_t Max>
uint8_t find_mnemonic(const std::array<const char* const, Max>& names, const std::string& name) {
  for (size_t z = 0; z < Max; z++) {
    if (names[z] == name) {
      return z;
    }
  }
  throw runtime_error("unknown opcode");
}

uint8_t condition_code_for_mnemonic(const std::string& mnemonic) {
  if (mnemonic == "o") {
    return 0x00;
  } else if (mnemonic == "no") {
    return 0x01;
  } else if (mnemonic == "b" || mnemonic == "nae" || mnemonic == "c") {
    return 0x02;
  } else if (mnemonic == "nb" || mnemonic == "ae" || mnemonic == "nc") {
    return 0x03;
  } else if (mnemonic == "z" || mnemonic == "e") {
    return 0x04;
  } else if (mnemonic == "nz" || mnemonic == "ne") {
    return 0x05;
  } else if (mnemonic == "be" || mnemonic == "na") {
    return 0x06;
  } else if (mnemonic == "nbe" || mnemonic == "a") {
    return 0x07;
  } else if (mnemonic == "s") {
    return 0x08;
  } else if (mnemonic == "ns") {
    return 0x09;
  } else if (mnemonic == "p" || mnemonic == "pe") {
    return 0x0A;
  } else if (mnemonic == "np" || mnemonic == "po") {
    return 0x0B;
  } else if (mnemonic == "l" || mnemonic == "nge") {
    return 0x0C;
  } else if (mnemonic == "nl" || mnemonic == "ge") {
    return 0x0D;
  } else if (mnemonic == "le" || mnemonic == "ng") {
    return 0x0E;
  } else if (mnemonic == "nle" || mnemonic == "g") {
    return 0x0F;
  } else {
    throw runtime_error("unknown condition code mnemonic");
  }
}

void X86Emulator::Assembler::asm_add_or_adc_sbb_and_sub_xor_cmp(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::MEM_OR_IREG, T::MEM_OR_IREG_OR_IMM});

  uint8_t subopcode = find_mnemonic(integer_math_opcode_names, si.op_name);
  uint8_t operand_size = si.resolve_operand_size(w);

  if (si.args[1].type == T::IMMEDIATE) {
    if (si.args[0].type == T::INT_REGISTER && si.args[0].reg_num == 0) {
      // <op> al/ax/eax, imm
      w.put_u8((subopcode << 3) | ((operand_size > 1) ? 0x05 : 0x04));
      switch (operand_size) {
        case 1:
          w.put_u8(si.args[1].value);
          break;
        case 2:
          w.put_u16(si.args[1].value);
          break;
        case 4:
          w.put_u32(si.args[1].value);
          break;
        default:
          throw runtime_error("invalid operand size");
      }
    } else {
      // <op> r/m, imm
      // TODO: If the value is close enough to zero, use the 0x83 form of this
      // instead for the 2 and 4 cases
      bool use_imm8 = can_encode_as_int8(si.args[1].value);
      w.put_u8(0x80 | ((operand_size > 1) ? 0x01 : 0x00) | (use_imm8 ? 2 : 0));
      this->encode_rm(w, si.args[0], subopcode);
      this->encode_imm(w, si.args[1].value, use_imm8 ? 1 : operand_size);
    }
  } else {
    // <op> r/m, r OR <op> r, r/m
    if (!si.args[1].is_reg_ref()) {
      w.put_u8((subopcode << 3) | ((operand_size > 1) ? 0x03 : 0x02));
      this->encode_rm(w, si.args[1], si.args[0]);
    } else {
      w.put_u8((subopcode << 3) | ((operand_size > 1) ? 0x01 : 0x00));
      this->encode_rm(w, si.args[0], si.args[1]);
    }
  }
}

void X86Emulator::Assembler::asm_amx_adx(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::IMMEDIATE});
  w.put_u8(si.op_name == "adx" ? 0xD5 : 0xD4);
  w.put_u8(si.args[0].value);
}

void X86Emulator::Assembler::asm_bsf_bsr(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::INT_REGISTER, T::MEM_OR_IREG});
  si.require_16_or_32(w);
  w.put_u8(0x0F);
  w.put_u8(0xBC | (si.op_name == "bsr"));
  this->encode_rm(w, si.args[1], si.args[0]);
}

void X86Emulator::Assembler::asm_bswap(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::INT_REGISTER});
  si.require_16_or_32(w);
  w.put_u8(0x0F);
  w.put_u8(0xC8 + si.args[0].reg_num);
}

void X86Emulator::Assembler::asm_bt_bts_btr_btc(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::MEM_OR_IREG, T::MEM_OR_IREG_OR_IMM});

  uint8_t subopcode = find_mnemonic(bit_test_opcode_names, si.op_name);

  si.require_16_or_32(w);
  w.put_u8(0x0F);
  if (si.args[1].type == T::IMMEDIATE) {
    w.put_u8(0xBA);
    this->encode_rm(w, si.args[0], subopcode | 4);
  } else {
    w.put_u8(0xA3 | (subopcode << 3));
    this->encode_rm(w, si.args[0], si.args[1]);
  }
}

uint32_t X86Emulator::Assembler::compute_branch_delta_from_arg(const StreamItem& si, const Argument& arg) const {
  if (arg.type == T::BRANCH_TARGET) {
    // On first pass, we can't know the correct delta, so just pick a far-away
    // delta to get the largest opcode size
    return si.assembled_data.empty()
        ? 0x80000000
        : this->compute_branch_delta(si.index + 1, this->label_si_indexes.at(arg.label_name));

  } else if (arg.type == T::IMMEDIATE) {
    if (arg.value) { // Relative (+X or -X)
      return arg.value;
    } else { // Absolute (X without + or -)
      return arg.value - (this->stream[si.index + 1].offset + this->start_address);
    }

  } else {
    throw logic_error("static branch delta must come from BRANCH_TARGET or IMMEDIATE argument");
  }
}

void X86Emulator::Assembler::asm_call_jmp(StringWriter& w, StreamItem& si) const {
  bool is_call = (si.op_name == "call");
  bool is_branch_target = si.arg_types_match({T::BRANCH_TARGET});
  bool is_immediate = si.arg_types_match({T::IMMEDIATE});
  if (is_branch_target || is_immediate) {
    si.has_code_delta = true;

    uint32_t delta = this->compute_branch_delta_from_arg(si, si.args[0]);
    if (is_call) {
      w.put_u8(0xE8);
      w.put_u32l(delta);
    } else if (delta == sign_extend<uint32_t, uint8_t>(delta)) {
      w.put_u8(0xEB);
      w.put_u8(delta);
    } else {
      w.put_u8(0xE9);
      w.put_u32l(delta);
    }

  } else if (si.arg_types_match({T::MEM_OR_IREG})) {
    if (si.args[0].operand_size != 0 && si.args[0].operand_size != 4) {
      throw runtime_error("invalid operand size for call/jmp opcode");
    }
    w.put_u8(0xFF);
    this->encode_rm(w, si.args[0], is_call ? 2 : 4);
  } else {
    throw runtime_error("invalid arguemnt type for call/jmp opcode");
  }
}

void X86Emulator::Assembler::asm_cbw_cwde(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  if (si.op_name == "cbw") {
    w.put_u8(0x66);
  }
  w.put_u8(0x98);
}

void X86Emulator::Assembler::asm_cwd_cdq(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  if (si.op_name == "cwd") {
    w.put_u8(0x66);
  }
  w.put_u8(0x99);
}

void X86Emulator::Assembler::asm_clc(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0xF8);
}

void X86Emulator::Assembler::asm_cld(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0xFC);
}
void X86Emulator::Assembler::asm_cli(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0xFA);
}

void X86Emulator::Assembler::asm_cmc(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0xF5);
}

void X86Emulator::Assembler::asm_cmov_mnemonics(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::INT_REGISTER, T::MEM_OR_IREG});
  uint8_t operand_size = si.resolve_operand_size(w);
  if (operand_size == 1) {
    throw runtime_error("cmov cannot be used with byte operands");
  }
  w.put_u8(0x0F);
  w.put_u8(0x40 | condition_code_for_mnemonic(si.op_name.substr(4)));
  this->encode_rm(w, si.args[1], si.args[0]);
}

void X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});

  static const array<pair<const char*, uint8_t>, 7> defs = {{{"ins", 0x6C}, {"outs", 0x6E}, {"movs", 0xA4}, {"cmps", 0xA6}, {"stos", 0xAA}, {"lods", 0xAC}, {"scas", 0xAE}}};

  uint8_t operand_size = 0;
  uint8_t base_opcode = 0;
  for (const auto& def : defs) {
    if (si.op_name.starts_with(def.first)) {
      operand_size = si.require_size_mnemonic_suffix(w, def.first);
      base_opcode = def.second;
      break;
    }
  }
  if (base_opcode == 0) {
    throw runtime_error("invalid string opcode");
  }

  w.put_u8(base_opcode | ((operand_size == 1) ? 0x00 : 0x01));
}

void X86Emulator::Assembler::asm_cmpxchg(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::MEM_OR_IREG, T::INT_REGISTER, T::INT_REGISTER});
  if (si.args[1].reg_num != 0) {
    throw runtime_error("second argument must be al/ax/eax");
  }
  uint8_t operand_size = si.resolve_operand_size(w);
  w.put_u8(0x0F);
  w.put_u8(operand_size == 1 ? 0xB0 : 0xB1);
  this->encode_rm(w, si.args[0], si.args[2]);
}

void X86Emulator::Assembler::asm_cmpxchg8b(StringWriter& w, StreamItem& si) const {
  if (si.arg_types_match({T::MEMORY_REFERENCE, T::INT_REGISTER, T::INT_REGISTER})) {
    si.check_arg_operand_sizes({8, 4, 4});
    si.check_arg_fixed_registers({0xFF, 0, 2});
  } else {
    si.check_arg_types({T::MEMORY_REFERENCE});
    si.check_arg_operand_sizes({8});
  }
  w.put_u8(0x0F);
  w.put_u8(0xC7);
  this->encode_rm(w, si.args[0], 1);
}

void X86Emulator::Assembler::asm_cpuid(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0x0F);
  w.put_u8(0xA2);
}

void X86Emulator::Assembler::asm_crc32(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::INT_REGISTER, T::MEM_OR_IREG});
  si.check_arg_operand_sizes({4, 1});
  w.put_u8(0xF2);
  w.put_u8(0x0F);
  w.put_u8(0x38);
  w.put_u8(0xF0);
  this->encode_rm(w, si.args[1], si.args[0]);
}

void X86Emulator::Assembler::asm_cs(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0x2E);
}

void X86Emulator::Assembler::asm_daa(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0x27);
}

void X86Emulator::Assembler::asm_das(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0x2F);
}

void X86Emulator::Assembler::asm_inc_dec(StringWriter& w, StreamItem& si) const {
  bool is_dec = (si.op_name == "dec");
  si.check_arg_types({T::MEM_OR_IREG});
  uint8_t operand_size = si.resolve_operand_size(w);
  if (si.args[0].is_reg_ref() && si.args[0].operand_size > 1) {
    w.put_u8((is_dec ? 0x48 : 0x40) | (si.args[0].reg_num & 7));
  } else {
    w.put_u8(is_dec ? 0xFF : 0xFE);
    this->encode_rm(w, si.args[0], (operand_size == 1) ? 0 : 1);
  }
}

void X86Emulator::Assembler::asm_div_idiv(StringWriter& w, StreamItem& si) const {
  bool is_idiv = (si.op_name == "idiv");

  uint8_t operand_size;
  if (si.arg_types_match({T::INT_REGISTER, T::INT_REGISTER, T::INT_REGISTER, T::MEM_OR_IREG})) {
    si.check_arg_fixed_registers({0, 4, 0, 0xFF}); // al, ah, ax, r/m8
    si.check_arg_operand_sizes({1, 1, 2, 1});
    operand_size = 1;
  } else if (si.arg_types_match({T::INT_REGISTER, T::INT_REGISTER, T::MEM_OR_IREG})) {
    si.check_arg_fixed_registers({2, 0, 0xFF}); // (e)dx, (e)ax, r/m16/32
    operand_size = si.resolve_operand_size(w);
  } else if (si.arg_types_match({T::MEM_OR_IREG})) {
    operand_size = si.resolve_operand_size(w);
  } else {
    throw runtime_error("invalid arguments");
  }

  w.put_u8((operand_size == 1) ? 0xF6 : 0xF7);
  this->encode_rm(w, si.args[si.args.size() - 1], is_idiv ? 7 : 6);
}

void X86Emulator::Assembler::asm_ds(StringWriter& w, StreamItem& si) const {
  si.check_arg_fixed_registers({});
  w.put_u8(0x3E);
}

void X86Emulator::Assembler::asm_enter(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::IMMEDIATE, T::IMMEDIATE});
  w.put_u8(0xC8);
  w.put_u16l(si.args[0].value);
  w.put_u8(si.args[1].value);
}

void X86Emulator::Assembler::asm_es(StringWriter& w, StreamItem& si) const {
  si.check_arg_fixed_registers({});
  w.put_u8(0x26);
}

void X86Emulator::Assembler::asm_fs(StringWriter& w, StreamItem& si) const {
  si.check_arg_fixed_registers({});
  w.put_u8(0x64);
}

void X86Emulator::Assembler::asm_gs(StringWriter& w, StreamItem& si) const {
  si.check_arg_fixed_registers({});
  w.put_u8(0x65);
}

void X86Emulator::Assembler::asm_hlt(StringWriter& w, StreamItem& si) const {
  si.check_arg_fixed_registers({});
  w.put_u8(0xF4);
}

void X86Emulator::Assembler::asm_imul_mul(StringWriter& w, StreamItem& si) const {
  bool is_imul = (si.op_name == "imul");
  if (is_imul) {
    if (si.arg_types_match({T::INT_REGISTER, T::MEM_OR_IREG})) {
      // 0F AF  imul r16/32, r/m16/32
      si.require_16_or_32(w);
      w.put_u8(0x0F);
      w.put_u8(0xAF);
      this->encode_rm(w, si.args[1], si.args[0]);
      return;

    } else if (si.arg_types_match({T::INT_REGISTER, T::MEM_OR_IREG, T::IMMEDIATE})) {
      // 69     imul r16/32, r/m16/32, imm16/32
      // 6B     imul r16/32, r/m16/32, imm8
      uint8_t operand_size = si.resolve_operand_size(w);
      bool short_imm = (sign_extend<uint64_t, uint8_t>(si.args[2].value) == si.args[2].value);
      w.put_u8(short_imm ? 0x6B : 0x69);
      this->encode_rm(w, si.args[1], si.args[0]);
      this->encode_imm(w, si.args[2].value, short_imm ? 1 : operand_size);
      return;
    }
  }

  uint8_t operand_size;
  if (si.arg_types_match({T::INT_REGISTER, T::INT_REGISTER, T::MEM_OR_IREG})) {
    if (si.args[1].operand_size == 1) {
      // F6/4   mul ax, al, r/m8
      // F6/5   imul ax, al, r/m8
      si.check_arg_fixed_registers({0, 0, 0xFF});
      si.check_arg_operand_sizes({2, 1, 1});
      operand_size = 1;
    } else {
      // F7/4   mul (e)dx, (e)ax, r/m16/32
      // F7/5   imul (e)dx, (e)ax, r/m16/32
      si.check_arg_fixed_registers({2, 0, 0xFF});
      operand_size = si.resolve_operand_size(w);
    }
  } else {
    // Same as F6/F7 cases but first 2 args are implicit
    si.check_arg_types({T::MEM_OR_IREG});
    operand_size = si.resolve_operand_size(w);
  }

  w.put_u8((operand_size == 1) ? 0xF6 : 0xF7);
  this->encode_rm(w, si.args[si.args.size() - 1], is_imul ? 5 : 4);
}

void X86Emulator::Assembler::asm_in_out(StringWriter& w, StreamItem& si) const {
  bool is_out = (si.op_name == "out");
  bool is_imm = false;
  uint8_t operand_size;
  if (is_out) {
    if (si.arg_types_match({T::IMMEDIATE, T::INT_REGISTER})) {
      si.check_arg_fixed_registers({0xFF, 0});
      is_imm = true;
    } else {
      si.check_arg_types({T::INT_REGISTER, T::INT_REGISTER});
      si.check_arg_fixed_registers({2, 0});
    }
    operand_size = si.args[1].operand_size;
  } else {
    if (si.arg_types_match({T::INT_REGISTER, T::IMMEDIATE})) {
      si.check_arg_fixed_registers({0, 0xFF});
      is_imm = true;
    } else {
      si.check_arg_types({T::INT_REGISTER, T::INT_REGISTER});
      si.check_arg_fixed_registers({0, 2});
    }
    operand_size = si.args[0].operand_size;
  }

  if (operand_size == 2) {
    w.put_u8(0x66);
  }
  w.put_u8(0xE4 | (is_imm ? 0x00 : 0x08) | (is_out ? 0x02 : 0x00) | ((operand_size == 1) ? 0x00 : 0x01));
}

void X86Emulator::Assembler::asm_int(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::IMMEDIATE});
  if (si.args[0].value == 3) {
    w.put_u8(0xCC);
  } else {
    w.put_u8(0xCD);
    w.put_u8(si.args[0].value);
  }
}

void X86Emulator::Assembler::asm_iret(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0xCF);
}

void X86Emulator::Assembler::asm_j_mnemonics(StringWriter& w, StreamItem& si) const {
  if (!si.arg_types_match({T::BRANCH_TARGET}) && !si.arg_types_match({T::IMMEDIATE})) {
    throw runtime_error("incorrect argument type");
  }

  si.has_code_delta = true;
  uint32_t delta = this->compute_branch_delta_from_arg(si, si.args[0]);

  uint8_t condition_code = condition_code_for_mnemonic(si.op_name.substr(1));
  if (delta == sign_extend<uint32_t, uint8_t>(delta)) {
    w.put_u8(0x70 | condition_code);
    w.put_u8(delta);
  } else {
    w.put_u8(0x0F);
    w.put_u8(0x80 | condition_code);
    w.put_u32l(delta);
  }
}

void X86Emulator::Assembler::asm_jcxz_jecxz_loop_mnemonics(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::BRANCH_TARGET});

  si.has_code_delta = true;

  uint32_t delta = this->compute_branch_delta_from_arg(si, si.args[0]);
  if (delta != sign_extend<uint32_t, uint8_t>(delta)) {
    throw runtime_error("target too far away for conditional jump opcode");
  }

  if (si.op_name == "loopnz" || si.op_name == "loopne") {
    w.put_u8(0xE0);
  } else if (si.op_name == "loopz" || si.op_name == "loope") {
    w.put_u8(0xE1);
  } else if (si.op_name == "loop") {
    w.put_u8(0xE2);
  } else if (si.op_name == "jcxz") {
    w.put_u8(0x66);
    w.put_u8(0xE3);
  } else if (si.op_name == "jecxz") {
    w.put_u8(0xE3);
  } else {
    throw runtime_error("invalid loop opcode");
  }
  w.put_u8(delta);
}

void X86Emulator::Assembler::asm_lahf_sahf(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(si.op_name == "sahf" ? 0x9E : 0x9F);
}

void X86Emulator::Assembler::asm_lea(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::INT_REGISTER, T::MEMORY_REFERENCE});
  if (si.args[1].is_reg_ref()) {
    throw runtime_error("cannot take the address of a register");
  }
  if (si.args[0].operand_size != 4) {
    throw runtime_error("incorrect register size for lea opcode");
  }
  w.put_u8(0x8D);
  this->encode_rm(w, si.args[1], si.args[0]);
}

void X86Emulator::Assembler::asm_leave(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0xC9);
}

void X86Emulator::Assembler::asm_lock(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0xF0);
}

void X86Emulator::Assembler::asm_mov(StringWriter& w, StreamItem& si) const {
  uint8_t operand_size = si.resolve_operand_size(w);
  if (si.arg_types_match({T::INT_REGISTER, T::IMMEDIATE})) {
    // B0+r   mov r8, imm8
    // B8+r   mov r16/32, imm16/32
    w.put_u8(0xB0 | ((operand_size == 1) ? 0x00 : 0x08) | (si.args[0].reg_num & 7));
    this->encode_imm(w, si.args[1].value, operand_size);

  } else if (si.arg_types_match({T::MEMORY_REFERENCE, T::IMMEDIATE})) {
    // C6     mov r/m8, imm16/32
    // C7     mov r/m16/32, imm16/32
    w.put_u8(0xC6 | ((operand_size == 1) ? 0x00 : 0x01));
    this->encode_rm(w, si.args[0], 0);
    this->encode_imm(w, si.args[1].value, operand_size);

  } else {
    bool dest_is_mem;
    if (si.arg_types_match({T::MEM_OR_IREG, T::INT_REGISTER})) {
      dest_is_mem = true;
    } else if (si.arg_types_match({T::INT_REGISTER, T::MEM_OR_IREG})) {
      dest_is_mem = false;
    } else {
      throw runtime_error("invalid argument types for mov opcode");
    }
    const auto& mem_arg = si.args[dest_is_mem ? 0 : 1];
    const auto& reg_arg = si.args[dest_is_mem ? 1 : 0];

    if (reg_arg.reg_num == 0 && mem_arg.reg_num == 0xFF && mem_arg.scale == 0) {
      // A0     mov al, [disp32]
      // A1     mov (e)ax, [disp32]
      // A2     mov [disp32], al
      // A3     mov [disp32], (e)ax
      w.put_u8(0xA0 | (dest_is_mem ? 0x02 : 0x00) | ((operand_size == 1) ? 0x00 : 0x01));
      this->encode_imm(w, mem_arg.value, 4);

    } else {
      // 88     mov r/m8, r8
      // 89     mov r/m16/32, r16/32
      // 8A     mov r8, r/m8
      // 8B     mov r16/32, r/m16/32
      w.put_u8(0x88 | (dest_is_mem ? 0x00 : 0x02) | ((operand_size == 1) ? 0x00 : 0x01));
      this->encode_rm(w, mem_arg, reg_arg);
    }
  }

  // TODO: mov segment regs, debug regs, control regs
}

void X86Emulator::Assembler::asm_movbe(StringWriter& w, StreamItem& si) const {
  bool dest_is_mem;
  if (si.arg_types_match({T::MEM_OR_IREG, T::INT_REGISTER})) {
    dest_is_mem = true;
  } else if (si.arg_types_match({T::INT_REGISTER, T::MEM_OR_IREG})) {
    dest_is_mem = false;
  } else {
    throw runtime_error("invalid argument types for mov opcode");
  }
  const auto& mem_arg = si.args[dest_is_mem ? 0 : 1];
  const auto& reg_arg = si.args[dest_is_mem ? 1 : 0];

  w.put_u8(0x0F);
  w.put_u8(0x38);
  w.put_u8(0xF0 | (dest_is_mem ? 0x01 : 0x00));
  this->encode_rm(w, mem_arg, reg_arg);
}

void X86Emulator::Assembler::asm_movsx_movzx(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::INT_REGISTER, T::MEM_OR_IREG});

  if (si.args[1].operand_size == 0) {
    throw runtime_error("cannot determine operand size");
  }
  if (si.args[1].operand_size > 2) {
    throw runtime_error("invalid operand size");
  }

  uint8_t base_opcode = (si.op_name == "movzx") ? 0xB6 : 0xBE;
  w.put_u8(0x0F);
  w.put_u8(base_opcode | ((si.args[1].operand_size == 1) ? 0x00 : 0x01));
  this->encode_rm(w, si.args[1], si.args[0]);
}

void X86Emulator::Assembler::asm_neg_not(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::MEM_OR_IREG});
  uint8_t operand_size = si.resolve_operand_size(w);
  w.put_u8(operand_size == 1 ? 0xF6 : 0xF7);
  this->encode_rm(w, si.args[0], (si.op_name == "not" ? 2 : 3));
}

void X86Emulator::Assembler::asm_nop(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0x90);
}

void X86Emulator::Assembler::asm_pop_push(StringWriter& w, StreamItem& si) const {
  bool is_push = (si.op_name == "push");

  if (si.arg_types_match({T::INT_REGISTER})) {
    // 50+r  push r16/32
    // 58+r  pop r16/32
    si.require_16_or_32(w);
    w.put_u8(0x50 | (is_push ? 0x00 : 0x08) | (si.args[0].reg_num & 7));
  } else if (si.arg_types_match({T::MEMORY_REFERENCE})) {
    // FF/6  push r/m16/32
    // 8F/0  pop r/m16/32
    si.require_16_or_32(w);
    w.put_u8(is_push ? 0xFF : 0x8F);
    this->encode_rm(w, si.args[0], is_push ? 6 : 0);
  } else if (is_push && si.arg_types_match({T::IMMEDIATE})) {
    // 68    push imm16/32
    // 6A    push imm8
    if (sign_extend<uint32_t, uint8_t>(si.args[0].value) == si.args[0].value) {
      w.put_u8(0x6A);
      this->encode_imm(w, si.args[0].value, 1);
    } else {
      // TODO: Can we do 66 68 <imm16> here if the value will fit?
      w.put_u8(0x68);
      this->encode_imm(w, si.args[0].value, 4);
    }
  } else {
    // TODO:
    // 06    push es
    // 07    pop es
    // 0E    push cs
    // 16    push ss
    // 17    pop ss
    // 1E    push ds
    // 1F    pop ds
    // 0FA0  push fs
    // 0FA1  pop fs
    // 0FA8  push gs
    // 0FA9  pop gs
    throw runtime_error("invalid argumentsto pop opcode");
  }
}

void X86Emulator::Assembler::asm_popa_popad(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0x61);
}

void X86Emulator::Assembler::asm_popcnt(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::INT_REGISTER, T::MEMORY_REFERENCE});
  si.require_16_or_32(w);
  w.put_u8(0xF3);
  w.put_u8(0x0F);
  w.put_u8(0xB8);
  this->encode_rm(w, si.args[1], si.args[0]);
}

void X86Emulator::Assembler::asm_popf_popfd(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0x9D);
}

void X86Emulator::Assembler::asm_pusha_pushad(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0x60);
}

void X86Emulator::Assembler::asm_pushf_pushfd(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0x9C);
}

void X86Emulator::Assembler::asm_rol_ror_rcl_rcr_shl_sal_shr_sar(StringWriter& w, StreamItem& si) const {
  uint8_t subopcode = find_mnemonic(bit_shift_opcode_names, si.op_name);

  uint8_t operand_size = si.resolve_operand_size(w, 1);
  if (si.arg_types_match({T::MEM_OR_IREG, T::IMMEDIATE})) {
    w.put_u8(0xC0 | (si.args[1].value == 1 ? 0x10 : 0x00) | (operand_size == 0 ? 0x00 : 0x01));
    this->encode_rm(w, si.args[0], subopcode);
    if (si.args[1].value != 1) {
      this->encode_imm(w, si.args[1].value, 1);
    }
  } else {
    si.check_arg_types({T::MEM_OR_IREG, T::INT_REGISTER});
    si.check_arg_fixed_registers({0xFF, 1});
    si.check_arg_operand_sizes({0xFF, 1});
    w.put_u8(0xD2 | (operand_size == 0 ? 0x00 : 0x01));
    this->encode_rm(w, si.args[0], subopcode);
  }
}

void X86Emulator::Assembler::asm_rdtsc(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0x0F);
  w.put_u8(0x31);
}

void X86Emulator::Assembler::asm_rep_mnemomics(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  if (si.op_name == "repnz" || si.op_name == "repne") {
    w.put_u8(0xF2);
  } else if (si.op_name == "repz" || si.op_name == "repe") {
    w.put_u8(0xF3);
  } else {
    throw runtime_error("invalid repeat opcode");
  }
}

void X86Emulator::Assembler::asm_ret(StringWriter& w, StreamItem& si) const {
  if (si.arg_types_match({T::IMMEDIATE})) {
    w.put_u8(0xC2);
    w.put_u16l(si.args[0].value);
  } else {
    si.check_arg_types({});
    w.put_u8(0xC3);
  }
}

void X86Emulator::Assembler::asm_salc_setalc(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0xD6);
}

void X86Emulator::Assembler::asm_set_mnemonics(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::MEM_OR_IREG});
  w.put_u8(0x0F);
  w.put_u8(0x90 | condition_code_for_mnemonic(si.op_name.substr(1)));
  this->encode_rm(w, si.args[0], 0);
}

void X86Emulator::Assembler::asm_shld_shrd(StringWriter& w, StreamItem& si) const {
  uint8_t base_opcode = (si.op_name == "shrd") ? 0xAC : 0xA4;
  si.require_16_or_32(w, 2);
  w.put_u8(0x0F);
  if (si.arg_types_match({T::MEM_OR_IREG, T::INT_REGISTER, T::IMMEDIATE})) {
    w.put_u8(base_opcode);
    this->encode_rm(w, si.args[0], si.args[1]);
    this->encode_imm(w, si.args[1].value, 1);
  } else if (si.arg_types_match({T::MEM_OR_IREG, T::INT_REGISTER, T::INT_REGISTER})) {
    si.check_arg_fixed_registers({0xFF, 0xFF, 1}); // last arg must be cl
    si.check_arg_operand_sizes({0xFF, 0xFF, 1});
    w.put_u8(base_opcode | 0x01);
    this->encode_rm(w, si.args[0], si.args[1]);
  } else {
    throw runtime_error("invalid argument type(s)");
  }
}

void X86Emulator::Assembler::asm_ss(StringWriter& w, StreamItem& si) const {
  si.check_arg_fixed_registers({});
  w.put_u8(0x36);
}

void X86Emulator::Assembler::asm_stc(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0xF9);
}

void X86Emulator::Assembler::asm_std(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0xFD);
}

void X86Emulator::Assembler::asm_sti(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({});
  w.put_u8(0xFB);
}

void X86Emulator::Assembler::asm_test(StringWriter& w, StreamItem& si) const {
  uint8_t operand_size = si.resolve_operand_size(w);
  if (si.arg_types_match({T::MEM_OR_IREG, T::INT_REGISTER})) {
    // 84    test r/m8, r8
    // 85    test r/m16/32, r16/32
    w.put_u8(0x84 | (operand_size == 1 ? 0x00 : 0x01));
    this->encode_rm(w, si.args[0], si.args[1]);
  } else if (si.arg_types_match({T::MEM_OR_IREG, T::IMMEDIATE})) {
    if (si.args[0].is_reg_ref() && si.args[0].reg_num == 0) {
      // A8    test al, imm8
      // A9    test (e)ax, imm16/32
      w.put_u8(0xA8 | (operand_size == 1 ? 0x00 : 0x01));
      this->encode_imm(w, si.args[1].value, operand_size);
    } else {
      // F6/0  test r/m8, imm8 (also F6/1)
      // F7/0  test r/m16/32, imm16/32 (also F7/1)
      w.put_u8(0xF6 | (operand_size == 1 ? 0x00 : 0x01));
      this->encode_rm(w, si.args[0], 0);
      this->encode_imm(w, si.args[1].value, operand_size);
    }
  } else {
    throw runtime_error("invalid arguments to test opcode");
  }
}

void X86Emulator::Assembler::asm_xadd(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::MEM_OR_IREG, T::INT_REGISTER});
  uint8_t operand_size = si.resolve_operand_size(w);
  w.put_u8(0x0F);
  w.put_u8(0xC0 | ((operand_size == 1) ? 0x00 : 0x01));
  this->encode_rm(w, si.args[0], si.args[1]);
}

void X86Emulator::Assembler::asm_xchg(StringWriter& w, StreamItem& si) const {
  uint8_t operand_size = si.resolve_operand_size(w);

  bool dest_is_mem;
  if (si.arg_types_match({T::MEM_OR_IREG, T::INT_REGISTER})) {
    dest_is_mem = true;
  } else if (si.arg_types_match({T::INT_REGISTER, T::MEM_OR_IREG})) {
    dest_is_mem = false;
  } else {
    throw runtime_error("invalid argument types for mov opcode");
  }
  const auto& mem_arg = si.args[dest_is_mem ? 0 : 1];
  const auto& reg_arg = si.args[dest_is_mem ? 1 : 0];

  if (mem_arg.is_reg_ref() && mem_arg.reg_num == 0) {
    w.put_u8(0x90 | (reg_arg.reg_num & 7));
  } else if (mem_arg.is_reg_ref() && reg_arg.reg_num == 0) {
    w.put_u8(0x90 | (mem_arg.reg_num & 7));
  } else {
    w.put_u8(0x86 | ((operand_size == 1) ? 0x00 : 0x01));
    this->encode_rm(w, mem_arg, reg_arg);
  }
}

void X86Emulator::Assembler::asm_dir_offsetof(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::BRANCH_TARGET});
  if (si.args[0].type == T::IMMEDIATE) {
    throw runtime_error(".offsetof requires a label name");
  }
  si.has_code_delta = true;
  uint32_t value = si.assembled_data.empty()
      ? 0xFFFFFFFF
      : this->stream.at(this->label_si_indexes.at(si.args[0].label_name)).offset;
  w.put_u32l(value);
}

void X86Emulator::Assembler::asm_dir_deltaof(StringWriter& w, StreamItem& si) const {
  si.check_arg_types({T::BRANCH_TARGET, T::BRANCH_TARGET});
  if ((si.args[0].type == T::IMMEDIATE) || (si.args[1].type == T::IMMEDIATE)) {
    throw runtime_error(".deltaof requires two label names");
  }
  si.has_code_delta = true;
  uint32_t value = 0xFFFFFFFF;
  if (!si.assembled_data.empty()) {
    value = this->stream.at(this->label_si_indexes.at(si.args[1].label_name)).offset -
        this->stream.at(this->label_si_indexes.at(si.args[0].label_name)).offset;
  }
  w.put_u32l(value);
}

const unordered_map<string, X86Emulator::Assembler::AssembleFunction> X86Emulator::Assembler::assemble_functions = {
    {"aaa", &X86Emulator::Assembler::asm_aaa_aas_aad_aam},
    {"aad", &X86Emulator::Assembler::asm_aaa_aas_aad_aam},
    {"aam", &X86Emulator::Assembler::asm_aaa_aas_aad_aam},
    {"aas", &X86Emulator::Assembler::asm_aaa_aas_aad_aam},
    {"add", &X86Emulator::Assembler::asm_add_or_adc_sbb_and_sub_xor_cmp},
    {"or", &X86Emulator::Assembler::asm_add_or_adc_sbb_and_sub_xor_cmp},
    {"adc", &X86Emulator::Assembler::asm_add_or_adc_sbb_and_sub_xor_cmp},
    {"sbb", &X86Emulator::Assembler::asm_add_or_adc_sbb_and_sub_xor_cmp},
    {"and", &X86Emulator::Assembler::asm_add_or_adc_sbb_and_sub_xor_cmp},
    {"sub", &X86Emulator::Assembler::asm_add_or_adc_sbb_and_sub_xor_cmp},
    {"xor", &X86Emulator::Assembler::asm_add_or_adc_sbb_and_sub_xor_cmp},
    {"cmp", &X86Emulator::Assembler::asm_add_or_adc_sbb_and_sub_xor_cmp},
    {"adx", &X86Emulator::Assembler::asm_amx_adx},
    {"amx", &X86Emulator::Assembler::asm_amx_adx},
    {"bsf", &X86Emulator::Assembler::asm_bsf_bsr},
    {"bsr", &X86Emulator::Assembler::asm_bsf_bsr},
    {"bswap", &X86Emulator::Assembler::asm_bswap},
    {"bt", &X86Emulator::Assembler::asm_bt_bts_btr_btc},
    {"bts", &X86Emulator::Assembler::asm_bt_bts_btr_btc},
    {"btr", &X86Emulator::Assembler::asm_bt_bts_btr_btc},
    {"btc", &X86Emulator::Assembler::asm_bt_bts_btr_btc},
    {"call", &X86Emulator::Assembler::asm_call_jmp},
    {"jmp", &X86Emulator::Assembler::asm_call_jmp},
    {"cbw", &X86Emulator::Assembler::asm_cbw_cwde},
    {"cwde", &X86Emulator::Assembler::asm_cbw_cwde},
    {"clc", &X86Emulator::Assembler::asm_clc},
    {"cld", &X86Emulator::Assembler::asm_cld},
    {"cli", &X86Emulator::Assembler::asm_cli},
    {"cmc", &X86Emulator::Assembler::asm_cmc},
    {"cmova", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovae", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovb", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovbe", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovc", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmove", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovg", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovge", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovl", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovle", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovna", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovnae", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovnb", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovnbe", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovnc", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovne", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovng", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovnge", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovnl", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovnle", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovno", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovnp", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovns", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovnz", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovo", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovp", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovpe", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovpo", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovs", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"cmovz", &X86Emulator::Assembler::asm_cmov_mnemonics},
    {"ins", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"outs", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"movs", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"cmps", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"stos", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"lods", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"scas", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"insb", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"outsb", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"movsb", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"cmpsb", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"stosb", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"lodsb", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"scasb", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"insw", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"outsw", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"movsw", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"cmpsw", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"stosw", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"lodsw", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"scasw", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"insd", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"outsd", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"movsd", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"cmpsd", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"stosd", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"lodsd", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"scasd", &X86Emulator::Assembler::asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics},
    {"cmpxchg", &X86Emulator::Assembler::asm_cmpxchg},
    {"cmpxchg8b", &X86Emulator::Assembler::asm_cmpxchg8b},
    {"cpuid", &X86Emulator::Assembler::asm_cpuid},
    {"crc32", &X86Emulator::Assembler::asm_crc32},
    {"cs", &X86Emulator::Assembler::asm_cs},
    {"cwd", &X86Emulator::Assembler::asm_cwd_cdq},
    {"cdq", &X86Emulator::Assembler::asm_cwd_cdq},
    {"daa", &X86Emulator::Assembler::asm_daa},
    {"das", &X86Emulator::Assembler::asm_das},
    {"inc", &X86Emulator::Assembler::asm_inc_dec},
    {"dec", &X86Emulator::Assembler::asm_inc_dec},
    {"div", &X86Emulator::Assembler::asm_div_idiv},
    {"idiv", &X86Emulator::Assembler::asm_div_idiv},
    {"ds", &X86Emulator::Assembler::asm_ds},
    {"enter", &X86Emulator::Assembler::asm_enter},
    {"es", &X86Emulator::Assembler::asm_es},
    {"fs", &X86Emulator::Assembler::asm_fs},
    {"gs", &X86Emulator::Assembler::asm_gs},
    {"hlt", &X86Emulator::Assembler::asm_hlt},
    {"imul", &X86Emulator::Assembler::asm_imul_mul},
    {"mul", &X86Emulator::Assembler::asm_imul_mul},
    {"in", &X86Emulator::Assembler::asm_in_out},
    {"out", &X86Emulator::Assembler::asm_in_out},
    {"int", &X86Emulator::Assembler::asm_int},
    {"iret", &X86Emulator::Assembler::asm_iret},
    {"ja", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jae", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jb", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jbe", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jc", &X86Emulator::Assembler::asm_j_mnemonics},
    {"je", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jg", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jge", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jl", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jle", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jna", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jnae", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jnb", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jnbe", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jnc", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jne", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jng", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jnge", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jnl", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jnle", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jno", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jnp", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jns", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jnz", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jo", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jp", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jpe", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jpo", &X86Emulator::Assembler::asm_j_mnemonics},
    {"js", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jz", &X86Emulator::Assembler::asm_j_mnemonics},
    {"jcxz", &X86Emulator::Assembler::asm_jcxz_jecxz_loop_mnemonics},
    {"jecxz", &X86Emulator::Assembler::asm_jcxz_jecxz_loop_mnemonics},
    {"loopz", &X86Emulator::Assembler::asm_jcxz_jecxz_loop_mnemonics},
    {"loope", &X86Emulator::Assembler::asm_jcxz_jecxz_loop_mnemonics},
    {"loopnz", &X86Emulator::Assembler::asm_jcxz_jecxz_loop_mnemonics},
    {"loopne", &X86Emulator::Assembler::asm_jcxz_jecxz_loop_mnemonics},
    {"loop", &X86Emulator::Assembler::asm_jcxz_jecxz_loop_mnemonics},
    {"lahf", &X86Emulator::Assembler::asm_lahf_sahf},
    {"sahf", &X86Emulator::Assembler::asm_lahf_sahf},
    {"lea", &X86Emulator::Assembler::asm_lea},
    {"leave", &X86Emulator::Assembler::asm_leave},
    {"lock", &X86Emulator::Assembler::asm_lock},
    {"mov", &X86Emulator::Assembler::asm_mov},
    {"movbe", &X86Emulator::Assembler::asm_movbe},
    {"movsx", &X86Emulator::Assembler::asm_movsx_movzx},
    {"movzx", &X86Emulator::Assembler::asm_movsx_movzx},
    {"neg", &X86Emulator::Assembler::asm_neg_not},
    {"not", &X86Emulator::Assembler::asm_neg_not},
    {"nop", &X86Emulator::Assembler::asm_nop},
    {"pop", &X86Emulator::Assembler::asm_pop_push},
    {"push", &X86Emulator::Assembler::asm_pop_push},
    {"popa", &X86Emulator::Assembler::asm_popa_popad},
    {"popad", &X86Emulator::Assembler::asm_popa_popad},
    {"popcnt", &X86Emulator::Assembler::asm_popcnt},
    {"popf", &X86Emulator::Assembler::asm_popf_popfd},
    {"popfd", &X86Emulator::Assembler::asm_popf_popfd},
    {"pusha", &X86Emulator::Assembler::asm_pusha_pushad},
    {"pushad", &X86Emulator::Assembler::asm_pusha_pushad},
    {"pushf", &X86Emulator::Assembler::asm_pushf_pushfd},
    {"pushfd", &X86Emulator::Assembler::asm_pushf_pushfd},
    {"rol", &X86Emulator::Assembler::asm_rol_ror_rcl_rcr_shl_sal_shr_sar},
    {"ror", &X86Emulator::Assembler::asm_rol_ror_rcl_rcr_shl_sal_shr_sar},
    {"rcl", &X86Emulator::Assembler::asm_rol_ror_rcl_rcr_shl_sal_shr_sar},
    {"rcr", &X86Emulator::Assembler::asm_rol_ror_rcl_rcr_shl_sal_shr_sar},
    {"shl", &X86Emulator::Assembler::asm_rol_ror_rcl_rcr_shl_sal_shr_sar},
    {"sal", &X86Emulator::Assembler::asm_rol_ror_rcl_rcr_shl_sal_shr_sar},
    {"shr", &X86Emulator::Assembler::asm_rol_ror_rcl_rcr_shl_sal_shr_sar},
    {"sar", &X86Emulator::Assembler::asm_rol_ror_rcl_rcr_shl_sal_shr_sar},
    {"rdtsc", &X86Emulator::Assembler::asm_rdtsc},
    {"repz", &X86Emulator::Assembler::asm_rep_mnemomics},
    {"repe", &X86Emulator::Assembler::asm_rep_mnemomics},
    {"repnz", &X86Emulator::Assembler::asm_rep_mnemomics},
    {"repne", &X86Emulator::Assembler::asm_rep_mnemomics},
    {"ret", &X86Emulator::Assembler::asm_ret},
    {"salc", &X86Emulator::Assembler::asm_salc_setalc},
    {"setalc", &X86Emulator::Assembler::asm_salc_setalc},
    {"setmova", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovae", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovb", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovbe", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovc", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmove", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovg", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovge", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovl", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovle", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovna", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovnae", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovnb", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovnbe", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovnc", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovne", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovng", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovnge", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovnl", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovnle", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovno", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovnp", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovns", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovnz", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovo", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovp", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovpe", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovpo", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovs", &X86Emulator::Assembler::asm_set_mnemonics},
    {"setmovz", &X86Emulator::Assembler::asm_set_mnemonics},
    {"shld", &X86Emulator::Assembler::asm_shld_shrd},
    {"shrd", &X86Emulator::Assembler::asm_shld_shrd},
    {"ss", &X86Emulator::Assembler::asm_ss},
    {"stc", &X86Emulator::Assembler::asm_stc},
    {"std", &X86Emulator::Assembler::asm_std},
    {"sti", &X86Emulator::Assembler::asm_sti},
    {"test", &X86Emulator::Assembler::asm_test},
    {"xadd", &X86Emulator::Assembler::asm_xadd},
    {"xchg", &X86Emulator::Assembler::asm_xchg},
    {".offsetof", &X86Emulator::Assembler::asm_dir_offsetof},
    {".deltaof", &X86Emulator::Assembler::asm_dir_deltaof},
};

X86Emulator::AssembleResult X86Emulator::assemble(const std::string& text,
    std::function<std::string(const std::string&)> get_include,
    uint32_t start_address) {
  Assembler a;
  a.start_address = start_address;
  return a.assemble(text, get_include);
}

X86Emulator::AssembleResult X86Emulator::assemble(const std::string& text,
    const std::vector<std::string>& include_dirs,
    uint32_t start_address) {
  if (include_dirs.empty()) {
    return X86Emulator::assemble(text, nullptr, start_address);

  } else {
    unordered_set<string> get_include_stack;
    function<string(const string&)> get_include = [&](const string& name) -> string {
      for (const auto& dir : include_dirs) {
        string filename = dir + "/" + name + ".inc.s";
        if (isfile(filename)) {
          if (!get_include_stack.emplace(name).second) {
            throw runtime_error("mutual recursion between includes: " + name);
          }
          const auto& ret = X86Emulator::assemble(load_file(filename), get_include, start_address).code;
          get_include_stack.erase(name);
          return ret;
        }
        filename = dir + "/" + name + ".inc.bin";
        if (isfile(filename)) {
          return load_file(filename);
        }
      }
      throw runtime_error("data not found for include: " + name);
    };
    return X86Emulator::assemble(text, get_include, start_address);
  }
}
