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

#include "../LowMemoryGlobals.hh"
#include "../TrapInfo.hh"
#include "M68KEmulator.hh"

using namespace std;

static const uint8_t SIZE_BYTE = 0;
static const uint8_t SIZE_WORD = 1;
static const uint8_t SIZE_LONG = 2;

static const string char_for_size = "bwl?";
static const string char_for_tsize = "wl";
static const string char_for_dsize = "?blw";

static const array<const char*, 7> name_for_value_type({
    "int32_t",
    "float",
    "extended",
    "packed_real",
    "int16_t",
    "double",
    "int8_t",
});

static const array<M68KEmulator::ValueType, 3> value_type_for_size({
    M68KEmulator::ValueType::BYTE,
    M68KEmulator::ValueType::WORD,
    M68KEmulator::ValueType::LONG,
});

static const array<uint8_t, 2> size_for_tsize({
    SIZE_WORD,
    SIZE_LONG,
});

static const array<M68KEmulator::ValueType, 2> value_type_for_tsize({
    M68KEmulator::ValueType::WORD,
    M68KEmulator::ValueType::LONG,
});

static const array<uint8_t, 4> size_for_dsize({
    0xFF, // 0 is not a valid dsize
    SIZE_BYTE,
    SIZE_LONG,
    SIZE_WORD,
});

static const array<M68KEmulator::ValueType, 4> value_type_for_dsize({
    M68KEmulator::ValueType::INVALID,
    M68KEmulator::ValueType::BYTE,
    M68KEmulator::ValueType::LONG,
    M68KEmulator::ValueType::WORD,
});

static const vector<uint8_t> bytes_for_size({
    1,
    2,
    4,
    0xFF,
});

static const vector<const char*> string_for_condition({"t ", "f ", "hi", "ls", "cc", "cs", "ne", "eq",
    "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"});

enum Condition {
  C = 0x01,
  V = 0x02,
  Z = 0x04,
  N = 0x08,
  X = 0x10,
};

// Opcode bit fields:
// 0000000000000000
// iiiiaaabbbcccddd
//        gww  vvvv   (w is called "size" everywhere in this file)
//          t
//     kkkkyyyyyyyy

static inline uint8_t op_get_i(uint16_t op) {
  return ((op >> 12) & 0x000F);
}

static inline uint8_t op_get_a(uint16_t op) {
  return ((op >> 9) & 0x0007);
}

static inline uint8_t op_get_b(uint16_t op) {
  return ((op >> 6) & 0x0007);
}

static inline uint8_t op_get_c(uint16_t op) {
  return ((op >> 3) & 0x0007);
}

static inline uint8_t op_get_d(uint16_t op) {
  return (op & 0x0007);
}

static inline bool op_get_g(uint16_t op) {
  return ((op >> 8) & 0x0001);
}

static inline uint8_t op_get_size(uint16_t op) {
  return ((op >> 6) & 0x0003);
}

static inline uint8_t op_get_v(uint16_t op) {
  return (op & 0x000F);
}

static inline bool op_get_t(uint16_t op) {
  return ((op >> 6) & 0x0001);
}

static inline uint8_t op_get_k(uint16_t op) {
  return ((op >> 8) & 0x000F);
}

static inline uint8_t op_get_y(uint16_t op) {
  return (op & 0x00FF);
}

static bool is_negative(uint32_t v, uint8_t size) {
  if (size == SIZE_BYTE) {
    return (v & 0x80);
  } else if (size == SIZE_WORD) {
    return (v & 0x8000);
  } else if (size == SIZE_LONG) {
    return (v & 0x80000000);
  }
  throw runtime_error("incorrect size in is_negative");
}

static int32_t sign_extend(uint32_t value, uint8_t size) {
  if (size == SIZE_BYTE) {
    return (value & 0x80) ? (value | 0xFFFFFF00) : (value & 0x000000FF);
  } else if (size == SIZE_WORD) {
    return (value & 0x8000) ? (value | 0xFFFF0000) : (value & 0x0000FFFF);
  } else if (size == SIZE_LONG) {
    return value;
  } else {
    throw runtime_error("incorrect size in sign_extend");
  }
}

static int64_t read_immediate_int(StringReader& r, uint8_t s) {
  switch (s) {
    case SIZE_BYTE:
      return r.get_u16b() & 0x00FF;
    case SIZE_WORD:
      return r.get_u16b();
    case SIZE_LONG:
      return r.get_u32b();
    default:
      return -1;
  }
}

static inline bool maybe_char(uint8_t ch) {
  return (ch == 0) || (ch == '\t') || (ch == '\r') || (ch == '\n') || ((ch >= 0x20) && (ch <= 0x7E));
}

static string format_immediate(int64_t value, bool include_comment_tokens = true) {
  string hex_repr = string_printf("0x%" PRIX64, value);

  string char_repr;
  for (ssize_t shift = 56; shift >= 0; shift -= 8) {
    uint8_t byte = (value >> shift) & 0xFF;
    if (!maybe_char(byte)) {
      return hex_repr;
    }
    if (char_repr.empty() && (byte == 0)) {
      continue; // Ignore leading \0 bytes
    }
    if (byte == 0) {
      char_repr += "\\0";
    } else if (byte == '\t') {
      char_repr += "\\t";
    } else if (byte == '\r') {
      char_repr += "\\r";
    } else if (byte == '\n') {
      char_repr += "\\n";
    } else if (byte == '\'') {
      char_repr += "\\\'";
    } else if (byte == '\"') {
      char_repr += "\\\"";
    } else if (byte == '\\') {
      char_repr += "\\\\";
    } else {
      char_repr += static_cast<char>(byte);
    }
  }
  if (char_repr.empty()) {
    return hex_repr; // value is zero
  }

  if (include_comment_tokens) {
    return string_printf("%s /* \'%s\' */", hex_repr.c_str(), char_repr.c_str());
  } else {
    return string_printf("%s \'%s\'", hex_repr.c_str(), char_repr.c_str());
  }
}

static string format_packed_decimal_real(uint32_t high, uint64_t low) {
  // Bits:
  // MGYY [EEEE]x4 [XXXX]x2 IIII [FFFF]x16
  // M = mantissa sign
  // G = exponent sign
  // Y = control bits for special values (Inf, NaN, etc.)
  // +/- Inf: M=SIGN G=1 Y=11 EEE=FFF I=? D=0000000000000000
  // +/- NaN: M=SIGN G=1 Y=11 EEE=FFF I=? D=anything nonzero
  // +/- zero: M=SIGN G=? Y=?? EEE=??? (but must be valid digits) I=0 D=0000000000000000
  if ((high & 0x7FFF0000) == 0x7FFF0000) {
    if (low == 0) {
      return (high & 0x80000000) ? "-Infinity" : "+Infinity";
    } else {
      return (high & 0x80000000) ? "-NaN" : "+NaN";
    }
  } else {
    return string_printf("%01" PRIX32 "%c%016" PRIX64 "e%c%04" PRIX32,
        high & 0x0000000F, (high & 0x80000000) ? '-' : '+', low,
        (high & 0x40000000) ? '-' : '+', (high >> 16) & 0x0FFF);
  }
}

M68KEmulator::Regs::Regs() {
  for (size_t x = 0; x < 8; x++) {
    this->a[x] = 0;
    this->d[x].u = 0;
  }
  this->pc = 0;
  this->sr = 0;
}

void M68KEmulator::Regs::import_state(FILE* stream) {
  uint8_t version = freadx<uint8_t>(stream);
  if (version > 1) {
    throw runtime_error("unknown format version");
  }

  for (size_t x = 0; x < 8; x++) {
    this->d[x].u = freadx<le_uint32_t>(stream);
  }
  for (size_t x = 0; x < 8; x++) {
    this->a[x] = freadx<le_uint32_t>(stream);
  }
  this->pc = freadx<le_uint32_t>(stream);
  this->sr = freadx<le_uint16_t>(stream);
  if (version == 0) {
    // Version 0 had two extra registers (debug read and write addresses). These
    // no longer exist, so skip them.
    fseek(stream, 8, SEEK_CUR);
  }
}

void M68KEmulator::Regs::export_state(FILE* stream) const {
  fwritex(stream, 1); // version

  for (size_t x = 0; x < 8; x++) {
    fwritex<le_uint32_t>(stream, this->d[x].u);
  }
  for (size_t x = 0; x < 8; x++) {
    fwritex<le_uint32_t>(stream, this->a[x]);
  }
  fwritex<le_uint32_t>(stream, this->pc);
  fwritex<le_uint16_t>(stream, this->sr);
}

void M68KEmulator::Regs::set_by_name(const string& reg_name, uint32_t value) {
  if (reg_name.size() < 2) {
    throw invalid_argument("invalid register name");
  }
  uint8_t reg_num = strtoul(reg_name.data() + 1, nullptr, 10);
  if (reg_name.at(0) == 'a' || reg_name.at(0) == 'A') {
    this->a[reg_num] = value;
  } else if (reg_name.at(0) == 'd' || reg_name.at(0) == 'D') {
    this->d[reg_num].u = value;
  } else {
    throw invalid_argument("invalid register name");
  }
}

uint32_t M68KEmulator::Regs::get_reg_value(bool is_a_reg, uint8_t reg_num) {
  if (is_a_reg) {
    return this->a[reg_num];
  } else {
    return this->d[reg_num].u;
  }
}

void M68KEmulator::Regs::set_ccr_flags(int64_t x, int64_t n, int64_t z, int64_t v,
    int64_t c) {
  uint16_t mask = 0xFFFF;
  uint16_t replace = 0x0000;

  int64_t values[5] = {c, v, z, n, x};
  for (size_t x = 0; x < 5; x++) {
    if (values[x] == 0) {
      mask &= ~(1 << x);
    } else if (values[x] > 0) {
      mask &= ~(1 << x);
      replace |= (1 << x);
    }
  }

  this->sr = (this->sr & mask) | replace;
}

void M68KEmulator::Regs::set_ccr_flags_integer_add(int32_t left_value,
    int32_t right_value, uint8_t size) {
  left_value = sign_extend(left_value, size);
  right_value = sign_extend(right_value, size);
  int32_t result = sign_extend(left_value + right_value, size);

  bool overflow = (((left_value > 0) && (right_value > 0) && (result < 0)) ||
      ((left_value < 0) && (right_value < 0) && (result > 0)));

  // This looks kind of dumb, but it's necessary to force the compiler not to
  // sign-extend the 32-bit ints when converting to 64-bit
  uint64_t left_value_c = static_cast<uint32_t>(left_value);
  uint64_t right_value_c = static_cast<uint32_t>(right_value);
  bool carry = (left_value_c + right_value_c) > 0xFFFFFFFF;

  this->set_ccr_flags(-1, (result < 0), (result == 0), overflow, carry);
}

void M68KEmulator::Regs::set_ccr_flags_integer_subtract(int32_t left_value,
    int32_t right_value, uint8_t size) {
  left_value = sign_extend(left_value, size);
  right_value = sign_extend(right_value, size);
  int32_t result = sign_extend(left_value - right_value, size);

  bool overflow = (((left_value > 0) && (right_value < 0) && (result < 0)) ||
      ((left_value < 0) && (right_value > 0) && (result > 0)));
  bool carry = (static_cast<uint32_t>(left_value) < static_cast<uint32_t>(right_value));
  this->set_ccr_flags(-1, (result < 0), (result == 0), overflow, carry);
}

uint32_t M68KEmulator::Regs::pop_u32(shared_ptr<const MemoryContext> mem) {
  uint32_t ret = mem->read_u32b(this->a[7]);
  this->a[7] += 4;
  return ret;
}

int32_t M68KEmulator::Regs::pop_s32(shared_ptr<const MemoryContext> mem) {
  int32_t ret = mem->read_s32b(this->a[7]);
  this->a[7] += 4;
  return ret;
}

uint16_t M68KEmulator::Regs::pop_u16(shared_ptr<const MemoryContext> mem) {
  uint16_t ret = mem->read_u16b(this->a[7]);
  this->a[7] += 2;
  return ret;
}

int16_t M68KEmulator::Regs::pop_s16(shared_ptr<const MemoryContext> mem) {
  int16_t ret = mem->read_s16b(this->a[7]);
  this->a[7] += 2;
  return ret;
}

uint8_t M68KEmulator::Regs::pop_u8(shared_ptr<const MemoryContext> mem) {
  int8_t ret = mem->read_u16b(this->a[7]);
  this->a[7] += 2;
  return ret;
}

int8_t M68KEmulator::Regs::pop_s8(shared_ptr<const MemoryContext> mem) {
  int8_t ret = mem->read_s16b(this->a[7]);
  this->a[7] += 2;
  return ret;
}

void M68KEmulator::Regs::push_u32(shared_ptr<MemoryContext> mem, uint32_t v) {
  this->a[7] -= 4;
  this->write_stack_u32(mem, v);
}

void M68KEmulator::Regs::push_s32(shared_ptr<MemoryContext> mem, int32_t v) {
  this->a[7] -= 4;
  this->write_stack_s32(mem, v);
}

void M68KEmulator::Regs::push_u16(shared_ptr<MemoryContext> mem, uint16_t v) {
  this->a[7] -= 2;
  this->write_stack_u16(mem, v);
}

void M68KEmulator::Regs::push_s16(shared_ptr<MemoryContext> mem, int16_t v) {
  this->a[7] -= 2;
  this->write_stack_s16(mem, v);
}

void M68KEmulator::Regs::push_u8(shared_ptr<MemoryContext> mem, uint8_t v) {
  // Note: A7 must always be word-aligned, so `move.b -[A7], x` decrements by 2
  this->a[7] -= 2;
  this->write_stack_u16(mem, v);
}

void M68KEmulator::Regs::push_s8(shared_ptr<MemoryContext> mem, int8_t v) {
  // Note: A7 must always be word-aligned, so `move.b -[A7], x` decrements by 2
  this->a[7] -= 2;
  this->write_stack_s16(mem, v);
}

void M68KEmulator::Regs::write_stack_u32(shared_ptr<MemoryContext> mem, uint32_t v) {
  mem->write_u32b(this->a[7], v);
}

void M68KEmulator::Regs::write_stack_s32(shared_ptr<MemoryContext> mem, int32_t v) {
  mem->write_s32b(this->a[7], v);
}

void M68KEmulator::Regs::write_stack_u16(shared_ptr<MemoryContext> mem, uint16_t v) {
  mem->write_u16b(this->a[7], v);
}

void M68KEmulator::Regs::write_stack_s16(shared_ptr<MemoryContext> mem, int16_t v) {
  mem->write_s16b(this->a[7], v);
}

void M68KEmulator::Regs::write_stack_u8(shared_ptr<MemoryContext> mem, uint8_t v) {
  mem->write_u8(this->a[7], v);
}

void M68KEmulator::Regs::write_stack_s8(shared_ptr<MemoryContext> mem, int8_t v) {
  mem->write_s8(this->a[7], v);
}

M68KEmulator::M68KEmulator(shared_ptr<MemoryContext> mem) : EmulatorBase(mem) {}

M68KEmulator::Regs& M68KEmulator::registers() {
  return this->regs;
}

void M68KEmulator::print_state_header(FILE* stream) const {
  fprintf(stream, "\
---D0--- ---D1--- ---D2--- ---D3--- ---D4--- ---D5--- ---D6--- ---D7---  \
---A0--- ---A1--- ---A2--- ---A3--- ---A4--- ---A5--- ---A6--- -A7--SP- \
CBITS ---PC--- = INSTRUCTION\n");
}

void M68KEmulator::print_state(FILE* stream) const {
  uint8_t pc_data[16];
  size_t pc_data_available = 0;
  for (; pc_data_available < 16; pc_data_available++) {
    try {
      pc_data[pc_data_available] = this->read(this->regs.pc + pc_data_available, SIZE_BYTE);
    } catch (const exception&) {
      break;
    }
  }

  string disassembly;
  try {
    disassembly = this->disassemble_one(pc_data, pc_data_available, this->regs.pc);
  } catch (const exception& e) {
    disassembly = string_printf(" (failed: %s)", e.what());
  }

  fprintf(stream, "\
%08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 "  \
%08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " \
%c%c%c%c%c %08" PRIX32 " =%s\n",
      this->regs.d[0].u, this->regs.d[1].u, this->regs.d[2].u, this->regs.d[3].u,
      this->regs.d[4].u, this->regs.d[5].u, this->regs.d[6].u, this->regs.d[7].u,
      this->regs.a[0], this->regs.a[1], this->regs.a[2], this->regs.a[3],
      this->regs.a[4], this->regs.a[5], this->regs.a[6], this->regs.a[7],
      ((this->regs.sr & 0x10) ? 'x' : '-'), ((this->regs.sr & 0x08) ? 'n' : '-'),
      ((this->regs.sr & 0x04) ? 'z' : '-'), ((this->regs.sr & 0x02) ? 'v' : '-'),
      ((this->regs.sr & 0x01) ? 'c' : '-'), this->regs.pc, disassembly.c_str());
}

bool M68KEmulator::ResolvedAddress::is_register() const {
  return this->location != Location::MEMORY;
}

uint32_t M68KEmulator::read(const ResolvedAddress& addr, uint8_t size) const {
  if (addr.location == ResolvedAddress::Location::D_REGISTER) {
    if (size == SIZE_BYTE) {
      return *reinterpret_cast<const uint8_t*>(&this->regs.d[addr.addr].u);
    } else if (size == SIZE_WORD) {
      return *reinterpret_cast<const uint16_t*>(&this->regs.d[addr.addr].u);
    } else if (size == SIZE_LONG) {
      return this->regs.d[addr.addr].u;
    } else {
      throw runtime_error("incorrect size on d-register read");
    }
  } else if (addr.location == ResolvedAddress::Location::A_REGISTER) {
    if (size == SIZE_BYTE) {
      return *reinterpret_cast<const uint8_t*>(&this->regs.a[addr.addr]);
    } else if (size == SIZE_WORD) {
      return *reinterpret_cast<const uint16_t*>(&this->regs.a[addr.addr]);
    } else if (size == SIZE_LONG) {
      return this->regs.a[addr.addr];
    } else {
      throw runtime_error("incorrect size on a-register read");
    }
    return this->regs.a[addr.addr];
  } else if (addr.location == ResolvedAddress::Location::MEMORY) {
    return this->read(addr.addr, size);
  } else { // Location::SR
    return this->regs.sr;
  }
}

uint32_t M68KEmulator::read(uint32_t addr, uint8_t size) const {
  if (size == SIZE_BYTE) {
    return this->mem->read_u8(addr);
  } else if (size == SIZE_WORD) {
    return this->mem->read_u16b(addr);
  } else if (size == SIZE_LONG) {
    return this->mem->read_u32b(addr);
  } else {
    throw runtime_error("incorrect size on read");
  }
}

void M68KEmulator::write(const ResolvedAddress& addr, uint32_t value, uint8_t size) {
  if (addr.location == ResolvedAddress::Location::D_REGISTER) {
    if (size == SIZE_BYTE) {
      *reinterpret_cast<uint8_t*>(&this->regs.d[addr.addr].u) = value;
    } else if (size == SIZE_WORD) {
      *reinterpret_cast<uint16_t*>(&this->regs.d[addr.addr].u) = value;
    } else if (size == SIZE_LONG) {
      this->regs.d[addr.addr].u = value;
    } else {
      throw runtime_error("incorrect size on d-register write");
    }
  } else if (addr.location == ResolvedAddress::Location::A_REGISTER) {
    if (size == SIZE_BYTE) {
      *reinterpret_cast<uint8_t*>(&this->regs.a[addr.addr]) = value;
    } else if (size == SIZE_WORD) {
      *reinterpret_cast<uint16_t*>(&this->regs.a[addr.addr]) = value;
    } else if (size == SIZE_LONG) {
      this->regs.a[addr.addr] = value;
    } else {
      throw runtime_error("incorrect size on a-register write");
    }
  } else if (addr.location == ResolvedAddress::Location::MEMORY) {
    this->write(addr.addr, value, size);
  } else { // Location::SR
    this->regs.sr = value;
  }
}

void M68KEmulator::write(uint32_t addr, uint32_t value, uint8_t size) {
  if (size == SIZE_BYTE) {
    this->mem->write_u8(addr, value);
  } else if (size == SIZE_WORD) {
    this->mem->write_u16b(addr, value);
  } else if (size == SIZE_LONG) {
    this->mem->write_u32b(addr, value);
  } else {
    throw runtime_error("incorrect size on write");
  }
}

uint16_t M68KEmulator::fetch_instruction_word(bool advance) {
  return this->fetch_instruction_data(SIZE_WORD, advance);
}

int16_t M68KEmulator::fetch_instruction_word_signed(bool advance) {
  return static_cast<int16_t>(this->fetch_instruction_data(SIZE_WORD, advance));
}

uint32_t M68KEmulator::fetch_instruction_data(uint8_t size, bool advance) {
  if (size == SIZE_BYTE) {
    uint32_t ret = this->mem->read<uint8_t>(this->regs.pc);
    this->regs.pc += (1 * advance);
    return ret;

  } else if (size == SIZE_WORD) {
    uint32_t ret = this->mem->read<be_uint16_t>(this->regs.pc);
    this->regs.pc += (2 * advance);
    return ret;

  } else if (size == SIZE_LONG) {
    uint32_t ret = this->mem->read<be_uint32_t>(this->regs.pc);
    this->regs.pc += (4 * advance);
    return ret;
  }

  throw runtime_error("incorrect size in instruction fetch");
}

int32_t M68KEmulator::fetch_instruction_data_signed(uint8_t size, bool advance) {
  int32_t data = this->fetch_instruction_data(size, advance);
  if ((size == SIZE_BYTE) && (data & 0x00000080)) {
    data |= 0xFFFFFF00;
  } else if ((size == SIZE_WORD) && (data & 0x00008000)) {
    data |= 0xFFFF0000;
  }
  return data;
}

uint32_t M68KEmulator::resolve_address_extension(uint16_t ext) {
  bool is_a_reg = ext & 0x8000;
  uint8_t reg_num = static_cast<uint8_t>((ext >> 12) & 7);
  bool index_is_word = !(ext & 0x0800);
  uint8_t scale = 1 << ((ext >> 9) & 3);

  int32_t disp_reg_value = this->regs.get_reg_value(is_a_reg, reg_num);
  if (index_is_word && (disp_reg_value & 0x8000)) {
    disp_reg_value |= 0xFFFF0000;
  }
  uint32_t ret = disp_reg_value * scale;
  if (!(ext & 0x0100)) {
    // Brief extension word
    // TODO: is this signed? here we're assuming it is
    int8_t offset = static_cast<int8_t>(ext & 0xFF);
    ret += offset;
    return ret;
  }

  // Full extension word
  // TODO: implement this. See page 43 in the programmers' manual
  throw runtime_error("unimplemented: full extension word");
}

uint32_t M68KEmulator::resolve_address_control(uint8_t M, uint8_t Xn) {
  switch (M) {
    case 2:
      return this->regs.a[Xn];
    case 5:
      return this->regs.a[Xn] + this->fetch_instruction_word_signed();
    case 6:
      return this->regs.a[Xn] + this->resolve_address_extension(this->fetch_instruction_word());
    case 7: {
      switch (Xn) {
        case 0:
          return this->fetch_instruction_data_signed(SIZE_WORD);
        case 1:
          return this->fetch_instruction_data(SIZE_LONG);
        case 2: {
          uint32_t orig_pc = this->regs.pc;
          return orig_pc + this->fetch_instruction_word_signed();
        }
        case 3: {
          uint32_t orig_pc = this->regs.pc;
          return orig_pc + this->resolve_address_extension(this->fetch_instruction_word());
        }
        default:
          throw runtime_error("incorrect address mode in control reference");
      }
    }
    default:
      throw runtime_error("incorrect address mode in control reference");
  }
}

M68KEmulator::ResolvedAddress M68KEmulator::resolve_address(uint8_t M, uint8_t Xn, uint8_t size) {
  switch (M) {
    case 0:
      return {Xn, ResolvedAddress::Location::D_REGISTER};
    case 1:
      return {Xn, ResolvedAddress::Location::A_REGISTER};
    case 2:
      return {this->regs.a[Xn], ResolvedAddress::Location::MEMORY};
    case 3: {
      ResolvedAddress ret = {this->regs.a[Xn], ResolvedAddress::Location::MEMORY};
      if (size == SIZE_BYTE && Xn == 7) {
        this->regs.a[Xn] += 2; // A7 should always be word-aligned
      } else {
        this->regs.a[Xn] += bytes_for_size[size];
      }
      return ret;
    }
    case 4:
      if (size == SIZE_BYTE && Xn == 7) {
        this->regs.a[Xn] -= 2; // A7 should always be word-aligned
      } else {
        this->regs.a[Xn] -= bytes_for_size[size];
      }
      return {this->regs.a[Xn], ResolvedAddress::Location::MEMORY};
    case 5:
      return {this->regs.a[Xn] + this->fetch_instruction_word_signed(),
          ResolvedAddress::Location::MEMORY};
    case 6:
      return {this->regs.a[Xn] + this->resolve_address_extension(this->fetch_instruction_word()),
          ResolvedAddress::Location::MEMORY};
    case 7: {
      switch (Xn) {
        case 0:
          return {static_cast<uint32_t>(this->fetch_instruction_word_signed()), ResolvedAddress::Location::MEMORY};
        case 1:
          return {this->fetch_instruction_data(SIZE_LONG), ResolvedAddress::Location::MEMORY};
        case 2:
          return {this->regs.pc + this->fetch_instruction_word_signed(), ResolvedAddress::Location::MEMORY};
        case 3:
          return {this->regs.pc + this->resolve_address_extension(this->fetch_instruction_word()),
              ResolvedAddress::Location::MEMORY};
        case 4:
          if (size == SIZE_LONG) {
            this->regs.pc += 4;
            return {this->regs.pc - 4, ResolvedAddress::Location::MEMORY};
          } else if (size == SIZE_WORD) {
            this->regs.pc += 2;
            return {this->regs.pc - 2, ResolvedAddress::Location::MEMORY};
          } else {
            // For byte-sized immediate values, read a word and take the low 8
            // bits.
            this->regs.pc += 2;
            return {this->regs.pc - 1, ResolvedAddress::Location::MEMORY};
          }
        default:
          throw runtime_error("invalid special address");
      }
    }
    default:
      throw runtime_error("invalid address");
  }
}

string M68KEmulator::dasm_reg_mask(uint16_t mask, bool reverse) {
  if (mask == 0) {
    return "<none>";
  }

  string ret;
  if (reverse) {
    for (ssize_t x = 15; x >= 8; x--) {
      if (mask & (1 << x)) {
        ret += string_printf("D%zd,", 15 - x);
      }
    }
    for (ssize_t x = 7; x >= 0; x--) {
      if (mask & (1 << x)) {
        ret += string_printf("A%zd,", 7 - x);
      }
    }

  } else {
    for (ssize_t x = 0; x < 8; x++) {
      if (mask & (1 << x)) {
        ret += string_printf("D%zd,", x);
      }
    }
    for (ssize_t x = 8; x < 16; x++) {
      if (mask & (1 << x)) {
        ret += string_printf("A%zd,", x - 8);
      }
    }
  }

  ret.resize(ret.size() - 1); // Remove the last ','
  return ret;
}

string M68KEmulator::dasm_address_extension(StringReader& r, uint16_t ext, int8_t An) {
  bool index_is_a_reg = ext & 0x8000;
  uint8_t index_reg_num = static_cast<uint8_t>((ext >> 12) & 7);
  bool index_is_word = !(ext & 0x0800); // true = signed word, false = long
  uint8_t scale = 1 << ((ext >> 9) & 3);

  string ret;

  if (!(ext & 0x0100)) {
    // Brief extension word
    ret += (An == -1) ? "[PC" : string_printf("[A%hhd", An);

    if (scale != 1) {
      ret += string_printf(" + %c%hhu%s * %hhu", index_is_a_reg ? 'A' : 'D',
          index_reg_num, index_is_word ? ".w" : "", scale);
    } else {
      ret += string_printf(" + %c%hhu%s", index_is_a_reg ? 'A' : 'D',
          index_reg_num, index_is_word ? ".w" : "");
    }

    // TODO: is this signed? here we're assuming it is
    int8_t offset = static_cast<int8_t>(ext & 0xFF);
    if (offset > 0) {
      return ret + string_printf(" + 0x%hhX]", offset);
    } else if (offset < 0) {
      return ret + string_printf(" - 0x%hhX]", static_cast<uint8_t>(-offset));
    }
    return ret + ']';
  }

  // Full extension word; see page 43 in the programmers' manual
  bool include_base_register = !(ext & 0x0080);
  bool include_index_register = !(ext & 0x0040);
  // 1 = null displacement, 2 = word displacement, 3 = long displacement
  uint8_t base_displacement_size = (ext & 0x0030) >> 4;
  uint8_t index_indirect_select = ext & 7;

  // The access type depends on the above variables like this:
  // include_index_register, index_indirect_select => result
  //   true,  0 => No Memory Indirect Action
  //   true,  1 => Indirect Preindexed with Null Outer Displacement
  //   true,  2 => Indirect Preindexed with Word Outer Displacement
  //   true,  3 => Indirect Preindexed with Long Outer Displacement
  //   true,  4 => Reserved
  //   true,  5 => Indirect Postindexed with Null Outer Displacement
  //   true,  6 => Indirect Postindexed with Word Outer Displacement
  //   true,  7 => Indirect Postindexed with Long Outer Displacement
  //   false, 0 => No Memory Indirect Action
  //   false, 1 => Memory Indirect with Null Outer Displacement
  //   false, 2 => Memory Indirect with Word Outer Displacement
  //   false, 3 => Memory Indirect with Long Outer Displacement
  //   false, 4 => Reserved
  //   false, 5 => Reserved
  //   false, 6 => Reserved
  //   false, 7 => Reserved

  // The various actions are like this:
  //   No memory indirect action (I'm guessing here; the manual is confusing):
  //     [base_disp + index_reg.SIZE * SCALE]
  //   Indirect preindexed:
  //     [[An + base_disp + index_reg.SIZE * SCALE] + outer_disp]
  //   Indirect postindexed:
  //     [[An + base_disp] + index_reg.SIZE * SCALE + outer_disp]
  //   Memory indirect (I'm guessing; this isn't in the manual):
  //     [[An + base_disp] + outer_disp]
  // Note that An is determined by the caller (it's not part of the extension).
  // An can also be -1, which means to use PC.

  if (index_indirect_select == 4) {
    return "<<invalid full ext with I/IS == 4>>";
  }

  ret = "[";
  if (index_indirect_select == 0) {
    if (include_base_register) {
      ret += (An == -1) ? "PC" : string_printf("A%hhd", An);
    }

    int32_t base_displacement = 0;
    if (base_displacement_size == 0) {
      ret += " + <<invalid base displacement size>>";
    } else if (base_displacement_size == 2) {
      base_displacement = r.get_s16b();
    } else if (base_displacement_size == 3) {
      base_displacement = r.get_s32b();
    }
    if (base_displacement > 0) {
      ret += string_printf("%s0x%" PRIX32, include_base_register ? " + " : "",
          base_displacement);
    } else if (base_displacement < 0) {
      ret += string_printf("%s0x%" PRIX32, include_base_register ? " - " : "-",
          -base_displacement);
    }

    if (include_index_register) {
      string scale_str = (scale != 1) ? string_printf(" * %hhu", scale) : "";
      ret += string_printf(" + %c%hhu%s", index_is_a_reg ? 'A' : 'D',
          index_reg_num, scale_str.c_str());
    }
    ret += ']';

  } else {
    if (!include_index_register && (index_indirect_select > 4)) {
      return string_printf("<<invalid full ext with IS == 1 and I/IS == %hhu>>",
          index_indirect_select);
    }

    ret += '[';
    if (include_base_register) {
      ret += (An == -1) ? "PC" : string_printf("A%hhd", An);
    }

    int32_t base_displacement = 0;
    if (base_displacement_size == 0) {
      ret += " + <<invalid base displacement size>>";
    } else if (base_displacement_size == 2) {
      base_displacement = r.get_s16b();
    } else if (base_displacement_size == 3) {
      base_displacement = r.get_s32b();
    }
    if (base_displacement > 0) {
      ret += string_printf("%s0x%" PRIX32, include_base_register ? " + " : "",
          base_displacement);
    } else if (base_displacement < 0) {
      ret += string_printf("%s0x%" PRIX32, include_base_register ? " - " : "-",
          -base_displacement);
    }

    if (include_index_register) {
      bool index_before_indirection = (index_indirect_select < 4);
      string scale_str = (scale != 1) ? string_printf(" * %hhu", scale) : "";
      ret += string_printf("%s + %c%hhu%s%s",
          index_before_indirection ? "" : "]", index_is_a_reg ? 'A' : 'D',
          index_reg_num, scale_str.c_str(),
          index_before_indirection ? "]" : "");
    } else {
      ret += ']';
    }

    uint8_t outer_displacement_mode = index_indirect_select & 3;
    int32_t outer_displacement = 0;
    if (outer_displacement_mode == 0) {
      ret += " + <<invalid outer displacement mode>>";
    } else if (outer_displacement_mode == 2) {
      outer_displacement = r.get_s16b();
    } else if (outer_displacement_mode == 3) {
      outer_displacement = r.get_s32b();
    }
    if (outer_displacement > 0) {
      ret += string_printf(" + 0x%" PRIX32, outer_displacement);
    } else if (outer_displacement < 0) {
      ret += string_printf(" - 0x%" PRIX32, -outer_displacement);
    }
    ret += ']';
  }

  return ret;
}

static string estimate_pstring(const StringReader& r, uint32_t addr) {
  try {
    uint8_t len = r.pget_u8(addr);
    if (len < 2) {
      return "";
    }

    string data = r.pread(addr + 1, len);
    string formatted_data = "\"";
    for (char ch : data) {
      if (ch == '\r') {
        formatted_data += "\\r";
      } else if (ch == '\n') {
        formatted_data += "\\n";
      } else if (ch == '\t') {
        formatted_data += "\\t";
      } else if (ch == '\'') {
        formatted_data += "\\\'";
      } else if (ch == '\"') {
        formatted_data += "\\\"";
      } else if (ch >= 0x20 && ch <= 0x7E) {
        formatted_data += ch;
      } else {
        return "";
      }
    }
    formatted_data += '\"';
    return formatted_data;

  } catch (const out_of_range&) {
    return "";
  }
}

static string estimate_cstring(const StringReader& r, uint32_t addr) {
  string formatted_data = "\"";

  try {
    StringReader sr = r.sub(addr);

    char ch;
    for (ch = sr.get_s8();
         ch != 0 && formatted_data.size() < 0x20;
         ch = sr.get_s8()) {
      if (ch == '\r') {
        formatted_data += "\\\r";
      } else if (ch == '\n') {
        formatted_data += "\\\n";
      } else if (ch == '\t') {
        formatted_data += "\\\t";
      } else if (ch == '\'') {
        formatted_data += "\\\'";
      } else if (ch == '\"') {
        formatted_data += "\\\"";
      } else if (ch >= 0x20 && ch <= 0x7E) {
        formatted_data += ch;
      } else {
        return ""; // Probably not an ASCII cstring
      }
    }
    if (ch) {
      formatted_data += "\"...";
    } else {
      formatted_data += '\"';
    }
  } catch (const out_of_range&) {
    // Valid cstrings are always terminated; if we reach EOF, treat it as an
    // invalid cstring
    return "";
  }
  return formatted_data;
}

string M68KEmulator::dasm_address(
    DisassemblyState& s,
    uint8_t M,
    uint8_t Xn,
    ValueType type,
    AddressDisassemblyType dasm_type) {
  switch (M) {
    case 0:
      return string_printf("D%hhu", Xn);
    case 1:
      return string_printf("A%hhu", Xn);
    case 2:
      return string_printf("[A%hhu]", Xn);
    case 3:
      return string_printf("[A%hhu]+", Xn);
    case 4:
      return string_printf("-[A%hhu]", Xn);
    case 5: {
      int16_t displacement = s.r.get_u16b();
      if (displacement < 0) {
        return string_printf("[A%hhu - 0x%X]", Xn, -displacement);
      } else {
        // Special case: the jump table is located at A5. So if displacement is
        // positive and aligned with a jump table entry, and Xn is A5, write the
        // export label name as well.
        if (Xn == 5 && displacement >= 0x20 && (displacement & 7) == 2) {
          size_t export_number = (displacement - 0x22) / 8;
          if (s.jump_table) {
            if (export_number < s.jump_table->size()) {
              const auto& entry = (*s.jump_table)[export_number];
              return string_printf(
                  "[A%hhu + 0x%" PRIX16 " /* export_%zu, CODE:%hd @ %08hX */]",
                  Xn, displacement, export_number, entry.code_resource_id, entry.offset);
            } else {
              return string_printf(
                  "[A%hhu + 0x%" PRIX16 " /* export_%zu, out of jump table range */]",
                  Xn, displacement, export_number);
            }
          } else {
            return string_printf("[A%hhu + 0x%" PRIX16 " /* export_%zu */]", Xn,
                displacement, export_number);
          }
        } else {
          return string_printf("[A%hhu + 0x%" PRIX16 "]", Xn, displacement);
        }
      }
    }
    case 6: {
      uint16_t ext = s.r.get_u16b();
      return M68KEmulator::dasm_address_extension(s.r, ext, Xn);
    }
    case 7: {
      switch (Xn) {
        case 0: {
          uint32_t address = s.r.get_u16b();
          if (address & 0x00008000) {
            address |= 0xFFFF0000;
          }
          const char* name = name_for_lowmem_global(address);
          if (name) {
            return string_printf("[0x%08" PRIX32 " /* %s */]", address, name);
          } else {
            return string_printf("[0x%08" PRIX32 "]", address);
          }
        }
        case 1: {
          uint32_t address = s.r.get_u32b();
          const char* name = name_for_lowmem_global(address);
          if (name) {
            return string_printf("[0x%08" PRIX32 " /* %s */]", address, name);
          } else {
            return string_printf("[0x%08" PRIX32 "]", address);
          }
        }
        case 2: {
          int16_t displacement = s.r.get_s16b();
          uint32_t target_address = s.opcode_start_address + displacement + 2;
          if ((dasm_type != AddressDisassemblyType::DATA) && !(target_address & 1)) {
            if (dasm_type == AddressDisassemblyType::FUNCTION_CALL) {
              s.branch_target_addresses[target_address] = true;
            } else {
              s.branch_target_addresses.emplace(target_address, false);
            }
          }
          if (displacement == 0) {
            return string_printf("[PC /* %08" PRIX32 " */]", target_address);
          } else {
            string offset_str = (displacement > 0)
                ? string_printf(" + 0x%" PRIX16, displacement)
                : string_printf(" - 0x%X", -displacement);

            vector<string> comment_tokens;
            comment_tokens.emplace_back(string_printf("%08" PRIX32, target_address));

            // Values are probably not useful if this is a jump or call
            if (dasm_type == AddressDisassemblyType::DATA) {
              try {
                switch (type) {
                  case ValueType::BYTE:
                    comment_tokens.emplace_back("value " + format_immediate(s.r.pget_u8(target_address - s.start_address), false));
                    break;
                  case ValueType::WORD:
                    comment_tokens.emplace_back("value " + format_immediate(s.r.pget_u16b(target_address - s.start_address), false));
                    break;
                  case ValueType::LONG:
                    comment_tokens.emplace_back("value " + format_immediate(s.r.pget_u32b(target_address - s.start_address), false));
                    break;
                  case ValueType::FLOAT:
                    comment_tokens.emplace_back(string_printf(
                        "value %g", s.r.pget<be_float>(target_address - s.start_address).load()));
                    break;
                  case ValueType::DOUBLE:
                    comment_tokens.emplace_back(string_printf(
                        "value %g", s.r.pget<be_double>(target_address - s.start_address).load()));
                    break;
                  default:
                    // TODO: implement this for EXTENDED and PACKED_DECIMAL_REAL
                    // See page 1-23 in programmer's manual for EXTENDED format;
                    // see page 1-24 for PACKED_DECIMAL_REAL format
                    break;
                }
              } catch (const out_of_range&) {
              }

              string estimated_pstring = estimate_pstring(s.r, target_address - s.start_address);
              if (!estimated_pstring.empty()) {
                comment_tokens.emplace_back("pstring " + estimated_pstring);
              } else {
                string estimated_cstring = estimate_cstring(s.r, target_address - s.start_address);
                if (!estimated_cstring.empty()) {
                  comment_tokens.emplace_back("cstring " + estimated_cstring);
                }
              }
            }

            string joined_tokens = join(comment_tokens, ", ");
            return string_printf("[PC%s /* %s */]",
                offset_str.c_str(), joined_tokens.c_str());
          }
        }
        case 3: {
          uint16_t ext = s.r.get_u16b();
          return M68KEmulator::dasm_address_extension(s.r, ext, -1);
        }
        case 4:
          switch (type) {
            case ValueType::BYTE:
              return format_immediate(read_immediate_int(s.r, SIZE_BYTE));
            case ValueType::WORD:
              return format_immediate(read_immediate_int(s.r, SIZE_WORD));
            case ValueType::LONG:
              return format_immediate(read_immediate_int(s.r, SIZE_LONG));
            case ValueType::FLOAT:
              return string_printf("%g", s.r.get<be_float>().load());
            case ValueType::DOUBLE:
              return string_printf("%g", s.r.get<be_double>().load());
            case ValueType::EXTENDED:
              return "(extended)0x" + format_data_string(s.r.read(12));
            case ValueType::PACKED_DECIMAL_REAL: {
              uint32_t high = s.r.get_u32b();
              uint64_t low = s.r.get_u64b();
              return "(packed)" + format_packed_decimal_real(high, low);
            }
            default:
              throw logic_error("invalid value type");
          }
        default:
          return "<<invalid special address>>";
      }
    }
    default:
      return "<<invalid address>>";
  }
}

bool M68KEmulator::check_condition(uint8_t condition) {
  // Bits in the CCR are xnzvc so e.g. 0x16 means x, z, and v are set
  switch (condition) {
    case 0x00: // true
      return true;
    case 0x01: // false
      return false;
    case 0x02: // hi (high, unsigned greater; c=0 and z=0)
      return (this->regs.sr & 0x0005) == 0;
    case 0x03: // ls (low or same, unsigned less or equal; c=1 or z=1)
      return (this->regs.sr & 0x0005) != 0;
    case 0x04: // cc (carry clear; c=0)
      return (this->regs.sr & 0x0001) == 0;
    case 0x05: // cs (carry set; c=1)
      return (this->regs.sr & 0x0001) != 0;
    case 0x06: // ne (not equal; z=0)
      return (this->regs.sr & 0x0004) == 0;
    case 0x07: // eq (equal; z=1)
      return (this->regs.sr & 0x0004) != 0;
    case 0x08: // vc (overflow clear; v=0)
      return (this->regs.sr & 0x0002) == 0;
    case 0x09: // vs (overflow set; v=1)
      return (this->regs.sr & 0x0002) != 0;
    case 0x0A: // pl (plus; n=0)
      return (this->regs.sr & 0x0008) == 0;
    case 0x0B: // mi (minus; n=1)
      return (this->regs.sr & 0x0008) != 0;
    case 0x0C: // ge (greater or equal; n=v)
      return ((this->regs.sr & 0x000A) == 0x0000) || ((this->regs.sr & 0x000A) == 0x000A);
    case 0x0D: // lt (less; n!=v)
      return ((this->regs.sr & 0x000A) == 0x0008) || ((this->regs.sr & 0x000A) == 0x0002);
    case 0x0E: // gt (greater; n=v && z=0)
      return ((this->regs.sr & 0x000E) == 0x000A) || ((this->regs.sr & 0x000E) == 0x0000);
    case 0x0F: // le (less or equal; n!=v || z=1)
      return ((this->regs.sr & 0x0004) == 0x0004) || ((this->regs.sr & 0x000A) == 0x0008) || ((this->regs.sr & 0x000A) == 0x0002);
    default:
      throw runtime_error("invalid condition code");
  }
}

void M68KEmulator::exec_unimplemented(uint16_t) {
  throw runtime_error("unimplemented opcode");
}

string M68KEmulator::dasm_unimplemented(DisassemblyState& s) {
  return string_printf(".unimplemented %04hX", s.r.get_u16b());
}

void M68KEmulator::exec_0123(uint16_t opcode) {
  // 1, 2, 3 are actually also handled by 0 (this is the only case where the i
  // field is split)
  uint8_t i = op_get_i(opcode);
  if (i) {
    uint8_t size = size_for_dsize[i];
    if (op_get_b(opcode) == 1) {
      // movea.S An, ADDR
      if (size == SIZE_BYTE) {
        throw runtime_error("invalid movea.b opcode");
      }

      uint8_t source_M = op_get_c(opcode);
      uint8_t source_Xn = op_get_d(opcode);
      auto source = this->resolve_address(source_M, source_Xn, size);

      // movea is always a long write, even if it's a word read - so we don't
      // use this->write, etc.
      this->regs.a[op_get_a(opcode)] = sign_extend(this->read(source, size), size);
      return;

    } else {
      // move.S ADDR1, ADDR2

      uint8_t source_M = op_get_c(opcode);
      uint8_t source_Xn = op_get_d(opcode);
      auto source_addr = this->resolve_address(source_M, source_Xn, size);

      // Note: this isn't a bug; the instruction format really is
      // <r1><m1><m2><r2>
      uint8_t dest_M = op_get_b(opcode);
      uint8_t dest_Xn = op_get_a(opcode);
      auto dest_addr = this->resolve_address(dest_M, dest_Xn, size);

      uint32_t value = this->read(source_addr, size);
      this->write(dest_addr, value, size);
      this->regs.set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
      return;
    }
  }

  // Note: i == 0 if we get here

  uint8_t a = op_get_a(opcode);
  uint8_t M = op_get_c(opcode);
  uint8_t Xn = op_get_d(opcode);
  uint8_t op_size = op_get_size(opcode);
  // TODO: movep

  string operation;
  if (op_get_g(opcode)) {
    auto addr = this->resolve_address(M, Xn, op_size);

    uint32_t test_value = 1 << (this->regs.d[a].u & (addr.is_register() ? 0x1F : 0x07));
    uint8_t data_size = addr.is_register() ? SIZE_LONG : SIZE_BYTE;
    uint32_t mem_value = this->read(addr, data_size);

    this->regs.set_ccr_flags(-1, -1, (mem_value & test_value) ? 0 : 1, -1, -1);

    switch (op_size) {
      case 0: // btst ADDR, Dn
        // Don't change the bit
        break;
      case 1: // bchg ADDR, Dn
        mem_value ^= test_value;
        break;
      case 2: // bclr ADDR, Dn
        mem_value &= ~test_value;
        break;
      case 3: { // bset ADDR, Dn
        mem_value |= test_value;
      }
    }

    this->write(addr, mem_value, data_size);
    return;
  }

  // Note: the bit operations (btst, bchg, bclr, bset) are always byte
  // operations, and the size field (s) instead says which operation it is.
  if (a == 4) {
    auto addr = this->resolve_address(M, Xn, SIZE_BYTE);
    uint32_t value = this->fetch_instruction_data(SIZE_WORD);

    uint32_t mask;
    uint8_t data_size;
    if (addr.is_register()) {
      data_size = SIZE_LONG;
      mask = (1 << (value & 0x1F));
    } else {
      data_size = SIZE_BYTE;
      mask = (1 << (value & 0x07));
    }
    uint32_t mem_value = this->read(addr, data_size);

    this->regs.set_ccr_flags(-1, -1, (mem_value & mask) ? 0 : 1, -1, -1);

    switch (op_size) {
      case 0: // btst ADDR, IMM
        break;
      case 1: // bchg ADDR, IMM
        this->write(addr, mem_value ^ mask, data_size);
        break;
      case 2: // bclr ADDR, IMM
        this->write(addr, mem_value & (~mask), data_size);
        break;
      case 3: // bset ADDR, IMM
        this->write(addr, mem_value | mask, data_size);
        break;
      default:
        throw logic_error("s >= 4");
    }
    return;
  }

  // Note: This must happen before the address is resolved, since the immediate
  // data comes before any address extension words.
  uint32_t value = this->fetch_instruction_data(
      (op_size == SIZE_BYTE) ? SIZE_WORD : op_size);

  // ccr/sr are allowed for ori, andi, and xori opcodes
  ResolvedAddress target;
  if (((a == 0) || (a == 1) || (a == 5)) && (M == 7) && (Xn == 4)) {
    if (op_size != SIZE_BYTE && op_size != SIZE_WORD) {
      throw runtime_error("incorrect size for status register");
    }
    target = {0, ResolvedAddress::Location::SR};
  } else {
    target = this->resolve_address(M, Xn, op_size);
  }

  uint32_t mem_value = this->read(target, op_size);
  switch (a) {
    case 0: // ori ADDR, IMM
      mem_value |= value;
      this->write(target, mem_value, op_size);
      this->regs.set_ccr_flags(-1, is_negative(mem_value, op_size), !mem_value, 0, 0);
      break;

    case 1: // andi ADDR, IMM
      mem_value &= value;
      this->write(target, mem_value, op_size);
      this->regs.set_ccr_flags(-1, is_negative(mem_value, op_size), !mem_value, 0, 0);
      break;

    case 2: // subi ADDR, IMM
      this->regs.set_ccr_flags_integer_subtract(mem_value, value, op_size);
      this->regs.set_ccr_flags(this->regs.sr & 0x0001, -1, -1, -1, -1);
      mem_value -= value;
      this->write(target, mem_value, op_size);
      break;

    case 3: // addi ADDR, IMM
      this->regs.set_ccr_flags_integer_add(mem_value, value, op_size);
      this->regs.set_ccr_flags(this->regs.sr & 0x0001, -1, -1, -1, -1);
      mem_value += value;
      this->write(target, mem_value, op_size);
      break;

    case 5: // xori ADDR, IMM
      mem_value ^= value;
      this->write(target, mem_value, op_size);
      this->regs.set_ccr_flags(-1, is_negative(mem_value, op_size), !mem_value, 0, 0);
      break;

    case 6: // cmpi ADDR, IMM
      this->regs.set_ccr_flags_integer_subtract(mem_value, value, op_size);
      break;

    case 4:
      throw logic_error("this should have been handled already");

    default:
      throw runtime_error("invalid immediate operation");
  }
}

string M68KEmulator::dasm_0123(DisassemblyState& s) {
  // 1, 2, 3 are actually also handled by 0 (this is the only case where the i
  // field is split)
  uint16_t op = s.r.get_u16b();
  uint8_t i = op_get_i(op);
  if (i) {
    ValueType value_type = value_type_for_dsize.at(i);
    if (op_get_b(op) == 1) {
      // movea isn't valid with the byte operand size. We'll disassemble it
      // anyway, but complain at the end of the line

      uint8_t source_M = op_get_c(op);
      uint8_t source_Xn = op_get_d(op);
      string source_addr = M68KEmulator::dasm_address(
          s, source_M, source_Xn, value_type);

      uint8_t An = op_get_a(op);
      if (i == SIZE_BYTE) {
        return string_printf(".invalid   A%d, %s // movea not valid with byte operand size",
            An, source_addr.c_str());
      } else {
        return string_printf("movea.%c    A%d, %s", char_for_dsize.at(i), An, source_addr.c_str());
      }

    } else {
      // Note: empirically the order seems to be source addr first, then dest
      // addr. This is relevant when both contain displacements or extensions
      uint8_t source_M = op_get_c(op);
      uint8_t source_Xn = op_get_d(op);
      string source_addr = M68KEmulator::dasm_address(
          s, source_M, source_Xn, value_type);

      // Note: this isn't a bug; the instruction format really is
      // <r1><m1><m2><r2>
      uint8_t dest_M = op_get_b(op);
      uint8_t dest_Xn = op_get_a(op);
      string dest_addr = M68KEmulator::dasm_address(
          s, dest_M, dest_Xn, value_type);

      return string_printf("move.%c     %s, %s", char_for_dsize.at(i),
          dest_addr.c_str(), source_addr.c_str());
    }
  }

  // Note: i == 0 if we get here

  uint8_t a = op_get_a(op);
  uint8_t M = op_get_c(op);
  uint8_t Xn = op_get_d(op);
  uint8_t size = op_get_size(op);
  // TODO: movep
  string operation;
  const char* invalid_str = "";
  bool special_regs_allowed = false;
  if (op_get_g(op)) {
    switch (size) {
      case 0:
        operation = "btst";
        break;
      case 1:
        operation = "bchg";
        break;
      case 2:
        operation = "bclr";
        break;
      case 3:
        operation = "bset";
        break;
    }

    string addr = M68KEmulator::dasm_address(
        s, M, Xn, value_type_for_size.at(size));
    return string_printf("%s       %s, D%d", operation.c_str(), addr.c_str(), op_get_a(op));

  } else {
    switch (a) {
      case 0:
        operation = "ori";
        special_regs_allowed = true;
        break;
      case 1:
        operation = "andi";
        special_regs_allowed = true;
        break;
      case 2:
        operation = "subi";
        break;
      case 3:
        operation = "addi";
        break;
      case 5:
        operation = "xori";
        special_regs_allowed = true;
        break;
      case 6:
        operation = "cmpi";
        break;

      case 4:
        switch (size) {
          case 0:
            operation = "btst";
            break;
          case 1:
            operation = "bchg";
            break;
          case 2:
            operation = "bclr";
            break;
          case 3:
            operation = "bset";
            break;
        }
        size = SIZE_BYTE; // TODO: support longs somehow
        break;

      default:
        operation = ".invalid";
        invalid_str = " // invalid immediate operation";
    }
  }

  operation += '.';
  operation += char_for_size.at(size);
  operation.resize(10, ' ');

  if (special_regs_allowed && (M == 7) && (Xn == 4)) {
    if (size == 0) {
      return string_printf("%s ccr, %d%s", operation.c_str(),
          s.r.get_u16b() & 0x00FF, invalid_str);
    } else if (size == 1) {
      return string_printf("%s sr, %d%s", operation.c_str(), s.r.get_u16b(),
          invalid_str);
    }
  }

  // Note: format_immediate must happen before the address is resolved, since
  // the immediate data comes before any address extension words.
  string imm = format_immediate(read_immediate_int(s.r, size));
  string addr = M68KEmulator::dasm_address(
      s, M, Xn, value_type_for_size.at(size));
  return string_printf("%s %s, %s%s", operation.c_str(), addr.c_str(),
      imm.c_str(), invalid_str);
}

void M68KEmulator::exec_4(uint16_t opcode) {
  uint8_t g = op_get_g(opcode);

  if (g == 0) {
    if (opcode == 0x4AFC) {
      throw runtime_error("invalid opcode 4AFC");
    }
    if ((opcode & 0xFFF0) == 0x4E70) {
      switch (opcode & 0x000F) {
        case 0: // reset
          throw terminate_emulation();
          return;
        case 1: // nop
          return;
        case 2: // stop IMM
          throw runtime_error("unimplemented: stop IMM");
        case 3: // rte
          throw runtime_error("unimplemented: rte");
        case 4: // rtd IMM
          throw runtime_error("unimplemented: rtd IMM");
        case 5: // rts
          this->regs.pc = this->read(this->regs.a[7], SIZE_LONG);
          this->regs.a[7] += 4;
          return;
        case 6: // trapv
          if (this->regs.sr & Condition::V) {
            throw runtime_error("unimplemented: overflow trap");
          }
          return;
        case 7: // rtr
          // The supervisor portion (high byte) of SR is unaffected
          this->regs.sr = (this->regs.sr & 0xFF00) |
              (this->read(this->regs.a[7], SIZE_WORD) & 0x00FF);
          this->regs.pc = this->read(this->regs.a[7] + 2, SIZE_LONG);
          this->regs.a[7] += 6;
          return;
        default:
          throw runtime_error("invalid special operation");
      }
    }

    uint8_t a = op_get_a(opcode);
    if (!(a & 0x04)) {
      uint8_t size = op_get_size(opcode);
      auto addr = this->resolve_address(op_get_c(opcode), op_get_d(opcode),
          (size == 3) ? SIZE_WORD : size);

      if (size == 3) {
        if (a == 0) { // move.w ADDR, sr
          throw runtime_error("cannot read from sr in user mode");
        } else if (a == 1) { // move.w ccr, ADDR
          this->regs.sr = (this->regs.sr & 0xFF00) | (this->read(addr, SIZE_WORD) & 0x001F);
          return;
        } else if (a == 2) { // move.w ADDR, ccr
          this->write(addr, this->regs.sr & 0x00FF, SIZE_WORD);
          return;
        } else if (a == 3) { // move.w sr, ADDR
          throw runtime_error("cannot write to sr in user mode");
        }
        throw runtime_error("invalid opcode 4:1");

      } else { // s is a valid SIZE_*
        switch (a) {
          case 0: // negx.S ADDR
            throw runtime_error("unimplemented: negx.S ADDR");
          case 1: // clr.S ADDR
            this->write(addr, 0, size);
            this->regs.set_ccr_flags(-1, 0, 1, 0, 0);
            return;
          case 2: { // neg.S ADDR
            int32_t value = -static_cast<int32_t>(this->read(addr, size));
            this->write(addr, value, size);
            this->regs.set_ccr_flags((value != 0), is_negative(value, size),
                (value == 0), (-value == value), (value != 0));
            return;
          }
          case 3: { // not.S ADDR
            uint32_t value = ~static_cast<int32_t>(this->read(addr, size));
            this->write(addr, value, size);
            this->regs.set_ccr_flags(-1, is_negative(value, size), (value == 0),
                0, 0);
            return;
          }
        }
      }

    } else { // a & 0x04
      uint8_t b = op_get_b(opcode); // b must be 0-3 since we already checked that g = 0

      if (a == 4) {
        uint8_t M = op_get_c(opcode);
        if (b & 2) {
          if (M == 0) { // ext.S REG
            uint8_t d = op_get_d(opcode);
            switch (b) {
              case 2: // extend byte to word
                this->regs.d[d].u = (this->regs.d[d].u & 0xFFFF00FF) |
                    ((this->regs.d[d].u & 0x00000080) ? 0x0000FF00 : 0x00000000);
                this->regs.set_ccr_flags(-1, is_negative(this->regs.d[d].u, SIZE_LONG),
                    (this->regs.d[d].u == 0), 0, 0);
                return;

              case 3: // extend word to long
                this->regs.d[d].u = (this->regs.d[d].u & 0x0000FFFF) |
                    ((this->regs.d[d].u & 0x00008000) ? 0xFFFF0000 : 0x00000000);
                this->regs.set_ccr_flags(-1, is_negative(this->regs.d[d].u, SIZE_LONG),
                    (this->regs.d[d].u == 0), 0, 0);
                return;

              case 7: // extend byte to long
                this->regs.d[d].u = (this->regs.d[d].u & 0x000000FF) |
                    ((this->regs.d[d].u & 0x00000080) ? 0xFFFFFF00 : 0x00000000);
                this->regs.set_ccr_flags(-1, is_negative(this->regs.d[d].u, SIZE_LONG),
                    (this->regs.d[d].u == 0), 0, 0);
                return;

              default:
                throw runtime_error("unimplemented: like ext.S REG");
            }

          } else { // movem.S ADDR REGMASK
            uint8_t size = size_for_tsize[op_get_t(opcode)];
            uint8_t bytes_per_value = bytes_for_size[size];
            uint8_t Xn = op_get_d(opcode);
            uint16_t reg_mask = this->fetch_instruction_word();

            // Predecrement mode is special-cased for this opcode. In this mode
            // we write the registers in reverse order
            if (M == 4) {
              // bit 15 is D0, bit 0 is A7
              for (size_t x = 0; x < 8; x++) {
                if (reg_mask & (1 << x)) {
                  this->regs.a[Xn] -= bytes_per_value;
                  this->write(this->regs.a[Xn], this->regs.a[7 - x], size);
                }
              }
              for (size_t x = 0; x < 8; x++) {
                if (reg_mask & (1 << (x + 8))) {
                  this->regs.a[Xn] -= bytes_per_value;
                  this->write(this->regs.a[Xn], this->regs.d[7 - x].u, size);
                }
              }

            } else {
              // bit 15 is A7, bit 0 is D0
              uint32_t addr = this->resolve_address_control(M, Xn);
              for (size_t x = 0; x < 8; x++) {
                if (reg_mask & (1 << x)) {
                  this->write(addr, this->regs.d[x].u, size);
                  addr += bytes_per_value;
                }
              }
              for (size_t x = 0; x < 8; x++) {
                if (reg_mask & (1 << (x + 8))) {
                  this->write(addr, this->regs.a[x], size);
                  addr += bytes_per_value;
                }
              }
            }

            // Note: ccr not affected
            return;
          }
        }
        if (b == 0) { // nbcd.b ADDR
          // void* addr = this->resolve_address(M, op_get_d(opcode), SIZE_BYTE);
          throw runtime_error("unimplemented: nbcd.b ADDR");
        }
        // b == 1
        if (M == 0) { // swap.w REG
          uint8_t reg = op_get_d(opcode);
          this->regs.d[reg].u = (this->regs.d[reg].u >> 16) | (this->regs.d[reg].u << 16);
          return;
        }

        // pea.l ADDR
        uint32_t addr = this->resolve_address_control(op_get_c(opcode),
            op_get_d(opcode));
        this->regs.a[7] -= 4;
        this->write(this->regs.a[7], addr, SIZE_LONG);
        // Note: ccr not affected
        return;

      } else if (a == 5) {
        if (b == 3) { // tas.b ADDR
          // void* addr = this->resolve_address(op_get_c(opcode), op_get_d(opcode), SIZE_LONG);
          throw runtime_error("unimplemented: tas.b ADDR");
        }

        // tst.S ADDR
        auto addr = this->resolve_address(op_get_c(opcode), op_get_d(opcode), b);
        uint8_t size = op_get_b(opcode) & 3;
        uint32_t value = this->read(addr, size);
        this->regs.set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
        return;

      } else if (a == 6) {
        if ((b & (~1)) == 0) {
          throw runtime_error("unimplemented: muls/mulu/divs/divu (long)");

        } else {
          // movem.S REGMASK ADDR
          uint8_t size = size_for_tsize[op_get_t(opcode)];
          uint8_t bytes_per_value = bytes_for_size[size];
          uint8_t M = op_get_c(opcode);
          uint8_t Xn = op_get_d(opcode);
          uint16_t reg_mask = this->fetch_instruction_word();

          // Postincrement mode is special-cased for this opcode
          uint32_t addr;
          if (M == 3) {
            addr = this->regs.a[Xn];
          } else {
            addr = this->resolve_address_control(M, Xn);
          }

          // Load the regs; bit 15 is A7, bit 0 is D0
          for (size_t x = 0; x < 8; x++) {
            if (reg_mask & (1 << x)) {
              this->regs.d[x].u = this->read(addr, size);
              addr += bytes_per_value;
            }
          }
          for (size_t x = 0; x < 8; x++) {
            if (reg_mask & (1 << (x + 8))) {
              this->regs.a[x] = this->read(addr, size);
              addr += bytes_per_value;
            }
          }

          // In postincrement mode, update the address register
          if (M == 3) {
            this->regs.a[Xn] = addr;
          }

          // Note: ccr not affected
          return;
        }

      } else if (a == 7) {
        if (b == 1) {
          uint8_t c = op_get_c(opcode);
          if (c == 2) { // link
            uint8_t d = op_get_d(opcode);
            this->regs.a[7] -= 4;
            this->write(this->regs.a[7], this->regs.a[d], SIZE_LONG);
            this->regs.a[d] = this->regs.a[7];
            this->regs.a[7] += this->fetch_instruction_word_signed();
            // Note: ccr not affected
            return;

          } else if (c == 3) { // unlink
            uint8_t d = op_get_d(opcode);
            this->regs.a[7] = this->regs.a[d];
            this->regs.a[d] = this->read(this->regs.a[7], SIZE_LONG);
            this->regs.a[7] += 4;
            // Note: ccr not affected
            return;

          } else if ((c & 6) == 0) { // trap NUM
            throw runtime_error("unimplemented: trap NUM"); // num is v field

          } else if ((c & 6) == 4) { // move USP, AREG or AREG, USP
            throw runtime_error("unimplemented: move USP AREG STORE/LOAD"); // areg is d field, c&1 means store
          }

        } else if (b == 2) { // jsr ADDR
          uint32_t addr = this->resolve_address_control(op_get_c(opcode), op_get_d(opcode));
          this->regs.a[7] -= 4;
          this->write(this->regs.a[7], this->regs.pc, SIZE_LONG);
          this->regs.pc = addr;
          // Note: ccr not affected
          return;

        } else if (b == 3) { // jmp ADDR
          this->regs.pc = this->resolve_address_control(op_get_c(opcode), op_get_d(opcode));
          // Note: ccr not affected
          return;
        }

      } else {
        throw runtime_error("invalid opcode 4");
      }
    }

  } else { // g == 1
    uint8_t b = op_get_b(opcode);
    if (b == 7) { // lea.l AREG, ADDR
      this->regs.a[op_get_a(opcode)] = this->resolve_address_control(
          op_get_c(opcode), op_get_d(opcode));
      // Note: ccr not affected
      return;

    } else if (b == 5) { // chk.w DREG, ADDR
      // void* addr = this->resolve_address(op_get_c(opcode), op_get_d(opcode), SIZE_WORD);
      throw runtime_error("unimplemented: chk.w DREG ADDR"); // dreg is a field
    }
  }

  throw runtime_error("invalid opcode 4");
}

string M68KEmulator::dasm_4(DisassemblyState& s) {
  uint16_t op = s.r.get_u16b();
  uint8_t g = op_get_g(op);

  if (g == 0) {
    if (op == 0x4AFA) {
      return "bgnd";
    }
    if (op == 0x4AFC) {
      return ".invalid";
    }
    if ((op & 0xFFF0) == 0x4E70) {
      switch (op & 0x000F) {
        case 0:
          return "reset";
        case 1:
          return "nop";
        case 2:
          return string_printf("stop       0x%04X", s.r.get_u16b());
        case 3:
          return "rte";
        case 4:
          s.prev_was_return = true;
          return string_printf("rtd        0x%04X", s.r.get_u16b());
        case 5:
          s.prev_was_return = true;
          return "rts";
        case 6:
          return "trapv";
        case 7:
          return "rtr";
      }
    }

    uint8_t a = op_get_a(op);
    if (!(a & 0x04)) {
      string addr = M68KEmulator::dasm_address(s, op_get_c(op), op_get_d(op), ValueType::LONG);

      uint8_t size = op_get_size(op);
      if (size == 3) {
        if (a == 0) {
          return string_printf("move.w     %s, SR", addr.c_str());
        } else if (a == 2) {
          return string_printf("move.b     %s, CCR", addr.c_str());
        } else if (a == 3) {
          return string_printf("move.w     SR, %s", addr.c_str());
        }
        return string_printf(".invalid   %s // invalid opcode 4 with subtype 1",
            addr.c_str());

      } else { // s is a valid SIZE_x
        char size_ch = char_for_size.at(size);
        switch (a) {
          case 0:
            return string_printf("negx.%c     %s", size_ch, addr.c_str());
          case 1:
            return string_printf("clr.%c      %s", size_ch, addr.c_str());
          case 2:
            return string_printf("neg.%c      %s", size_ch, addr.c_str());
          case 3:
            return string_printf("not.%c      %s", size_ch, addr.c_str());
        }
      }

    } else { // a & 0x04
      uint8_t b = op_get_b(op); // b must be 0-3 since we already checked that g = 0

      if (a == 4) {
        uint8_t M = op_get_c(op);
        if (b & 2) {
          if (M == 0) {
            return string_printf("ext.%c      D%d", char_for_tsize.at(op_get_t(op)), op_get_d(op));
          } else {
            uint8_t t = op_get_t(op);
            string reg_mask = M68KEmulator::dasm_reg_mask(s.r.get_u16b(), (M == 4));
            string addr = M68KEmulator::dasm_address(
                s, M, op_get_d(op), value_type_for_tsize.at(t));
            return string_printf("movem.%c    %s, %s", char_for_tsize.at(t), addr.c_str(), reg_mask.c_str());
          }
        }
        if (b == 0) {
          string addr = M68KEmulator::dasm_address(
              s, M, op_get_d(op), ValueType::BYTE);
          return string_printf("nbcd.b     %s", addr.c_str());
        }
        // b == 1
        if (M == 0) {
          return string_printf("swap.w     D%d", op_get_d(op));
        }
        // Special-case `pea.l [IMM]` since the 32-bit form is likely to contain
        // an OSType, which we should ASCII-decode if possible
        if ((op & 0xFFFE) == 0x4878) {
          string imm = format_immediate(read_immediate_int(
              s.r, (op & 1) ? SIZE_LONG : SIZE_WORD));
          return string_printf("push.l     %s", imm.c_str());
        } else {
          string addr = M68KEmulator::dasm_address(
              s, M, op_get_d(op), ValueType::LONG);
          return string_printf("pea.l      %s", addr.c_str());
        }

      } else if (a == 5) {
        if (b == 3) {
          string addr = M68KEmulator::dasm_address(
              s, op_get_c(op), op_get_d(op), ValueType::LONG);
          return string_printf("tas.b      %s", addr.c_str());
        }

        string addr = M68KEmulator::dasm_address(
            s, op_get_c(op), op_get_d(op), value_type_for_size.at(b));
        return string_printf("tst.%c      %s", char_for_size.at(b), addr.c_str());

      } else if (a == 6) {
        if ((b & (~1)) == 0) {
          string addr = M68KEmulator::dasm_address(
              s, op_get_c(op), op_get_d(op), ValueType::LONG);

          uint16_t args = s.r.get_u16b();
          bool is_signed = args & 0x0800;
          bool is_64bit = args & 0x0400;
          if (b & 1) {
            uint8_t rq = (args >> 12) & 7;
            uint8_t rr = args & 7;
            string opcode_name = "div";
            opcode_name += is_signed ? 's' : 'u';
            if (is_64bit) {
              opcode_name += 'l';
            }
            opcode_name += ".l";
            opcode_name.resize(11, ' ');
            return string_printf("%sD%hhu:D%hhu, %s",
                opcode_name.c_str(), rr, rq, addr.c_str());
          } else {
            uint8_t rl = (args >> 12) & 7;
            if (is_64bit) {
              uint8_t rh = args & 7;
              return string_printf("mul%c.l     D%hhu:D%hhu, %s",
                  is_signed ? 's' : 'u', rh, rl, addr.c_str());
            } else {
              return string_printf("mul%c.l     D%hhu, %s",
                  is_signed ? 's' : 'u', rl, addr.c_str());
            }
          }

        } else {
          uint8_t t = op_get_t(op);
          uint8_t M = op_get_c(op);
          string reg_mask = M68KEmulator::dasm_reg_mask(s.r.get_u16b(), (M == 4));
          string addr = M68KEmulator::dasm_address(
              s, M, op_get_d(op), value_type_for_tsize.at(t));
          return string_printf("movem.%c    %s, %s", char_for_tsize.at(t), reg_mask.c_str(), addr.c_str());
        }

      } else if (a == 7) {
        if (b == 1) {
          uint8_t c = op_get_c(op);
          if (c == 2) {
            int16_t delta = s.r.get_s16b();
            if (delta == 0) {
              return string_printf("link       A%d, 0", op_get_d(op));
            } else {
              return string_printf("link       A%d, -0x%04X", op_get_d(op), -delta);
            }
          } else if (c == 3) {
            return string_printf("unlink     A%d", op_get_d(op));
          } else if ((c & 6) == 0) {
            return string_printf("trap       %d", op_get_v(op));
          } else if ((c & 6) == 4) {
            if (c & 1) {
              return string_printf("move       A%d, USP", op_get_d(op));
            } else {
              return string_printf("move       USP, A%d", op_get_d(op));
            }
          }

        } else if (b == 2) {
          string addr = M68KEmulator::dasm_address(
              s, op_get_c(op), op_get_d(op), ValueType::LONG,
              AddressDisassemblyType::FUNCTION_CALL);
          return string_printf("jsr        %s", addr.c_str());

        } else if (b == 3) {
          string addr = M68KEmulator::dasm_address(
              s, op_get_c(op), op_get_d(op), ValueType::LONG,
              AddressDisassemblyType::JUMP);
          s.prev_was_return = (op == 0x4ED0); // jmp [A0]
          return string_printf("jmp        %s", addr.c_str());
        }
      }

      return ".invalid   // invalid opcode 4";
    }

  } else { // g == 1
    uint8_t b = op_get_b(op);
    if (b == 7) {
      string addr = M68KEmulator::dasm_address(
          s, op_get_c(op), op_get_d(op), ValueType::LONG);
      return string_printf("lea.l      A%d, %s", op_get_a(op), addr.c_str());

    } else if (b == 5) {
      string addr = M68KEmulator::dasm_address(
          s, op_get_c(op), op_get_d(op), ValueType::WORD);
      return string_printf("chk.w      D%d, %s", op_get_a(op), addr.c_str());

    } else {
      string addr = M68KEmulator::dasm_address(
          s, op_get_c(op), op_get_d(op), ValueType::LONG);
      return string_printf(".invalid   %d, %s // invalid opcode 4 with b == %d",
          op_get_a(op), addr.c_str(), b);
    }
  }

  return ".invalid   // invalid opcode 4";
}

void M68KEmulator::exec_5(uint16_t opcode) {
  uint8_t M = op_get_c(opcode);
  uint8_t Xn = op_get_d(opcode);

  // TODO: apparently TRAPcc is a special case of opcode 5; implement it

  uint8_t size = op_get_size(opcode);
  if (size == 3) {
    bool result = this->check_condition(op_get_k(opcode));

    if (M == 1) { // dbCC DISPLACEMENT
      int16_t displacement = this->fetch_instruction_word_signed();
      if (!result) {
        // This is not a bug: dbCC actually does only affect the low 16 bits
        uint16_t& target = *reinterpret_cast<uint16_t*>(&this->regs.d[Xn].u);
        target--;
        if (target != 0xFFFF) {
          this->regs.pc += displacement - 2;
        }
      }
      // Note: ccr not affected

    } else { // sCC ADDR
      auto addr = this->resolve_address(M, Xn, SIZE_BYTE);
      this->write(addr, (result ? 0xFF : 0x00), SIZE_BYTE);
      // Note: ccr not affected
    }

  } else { // subq/addq ADDR, IMM
    // TODO: when dealing with address registers, size is ignored according to
    // the manual. Implement this.
    auto addr = this->resolve_address(M, Xn, size);
    uint8_t value = op_get_a(opcode);
    if (value == 0) {
      value = 8;
    }

    // Note: ccr flags are skipped when operating on an A register (M == 1)
    uint32_t mem_value = this->read(addr, size);
    if (op_get_g(opcode)) {
      this->write(addr, mem_value - value, size);
      if (M != 1) {
        this->regs.set_ccr_flags_integer_subtract(mem_value, value, size);
      }
    } else {
      this->write(addr, mem_value + value, size);
      if (M != 1) {
        this->regs.set_ccr_flags_integer_add(mem_value, value, size);
      }
    }
    this->regs.set_ccr_flags(this->regs.sr & 0x01, -1, -1, -1, -1);
  }
}

string M68KEmulator::dasm_5(DisassemblyState& s) {
  uint16_t op = s.r.get_u16b();
  uint32_t pc_base = s.start_address + s.r.where();

  uint8_t M = op_get_c(op);
  uint8_t Xn = op_get_d(op);

  uint8_t size = op_get_size(op);
  if (size == 3) {
    uint8_t k = op_get_k(op);
    const char* cond = string_for_condition.at(k);

    if (M == 1) {
      int16_t displacement = s.r.get_s16b();
      uint32_t target_address = pc_base + displacement;
      if (!(target_address & 1)) {
        s.branch_target_addresses.emplace(target_address, false);
      }
      if (displacement < 0) {
        return string_printf("db%s       D%d, -0x%X /* %08" PRIX32 " */",
            cond, Xn, -displacement + 2, target_address);
      } else {
        return string_printf("db%s       D%d, +0x%X /* %08" PRIX32 " */",
            cond, Xn, displacement + 2, target_address);
      }
    }
    string addr = M68KEmulator::dasm_address(s, M, Xn, ValueType::BYTE,
        AddressDisassemblyType::JUMP);
    return string_printf("s%s        %s", cond, addr.c_str());

  } else {
    string addr = M68KEmulator::dasm_address(s, M, Xn,
        value_type_for_size.at(size));
    uint8_t value = op_get_a(op);
    if (value == 0) {
      value = 8;
    }
    return string_printf("%s.%c     %s, %d", op_get_g(op) ? "subq" : "addq",
        char_for_size.at(size), addr.c_str(), value);
  }
}

void M68KEmulator::exec_6(uint16_t opcode) {
  // bra/bsr/bhi/bls/bcc/bcs/bne/beq/bvc/bvs/bpl/bmi/bge/blt/bgt/ble DISPLACEMENT

  uint32_t return_address = this->regs.pc;
  int32_t displacement = static_cast<int8_t>(op_get_y(opcode));
  if (displacement == 0) {
    displacement = this->fetch_instruction_data_signed(SIZE_WORD, false);
    return_address = this->regs.pc + 2;
  } else if (displacement == -1) {
    displacement = this->fetch_instruction_data_signed(SIZE_LONG, false);
    return_address = this->regs.pc + 4;
  }

  // According to the programmer's manual, the displacement is relative to
  // (pc + 2) regardless of whether there's an extended displacement, hence the
  // initial fetch_instruction_word (before this function was called) doesn't
  // need to be corrected.

  uint8_t k = op_get_k(opcode);
  bool should_branch;
  if (k == 1) { // The 'false' cond has a special meaning here (branch and link)
    this->regs.a[7] -= 4;
    this->write(this->regs.a[7], return_address, SIZE_LONG);
    should_branch = true;
  } else {
    should_branch = this->check_condition(k);
  }

  if (should_branch) {
    this->regs.pc += displacement;
  } else {
    this->regs.pc = return_address;
  }

  // Note: ccr not affected
}

string M68KEmulator::dasm_6(DisassemblyState& s) {
  uint16_t op = s.r.get_u16b();
  uint32_t pc_base = s.start_address + s.r.where();

  int64_t displacement = static_cast<int8_t>(op_get_y(op));
  if (displacement == 0) {
    displacement = s.r.get_s16b();
  } else if (displacement == -1) {
    displacement = s.r.get_s32b();
  }

  // According to the programmer's manual, the displacement is relative to
  // (pc + 2) regardless of whether there's an extended displacement.
  string displacement_str;
  uint32_t target_address = pc_base + displacement;
  if (displacement < 0) {
    displacement_str = string_printf("-0x%" PRIX64 " /* %08" PRIX32 " */",
        -displacement - 2, target_address);
  } else {
    displacement_str = string_printf("+0x%" PRIX64 " /* %08" PRIX32 " */",
        displacement + 2, target_address);
  }

  uint8_t k = op_get_k(op);
  if (!(target_address & 1)) {
    if (k == 1) {
      s.branch_target_addresses[target_address] = true;
    } else {
      s.branch_target_addresses.emplace(target_address, false);
    }
  }

  if (k == 0) {
    return "bra        " + displacement_str;
  }
  if (k == 1) {
    return "bsr        " + displacement_str;
  }
  return string_printf("b%s        %s", string_for_condition.at(k), displacement_str.c_str());
}

void M68KEmulator::exec_7(uint16_t opcode) {
  // moveq DREG, IMM
  uint32_t y = op_get_y(opcode);
  if (y & 0x00000080) {
    y |= 0xFFFFFF00;
  }
  this->regs.d[op_get_a(opcode)].u = y;
  this->regs.set_ccr_flags(-1, (y & 0x80000000), (y == 0), 0, 0);
}

string M68KEmulator::dasm_7(DisassemblyState& s) {
  uint16_t op = s.r.get_u16b();
  int32_t value = static_cast<int32_t>(static_cast<int8_t>(op_get_y(op)));
  return string_printf("moveq.l    D%d, 0x%02X", op_get_a(op), value);
}

void M68KEmulator::exec_8(uint16_t opcode) {
  uint8_t a = op_get_a(opcode);
  uint8_t opmode = op_get_b(opcode);
  uint8_t M = op_get_c(opcode);
  uint8_t Xn = op_get_d(opcode);

  if ((opmode & 3) == 3) {
    auto addr = this->resolve_address(M, Xn, SIZE_WORD);
    uint16_t value = this->read(addr, SIZE_WORD);
    if (value == 0) {
      throw runtime_error("division by zero");
    }

    if (opmode == 3) { // divu.w DREG, ADDR
      uint32_t quotient = this->regs.d[a].u / value;
      uint32_t modulo = this->regs.d[a].u % value;
      this->regs.d[a].s = (modulo << 16) | (quotient & 0xFFFF);
      this->regs.set_ccr_flags(-1, 0, (quotient == 0), !!(quotient & 0xFFFF0000), 0);

    } else { // divs.w DREG, ADDR
      int32_t quotient = this->regs.d[a].s / static_cast<int16_t>(value);
      int32_t modulo = this->regs.d[a].s % static_cast<int16_t>(value);
      this->regs.d[a].s = (modulo << 16) | (quotient & 0xFFFF);
      this->regs.set_ccr_flags(-1, is_negative(quotient, SIZE_WORD), (quotient == 0), !!(quotient & 0xFFFF0000), 0);
    }
    return;
  }

  if ((opmode & 4) && !(M & 6)) {
    if (opmode == 4) { // sbcd DREG, DREG or sbcd -[AREG], -[AREG]
      throw runtime_error("unimplemented: sbcd DREG, DREG or sbcd -[AREG], -[AREG]");
    }
    if (opmode == 5) { // pack DREG, DREG or unpk -[AREG], -[AREG]
      this->fetch_instruction_word();
      throw runtime_error("unimplemented: pack DREG, DREG or unpk -[AREG], -[AREG]");
    }
    if (opmode == 6) { // unpk DREG, DREG or unpk -[AREG], -[AREG]
      this->fetch_instruction_word();
      throw runtime_error("unimplemented: unpk DREG, DREG or unpk -[AREG], -[AREG]");
    }
  }

  uint8_t size = opmode & 3;
  auto addr = this->resolve_address(M, Xn, size);
  uint32_t value = this->read(addr, size) | this->regs.d[a].u;
  if (opmode & 4) { // or.S ADDR DREG
    this->write(addr, value, size);
  } else { // or.S DREG ADDR
    this->regs.d[a].u = value;
  }
  this->regs.set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
}

string M68KEmulator::dasm_8(DisassemblyState& s) {
  uint16_t op = s.r.get_u16b();
  uint8_t a = op_get_a(op);
  uint8_t opmode = op_get_b(op);
  uint8_t M = op_get_c(op);
  uint8_t Xn = op_get_d(op);

  if ((opmode & 3) == 3) {
    char size_ch = (opmode & 4) ? 's' : 'u';
    string ea_dasm = M68KEmulator::dasm_address(s, M, Xn, ValueType::WORD);
    return string_printf("div%c.w     D%hhu, %s", size_ch, a, ea_dasm.c_str());
  }

  if ((opmode & 4) && !(M & 6)) {
    if (opmode == 4) {
      if (M) {
        return string_printf("sbcd       -[A%hhu], -[A%hhu]", a, Xn);
      } else {
        return string_printf("sbcd       D%hhu, D%hhu", a, Xn);
      }
    }
    if ((opmode == 5) || (opmode == 6)) {
      uint16_t value = s.r.get_u16b();
      const char* opcode_name = (opmode == 6) ? "unpk" : "pack";
      if (M) {
        return string_printf("%s       -[A%hhu], -[A%hhu], 0x%04hX",
            opcode_name, a, Xn, value);
      } else {
        return string_printf("%s       D%hhu, D%hhu, 0x%04hX", opcode_name, a,
            Xn, value);
      }
    }
  }

  string ea_dasm = M68KEmulator::dasm_address(
      s, M, Xn, value_type_for_size.at(opmode & 3));
  if (opmode & 4) {
    return string_printf("or.%c       %s, D%hhu", char_for_size.at(opmode & 3),
        ea_dasm.c_str(), a);
  } else {
    return string_printf("or.%c       D%hhu, %s", char_for_size.at(opmode & 3),
        a, ea_dasm.c_str());
  }
}

void M68KEmulator::exec_9D(uint16_t opcode) {
  bool is_add = (opcode & 0xF000) == 0xD000;

  uint8_t dest = op_get_a(opcode);
  uint8_t opmode = op_get_b(opcode);
  uint8_t M = op_get_c(opcode);
  uint8_t Xn = op_get_d(opcode);

  if (((M & 6) == 0) && (opmode & 4) && (opmode != 7)) {
    throw runtime_error("unimplemented: opcode 9/D");
  }

  if ((opmode & 3) == 3) {
    uint32_t mem_value;
    if (opmode & 4) { // add.l/sub.l AREG, ADDR
      auto addr = this->resolve_address(M, Xn, SIZE_LONG);
      mem_value = this->read(addr, SIZE_LONG);

    } else { // add.w/sub.w AREG, ADDR (mem value is sign-extended)
      auto addr = this->resolve_address(M, Xn, SIZE_WORD);
      mem_value = this->read(addr, SIZE_WORD);
      if (mem_value & 0x00008000) {
        mem_value |= 0xFFFF0000;
      }
    }

    // TODO: should we sign-extend here? Is this always a long operation?
    if (is_add) {
      this->regs.set_ccr_flags_integer_add(this->regs.a[dest], mem_value, SIZE_LONG);
      this->regs.a[dest] += mem_value;
    } else {
      this->regs.set_ccr_flags_integer_subtract(this->regs.a[dest], mem_value, SIZE_LONG);
      this->regs.a[dest] -= mem_value;
    }
    this->regs.set_ccr_flags(this->regs.sr & 0x01, -1, -1, -1, -1);
    return;
  }

  // add.S/sub.S DREG, ADDR
  // add.S/sub.S ADDR, DREG
  uint8_t size = opmode & 3;
  auto addr = this->resolve_address(M, Xn, size);
  uint32_t mem_value = this->read(addr, size);
  uint32_t reg_value = this->read({dest, ResolvedAddress::Location::D_REGISTER}, size);
  if (opmode & 4) {
    if (is_add) {
      this->regs.set_ccr_flags_integer_add(mem_value, reg_value, size);
      mem_value += reg_value;
    } else {
      this->regs.set_ccr_flags_integer_subtract(mem_value, reg_value, size);
      mem_value -= reg_value;
    }
    this->write(addr, mem_value, size);
  } else {
    if (is_add) {
      this->regs.set_ccr_flags_integer_add(reg_value, mem_value, size);
      reg_value += mem_value;
    } else {
      this->regs.set_ccr_flags_integer_subtract(reg_value, mem_value, size);
      reg_value -= mem_value;
    }
    this->write({dest, ResolvedAddress::Location::D_REGISTER}, reg_value, size);
  }
  this->regs.set_ccr_flags(this->regs.sr & 0x01, -1, -1, -1, -1);
}

string M68KEmulator::dasm_9D(DisassemblyState& s) {
  uint16_t op = s.r.get_u16b();
  const char* op_name = ((op & 0xF000) == 0x9000) ? "sub" : "add";

  uint8_t dest = op_get_a(op);
  uint8_t opmode = op_get_b(op);
  uint8_t M = op_get_c(op);
  uint8_t Xn = op_get_d(op);

  if (((M & 6) == 0) && (opmode & 4) && (opmode != 7)) {
    char ch = char_for_size.at(opmode & 3);
    if (M) {
      return string_printf("%sx.%c     -[A%hhu], -[A%hhu]", op_name, ch, dest, Xn);
    } else {
      return string_printf("%sx.%c     D%hhu, D%hhu", op_name, ch, dest, Xn);
    }
  }

  if ((opmode & 3) == 3) {
    if (opmode & 4) {
      string ea_dasm = M68KEmulator::dasm_address(s, M, Xn, ValueType::LONG);
      return string_printf("%s.l      A%hhu, %s", op_name, dest, ea_dasm.c_str());
    } else {
      string ea_dasm = M68KEmulator::dasm_address(s, M, Xn, ValueType::WORD);
      return string_printf("%s.w      A%hhu, %s", op_name, dest, ea_dasm.c_str());
    }
  }

  string ea_dasm = M68KEmulator::dasm_address(
      s, M, Xn, value_type_for_size.at(opmode & 3));
  char ch = char_for_size.at(opmode & 3);
  if (opmode & 4) {
    return string_printf("%s.%c      %s, D%hhu", op_name, ch, ea_dasm.c_str(), dest);
  } else {
    return string_printf("%s.%c      D%hhu, %s", op_name, ch, dest, ea_dasm.c_str());
  }
}

void M68KEmulator::exec_A(uint16_t opcode) {
  if (this->syscall_handler) {
    this->syscall_handler(*this, opcode);
  } else {
    this->exec_unimplemented(opcode);
  }
}

string M68KEmulator::dasm_A(DisassemblyState& s) {
  uint16_t op = s.r.get_u16b();

  if (s.is_mac_environment) {
    uint16_t syscall_number;
    bool auto_pop = false;
    uint8_t flags = 0;
    if (op & 0x0800) {
      syscall_number = op & 0x0BFF;
      auto_pop = op & 0x0400;
    } else {
      syscall_number = op & 0xFF;
      flags = (op >> 8) & 7;
    }

    string ret = "syscall    ";
    const auto* syscall_info = info_for_68k_trap(syscall_number, flags);
    if (syscall_info) {
      ret += syscall_info->name;
    } else {
      ret += string_printf("0x%03hX", syscall_number);
    }

    if (flags) {
      ret += string_printf(", flags=%hhu", flags);
    }

    if (auto_pop) {
      ret += ", auto_pop";
    }

    return ret;

  } else { // Not Mac environment
    return string_printf(".invalid   0x%04hX", op);
  }
}

void M68KEmulator::exec_B(uint16_t opcode) {
  uint8_t dest = op_get_a(opcode);
  uint8_t opmode = op_get_b(opcode);
  uint8_t M = op_get_c(opcode);
  uint8_t Xn = op_get_d(opcode);

  int32_t left_value, right_value;
  uint8_t size;
  if (opmode < 3) { // cmp.S DREG, ADDR
    size = opmode;

    left_value = this->regs.d[dest].u;
    if (size == SIZE_BYTE) {
      left_value &= 0x000000FF;
    } else if (size == SIZE_WORD) {
      left_value &= 0x0000FFFF;
    }

    auto addr = this->resolve_address(M, Xn, size);
    right_value = this->read(addr, size);

  } else if ((opmode & 3) == 3) { // cmpa.S AREG, ADDR
    size = (opmode & 4) ? SIZE_LONG : SIZE_WORD;

    left_value = this->regs.a[dest];

    auto addr = this->resolve_address(M, Xn, size);
    right_value = this->read(addr, size);

  } else { // probably xor
    throw runtime_error("unimplemented: opcode B");
  }

  this->regs.set_ccr_flags_integer_subtract(left_value, right_value, size);
}

string M68KEmulator::dasm_B(DisassemblyState& s) {
  uint16_t op = s.r.get_u16b();
  uint8_t dest = op_get_a(op);
  uint8_t opmode = op_get_b(op);
  uint8_t M = op_get_c(op);
  uint8_t Xn = op_get_d(op);

  if ((opmode & 4) && (opmode != 7) && (M == 1)) {
    return string_printf("cmpm.%c     [A%hhu]+, [A%hhu]+",
        char_for_size.at(opmode & 3), dest, Xn);
  }

  if (opmode < 3) {
    string ea_dasm = M68KEmulator::dasm_address(
        s, M, Xn, value_type_for_size.at(opmode));
    return string_printf("cmp.%c      D%hhu, %s", char_for_size.at(opmode),
        dest, ea_dasm.c_str());
  }

  if ((opmode & 3) == 3) {
    if (opmode & 4) {
      string ea_dasm = M68KEmulator::dasm_address(s, M, Xn, ValueType::LONG);
      return string_printf("cmpa.l     A%hhu, %s", dest, ea_dasm.c_str());
    } else {
      string ea_dasm = M68KEmulator::dasm_address(s, M, Xn, ValueType::WORD);
      return string_printf("cmpa.w     A%hhu, %s", dest, ea_dasm.c_str());
    }
  }

  string ea_dasm = M68KEmulator::dasm_address(
      s, M, Xn, value_type_for_size.at(opmode & 3));
  return string_printf("xor.%c      %s, D%hhu", char_for_size.at(opmode & 3),
      ea_dasm.c_str(), dest);
}

void M68KEmulator::exec_C(uint16_t opcode) {
  uint8_t a = op_get_a(opcode);
  uint8_t b = op_get_b(opcode);
  uint8_t c = op_get_c(opcode);
  uint8_t d = op_get_d(opcode);
  uint8_t size = b & 3;

  if (b < 3) { // and.S DREG, ADDR
    auto addr = this->resolve_address(c, d, size);
    ResolvedAddress reg = {a, ResolvedAddress::Location::D_REGISTER};
    uint32_t value = this->read(addr, size) & this->read(reg, size);
    this->write(reg, value, size);
    this->regs.set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);

  } else if (b == 3) { // mulu.w DREG, ADDR (word * word = long form)
    auto addr = this->resolve_address(c, d, SIZE_WORD);
    uint32_t left = this->regs.d[a].u & 0x0000FFFF;
    uint32_t right = this->read(addr, SIZE_WORD);
    this->regs.d[a].u = left * right;

  } else if (b == 4) {
    if (c == 0) { // abcd DREG, DREG
      throw runtime_error("unimplemented: abcd DREG, DREG");

    } else if (c == 1) { // abcd -[AREG], -[AREG]
      throw runtime_error("unimplemented: abcd -[AREG], -[AREG]");

    } else { // and.S ADDR, DREG
      auto addr = this->resolve_address(c, d, size);
      ResolvedAddress reg = {a, ResolvedAddress::Location::D_REGISTER};
      uint32_t value = this->read(addr, size) & this->read(reg, size);
      this->write(addr, value, size);
      this->regs.set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
    }

  } else if (b == 5) {
    if (c == 0) { // exg DREG, DREG
      uint32_t tmp = this->regs.d[a].u;
      this->regs.d[a].u = this->regs.d[d].u;
      this->regs.d[d].u = tmp;
      // Note: ccr not affected

    } else if (c == 1) { // exg AREG, AREG
      uint32_t tmp = this->regs.a[a];
      this->regs.a[a] = this->regs.a[d];
      this->regs.a[d] = tmp;
      // Note: ccr not affected

    } else { // and.S ADDR, DREG
      auto addr = this->resolve_address(c, d, size);
      ResolvedAddress reg = {a, ResolvedAddress::Location::D_REGISTER};
      uint32_t value = this->read(addr, size) & this->read(reg, size);
      this->write(addr, value, size);
      this->regs.set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
    }

  } else if (b == 6) {
    if (c == 1) { // exg DREG, AREG
      uint32_t tmp = this->regs.a[d];
      this->regs.a[d] = this->regs.d[a].u;
      this->regs.d[a].u = tmp;
      // Note: ccr not affected

    } else { // and.S ADDR, DREG
      auto addr = this->resolve_address(c, d, size);
      ResolvedAddress reg = {a, ResolvedAddress::Location::D_REGISTER};
      uint32_t value = this->read(addr, size) & this->read(reg, size);
      this->write(addr, value, size);
      this->regs.set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
    }

  } else if (b == 7) { // muls DREG, ADDR (word * word = long form)
    // I'm too lazy to figure out the sign-extension right now
    throw runtime_error("unimplemented: muls DREG, ADDR (word * word = long form)");
  }
}

string M68KEmulator::dasm_C(DisassemblyState& s) {
  uint16_t op = s.r.get_u16b();
  uint8_t a = op_get_a(op);
  uint8_t b = op_get_b(op);
  uint8_t c = op_get_c(op);
  uint8_t d = op_get_d(op);

  if (b < 3) { // and.S DREG, ADDR
    string ea_dasm = M68KEmulator::dasm_address(
        s, c, d, value_type_for_size.at(b));
    return string_printf("and.%c      D%hhu, %s", char_for_size.at(b), a,
        ea_dasm.c_str());

  } else if (b == 3) { // mulu.w DREG, ADDR (word * word = long form)
    string ea_dasm = M68KEmulator::dasm_address(s, c, d, ValueType::WORD);
    return string_printf("mulu.w     D%hhu, %s", a, ea_dasm.c_str());

  } else if (b == 4) {
    if (c == 0) { // abcd DREG, DREG
      return string_printf("abcd       D%hhu, D%hhu", a, d);
    } else if (c == 1) { // abcd -[AREG], -[AREG]
      return string_printf("abcd       -[A%hhu], -[A%hhu]", a, d);
    } else { // and.S ADDR, DREG
      string ea_dasm = M68KEmulator::dasm_address(s, c, d, ValueType::BYTE);
      return string_printf("and.b      %s, D%hhu", ea_dasm.c_str(), a);
    }

  } else if (b == 5) {
    if (c == 0) { // exg DREG, DREG
      return string_printf("exg        D%hhu, D%hhu", a, d);
    } else if (c == 1) { // exg AREG, AREG
      return string_printf("exg        A%hhu, A%hhu", a, d);
    } else { // and.S ADDR, DREG
      string ea_dasm = M68KEmulator::dasm_address(s, c, d, ValueType::WORD);
      return string_printf("and.w      %s, D%hhu", ea_dasm.c_str(), a);
    }

  } else if (b == 6) {
    if (c == 1) { // exg DREG, AREG
      return string_printf("exg        D%hhu, A%hhu", a, d);
    } else { // and.S ADDR, DREG
      string ea_dasm = M68KEmulator::dasm_address(s, c, d, ValueType::LONG);
      return string_printf("and.l      %s, D%hhu", ea_dasm.c_str(), a);
    }

  } else if (b == 7) { // muls DREG, ADDR (word * word = long form)
    string ea_dasm = M68KEmulator::dasm_address(s, c, d, ValueType::WORD);
    return string_printf("muls.w     D%hhu, %s", a, ea_dasm.c_str());
  }

  // This should be impossible; we covered all possible values for b and all
  // branches unconditionally return
  throw logic_error("no cases matched for 1100bbb opcode");
}

void M68KEmulator::exec_E(uint16_t opcode) {
  uint8_t size = op_get_size(opcode);
  uint8_t Xn = op_get_d(opcode);
  if (size == 3) {
    uint8_t which = (opcode >> 8) & 0x0F;
    switch (which) {
      case 0xB: // bfexts
      case 0x9: { // bfextu
        bool is_signed = which & 2;
        uint16_t options = this->fetch_instruction_word();
        auto source = this->resolve_address(op_get_c(opcode), op_get_d(opcode), SIZE_LONG);
        uint8_t dest_reg = op_get_i(options) & 7;
        int32_t offset = (options >> 6) & 0x1F;
        uint32_t width = options & 0x1F;
        if (options & 0x0800) { // offset is a D reg
          offset = this->regs.d[offset & 7].s;
        }
        if (options & 0x0020) {
          width = this->regs.d[width & 7].u & 0x1F;
        }
        if (width == 0) {
          width = 32;
        }

        if (source.location != ResolvedAddress::Location::MEMORY) {
          throw runtime_error("unimplemented bfextu from register");
        }
        if (offset < 0) {
          throw runtime_error("unimplemented bfextu with negative offset");
        }

        uint32_t start_addr = source.addr + (offset >> 3);
        uint8_t bit_offset = offset & 7;
        const void* data = this->mem->at<void>(start_addr, (bit_offset + width + 7) / 8);

        BitReader r(data, bit_offset + width);
        r.skip(bit_offset);
        this->regs.d[dest_reg].u = r.read(width);

        if (is_signed && (this->regs.d[dest_reg].u & (1 << width))) {
          this->regs.d[dest_reg].u |= (0xFFFFFFFF << width);
        }
        break;
      }

      case 0x0: // asl
      case 0x1: // asr
      case 0x2: // lsl
      case 0x3: // lsr
      case 0x4: // roxl
      case 0x5: // roxr
      case 0x6: // rol
      case 0x7: // ror
      case 0x8: // bftst
      case 0xA: // bfchg
      case 0xC: // bfclr
      case 0xD: // bfffo
      case 0xE: // bfset
      case 0xF: // bfins
      default:
        throw runtime_error(string_printf(
            "unimplemented (E; s=3; which=%hhX)", which));
    }
    return;
  }

  uint8_t c = op_get_c(opcode);
  bool shift_is_reg = (c & 4);
  uint8_t a = op_get_a(opcode);
  uint8_t k = ((c & 3) << 1) | op_get_g(opcode);

  uint8_t shift_amount;
  if (shift_is_reg) {
    if (size == SIZE_BYTE) {
      shift_amount = this->regs.d[a].u & 0x00000007;
    } else if (size == SIZE_WORD) {
      shift_amount = this->regs.d[a].u & 0x0000000F;
    } else {
      shift_amount = this->regs.d[a].u & 0x0000001F;
    }
  } else {
    shift_amount = (a == 0) ? 8 : a;
    if (shift_amount == 8 && size == SIZE_BYTE) {
      throw runtime_error("unimplemented: shift opcode with size=byte and shift=8");
    }
  }

  switch (k) {
    case 0x00: // asr DREG, COUNT/REG
    case 0x01: // asl DREG, COUNT/REG
    case 0x02: // lsr DREG, COUNT/REG
    case 0x03: // lsl DREG, COUNT/REG
    case 0x04: // roxr DREG, COUNT/REG
    case 0x05: // roxl DREG, COUNT/REG
    case 0x06: // ror DREG, COUNT/REG
    case 0x07: { // rol DREG, COUNT/REG
      bool left_shift = (k & 1);
      bool logical_shift = (k & 2);
      bool rotate = (k & 4);

      this->regs.sr &= 0xFFE0;
      if (shift_amount == 0) {
        this->regs.set_ccr_flags(-1, is_negative(this->regs.d[Xn].u, SIZE_LONG),
            (this->regs.d[Xn].u == 0), 0, 0);

      } else if (size == SIZE_BYTE) {
        uint8_t& target = *reinterpret_cast<uint8_t*>(&this->regs.d[Xn].u);

        int8_t last_shifted_bit = (left_shift ? (target & (1 << (8 - shift_amount))) : (target & (1 << (shift_amount - 1))));

        bool msb_changed;
        if (!rotate && logical_shift && left_shift) {
          uint32_t msb_values = (target >> (8 - shift_amount));
          uint32_t mask = (1 << shift_amount) - 1;
          msb_values &= mask;
          msb_changed = ((msb_values == mask) || (msb_values == 0));
        } else {
          msb_changed = false;
        }

        if (rotate) {
          if (logical_shift) { // rotate without extend (rol, ror)
            if (left_shift) {
              target = (target << shift_amount) | (target >> (8 - shift_amount));
            } else {
              target = (target >> shift_amount) | (target << (8 - shift_amount));
            }
            last_shifted_bit = -1; // X unaffected for these opcodes

          } else { // rotate with extend (roxl, roxr) (TODO)
            throw runtime_error("unimplemented: roxl/roxr DREG, COUNT/REG");
          }

        } else {
          if (logical_shift) {
            if (left_shift) {
              target <<= shift_amount;
            } else {
              target >>= shift_amount;
            }
          } else {
            int8_t& signed_target = *reinterpret_cast<int8_t*>(&this->regs.d[Xn].u);
            if (left_shift) {
              signed_target <<= shift_amount;
            } else {
              signed_target >>= shift_amount;
            }
          }
        }

        this->regs.set_ccr_flags(last_shifted_bit, (target & 0x80), (target == 0),
            msb_changed, last_shifted_bit);

      } else if (size == SIZE_WORD) {
        uint16_t& target = *reinterpret_cast<uint16_t*>(&this->regs.d[Xn].u);

        int8_t last_shifted_bit = (left_shift ? (target & (1 << (16 - shift_amount))) : (target & (1 << (shift_amount - 1))));

        bool msb_changed;
        if (!rotate && logical_shift && left_shift) {
          uint32_t msb_values = (target >> (16 - shift_amount));
          uint32_t mask = (1 << shift_amount) - 1;
          msb_values &= mask;
          msb_changed = ((msb_values == mask) || (msb_values == 0));
        } else {
          msb_changed = false;
        }

        if (rotate) {
          if (logical_shift) { // rotate without extend (rol, ror)
            if (left_shift) {
              target = (target << shift_amount) | (target >> (16 - shift_amount));
            } else {
              target = (target >> shift_amount) | (target << (16 - shift_amount));
            }
            last_shifted_bit = -1; // X unaffected for these opcodes

          } else { // rotate with extend (roxl, roxr) (TODO)
            throw runtime_error("unimplemented: roxl/roxr DREG, COUNT/REG");
          }

        } else {
          if (logical_shift) {
            if (left_shift) {
              target <<= shift_amount;
            } else {
              target >>= shift_amount;
            }
          } else {
            int16_t& signed_target = *reinterpret_cast<int16_t*>(&this->regs.d[Xn].u);
            if (left_shift) {
              signed_target <<= shift_amount;
            } else {
              signed_target >>= shift_amount;
            }
          }
        }

        this->regs.set_ccr_flags(last_shifted_bit, (target & 0x8000), (target == 0),
            msb_changed, last_shifted_bit);

      } else if (size == SIZE_LONG) {
        uint32_t& target = this->regs.d[Xn].u;

        int8_t last_shifted_bit = (left_shift ? (target & (1 << (32 - shift_amount))) : (target & (1 << (shift_amount - 1))));

        bool msb_changed;
        if (!rotate && logical_shift && left_shift) {
          uint32_t msb_values = (target >> (32 - shift_amount));
          uint32_t mask = (1 << shift_amount) - 1;
          msb_values &= mask;
          msb_changed = ((msb_values == mask) || (msb_values == 0));
        } else {
          msb_changed = false;
        }

        if (rotate) {
          if (logical_shift) { // rotate without extend (rol, ror)
            if (left_shift) {
              target = (target << shift_amount) | (target >> (32 - shift_amount));
            } else {
              target = (target >> shift_amount) | (target << (32 - shift_amount));
            }
            last_shifted_bit = -1; // X unaffected for these opcodes

          } else { // rotate with extend (roxl, roxr) (TODO)
            throw runtime_error("unimplemented: roxl/roxr DREG, COUNT/REG");
          }

        } else {
          if (logical_shift) {
            if (left_shift) {
              target <<= shift_amount;
            } else {
              target >>= shift_amount;
            }
          } else {
            int32_t& signed_target = *reinterpret_cast<int32_t*>(&this->regs.d[Xn].u);
            if (left_shift) {
              signed_target <<= shift_amount;
            } else {
              signed_target >>= shift_amount;
            }
          }
        }

        this->regs.set_ccr_flags(last_shifted_bit, (target & 0x80000000),
            (target == 0), msb_changed, last_shifted_bit);

      } else {
        throw runtime_error("invalid size for bit shift operation");
      }
      break;
    }

    case 0x08: // bftst
    case 0x09: // bfextu
    case 0x0A: // bfchg
    case 0x0B: // bfexts
    case 0x0C: // bfclr
    case 0x0D: // bfffo
    case 0x0E: // bfset
    case 0x0F: // bfins
    default:
      throw runtime_error("unimplemented: opcode E+k");
  }
}

string M68KEmulator::dasm_E(DisassemblyState& s) {
  uint16_t op = s.r.get_u16b();

  static const vector<const char*> op_names = {
      "asr   ", "asl   ", "lsr   ", "lsl   ", "roxr  ", "roxl  ", "ror   ", "rol   ",
      "bftst ", "bfextu", "bfchg ", "bfexts", "bfclr ", "bfffo ", "bfset ", "bfins "};

  uint8_t size = op_get_size(op);
  uint8_t Xn = op_get_d(op);
  if (size == 3) {
    uint8_t M = op_get_c(op);
    uint8_t k = op_get_k(op);
    const char* op_name = op_names[k];

    if (k & 8) {
      uint16_t ext = s.r.get_u16b();
      string ea_dasm = M68KEmulator::dasm_address(s, M, Xn, ValueType::LONG);
      string offset_str = (ext & 0x0800) ? string_printf("D%u", (ext & 0x01C0) >> 6) : string_printf("%u", (ext & 0x07C0) >> 6);
      // If immediate, 0 in the width field means 32
      string width_str;
      if ((ext & 0x003F) == 0x0000) {
        width_str = "32";
      } else {
        width_str = (ext & 0x0020) ? string_printf("D%u", (ext & 0x0007))
                                   : string_printf("%u", (ext & 0x001F));
      }

      if (k & 1) {
        uint8_t Dn = (ext >> 12) & 7;
        // bfins reads data from Dn; all the others write to Dn
        if (k == 0x0F) {
          return string_printf("%s     %s {%s:%s}, D%hhu", op_name,
              ea_dasm.c_str(), offset_str.c_str(), width_str.c_str(), Dn);
        } else {
          return string_printf("%s     D%hhu, %s {%s:%s}", op_name, Dn,
              ea_dasm.c_str(), offset_str.c_str(), width_str.c_str());
        }
      } else {
        return string_printf("%s     %s {%s:%s}", op_name, ea_dasm.c_str(),
            offset_str.c_str(), width_str.c_str());
      }
    }
    string ea_dasm = M68KEmulator::dasm_address(s, M, Xn, ValueType::WORD);
    return string_printf("%s.w   %s", op_name, ea_dasm.c_str());
  }

  uint8_t c = op_get_c(op);
  bool shift_is_reg = (c & 4);
  uint8_t a = op_get_a(op);
  uint8_t k = ((c & 3) << 1) | op_get_g(op);
  const char* op_name = op_names[k];

  string dest_reg_str;
  if (size == SIZE_BYTE) {
    dest_reg_str = string_printf("D%hhu.b", Xn);
  } else if (size == SIZE_WORD) {
    dest_reg_str = string_printf("D%hhu.w", Xn);
  } else if (size == SIZE_LONG) {
    dest_reg_str = string_printf("D%hhu", Xn);
  } else {
    dest_reg_str = string_printf("D%hhu.?", Xn);
  }

  if (shift_is_reg) {
    return string_printf("%s     %s, D%hhu", op_name, dest_reg_str.c_str(), a);
  } else {
    if (!a) {
      a = 8;
    }
    return string_printf("%s     %s, %hhu", op_name, dest_reg_str.c_str(), a);
  }
}

void M68KEmulator::exec_F(uint16_t opcode) {
  // TODO: Implement floating-point opcodes here
  if (this->syscall_handler) {
    this->syscall_handler(*this, opcode);
  } else {
    this->exec_unimplemented(opcode);
  }
}

string M68KEmulator::dasm_F(DisassemblyState& s) {
  uint16_t opcode = s.r.get_u16b();
  uint8_t w = op_get_a(opcode);
  uint8_t subop = op_get_b(opcode);
  uint8_t M = op_get_c(opcode);
  uint8_t Xn = op_get_d(opcode);

  if ((w == 2) && !(subop & 4)) {
    // cinv         11110100HH0DDRRR
    // cpush        11110100HH1DDRRR

    string ret = (M & 4) ? "cpush" : "cinv";
    switch (M & 3) {
      case 0:
        return ".invalid   <<cinv/cpush with scope=0>>";
      case 1:
        ret += 'l';
        break;
      case 2:
        ret += 'p';
        break;
      case 3:
        ret += 'a';
        break;
    }
    ret.resize(11, ' ');

    static const array<const char*, 4> caches({"NONE", "DATA", "INST", "DATA+INST"});
    ret += caches[subop & 3];
    if ((M & 3) != 3) {
      ret += string_printf(", [A%hhu]", Xn);
    }
    return ret;
  }

  // Field definitions for descriptions of these opcodes' bits:
  // A = ?
  // B = K-factor
  // C = FC
  // D = scope
  // E = opmode
  // F = F/D
  // G = R/M
  // H = cache
  // I = imm
  // J = coprocessor-dependent command or data
  // K = mask
  // L = level
  // M = mode
  // N = num
  // P = ACX/TT reg
  // R = A/D reg
  // S = size
  // U = source specifier
  // V = d/r
  // W = coprocessor ID
  // X = condition
  // Y = displacement or address (e.g. for move16)
  // Z = R/W

  switch (subop) {
    case 0: {
      uint16_t args = s.r.get_u16b();
      if (w == 0) {
        // TODO: ValueType::LONG is not always correct here; the size depends on
        // which register is being read/written. See the PMOVE page in the
        // programmer's manual (paragraph 3).
        string ea_dasm = M68KEmulator::dasm_address(s, M, Xn, ValueType::LONG);
        switch ((args >> 13) & 7) {
          case 0: {
            // pmove        1111000000MMMRRR 000PPPZF00000000
            uint8_t mmu_reg = (args >> 10) & 7;
            bool to_mmu_reg = (args >> 9) & 1;
            bool skip_flush = (args >> 8) & 1;
            string ret = skip_flush ? "pmovefd" : "pmove";
            ret.resize(11, ' ');
            if (to_mmu_reg) {
              ret += string_printf("MR%hhu, %s", mmu_reg, ea_dasm.c_str());
            } else {
              ret += string_printf("%s, MR%hhu", ea_dasm.c_str(), mmu_reg);
            }
            return ret;
          }
          case 1: {
            uint8_t op_mode = (args >> 10) & 7;
            if (op_mode == 0) {
              // pload        1111000000MMMRRR 001000Z0000CCCCC
              bool is_read = (args >> 9) & 1;
              // TODO: function_code has different meanings for different
              // processors, unfortunately, so we can't disassemble it in a
              // uniform way. Find a reasonable way to disassemble it.
              uint8_t function_code = args & 0x1F;
              return string_printf("pload%c     0x%02hhX, %s", is_read ? 'r' : 'w', function_code, ea_dasm.c_str());

            } else if (op_mode == 2) {
              // pvalid       1111000000MMMRRR 0010100000000000
              // pvalid       1111000000MMMRRR 0010100000000RRR
              // TODO: How are we supposed to be able to tell these forms apart?
              // Can you just not use A0 with this opcode, or what?
              uint8_t reg = op_get_d(args);
              if (reg == 0) {
                return string_printf("pvalid     VAL, %s", ea_dasm.c_str());
              } else {
                return string_printf("pvalid     A%hhu, %s", reg, ea_dasm.c_str());
              }

            } else {
              // TODO: pflush       1111000000MMMRRR 001MMM00KKKCCCCC
              // TODO: pflush(a/s)  1111000000MMMRRR 001MMM0KKKKCCCCC
              return string_printf(".pflush    0x%04hX, 0x%04hX // unimplemented",
                  opcode, args);
            }
            break;
          }
          case 2:
            // TODO: pmove        1111000000MMMRRR 010PPPZ000000000
            // TODO: pmove        1111000000MMMRRR 010PPPZF00000000
            return string_printf(".pmove2    0x%04hX, 0x%04hX // unimplemented",
                opcode, args);
          case 3:
            // TODO: pmove        1111000000MMMRRR 011000Z000000000
            // TODO: pmove        1111000000MMMRRR 011PPPZ000000000
            // TODO: pmove        1111000000MMMRRR 011PPPZ0000NNN00
            return string_printf(".pmove3    0x%04hX, 0x%04hX // unimplemented",
                opcode, args);
          case 4:
            // TODO: ptest        1111000000MMMRRR 100000Z0RRRCCCCC
            // TODO: ptest        1111000000MMMRRR 100LLLZARRCCCCCC
            // TODO: ptest        1111000000MMMRRR 100LLLZRRRCCCCCC
            return string_printf(".ptest     0x%04hX, 0x%04hX // unimplemented",
                opcode, args);
          case 5:
            // pflushr      1111000000MMMRRR 1010000000000000
            // TODO: ValueType::DOUBLE is sort of wrong here; the actual type is
            // just 64 bits (but is not a float).
            return "pflushr    " + M68KEmulator::dasm_address(s, M, Xn, ValueType::DOUBLE);

          default:
            return string_printf(".invalid   0x%04hX, 0x%04hX // unimplemented",
                opcode, args);
        }
      } else if (w == 1) {
        if (args & 0x8000) {
          if ((args & 0xC700) == 0xC000) {
            // TODO: fmovem       1111WWW000MMMRRR 11VEE000KKKKKKKK
            return string_printf(".fmovem    0x%04hX, 0x%04hX // unimplemented",
                opcode, args);
          } else if ((args & 0xC300) == 0x8000) {
            // TODO: fmove        1111WWW000MMMRRR 10VRRR0000000000
            // TODO: fmovem       1111WWW000MMMRRR 10VRRR0000000000
            return string_printf(".fmove(m)  0x%04hX, 0x%04hX // unimplemented",
                opcode, args);
          } else {
            // TODO: cpgen        1111WWW000MMMRRR JJJJJJJJJJJJJJJJ [...]
            return string_printf(".cpgen     0x%04hX, 0x%04hX // unimplemented",
                opcode, args);
          }
        }
        bool rm = (args >> 14) & 1;
        bool is_fmove_to_mem = ((args >> 13) & 1);
        uint8_t u = (args >> 10) & 7;
        uint8_t dest_reg = (args >> 7) & 7;
        uint8_t mode = args & 0x7F;
        if ((u == 7) && !is_fmove_to_mem) {
          // TODO: fmovecr      1111WWW000000000 010111RRRYYYYYYY
          return string_printf(".fmovecr   0x%04hX, 0x%04hX // unimplemented",
              opcode, args);
        }

        string source_str;
        if (rm) {
          ValueType type = static_cast<ValueType>(u);
          string ea_dasm = M68KEmulator::dasm_address(s, M, Xn, type);
          source_str = string_printf("(%s) %s", name_for_value_type.at(u),
              ea_dasm.c_str());
        } else {
          source_str = string_printf("fp%hhu", u);
        }

        if (is_fmove_to_mem) {
          if (!rm) {
            return string_printf(".invalid   fmove, !rm");
          }
          // fmove        1111001000MMMRRR 011UUURRRBBBBBBB
          return string_printf("fmove      %s, fp%u", source_str.c_str(), dest_reg);
        }

        // (many opcodes)      1111WWW000MMMRRR 0G0UUURRR0011111

        if ((mode & 0x78) == 0x30) {
          return string_printf("fsincos    fp%u /*cos*/, fp%hhu /*sin*/, %s",
              mode & 7, dest_reg, source_str.c_str());
        } else {
          static const array<const char*, 0x80> opcode_names = {
              // clang-format off
              // 0x00
              "fmove", "fint", "fsinh", "fintrz", "fsqrt", ".invalid", "flognp1", ".invalid",
              // 0x08
              "fetoxm1", "ftanh", "fatan", ".invalid", "fasin", "fatanh", "fsin", "ftan",
              // 0x10
              "fetox", "ftwotox", "ftentox", ".invalid", "flogn", "flog10", "flog2", ".invalid",
              // 0x18
              "fabs", "fcosh", "fneg", ".invalid", "facos", "fcos", "fgetexp", "fgetman",
              // 0x20
              "fdiv", "fmod", "fadd", "fmul", "fsgldiv", "frem", "fscale", "fsglmul",
              // 0x28
              "fsub", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid",
              // 0x30 (these should have been handled above already)
              nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
              // 0x38
              "fcmp", ".invalid", "ftst", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid",
              // 0x40
              "fsmove", "fssqrt", ".invalid", ".invalid", "fdmove", "fdsqrt", ".invalid", ".invalid",
              // 0x48
              ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid",
              // 0x50
              ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid",
              // 0x58
              "fsabs", ".invalid", "fsneg", ".invalid", "fdabs", ".invalid", "fdneg", ".invalid",
              // 0x60
              "fsdiv", ".invalid", "fsadd", "fsmul", "fddiv", ".invalid", "fdadd", "fdmul",
              // 0x68
              "fssub", ".invalid", ".invalid", ".invalid", "fdsub", ".invalid", ".invalid", ".invalid",
              // 0x70
              ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid",
              // 0x78
              ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid", ".invalid",
              // clang-format on
          };
          string ret = opcode_names.at(mode);
          ret.resize(11, ' ');
          ret += string_printf("fp%hhu, %s", dest_reg, source_str.c_str());
          return ret;
        }

      } else if (w == 3) {
        // TODO: move16       11110110000EERRR YYYYYYYYYYYYYYYY YYYYYYYYYYYYYYYY
        // TODO: move16       1111011000100RRR 1RRR000000000000
        return string_printf(".move16    0x%04hX, 0x%04hX // unimplemented",
            opcode, args);
      } else if (w == 4) {
        // TODO: tblu/tblun   1111100000MMMRRR 0RRR0?01S0000000
        // TODO: tbls/tblsn   1111100000MMMRRR 0RRR1?01SS000000
        // TODO: tblu/tblun   1111100000000RRR 0RRR0?00SS000RRR
        // TODO: tbls/tblsn   1111100000000RRR 0RRR1?00SS000RRR
        // TODO: lpstop       1111100000000000 0000000111000000 IIIIIIIIIIIIIIII
        return string_printf(".tblXX     0x%04hX, 0x%04hX // unimplemented",
            opcode, args);
      } else {
        return string_printf(".unknown   0x%04hX 0x%04hX (W = %hhu)", opcode, args, w);
      }
    }
    case 1: {
      uint16_t args = s.r.get_u16b();
      // TODO: pscc         1111000001MMMRRR 0000000000XXXXXX
      // TODO: pdbcc        1111000001001RRR 0000000000XXXXXX YYYYYYYYYYYYYYYY
      // TODO: ptrapcc      1111000001111EEE 0000000000XXXXXX [YYYYYYYYYYYYYYYY [YYYYYYYYYYYYYYYY]]
      // TODO: fscc         1111WWW001MMMRRR 0000000000XXXX??
      // TODO: cpscc        1111WWW001MMMRRR 0000000000XXXXXX [...]
      // TODO: fdbcc        1111WWW001001RRR 0000000000XXXXXX YYYYYYYYYYYYYYYY
      // TODO: cpdbcc       1111WWW001001RRR 0000000000XXXXXX YYYYYYYYYYYYYYYY
      // TODO: ftrapcc      1111WWW001111EEE 0000000000XXXXXX [YYYYYYYYYYYYYYYY [YYYYYYYYYYYYYYYY]]
      // TODO: cptrapcc     1111WWW0011111EE 0000000000XXXXXX [JJJJJJJJJJJJJJJJ ...]
      return string_printf(".extension 0x%03X <<F/1/%hhu>>, 0x%04hX // unimplemented",
          opcode & 0x0FFF, w, args);
    }
    case 2:
    case 3: {
      uint16_t args = s.r.get_u16b();
      if (((opcode & 0xF1FF) == 0xF080) && (args == 0)) {
        // fnop         1111WWW010000000 0000000000000000
        if (w == 1) {
          return "fnop";
        } else {
          return string_printf("fnop       w%hhu", w);
        }
      } else {
        // TODO: pbcc         111100001SXXXXXX YYYYYYYYYYYYYYYY [YYYYYYYYYYYYYYYY]
        // TODO: fbcc         1111WWW01SXXXXXX YYYYYYYYYYYYYYYY [YYYYYYYYYYYYYYYY]
        // TODO: cpbcc        1111WWW01SXXXXXX JJJJJJJJJJJJJJJJ [...] YYYYYYYYYYYYYYYY [YYYYYYYYYYYYYYYY]
      }
      return string_printf(".extension 0x%03X <<F/2-3/%hhu>> // unimplemented", opcode & 0x0FFF, w);
    }
    case 4:
    case 5:
      // TODO: psave        1111000100MMMRRR
      // TODO: prestore     1111000101MMMRRR
      // TODO: pflush       11110101000EERRR
      // TODO: ptest        1111010101Z01RRR
      // TODO: cpsave       1111WWW100MMMRRR
      // TODO: cprestore    1111WWW101MMMRRR
      // TODO: fsave        1111WWW100MMMRRR
      // TODO: frestore     1111WWW101MMMRRR
      return string_printf(".extension 0x%03X <<F/4-5/%hhu>> // unimplemented", opcode & 0x0FFF, w);
    default:
      return string_printf(".invalid   <<F/%hhu/%hhu>>", subop, w);
  }

  throw logic_error("all F-subopcode cases should return");
}

const M68KEmulator::OpcodeImplementation M68KEmulator::fns[0x10] = {
    {&M68KEmulator::exec_0123, &M68KEmulator::dasm_0123},
    {&M68KEmulator::exec_0123, &M68KEmulator::dasm_0123},
    {&M68KEmulator::exec_0123, &M68KEmulator::dasm_0123},
    {&M68KEmulator::exec_0123, &M68KEmulator::dasm_0123},
    {&M68KEmulator::exec_4, &M68KEmulator::dasm_4},
    {&M68KEmulator::exec_5, &M68KEmulator::dasm_5},
    {&M68KEmulator::exec_6, &M68KEmulator::dasm_6},
    {&M68KEmulator::exec_7, &M68KEmulator::dasm_7},
    {&M68KEmulator::exec_8, &M68KEmulator::dasm_8},
    {&M68KEmulator::exec_9D, &M68KEmulator::dasm_9D},
    {&M68KEmulator::exec_A, &M68KEmulator::dasm_A},
    {&M68KEmulator::exec_B, &M68KEmulator::dasm_B},
    {&M68KEmulator::exec_C, &M68KEmulator::dasm_C},
    {&M68KEmulator::exec_9D, &M68KEmulator::dasm_9D},
    {&M68KEmulator::exec_E, &M68KEmulator::dasm_E},
    {&M68KEmulator::exec_F, &M68KEmulator::dasm_F},
};

////////////////////////////////////////////////////////////////////////////////

static bool is_valid_macsbug_symbol_char(char ch) {
  // "Building and Managing Programs in MPW", chapter B-25:
  //
  //    A valid MacsBug symbol consists of _ characters, % characters, spaces,
  //    digits, and uppercase and lowercase letters
  //
  // "Macsbug Reference and Debugging Guide", page 367:
  //
  //    Valid characters for procedure names are a–z, A–Z, 0–9, underscore (_),
  //    percent (%), period (.), and space

  // Do not use 'isalpha' etc. as they take the current locale into account
  return ch == '_' ||
      ch == '%' ||
      ch == '.' ||
      ch == ' ' ||
      (ch >= '0' && ch <= '9') ||
      (ch >= 'A' && ch <= 'Z') ||
      (ch >= 'a' && ch <= 'z');
}

static bool try_decode_macsbug_symbol_part(StringReader& r, string& symbol, uint16_t symbol_length) {
  if (r.remaining() < symbol_length) {
    return false;
  }

  for (uint16_t i = 0; i < symbol_length; ++i) {
    uint8_t ch = r.get_u8();
    if (!is_valid_macsbug_symbol_char(ch)) {
      return false;
    }
    symbol += ch;
  }

  return true;
}

struct DecodedSymbol {
  string symbol;
  uint16_t num_constants;
};

static DecodedSymbol try_decode_macsbug_symbol(StringReader& r) {
  // All indented comments are from "Macsbug Reference and Debugging Guide", page 367,
  // and "Building and Managing Programs in MPW", page B-25f

  if (r.remaining() < 2) {
    return {};
  }

  uint32_t start = r.where();
  uint8_t symbol_0 = r.get_u8();
  uint8_t symbol_1 = r.get_u8();
  uint8_t symbol_0_low7 = symbol_0 & 0x7F;
  uint8_t symbol_1_low7 = symbol_1 & 0x7F;

  //    With fixed-length format, the first byte is in the range $20 through $7F.
  //    The high-order bit may or may not be set.

  string symbol;
  if (symbol_0_low7 >= 0x20 && symbol_0_low7 <= 0x7F) {
    //    The high-order bit of the second byte is set for 16-character names,
    //    clear for 8-character names. Fixed-length 16-character names are used
    //    in object Pascal to show class.method names instead of procedure names.
    //    The method name is contained in the first 8 bytes and the class name is
    //    in the second 8 bytes. MacsBug swaps the order and inserts the period
    //    before displaying the name.

    if (is_valid_macsbug_symbol_char(symbol_0_low7) && is_valid_macsbug_symbol_char(symbol_1_low7)) {
      symbol += symbol_0_low7;
      symbol += symbol_1_low7;

      if (symbol_1 & 0x80) {
        if (try_decode_macsbug_symbol_part(r, symbol, 16 - 2)) {
          return {symbol.substr(8, 8) + "." + symbol.substr(0, 8), 0};
        }
      } else {
        if (try_decode_macsbug_symbol_part(r, symbol, 8 - 2)) {
          return {symbol, 0};
        }
      }
    }
  } else if (symbol_0 >= 0x80 && symbol_0 <= 0x9F) {
    //    With variable-length format, the first byte is in the range $80 to $9F.
    //    Stripping the high-order bit produces a length in the range $00 through
    //    $1F. If the length is 0, the next byte contains the actual length, in
    //    the range $01 through $FF [otherwise the next byte is the name's first
    //    character]. Data after the name starts on a word boundary.

    uint16_t symbol_length = symbol_0_low7;
    bool valid = true;
    if (symbol_length == 0) {
      symbol_length = symbol_1;
    } else if (is_valid_macsbug_symbol_char(symbol_1)) {
      symbol += symbol_1;
      --symbol_length;
    } else {
      valid = false;
    }

    if (valid && try_decode_macsbug_symbol_part(r, symbol, symbol_length)) {
      if (r.where() & 1) {
        //    Data after the name starts on a word boundary.
        r.skip(1);
      }

      //    Compilers can place a procedure’s constant data immediately after
      //    the procedure in memory. The first word after the name specifies
      //    how many bytes of constant data are present. If there are no
      //    constants, a length of 0 must be given.

      uint16_t num_constants = r.get_u16b();
      // TODO: unclear if this necessary, or if the size of the constants is always even
      if (num_constants & 1) {
        ++num_constants;
      }
      return {symbol, num_constants};
    }
  }

  // No MacsBug symbol
  r.go(start);

  return {};
}

M68KEmulator::DisassemblyState::DisassemblyState(
    const void* data,
    size_t size,
    uint32_t start_address,
    bool is_mac_environment,
    const std::vector<JumpTableEntry>* jump_table)
    : r(data, size),
      start_address(start_address),
      opcode_start_address(this->start_address),
      prev_was_return(false),
      is_mac_environment(is_mac_environment),
      jump_table(jump_table) {}

string M68KEmulator::disassemble_one(DisassemblyState& s) {
  size_t opcode_offset = s.r.where();
  string opcode_disassembly;
  if (s.is_mac_environment && s.prev_was_return) {
    auto [symbol, num_constants] = try_decode_macsbug_symbol(s.r);
    if (!symbol.empty()) {
      // We have a MacsBug symbol plus additional constant data
      // TODO: decode type/length of symbol like ResEdit/Resorcerer do?
      opcode_disassembly = string_printf("dc.b       \"%s\"", symbol.c_str());

      if (num_constants > 0) {
        // TODO: disassemble constants instead of skipping them
        opcode_disassembly += string_printf(" + %u constant bytes", num_constants);
        s.r.skip(num_constants);
      }
    }
  }
  s.prev_was_return = false;

  if (opcode_disassembly.empty()) {
    // Didn't decode any MacsBug symbol: disassemble instruction
    s.opcode_start_address = s.start_address + s.r.where();
    try {
      uint8_t fn_index = (s.r.get_u8(false) >> 4) & 0x000F;
      opcode_disassembly = M68KEmulator::fns[fn_index].dasm(s);
    } catch (const out_of_range&) {
      if (s.r.where() == opcode_offset) {
        // There must be at least 1 byte available since r.eof() was false
        s.r.get_u8();
      }
      opcode_disassembly = ".incomplete";
    }
  }

  string line;
  {
    string hex_data;
    size_t end_offset = s.r.where();
    if (end_offset <= opcode_offset) {
      throw logic_error(string_printf(
          "disassembly did not advance; used %zX/%zX bytes",
          s.r.where(), s.r.size()));
    }

    for (s.r.go(opcode_offset); s.r.where() < (end_offset & (~1));) {
      hex_data += string_printf(" %04X", s.r.get_u16b());
    }
    if (end_offset & 1) {
      // This should only happen for .incomplete at the end of the stream
      hex_data += string_printf(" %02X  ", s.r.get_u8());
    }
    if (hex_data.size() > 25) {
      // This should only happen for MacsBug symbols
      hex_data.resize(22);
      hex_data += "...";

    } else {
      while (hex_data.size() < 25) {
        hex_data += "     ";
      }
    }
    line += hex_data;
  }

  line += ' ';
  line += opcode_disassembly;
  return line;
}

string M68KEmulator::disassemble_one(
    const void* vdata,
    size_t size,
    uint32_t start_address,
    bool is_mac_environment,
    const vector<JumpTableEntry>* jump_table) {
  DisassemblyState s(vdata, size, start_address, is_mac_environment, jump_table);
  return M68KEmulator::disassemble_one(s);
}

string M68KEmulator::disassemble(
    const void* vdata,
    size_t size,
    uint32_t start_address,
    const multimap<uint32_t, string>* labels,
    bool is_mac_environment,
    const std::vector<JumpTableEntry>* jump_table) {
  static const multimap<uint32_t, string> empty_labels_map = {};
  if (!labels) {
    labels = &empty_labels_map;
  }

  map<uint32_t, pair<string, uint32_t>> lines; // {pc: (line, next_pc)}

  // Phase 1: Generate the disassembly for each opcode, and collect branch
  // target addresses
  // TODO: Rewrite this to use a queue of pending PCs to disassemble instead of
  // explicitly doing backups in a separate phase. The code should look like:
  //   while there are still PCs to disassemble:
  //     if the first PC in the queue is out of range:
  //       discard it
  //     else:
  //       disassemble the first PC in the queue and add it to lines
  //       add any new branch targets to the end of the queue
  //       add the address after the disassembled opcode to the queue
  DisassemblyState s(vdata, size, start_address, is_mac_environment, jump_table);
  while (!s.r.eof()) {
    s.opcode_start_address = s.r.where() + s.start_address;
    string line = string_printf("%08" PRIX32 " ", s.opcode_start_address);
    line += M68KEmulator::disassemble_one(s);
    line += '\n';
    uint32_t next_pc = s.r.where() + s.start_address;
    lines.emplace(s.opcode_start_address, make_pair(move(line), next_pc));
  }

  // Phase 2: Handle backups. Because opcodes can be different lengths in the
  // 68K architecture, sometimes we mis-disassemble an opcode because it starts
  // during a previous "opcode" that is actually unused or data. To handle this,
  // we re-disassemble any branch targets and labels that are word-aligned, are
  // within the address space, and do not have an existing line.
  unordered_set<uint32_t> pending_start_addrs;
  for (const auto& target_it : s.branch_target_addresses) {
    uint32_t target_pc = target_it.first;
    if (!(target_pc & 1) &&
        (target_pc >= s.start_address) &&
        (target_pc < s.start_address + size) &&
        !lines.count(target_pc)) {
      pending_start_addrs.emplace(target_pc);
    }
  }
  for (const auto& label_it : *labels) {
    uint32_t target_pc = label_it.first;
    if (!(target_pc & 1) &&
        (target_pc >= s.start_address) &&
        (target_pc < s.start_address + size) &&
        !lines.count(target_pc)) {
      pending_start_addrs.emplace(target_pc);
    }
  }
  set<pair<uint32_t, uint32_t>> backup_branches; // {start_pc, end_pc}
  while (!pending_start_addrs.empty()) {
    auto pending_start_addrs_it = pending_start_addrs.begin();
    uint32_t branch_start_pc = *pending_start_addrs_it;
    pending_start_addrs.erase(pending_start_addrs_it);
    uint32_t pc = branch_start_pc;
    s.r.go(pc - s.start_address);

    s.prev_was_return = false;
    while (!lines.count(pc) && !s.r.eof()) {
      string line = string_printf("%08" PRIX32 " ", pc);
      map<uint32_t, bool> temp_branch_target_addresses;
      s.branch_target_addresses.swap(temp_branch_target_addresses);
      line += M68KEmulator::disassemble_one(s);
      s.branch_target_addresses.swap(temp_branch_target_addresses);
      line += '\n';
      uint32_t next_pc = s.r.where() + s.start_address;
      lines.emplace(pc, make_pair(move(line), next_pc));
      pc = next_pc;

      // If any new branch target addresses were generated, we may need to do
      // more backups for them as well - we need to add them to both sets.
      for (const auto& target_it : temp_branch_target_addresses) {
        uint32_t addr = target_it.first;
        bool is_function_call = target_it.second;
        s.branch_target_addresses.emplace(addr, is_function_call);
        if (!(addr & 1)) {
          pending_start_addrs.emplace(addr);
        }
      }
    }

    if (pc != branch_start_pc) {
      backup_branches.emplace(branch_start_pc, pc);
    }
  }

  // Phase 3: generate output lines, including passed-in labels, branch target
  // labels, and alternate disassembly branches
  size_t ret_bytes = 0;
  deque<string> ret_lines;
  auto branch_target_it = s.branch_target_addresses.lower_bound(s.start_address);
  auto label_it = labels->lower_bound(s.start_address);
  auto backup_branch_it = backup_branches.begin();

  auto add_line = [&](uint32_t pc, const string& line) {
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
  };

  for (auto line_it = lines.begin();
       line_it != lines.end();
       line_it = lines.find(line_it->second.second)) {
    uint32_t pc = line_it->first;
    string& line = line_it->second.first;

    // Write branches first, if there are any here
    for (; backup_branch_it != backup_branches.end() &&
         backup_branch_it->first <= pc;
         backup_branch_it++) {
      uint32_t start_pc = backup_branch_it->first;
      uint32_t end_pc = backup_branch_it->second;
      auto orig_branch_target_it = branch_target_it;
      auto orig_label_it = label_it;
      branch_target_it = s.branch_target_addresses.lower_bound(start_pc);
      label_it = labels->lower_bound(start_pc);

      string branch_start_comment = string_printf("// begin alternate branch %08X-%08X\n", start_pc, end_pc);
      ret_bytes += branch_start_comment.size();
      ret_lines.emplace_back(move(branch_start_comment));

      for (auto backup_line_it = lines.find(start_pc);
           (backup_line_it != lines.end()) && (backup_line_it->first != end_pc);
           backup_line_it = lines.find(backup_line_it->second.second)) {
        add_line(backup_line_it->first, backup_line_it->second.first);
      }

      string branch_end_comment = string_printf("// end alternate branch %08X-%08X\n", start_pc, end_pc);
      ret_bytes += branch_end_comment.size();
      ret_lines.emplace_back(move(branch_end_comment));

      branch_target_it = orig_branch_target_it;
      label_it = orig_label_it;
    }

    add_line(pc, line);
  }

  // Phase 4: assemble the output lines into a single string and return it
  string ret;
  ret.reserve(ret_bytes);
  for (const auto& line : ret_lines) {
    ret += line;
  }
  return ret;
}

void M68KEmulator::print_source_trace(FILE*, const string&, size_t) const {
  throw runtime_error("source tracing is not implemented in M68KEmulator");
}

void M68KEmulator::execute() {
  if (!this->interrupt_manager.get()) {
    this->interrupt_manager.reset(new InterruptManager());
  }

  for (;;) {
    try {
      // Call debug hook if present
      if (this->debug_hook) {
        this->debug_hook(*this);
      }

      // Call any timer interrupt functions scheduled for this cycle
      this->interrupt_manager->on_cycle_start();

      // Execute a cycle
      uint16_t opcode = this->fetch_instruction_word();
      auto fn = this->fns[(opcode >> 12) & 0x000F].exec;
      (this->*fn)(opcode);

      this->instructions_executed++;

    } catch (const terminate_emulation&) {
      break;
    }
  }
}

void M68KEmulator::import_state(FILE* stream) {
  uint8_t version = freadx<uint8_t>(stream);
  if (version != 0) {
    throw runtime_error("unknown format version");
  }

  this->regs.import_state(stream);
  this->mem->import_state(stream);
}

void M68KEmulator::export_state(FILE* stream) const {
  fwritex<uint8_t>(stream, 0); // version

  this->regs.export_state(stream);
  this->mem->export_state(stream);
}
