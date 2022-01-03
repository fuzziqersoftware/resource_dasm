#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <deque>
#include <forward_list>
#include <utility>
#include <phosg/Encoding.hh>
#include <unordered_map>

#include "M68KEmulator.hh"
#include "TrapInfo.hh"

using namespace std;



static const uint8_t SIZE_BYTE = 0;
static const uint8_t SIZE_WORD = 1;
static const uint8_t SIZE_LONG = 2;

static const string char_for_size = "bwl?";
static const string char_for_tsize = "wl";
static const string char_for_dsize = "?blw";

static const vector<uint8_t> size_for_tsize({
  SIZE_WORD,
  SIZE_LONG,
});

static const vector<uint8_t> size_for_dsize({
  0xFF, // 0 is not a valid dsize
  SIZE_BYTE,
  SIZE_LONG,
  SIZE_WORD,
});

static const vector<uint8_t> bytes_for_size({
  1, 2, 4, 0xFF,
});



static const vector<const char*> string_for_condition({
    "t ", "f ", "hi", "ls", "cc", "cs", "ne", "eq",
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
//        gss  vvvv
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

static inline uint8_t op_get_s(uint16_t op) {
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

static int64_t read_immediate(StringReader& r, uint8_t s) {
  switch (s) {
    case SIZE_BYTE:
      return r.get_u16r() & 0x00FF;
    case SIZE_WORD:
      return r.get_u16r();
    case SIZE_LONG:
      return r.get_u32r();
    default:
      return -1;
  }
}

static inline bool maybe_char(uint8_t ch) {
  return (ch == 0) || (ch == '\t') || (ch == '\r') || (ch == '\n') || ((ch >= 0x20) && (ch <= 0x7E));
}

static string format_immediate(int64_t value) {
  string hex_repr = string_printf("0x%" PRIX64, value);

  string char_repr;
  for (ssize_t shift = 56; shift >= 0; shift-= 8) {
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
    } else if (byte == '\\') {
      char_repr += "\\\\";
    } else {
      char_repr += static_cast<char>(byte);
    }
  }
  if (char_repr.empty()) {
    return hex_repr; // value is zero
  }

  return string_printf("%s /* \'%s\' */", hex_repr.c_str(), char_repr.c_str());
}



M68KRegisters::M68KRegisters() {
  for (size_t x = 0; x < 8; x++) {
    this->a[x] = 0;
    this->d[x].u = 0;
  }
  this->pc = 0;
  this->sr = 0;
  this->debug.read_addr = 0;
  this->debug.write_addr = 0;
}

uint32_t M68KRegisters::get_reg_value(bool is_a_reg, uint8_t reg_num) {
  if (is_a_reg) {
    return this->a[reg_num];
  } else {
    return this->d[reg_num].u;
  }
}

void M68KRegisters::set_ccr_flags(int64_t x, int64_t n, int64_t z, int64_t v,
    int64_t c) {
  uint8_t mask = 0xFF;
  uint8_t replace = 0x00;

  int64_t values[5] = {c, v, z, n, x};
  for (size_t x = 0; x < 5; x++) {
    if (values[x] == 0) {
      mask &= ~(1 << x);
    } else if (values[x] > 0) {
      mask &= ~(1 << x);
      replace |= (1 << x);
    }
  }

  this->ccr = (this->ccr & mask) | replace;
}

void M68KRegisters::set_ccr_flags_integer_add(int32_t left_value,
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

void M68KRegisters::set_ccr_flags_integer_subtract(int32_t left_value,
    int32_t right_value, uint8_t size) {
  left_value = sign_extend(left_value, size);
  right_value = sign_extend(right_value, size);
  int32_t result = sign_extend(left_value - right_value, size);

  bool overflow = (((left_value > 0) && (right_value < 0) && (result < 0)) ||
       ((left_value < 0) && (right_value > 0) && (result > 0)));
  bool carry = (static_cast<uint32_t>(left_value) < static_cast<uint32_t>(right_value));
  this->set_ccr_flags(-1, (result < 0), (result == 0), overflow, carry);
}



M68KEmulator::M68KEmulator(shared_ptr<MemoryContext> mem) : should_exit(false), mem(mem), exec_fns{
  &M68KEmulator::exec_0123,
  &M68KEmulator::exec_0123,
  &M68KEmulator::exec_0123,
  &M68KEmulator::exec_0123,
  &M68KEmulator::exec_4,
  &M68KEmulator::exec_5,
  &M68KEmulator::exec_6,
  &M68KEmulator::exec_7,
  &M68KEmulator::exec_8,
  &M68KEmulator::exec_9D,
  &M68KEmulator::exec_A,
  &M68KEmulator::exec_B,
  &M68KEmulator::exec_C,
  &M68KEmulator::exec_9D,
  &M68KEmulator::exec_E,
  &M68KEmulator::exec_F,
} { }

shared_ptr<MemoryContext> M68KEmulator::memory() {
  return this->mem;
}

void M68KEmulator::print_state_header(FILE* stream) {
  fprintf(stream, "\
---D0---/---D1---/---D2---/---D3---/---D4---/---D5---/---D6---/---D7--- \
---A0---/---A1---/---A2---/---A3---/---A4---/---A5---/---A6---/-A7--SP- \
CBITS -RDADDR-=>-WRADDR- ---PC--- = INSTRUCTION\n");
}

void M68KEmulator::print_state(FILE* stream) {
  uint8_t pc_data[16];
  size_t pc_data_available = 0;
  uint32_t orig_debug_read = this->regs.debug.read_addr;
  for (; pc_data_available < 16; pc_data_available++) {
    try {
      pc_data[pc_data_available] = this->read(this->regs.pc + pc_data_available, SIZE_BYTE);
    } catch (const exception&) {
      break;
    }
  }
  this->regs.debug.read_addr = orig_debug_read;

  string disassembly = this->disassemble_one(pc_data, pc_data_available, this->regs.pc);

  fprintf(stream, "\
%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 " \
%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 " \
%c%c%c%c%c %08" PRIX32 "=>%08" PRIX32 " %08" PRIX32 " =%s\n",
      this->regs.d[0].u, this->regs.d[1].u, this->regs.d[2].u, this->regs.d[3].u,
      this->regs.d[4].u, this->regs.d[5].u, this->regs.d[6].u, this->regs.d[7].u,
      this->regs.a[0], this->regs.a[1], this->regs.a[2], this->regs.a[3],
      this->regs.a[4], this->regs.a[5], this->regs.a[6], this->regs.a[7],
      ((this->regs.sr & 0x10) ? 'x' : '-'), ((this->regs.sr & 0x08) ? 'n' : '-'),
      ((this->regs.sr & 0x04) ? 'z' : '-'), ((this->regs.sr & 0x02) ? 'v' : '-'),
      ((this->regs.sr & 0x01) ? 'c' : '-'), this->regs.debug.read_addr,
      this->regs.debug.write_addr, this->regs.pc, disassembly.c_str());
}



bool M68KEmulator::ResolvedAddress::is_register() const {
  return this->location != Location::MEMORY;
}

uint32_t M68KEmulator::read(const ResolvedAddress& addr, uint8_t size) {
  if (addr.location == ResolvedAddress::Location::D_REGISTER) {
    if (size == SIZE_BYTE) {
      return *reinterpret_cast<uint8_t*>(&this->regs.d[addr.addr].u);
    } else if (size == SIZE_WORD) {
      return *reinterpret_cast<uint16_t*>(&this->regs.d[addr.addr].u);
    } else if (size == SIZE_LONG) {
      return this->regs.d[addr.addr].u;
    } else {
      throw runtime_error("incorrect size on d-register read");
    }
  } else if (addr.location == ResolvedAddress::Location::A_REGISTER) {
    if (size == SIZE_BYTE) {
      return *reinterpret_cast<uint8_t*>(&this->regs.a[addr.addr]);
    } else if (size == SIZE_WORD) {
      return *reinterpret_cast<uint16_t*>(&this->regs.a[addr.addr]);
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

uint32_t M68KEmulator::read(uint32_t addr, uint8_t size) {
  this->regs.debug.read_addr = addr;

  if (size == SIZE_BYTE) {
    return this->mem->read_u8(addr);
  } else if (size == SIZE_WORD) {
    return this->mem->read_u16(addr);
  } else if (size == SIZE_LONG) {
    return this->mem->read_u32(addr);
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
  this->regs.debug.write_addr = addr;

  if (size == SIZE_BYTE) {
    this->mem->write_u8(addr, value);
  } else if (size == SIZE_WORD) {
    this->mem->write_u16(addr, value);
  } else if (size == SIZE_LONG) {
    this->mem->write_u32(addr, value);
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
    uint32_t ret = bswap16(this->mem->read<uint16_t>(this->regs.pc));
    this->regs.pc += (2 * advance);
    return ret;

  } else if (size == SIZE_LONG) {
    uint32_t ret = bswap32(this->mem->read<uint32_t>(this->regs.pc));
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
  // bool index_is_ulong = ext & 0x0800; // if false, it's a signed word
  uint8_t scale = 1 << ((ext >> 9) & 3);

  uint32_t ret = this->regs.get_reg_value(is_a_reg, reg_num) * scale;
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
      return this->regs.a[Xn] + this->resolve_address_extension(
          this->fetch_instruction_word());
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
          return orig_pc + this->resolve_address_extension(
              this->fetch_instruction_word());
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
            throw invalid_argument("invalid byte-sized immediate value");
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
    for (ssize_t x = 15; x >= 8; x--) {
      if (mask & (1 << x)) {
        ret += string_printf("A%zd,", x - 8);
      }
    }
    for (ssize_t x = 7; x >= 0; x--) {
      if (mask & (1 << x)) {
        ret += string_printf("D%zd,", x);
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
      return ret + string_printf(" - 0x%hhX]", -offset);
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
      base_displacement = r.get_s16r();
    } else if (base_displacement_size == 3) {
      base_displacement = r.get_s32r();
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
      base_displacement = r.get_s16r();
    } else if (base_displacement_size == 3) {
      base_displacement = r.get_s32r();
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
      outer_displacement = r.get_s16r();
    } else if (outer_displacement_mode == 3) {
      outer_displacement = r.get_s32r();
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
  uint8_t len = 0;
  r.pread_into(addr, &len, 1);
  if (len < 2) {
    return "";
  }

  string data = r.pread(addr + 1, len);
  string formatted_data = "\"";
  for (uint8_t ch : data) {
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
    } else if (ch >= 0x20 && ch <= 0x7F) {
      formatted_data += ch;
    } else {
      return "";
    }
  }
  formatted_data += '\"';

  return formatted_data;
}

string M68KEmulator::dasm_address(StringReader& r, uint32_t opcode_start_address,
    uint8_t M, uint8_t Xn, uint8_t size, map<uint32_t, bool>* branch_target_addresses,
    bool is_function_call) {
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
      int16_t displacement = r.get_u16r();
      if (displacement < 0) {
        return string_printf("[A%hhu - 0x%" PRIX16 "]", Xn, -displacement);
      } else {
        return string_printf("[A%hhu + 0x%" PRIX16 "]", Xn, displacement);
      }
    }
    case 6: {
      uint16_t ext = r.get_u16r();
      return M68KEmulator::dasm_address_extension(r, ext, Xn);
    }
    case 7: {
      switch (Xn) {
        case 0: {
          uint32_t address = r.get_u16r();
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
          uint32_t address = r.get_u32r();
          const char* name = name_for_lowmem_global(address);
          if (name) {
            return string_printf("[0x%08" PRIX32 " /* %s */]", address, name);
          } else {
            return string_printf("[0x%08" PRIX32 "]", address);
          }
        }
        case 2: {
          int16_t displacement = r.get_s16r();
          uint32_t target_address = opcode_start_address + displacement + 2;
          if (branch_target_addresses && !(target_address & 1)) {
            if (is_function_call) {
              (*branch_target_addresses)[target_address] = true;
            } else {
              branch_target_addresses->emplace(target_address, false);
            }
          }
          if (displacement == 0) {
            return string_printf("[PC] /* %08" PRIX32 " */", target_address);
          } else {
            string offset_str = (displacement > 0) ? string_printf(" + 0x%" PRIX16, displacement) : string_printf(" - 0x%" PRIX16, -displacement);
            string estimated_pstring = estimate_pstring(r, target_address);
            if (estimated_pstring.size()) {
              return string_printf("[PC%s /* %08" PRIX32 ", pstring %s */]", offset_str.c_str(), target_address, estimated_pstring.c_str());
            } else {
              return string_printf("[PC%s /* %08" PRIX32 " */]", offset_str.c_str(), target_address);
            }
          }
        }
        case 3: {
          uint16_t ext = r.get_u16r();
          return M68KEmulator::dasm_address_extension(r, ext, -1);
        }
        case 4:
          return format_immediate(read_immediate(r, size));
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
      return (this->regs.ccr & 0x05) == 0;
    case 0x03: // ls (low or same, unsigned less or equal; c=1 or z=1)
      return (this->regs.ccr & 0x05) != 0;
    case 0x04: // cc (carry clear; c=0)
      return (this->regs.ccr & 0x01) == 0;
    case 0x05: // cs (carry set; c=1)
      return (this->regs.ccr & 0x01) != 0;
    case 0x06: // ne (not equal; z=0)
      return (this->regs.ccr & 0x04) == 0;
    case 0x07: // eq (equal; z=1)
      return (this->regs.ccr & 0x04) != 0;
    case 0x08: // vc (overflow clear; v=0)
      return (this->regs.ccr & 0x02) == 0;
    case 0x09: // vs (overflow set; v=1)
      return (this->regs.ccr & 0x02) != 0;
    case 0x0A: // pl (plus; n=0)
      return (this->regs.ccr & 0x08) == 0;
    case 0x0B: // mi (minus; n=1)
      return (this->regs.ccr & 0x08) != 0;
    case 0x0C: // ge (greater or equal; n=v)
      return ((this->regs.ccr & 0x0A) == 0x00) || ((this->regs.ccr & 0x0A) == 0x0A);
    case 0x0D: // lt (less; n!=v)
      return ((this->regs.ccr & 0x0A) == 0x08) || ((this->regs.ccr & 0x0A) == 0x02);
    case 0x0E: // gt (greater; n=v && z=0)
      return ((this->regs.ccr & 0x0E) == 0x0A) || ((this->regs.ccr & 0x0E) == 0x00);
    case 0x0F: // le (less or equal; n!=v || z=1)
      return ((this->regs.ccr & 0x04) == 0x04) || ((this->regs.ccr & 0x0A) == 0x08) || ((this->regs.ccr & 0x0A) == 0x02);
    default:
      throw runtime_error("invalid condition code");
  }
}



void M68KEmulator::exec_unimplemented(uint16_t) {
  throw runtime_error("unimplemented opcode");
}

string M68KEmulator::dasm_unimplemented(StringReader& r, uint32_t, map<uint32_t, bool>&) {
  return string_printf(".unimplemented %04hX", r.get_u16r());
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
  uint8_t s = op_get_s(opcode);
  // TODO: movep

  string operation;
  if (op_get_g(opcode)) {
    auto addr = this->resolve_address(M, Xn, s);

    uint32_t test_value = 1 << (this->regs.d[a].u & (addr.is_register() ? 0x1F : 0x07));
    uint8_t size = addr.is_register() ? SIZE_LONG : SIZE_BYTE;
    uint32_t mem_value = this->read(addr, size);

    this->regs.set_ccr_flags(-1, -1, (mem_value & test_value) ? 0 : 1, -1, -1);

    switch (s) {
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

    this->write(addr, mem_value, size);
    return;
  }

  // Note: This must happen before the address is resolved, since the immediate
  // data comes before any address extension words.
  uint8_t fetch_size = (s == SIZE_BYTE) ? SIZE_WORD : s;
  uint32_t value = this->fetch_instruction_data(fetch_size);

  // ccr/sr are allowed for ori, andi, and xori opcodes
  ResolvedAddress target;
  if (((a == 0) || (a == 1) || (a == 5)) && (M == 7) && (Xn == 4)) {
    if (s != SIZE_BYTE && s != SIZE_WORD) {
      throw runtime_error("incorrect size for status register");
    }
    target = {0, ResolvedAddress::Location::SR};
  } else {
    target = this->resolve_address(M, Xn, s);
  }

  uint32_t mem_value = this->read(target, s);
  switch (a) {
    case 0: // ori ADDR, IMM
      mem_value |= value;
      this->write(target, mem_value, s);
      this->regs.set_ccr_flags(-1, is_negative(mem_value, s), !mem_value, 0, 0);
      break;

    case 1: // andi ADDR, IMM
      mem_value &= value;
      this->write(target, mem_value, s);
      this->regs.set_ccr_flags(-1, is_negative(mem_value, s), !mem_value, 0, 0);
      break;

    case 2: // subi ADDR, IMM
      this->regs.set_ccr_flags_integer_subtract(mem_value, value, s);
      this->regs.set_ccr_flags(this->regs.ccr & 0x01, -1, -1, -1, -1);
      mem_value -= value;
      this->write(target, mem_value, s);
      break;

    case 3: // addi ADDR, IMM
      this->regs.set_ccr_flags_integer_add(mem_value, value, s);
      this->regs.set_ccr_flags(this->regs.ccr & 0x01, -1, -1, -1, -1);
      mem_value += value;
      this->write(target, mem_value, s);
      break;

    case 5: // xori ADDR, IMM
      mem_value ^= value;
      this->write(target, mem_value, s);
      this->regs.set_ccr_flags(-1, is_negative(mem_value, s), !mem_value, 0, 0);
      break;

    case 6: // cmpi ADDR, IMM
      this->regs.set_ccr_flags_integer_subtract(mem_value, value, s);
      break;

    case 4: {
      // TODO: these are all byte operations and they ignore the size field
      auto addr = this->resolve_address(M, Xn, SIZE_BYTE);

      switch (s) {
        case 0:
          if (addr.is_register()) {
            uint32_t mem_value = this->read(addr, SIZE_LONG);
            this->regs.set_ccr_flags(-1, -1, (mem_value & (1 << (value & 0x1F))) ? 0 : 1, -1, -1);
          } else {
            uint32_t mem_value = this->read(addr, SIZE_BYTE);
            this->regs.set_ccr_flags(-1, -1, (mem_value & (1 << (value & 0x07))) ? 0 : 1, -1, -1);
          }
          break;
        case 1:
          throw runtime_error("unimplemented: bchg ADDR, IMM");
        case 2:
          throw runtime_error("unimplemented: bclr ADDR, IMM");
        case 3:
          throw runtime_error("unimplemented: bset ADDR, IMM");
        default:
          throw runtime_error("unimplemented: opcode 0:4");
      }
      break;
    }

    default:
      throw runtime_error("invalid immediate operation");
  }
}

string M68KEmulator::dasm_0123(StringReader& r, uint32_t start_address, map<uint32_t, bool>&) {
  // 1, 2, 3 are actually also handled by 0 (this is the only case where the i
  // field is split)

  uint32_t opcode_start_address = start_address + r.where();
  uint16_t op = r.get_u16r();
  uint8_t i = op_get_i(op);
  if (i) {
    uint8_t size = size_for_dsize.at(i);
    if (op_get_b(op) == 1) {
      // movea isn't valid with the byte operand size. We'll disassemble it
      // anyway, but complain at the end of the line

      uint8_t source_M = op_get_c(op);
      uint8_t source_Xn = op_get_d(op);
      string source_addr = M68KEmulator::dasm_address(r, opcode_start_address, source_M, source_Xn, size);

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
      string source_addr = M68KEmulator::dasm_address(r, opcode_start_address, source_M, source_Xn, size);

      // Note: this isn't a bug; the instruction format really is
      // <r1><m1><m2><r2>
      uint8_t dest_M = op_get_b(op);
      uint8_t dest_Xn = op_get_a(op);
      string dest_addr = M68KEmulator::dasm_address(r, opcode_start_address, dest_M, dest_Xn, size);

      return string_printf("move.%c     %s, %s", char_for_dsize.at(i),
          dest_addr.c_str(), source_addr.c_str());
    }
  }

  // Note: i == 0 if we get here

  uint8_t a = op_get_a(op);
  uint8_t M = op_get_c(op);
  uint8_t Xn = op_get_d(op);
  uint8_t s = op_get_s(op);
  // TODO: movep
  string operation;
  const char* invalid_str = "";
  bool special_regs_allowed = false;
  if (op_get_g(op)) {
    switch (op_get_s(op)) {
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

    string addr = M68KEmulator::dasm_address(r, opcode_start_address, M, Xn, s);
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
        switch (s) {
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
        s = SIZE_BYTE; // TODO: support longs somehow
        break;

      default:
        operation = ".invalid";
        invalid_str = " // invalid immediate operation";
    }
  }

  operation += '.';
  operation += char_for_size.at(s);
  operation.resize(10, ' ');

  if (special_regs_allowed && (M == 7) && (Xn == 4)) {
    if (s == 0) {
      return string_printf("%s ccr, %d%s", operation.c_str(),
          r.get_u16r() & 0x00FF, invalid_str);
    } else if (s == 1) {
      return string_printf("%s sr, %d%s", operation.c_str(), r.get_u16r(),
          invalid_str);
    }
  }

  // Note: format_immediate must happen before the address is resolved, since
  // the immediate data comes before any address extension words.
  string imm = format_immediate(read_immediate(r, s));
  string addr = M68KEmulator::dasm_address(r, opcode_start_address, M, Xn, s);
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
          this->should_exit = true;
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
          if (this->regs.ccr & Condition::V) {
            throw runtime_error("unimplemented: overflow trap");
          }
          return;
        case 7: // rtr
          this->regs.ccr = this->read(this->regs.a[7], SIZE_WORD);
          this->regs.pc = this->read(this->regs.a[7] + 2, SIZE_LONG);
          this->regs.a[7] += 6;
          return;
        default:
          throw runtime_error("invalid special operation");
      }
    }

    uint8_t a = op_get_a(opcode);
    if (!(a & 0x04)) {
      uint8_t s = op_get_s(opcode);
      auto addr = this->resolve_address(op_get_c(opcode), op_get_d(opcode),
          (s == 3) ? SIZE_WORD : s);

      if (s == 3) {
        if (a == 0) { // move.w ADDR, sr
          throw runtime_error("cannot read from sr in user mode");
        } else if (a == 1) { // move.w ccr, ADDR
          this->regs.ccr = this->read(addr, SIZE_WORD) & 0x1F;
          return;
        } else if (a == 2) { // move.w ADDR, ccr
          this->write(addr, this->regs.ccr, SIZE_WORD);
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
            this->write(addr, 0, s);
            this->regs.set_ccr_flags(-1, 0, 1, 0, 0);
            return;
          case 2: { // neg.S ADDR
            int32_t value = -static_cast<int32_t>(this->read(addr, s));
            this->write(addr, value, s);
            this->regs.set_ccr_flags((value != 0), is_negative(value, s), (value == 0),
                (-value == value), (value != 0));
            return;
          }
          case 3: { // not.S ADDR
            uint32_t value = ~static_cast<int32_t>(this->read(addr, s));
            this->write(addr, value, s);
            this->regs.set_ccr_flags(-1, is_negative(value, s), (value == 0), 0, 0);
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

string M68KEmulator::dasm_4(StringReader& r, uint32_t start_address, map<uint32_t, bool>& branch_target_addresses) {
  uint32_t opcode_start_address = start_address + r.where();
  uint16_t op = r.get_u16r();
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
          return string_printf("stop       0x%04X", r.get_u16r());
        case 3:
          return "rte";
        case 4:
          return string_printf("rtd        0x%04X", r.get_u16r());
        case 5:
          return "rts";
        case 6:
          return "trapv";
        case 7:
          return "rtr";
      }
    }

    uint8_t a = op_get_a(op);
    if (!(a & 0x04)) {
      string addr = M68KEmulator::dasm_address(r, opcode_start_address, op_get_c(op), op_get_d(op), SIZE_LONG);

      uint8_t s = op_get_s(op);
      if (s == 3) {
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
        switch (a) {
          case 0:
            return string_printf("negx.%c     %s", char_for_size.at(s), addr.c_str());
          case 1:
            return string_printf("clr.%c      %s", char_for_size.at(s), addr.c_str());
          case 2:
            return string_printf("neg.%c      %s", char_for_size.at(s), addr.c_str());
          case 3:
            return string_printf("not.%c      %s", char_for_size.at(s), addr.c_str());
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
            string reg_mask = M68KEmulator::dasm_reg_mask(r.get_u16r(), (M == 4));
            string addr = M68KEmulator::dasm_address(r, opcode_start_address, M, op_get_d(op), size_for_tsize.at(t));
            return string_printf("movem.%c    %s, %s", char_for_tsize.at(t), addr.c_str(), reg_mask.c_str());
          }
        }
        if (b == 0) {
          string addr = M68KEmulator::dasm_address(r, opcode_start_address, M, op_get_d(op), SIZE_BYTE);
          return string_printf("nbcd.b     %s", addr.c_str());
        }
        // b == 1
        if (M == 0) {
          return string_printf("swap.w     D%d", op_get_d(op));
        }
        string addr = M68KEmulator::dasm_address(r, opcode_start_address, M, op_get_d(op), SIZE_LONG);
        return string_printf("pea.l      %s", addr.c_str());

      } else if (a == 5) {
        if (b == 3) {
          string addr = M68KEmulator::dasm_address(r, opcode_start_address, op_get_c(op), op_get_d(op), SIZE_LONG);
          return string_printf("tas.b      %s", addr.c_str());
        }

        string addr = M68KEmulator::dasm_address(r, opcode_start_address, op_get_c(op), op_get_d(op), b);
        return string_printf("tst.%c      %s", char_for_size.at(b), addr.c_str());

      } else if (a == 6) {
        uint8_t t = op_get_t(op);
        uint8_t M = op_get_c(op);
        string reg_mask = M68KEmulator::dasm_reg_mask(r.get_u16r(), (M == 4));
        string addr = M68KEmulator::dasm_address(r, opcode_start_address, M, op_get_d(op), size_for_tsize.at(t));
        return string_printf("movem.%c    %s, %s", char_for_tsize.at(t), reg_mask.c_str(), addr.c_str());

      } else if (a == 7) {
        if (b == 1) {
          uint8_t c = op_get_c(op);
          if (c == 2) {
            int16_t delta = r.get_s16r();
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
          string addr = M68KEmulator::dasm_address(r, opcode_start_address, op_get_c(op), op_get_d(op), b, &branch_target_addresses, true);
          return string_printf("jsr        %s", addr.c_str());

        } else if (b == 3) {
          string addr = M68KEmulator::dasm_address(r, opcode_start_address, op_get_c(op), op_get_d(op), SIZE_LONG, &branch_target_addresses);
          return string_printf("jmp        %s", addr.c_str());
        }
      }

      return ".invalid   // invalid opcode 4";
    }

  } else { // g == 1
    uint8_t b = op_get_b(op);
    if (b == 7) {
      string addr = M68KEmulator::dasm_address(r, opcode_start_address, op_get_c(op), op_get_d(op), SIZE_LONG);
      return string_printf("lea.l      A%d, %s", op_get_a(op), addr.c_str());

    } else if (b == 5) {
      string addr = M68KEmulator::dasm_address(r, opcode_start_address, op_get_c(op), op_get_d(op), SIZE_WORD);
      return string_printf("chk.w      D%d, %s", op_get_a(op), addr.c_str());

    } else {
      string addr = M68KEmulator::dasm_address(r, opcode_start_address, op_get_c(op), op_get_d(op), SIZE_LONG);
      return string_printf(".invalid   %d, %s // invalid opcode 4 with b == %d",
          op_get_a(op), addr.c_str(), b);
    }
  }

  return ".invalid   // invalid opcode 4";
}



void M68KEmulator::exec_5(uint16_t opcode) {
  uint8_t M = op_get_c(opcode);
  uint8_t Xn = op_get_d(opcode);

  uint8_t s = op_get_s(opcode);
  if (s == 3) {
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
    uint8_t size = op_get_s(opcode);
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
    this->regs.set_ccr_flags(this->regs.ccr & 0x01, -1, -1, -1, -1);
  }
}

string M68KEmulator::dasm_5(StringReader& r, uint32_t start_address, map<uint32_t, bool>& branch_target_addresses) {
  uint32_t opcode_start_address = start_address + r.where();
  uint16_t op = r.get_u16r();
  uint32_t pc_base = start_address + r.where();

  uint8_t M = op_get_c(op);
  uint8_t Xn = op_get_d(op);

  uint8_t s = op_get_s(op);
  if (s == 3) {
    uint8_t k = op_get_k(op);
    const char* cond = string_for_condition.at(k);

    if (M == 1) {
      int16_t displacement = r.get_s16r();
      uint32_t target_address = pc_base + displacement;
      if (!(target_address & 1)) {
        branch_target_addresses.emplace(target_address, false);
      }
      if (displacement < 0) {
        return string_printf("db%s       D%d, -0x%" PRIX16 " /* %08" PRIX32 " */",
            cond, Xn, -displacement + 2, target_address);
      } else {
        return string_printf("db%s       D%d, +0x%" PRIX16 " /* %08" PRIX32 " */",
            cond, Xn, displacement + 2, target_address);
      }
    }
    string addr = M68KEmulator::dasm_address(r, opcode_start_address, M, Xn, SIZE_BYTE, &branch_target_addresses);
    return string_printf("s%s        %s", cond, addr.c_str(), &branch_target_addresses);
  }

  uint8_t size = op_get_s(op);
  string addr = M68KEmulator::dasm_address(r, opcode_start_address, M, Xn, size);
  uint8_t value = op_get_a(op);
  if (value == 0) {
    value = 8;
  }
  return string_printf("%s.%c     %s, %d", op_get_g(op) ? "subq" : "addq",
      char_for_size.at(size), addr.c_str(), value);
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

string M68KEmulator::dasm_6(StringReader& r, uint32_t start_address, map<uint32_t, bool>& branch_target_addresses) {
  uint16_t op = r.get_u16r();
  uint32_t pc_base = start_address + r.where();

  int64_t displacement = static_cast<int8_t>(op_get_y(op));
  if (displacement == 0) {
    displacement = r.get_s16r();
  } else if (displacement == -1) {
    displacement = r.get_s32r();
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
      branch_target_addresses[target_address] = true;
    } else {
      branch_target_addresses.emplace(target_address, false);
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

string M68KEmulator::dasm_7(StringReader& r, uint32_t, map<uint32_t, bool>&) {
  uint16_t op = r.get_u16r();
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

string M68KEmulator::dasm_8(StringReader& r, uint32_t start_address, map<uint32_t, bool>&) {
  uint16_t op = r.get_u16r();
  uint8_t a = op_get_a(op);
  uint8_t opmode = op_get_b(op);
  uint8_t M = op_get_c(op);
  uint8_t Xn = op_get_d(op);

  if ((opmode & 3) == 3) {
    char s = (opmode & 4) ? 's' : 'u';
    string ea_dasm = M68KEmulator::dasm_address(r, start_address, M, Xn, SIZE_WORD);
    return string_printf("div%c.w     D%hhu, %s", s, a, ea_dasm.c_str());
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
      uint16_t value = r.get_u16r();
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

  string ea_dasm = M68KEmulator::dasm_address(r, start_address, M, Xn, opmode & 3);
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
    this->regs.set_ccr_flags(this->regs.ccr & 0x01, -1, -1, -1, -1);
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
  this->regs.set_ccr_flags(this->regs.ccr & 0x01, -1, -1, -1, -1);
}

string M68KEmulator::dasm_9D(StringReader& r, uint32_t start_address, map<uint32_t, bool>&) {
  uint32_t opcode_start_address = start_address + r.where();
  uint16_t op = r.get_u16r();
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
      string ea_dasm = M68KEmulator::dasm_address(r, opcode_start_address, M, Xn, SIZE_LONG);
      return string_printf("%s.l      A%hhu, %s", op_name, dest, ea_dasm.c_str());
    } else {
      string ea_dasm = M68KEmulator::dasm_address(r, opcode_start_address, M, Xn, SIZE_WORD);
      return string_printf("%s.w      A%hhu, %s", op_name, dest, ea_dasm.c_str());
    }
  }

  string ea_dasm = M68KEmulator::dasm_address(r, opcode_start_address, M, Xn, opmode & 3);
  char ch = char_for_size.at(opmode & 3);
  if (opmode & 4) {
    return string_printf("%s.%c      %s, D%hhu", op_name, ch, ea_dasm.c_str(), dest);
  } else {
    return string_printf("%s.%c      D%hhu, %s", op_name, ch, dest, ea_dasm.c_str());
  }
}



void M68KEmulator::exec_A(uint16_t opcode) {
  if (this->syscall_handler) {
    if (!this->syscall_handler(*this, this->regs, opcode)) {
      this->should_exit = true;
    }
  } else {
    this->exec_unimplemented(opcode);
  }
}

string M68KEmulator::dasm_A(StringReader& r, uint32_t, map<uint32_t, bool>&) {
  uint16_t op = r.get_u16r();

  uint16_t trap_number;
  bool auto_pop = false;
  uint8_t flags = 0;
  if (op & 0x0800) {
    trap_number = op & 0x0BFF;
    auto_pop = op & 0x0400;
  } else {
    trap_number = op & 0xFF;
    flags = (op >> 8) & 7;
  }

  string ret = "trap       ";
  const auto* trap_info = info_for_68k_trap(trap_number, flags);
  if (trap_info) {
    ret += trap_info->name;
  } else {
    ret += string_printf("0x%03hX", trap_number);
  }

  if (flags) {
    ret += string_printf(", flags=%hhu", flags);
  }

  if (auto_pop) {
    ret += ", auto_pop";
  }

  return ret;
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

string M68KEmulator::dasm_B(StringReader& r, uint32_t start_address, map<uint32_t, bool>&) {
  uint32_t opcode_start_address = start_address + r.where();
  uint16_t op = r.get_u16r();
  uint8_t dest = op_get_a(op);
  uint8_t opmode = op_get_b(op);
  uint8_t M = op_get_c(op);
  uint8_t Xn = op_get_d(op);

  if ((opmode & 4) && (opmode != 7) && (M == 1)) {
    return string_printf("cmpm.%c     [A%hhu]+, [A%hhu]+",
        char_for_size.at(opmode & 3), dest, Xn);
  }

  if (opmode < 3) {
    string ea_dasm = M68KEmulator::dasm_address(r, opcode_start_address, M, Xn, opmode);
    return string_printf("cmp.%c      D%hhu, %s", char_for_size.at(opmode),
        dest, ea_dasm.c_str());
  }

  if ((opmode & 3) == 3) {
    if (opmode & 4) {
      string ea_dasm = M68KEmulator::dasm_address(r, opcode_start_address, M, Xn, SIZE_LONG);
      return string_printf("cmpa.l     A%hhu, %s", dest, ea_dasm.c_str());
    } else {
      string ea_dasm = M68KEmulator::dasm_address(r, opcode_start_address, M, Xn, SIZE_WORD);
      return string_printf("cmpa.w     A%hhu, %s", dest, ea_dasm.c_str());
    }
  }

  string ea_dasm = M68KEmulator::dasm_address(r, opcode_start_address, M, Xn, opmode & 3);
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

string M68KEmulator::dasm_C(StringReader& r, uint32_t start_address, map<uint32_t, bool>&) {
  uint16_t op = r.get_u16r();
  uint8_t a = op_get_a(op);
  uint8_t b = op_get_b(op);
  uint8_t c = op_get_c(op);
  uint8_t d = op_get_d(op);

  if (b < 3) { // and.S DREG, ADDR
    string ea_dasm = M68KEmulator::dasm_address(r, start_address, c, d, b);
    return string_printf("and.%c      D%hhu, %s", char_for_size.at(b), a,
        ea_dasm.c_str());

  } else if (b == 3) { // mulu.w DREG, ADDR (word * word = long form)
    string ea_dasm = M68KEmulator::dasm_address(r, start_address, c, d, b);
    return string_printf("mulu.w     D%hhu, %s", a, ea_dasm.c_str());

  } else if (b == 4) {
    if (c == 0) { // abcd DREG, DREG
      return string_printf("abcd       D%hhu, D%hhu", a, d);
    } else if (c == 1) { // abcd -[AREG], -[AREG]
      return string_printf("abcd       -[A%hhu], -[A%hhu]", a, d);
    } else { // and.S ADDR, DREG
      string ea_dasm = M68KEmulator::dasm_address(r, start_address, c, d, b);
      return string_printf("and.%c      %s, D%hhu", char_for_size.at(b),
          ea_dasm.c_str(), a);
    }

  } else if (b == 5) {
    if (c == 0) { // exg DREG, DREG
      return string_printf("exg        D%hhu, D%hhu", a, d);
    } else if (c == 1) { // exg AREG, AREG
      return string_printf("exg        A%hhu, A%hhu", a, d);
    } else { // and.S ADDR, DREG
      string ea_dasm = M68KEmulator::dasm_address(r, start_address, c, d, b);
      return string_printf("and.%c      %s, D%hhu", char_for_size.at(b),
          ea_dasm.c_str(), a);
    }

  } else if (b == 6) {
    if (c == 1) { // exg DREG, AREG
      return string_printf("exg        D%hhu, A%hhu", a, d);
    } else { // and.S ADDR, DREG
      string ea_dasm = M68KEmulator::dasm_address(r, start_address, c, d, b);
      return string_printf("and.%c      %s, D%hhu", char_for_size.at(b),
          ea_dasm.c_str(), a);
    }

  } else if (b == 7) { // muls DREG, ADDR (word * word = long form)
    string ea_dasm = M68KEmulator::dasm_address(r, start_address, c, d, b);
    return string_printf("muls.w     D%hhu, %s", a, ea_dasm.c_str());
  }

  // This should be impossible; we covered all possible values for b and all
  // branches unconditionally return
  throw logic_error("no cases matched for 1100bbb opcode");
}



void M68KEmulator::exec_E(uint16_t opcode) {
  uint8_t s = op_get_s(opcode);
  uint8_t Xn = op_get_d(opcode);
  if (s == 3) {
    // uint8_t M = op_get_c(opcode);
    // uint8_t k = op_get_k(opcode);
    // void* addr = this->resolve_address(M, Xn, SIZE_WORD);
    throw runtime_error("unimplemented (E; s=3)");
  }

  uint8_t c = op_get_c(opcode);
  bool shift_is_reg = (c & 4);
  uint8_t a = op_get_a(opcode);
  uint8_t k = ((c & 3) << 1) | op_get_g(opcode);

  uint8_t shift_amount;
  if (shift_is_reg) {
    if (s == SIZE_BYTE) {
      shift_amount = this->regs.d[a].u & 0x00000007;
    } else if (s == SIZE_WORD) {
      shift_amount = this->regs.d[a].u & 0x0000000F;
    } else {
      shift_amount = this->regs.d[a].u & 0x0000001F;
    }
  } else {
    shift_amount = (a == 0) ? 8 : a;
    if (shift_amount == 8 && s == SIZE_BYTE) {
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

      this->regs.ccr &= 0xE0;
      if (shift_amount == 0) {
        this->regs.set_ccr_flags(-1, is_negative(this->regs.d[Xn].u, SIZE_LONG),
            (this->regs.d[Xn].u == 0), 0, 0);

      } else if (s == SIZE_BYTE) {
        uint8_t& target = *reinterpret_cast<uint8_t*>(&this->regs.d[Xn].u);

        int8_t last_shifted_bit = (left_shift ?
            (target & (1 << (8 - shift_amount))) :
            (target & (1 << (shift_amount - 1))));

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

      } else if (s == SIZE_WORD) {
        uint16_t& target = *reinterpret_cast<uint16_t*>(&this->regs.d[Xn].u);

        int8_t last_shifted_bit = (left_shift ?
            (target & (1 << (16 - shift_amount))) :
            (target & (1 << (shift_amount - 1))));

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

      } else if (s == SIZE_LONG) {
        uint32_t& target = this->regs.d[Xn].u;

        int8_t last_shifted_bit = (left_shift ?
            (target & (1 << (32 - shift_amount))) :
            (target & (1 << (shift_amount - 1))));

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

string M68KEmulator::dasm_E(StringReader& r, uint32_t start_address, map<uint32_t, bool>&) {
  uint16_t op = r.get_u16r();

  static const vector<const char*> op_names({
      "asr   ", "asl   ", "lsr   ", "lsl   ", "roxr  ", "roxl  ", "ror   ", "rol   ",
      "bftst ", "bfextu", "bfchg ", "bfexts", "bfclr ", "bfffo ", "bfset ", "bfins "});

  uint8_t size = op_get_s(op);
  uint8_t Xn = op_get_d(op);
  if (size == 3) {
    uint8_t M = op_get_c(op);
    uint8_t k = op_get_k(op);
    const char* op_name = op_names[k];

    if (k & 8) {
      uint16_t ext = r.get_u16r();
      string ea_dasm = M68KEmulator::dasm_address(r, start_address, M, Xn, SIZE_LONG);
      string offset_str = (ext & 0x0800) ?
          string_printf("D%hu", (ext & 0x01C0) >> 6) :
          string_printf("%hu", (ext & 0x07C0) >> 6);
      // If immediate, 0 in the width field means 32
      string width_str;
      if ((ext & 0x003F) == 0x0000) {
        width_str = "32";
      } else {
        width_str = (ext & 0x0020) ? string_printf("D%hu", (ext & 0x0007))
            : string_printf("%hu", (ext & 0x001F));
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
    string ea_dasm = M68KEmulator::dasm_address(r, start_address, M, Xn, SIZE_WORD);
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
  if (this->syscall_handler) {
    if (!this->syscall_handler(*this, this->regs, opcode)) {
      this->should_exit = true;
    }
  } else {
    this->exec_unimplemented(opcode);
  }
}

string M68KEmulator::dasm_F(StringReader& r, uint32_t, map<uint32_t, bool>&) {
  uint16_t opcode = r.get_u16r();
  return string_printf(".extension 0x%03hX // unimplemented", opcode & 0x0FFF);
}



const vector<string (*)(StringReader& r, uint32_t start_address, map<uint32_t, bool>& branch_target_addresses)> M68KEmulator::dasm_fns({
  &M68KEmulator::dasm_0123,
  &M68KEmulator::dasm_0123,
  &M68KEmulator::dasm_0123,
  &M68KEmulator::dasm_0123,
  &M68KEmulator::dasm_4,
  &M68KEmulator::dasm_5,
  &M68KEmulator::dasm_6,
  &M68KEmulator::dasm_7,
  &M68KEmulator::dasm_8,
  &M68KEmulator::dasm_9D,
  &M68KEmulator::dasm_A,
  &M68KEmulator::dasm_B,
  &M68KEmulator::dasm_C,
  &M68KEmulator::dasm_9D,
  &M68KEmulator::dasm_E,
  &M68KEmulator::dasm_F,
});

////////////////////////////////////////////////////////////////////////////////

string M68KEmulator::disassemble_one(StringReader& r, uint32_t start_address,
    map<uint32_t, bool>& branch_target_addresses) {
  size_t opcode_offset = r.where();
  string opcode_disassembly;
  try {
    uint8_t op_high = r.get_u8(false);
    opcode_disassembly = (M68KEmulator::dasm_fns[(op_high >> 4) & 0x000F])(r,
        start_address, branch_target_addresses);
  } catch (const out_of_range&) {
    if (r.where() == opcode_offset) {
      // There must be at least 1 byte available since r.eof() was false
      r.get_u8();
    }
    opcode_disassembly = ".incomplete";
  }

  string line;
  {
    string hex_data;
    size_t end_offset = r.where();
    if (end_offset <= opcode_offset) {
      throw logic_error(string_printf("disassembly did not advance; used %zX/%zX bytes", r.where(), r.size()));
    }

    for (r.go(opcode_offset); r.where() < (end_offset & (~1));) {
      hex_data += string_printf(" %04X", r.get_u16r());
    }
    if (end_offset & 1) {
      // This should only happen for .incomplete at the end of the stream
      hex_data += string_printf(" %02X  ", r.get_u8());
    }
    while (hex_data.size() < 25) {
      hex_data += "     ";
    }
    line += hex_data;
  }

  line += ' ';
  line += opcode_disassembly;
  return line;
}

string M68KEmulator::disassemble_one(const void* vdata, size_t size,
    uint32_t start_address) {
  StringReader r(vdata, size);
  map<uint32_t, bool> branch_target_addresses;
  return M68KEmulator::disassemble_one(r, start_address, branch_target_addresses);
}

string M68KEmulator::disassemble(const void* vdata, size_t size,
    uint32_t start_address, const multimap<uint32_t, string>* labels) {
  static const multimap<uint32_t, string> empty_labels_map = {};
  if (!labels) {
    labels = &empty_labels_map;
  }

  map<uint32_t, bool> branch_target_addresses;
  map<uint32_t, pair<string, uint32_t>> lines; // {pc: (line, next_pc)}

  // Phase 1: generate the disassembly for each opcode, and collect branch
  // target addresses
  StringReader r(vdata, size);
  while (!r.eof()) {
    uint32_t pc = r.where() + start_address;
    string line = string_printf("%08" PRIX64 " ", pc);
    line += M68KEmulator::disassemble_one(r, start_address, branch_target_addresses);
    line += '\n';
    uint32_t next_pc = r.where() + start_address;
    lines.emplace(pc, make_pair(move(line), next_pc));
  }

  // Phase 2: handle backups. Because opcodes can be different lengths in the
  // 68K architecture, sometimes we mis-disassemble an opcode because it starts
  // during a previous "opcode" that is actually unused or data. To handle this,
  // we re-disassemble any branch targets that are word-aligned, are within the
  // address space, and do not have an existing line.
  unordered_set<uint32_t> pending_start_addrs;
  for (const auto& target_it : branch_target_addresses) {
    uint32_t target_pc = target_it.first;
    if (!(target_pc & 1) &&
        (target_pc >= start_address) &&
        (target_pc < start_address + size) &&
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
    r.go(pc - start_address);

    while (!lines.count(pc) && !r.eof()) {
      string line = string_printf("%08" PRIX64 " ", pc);
      map<uint32_t, bool> temp_branch_target_addresses;
      line += M68KEmulator::disassemble_one(r, start_address, temp_branch_target_addresses);
      line += '\n';
      uint32_t next_pc = r.where() + start_address;
      lines.emplace(pc, make_pair(move(line), next_pc));
      pc = next_pc;

      // If any new branch target addresses were generated, we may need to do
      // more backups for them as well - we need to add them to both sets.
      for (const auto& target_it : temp_branch_target_addresses) {
        uint32_t addr = target_it.first;
        bool is_function_call = target_it.second;
        branch_target_addresses.emplace(addr, is_function_call);
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
  auto branch_target_it = branch_target_addresses.begin();
  auto label_it = labels->begin();
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
    for (; (branch_target_it != branch_target_addresses.end()) &&
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
      branch_target_it = branch_target_addresses.lower_bound(start_pc);
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



void M68KEmulator::execute(const M68KRegisters& regs) {
  this->regs = regs;
  if (!this->interrupt_manager.get()) {
    this->interrupt_manager.reset(new InterruptManager());
  }

  this->should_exit = false;
  while (!this->should_exit) {

    // Call debug hook if present
    if (this->debug_hook && !this->debug_hook(*this, this->regs)) {
      break;
    }

    // Call any timer interrupt functions scheduled for this cycle
    if (!this->interrupt_manager->on_cycle_start()) {
      break;
    }

    // Execute a cycle
    uint16_t opcode = this->fetch_instruction_word();
    auto fn = this->exec_fns[(opcode >> 12) & 0x000F];
    (this->*fn)(opcode);
  }
}

void M68KEmulator::set_syscall_handler(
    std::function<bool(M68KEmulator&, M68KRegisters&, uint16_t)> handler) {
  this->syscall_handler = handler;
}

void M68KEmulator::set_debug_hook(
    std::function<bool(M68KEmulator&, M68KRegisters&)> hook) {
  this->debug_hook = hook;
}

void M68KEmulator::set_interrupt_manager(shared_ptr<InterruptManager> im) {
  this->interrupt_manager = im;
}
