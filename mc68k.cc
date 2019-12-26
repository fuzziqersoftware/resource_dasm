#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <phosg/Encoding.hh>
#include <unordered_map>

#include "mc68k.hh"

using namespace std;

// bit fields
// 0000000000000000
// iiiiaaabbbcccddd
//   zz   gss  vvvv
//          t
//     kkkkyyyyyyyy

inline int op_get_i(uint16_t op) {
  return ((op >> 12) & 0x000F);
}

inline int op_get_a(uint16_t op) {
  return ((op >> 9) & 0x0007);
}

inline int op_get_b(uint16_t op) {
  return ((op >> 6) & 0x0007);
}

inline int op_get_c(uint16_t op) {
  return ((op >> 3) & 0x0007);
}

inline int op_get_d(uint16_t op) {
  return (op & 0x0007);
}

inline int op_get_z(uint16_t op) {
  return ((op >> 12) & 0x0003);
}

inline int op_get_g(uint16_t op) {
  return ((op >> 8) & 0x0001);
}

inline int op_get_s(uint16_t op) {
  return ((op >> 6) & 0x0003);
}

inline int op_get_v(uint16_t op) {
  return (op & 0x000F);
}

inline int op_get_t(uint16_t op) {
  return ((op >> 6) & 0x0001);
}

inline int op_get_k(uint16_t op) {
  return ((op >> 8) & 0x000F);
}

inline int op_get_y(uint16_t op) {
  return (op & 0x00FF);
}



inline bool is_negative(uint32_t v, Size size) {
  if (size == Size::BYTE) {
    return (v & 0x80);
  } else if (size == Size::WORD) {
    return (v & 0x8000);
  } else if (size == Size::LONG) {
    return (v & 0x80000000);
  }
  throw runtime_error("incorrect size in is_negative");
}

int32_t sign_extend(uint32_t value, Size size) {
  if (size == Size::BYTE) {
    return (value & 0x80) ? (value | 0xFFFFFF00) : (value & 0x000000FF);
  } else if (size == Size::WORD) {
    return (value & 0x8000) ? (value | 0xFFFF0000) : (value & 0x0000FFFF);
  } else if (size == Size::LONG) {
    return value;
  } else {
    throw runtime_error("incorrect size in sign_extend");
  }
}



static uint8_t bytes_for_size(Size s) {
  if (s == Size::BYTE) {
    return 1;
  } else if (s == Size::WORD) {
    return 2;
  }
  return 4;
}

static Size size_for_tsize(TSize ts) {
  if (ts == TSize::WORD) {
    return Size::WORD;
  }
  return Size::LONG;
}

static Size size_for_tsize(uint8_t ts) {
  return size_for_tsize(static_cast<TSize>(ts));
}

static Size size_for_dsize(DSize ds) {
  if (ds == DSize::BYTE) {
    return Size::BYTE;
  } else if (ds == DSize::WORD) {
    return Size::WORD;
  }
  return Size::LONG;
}

static Size size_for_dsize(uint8_t ds) {
  return size_for_dsize(static_cast<DSize>(ds));
}



MC68KEmulator::MC68KEmulator() : pc(0), sr(0), execute(false),
    debug(DebuggingMode::Disabled), trap_call_region(NULL) {
  for (size_t x = 0; x < 8; x++) {
    this->d[x] = 0;
    this->a[x] = 0;
  }
}



void MC68KEmulator::print_state(FILE* stream, bool print_memory) {
  uint16_t pc_data[3];
  for (size_t x = 0; x < 3; x++) {
    try {
      pc_data[x] = this->read(this->pc + (2 * x), Size::WORD);
    } catch (const exception&) {
      pc_data[x] = 0;
    }
  }

  fprintf(stream, "  %08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 " / %08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 "/%08" PRIX32 " + %04hX(%c%c%c%c%c)/%08" PRIX32 " = %04hX %04hX %04hX\n",
      this->d[0], this->d[1], this->d[2], this->d[3], this->d[4], this->d[5], this->d[6], this->d[7],
      this->a[0], this->a[1], this->a[2], this->a[3], this->a[4], this->a[5], this->a[6], this->a[7],
      this->sr, ((this->sr & 0x10) ? 'x' : '-'), ((this->sr & 0x08) ? 'n' : '-'),
      ((this->sr & 0x04) ? 'z' : '-'), ((this->sr & 0x02) ? 'v' : '-'),
      ((this->sr & 0x01) ? 'c' : '-'), this->pc, pc_data[0], pc_data[1], pc_data[2]);

  if (print_memory) {
    for (const auto& region_it : this->memory_regions) {
      print_data(stderr, region_it.second, region_it.first, NULL,
          PrintDataFlags::PrintAscii | PrintDataFlags::CollapseZeroLines);
    }
  }
}



uint32_t MC68KEmulator::get_reg_value(bool is_a_reg, uint8_t reg_num) {
  if (is_a_reg) {
    return this->a[reg_num];
  } else {
    return this->d[reg_num];
  }
}

void MC68KEmulator::set_ccr_flags(int64_t x, int64_t n, int64_t z, int64_t v,
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

void MC68KEmulator::set_ccr_flags_integer_add(int32_t left_value,
    int32_t right_value, Size size) {
  left_value = sign_extend(left_value, size);
  right_value = sign_extend(right_value, size);
  int32_t result = sign_extend(left_value + right_value, size);

  bool overflow = (((left_value > 0) && (right_value > 0) && (result < 0)) ||
       ((left_value < 0) && (right_value < 0) && (result > 0)));

  // this looks kind of dumb, but it's necessary to force the compiler not to
  // sign-extend the 32-bit ints when converting to 64-bit
  uint64_t left_value_c = static_cast<uint32_t>(left_value);
  uint64_t right_value_c = static_cast<uint32_t>(right_value);
  bool carry = (left_value_c + right_value_c) > 0xFFFFFFFF;

  this->set_ccr_flags(-1, (result < 0), (result == 0), overflow, carry);
}

void MC68KEmulator::set_ccr_flags_integer_subtract(int32_t left_value,
    int32_t right_value, Size size) {
  left_value = sign_extend(left_value, size);
  right_value = sign_extend(right_value, size);
  int32_t result = sign_extend(left_value - right_value, size);

  bool overflow = (((left_value > 0) && (right_value < 0) && (result < 0)) ||
       ((left_value < 0) && (right_value > 0) && (result > 0)));
  bool carry = (static_cast<uint32_t>(left_value) < static_cast<uint32_t>(right_value));
  this->set_ccr_flags(-1, (result < 0), (result == 0), overflow, carry);
}

bool MC68KEmulator::address_is_register(void* addr) {
  return (addr >= this) && (addr < reinterpret_cast<const char*>(this) + sizeof(*this));
}

uint32_t MC68KEmulator::read(uint32_t addr, Size size) {
  return this->read(this->translate_address(addr), size);
}

uint32_t MC68KEmulator::read(void* addr, Size size) {
  if (size == Size::BYTE) {
    return *reinterpret_cast<uint8_t*>(addr);
  }

  // awful hack: if the address is within this class, don't byteswap. otherwise,
  // it must be in memory, so byteswap
  if (this->address_is_register(addr)) {
    if (size == Size::WORD) {
      return *reinterpret_cast<uint16_t*>(addr);
    } else if (size == Size::LONG) {
      return *reinterpret_cast<uint32_t*>(addr);
    }
  } else {
    if (size == Size::WORD) {
      return bswap16(*reinterpret_cast<uint16_t*>(addr));
    } else if (size == Size::LONG) {
      return bswap32(*reinterpret_cast<uint32_t*>(addr));
    }
  }

  throw invalid_argument("incorrect size on read");
}

void MC68KEmulator::write(uint32_t addr, uint32_t value, Size size) {
  this->write(this->translate_address(addr), value, size);
}

void MC68KEmulator::write(void* addr, uint32_t value, Size size) {
  if (size == Size::BYTE) {
    *reinterpret_cast<uint8_t*>(addr) = value;
    return;
  }

  // awful hack: if the address is within this class, don't byteswap. otherwise,
  // it must be in memory, so byteswap
  if (this->address_is_register(addr)) {
    if (size == Size::WORD) {
      *reinterpret_cast<uint16_t*>(addr) = value;
    } else if (size == Size::LONG) {
      *reinterpret_cast<uint32_t*>(addr) = value;
    } else {
      throw invalid_argument("incorrect size on write");
    }
  } else {
    if (size == Size::WORD) {
      *reinterpret_cast<uint16_t*>(addr) = bswap16(value);
    } else if (size == Size::LONG) {
      *reinterpret_cast<uint32_t*>(addr) = bswap32(value);
    } else {
      throw invalid_argument("incorrect size on write");
    }
  }
}

void* MC68KEmulator::translate_address(uint32_t addr) {
  auto region_it = memory_regions.upper_bound(addr);
  if (region_it == memory_regions.begin()) {
    throw out_of_range(string_printf("memory access before any range (%08" PRIX32 ")", addr));
  }

  region_it--;
  uint32_t offset = addr - region_it->first;
  if (offset >= region_it->second.size()) {
    throw out_of_range(string_printf("memory access out of range (%08" PRIX32 ")", addr));
  }

  return const_cast<char*>(region_it->second.data() + offset);
}

uint16_t MC68KEmulator::fetch_instruction_word(bool advance) {
  return this->fetch_instruction_data(Size::WORD, advance);
}

int16_t MC68KEmulator::fetch_instruction_word_signed(bool advance) {
  return static_cast<int16_t>(this->fetch_instruction_data(Size::WORD, advance));
}

uint32_t MC68KEmulator::fetch_instruction_data(Size size, bool advance) {
  if (size == Size::BYTE) {
    uint32_t ret = *reinterpret_cast<uint8_t*>(this->translate_address(this->pc));
    this->pc += (1 * advance);
    return ret;

  } else if (size == Size::WORD) {
    uint32_t ret = bswap16(*reinterpret_cast<uint16_t*>(this->translate_address(this->pc)));
    this->pc += (2 * advance);
    return ret;

  } else if (size == Size::LONG) {
    uint32_t ret = bswap32(*reinterpret_cast<uint32_t*>(this->translate_address(this->pc)));
    this->pc += (4 * advance);
    return ret;
  }

  throw runtime_error("incorrect size in instruction fetch");
}

int32_t MC68KEmulator::fetch_instruction_data_signed(Size size, bool advance) {
  int32_t data = this->fetch_instruction_data(size, advance);
  if ((size == Size::BYTE) && (data & 0x00000080)) {
    data |= 0xFFFFFF00;
  } else if ((size == Size::WORD) && (data & 0x00008000)) {
    data |= 0xFFFF0000;
  }
  return data;
}

uint32_t MC68KEmulator::resolve_address_extension(uint16_t ext) {
  bool is_a_reg = ext & 0x8000;
  uint8_t reg_num = static_cast<uint8_t>((ext >> 12) & 7);
  //bool index_is_ulong = ext & 0x0800; // if false, it's a signed word
  uint8_t scale = 1 << ((ext >> 9) & 3);

  uint32_t ret = this->get_reg_value(is_a_reg, reg_num) * scale;
  if (!(ext & 0x0100)) {
    // brief extension word
    // TODO: is this signed? here we're assuming it is
    int8_t offset = static_cast<int8_t>(ext & 0xFF);
    ret += offset;
    return ret;
  }

  // full extension word
  // page 43 in the programmers' manual
  throw runtime_error("full extension word not implemented");
}

uint32_t MC68KEmulator::resolve_address_control(uint8_t M, uint8_t Xn) {
  switch (M) {
    case 2:
      return this->a[Xn];
    case 5:
      return this->a[Xn] + this->fetch_instruction_word_signed();
    case 6:
      return this->a[Xn] + this->resolve_address_extension(
          this->fetch_instruction_word());
    case 7: {
      switch (Xn) {
        case 0:
          throw runtime_error("absolute short memory access");
        case 1:
          throw runtime_error("absolute long memory access");
        case 2: {
          uint32_t orig_pc = this->pc;
          return orig_pc + this->fetch_instruction_word_signed();
        }
        case 3: {
          uint32_t orig_pc = this->pc;
          return orig_pc + this->resolve_address_extension(
              this->fetch_instruction_word());
        }
        default:
          throw invalid_argument("incorrect address mode in control reference");
      }
    }
    default:
      throw invalid_argument("incorrect address mode in control reference");
  }
}

void* MC68KEmulator::resolve_address(uint8_t M, uint8_t Xn, Size size) {
  switch (M) {
    case 0:
      return &this->d[Xn];
    case 1:
      return &this->a[Xn];
    case 2:
      return this->translate_address(this->a[Xn]);
    case 3: {
      void* ret = this->translate_address(this->a[Xn]);
      this->a[Xn] += bytes_for_size(size);
      return ret;
    }
    case 4:
      this->a[Xn] -= bytes_for_size(size);
      return this->translate_address(this->a[Xn]);
    case 5:
      return this->translate_address(
          this->a[Xn] + this->fetch_instruction_word_signed());
    case 6:
      return this->translate_address(
          this->a[Xn] + this->resolve_address_extension(
            this->fetch_instruction_word()));
    case 7: {
      switch (Xn) {
        case 0:
          throw runtime_error("absolute short memory access");
        case 1:
          throw runtime_error("absolute long memory access");
        case 2:
          return this->translate_address(
              this->pc + this->fetch_instruction_word_signed());
        case 3:
          return this->translate_address(
              this->pc + this->resolve_address_extension(
                this->fetch_instruction_word()));
        case 4:
          this->pc += 2;
          return this->translate_address(this->pc - 2);
        default:
          throw runtime_error("invalid special address");
      }
    }
    default:
      throw runtime_error("invalid address");
  }
}



bool MC68KEmulator::check_condition(uint8_t condition) {
  switch (condition) {
    case 0x00: // true
      return true;
    case 0x01: // false
      return false;
    case 0x02: // hi (high; c=0 and z=0)
      return (this->ccr & 0x05) == 0;
    case 0x03: // ls (low or same; c=1 or z=1)
      return (this->ccr & 0x05) != 0;
    case 0x04: // cc (carry clear; c=0)
      return (this->ccr & 0x01) == 0;
    case 0x05: // cs (carry set)
      return (this->ccr & 0x01) != 0;
    case 0x06: // ne (not equal)
      return (this->ccr & 0x04) == 0;
    case 0x07: // eq (equal)
      return (this->ccr & 0x04) != 0;
    case 0x08: // vc (overflow clear)
      return (this->ccr & 0x02) == 0;
    case 0x09: // vs (overflow set)
      return (this->ccr & 0x02) != 0;
    case 0x0A: // pl (plus)
      return (this->ccr & 0x08) == 0;
    case 0x0B: // mi (minus)
      return (this->ccr & 0x08) != 0;
    case 0x0C: // ge (greater or equal)
      return ((this->ccr & 0x0A) == 0x00) || ((this->ccr & 0x0A) == 0x0A);
    case 0x0D: // lt (less)
      return ((this->ccr & 0x0A) == 0x08) || ((this->ccr & 0x0A) == 0x02);
    case 0x0E: // gt (greater)
      return ((this->ccr & 0x0E) == 0x0A) || ((this->ccr & 0x0A) == 0x00);
    case 0x0F: // le (less or equal)
      return ((this->ccr & 0x04) == 0x04) || ((this->ccr & 0x0A) == 0x08) || ((this->ccr & 0x0A) == 0x02);
    default:
      throw invalid_argument("invalid condition code");
  }
}



void MC68KEmulator::opcode_unimplemented(uint16_t opcode) {
  throw runtime_error(string_printf("unknown opcode: %04hX", opcode));
}

void MC68KEmulator::opcode_0123(uint16_t opcode) {
  // 1, 2, 3 are actually also handled by 0 (this is the only case where the i
  // field is split)
  uint8_t i = op_get_i(opcode);
  if (i) {
    Size size = size_for_dsize(i);
    if (op_get_b(opcode) == 1) {
      // movea.S An, ADDR
      if (size == Size::BYTE) {
        throw runtime_error("invalid movea.b opcode");
      }

      uint8_t source_M = op_get_c(opcode);
      uint8_t source_Xn = op_get_d(opcode);
      void* source = this->resolve_address(source_M, source_Xn, size);

      // movea is always a long write, even if it's a word read
      uint32_t value = sign_extend(this->read(source, size), size);
      this->write(&this->a[op_get_a(opcode)], value, Size::LONG);
      return;

    } else {
      // move.S ADDR1, ADDR2

      uint8_t source_M = op_get_c(opcode);
      uint8_t source_Xn = op_get_d(opcode);
      void* s = this->resolve_address(source_M, source_Xn, size);

      // note: this isn't a bug; the instruction format actually is
      // <r1><m1><m2><r2>
      uint8_t dest_M = op_get_b(opcode);
      uint8_t dest_Xn = op_get_a(opcode);
      void* d = this->resolve_address(dest_M, dest_Xn, size);

      uint32_t value = this->read(s, size);
      this->write(d, value, size);
      this->set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
      return;
    }
  }

  // note: i == 0 if we get here
  // 0000000000000000
  // iiiiaaabbbcccddd
  //   zz   gss  vvvv
  //          t
  //     kkkkyyyyyyyy

  uint8_t a = op_get_a(opcode);
  uint8_t M = op_get_c(opcode);
  uint8_t Xn = op_get_d(opcode);
  uint8_t s = op_get_s(opcode);
  Size size = static_cast<Size>(s);
  // TODO: movep

  string operation;
  if (op_get_g(opcode)) {
    void* addr = this->resolve_address(M, Xn, size);
    bool addr_is_reg = this->address_is_register(addr);

    switch (s) {
      case 0: // btst ADDR, Dn
        if (addr_is_reg) {
          uint32_t mem_value = this->read(addr, Size::LONG);
          this->set_ccr_flags(-1, -1, (mem_value & (1 << (this->d[a] & 0x1F))) ? 0 : 1, -1, -1);
        } else {
          uint32_t mem_value = this->read(addr, Size::BYTE);
          this->set_ccr_flags(-1, -1, (mem_value & (1 << (this->d[a] & 0x07))) ? 0 : 1, -1, -1);
        }
        break;

      case 1: // bchg ADDR, Dn
        throw runtime_error("bchg ADDR, Dn");

      case 2: // bclr ADDR, Dn
        throw runtime_error("bclr ADDR, Dn");

      case 3: { // bset ADDR, Dn
        uint32_t test_value = 1 << (this->d[a] & (addr_is_reg ? 0x1F : 0x07));
        Size size = addr_is_reg ? Size::LONG : Size::BYTE;

        uint32_t mem_value = this->read(addr, size);
        this->set_ccr_flags(-1, -1, (mem_value & test_value) ? 0 : 1, -1, -1);
        mem_value |= test_value;
        this->write(addr, mem_value, size);
      }
    }
    return;
  }

  // ccr/sr are allowed for ori, andi, and xori opcodes
  void* target;
  if (((a == 0) || (a == 1) || (a == 5)) && (M == 7) && (Xn == 4)) {
    if (size != Size::BYTE && size != Size::WORD) {
      throw runtime_error("size incorrect for status register");
    }
    target = &this->sr;
  } else {
    target = this->resolve_address(M, Xn, size);
  }

  Size fetch_size = (size == Size::BYTE) ? Size::WORD : size;
  uint32_t value = this->fetch_instruction_data(fetch_size);
  uint32_t mem_value = this->read(target, size);
  switch (a) {
    case 0: // ori ADDR, IMM
      mem_value |= value;
      this->write(target, mem_value, size);
      this->set_ccr_flags(-1, is_negative(mem_value, size), !mem_value, 0, 0);
      break;

    case 1: // andi ADDR, IMM
      mem_value &= value;
      this->write(target, mem_value, size);
      this->set_ccr_flags(-1, is_negative(mem_value, size), !mem_value, 0, 0);
      break;

    case 2: // subi ADDR, IMM
      this->set_ccr_flags_integer_subtract(mem_value, value, size);
      this->set_ccr_flags(this->ccr & 0x01, -1, -1, -1, -1);
      mem_value -= value;
      this->write(target, mem_value, size);
      break;

    case 3: // addi ADDR, IMM
      this->set_ccr_flags_integer_add(mem_value, value, size);
      this->set_ccr_flags(this->ccr & 0x01, -1, -1, -1, -1);
      mem_value += value;
      this->write(target, mem_value, size);
      break;

    case 5: // xori ADDR, IMM
      mem_value ^= value;
      this->write(target, mem_value, size);
      this->set_ccr_flags(-1, is_negative(mem_value, size), !mem_value, 0, 0);
      break;

    case 6: // cmpi ADDR, IMM
      this->set_ccr_flags_integer_subtract(mem_value, value, size);
      break;

    case 4:
      // TODO: these are all byte operations and they ignore the size field
      switch (s) {
        case 0:
          throw runtime_error("btst ADDR, IMM");
        case 1:
          throw runtime_error("bchg ADDR, IMM");
        case 2:
          throw runtime_error("bclr ADDR, IMM");
        case 3:
          throw runtime_error("bset ADDR, IMM");
      }
      throw runtime_error("opcode not implemented");

    default:
      throw runtime_error("invalid immediate operation");
  }
}

void MC68KEmulator::opcode_4(uint16_t opcode) {
  uint8_t g = op_get_g(opcode);

  if (g == 0) {
    if (opcode == 0x4AFC) {
      throw invalid_argument("invalid opcode 4AFC");
    }
    if ((opcode & 0xFFF0) == 0x4E70) {
      switch (opcode & 0x000F) {
        case 0: // reset
          this->execute = false;
          return;
        case 1: // nop
          return;
        case 2: // stop IMM
          throw runtime_error("stop IMM");
        case 3: // rte
          throw runtime_error("rte");
        case 4: // rtd IMM
          throw runtime_error("rtd IMM");
        case 5: // rts
          this->pc = this->read(this->a[7], Size::LONG);
          this->a[7] += 4;
          return;
        case 6: // trapv
          if (this->ccr & Condition::V) {
            throw runtime_error("overflow trap");
          }
          return;
        case 7: // rtr
          this->ccr = this->read(this->a[7], Size::WORD);
          this->pc = this->read(this->a[7] + 2, Size::LONG);
          this->a[7] += 6;
          return;
        default:
          throw invalid_argument("invalid special operation");
      }
    }

    uint8_t a = op_get_a(opcode);
    if (!(a & 0x04)) {
      uint8_t s = op_get_s(opcode);
      Size size = static_cast<Size>(s);
      void* addr = this->resolve_address(op_get_c(opcode), op_get_d(opcode),
          (s == 3) ? Size::WORD : size);

      if (s == 3) {
        if (a == 0) { // move.w ADDR, sr
          throw invalid_argument("cannot read from sr in user mode");
        } else if (a == 1) { // move.w ccr, ADDR
          this->ccr = this->read(addr, Size::WORD) & 0x1F;
          return;
        } else if (a == 2) { // move.w ADDR, ccr
          this->write(addr, this->ccr, Size::WORD);
          return;
        } else if (a == 3) { // move.w sr, ADDR
          throw invalid_argument("cannot write to sr in user mode");
        }
        throw invalid_argument("invalid opcode 4 with subtype 1");

      } else { // s is a valid Size
        switch (a) {
          case 0: // negx.S ADDR
            throw runtime_error("negx.S ADDR");
          case 1: // clr.S ADDR
            this->write(addr, 0, size);
            this->set_ccr_flags(-1, 0, 1, 0, 0);
            return;
          case 2: { // neg.S ADDR
            int32_t value = -static_cast<int32_t>(this->read(addr, size));
            this->write(addr, value, size);
            this->set_ccr_flags((value != 0), is_negative(value, size), (value == 0),
                (-value == value), (value != 0));
            return;
          }
          case 3: { // not.S ADDR
            uint32_t value = ~static_cast<int32_t>(this->read(addr, size));
            this->write(addr, value, size);
            this->set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
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
                this->d[d] = (this->d[d] & 0xFFFF00FF) |
                    ((this->d[d] & 0x00000080) ? 0x0000FF00 : 0x00000000);
                this->set_ccr_flags(-1, is_negative(this->d[d], Size::LONG),
                    (this->d[d] == 0), 0, 0);
                return;

              case 3: // extend word to long
                this->d[d] = (this->d[d] & 0x0000FFFF) |
                    ((this->d[d] & 0x00008000) ? 0xFFFF0000 : 0x00000000);
                this->set_ccr_flags(-1, is_negative(this->d[d], Size::LONG),
                    (this->d[d] == 0), 0, 0);
                return;

              case 7: // extend byte to long
                this->d[d] = (this->d[d] & 0x000000FF) |
                    ((this->d[d] & 0x00000080) ? 0xFFFFFF00 : 0x00000000);
                this->set_ccr_flags(-1, is_negative(this->d[d], Size::LONG),
                    (this->d[d] == 0), 0, 0);
                return;

              default:
                throw runtime_error("unknown opcode like ext.S REG");
            }

          } else { // movem.S ADDR REGMASK
            Size size = size_for_tsize(op_get_t(opcode));
            uint8_t bytes_per_value = bytes_for_size(size);
            uint8_t Xn = op_get_d(opcode);
            uint16_t reg_mask = this->fetch_instruction_word();

            // predecrement mode is special-cased for this opcode. in this mode
            // we write the registers in reverse order
            if (M == 4) {
              // bit 15 is D0, bit 0 is A7
              for (size_t x = 0; x < 8; x++) {
                if (reg_mask & (1 << x)) {
                  this->a[Xn] -= bytes_per_value;
                  this->write(this->a[Xn], this->a[7 - x], size);
                }
              }
              for (size_t x = 0; x < 8; x++) {
                if (reg_mask & (1 << (x + 8))) {
                  this->a[Xn] -= bytes_per_value;
                  this->write(this->a[Xn], this->d[7 - x], size);
                }
              }

            } else {
              // bit 15 is A7, bit 0 is D0
              uint32_t addr = this->resolve_address_control(M, Xn);
              for (size_t x = 0; x < 8; x++) {
                if (reg_mask & (1 << x)) {
                  this->write(addr, this->d[x], size);
                  addr += bytes_per_value;
                }
              }
              for (size_t x = 0; x < 8; x++) {
                if (reg_mask & (1 << (x + 8))) {
                  this->write(addr, this->a[x], size);
                  addr += bytes_per_value;
                }
              }
            }

            // note: ccr not affected
            return;
          }
        }
        if (b == 0) { // nbcd.b ADDR
          //void* addr = this->resolve_address(M, op_get_d(opcode), Size::BYTE);
          throw runtime_error("nbcd.b ADDR");
        }
        // b == 1
        if (M == 0) { // swap.w REG
          uint8_t reg = op_get_d(opcode);
          this->d[reg] = (this->d[reg] >> 16) | (this->d[reg] << 16);
          return;
        }

        // pea.l ADDR
        uint32_t addr = this->resolve_address_control(op_get_c(opcode),
            op_get_d(opcode));
        this->a[7] -= 4;
        this->write(this->a[7], addr, Size::LONG);
        // note: ccr not affected
        return;

      } else if (a == 5) {
        if (b == 3) { // tas.b ADDR
          //void* addr = this->resolve_address(op_get_c(opcode), op_get_d(opcode), Size::LONG);
          throw runtime_error("tas.b ADDR");
        }

        // tst.S ADDR
        void* addr = this->resolve_address(op_get_c(opcode), op_get_d(opcode), static_cast<Size>(b));
        Size size = static_cast<Size>(op_get_b(opcode) & 3);
        uint32_t value = this->read(addr, size);
        this->set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
        return;

      } else if (a == 6) {
         // movem.S REGMASK ADDR
        Size size = size_for_tsize(op_get_t(opcode));
        uint8_t bytes_per_value = bytes_for_size(size);
        uint8_t M = op_get_c(opcode);
        uint8_t Xn = op_get_d(opcode);
        uint16_t reg_mask = this->fetch_instruction_word();

        // postincrement mode is special-cased for this opcode
        uint32_t addr;
        if (M == 3) {
          addr = this->a[Xn];
        } else {
          addr = this->resolve_address_control(M, Xn);
        }

        // load the regs
        // bit 15 is A7, bit 0 is D0
        for (size_t x = 0; x < 8; x++) {
          if (reg_mask & (1 << x)) {
            this->d[x] = this->read(addr, size);
            addr += bytes_per_value;
          }
        }
        for (size_t x = 0; x < 8; x++) {
          if (reg_mask & (1 << (x + 8))) {
            this->a[x] = this->read(addr, size);
            addr += bytes_per_value;
          }
        }

        // in postincrement mode, update the address register
        if (M == 3) {
          this->a[Xn] = addr;
        }

        // note: ccr not affected
        return;

      } else if (a == 7) {
        if (b == 1) {
          uint8_t c = op_get_c(opcode);
          if (c == 2) { // link
            uint8_t d = op_get_d(opcode);
            this->a[7] -= 4;
            this->write(this->a[7], this->a[d], Size::LONG);
            this->a[d] = this->a[7];
            this->a[7] += this->fetch_instruction_word_signed();
            // note: ccr not affected
            return;

          } else if (c == 3) { // unlink
            uint8_t d = op_get_d(opcode);
            this->a[7] = this->a[d];
            this->a[d] = this->read(this->a[7], Size::LONG);
            this->a[7] += 4;
            // note: ccr not affected
            return;

          } else if ((c & 6) == 0) { // trap NUM
            throw runtime_error("trap NUM"); // num is v field

          } else if ((c & 6) == 4) { // move.usp AREG STORE/LOAD
            throw runtime_error("move.usp AREG STORE/LOAD"); // areg is d field, c&1 means store
          }

        } else if (b == 2) { // jsr ADDR
          uint32_t addr = this->resolve_address_control(op_get_c(opcode), op_get_d(opcode));
          this->a[7] -= 4;
          this->write(this->a[7], this->pc, Size::LONG);
          this->pc = addr;
          // note: ccr not affected
          return;

        } else if (b == 3) { // jmp ADDR
          this->pc = this->resolve_address_control(op_get_c(opcode), op_get_d(opcode));
          // note: ccr not affected
          return;
        }

      } else {
        throw invalid_argument("invalid opcode 4");
      }
    }

  } else { // g == 1
    uint8_t b = op_get_b(opcode);
    if (b == 7) { // lea.l AREG, ADDR
      this->a[op_get_a(opcode)] = this->resolve_address_control(
          op_get_c(opcode), op_get_d(opcode));
      // note: ccr not affected
      return;

    } else if (b == 5) { // chk.w DREG, ADDR
      //void* addr = this->resolve_address(op_get_c(opcode), op_get_d(opcode), Size::WORD);
      throw runtime_error("chk.w DREG ADDR"); // dreg is a field

    } else {
      throw invalid_argument(string_printf("invalid opcode 4 with b == %d", b));
    }
  }

  throw invalid_argument("invalid opcode 4");
}

void MC68KEmulator::opcode_5(uint16_t opcode) {
  uint8_t M = op_get_c(opcode);
  uint8_t Xn = op_get_d(opcode);

  uint8_t s = op_get_s(opcode);
  if (s == 3) {
    bool result = this->check_condition(op_get_k(opcode));

    if (M == 1) { // dbCC DISPLACEMENT
      int16_t displacement = this->fetch_instruction_word_signed();
      if (!result) {
        // TODO: this is supposed to be word opcode, not long
        this->d[Xn]--;
        if (this->d[Xn] != 0xFFFFFFFF) {
          this->pc += displacement - 2;
        }
      }
      // note: ccr not affected

    } else { // sCC ADDR
      void* addr = this->resolve_address(M, Xn, Size::BYTE);
      this->write(addr, (result ? 0xFF : 0x00), Size::BYTE);
      // note: ccr not affected
    }

  } else { // subq/addq ADDR, IMM
    Size size = static_cast<Size>(op_get_s(opcode));
    // TODO: when dealing with address registers, size is ignored according to
    // the manual. implement this.
    void* addr = this->resolve_address(M, Xn, size);
    uint8_t value = op_get_a(opcode);
    if (value == 0) {
      value = 8;
    }

    uint32_t mem_value = this->read(addr, size);
    if (op_get_g(opcode)) {
      this->write(addr, mem_value - value, size);
      this->set_ccr_flags_integer_subtract(mem_value, value, size);
    } else {
      this->write(addr, mem_value + value, size);
      this->set_ccr_flags_integer_add(mem_value, value, size);
    }
    this->set_ccr_flags(this->ccr & 0x01, -1, -1, -1, -1);
  }
}

void MC68KEmulator::opcode_6(uint16_t opcode) {
  // bra/bsr/bhi/bls/bcc/bcs/bne/beq/bvc/bvs/bpl/bmi/bge/blt/bgt/ble DISPLACEMENT

  uint32_t return_address = this->pc;
  int32_t displacement = static_cast<int8_t>(op_get_y(opcode));
  if (displacement == 0) {
    displacement = this->fetch_instruction_data_signed(Size::WORD, false);
    return_address = this->pc + 2;
  } else if (displacement == -1) {
    displacement = this->fetch_instruction_data_signed(Size::LONG, false);
    return_address = this->pc + 4;
  }

  // according to the programmer's manual, the displacement is relative to
  // (pc + 2) regardless of whether there's an extended displacement, hence the
  // initial fetch_instruction_word (before this function was called) doesn't
  // need to be corrected

  uint8_t k = op_get_k(opcode);
  bool should_branch;
  if (k == 1) { // false has a special meaning here (branch and link)
    this->a[7] -= 4;
    this->write(this->a[7], return_address, Size::LONG);
    should_branch = true;
  } else {
    should_branch = this->check_condition(k);
  }

  if (should_branch) {
    this->pc += displacement;
  } else {
    this->pc = return_address;
  }

  // note: ccr not affected
}

void MC68KEmulator::opcode_7(uint16_t opcode) {
  // moveq DREG, IMM
  uint32_t y = op_get_y(opcode);
  if (y & 0x00000080) {
    y |= 0xFFFFFF00;
  }
  this->d[op_get_a(opcode)] = y;
  this->set_ccr_flags(-1, (y & 0x80000000), (y == 0), 0, 0);
}

void MC68KEmulator::opcode_8(uint16_t opcode) {
  uint8_t a = op_get_a(opcode);
  uint8_t opmode = op_get_b(opcode);
  uint8_t M = op_get_c(opcode);
  uint8_t Xn = op_get_d(opcode);

  if ((opmode & 3) == 3) {
    if (opmode == 3) { // divu.S/divul.S ADDR, DREGS
      throw runtime_error("divu.S/divul.S ADDR, DREGS");
    } else { // divs.S/divsl.S ADDR, DREGS
      throw runtime_error("divs.S/divsl.S ADDR, DREGS");
    }
  }

  if ((opmode & 4) && !(M & 6)) {
    if (opmode == 4) { // sbcd DREG, DREG or sbcd -[AREG], -[AREG]
      throw runtime_error("sbcd DREG, DREG or sbcd -[AREG], -[AREG]");
    }
    if (opmode == 5) { // pack DREG, DREG or unpk -[AREG], -[AREG]
      this->fetch_instruction_word();
      throw runtime_error("pack DREG, DREG or unpk -[AREG], -[AREG]");
    }
    if (opmode == 6) { // unpk DREG, DREG or unpk -[AREG], -[AREG]
      this->fetch_instruction_word();
      throw runtime_error("unpk DREG, DREG or unpk -[AREG], -[AREG]");
    }
  }

  Size size = static_cast<Size>(opmode & 3);
  void* addr = this->resolve_address(M, Xn, size);
  uint32_t value = this->read(addr, size) | this->d[a];
  if (opmode & 4) { // or.S ADDR DREG
    this->write(addr, value, size);
  } else { // or.S DREG ADDR
    this->write(&this->d[a], value, size);
  }
  this->set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
}

void MC68KEmulator::opcode_9D(uint16_t opcode) {
  bool is_add = (opcode & 0xF000) == 0xD000;

  uint8_t dest = op_get_a(opcode);
  uint8_t opmode = op_get_b(opcode);
  uint8_t M = op_get_c(opcode);
  uint8_t Xn = op_get_d(opcode);

  if (((M & 6) == 0) && (opmode & 4) && (opmode != 7)) {
    throw runtime_error("unimplemented case of opcode 9/D");
  }

  if ((opmode & 3) == 3) {
    uint32_t mem_value;
    if (opmode & 4) { // add.l/sub.l AREG, ADDR
      void* addr = this->resolve_address(M, Xn, Size::LONG);
      mem_value = this->read(addr, Size::LONG);

    } else { // add.w/sub.w AREG, ADDR (mem value is sign-extended)
      void* addr = this->resolve_address(M, Xn, Size::WORD);
      mem_value = this->read(addr, Size::WORD);
      if (mem_value & 0x00008000) {
        mem_value |= 0xFFFF0000;
      }
    }

    // TODO: should we sign-extend here? is this always a long operation?
    if (is_add) {
      this->set_ccr_flags_integer_add(this->a[dest], mem_value, Size::LONG);
      this->a[dest] += mem_value;
    } else {
      this->set_ccr_flags_integer_subtract(this->a[dest], mem_value, Size::LONG);
      this->a[dest] -= mem_value;
    }
    this->set_ccr_flags(this->ccr & 0x01, -1, -1, -1, -1);
    return;
  }

  // add.S/sub.S DREG, ADDR
  // add.S/sub.S ADDR, DREG
  Size size = static_cast<Size>(opmode & 3);
  void* addr = this->resolve_address(M, Xn, size);
  uint32_t mem_value = this->read(addr, size);
  uint32_t reg_value = this->read(&this->d[dest], size);
  if (opmode & 4) {
    if (is_add) {
      this->set_ccr_flags_integer_add(mem_value, reg_value, size);
      mem_value += reg_value;
    } else {
      this->set_ccr_flags_integer_subtract(mem_value, reg_value, size);
      mem_value -= reg_value;
    }
    this->write(addr, mem_value, size);
  } else {
    if (is_add) {
      this->set_ccr_flags_integer_add(reg_value, mem_value, size);
      reg_value += mem_value;
    } else {
      this->set_ccr_flags_integer_subtract(reg_value, mem_value, size);
      reg_value -= mem_value;
    }
    this->write(&this->d[dest], reg_value, size);
  }
  this->set_ccr_flags(this->ccr & 0x01, -1, -1, -1, -1);
}

void MC68KEmulator::opcode_A(uint16_t opcode) {
  uint16_t trap_number;
  bool auto_pop = false;
  uint8_t flags = 0;

  if (opcode & 0x0800) {
    trap_number = opcode & 0x0BFF;
    auto_pop = opcode & 0x0400;
  } else {
    trap_number = opcode & 0x00FF;
    flags = (opcode >> 9) & 3;
  }

  switch (trap_number) {
    case 0x0046: { // _GetTrapAddress
      uint16_t trap_number = this->d[0] & 0xFFFF;
      if ((trap_number > 0x4F) && (trap_number != 0x54) && (trap_number != 0x57)) {
        trap_number |= 0x0800;
      }

      // if it already has a call routine, just return that
      try {
        this->a[0] = this->trap_to_call_addr.at(trap_number);
        return;
      } catch (const out_of_range&) { }

      // if the trap calls region doesn't exist, create it
      if (!this->trap_call_region) {
        this->trap_call_region = &this->memory_regions[0xF0000000];
      }

      // add this trap call to the trap call region
      this->trap_call_region->resize(this->trap_call_region->size() + 4);
      uint16_t* trap_call_routine = reinterpret_cast<uint16_t*>(const_cast<char*>(
          this->trap_call_region->data() + this->trap_call_region->size() - 4));
      uint32_t call_addr = 0xF0000000 + this->trap_call_region->size() - 4;

      // write the actual call and note its address
      trap_call_routine[0] = bswap16(0xA000 | trap_number); // A-trap opcode
      trap_call_routine[1] = bswap16(0x4E75); // rts
      this->trap_to_call_addr.emplace(trap_number, call_addr);

      // return the address
      this->a[0] = call_addr;
      break;
    }

    case 0x003D:
      if ((this->debug != DebuggingMode::Disabled) && (this->debug != DebuggingMode::Passive)) {
        fprintf(stderr, "warning: skipping trap 03D\n");
      }
      break;

    default:
      if (trap_number & 0x0800) {
        throw runtime_error(string_printf("unimplemented toolbox trap (num=%hX, auto_pop=%s)",
            trap_number & 0x0BFF, auto_pop ? "true" : "false"));
      } else {
        throw runtime_error(string_printf("unimplemented os trap (num=%hX, flags=%hhu)",
            trap_number & 0x00FF, flags));
      }
  }
}

void MC68KEmulator::opcode_B(uint16_t opcode) {
  uint8_t dest = op_get_a(opcode);
  uint8_t opmode = op_get_b(opcode);
  uint8_t M = op_get_c(opcode);
  uint8_t Xn = op_get_d(opcode);

  int32_t left_value, right_value;
  Size size;
  if (opmode < 3) { // cmp.S DREG, ADDR
    size = static_cast<Size>(opmode);

    left_value = this->d[dest];
    if (size == Size::BYTE) {
      left_value &= 0x000000FF;
    } else if (size == Size::WORD) {
      left_value &= 0x0000FFFF;
    }

    void* addr = this->resolve_address(M, Xn, size);
    right_value = this->read(addr, size);

  } else if ((opmode & 3) == 3) { // cmpa.S AREG, ADDR
    size = (opmode & 4) ? Size::LONG : Size::WORD;

    left_value = this->a[dest];

    void* addr = this->resolve_address(M, Xn, size);
    right_value = this->read(addr, size);

  } else { // probably xor
    throw invalid_argument("unimplemented opcode B");
  }

  this->set_ccr_flags_integer_subtract(left_value, right_value, size);
}

void MC68KEmulator::opcode_C(uint16_t opcode) {
  uint8_t a = op_get_a(opcode);
  uint8_t b = op_get_b(opcode);
  uint8_t c = op_get_c(opcode);
  uint8_t d = op_get_d(opcode);
  Size size = static_cast<Size>(b & 3);

  if (b < 3) { // and.S DREG, ADDR
    void* addr = this->resolve_address(c, d, size);
    void* reg = &this->d[a];
    uint32_t value = this->read(addr, size) & this->read(reg, size);
    this->write(reg, value, size);
    this->set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);

  } else if (b == 3) { // mulu.w DREG, ADDR (word * word = long form)
    void* addr = this->resolve_address(c, d, Size::WORD);
    uint32_t left = this->d[a] & 0x0000FFFF;
    uint32_t right = this->read(addr, Size::WORD);
    this->d[a] = left * right;

  } else if (b == 4) {
    if (c == 0) { // abcd DREG, DREG
      throw runtime_error("abcd DREG, DREG");

    } else if (c == 1) { // abcd -[AREG], -[AREG]
      throw runtime_error("abcd -[AREG], -[AREG]");

    } else { // and.S ADDR, DREG
      void* addr = this->resolve_address(c, d, size);
      void* reg = &this->d[a];
      uint32_t value = this->read(addr, size) & this->read(reg, size);
      this->write(addr, value, size);
      this->set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
    }

  } else if (b == 5) {
    if (c == 0) { // exg DREG, DREG
      uint32_t tmp = this->d[a];
      this->d[a] = this->d[d];
      this->d[d] = tmp;
      // note: ccr not affected

    } else if (c == 1) { // exg AREG, AREG
      uint32_t tmp = this->a[a];
      this->a[a] = this->a[d];
      this->a[d] = tmp;
      // note: ccr not affected

    } else { // and.S ADDR, DREG
      void* addr = this->resolve_address(c, d, size);
      void* reg = &this->d[a];
      uint32_t value = this->read(addr, size) & this->read(reg, size);
      this->write(addr, value, size);
      this->set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
    }

  } else if (b == 6) {
    if (c == 1) { // exg AREG, DREG
      uint32_t tmp = this->a[a];
      this->a[a] = this->d[d];
      this->d[d] = tmp;
      // note: ccr not affected

    } else { // and.S ADDR, DREG
      void* addr = this->resolve_address(c, d, size);
      void* reg = &this->d[a];
      uint32_t value = this->read(addr, size) & this->read(reg, size);
      this->write(addr, value, size);
      this->set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
    }

  } else if (b == 7) { // muls DREG, ADDR (word * word = long form)
    // I'm too lazy to figure out the sign-extension right now
    throw runtime_error("muls DREG, ADDR (word * word = long form)");
  }
}

void MC68KEmulator::opcode_E(uint16_t opcode) {
  uint8_t s = op_get_s(opcode);
  uint8_t Xn = op_get_d(opcode);
  if (s == 3) {
    //uint8_t M = op_get_c(opcode);
    //uint8_t k = op_get_k(opcode);
    //void* addr = this->resolve_address(M, Xn, Size::WORD);
    throw runtime_error("unimplemented opcode (E; s=3)");
  }

  uint8_t c = op_get_c(opcode);
  bool shift_is_reg = (c & 4);
  uint8_t a = op_get_a(opcode);
  uint8_t k = ((c & 3) << 1) | op_get_g(opcode);

  uint8_t shift_amount;
  if (shift_is_reg) {
    shift_amount = this->d[a] & 0x0000001F;
  } else {
    shift_amount = (a == 0) ? 8 : a;
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

      if (shift_amount == 0) {
        this->set_ccr_flags(-1, is_negative(this->d[Xn], Size::LONG),
            (this->d[Xn] == 0), 0, 0);
      } else {
        this->ccr &= 0xE0;

        int8_t last_shifted_bit = (left_shift ?
            (this->d[Xn] & (1 << (32 - shift_amount))) :
            (this->d[Xn] & (1 << (shift_amount - 1))));

        bool msb_changed;
        if (!rotate && logical_shift && left_shift) {
          uint32_t msb_values = (this->d[Xn] >> (32 - shift_amount));
          uint32_t mask = (1 << shift_amount) - 1;
          msb_values &= mask;
          msb_changed = ((msb_values == mask) || (msb_values == 0));
        } else {
          msb_changed = false;
        }

        if (rotate) {
          if (logical_shift) { // rotate without extend (rol, ror)
            if (left_shift) {
              this->d[Xn] = (this->d[Xn] << shift_amount) | (this->d[Xn] >> (32 - shift_amount));
            } else {
              this->d[Xn] = (this->d[Xn] >> shift_amount) | (this->d[Xn] << (32 - shift_amount));
            }
            last_shifted_bit = -1; // X unaffected for these opcodes

          } else { // rotate with extend (roxl, roxr) (TODO)
            throw runtime_error("roxl/roxr DREG, COUNT/REG");
          }

        } else {
          if (logical_shift) {
            if (left_shift) {
              this->d[Xn] <<= shift_amount;
            } else {
              this->d[Xn] >>= shift_amount;
            }
          } else {
            int32_t* value = reinterpret_cast<int32_t*>(&this->d[Xn]);
            if (left_shift) {
              *value <<= shift_amount;
            } else {
              *value >>= shift_amount;
            }
          }
        }

        this->set_ccr_flags(last_shifted_bit, (this->d[Xn] & 0x80000000),
            (this->d[Xn] == 0), msb_changed, last_shifted_bit);
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
      throw runtime_error("unimplemented opcode (E+k)");
  }
}



void MC68KEmulator::execute_next_opcode() {
  uint16_t opcode = this->fetch_instruction_word();
  switch ((opcode >> 12) & 0x000F) {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
      this->opcode_0123(opcode);
      break;
    case 0x04:
      this->opcode_4(opcode);
      break;
    case 0x05:
      this->opcode_5(opcode);
      break;
    case 0x06:
      this->opcode_6(opcode);
      break;
    case 0x07:
      this->opcode_7(opcode);
      break;
    case 0x08:
      this->opcode_8(opcode);
      break;
    case 0x09:
    case 0x0D:
      this->opcode_9D(opcode);
      break;
    case 0x0A:
      this->opcode_A(opcode);
      break;
    case 0x0B:
      this->opcode_B(opcode);
      break;
    case 0x0C:
      this->opcode_C(opcode);
      break;
    case 0x0E:
      this->opcode_E(opcode);
      break;
    case 0x0F:
      this->opcode_unimplemented(opcode);
      break;
  }
}

void MC68KEmulator::execute_forever() {

  if ((this->debug != DebuggingMode::Disabled) && (this->debug != DebuggingMode::Passive)) {
    fprintf(stderr, "  ===D0===/===D1===/===D2===/===D3===/===D4===/===D5===/===D6===/===D7=== / "
                      "===A0===/===A1===/===A2===/===A3===/===A4===/===A5===/===A6===/=A7==SP= + "
                      "=SR=(CBITS)/===PC=== = =INSTRUCTIONS=\n");
  }

  this->execute = true;
  while (this->execute) {
    this->execute_next_opcode();
    if ((this->debug != DebuggingMode::Disabled) && (this->debug != DebuggingMode::Passive)) {
      this->print_state(stderr, false);
    }

    if (this->debug == DebuggingMode::Interactive) {
      for (;;) {
        fprintf(stderr, ">>> ");

        char command[0x80];
        fgets(command, sizeof(command), stdin);
        size_t len = strlen(command);
        if ((len > 0) && (command[len - 1] == '\n')) {
          command[len - 1] = 0;
        }

        if (!strcmp(command, "c") || command[0] == 0) {
          break;
        } else if (!strcmp(command, "s")) {
          this->print_state(stderr, false);
        } else if (!strcmp(command, "m")) {
          this->print_state(stderr, true);
        } else {
          fprintf(stderr, "unknown command\n");
        }
      }
    }
  }
}
