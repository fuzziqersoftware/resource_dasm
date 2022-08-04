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
    throw logic_error("invalid operand size in get_operand");
  }
}

static const char* const name_for_condition_code[0x10] = {
    "o", "no", "b", "ae", "e", "ne", "be", "a",
    "s", "ns", "pe", "po", "l", "ge", "le", "g"};



X86Registers::XMMReg::XMMReg() {
  this->u64[0] = 0;
  this->u64[1] = 0;
}

X86Registers::XMMReg::XMMReg(uint32_t v) {
  this->u32[0] = v;
  this->u32[1] = 0;
  this->u32[2] = 0;
  this->u32[3] = 0;
}

X86Registers::XMMReg::XMMReg(uint64_t v) {
  this->u64[0] = v;
  this->u64[1] = 0;
}

X86Registers::XMMReg& X86Registers::XMMReg::operator=(uint32_t v) {
  this->u32[0] = v;
  this->u32[1] = 0;
  this->u32[2] = 0;
  this->u32[3] = 0;
  return *this;
}

X86Registers::XMMReg& X86Registers::XMMReg::operator=(uint64_t v) {
  this->u64[0] = v;
  this->u64[1] = 0;
  return *this;
}

X86Registers::XMMReg::operator uint32_t() const {
  return this->u32[0];
}

X86Registers::XMMReg::operator uint64_t() const {
  return this->u64[0];
}



X86Registers::X86Registers() {
  for (size_t x = 0; x < 8; x++) {
    this->regs[x].u = 0;
  }
  for (size_t x = 0; x < 8; x++) {
    this->xmm[x].u64[0] = 0;
    this->xmm[x].u64[1] = 0;
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

uint8_t& X86Registers::reg_unreported8(uint8_t which) {
  if (which & ~7) {
    throw std::logic_error("invalid register index");
  }
  if (which & 4) {
    return this->regs[which & 3].u8.h;
  } else {
    return this->regs[which].u8.l;
  }
}

le_uint16_t& X86Registers::reg_unreported16(uint8_t which) {
  if (which & ~7) {
    throw std::logic_error("invalid register index");
  }
  return this->regs[which].u16;
}

le_uint32_t& X86Registers::reg_unreported32(uint8_t which) {
  if (which & ~7) {
    throw std::logic_error("invalid register index");
  }
  return this->regs[which].u;
}

const uint8_t& X86Registers::reg_unreported8(uint8_t which) const {
  return const_cast<X86Registers*>(this)->reg_unreported8(which);
}
const le_uint16_t& X86Registers::reg_unreported16(uint8_t which) const {
  return const_cast<X86Registers*>(this)->reg_unreported16(which);
}
const le_uint32_t& X86Registers::reg_unreported32(uint8_t which) const {
  return const_cast<X86Registers*>(this)->reg_unreported32(which);
}

le_uint32_t& X86Registers::xmm_unreported32(uint8_t which) {
  if (which & ~7) {
    throw std::logic_error("invalid register index");
  }
  return this->xmm[which].u32[0];
}

le_uint64_t& X86Registers::xmm_unreported64(uint8_t which) {
  if (which & ~7) {
    throw std::logic_error("invalid register index");
  }
  return this->xmm[which].u64[0];
}

X86Registers::XMMReg& X86Registers::xmm_unreported128(uint8_t which) {
  if (which & ~7) {
    throw std::logic_error("invalid register index");
  }
  return this->xmm[which];
}

const le_uint32_t& X86Registers::xmm_unreported32(uint8_t which) const {
  return const_cast<X86Registers*>(this)->xmm_unreported32(which);
}
const le_uint64_t& X86Registers::xmm_unreported64(uint8_t which) const {
  return const_cast<X86Registers*>(this)->xmm_unreported64(which);
}
const X86Registers::XMMReg& X86Registers::xmm_unreported128(uint8_t which) const {
  return const_cast<X86Registers*>(this)->xmm_unreported128(which);
}

uint32_t X86Registers::read_unreported(uint8_t which, uint8_t size) const {
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

X86Registers::XMMReg X86Registers::read_xmm_unreported(uint8_t which, uint8_t size) const {
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



bool X86Registers::read_flag(uint32_t mask) {
  this->mark_flags_read(mask);
  return this->eflags & mask;
}

void X86Registers::replace_flag(uint32_t mask, bool value) {
  this->mark_flags_written(mask);
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

void X86Registers::mark_flags_read(uint32_t mask) const {
  this->flags_read |= mask;
}

void X86Registers::mark_flags_written(uint32_t mask) const {
  this->flags_written |= mask;
}

static void mark_reg(std::array<uint32_t, 8>& regs, uint8_t which, uint8_t size) {
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

void X86Registers::mark_read(uint8_t which, uint8_t size) const {
  mark_reg(this->regs_read, which, size);
}

void X86Registers::mark_written(uint8_t which, uint8_t size) const {
  mark_reg(this->regs_written, which, size);
}

static void mark_xmm(std::array<X86Registers::XMMReg, 8>& regs, uint8_t which, uint8_t size) {
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

void X86Registers::mark_xmm_read(uint8_t which, uint8_t size) const {
  mark_xmm(this->xmm_regs_read, which, size);
}

void X86Registers::mark_xmm_written(uint8_t which, uint8_t size) const {
  mark_xmm(this->xmm_regs_written, which, size);
}

static bool is_reg_marked(const std::array<uint32_t, 8>& regs, uint8_t which, uint8_t size) {
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

bool X86Registers::was_read(uint8_t which, uint8_t size) const {
  return is_reg_marked(this->regs_read, which, size);
}

bool X86Registers::was_written(uint8_t which, uint8_t size) const {
  return is_reg_marked(this->regs_written, which, size);
}

static bool is_xmm_marked(const std::array<X86Registers::XMMReg, 8>& regs, uint8_t which, uint8_t size) {
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

bool X86Registers::xmm_was_read(uint8_t which, uint8_t size) const {
  return is_xmm_marked(this->xmm_regs_read, which, size);
}

bool X86Registers::xmm_was_written(uint8_t which, uint8_t size) const {
  return is_xmm_marked(this->xmm_regs_written, which, size);
}

uint32_t X86Registers::get_read_flags() const {
  return this->flags_read;
}

uint32_t X86Registers::get_written_flags() const {
  return this->flags_written;
}

void X86Registers::reset_access_flags() const {
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

bool X86Registers::check_condition(uint8_t cc) {
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

template <typename T, enable_if_t<is_unsigned<T>::value, bool>>
T X86Registers::set_flags_integer_add(T a, T b, uint32_t apply_mask) {
  T res = a + b;

  this->set_flags_integer_result(res, apply_mask);

  if (apply_mask & OF) {
    // OF should be set if the result overflows the destination location, as if
    // the operation was signed. Equivalently, OF should be set if a and b have
    // the same sign and the result has the opposite sign (that is, the signed
    // result has overflowed).
    this->replace_flag(OF,
        ((a & (1 << (bits_for_type<T> - 1))) == (b & (1 << (bits_for_type<T> - 1)))) &&
        ((a & (1 << (bits_for_type<T> - 1))) != (res & (1 << (bits_for_type<T> - 1)))));
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
T X86Registers::set_flags_integer_add_with_carry(T a, T b, uint32_t apply_mask) {
  // If CF is not set, this operation is the same as a normal add. The rest of
  // this function will assume CF was set.
  if (!this->read_flag(X86Registers::CF)) {
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
        ((a & (1 << (bits_for_type<T> - 1))) == (b & (1 << (bits_for_type<T> - 1)))) &&
        ((a & (1 << (bits_for_type<T> - 1))) != (res & (1 << (bits_for_type<T> - 1)))));
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
T X86Registers::set_flags_integer_subtract(T a, T b, uint32_t apply_mask) {
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
        ((a & (1 << (bits_for_type<T> - 1))) != (b & (1 << (bits_for_type<T> - 1)))) &&
        ((a & (1 << (bits_for_type<T> - 1))) != (res & (1 << (bits_for_type<T> - 1)))));
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
T X86Registers::set_flags_integer_subtract_with_borrow(T a, T b, uint32_t apply_mask) {
  // If CF is not set, this operation is the same as a normal subtract. The rest
  // of this function will assume CF was set.
  if (!this->read_flag(X86Registers::CF)) {
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
        ((a & (1 << (bits_for_type<T> - 1))) == (b & (1 << (bits_for_type<T> - 1)))) &&
        ((a & (1 << (bits_for_type<T> - 1))) != (res & (1 << (bits_for_type<T> - 1)))));
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
-EFLAGS-(--BITS--) <XMM> @ --EIP--- = CODE\n");
}

void X86Emulator::print_state(FILE* stream) {
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
  } catch (const out_of_range&) { }

  this->compute_execution_labels();

  DisassemblyState s = {
    StringReader(data),
    this->regs.eip,
    0,
    this->overrides,
    {},
    &this->execution_labels,
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

static const char* name_for_xmm_reg(uint8_t reg) {
  if (reg & ~7) {
    throw logic_error("invalid register index");
  }
  static const char* const reg_names[8] = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
  return reg_names[reg];
}

bool X86Emulator::DecodedRM::has_mem_ref() const {
  return (this->ea_index_scale != -1);
}

string X86Emulator::DecodedRM::ea_str(
    uint8_t operand_size,
    uint8_t flags,
    const std::multimap<uint32_t, std::string>* labels) const {
  if (this->ea_index_scale == -1) {
    if (this->ea_reg & ~7) {
      throw logic_error("DecodedRM has reg ref but invalid ea_reg");
    }
    return (flags & EA_XMM)
        ? name_for_xmm_reg(this->ea_reg)
        : name_for_reg(this->ea_reg, operand_size);

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
        tokens.emplace_back(string_printf("%08" PRIX32, this->ea_disp));
        std::vector<const char*> label_tokens;
        if (labels) {
          for (auto label_its = labels->equal_range(this->ea_disp);
               label_its.first != label_its.second;
               label_its.first++) {
            label_tokens.emplace_back(label_its.first->second.c_str());
          }
          if (!label_tokens.empty()) {
            tokens.emplace_back("/* " + join(label_tokens, ", ") + " */");
          }
        }
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
    } else if (operand_size == 64) {
      size_str = "qword";
    } else if (operand_size == 128) {
      size_str = "oword";
    } else {
      size_str = string_printf("(%02" PRIX8 ")", operand_size);
    }
    // TODO: We should include the override segment name here.
    return size_str + " [" + join(tokens, " ") + "]";
  }
}

string X86Emulator::DecodedRM::non_ea_str(uint8_t operand_size, uint8_t flags) const {
  return (flags & NON_EA_XMM)
      ? name_for_xmm_reg(this->non_ea_reg)
      : name_for_reg(this->non_ea_reg, operand_size);
}

string X86Emulator::DecodedRM::str(uint8_t operand_size, uint8_t flags,
    const std::multimap<uint32_t, std::string>* labels) const {
  return this->str(operand_size, operand_size, flags, labels);
}

string X86Emulator::DecodedRM::str(
    uint8_t ea_operand_size, uint8_t non_ea_operand_size, uint8_t flags,
    const std::multimap<uint32_t, std::string>* labels) const {
  string ea_str = this->ea_str(ea_operand_size, flags, labels);
  string non_ea_str = this->non_ea_str(non_ea_operand_size, flags);
  if (flags & EA_FIRST) {
    return ea_str + ", " + non_ea_str;
  } else {
    return non_ea_str + ", " + ea_str;
  }
}

uint32_t X86Emulator::resolve_mem_ea(const DecodedRM& rm, bool always_trace_sources) {
  if (rm.ea_index_scale < 0) {
    throw logic_error("this should be handled outside of resolve_mem_ea");
  }

  bool trace_reg_accesses = always_trace_sources || this->trace_data_source_addrs;

  uint32_t base_component = 0;
  uint32_t index_component = 0;
  uint32_t disp_component = rm.ea_disp;
  if (rm.ea_reg >= 0) {
    base_component = trace_reg_accesses
        ? this->regs.read32(rm.ea_reg)
        : this->regs.reg_unreported32(rm.ea_reg).load();
  }
  if (rm.ea_index_scale > 0) {
    index_component = rm.ea_index_scale * (trace_reg_accesses
        ? this->regs.read32(rm.ea_index_reg)
        : this->regs.reg_unreported32(rm.ea_index_reg).load());
  }
  return base_component + index_component + disp_component;
}



std::string X86Emulator::DataAccess::str() const {
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
    loc_str = string_printf("[%08" PRIX32 "]", this->addr);
  }

  string val_str;
  if (this->size == 8) {
    val_str = string_printf("%02" PRIX64, this->value_low & 0xFF);
  } else if (this->size == 16) {
    val_str = string_printf("%04" PRIX64, this->value_low & 0xFFFF);
  } else if (this->size == 32) {
    val_str = string_printf("%08" PRIX64, this->value_low & 0xFFFFFFFF);
  } else if (this->size == 64) {
    val_str = string_printf("%016" PRIX64, this->value_low);
  } else if (this->size == 128) {
    val_str = string_printf("%016" PRIX64 "%016" PRIX64, this->value_high, this->value_low);
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
  shared_ptr<DataAccess> acc(new DataAccess({
      this->instructions_executed, addr, size, is_write, is_reg, is_xmm_reg, value_low, value_high, {}}));
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
        } catch (const out_of_range&) { }
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

static const char* const integer_math_opcode_names[8] = {
    "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};

void X86Emulator::exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math(uint8_t opcode) {
  uint8_t what = (opcode >> 3) & 7;
  DecodedRM rm = this->fetch_and_decode_rm();
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->w_ea16(rm, this->exec_integer_math_logic<uint16_t>(
          what, this->r_ea16(rm), this->r_non_ea16(rm)));
    } else {
      this->w_ea32(rm, this->exec_integer_math_logic<uint32_t>(
          what, this->r_ea32(rm), this->r_non_ea32(rm)));
    }
  } else {
    this->w_ea8(rm, this->exec_integer_math_logic<uint8_t>(
        what, this->r_ea8(rm), this->r_non_ea8(rm)));
  }
}

string X86Emulator::dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math(DisassemblyState& s) {
  string opcode_name = extend(integer_math_opcode_names[(s.opcode >> 3) & 7], 10);
  DecodedRM rm = X86Emulator::fetch_and_decode_rm(s.r);
  return opcode_name + rm.str(s.standard_operand_size(), DecodedRM::EA_FIRST, s.labels);
}

void X86Emulator::exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math(uint8_t opcode) {
  uint8_t what = (opcode >> 3) & 7;
  DecodedRM rm = this->fetch_and_decode_rm();
  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->w_non_ea16(rm, this->exec_integer_math_logic<uint16_t>(
          what, this->r_non_ea16(rm), this->r_ea16(rm)));
    } else {
      this->w_non_ea32(rm, this->exec_integer_math_logic<uint32_t>(
          what, this->r_non_ea32(rm), this->r_ea32(rm)));
    }
  } else {
    this->w_non_ea8(rm, this->exec_integer_math_logic<uint8_t>(
        what, this->r_non_ea8(rm), this->r_ea8(rm)));
  }
}

string X86Emulator::dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math(DisassemblyState& s) {
  string opcode_name = extend(integer_math_opcode_names[(s.opcode >> 3) & 7], 10);
  DecodedRM rm = X86Emulator::fetch_and_decode_rm(s.r);
  return opcode_name + rm.str(s.standard_operand_size(), 0, s.labels);
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

void X86Emulator::exec_27_daa(uint8_t) {
  uint8_t orig_al = this->regs.r_al();
  bool orig_cf = this->regs.read_flag(X86Registers::CF);

  // Note: The x86 manual says CF is written during this phase as well, but it's
  // also written in both branches of the below section, so we skip the writes
  // here.
  if (this->regs.read_flag(X86Registers::AF) || ((orig_al & 0x0F) > 9)) {
    uint8_t new_al = this->regs.r_al() + 6;
    this->regs.w_al(new_al);
    this->regs.replace_flag(X86Registers::AF, 1);
  } else {
    this->regs.replace_flag(X86Registers::AF, 0);
  }

  if (orig_cf || (orig_al > 0x99)) {
    uint8_t new_al = this->regs.r_al() + 0x60;
    this->regs.w_al(new_al);
    this->regs.replace_flag(X86Registers::CF, 1);
  } else {
    this->regs.replace_flag(X86Registers::CF, 0);
  }
}

std::string X86Emulator::dasm_27_daa(DisassemblyState&) {
  return "daa";
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

void X86Emulator::exec_37_aaa(uint8_t) {
  if (this->regs.read_flag(X86Registers::AF) || ((this->regs.r_al() & 0x0F) > 9)) {
    this->regs.w_al(this->regs.r_al() + 0x06);
    this->regs.w_ah(this->regs.r_ah() + 0x01);
    this->regs.replace_flag(X86Registers::AF, true);
    this->regs.replace_flag(X86Registers::CF, true);
  } else {
    this->regs.replace_flag(X86Registers::AF, false);
    this->regs.replace_flag(X86Registers::CF, false);
  }
  this->regs.w_al(this->regs.r_al() & 0x0F);
}

std::string X86Emulator::dasm_37_aaa(DisassemblyState&) {
  return "aaa";
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
  uint8_t which = opcode & 7;
  if (this->overrides.operand_size) {
    this->regs.write16(which, this->regs.set_flags_integer_add<uint16_t>(
        this->regs.read16(which), 1, ~X86Registers::CF));
  } else {
    this->regs.write32(which, this->regs.set_flags_integer_add<uint32_t>(
        this->regs.read32(which), 1, ~X86Registers::CF));
  }
}

void X86Emulator::exec_48_to_4F_dec(uint8_t opcode) {
  uint8_t which = opcode & 7;
  if (this->overrides.operand_size) {
    this->regs.write16(which, this->regs.set_flags_integer_subtract<uint16_t>(
        this->regs.read16(which), 1, ~X86Registers::CF));
  } else {
    this->regs.write32(which, this->regs.set_flags_integer_subtract<uint32_t>(
        this->regs.read32(which), 1, ~X86Registers::CF));
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
  return s.overrides.operand_size ? "pusha" : "pushad";
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
  return s.overrides.operand_size ? "popa" : "popad";
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
  if (s.opcode & 2) {
    return string_printf("push      %02" PRIX8, s.r.get_u8());
  } else if (s.overrides.operand_size) {
    return string_printf("push      %04" PRIX16, s.r.get_u16l());
  } else {
    return string_printf("push      %08" PRIX32, s.r.get_u32l());
  }
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
  return opcode_name + string_printf("%08" PRIX32, dest);
}

void X86Emulator::exec_80_to_83_imm_math(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      uint16_t v = (opcode & 2)
          ? sign_extend<uint16_t, uint8_t>(this->fetch_instruction_byte())
          : this->fetch_instruction_word();
      this->w_ea16(rm, this->exec_integer_math_logic<uint16_t>(
          rm.non_ea_reg, this->r_ea16(rm), v));

    } else {
      uint32_t v = (opcode & 2)
          ? sign_extend<uint32_t, uint8_t>(this->fetch_instruction_byte())
          : this->fetch_instruction_dword();
      this->w_ea32(rm, this->exec_integer_math_logic<uint32_t>(
          rm.non_ea_reg, this->r_ea32(rm), v));
    }
  } else {
    // It looks like 82 is actually identical to 80. Is this true?
    uint8_t v = this->fetch_instruction_byte();
    this->w_ea8(rm, this->exec_integer_math_logic<uint8_t>(
        rm.non_ea_reg, this->r_ea8(rm), v));
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
      return opcode_name + rm.ea_str(16, 0, s.labels) + string_printf(", %" PRIX16, imm);

    } else {
      uint32_t imm = (s.opcode & 2)
          ? sign_extend<uint32_t, uint8_t>(s.r.get_u8())
          : s.r.get_u32l();
      return opcode_name + rm.ea_str(32, 0, s.labels) + string_printf(", %" PRIX32, imm);
    }
  } else {
    // It looks like 82 is actually identical to 80. Is this true?
    uint8_t imm = s.r.get_u8();
    return opcode_name + rm.ea_str(8, 0, s.labels) + string_printf(", %" PRIX8, imm);
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
  return "test      " + rm.str(s.standard_operand_size(), DecodedRM::EA_FIRST, s.labels);
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
  return "xchg      " + rm.str(s.standard_operand_size(), DecodedRM::EA_FIRST, s.labels);
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
  return "mov       " + rm.str(
      s.standard_operand_size(),
      (s.opcode & 2) ? 0 : DecodedRM::EA_FIRST,
      s.labels);
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
  return "lea       " + rm.str(32, 0, s.labels);
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
  return "pop       " + rm.ea_str(s.overrides.operand_size ? 16 : 32, 0, s.labels);
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
  return s.overrides.operand_size ? "pushf" : "pushfd";
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
  return s.overrides.operand_size ? "popf" : "popfd";
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
    mem_str = string_printf("%s:[%08" PRIX32 "]", seg_name, addr);
  } else {
    mem_str = string_printf("[%08" PRIX32 "]", addr);
  }

  string reg_str;
  if (!(s.opcode & 1)) {
    reg_str = "al";
  } else if (s.overrides.operand_size) {
    reg_str = "ax";
  } else {
    reg_str = "eax";
  }

  if (s.opcode & 2) {
    return "mov       " + mem_str + ", " + reg_str;
  } else {
    return "mov       " + reg_str + ", " + mem_str;
  }
}

template <typename T, typename LET>
void X86Emulator::exec_string_op_logic(uint8_t opcode) {
  // Note: We ignore the segment registers here. Technically we should be
  // reading from ds:esi (ds may be overridden by another prefix) and writing to
  // es:edi (es may NOT be overridden). But on modern OSes, these segment
  // registers point to the same location in protected mode, so we ignore them.

  // BYTES = OPCODE = [EDI] = [ESI] = NOTES
  // A4/A5 = movs   = write = read  = does essentially `mov es:[edi], ds:[esi]`
  // A6/A7 = cmps   = read  = read  = sets status flags as if `cmp ds:[esi], es:[edi]`
  // AA/AB = stos   = write =       = does essentially `mov es:[edi], al/ax/eax`
  // AC/AD = lods   =       = read  = does essentially `mov al/ax/eax, ds:[esi]`
  // AE/AF = scas   = read  =       = does essentially `cmp al/ax/eax, es:[edi]` (yes, edi)

  uint32_t edi_delta = this->regs.read_flag(X86Registers::DF) ? static_cast<uint32_t>(-sizeof(T)) : sizeof(T);
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
      uint64_t mask = (1ULL << bits_for_type<T>) - 1;
      uint64_t prev_eax = this->regs.r_eax();
      uint64_t value = this->r_mem<LET>(this->regs.r_esi());
      this->regs.w_eax((prev_eax & (~mask)) | (value & mask));
      edi_delta = 0;
      break;
    }
    case 0x0E: { // scas
      uint64_t mask = (1ULL << bits_for_type<T>) - 1;
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
         this->regs.r_ecx() && this->regs.read_flag(X86Registers::ZF) == expected_zf;
         this->regs.w_ecx(this->regs.r_ecx() - 1)) {
      this->exec_string_op_logic<T, LET>(opcode);
      // Note: We manually link accesses during this opcode's execution because
      // we could be copying a large amount of data, and it would be incorrect
      // to link each source byte to all destination bytes.
      this->link_current_accesses();
    }
  } else {
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

std::string X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops(DisassemblyState& s) {
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
  if (!(s.opcode & 1)) {
    prefix += "byte ";
    a_reg_name = "al";
  } else if (s.overrides.operand_size) {
    prefix += "word ";
    a_reg_name = "ax";
  } else {
    prefix += "dword ";
    a_reg_name = "eax";
  }

  switch ((s.opcode >> 1) & 7) {
    case 2: // movs
      return prefix + string_printf("es:[edi], %s:[esi]", src_segment_name);
    case 3: // cmps
      return prefix + string_printf("%s:[esi], es:[edi]", src_segment_name);
    case 5: // stos
      return prefix + string_printf("es:[edi], %s", a_reg_name);
    case 6: // lods
      return prefix + string_printf("%s, %s:[esi]", a_reg_name, src_segment_name);
    case 7: // scas
      return prefix + string_printf("%s, es:[edi]", a_reg_name);
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
      return string_printf("test      ax, %04" PRIX16, s.r.get_u16l());
    } else {
      return string_printf("test      eax, %08" PRIX32, s.r.get_u32l());
    }
  } else {
    return string_printf("test      al, %02" PRIX8, s.r.get_u8());
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
T X86Emulator::exec_bit_shifts_logic(
    uint8_t what, T value, uint8_t distance) {
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
      bool cf = this->regs.read_flag(X86Registers::CF);
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
      bool cf = this->regs.read_flag(X86Registers::CF);
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
      bool cf = this->regs.read_flag(X86Registers::CF);
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
  return value;
}

static const char* const bit_shift_opcode_names[8] = {
    "rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar"};

void X86Emulator::exec_C0_C1_bit_shifts(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  uint8_t distance = this->fetch_instruction_byte();

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->w_ea16(rm, this->exec_bit_shifts_logic<uint16_t>(
          rm.non_ea_reg, this->r_ea16(rm), distance));
    } else {
      this->w_ea32(rm, this->exec_bit_shifts_logic<uint32_t>(
          rm.non_ea_reg, this->r_ea32(rm), distance));
    }
  } else {
    this->w_ea8(rm, this->exec_bit_shifts_logic<uint8_t>(
        rm.non_ea_reg, this->r_ea8(rm), distance));
  }
}

string X86Emulator::dasm_C0_C1_bit_shifts(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  uint8_t distance = s.r.get_u8();
  return extend(bit_shift_opcode_names[rm.non_ea_reg], 10)
      + rm.ea_str(s.standard_operand_size(), 0, s.labels)
      + string_printf(", %02" PRIX8, distance);
}

void X86Emulator::exec_C2_C3_ret(uint8_t opcode) {
  uint32_t new_eip = this->pop<le_uint32_t>();
  if (!(opcode & 1)) {
    // TODO: Is this signed? It wouldn't make sense for it to be, but......
    this->regs.w_esp(this->regs.r_esp() + this->fetch_instruction_word());
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
  return "mov       " + rm.ea_str(operand_size, 0, s.labels)
      + string_printf(", %" PRIX32, get_operand(s.r, operand_size));
}

void X86Emulator::exec_C8_enter(uint8_t) {
  uint16_t size = this->fetch_instruction_word();
  uint8_t nest_level = this->fetch_instruction_byte();
  throw runtime_error(string_printf("unimplemented opcode: enter %04hX %02hhX", size, nest_level));
}

string X86Emulator::dasm_C8_enter(DisassemblyState& s) {
  uint16_t size = s.r.get_u16l();
  uint8_t nest_level = s.r.get_u8();
  return string_printf("enter     %04hX, %02hhX", size, nest_level);
}

void X86Emulator::exec_C9_leave(uint8_t) {
  this->regs.w_esp(this->regs.r_ebp());
  this->regs.w_ebp(this->overrides.operand_size
      ? this->pop<le_uint16_t>() : this->pop<le_uint32_t>());
}

string X86Emulator::dasm_C9_leave(DisassemblyState&) {
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
  uint8_t int_num = (s.opcode & 1) ? s.r.get_u8() : 3;
  return string_printf("int       %02hhX", int_num);
}

void X86Emulator::exec_D0_to_D3_bit_shifts(uint8_t opcode) {
  uint8_t distance = (opcode & 2) ? this->regs.r_cl() : 1;
  auto rm = this->fetch_and_decode_rm();

  if (opcode & 1) {
    if (this->overrides.operand_size) {
      this->w_ea16(rm, this->exec_bit_shifts_logic<uint16_t>(
          rm.non_ea_reg, this->r_ea16(rm), distance));
    } else {
      this->w_ea32(rm, this->exec_bit_shifts_logic<uint32_t>(
          rm.non_ea_reg, this->r_ea32(rm), distance));
    }
  } else {
    this->w_ea8(rm, this->exec_bit_shifts_logic<uint8_t>(
        rm.non_ea_reg, this->r_ea8(rm), distance));
  }
}

string X86Emulator::dasm_D0_to_D3_bit_shifts(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return extend(bit_shift_opcode_names[rm.non_ea_reg], 10)
      + rm.ea_str(s.standard_operand_size(), 0, s.labels)
      + ((s.opcode & 2) ? ", cl" : ", 1");
}

void X86Emulator::exec_D4_amx_aam(uint8_t) {
  uint8_t base = this->fetch_instruction_byte();
  this->regs.w_ah(this->regs.r_al() / base);
  this->regs.w_al(this->regs.r_al() % base);
  this->regs.set_flags_integer_result<uint8_t>(this->regs.r_al());
}

std::string X86Emulator::dasm_D4_amx_aam(DisassemblyState& s) {
  uint8_t base = s.r.get_u8();
  if (base == 10) {
    return "aam";
  } else {
    return string_printf("amx       %02hhX", base);
  }
}

void X86Emulator::exec_D5_adx_aad(uint8_t) {
  uint8_t base = this->fetch_instruction_byte();
  this->regs.w_al(this->regs.r_al() + (this->regs.r_ah() * base));
  this->regs.w_ah(0);
  this->regs.set_flags_integer_result<uint8_t>(this->regs.r_al());
}

std::string X86Emulator::dasm_D5_adx_aad(DisassemblyState& s) {
  uint8_t base = s.r.get_u8();
  if (base == 10) {
    return "aad";
  } else {
    return string_printf("adx       %02hhX", base);
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
  return string_printf("%s      %08" PRIX32, opcode_name, dest);
}

void X86Emulator::exec_EB_jmp(uint8_t) {
  this->regs.eip += sign_extend<uint32_t, uint8_t>(this->fetch_instruction_byte());
}

string X86Emulator::dasm_EB_jmp(DisassemblyState& s) {
  uint32_t offset = sign_extend<uint32_t, uint16_t>(s.r.get_u8());
  uint32_t dest = s.start_address + s.r.where() + offset;
  s.branch_target_addresses.emplace(dest, false);
  return string_printf("jmp       %08" PRIX32, dest);
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
  this->regs.replace_flag(X86Registers::CF, !this->regs.read_flag(X86Registers::CF));
}

string X86Emulator::dasm_F5_cmc(DisassemblyState&) {
  return "cmc";
}

template <typename T, typename LET>
T X86Emulator::exec_F6_F7_misc_math_logic(uint8_t what, T value) {
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
      this->regs.replace_flag(X86Registers::OF, of_cf);
      this->regs.replace_flag(X86Registers::CF, of_cf);
      break;
    }
    case 5: { // imul (to edx:eax)
      bool of_cf = false;
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
      this->regs.replace_flag(X86Registers::OF, of_cf);
      this->regs.replace_flag(X86Registers::CF, of_cf);
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
        if (quotient > 0xFFFFFFFF) {
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
        if (quotient < -0x80000000ll || quotient > 0x7FFFFFFFll) {
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
        this->w_ea16(rm, this->exec_F6_F7_misc_math_logic<uint16_t>(
            rm.non_ea_reg, this->r_ea16(rm)));
      } else {
        this->w_ea32(rm, this->exec_F6_F7_misc_math_logic<uint32_t>(
            rm.non_ea_reg, this->r_ea32(rm)));
      }
    } else {
      this->w_ea8(rm, this->exec_F6_F7_misc_math_logic<uint8_t>(
          rm.non_ea_reg, this->r_ea8(rm)));
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
    return "test      " + rm.ea_str(operand_size, 0, s.labels) + string_printf(", %02" PRIX32, get_operand(s.r, operand_size));
  } else {
    const char* const opcode_names[8] = {
        "test", "test", "not", "neg", "mul", "imul", "div", "idiv"};
    string name = extend(opcode_names[rm.non_ea_reg], 10);
    return name + rm.ea_str(operand_size, 0, s.labels);
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
          this->w_ea16(rm, this->regs.set_flags_integer_add<uint16_t>(
              this->r_ea16(rm), 1, ~X86Registers::CF));
        } else {
          this->w_ea32(rm, this->regs.set_flags_integer_add<uint32_t>(
              this->r_ea32(rm), 1, ~X86Registers::CF));
        }
        break;
      case 1: // dec
        if (this->overrides.operand_size) {
          this->w_ea16(rm, this->regs.set_flags_integer_subtract<uint16_t>(
              this->r_ea16(rm), 1, ~X86Registers::CF));
        } else {
          this->w_ea32(rm, this->regs.set_flags_integer_subtract<uint32_t>(
              this->r_ea32(rm), 1, ~X86Registers::CF));
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
      this->w_ea8(rm, this->regs.set_flags_integer_add<uint8_t>(
          this->r_ea8(rm), 1, ~X86Registers::CF));
    } else {
      this->w_ea8(rm, this->regs.set_flags_integer_subtract<uint8_t>(
          this->r_ea8(rm), 1, ~X86Registers::CF));
    }
  }
}

string X86Emulator::dasm_FE_FF_inc_dec_misc(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  uint8_t operand_size = s.standard_operand_size();
  if (rm.non_ea_reg < 2) {
    return (rm.non_ea_reg ? "dec       " : "inc       ") + rm.ea_str(operand_size, 0, s.labels);
  }

  if (!(s.opcode & 1)) {
    return ".invalid  <<inc/dec/misc>>";
  }

  switch (rm.non_ea_reg) {
    case 2: // call
    case 4: // jmp
      return ((rm.non_ea_reg == 2) ? "call      " : "jmp       ") + rm.ea_str(operand_size, 0, s.labels);
    case 3: // call (far)
    case 5: // jmp (far)
      return ".unknown  <<far call/jmp>> // unimplemented";
    case 6: // push
      return "push      " + rm.ea_str(operand_size, 0, s.labels);
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
      this->w_non_ea_xmm128(rm, X86Registers::XMMReg());
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

  return opcode_name + rm.str(
      operand_size,
      ((s.opcode & 1) ? DecodedRM::EA_FIRST : 0) | DecodedRM::EA_XMM | DecodedRM::NON_EA_XMM,
      s.labels);
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
  return opcode_name + rm.ea_str(8, 0, s.labels);
}

void X86Emulator::exec_0F_31_rdtsc(uint8_t) {
  this->regs.w_edx(this->instructions_executed >> 32);
  this->regs.w_eax(this->instructions_executed);
}

string X86Emulator::dasm_0F_31_rdtsc(DisassemblyState&) {
  return "rdtsc";
}

void X86Emulator::exec_0F_40_to_4F_cmov_rm(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

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
  return opcode_name + rm.str(s.overrides.operand_size ? 16 : 32, 0, s.labels);
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
  } else {// all xmm/mem <- xmm EXCEPT for movq, which is the opposite (why?!)
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

  return opcode_name + rm.str(
      operand_size,
      (((s.opcode & 1) || !s.overrides.repeat_z) ? DecodedRM::EA_FIRST : 0) | DecodedRM::EA_XMM | DecodedRM::NON_EA_XMM,
      s.labels);
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
  return opcode_name + rm.ea_str(8, 0, s.labels);
}

template <typename T>
T X86Emulator::exec_shld_shrd_logic(
    bool is_right_shift, T dest_value, T incoming_value, uint8_t distance) {
  if ((distance & 0x1F) == 0) {
    return dest_value;
  }

  T orig_sign = dest_value & msb_for_type<T>;
  bool cf = this->regs.read_flag(X86Registers::CF);
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
  return dest_value;
}

void X86Emulator::exec_0F_A4_A5_AC_AD_shld_shrd(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();
  uint8_t distance = (opcode & 1)
      ? this->regs.r_cl() : this->fetch_instruction_byte();

  if (this->overrides.operand_size) {
    this->w_ea16(rm, this->exec_shld_shrd_logic<uint16_t>(
        opcode & 8, this->r_ea16(rm), this->r_non_ea16(rm), distance));
  } else {
    this->w_ea32(rm, this->exec_shld_shrd_logic<uint32_t>(
        opcode & 8, this->r_ea32(rm), this->r_non_ea32(rm), distance));
  }
}

string X86Emulator::dasm_0F_A4_A5_AC_AD_shld_shrd(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  string opcode_name = extend((s.opcode & 8) ? "shrd" : "shld", 10);
  string distance_str = (s.opcode & 1) ? ", cl" : string_printf(", %02" PRIX8, s.r.get_u8());
  return opcode_name + rm.str(s.overrides.operand_size ? 16 : 32, DecodedRM::EA_FIRST, s.labels) + distance_str;
}

template <typename T>
T X86Emulator::exec_bit_test_ops_logic(uint8_t what, T v, uint8_t bit_number) {
  uint32_t mask = (1 << bit_number);
  this->regs.replace_flag(X86Registers::CF, v & mask);
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
      uint8_t bit_number = this->r_non_ea16(rm) & 0x0F;
      this->w_ea16(rm, this->exec_bit_test_ops_logic<le_uint16_t>(
          what, this->r_ea16(rm), bit_number));
    } else {
      uint8_t bit_number = this->r_non_ea16(rm) & 0x1F;
      this->w_ea32(rm, this->exec_bit_test_ops_logic<le_uint32_t>(
          what, this->r_ea32(rm), bit_number));
    }

  } else {
    uint32_t bit_number = this->overrides.operand_size
        ? this->r_non_ea16(rm) : this->r_non_ea32(rm);
    uint32_t addr = this->resolve_mem_ea(rm) + (bit_number >> 3);
    this->w_mem<uint8_t>(addr, this->exec_bit_test_ops_logic<uint8_t>(
        what, this->r_mem<uint8_t>(addr), (bit_number & 7)));
  }
}

string X86Emulator::dasm_0F_A3_AB_B3_BB_bit_tests(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  string opcode_name = extend(bit_test_opcode_names[(s.opcode >> 3) & 3], 10);
  return opcode_name + rm.str(s.overrides.operand_size ? 16 : 32, DecodedRM::EA_FIRST, s.labels);
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
    this->w_non_ea16(rm, v);
  } else {
    this->w_non_ea32(rm, v);
  }
}

string X86Emulator::dasm_0F_B6_B7_BE_BF_movzx_movsx(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  string opcode_name = (s.opcode & 8) ? "movsx     " : "movzx     ";
  return opcode_name + rm.str((s.opcode & 1) ? 16 : 8, s.overrides.operand_size ? 16 : 32, 0, s.labels);
}

void X86Emulator::exec_0F_BA_bit_tests(uint8_t) {
  DecodedRM rm = this->fetch_and_decode_rm();
  if (!(rm.non_ea_reg & 4)) {
    throw runtime_error("invalid opcode 0F BA");
  }
  uint8_t what = rm.non_ea_reg & 3;
  uint8_t bit_number = this->fetch_instruction_byte();

  if (rm.ea_index_scale < 0) { // Bit field is in register
    // TODO: Docs seem to say that the mask is 7 (not 0x0F) for a 16-bit
    // operand, but that seems... wrong. Verify the correct behavior.
    if (this->overrides.operand_size) {
      this->w_ea16(rm, this->exec_bit_test_ops_logic<uint16_t>(
          what, this->r_ea16(rm), bit_number & 0x0F));
    } else {
      this->w_ea32(rm, this->exec_bit_test_ops_logic<uint32_t>(
          what, this->r_ea32(rm), bit_number & 0x1F));
    }

  } else {
    // TODO: Should we AND bit_number with something here? What's the effective
    // operand size when accessing memory with these opcodes?
    uint32_t addr = this->resolve_mem_ea(rm) + (bit_number >> 3);
    this->w_mem<uint8_t>(addr, this->exec_bit_test_ops_logic<uint8_t>(
        what, this->r_mem<uint8_t>(addr), (bit_number & 7)));
  }
}

string X86Emulator::dasm_0F_BA_bit_tests(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  if (!(rm.non_ea_reg & 4)) {
    return ".invalid  <<bit test with subopcode 0-3>>";
  }
  uint8_t bit_number = s.r.get_u8();
  string opcode_name = extend(bit_test_opcode_names[rm.non_ea_reg & 3], 10);
  return opcode_name + rm.ea_str(s.overrides.operand_size ? 16 : 32, 0, s.labels) + string_printf(", %02" PRIX8, bit_number);
}

void X86Emulator::exec_0F_BC_BD_bsf_bsr(uint8_t opcode) {
  auto rm = this->fetch_and_decode_rm();

  uint32_t value = this->overrides.operand_size
      ? this->r_ea16(rm) : this->r_ea32(rm);

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
      this->w_non_ea16(rm, result);
    } else {
      this->w_non_ea32(rm, result);
    }
  }
}

string X86Emulator::dasm_0F_BC_BD_bsf_bsr(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);
  return ((s.opcode & 1) ? "bsr       " : "bsf       ") + rm.str(s.overrides.operand_size ? 16 : 32, 0, s.labels);
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
  return "xadd      " + rm.str(s.standard_operand_size(), DecodedRM::EA_FIRST, s.labels);
}

void X86Emulator::exec_0F_C8_to_CF_bswap(uint8_t opcode) {
  uint8_t which = opcode & 7;
  if (this->overrides.operand_size) {
    this->regs.write16(which, bswap16(this->regs.read16(which)));
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
    this->w_ea_xmm128(rm, X86Registers::XMMReg());
  }
  this->w_ea_xmm64(rm, this->r_non_ea_xmm64(rm));
}

string X86Emulator::dasm_0F_D6_movq_variants(DisassemblyState& s) {
  auto rm = X86Emulator::fetch_and_decode_rm(s.r);

  if (!s.overrides.operand_size || s.overrides.repeat_z || s.overrides.repeat_nz) {
    throw runtime_error("mm registers are not supported");
  }

  return "movq      " + rm.str(
      64,
      DecodedRM::EA_FIRST | DecodedRM::EA_XMM | DecodedRM::NON_EA_XMM,
      s.labels);
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
  : EmulatorBase(mem),
    audit(false),
    current_audit_result(nullptr),
    execution_labels_computed(false),
    trace_data_sources(false),
    trace_data_source_addrs(false) { }

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
  {&X86Emulator::exec_27_daa, &X86Emulator::dasm_27_daa},
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
  {&X86Emulator::exec_37_aaa, &X86Emulator::dasm_37_aaa},
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
  {&X86Emulator::exec_60_pusha, &X86Emulator::dasm_60_pusha},
  {&X86Emulator::exec_61_popa, &X86Emulator::dasm_61_popa},
  {},
  {},
  {&X86Emulator::exec_64_fs, &X86Emulator::dasm_64_fs},
  {&X86Emulator::exec_65_gs, &X86Emulator::dasm_65_gs},
  {&X86Emulator::exec_66_operand_size, &X86Emulator::dasm_66_operand_size},
  {},
  {&X86Emulator::exec_68_6A_push, &X86Emulator::dasm_68_6A_push},
  {},
  {&X86Emulator::exec_68_6A_push, &X86Emulator::dasm_68_6A_push},
  {},
  {},
  {},
  {},
  {},
  // 70
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
  {&X86Emulator::exec_70_to_7F_jcc, &X86Emulator::dasm_70_to_7F_jcc},
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
  {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
  {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
  {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
  {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
  {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
  {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
  {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
  {&X86Emulator::exec_90_to_97_xchg_eax, &X86Emulator::dasm_90_to_97_xchg_eax},
  {&X86Emulator::exec_98_cbw_cwde, &X86Emulator::dasm_98_cbw_cwde},
  {&X86Emulator::exec_99_cwd_cdq, &X86Emulator::dasm_99_cwd_cdq},
  {},
  {},
  {&X86Emulator::exec_9C_pushf_pushfd, &X86Emulator::dasm_9C_pushf_pushfd},
  {&X86Emulator::exec_9D_popf_popfd, &X86Emulator::dasm_9D_popf_popfd},
  {},
  {&X86Emulator::exec_9F_lahf, &X86Emulator::dasm_9F_lahf},
  // A0
  {&X86Emulator::exec_A0_A1_A2_A3_mov_eax_memabs, &X86Emulator::dasm_A0_A1_A2_A3_mov_eax_memabs},
  {&X86Emulator::exec_A0_A1_A2_A3_mov_eax_memabs, &X86Emulator::dasm_A0_A1_A2_A3_mov_eax_memabs},
  {&X86Emulator::exec_A0_A1_A2_A3_mov_eax_memabs, &X86Emulator::dasm_A0_A1_A2_A3_mov_eax_memabs},
  {&X86Emulator::exec_A0_A1_A2_A3_mov_eax_memabs, &X86Emulator::dasm_A0_A1_A2_A3_mov_eax_memabs},
  {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
  {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
  {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
  {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
  {&X86Emulator::exec_A8_A9_test_eax_imm, &X86Emulator::dasm_A8_A9_test_eax_imm},
  {&X86Emulator::exec_A8_A9_test_eax_imm, &X86Emulator::dasm_A8_A9_test_eax_imm},
  {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
  {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
  {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
  {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
  {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
  {&X86Emulator::exec_A4_to_A7_AA_to_AF_string_ops, &X86Emulator::dasm_A4_to_A7_AA_to_AF_string_ops},
  // B0
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  {&X86Emulator::exec_B0_to_BF_mov_imm, &X86Emulator::dasm_B0_to_BF_mov_imm},
  // C0
  {&X86Emulator::exec_C0_C1_bit_shifts, &X86Emulator::dasm_C0_C1_bit_shifts},
  {&X86Emulator::exec_C0_C1_bit_shifts, &X86Emulator::dasm_C0_C1_bit_shifts},
  {&X86Emulator::exec_C2_C3_ret, &X86Emulator::dasm_C2_C3_ret},
  {&X86Emulator::exec_C2_C3_ret, &X86Emulator::dasm_C2_C3_ret},
  {},
  {},
  {&X86Emulator::exec_C6_C7_mov_rm_imm, &X86Emulator::dasm_C6_C7_mov_rm_imm},
  {&X86Emulator::exec_C6_C7_mov_rm_imm, &X86Emulator::dasm_C6_C7_mov_rm_imm},
  {&X86Emulator::exec_C8_enter, &X86Emulator::dasm_C8_enter},
  {&X86Emulator::exec_C9_leave, &X86Emulator::dasm_C9_leave},
  {},
  {},
  {&X86Emulator::exec_CC_CD_int, &X86Emulator::dasm_CC_CD_int},
  {&X86Emulator::exec_CC_CD_int, &X86Emulator::dasm_CC_CD_int},
  {},
  {},
  // D0
  {&X86Emulator::exec_D0_to_D3_bit_shifts, &X86Emulator::dasm_D0_to_D3_bit_shifts},
  {&X86Emulator::exec_D0_to_D3_bit_shifts, &X86Emulator::dasm_D0_to_D3_bit_shifts},
  {&X86Emulator::exec_D0_to_D3_bit_shifts, &X86Emulator::dasm_D0_to_D3_bit_shifts},
  {&X86Emulator::exec_D0_to_D3_bit_shifts, &X86Emulator::dasm_D0_to_D3_bit_shifts},
  {&X86Emulator::exec_D4_amx_aam, &X86Emulator::dasm_D4_amx_aam},
  {&X86Emulator::exec_D5_adx_aad, &X86Emulator::dasm_D5_adx_aad},
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
  {&X86Emulator::exec_EB_jmp, &X86Emulator::dasm_EB_jmp},
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
  {&X86Emulator::exec_0F_10_11_mov_xmm, &X86Emulator::dasm_0F_10_11_mov_xmm},
  {&X86Emulator::exec_0F_10_11_mov_xmm, &X86Emulator::dasm_0F_10_11_mov_xmm},
  {},
  {},
  {},
  {},
  {},
  {},
  {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
  {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
  {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
  {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
  {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
  {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
  {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
  {&X86Emulator::exec_0F_18_to_1F_prefetch_or_nop, &X86Emulator::dasm_0F_18_to_1F_prefetch_or_nop},
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
  {&X86Emulator::exec_0F_7E_7F_mov_xmm, &X86Emulator::dasm_0F_7E_7F_mov_xmm},
  {&X86Emulator::exec_0F_7E_7F_mov_xmm, &X86Emulator::dasm_0F_7E_7F_mov_xmm},
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
  {&X86Emulator::exec_0F_D6_movq_variants, &X86Emulator::dasm_0F_D6_movq_variants},
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

    // Execute a cycle
    uint8_t opcode = this->fetch_instruction_byte();
    auto fn = this->fns[opcode].exec;
    if (this->trace_data_sources) {
      this->prev_regs = this->regs;
      this->prev_regs.reset_access_flags();
    }
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
    this->link_current_accesses();
    this->overrides.on_opcode_complete();

    if (this->current_audit_result) {
      this->current_audit_result->regs_after = this->regs;
      uint32_t addr = this->current_audit_result->regs_before.eip;
      try {
        while (this->current_audit_result->opcode.size() < 0x20) {
          this->current_audit_result->opcode += this->mem->read_s8(addr++);
        }
      } catch (const out_of_range&) { }

      this->compute_execution_labels();

      DisassemblyState s = {
        StringReader(this->current_audit_result->opcode),
        this->current_audit_result->regs_before.eip,
        0,
        this->current_audit_result->overrides,
        {},
        &this->execution_labels,
      };
      this->current_audit_result->disassembly = this->disassemble_one(s);
      this->current_audit_result = nullptr;
    }

    this->instructions_executed++;
  }
  this->execution_labels.clear();
}



void X86Emulator::compute_execution_labels() {
  if (!this->execution_labels_computed) {
    this->execution_labels.clear();
    for (const auto& symbol_it : this->mem->all_symbols()) {
      this->execution_labels.emplace(symbol_it.second, symbol_it.first);
    }
    this->execution_labels_computed = true;
  }
}



std::string X86Emulator::disassemble_one(DisassemblyState& s) {
  size_t start_offset = s.r.where();

  string dasm;
  try {
    s.opcode = s.r.get_u8();
    auto dasm_fn = X86Emulator::fns[s.opcode].dasm;
    dasm = dasm_fn ? dasm_fn(s) : X86Emulator::dasm_unimplemented(s);
  } catch (const out_of_range&) {
    dasm = ".incomplete";
  } catch (const exception& e) {
    dasm = string_printf(".failed   (%s)", e.what());
  }

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
    labels,
  };

  // Generate disassembly lines for each opcode
  map<uint32_t, pair<string, uint32_t>> lines; // {pc: (line, next_pc)}
  while (!s.r.eof()) {
    uint32_t pc = s.start_address + s.r.where();
    string line = string_printf("%08" PRIX32 " ", pc);
    line += X86Emulator::disassemble_one(s) + "\n";
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



void X86Emulator::print_source_trace(FILE* stream, const string& what, size_t max_depth) const {
  if (!this->trace_data_sources) {
    fprintf(stderr, "source tracing is disabled\n");
    return;
  }

  std::unordered_set<shared_ptr<DataAccess>> sources;
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

  std::function<void(shared_ptr<DataAccess>, size_t)> print_source = [&](shared_ptr<DataAccess> acc, size_t depth) {
    if (!acc.get()) {
      return;
    }

    for (size_t z = 0; z < depth; z++) {
      fputs("| ", stream);
    }
    fputs("+-", stream);
    if (max_depth && depth >= max_depth) {
      fprintf(stderr, "(maximum depth reached)\n");
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
  uint8_t version;
  freadx(stream, &version, sizeof(version));
  if (version != 0) {
    throw runtime_error("unknown format version");
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
  uint8_t version = 0;
  fwritex(stream, &version, sizeof(version));

  this->regs.export_state(stream);
  this->mem->export_state(stream);
}
