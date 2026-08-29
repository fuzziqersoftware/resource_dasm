#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <array>
#include <deque>
#include <filesystem>
#include <forward_list>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <span>
#include <unordered_map>
#include <utility>

#include "../LowMemoryGlobals.hh"
#include "../TrapInfo.hh"
#include "M68KEmulator.hh"

namespace ResourceDASM {

using Size = M68KEmulator::Size;

constexpr char char_for_size(Size size) {
  return "bwl?"[std::min<size_t>(3, static_cast<size_t>(size))];
}

constexpr M68KEmulator::ValueType value_type_for_size(Size size) {
  using VT = M68KEmulator::ValueType;
  static constexpr std::array<M68KEmulator::ValueType, 4> data{VT::BYTE, VT::WORD, VT::LONG, VT::INVALID};
  return data[std::min<size_t>(3, static_cast<size_t>(size))];
}

constexpr uint8_t bytes_for_size(Size size) {
  static constexpr std::array<uint8_t, 4> data{1, 2, 4, 0xFF};
  return data[std::min<size_t>(3, static_cast<size_t>(size))];
}

static char char_for_value_type(M68KEmulator::ValueType type) {
  static constexpr std::array<char, 8> data{'l', 's', 'x', 'p', 'w', 'd', 'b', 'p'};
  return data.at(static_cast<size_t>(type));
}

constexpr std::array<const char*, 16> string_for_condition{
    "t", "f", "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"};
constexpr std::array<const char*, 32> string_for_float_condition{
    "f", "eq", "ogt", "oge", "olt", "ole", "ogl", "or", "un", "ueq", "ugt", "uge", "ult", "ule", "ne", "t", "sf", "seq", "gt", "ge", "lt", "le", "gl", "gle", "ngle", "ngl", "nle", "nlt", "nge", "ngt", "sne", "st"};

static bool is_negative(uint32_t v, Size size) {
  switch (size) {
    case Size::BYTE:
      return (v & 0x80);
    case Size::WORD:
      return (v & 0x8000);
    case Size::LONG:
      return (v & 0x80000000);
    default:
      throw std::runtime_error("incorrect size in is_negative");
  }
}

static int32_t sign_extend(uint32_t value, Size size) {
  switch (size) {
    case Size::BYTE:
      return (value & 0x80) ? (value | 0xFFFFFF00) : (value & 0x000000FF);
    case Size::WORD:
      return (value & 0x8000) ? (value | 0xFFFF0000) : (value & 0x0000FFFF);
    case Size::LONG:
      return value;
    default:
      throw std::runtime_error("incorrect size in sign_extend");
  }
}

constexpr bool maybe_char(uint8_t ch) {
  return (ch == 0) || (ch == '\t') || (ch == '\r') || (ch == '\n') || ((ch >= 0x20) && (ch <= 0x7E));
}

static std::string format_immediate(int64_t value, bool include_comment_tokens = true) {
  std::string hex_repr;
  if (value < -0xFFFF) {
    hex_repr = std::format("-0x{:08X}", value);
  } else if (value < -0xFF) {
    hex_repr = std::format("-0x{:04X}", value);
  } else if (value < 0) {
    hex_repr = std::format("-0x{:02X}", value);
  } else if (value > 0xFFFF) {
    hex_repr = std::format("0x{:08X}", value);
  } else if (value > 0xFF) {
    hex_repr = std::format("0x{:04X}", value);
  } else {
    hex_repr = std::format("0x{:02X}", value);
  }

  std::string char_repr;
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
    return hex_repr; // Value is zero
  }

  if (include_comment_tokens) {
    return std::format("{} /* \'{}\' */", hex_repr, char_repr);
  } else {
    return std::format("{} \'{}\'", hex_repr, char_repr);
  }
}

static std::string estimate_pstring(const phosg::StringReader& r, uint32_t addr) {
  try {
    uint8_t len = r.pget_u8(addr);
    if (len < 2) {
      return "";
    }

    std::string data = r.pread(addr + 1, len);
    std::string formatted_data = "\"";
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

  } catch (const std::out_of_range&) {
    return "";
  }
}

static std::string estimate_cstring(const phosg::StringReader& r, uint32_t addr) {
  std::string formatted_data = "\"";

  try {
    phosg::StringReader sr = r.sub(addr);

    char ch;
    for (ch = sr.get_s8(); ch != 0 && formatted_data.size() < 0x20; ch = sr.get_s8()) {
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
    formatted_data += ch ? "\"..." : "\"";
  } catch (const std::out_of_range&) {
    // Valid cstrings are always terminated; if we reach EOF, treat it as an invalid cstring
    return "";
  }
  return formatted_data;
}

template <typename VisitorT>
VisitorT::DecodeReturnT M68KEmulator::decode_instruction(VisitorT& visitor) {
  auto decode_address = [&visitor](uint8_t M, uint8_t Xn, Size size, bool sign_extend_imm) -> DecodedAddress {
    bool base_is_pc;
    uint32_t pc = 0;
    switch (M) {
      case 0:
        return DecodedAddress{.mode = AM::D_REG, .base_reg_num = Xn};
      case 1:
        return DecodedAddress{.mode = AM::A_REG, .base_reg_num = Xn};
      case 2:
        return DecodedAddress{.mode = AM::MEM_A, .base_reg_num = Xn};
      case 3:
        return DecodedAddress{.mode = AM::MEM_A_POSTINC, .base_reg_num = Xn};
      case 4:
        return DecodedAddress{.mode = AM::MEM_A_PREDEC, .base_reg_num = Xn};
      case 5:
        return DecodedAddress{.mode = AM::MEM_A_DISP, .base_reg_num = Xn, .base_disp = visitor.read_ins_s16(2)};
      case 6:
        base_is_pc = false;
        break;
      case 7: {
        switch (Xn) {
          case 0:
            return DecodedAddress{.mode = AM::MEM_ABSOLUTE, .base_disp = visitor.read_ins_s16(2)};
          case 1:
            return DecodedAddress{.mode = AM::MEM_ABSOLUTE, .base_disp = visitor.read_ins_s32(4)};
          case 2:
            return DecodedAddress{
                .mode = AM::MEM_PC_DISP, .base_pc = visitor.read_pc(), .base_disp = visitor.read_ins_s16(2)};
          case 3:
            base_is_pc = true;
            pc = visitor.read_pc();
            break;
          case 4:
            switch (size) {
              case Size::BYTE: {
                uint16_t v = visitor.read_ins_u16(1);
                if (v & 0xFF00) {
                  return DecodedAddress{
                      .mode = AM::INVALID, .invalid_reason = "bits above immediate 8-bit value are set"};
                }
                return DecodedAddress{
                    .mode = AM::IMM,
                    .base_disp = sign_extend_imm ? phosg::sign_extend<int32_t, uint8_t>(v) : (v & 0x00FF),
                };
              }
              case Size::WORD: {
                return DecodedAddress{
                    .mode = AM::IMM,
                    .base_disp = sign_extend_imm ? visitor.read_ins_s16(2) : visitor.read_ins_u16(2),
                };
              }
              case Size::LONG:
                return DecodedAddress{.mode = AM::IMM, .base_disp = visitor.read_ins_s32(4)};
              default:
                throw std::logic_error("Invalid size in decode_address 7/4");
            }
          default:
            return DecodedAddress{.mode = AM::INVALID, .invalid_reason = "undefined addressing mode"};
        }
        break;
      }
      default:
        throw std::logic_error(std::format("M is not less than 8 (received {})", M));
    }

    // If we get here, then there's an extension word, and one of the indirect forms will be used.
    uint16_t ext = visitor.read_ins_u16(2);

    DecodedAddress ret{
        .mode = AM::INVALID, // Will be set later
        .base_reg_num = base_is_pc ? static_cast<uint8_t>(0xFF) : Xn,
        .index_reg_num = static_cast<uint8_t>((ext >> 12) & 7),
        .index_scale = static_cast<uint8_t>(1 << ((ext >> 9) & 3)),
        .index_is_a_reg = static_cast<bool>(ext & 0x8000),
        .index_is_word = !(ext & 0x0800),
        .base_pc = pc,
    };

    if (!(ext & 0x0100)) { // Brief extension word
      ret.mode = base_is_pc ? AM::MEM_PC_INDEX : AM::MEM_A_INDEX;
      ret.base_disp = static_cast<int8_t>(ext & 0xFF);

    } else { // Full extension word
      // The IS and I/IS fields define the indirection behavior:
      //   0 000 => No Memory Indirect Action: [base + bd + index]
      //   0 001 => Indirect Preindexed with Null Outer Displacement: [[base + bd + index]]
      //   0 010 => Indirect Preindexed with Word Outer Displacement: [[base + bd + index] + sign_extend<16, 32>(od)]
      //   0 011 => Indirect Preindexed with Long Outer Displacement: [[base + bd + index] + od]
      //   0 100 => Reserved
      //   0 101 => Indirect Postindexed with Null Outer Displacement: [[base + bd] + index]
      //   0 110 => Indirect Postindexed with Word Outer Displacement: [[base + bd] + index + sign_extend<16, 32>(od)]
      //   0 111 => Indirect Postindexed with Long Outer Displacement: [[base + bd] + index + od]
      //   1 000 => No Memory Indirect Action (same as 0 000)
      //   1 001 => Memory Indirect with Null Outer Displacement: [[base + bd]]
      //   1 010 => Memory Indirect with Word Outer Displacement: [[base + bd] + sign_extend<16, 32>(od)]
      //   1 011 => Memory Indirect with Long Outer Displacement: [[base + bd] + od]
      //   1 100 => Reserved
      //   1 101 => Reserved
      //   1 110 => Reserved
      //   1 111 => Reserved
      // Additionally, the base may be suppressed, in which case it's treated as 0.

      ret.suppress_index = (ext & 0x0040);
      ret.suppress_base_reg = (ext & 0x0080);

      switch ((ext >> 4) & 3) {
        case 0:
          // 0 is a reserved value according to the programmer's manual (table 2-1 on page 2-3)
          return DecodedAddress{.mode = AM::INVALID, .invalid_reason = "full extension word with bd_size = 0"};
        case 1:
          ret.base_disp = 0;
          break;
        case 2:
          ret.base_disp = visitor.read_ins_s16(2);
          break;
        case 3:
          ret.base_disp = visitor.read_ins_s32(4);
          break;
        default:
          throw std::logic_error("unhandled displacement size value");
      }

      uint8_t i_is = ext & 7;
      if (i_is == 0) { // No memory indirect action (cases 0 000 and 1 000)
        ret.mode = base_is_pc ? AM::MEM_PC_INDEX : AM::MEM_A_INDEX;
      } else {
        if (i_is == 4) { // Reserved (cases 0 100 and 1 100)
          return DecodedAddress{.mode = AM::INVALID, .invalid_reason = "full extension word with I/IS = 4"};
        }
        if (ret.suppress_index && (i_is > 4)) { // Reserved (cases 1 101, 1 110, and 1 111)
          return DecodedAddress{.mode = AM::INVALID, .invalid_reason = "full extension word with IS = 1 and I/IS > 4"};
        }

        // All remaining cases (x 001, x 010, x 011)
        switch (i_is & 3) {
          case 0:
            // 0 is a reserved value according to the programmer's manual (table 2-1 on page 2-3)
            return DecodedAddress{.mode = AM::INVALID, .invalid_reason = "full extension word with od_size = 0"};
          case 1:
            ret.outer_disp = 0;
            break;
          case 2:
            ret.outer_disp = visitor.read_ins_s16(2);
            break;
          case 3:
            ret.outer_disp = visitor.read_ins_s32(4);
            break;
          default:
            throw std::logic_error("unhandled displacement size value");
        }
        ret.mode = (i_is & 4)
            ? (base_is_pc ? AM::MEM_PC_IND_POST : AM::MEM_A_IND_POST)
            : (base_is_pc ? AM::MEM_PC_IND_PRE : AM::MEM_A_IND_PRE);
      }
    }

    return ret;
  };

  uint16_t opcode = visitor.read_ins_u16();

  // Opcode bit fields:
  // 0000000000000000
  // iiiiaaabbbMMMRRR  (R = Xn)
  //         SS  vvvv  (S = size)
  // Not all fields are used for all opcodes; we pick out the most common fields here to precompute.
  uint8_t a = (opcode >> 9) & 7;
  uint8_t b = (opcode >> 6) & 7;
  uint8_t M = (opcode >> 3) & 7;
  uint8_t Xn = opcode & 7;
  Size size = static_cast<Size>(b & 3);

  switch (opcode & 0xF000) {
    case 0x0000:
      if ((opcode & 0xF5BF) == 0x003C) {
        uint16_t v = visitor.read_ins_u16(b ? 2 : 1);
        if (!b && (v & 0xFF00)) {
          return visitor.on_invalid("Immediate bitwise operation on CCR has high value bits set");
        } else if (a == 0) {
          // 0000000000111100 00000000VVVVVVVV ori ccr, imm
          // 0000000001111100 VVVVVVVVVVVVVVVV ori sr, imm
          return visitor.on_ori_sr_imm(b ? Size::WORD : Size::BYTE, v);
        } else if (a == 1) {
          // 0000001000111100 00000000VVVVVVVV andi ccr, imm
          // 0000001001111100 VVVVVVVVVVVVVVVV andi sr, imm
          if (!b) {
            v |= 0xFF00; // Don't affect high SR bits if it's a byte operation
          }
          return visitor.on_andi_sr_imm(b ? Size::WORD : Size::BYTE, v);
        } else if (a == 5) {
          // 0000101000111100 00000000VVVVVVVV xori ccr, imm
          // 0000101001111100 VVVVVVVVVVVVVVVV xori sr, imm
          return visitor.on_xori_sr_imm(b ? Size::WORD : Size::BYTE, v);
        } else {
          return visitor.on_invalid("Invalid opcode 0/SR");
        }

      } else if ((b & 4) || (a == 4)) {
        if ((b & 4) && (M == 1)) { // 0000RRRXXX001AAA disp16 (X>=4) movep
          if (size == Size::INVALID) {
            return visitor.on_invalid("Invalid size for movep");
          } else {
            return visitor.on_movep(b & 1, b & 2, b, Xn, visitor.read_ins_s16(2));
          }
        } else {
          uint16_t v = (b & 4) ? 0 : visitor.read_ins_u16(1);
          auto addr = decode_address(M, Xn, Size::BYTE, false);
          if (((b & 3) == 0) ? !addr.is_data_mode() : !addr.is_data_alterable_mode()) {
            return visitor.on_invalid(nullptr, &addr);
          }
          if (b & 4) {
            // 0000RRR100MMMRRR (DATA) btst
            // 0000RRR101MMMRRR (DATA ALTERABLE) bchg
            // 0000RRR110MMMRRR (DATA ALTERABLE) bclr
            // 0000RRR111MMMRRR (DATA ALTERABLE) bset
            return visitor.on_btst_bchg_bclr_bset(b & 3, addr, a, 0);
          } else if (v & 0xFF00) {
            return visitor.on_invalid("Immediate btst/bchg/bclr/bset operation has high value bits set", &addr);
          } else {
            // 0000100000MMMRRR 00000000VVVVVVVV (DATA) btst
            // 0000100001MMMRRR 00000000VVVVVVVV (DATA ALTERABLE) bchg
            // 0000100010MMMRRR 00000000VVVVVVVV (DATA ALTERABLE) bclr
            // 0000100011MMMRRR 00000000VVVVVVVV (DATA ALTERABLE) bset
            return visitor.on_btst_bchg_bclr_bset(b & 3, addr, 0xFF, v);
          }
        }

      } else if (size != Size::INVALID) {
        if (a == 7) { // 00001110SSMMMRRR WRRRX00000000000 (S<3 and MEMORY ALTERABLE) moves
          uint16_t ext = visitor.read_ins_u16();
          auto addr = decode_address(M, Xn, size, false);
          if (ext & 0x07FF) {
            return visitor.on_invalid("Arguments word has reserved bits set", &addr);
          }
          if (!addr.is_memory_alterable_mode()) {
            return visitor.on_invalid(nullptr, &addr);
          }
          return visitor.on_moves(size, addr, ext & 0x8000, (ext >> 12) & 7, ext & 0x0800);

        } else {
          uint32_t value = (size == Size::LONG) ? visitor.read_ins_u32(4) : visitor.read_ins_u16(2);
          auto addr = decode_address(M, Xn, size, false);
          if (!addr.is_data_alterable_mode()) {
            return visitor.on_invalid(nullptr, &addr);
          }
          switch (a) {
            case 0: // 00000000SSMMMRRR imm16/32 (S<3 and DATA ALTERABLE) ori
              return visitor.on_ori(size, addr, value);
            case 1: // 00000010SSMMMRRR imm16/32 (S<3 and DATA ALTERABLE) andi
              return visitor.on_andi(size, addr, value);
            case 2: // 00000100SSMMMRRR imm16/32 (S<3 and DATA ALTERABLE) subi
              return visitor.on_subi(size, addr, value);
            case 3: // 00000110SSMMMRRR imm16/32 (S<3 and DATA ALTERABLE) addi
              return visitor.on_addi(size, addr, value);
            case 4:
              return visitor.on_invalid("Invalid opcode 0/a/<3/4", &addr);
            case 5: // 00001010SSMMMRRR imm16/32 (S<3 and DATA ALTERABLE) xori
              return visitor.on_xori(size, addr, value);
            case 6: // 00001100SSMMMRRR imm16/32 (S<3 and DATA ALTERABLE) cmpi
              return visitor.on_cmpi(size, addr, value);
            default: // Also case 7, which should have been handled above
              throw std::logic_error("unhandled 0/a/<3 case");
          }
        }

      } else if (a == 3) {
        if (M == 1) { // 000001101100WRRR rtm
          return visitor.on_rtm();
        } else { // 0000011011MMMRRR 00000000CCCCCCCC (CONTROL) callm
          uint16_t ext = visitor.read_ins_u16(1);
          auto addr = decode_address(M, Xn, Size::LONG, false);
          if (ext & 0xFF00) {
            return visitor.on_invalid("Arguments word has reserved bits set", &addr);
          }
          if (!addr.is_control_mode()) {
            return visitor.on_invalid(nullptr, &addr);
          }
          return visitor.on_callm(addr, ext);
        }
      } else if (a & 4) {
        size = static_cast<Size>((a & 3) - 1); // a & 3 cannot be 0 here because a == 4 was handled above
        if (M == 7 && Xn == 4) { // 00001SS011111100 WNNN000UUU000CCC wnnn000uuu000ccc (S>1 and MEMORY ALTERABLE) cas2
          uint16_t args1 = visitor.read_ins_u16();
          uint16_t args2 = visitor.read_ins_u16();
          if (args1 & 0x0E38) {
            return visitor.on_invalid("Arguments word has reserved bits set");
          }
          if (args2 & 0x0E38) {
            return visitor.on_invalid("Arguments word has reserved bits set");
          }
          return visitor.on_cas2(size, args1 & 0x8000, (args1 >> 12) & 7, args1 & 7, (args1 >> 6) & 7,
              args2 & 0x8000, (args2 >> 12) & 7, args2 & 7, (args2 >> 6) & 7);

        } else { // 00001SS011MMMRRR 0000000UUU000CCC (S>0 and MEMORY ALTERABLE) cas
          uint16_t args1 = visitor.read_ins_u16();
          auto addr = decode_address(M, Xn, size, false);
          if (args1 & 0xFE38) {
            return visitor.on_invalid("Arguments word has reserved bits set", &addr);
          }
          if (!addr.is_memory_alterable_mode()) {
            return visitor.on_invalid(nullptr, &addr);
          }
          return visitor.on_cas(size, addr, args1 & 7, (args1 >> 6) & 7);
        }
      } else {
        // 00000SS011MMMRRR WRRR000000000000 (S<3 and CONTROL) cmp2
        // 00000SS011MMMRRR WRRR100000000000 (S<3 and CONTROL) chk2
        if (a == 0) {
          return visitor.on_invalid("Invalid size for cmp2/chk2");
        }
        size = static_cast<Size>((a & 3) - 1);
        uint16_t args = visitor.read_ins_u16();
        auto addr = decode_address(M, Xn, size, true);
        if (args & 0x07FF) {
          return visitor.on_invalid("Arguments word has reserved bits set", &addr);
        }
        return visitor.on_chk2_cmp2(size, addr, args & 0x8000, (args >> 12) & 7, args & 0x0800);
      }
      throw std::logic_error("Failed to decode any case of opcode 0");

    case 0x1000:
    case 0x2000:
    case 0x3000: {
      // 00SSDDD001MMMRRR movea
      // 00SSrrrmmmMMMRRR move (m != 1)

      // Note: this is not a bug; the two M fields really are next to each other
      uint8_t& dest_Xn = a;
      uint8_t& dest_M = b;
      uint8_t& src_M = M;
      uint8_t& src_Xn = Xn;

      constexpr std::array<Size, 4> size_map{Size::INVALID, Size::BYTE, Size::LONG, Size::WORD};
      Size size = size_map[(opcode >> 12) & 3];
      if (size == Size::INVALID) {
        throw std::logic_error("Incorrect size case for opcodes 1, 2, 3");
      }

      auto src_addr = decode_address(src_M, src_Xn, size, false);
      // All modes allowed for src_addr

      if (dest_M == 1) { // movea
        return (size == Size::BYTE)
            ? visitor.on_invalid("Invalid size for movea", &src_addr)
            : visitor.on_movea(size, dest_Xn, src_addr);
      } else { // move
        auto dest_addr = decode_address(dest_M, dest_Xn, size, false);
        if (!dest_addr.is_data_alterable_mode()) {
          return visitor.on_invalid(nullptr, &dest_addr);
        }
        return visitor.on_move(size, dest_addr, src_addr);
      }
      throw std::logic_error("Failed to decode any case of opcode 123");
    }

    case 0x4000:
      if (((a & 5) == 4) && ((b & 6) == 2) && (M >= 2)) {
        // 01001D001SMMMRRR mask16 (CONTROL or (PREDEC/POSTINC)) movem
        size = (b & 1) ? Size::LONG : Size::WORD;
        uint16_t reg_mask = visitor.read_ins_u16(2);
        auto addr = decode_address(M, Xn, size, false);
        if (a & 2) { // Memory to registers
          if (!addr.is_control_mode() && (addr.mode != AM::MEM_A_POSTINC)) {
            return visitor.on_invalid(nullptr, &addr);
          }
          return visitor.on_movem_read(size, addr, reg_mask);
        } else { // Registers to memory
          if (!addr.is_control_mode() && (addr.mode != AM::MEM_A_PREDEC)) {
            return visitor.on_invalid(nullptr, &addr);
          }
          return visitor.on_movem_write(size, addr, reg_mask);
        }

      } else if ((b == 7) && ((M == 2) || (M >= 5))) { // 0100RRR111MMMRRR (CONTROL) lea
        auto addr = decode_address(M, Xn, Size::LONG, false);
        if (!addr.is_control_mode()) {
          return visitor.on_invalid(nullptr, &addr);
        }
        return visitor.on_lea(a, addr);

      } else if (((b & 5) == 4) && (M != 1)) { // 0100RRRSS0MMMRRR (S>=2 and DATA) chk.S
        size = (b & 2) ? Size::WORD : Size::LONG;
        auto addr = decode_address(M, Xn, size, true);
        if (!addr.is_data_mode()) {
          return visitor.on_invalid(nullptr, &addr);
        }
        return visitor.on_chk(size, addr, a);

      } else if ((a == 4) && ((b == 2) || (b == 3) || (b == 7)) && (M == 0)) {
        // 0100100XXX000RRR (X in (2, 3, 7)) ext.S/extb.l Dn
        switch (b) {
          case 2: // ext.w (byte to word)
            return visitor.on_ext_byte_word(Xn);
          case 3: // ext.l (word to long)
            return visitor.on_ext_word_long(Xn);
          case 7: // extb.l (byte to long)
            return visitor.on_ext_byte_long(Xn);
          default:
            throw std::logic_error("Unhandled ext case");
        }

      } else if (opcode & 0x0100) {
        // All valid opcodes that have this bit set are special cases above
        return visitor.on_invalid("Invalid opcode 4 with bit 8 set");

      } else if ((a < 4) && (size == Size::INVALID)) {
        auto addr = decode_address(M, Xn, ((a + 1) & 2) ? Size::BYTE : Size::WORD, false);
        if ((a < 2) ? !addr.is_data_alterable_mode() : !addr.is_data_mode()) {
          return visitor.on_invalid(nullptr, &addr);
        }
        switch (a) {
          case 0: // 0100000011MMMRRR (DATA ALTERABLE) move DEST, SR
            return visitor.on_move_dest_sr(addr);
          case 1: // 0100001011MMMRRR (DATA ALTERABLE) move DEST, CCR
            return visitor.on_move_dest_ccr(addr);
          case 2: // 0100010011MMMRRR (DATA) move CCR, SRC
            return visitor.on_move_ccr_src(addr);
          case 3: // 0100011011MMMRRR (DATA) move SR, SRC
            return visitor.on_move_sr_src(addr);
          default:
            throw std::logic_error("Unhandled sr/ccr move case");
        }
      } else if (a < 4) {
        auto addr = decode_address(M, Xn, size, false);
        if (!addr.is_data_alterable_mode()) {
          return visitor.on_invalid(nullptr, &addr);
        }
        switch (a) {
          case 0: // 01000000SSMMMRRR (S<3 and DATA ALTERABLE) negx.S DEST
            return visitor.on_negx(size, addr);
          case 1: // 01000010SSMMMRRR (S<3 and DATA ALTERABLE) clr.S DEST
            return visitor.on_clr(size, addr);
          case 2: // 01000100SSMMMRRR (S<3 and DATA ALTERABLE) neg.S DEST
            return visitor.on_neg(size, addr);
          case 3: // 01000110SSMMMRRR (S<3 and DATA ALTERABLE) not.S DEST
            return visitor.on_not(size, addr);
          default:
            throw std::logic_error("Unhandled negx/clr/neg/not case");
        }
      } else {
        switch (a) {
          case 4:
            if (b == 0) {
              if (M == 1) { // 0100100000001RRR disp32 link An
                return visitor.on_link(Xn, visitor.read_ins_s32(4));
              } else { // 0100100000MMMRRR (DATA ALTERABLE) nbcd
                auto addr = decode_address(M, Xn, Size::BYTE, true);
                if (!addr.is_data_alterable_mode()) {
                  return visitor.on_invalid(nullptr, &addr);
                }
                return visitor.on_nbcd(addr);
              }
            } else if (b == 1) {
              if (M == 0) { // 0100100001000RRR swap.w Dn
                return visitor.on_swap(Xn);
              } else if (M == 1) { // 0100100001001VVV bkpt
                return visitor.on_bkpt(Xn);
              } else if ((M == 2) || (M >= 5)) { // 0100100001MMMRRR (CONTROL) pea.l
                auto addr = decode_address(M, Xn, Size::LONG, false);
                if (!addr.is_control_mode()) {
                  return visitor.on_invalid(nullptr, &addr);
                }
                return visitor.on_pea(addr);
              } else {
                return visitor.on_invalid("Invalid opcode 4/4/1");
              }
            } else {
              return visitor.on_invalid("Invalid opcode 4/4");
            }
            throw std::logic_error("Failed to decode any case of opcode 4/4");
          case 5: {
            if (size != Size::INVALID) { // 01001010SSMMMRRR (S<3) tst.S DEST
              return visitor.on_tst(size, decode_address(M, Xn, size, false));
            } else if (M != 7 || Xn < 2) { // 0100101011MMMRRR (DATA ALTERABLE) tas DEST
              auto addr = decode_address(M, Xn, Size::BYTE, false);
              if (!addr.is_data_alterable_mode()) {
                return visitor.on_invalid(nullptr, &addr);
              }
              return visitor.on_tas(addr);
            } else if (Xn == 2) { // 0100101011111010 bgnd
              return visitor.on_bgnd();
            } else if (Xn == 4) { // 0100101011111100 illegal
              return visitor.on_illegal();
            } else {
              return visitor.on_invalid("Invalid opcode 4/5");
            }
            throw std::logic_error("Failed to decode any case of opcode 4/5");
          }
          case 6: {
            uint16_t ext = visitor.read_ins_u16();
            auto addr = decode_address(M, Xn, Size::LONG, false);
            if (ext & 0x83F8) {
              return visitor.on_invalid("Arguments word has reserved bits set", &addr);
            }
            if (!addr.is_data_mode()) {
              return visitor.on_invalid(nullptr, &addr);
            }
            uint8_t reg_high = ext & 7;
            uint8_t reg_low = (ext >> 12) & 7;
            bool is_signed = (ext >> 11) & 1;
            bool is_64 = (ext >> 10) & 1;
            if (b == 0) {
              if (is_signed) { // 0100110000MMMRRR 0LLL1S0000000HHH (DATA) muls.S ...
                return visitor.on_muls_long(is_64, addr, reg_high, reg_low);
              } else { // 0100110000MMMRRR 0LLL0S0000000HHH (DATA) mulu.S ...
                return visitor.on_mulu_long(is_64, addr, reg_high, reg_low);
              }
            } else if (b == 1) {
              if (is_signed) { // 0100110001MMMRRR 0LLL1S0000000HHH (DATA) divs.S ...
                return visitor.on_divs_long(is_64, addr, reg_high, reg_low);
              } else { // 0100110001MMMRRR 0LLL0S0000000HHH (DATA) divu.S ...
                return visitor.on_divu_long(is_64, addr, reg_high, reg_low);
              }
            } else {
              return visitor.on_invalid("Invalid opcode 4/6", &addr);
            }
            throw std::logic_error("Failed to decode any case of opcode 4/6");
          }
          case 7:
            if ((b & 6) == 2) {
              // 0100111010MMMRRR (M=2 or M>=5) jsr DEST
              // 0100111011MMMRRR (M=2 or M>=5) jmp DEST
              auto addr = decode_address(M, Xn, Size::LONG, false);
              if (!addr.is_control_mode()) {
                return visitor.on_invalid(nullptr, &addr);
              }
              return visitor.on_jsr_jmp(addr, !(b & 1));
            } else if (b == 1) {
              switch (M) {
                case 0:
                case 1: // 010011100100VVVV trap
                  return visitor.on_trap(opcode & 0x000F);
                case 2: // 0100111001010RRR disp16 link An, DISP
                  return visitor.on_link(Xn, visitor.read_ins_s16(2));
                case 3: // 0100111001011RRR unlink
                  return visitor.on_unlink(Xn);
                case 4:
                case 5: // 010011100110DRRR move usp
                  return visitor.on_move_usp(M & 1, Xn);
                case 6:
                  switch (Xn) {
                    case 0: // 0100111001110000 reset
                      return visitor.on_reset();
                    case 1: // 0100111001110001 nop
                      return visitor.on_nop();
                    case 2: // 0100111001110010 imm16 stop IMM
                      return visitor.on_stop(visitor.read_ins_u16(2));
                    case 3: // 0100111001110011 rte
                      return visitor.on_rte();
                    case 4: // 0100111001110100 disp16 rtd DISP
                      return visitor.on_rtd(visitor.read_ins_s16(2));
                    case 5: // 0100111001110101 rts
                      return visitor.on_rts();
                    case 6: // 0100111001110110 trapv
                      return visitor.on_trapv();
                    case 7: // 0100111001110111 rtr
                      return visitor.on_rtr();
                  }
                  throw std::logic_error("Invalid value for Xn");
                case 7: // 010011100111101D ARRRCCCCCCCCCCCC movec ...
                  if ((Xn & 6) != 2) {
                    return visitor.on_invalid("Invalid opcode 4/7/7");
                  }
                  uint16_t args = visitor.read_ins_u16(2);
                  return visitor.on_movec(Xn & 1, args & 0x8000, (args >> 12) & 7, args & 0x0FFF);
              }
              throw std::logic_error("Invalid value for M");
            } else {
              return visitor.on_invalid("Invalid opcode 4/7");
            }
        }
        throw std::logic_error("Invalid value for a");
      }
      throw std::logic_error("Failed to decode any case of opcode 4");

    case 0x5000:
      if (size != Size::INVALID) {
        // 0101DDD0SSMMMRRR (S<3 and ALTERABLE) addq
        // 0101DDD1SSMMMRRR (S<3 and ALTERABLE) subq
        auto addr = decode_address(M, Xn, size, true);
        if (!addr.is_alterable_mode()) {
          return visitor.on_invalid(nullptr, &addr);
        }
        if (a == 0) {
          a = 8;
        }
        return visitor.on_addq_subq(size, addr, (b & 4) ? (-a) : a);

      } else if (M == 1) { // 0101CCCC11001RRR disp16 dbcc
        return visitor.on_dbcc((opcode >> 8) & 0xF, Xn, visitor.read_ins_s16(2));

      } else if ((M == 7) && (Xn >= 2) && (Xn <= 4)) { // 0101CCCC11111XXX imm16/32 (X in (2, 3, 4)) trapcc
        if (Xn == 2) {
          return visitor.on_trapcc((opcode >> 8) & 0xF, -1);
        } else if (Xn == 3) {
          return visitor.on_trapcc((opcode >> 8) & 0xF, visitor.read_ins_u16(2));
        } else {
          return visitor.on_trapcc((opcode >> 8) & 0xF, visitor.read_ins_u32(4));
        }

      } else { // 0101CCCC11MMMRRR (DATA ALTERABLE) scc
        auto addr = decode_address(M, Xn, Size::BYTE, false);
        if (!addr.is_data_alterable_mode()) {
          return visitor.on_invalid(nullptr, &addr);
        }
        return visitor.on_scc((opcode >> 8) & 0xF, addr);
      }
      throw std::logic_error("Failed to decode any case of opcode 5");

    case 0x6000: {
      // 01100000DDDDDDDD disp16/32 bra
      // 01100001DDDDDDDD disp16/32 bsr
      // 0110CCCCDDDDDDDD disp16/32 bcc

      uint8_t condition = (opcode >> 8) & 0xF;

      int32_t disp = static_cast<int8_t>(opcode & 0xFF);
      uint8_t disp_size = 0;
      if (disp == 0) {
        disp = visitor.read_ins_s16(2);
        disp_size = 2;
      } else if (disp == -1) {
        disp = visitor.read_ins_s32(4);
        disp_size = 4;
      }

      if (condition == 0) {
        return visitor.on_bra(disp, disp_size);
      } else if (condition == 1) {
        return visitor.on_bsr(disp, disp_size);
      } else {
        return visitor.on_bcc(condition, disp, disp_size);
      }

      throw std::logic_error("Failed to decode any case of opcode 6");
    }

    case 0x7000: // 0111RRR0VVVVVVVV moveq
      if (b & 4) {
        return visitor.on_invalid("Invalid opcode 7/1");
      }
      return visitor.on_moveq(a, static_cast<int8_t>(opcode & 0x00FF));

    case 0x8000:
      if ((M < 2) && (b == 4)) { // 1000XXX10000WRRR sbcd
        return visitor.on_sbcd(M & 1, a, Xn);

      } else if ((M < 2) && (b == 5)) { // 1000XXX10100WRRR imm16 pack
        return visitor.on_pack(M & 1, a, Xn, visitor.read_ins_s16(2));

      } else if ((M < 2) && (b == 6)) { // 1000XXX11000WRRR imm16 unpack
        return visitor.on_unpack(M & 1, a, Xn, visitor.read_ins_s16(2));

      } else if ((b & 3) == 3) {
        // 1000XXX011MMMRRR (DATA) divu (word)
        // 1000XXX111MMMRRR (DATA) divs (word)
        bool is_signed = (b & 4);
        auto addr = decode_address(M, Xn, Size::WORD, is_signed);
        if (!addr.is_data_mode()) {
          return visitor.on_invalid(nullptr, &addr);
        }
        return is_signed ? visitor.on_divs_word(addr, a) : visitor.on_divu_word(addr, a);

      } else { // 1000XXXZZZMMMRRR (Z not in (3, 7) and src DATA and dst MEMORY ALTERABLE) or
        auto addr = decode_address(M, Xn, size, false);
        bool dest_is_memory = (b & 4);
        if (dest_is_memory ? !addr.is_memory_alterable_mode() : !addr.is_data_mode()) {
          return visitor.on_invalid(nullptr, &addr);
        }
        return visitor.on_or(size, addr, a, dest_is_memory);
      }
      throw std::logic_error("Failed to decode any case of opcode 8");

    case 0x9000:
    case 0xD000: {
      bool is_add = (opcode & 0xF000) == 0xD000;
      if ((M < 2) && (b & 4) && (b != 7)) {
        // 1001YYY1SS00WXXX (S<3) subx
        // 1101YYY1SS00WXXX (S<3) addx
        return visitor.on_addx_subx(size, M, a, Xn, is_add);
      } else if ((b & 3) == 3) {
        // 1001YYYXXXMMMRRR (X in (3, 7)) suba
        // 1101YYYXXXMMMRRR (X in (3, 7)) adda
        auto addr = decode_address(M, Xn, (b & 4) ? Size::LONG : Size::WORD, true);
        return visitor.on_adda_suba(b & 4, addr, a, is_add);
      } else {
        // 1001YYYXXXMMMRRR (X not in (3, 7) and src ALL, dest MEMORY ALTERABLE) sub
        // 1101YYYXXXMMMRRR (X not in (3, 7) and src ALL, dest MEMORY ALTERABLE) add
        auto addr = decode_address(M, Xn, size, true);
        if ((b & 4) && !addr.is_memory_alterable_mode()) {
          return visitor.on_invalid(nullptr, &addr);
        }
        return visitor.on_add_sub(size, addr, a, b & 4, is_add);
      }
      throw std::logic_error("Failed to decode any case of opcode 9D");
    }

    case 0xA000:
      return visitor.on_syscall(opcode);

    case 0xB000:
      if ((M == 1) && (b & 4) && (size != Size::INVALID)) { // 1101XXX1SS001YYY (S<3) cmpm
        return visitor.on_cmpm(size, a, Xn);
      } else if (b < 3) { // 1101XXXZZZMMMRRR (Z<3) cmp
        return visitor.on_cmp(size, a, decode_address(M, Xn, size, true));
      } else if ((b & 3) == 3) { // 1101XXXZZZMMMRRR (Z in (3, 7)) cmpa
        return visitor.on_cmpa(b & 4, a, decode_address(M, Xn, (b & 4) ? Size::LONG : Size::WORD, true));
      } else { // 1101XXXZZZMMMRRR (Z in (4, 5, 6) and DATA ALTERABLE) xor
        auto addr = decode_address(M, Xn, size, true);
        if (!addr.is_data_alterable_mode()) {
          return visitor.on_invalid(nullptr, &addr);
        }
        return visitor.on_xor(size, a, addr);
      }
      throw std::logic_error("Failed to decode any case of opcode B");

    case 0xC000:
      if ((b & 3) == 3) {
        // 1100RRR011MMMRRR (DATA) mulu
        // 1100RRR111MMMRRR (DATA) muls
        bool is_signed = (b & 4);
        auto addr = decode_address(M, Xn, Size::WORD, is_signed);
        if (!addr.is_data_mode()) {
          return visitor.on_invalid(nullptr, &addr);
        }
        return is_signed ? visitor.on_muls_word(addr, a) : visitor.on_mulu_word(addr, a);

      } else if ((b == 4) && (M < 2)) { // 1100XXX10000WYYY abcd
        return visitor.on_abcd(M, a, Xn);

      } else if ((b == 5) && (M == 0)) { // 1100XXX101000YYY exg Dx, Dy
        return visitor.on_exg_d_d(a, Xn);

      } else if ((b == 5) && (M == 1)) { // 1100XXX101001YYY exg Ax, Ay
        return visitor.on_exg_a_a(a, Xn);

      } else if ((b == 6) && (M == 1)) { // 1100XXX110001YYY exg Dx, Ay
        return visitor.on_exg_d_a(a, Xn);

      } else { // 1100XXXZZZMMMRRR (Z not in (3, 7) and src DATA, dest MEMORY ALTERABLE) and
        bool dest_is_mem = (b & 4);
        auto addr = decode_address(M, Xn, size, false);
        if (dest_is_mem ? !addr.is_memory_alterable_mode() : !addr.is_data_mode()) {
          return visitor.on_invalid(nullptr, &addr);
        }
        return visitor.on_and(size, addr, a, dest_is_mem);
      }
      throw std::logic_error("Failed to decode any case of opcode C");

    case 0xE000:
      if ((size == Size::INVALID) && (a & 4)) {
        // 1110100011MMMRRR 0000oZZZZZwWWWWW (D_REG or CONTROL) bftst
        // 1110100111MMMRRR 0XXXoZZZZZwWWWWW (D_REG or CONTROL) bfextu
        // 1110101011MMMRRR 0000oZZZZZwWWWWW (D_REG or CONTROL ALTERABLE) bfchg
        // 1110101111MMMRRR 0XXXoZZZZZwWWWWW (D_REG or CONTROL) bfexts
        // 1110110011MMMRRR 0000oZZZZZwWWWWW (D_REG or CONTROL ALTERABLE) bfclr
        // 1110110111MMMRRR 0XXXoZZZZZwWWWWW (D_REG or CONTROL) bfffo
        // 1110111011MMMRRR 0000oZZZZZwWWWWW (D_REG or CONTROL ALTERABLE) bfset
        // 1110111111MMMRRR 0XXXoZZZZZwWWWWW (D_REG or CONTROL ALTERABLE) bfins
        uint8_t which = (opcode >> 8) & 7;
        uint16_t args = visitor.read_ins_u16();
        bool alterable_required = (which == 2) || (which == 4) || (which >= 6);
        auto addr = decode_address(M, Xn, Size::BYTE, false);
        if ((addr.mode != AM::D_REG) &&
            (alterable_required ? !addr.is_control_alterable_mode() : !addr.is_control_mode())) {
          return visitor.on_invalid(nullptr, &addr);
        }
        bool offset_is_reg = args & 0x0800;
        bool width_is_reg = args & 0x0040;
        return visitor.on_bf_ops(which, addr, (args >> 12) & 7, offset_is_reg,
            (args >> 6) & (offset_is_reg ? 7 : 0x1F), width_is_reg, args & (width_is_reg ? 7 : 0x1F));

      } else if (size == Size::INVALID) {
        // 1110000G11MMMRRR (MEMORY ALTERABLE) asl/asr
        // 1110001G11MMMRRR (MEMORY ALTERABLE) lsl/lsr
        // 1110010G11MMMRRR (MEMORY ALTERABLE) roxl/roxr
        // 1110011G11MMMRRR (MEMORY ALTERABLE) rol/ror
        uint8_t which = (opcode >> 8) & 7;
        auto addr = decode_address(M, Xn, Size::BYTE, false);
        if (!addr.is_memory_alterable_mode()) {
          return visitor.on_invalid(nullptr, &addr);
        }
        return visitor.on_bit_shift_mem(which, addr);

      } else {
        // 1110CCCGSSI00RRR (S<3) asl/asr
        // 1110CCCGSSI01RRR (S<3) lsl/lsr
        // 1110CCCGSSI10RRR (S<3) roxl/roxr
        // 1110CCCGSSI11RRR (S<3) rol/ror
        uint8_t which = ((opcode >> 2) & 6) | ((opcode >> 8) & 1); // Same values as for memory bit shifts
        bool count_is_reg = M & 4;
        uint8_t count = (!count_is_reg && (a == 0)) ? 8 : a;
        return visitor.on_bit_shift_reg(which, size, Xn, count_is_reg, count);
      }
      throw std::logic_error("Failed to decode any case of opcode E");

    case 0xF000: {
      if (a == 1) {
        switch (b) {
          case 0: {
            uint16_t args = visitor.read_ins_u16();
            uint8_t which = (args >> 13) & 7;
            uint8_t u = (args >> 10) & 7;
            uint8_t r = (args >> 7) & 7;
            uint8_t k = args & 0x7F;
            if ((which == 2) && (u == 7)) { // 1111WWW000000000 010111rrrKKKKKKK (W>0 (default 1)) fmovecr
              return visitor.on_fmovecr(r, k);

            } else if (which == 3) {
              // 1111WWW000MMMRRR 011uuurrrKKKKKKK (DATA ALTERABLE) fmove (DATA REGISTER, REGISTER TO MEMORY)
              auto addr = decode_address(M, Xn, Size::LONG, false);
              if (!addr.is_data_alterable_mode()) {
                return visitor.on_invalid(nullptr, &addr);
              }
              return visitor.on_fmove_to_mem(addr, r, static_cast<ValueType>(u), k);

            } else if ((which & 5) == 0) {
              auto addr = decode_address(M, Xn, Size::LONG, false);
              if (!addr.is_data_mode()) {
                return visitor.on_invalid(nullptr, &addr);
              }
              switch (k) {
                case 0b0000000: // 1111WWW000MMMRRR 0g0uuurrr0000000 (W>0 (default 1) and u<7 and DATA) fmove
                  return visitor.on_fmove(r, u, addr, which & 2);
                case 0b0000001: // 1111WWW000MMMRRR 0g0uuurrr0000001 (W>0 (default 1) and u<7 and DATA) fint
                  return visitor.on_fint(r, u, addr, which & 2);
                case 0b0000010: // 1111WWW000MMMRRR 0g0uuurrr0000010 (W>0 (default 1) and u<7 and DATA) fsinh
                  return visitor.on_fsinh(r, u, addr, which & 2);
                case 0b0000011: // 1111WWW000MMMRRR 0g0uuurrr0000011 (W>0 (default 1) and u<7 and DATA) fintrz
                  return visitor.on_fintrz(r, u, addr, which & 2);
                case 0b0000100: // 1111WWW000MMMRRR 0g0uuurrr0000100 (W>0 (default 1) and u<7 and DATA) fsqrt
                  return visitor.on_fsqrt(r, u, addr, which & 2);
                case 0b0000110: // 1111WWW000MMMRRR 0g0uuurrr0000110 (W>0 (default 1) and u<7 and DATA) flognp1
                  return visitor.on_flognp1(r, u, addr, which & 2);
                case 0b0001000: // 1111WWW000MMMRRR 0g0uuurrr0001000 (W>0 (default 1) and u<7 and DATA) fetoxm1
                  return visitor.on_fetoxm1(r, u, addr, which & 2);
                case 0b0001001: // 1111WWW000MMMRRR 0g0uuurrr0001001 (W>0 (default 1) and u<7 and DATA) ftanh
                  return visitor.on_ftanh(r, u, addr, which & 2);
                case 0b0001010: // 1111WWW000MMMRRR 0g0uuurrr0001010 (W>0 (default 1) and u<7 and DATA) fatan
                  return visitor.on_fatan(r, u, addr, which & 2);
                case 0b0001100: // 1111WWW000MMMRRR 0g0uuurrr0001100 (W>0 (default 1) and u<7 and DATA) fasin
                  return visitor.on_fasin(r, u, addr, which & 2);
                case 0b0001101: // 1111WWW000MMMRRR 0g0uuurrr0001101 (W>0 (default 1) and u<7 and DATA) fatanh
                  return visitor.on_fatanh(r, u, addr, which & 2);
                case 0b0001110: // 1111WWW000MMMRRR 0g0uuurrr0001110 (W>0 (default 1) and u<7 and DATA) fsin
                  return visitor.on_fsin(r, u, addr, which & 2);
                case 0b0001111: // 1111WWW000MMMRRR 0g0uuurrr0001111 (W>0 (default 1) and u<7 and DATA) ftan
                  return visitor.on_ftan(r, u, addr, which & 2);
                case 0b0010000: // 1111WWW000MMMRRR 0g0uuurrr0010000 (W>0 (default 1) and u<7 and DATA) fetox
                  return visitor.on_fetox(r, u, addr, which & 2);
                case 0b0010001: // 1111WWW000MMMRRR 0g0uuurrr0010001 (W>0 (default 1) and u<7 and DATA) ftwotox
                  return visitor.on_ftwotox(r, u, addr, which & 2);
                case 0b0010010: // 1111WWW000MMMRRR 0g0uuurrr0010010 (W>0 (default 1) and u<7 and DATA) ftentox
                  return visitor.on_ftentox(r, u, addr, which & 2);
                case 0b0010100: // 1111WWW000MMMRRR 0g0uuurrr0010100 (W>0 (default 1) and u<7 and DATA) flogn
                  return visitor.on_flogn(r, u, addr, which & 2);
                case 0b0010101: // 1111WWW000MMMRRR 0g0uuurrr0010101 (W>0 (default 1) and u<7 and DATA) flog10
                  return visitor.on_flog10(r, u, addr, which & 2);
                case 0b0010110: // 1111WWW000MMMRRR 0g0uuurrr0010110 (W>0 (default 1) and u<7 and DATA) flog2
                  return visitor.on_flog2(r, u, addr, which & 2);
                case 0b0011000: // 1111WWW000MMMRRR 0g0uuurrr0011000 (W>0 (default 1) and u<7 and DATA) fabs
                  return visitor.on_fabs(r, u, addr, which & 2);
                case 0b0011001: // 1111WWW000MMMRRR 0g0uuurrr0011001 (W>0 (default 1) and u<7 and DATA) fcosh
                  return visitor.on_fcosh(r, u, addr, which & 2);
                case 0b0011010: // 1111WWW000MMMRRR 0g0uuurrr0011010 (W>0 (default 1) and u<7 and DATA) fneg
                  return visitor.on_fneg(r, u, addr, which & 2);
                case 0b0011100: // 1111WWW000MMMRRR 0g0uuurrr0011100 (W>0 (default 1) and u<7 and DATA) facos
                  return visitor.on_facos(r, u, addr, which & 2);
                case 0b0011101: // 1111WWW000MMMRRR 0g0uuurrr0011101 (W>0 (default 1) and u<7 and DATA) fcos
                  return visitor.on_fcos(r, u, addr, which & 2);
                case 0b0011110: // 1111WWW000MMMRRR 0g0uuurrr0011110 (W>0 (default 1) and u<7 and DATA) fgetexp
                  return visitor.on_fgetexp(r, u, addr, which & 2);
                case 0b0011111: // 1111WWW000MMMRRR 0g0uuurrr0011111 (W>0 (default 1) and u<7 and DATA) fgetman
                  return visitor.on_fgetman(r, u, addr, which & 2);
                case 0b0100000: // 1111WWW000MMMRRR 0g0uuurrr0100000 (W>0 (default 1) and u<7 and DATA) fdiv
                  return visitor.on_fdiv(r, u, addr, which & 2);
                case 0b0100010: // 1111WWW000MMMRRR 0g0uuurrr0100010 (W>0 (default 1) and u<7 and DATA) fadd
                  return visitor.on_fadd(r, u, addr, which & 2);
                case 0b0100011: // 1111WWW000MMMRRR 0g0uuurrr0100011 (W>0 (default 1) and u<7 and DATA) fmul
                  return visitor.on_fmul(r, u, addr, which & 2);
                case 0b0100100: // 1111WWW000MMMRRR 0g0uuurrr0100100 (W>0 (default 1) and u<7 and DATA) fsgldiv
                  return visitor.on_fsgldiv(r, u, addr, which & 2);
                case 0b0100101: // 1111WWW000MMMRRR 0g0uuurrr0100101 (W>0 (default 1) and u<7 and DATA) frem
                  return visitor.on_frem(r, u, addr, which & 2);
                case 0b0100110: // 1111WWW000MMMRRR 0g0uuurrr0100110 (W>0 (default 1) and u<7 and DATA) fscale
                  return visitor.on_fscale(r, u, addr, which & 2);
                case 0b0100111: // 1111WWW000MMMRRR 0g0uuurrr0100111 (W>0 (default 1) and u<7 and DATA) fsglmul
                  return visitor.on_fsglmul(r, u, addr, which & 2);
                case 0b0101000: // 1111WWW000MMMRRR 0g0uuurrr0101000 (W>0 (default 1) and u<7 and DATA) fsub
                  return visitor.on_fsub(r, u, addr, which & 2);
                case 0b0101101: // 1111WWW000MMMRRR 0g0uuurrr0101101 (W>0 (default 1) and u<7 and DATA) fmod
                  return visitor.on_fmod(r, u, addr, which & 2);
                case 0b0110000:
                case 0b0110001:
                case 0b0110010:
                case 0b0110011:
                case 0b0110100:
                case 0b0110101:
                case 0b0110110:
                case 0b0110111:
                  // 1111WWW000MMMRRR 0g0uuurrr0110ccc (W>0 (default 1) and u<7 and DATA) fsincos
                  return visitor.on_fsincos(r, k & 7, u, addr, which & 2);
                case 0b0111000: // 1111WWW000MMMRRR 0g0uuurrr0111000 (W>0 (default 1) and u<7 and DATA) fcmp
                  return visitor.on_fcmp(r, u, addr, which & 2);
                case 0b0111010: // 1111WWW000MMMRRR 0g0uuurrr0111010 (W>0 (default 1) and u<7 and DATA) ftst
                  return visitor.on_ftst(r, u, addr, which & 2);
                case 0b1000000: // 1111WWW000MMMRRR 0g0uuurrr1000000 (W>0 (default 1) and u<7 and DATA) fsmove
                  return visitor.on_fsmove(r, u, addr, which & 2);
                case 0b1000001: // 1111WWW000MMMRRR 0g0uuurrr1000001 (W>0 (default 1) and u<7 and DATA) fssqrt
                  return visitor.on_fssqrt(r, u, addr, which & 2);
                case 0b1000100: // 1111WWW000MMMRRR 0g0uuurrr1000100 (W>0 (default 1) and u<7 and DATA) fdmove
                  return visitor.on_fdmove(r, u, addr, which & 2);
                case 0b1000101: // 1111WWW000MMMRRR 0g0uuurrr1000101 (W>0 (default 1) and u<7 and DATA) fdsqrt
                  return visitor.on_fdsqrt(r, u, addr, which & 2);
                case 0b1011000: // 1111WWW000MMMRRR 0g0uuurrr1011000 (W>0 (default 1) and u<7 and DATA) fsabs
                  return visitor.on_fsabs(r, u, addr, which & 2);
                case 0b1011010: // 1111WWW000MMMRRR 0g0uuurrr1011010 (W>0 (default 1) and u<7 and DATA) fneg
                  return visitor.on_fsneg(r, u, addr, which & 2);
                case 0b1011100: // 1111WWW000MMMRRR 0g0uuurrr1011100 (W>0 (default 1) and u<7 and DATA) fdabs
                  return visitor.on_fdabs(r, u, addr, which & 2);
                case 0b1011110: // 1111WWW000MMMRRR 0g0uuurrr1011110 (W>0 (default 1) and u<7 and DATA) fneg
                  return visitor.on_fdneg(r, u, addr, which & 2);
                case 0b1100000: // 1111WWW000MMMRRR 0g0uuurrr1100000 (W>0 (default 1) and u<7 and DATA) fsdiv
                  return visitor.on_fsdiv(r, u, addr, which & 2);
                case 0b1100010: // 1111WWW000MMMRRR 0g0uuurrr1100010 (W>0 (default 1) and u<7 and DATA) fsadd
                  return visitor.on_fsadd(r, u, addr, which & 2);
                case 0b1100011: // 1111WWW000MMMRRR 0g0uuurrr1100011 (W>0 (default 1) and u<7 and DATA) fsmul
                  return visitor.on_fsmul(r, u, addr, which & 2);
                case 0b1100100: // 1111WWW000MMMRRR 0g0uuurrr1100100 (W>0 (default 1) and u<7 and DATA) fddiv
                  return visitor.on_fddiv(r, u, addr, which & 2);
                case 0b1100110: // 1111WWW000MMMRRR 0g0uuurrr1100110 (W>0 (default 1) and u<7 and DATA) fdadd
                  return visitor.on_fdadd(r, u, addr, which & 2);
                case 0b1100111: // 1111WWW000MMMRRR 0g0uuurrr1100111 (W>0 (default 1) and u<7 and DATA) fdmul
                  return visitor.on_fdmul(r, u, addr, which & 2);
                case 0b1101000: // 1111WWW000MMMRRR 0g0uuurrr1101000 (W>0 (default 1) and u<7 and DATA) fssub
                  return visitor.on_fssub(r, u, addr, which & 2);
                case 0b1101100: // 1111WWW000MMMRRR 0g0uuurrr1101100 (W>0 (default 1) and u<7 and DATA) fdsub
                  return visitor.on_fdsub(r, u, addr, which & 2);
                default:
                  return visitor.on_invalid("Invalid opcode F/1/0/0,2", &addr);
              }

            } else if ((which & 6) == 4) {
              // 1111WWW000MMMRRR 10Vuuu0000000000 (W>0 (default 1) and from memory CONTROL or POSTINC, to memory CONTROL ALTERABLE or PREDEC) fmovem (CONTROL REGISTERS)
              // 1111WWW000MMMRRR 10Vuuu0000000000 (W>0 (default 1) and u in (1, 2, 4) and from memory ANY, to memory ALTERABLE) fmove (SYSTEM CONTROL REGISTER)
              bool is_write = which & 1;
              auto addr = decode_address(M, Xn, Size::LONG, false);
              // It seems the only difference between these two opcodes is whether multiple bits are set in the u field
              // or not. Since this affects the allowed addressing modes, we use that condition here in decoding.
              if ((u & (u - 1))
                      ? (is_write
                                ? (!addr.is_control_alterable_mode() && (addr.mode != AM::MEM_A_PREDEC))
                                : (!addr.is_control_mode() && (addr.mode != AM::MEM_A_POSTINC)))
                      : (is_write && !addr.is_alterable_mode())) {
                return visitor.on_invalid(nullptr, &addr);
              }
              return visitor.on_fmovem_control_regs(addr, u, is_write);

            } else if ((which & 6) == 6) {
              // 1111WWW000MMMRRR 11VEE000ZZZZZZZZ (W>0 (default 1) and from memory CONTROL or POSTINC, to memory CONTROL ALTERABLE or PREDEC) fmovem (DATA REGISTERS)
              bool is_write = which & 1;
              auto addr = decode_address(M, Xn, Size::LONG, false);
              if (is_write
                      ? (!addr.is_control_alterable_mode() && (addr.mode != AM::MEM_A_PREDEC))
                      : (!addr.is_control_mode() && (addr.mode != AM::MEM_A_POSTINC))) {
                return visitor.on_invalid(nullptr, &addr);
              }
              if ((u & 4) ? (addr.mode != AM::MEM_A_PREDEC) : (addr.mode == AM::MEM_A_PREDEC)) {
                return visitor.on_invalid("Operation mode for fmovem and addressing mode do not agree", &addr);
              }
              return visitor.on_fmovem_data_regs(addr, u & 2, args & 0xFF, is_write);

            } else {
              return visitor.on_invalid("Invalid opcode F/1/0");
            }
            throw std::logic_error("Failed to decode any case of opcode F/1/0");
          }
          case 1: {
            uint16_t args = visitor.read_ins_u16();
            if (args & 0xFFC0) {
              return visitor.on_invalid("Arguments word has reserved bits set");
            } else if (M == 1) { // 1111WWW001001RRR 0000000000XXXXXX disp16 (W>0) fdbcc
              return visitor.on_fdbcc(args, Xn, visitor.read_ins_s16(2));
            } else if ((M == 7) && (Xn >= 2) && (Xn <= 4)) {
              // 1111WWW001111EEE 0000000000XXXXXX imm16/32 (W>0 and E in (2, 3, 4)) ftrapcc
              if (Xn == 2) {
                return visitor.on_ftrapcc(args, -1);
              } else if (Xn == 3) {
                return visitor.on_ftrapcc(args, visitor.read_ins_u16(2));
              } else {
                return visitor.on_ftrapcc(args, visitor.read_ins_u32(4));
              }
            } else { // 1111WWW001MMMRRR 0000000000XXXXXX (W>0 and DATA ALTERABLE) fscc
              auto addr = decode_address(M, Xn, Size::BYTE, false);
              if (!addr.is_data_alterable_mode()) {
                return visitor.on_invalid(nullptr, &addr);
              }
              return visitor.on_fscc(args, addr);
            }
            throw std::logic_error("Failed to decode any case of opcode F/1/1");
          }
          case 2:
          case 3: {
            // 1111WWW010000000 0000000000000000 (W>0) fnop
            // 1111WWW01SXXXXXX disp16/32 (W>0) fbcc
            int32_t disp;
            uint8_t disp_size = 0;
            if (b & 1) {
              disp = visitor.read_ins_s32(4);
              disp_size = 4;
            } else {
              disp = visitor.read_ins_s16(2);
              disp_size = 2;
            }
            return visitor.on_fbcc(opcode & 0x003F, disp, disp_size);
          }
          case 4: { // 1111WWW100MMMRRR (W>0 and CONTROL ALTERABLE or PREDEC) fsave
            auto addr = decode_address(M, Xn, Size::LONG, false);
            if (!addr.is_control_alterable_mode() && (addr.mode != AM::MEM_A_PREDEC)) {
              return visitor.on_invalid(nullptr, &addr);
            }
            return visitor.on_fsave(addr);
          }
          case 5: { // 1111WWW101MMMRRR (W>0 and CONTROL or POSTINC) frestore
            auto addr = decode_address(M, Xn, Size::LONG, false);
            if (!addr.is_control_mode() && (addr.mode != AM::MEM_A_POSTINC)) {
              return visitor.on_invalid(nullptr, &addr);
            }
            return visitor.on_frestore(addr);
          }
          default:
            return visitor.on_invalid("Invalid opcode F/1");
        }

      } else {
        // 1111000000MMMRRR 000PPPZ000000000 (P in (1, 3)) pmove (MC68EC030, ACx REGISTERS)
        // 1111000000MMMRRR 000PPPZF00000000 (P in (2, 3)) pmove (TT REGISTERS)
        // 1111000000MMMRRR 001000Z0000CCCCC (CONTROL ALTERABLE; additional restrictions on C) pload
        // 1111000000MMMRRR 0010100000000000 (CONTROL ALTERABLE) pvalid (MC68851)
        // 1111000000MMMRRR 0010100000000RRR (R>0? and CONTROL ALTERABLE) pvalid (MC68851)
        // 1111000000MMMRRR 001mmm00KKKCCCCC (m in (1, 4, 6) and CONTROL ALTERABLE; additional restrictions on C) pflush MC68030
        // 1111000000MMMRRR 001mmm0KKKKCCCCC (m in (1, 4, 5, 6, 7) and (K is 0 if M==1) and CONTROL ALTERABLE; additional restrictions on C) pflush(a/s) MC68851
        // 1111000000MMMRRR 010PPPZ000000000 pmove (MC68851, TO/FROM TC, CRP, DRP, SRP, CAL, VAL, SCC, AND AC REGISTERS)
        // 1111000000MMMRRR 010PPPZF00000000 (P in (0, 2, 3) and CONTROL ALTERABLE) pmove (MC68030 ONLY, SRP, CRP, AND TC REGISTERS)
        // 1111000000MMMRRR 011000Z000000000 pmove (MC68030 ONLY, MMUSR; MC68EC030, ACUSR)
        // 1111000000MMMRRR 011PPPZ000000000 pmove (MC68851, TO/FROM PSR AND PCSR REGISTERS)
        // 1111000000MMMRRR 011PPPZ0000NNN00 pmove (MC68851, TO/FROM BADX AND BACX REGISTERS)
        // 1111000000MMMRRR 100000Z0RRRCCCCC (CONTROL ALTERABLE; additional restrictions on C) ptest (MC68EC030)
        // 1111000000MMMRRR 100LLLZARRCCCCCC (CONTROL ALTERABLE; additional restrictions on C) ptest (MC68030)
        // 1111000000MMMRRR 100LLLZRRRCCCCCC (CONTROL ALTERABLE; additional restrictions on C) ptest (MC68851)
        // 1111000000MMMRRR 1010000000000000 (MEMORY) pflushr
        // 1111000001001RRR 0000000000XXXXXX disp16 (X<0x10) pdbcc
        // 1111000001111EEE 0000000000XXXXXX imm16/32 (E in (2, 3, 4) and X<0x10) ptrapcc
        // 1111000001MMMRRR 0000000000XXXXXX (X<0x10 and DATA ALTERABLE) pscc
        // 111100001SXXXXXX disp16/32 (X<0x10) pbcc
        // 1111000100MMMRRR (CONTROL or PREDEC) psave
        // 1111000101MMMRRR (CONTROL or POSTINC) prestore
        // 11110100HH0DDRRR cinv
        // 11110100HH1DDRRR cpush
        // 11110101000EERRR pflush (MC68EC040, POSTINCREMENT SOURCE AND DESTINATION; MC68040/MC68LC040)
        // 1111010101Z01RRR ptest (MC68040/MC68LC040; MC68EC040)
        // 11110110000EERRR addr32 move16 (ABSOLUTE LONG ADDRESS SOURCE OR DESTINATION)
        // 1111011000100XXX 1YYY000000000000 move16 (POSTINCREMENT SOURCE AND DESTINATION)
        // 1111100000000000 0000000111000000 imm16 lpstop
        // 1111100000000mmm 0XXX0r00SS000nnn (S<3) tblu/tblun (DATA REGISTER INTERPOLATE)
        // 1111100000000mmm 0XXX1r00SS000nnn (S<3) tbls/tblsn (DATA REGISTER INTERPOLATE)
        // 1111100000MMMRRR 0RRR0r01SS000000 (S<3 and CONTROL ALTERABLE) tblu/tblun (TABLE LOOKUP AND INTERPOLATE)
        // 1111100000MMMRRR 0RRR1r01SS000000 (S<3 and CONTROL ALTERABLE) tbls/tblsn (TABLE LOOKUP AND INTERPOLATE)
        // 1111WWW000MMMRRR JJJJJJJJJJJJJJJJ [...] (W>0) cpgen
        // 1111WWW001001RRR 0000000000XXXXXX [...] disp16 (W>0) cpdbcc
        // 1111WWW0011111EE 0000000000XXXXXX [...] imm16/32 (W>0 and E in (2, 3, 4)) cptrapcc
        // 1111WWW001MMMRRR 0000000000XXXXXX [...] (W>0 and DATA ALTERABLE) cpscc
        // 1111WWW01SXXXXXX [...] disp16/32 (W>0) cpbcc
        // 1111WWW100MMMRRR (W>0 and CONTROL ALTERABLE or PREDEC) cpsave
        // 1111WWW101MMMRRR (W>0 and CONTROL or POSTINC) cprestore
        // TODO: We don't implement decoding for any of these. Perhaps we should in the future.
        return visitor.on_coprocessor(opcode);
      }
      throw std::logic_error("Failed to decode any case of opcode F");
    }

    default:
      throw std::logic_error("Unhandled opcode type");
  }
}

M68KEmulator::Regs::Regs() {
  for (size_t x = 0; x < 8; x++) {
    this->a[x] = 0;
    this->d[x].u = 0;
  }
  this->pc = 0;
  this->sr.u = 0;
}

void M68KEmulator::Regs::import_state(FILE* stream) {
  uint8_t version = phosg::freadx<uint8_t>(stream);
  if (version > 1) {
    throw std::runtime_error("unknown format version");
  }

  for (size_t x = 0; x < 8; x++) {
    this->d[x].u = phosg::freadx<phosg::le_uint32_t>(stream);
  }
  for (size_t x = 0; x < 8; x++) {
    this->a[x] = phosg::freadx<phosg::le_uint32_t>(stream);
  }
  this->pc = phosg::freadx<phosg::le_uint32_t>(stream);
  this->sr.u = phosg::freadx<phosg::le_uint16_t>(stream);
  if (version == 0) {
    // Version 0 had two extra registers (debug read and write addresses). These no longer exist, so skip them.
    fseek(stream, 8, SEEK_CUR);
  }
}

void M68KEmulator::Regs::export_state(FILE* stream) const {
  phosg::fwritex(stream, 1); // version

  for (size_t x = 0; x < 8; x++) {
    phosg::fwritex<phosg::le_uint32_t>(stream, this->d[x].u);
  }
  for (size_t x = 0; x < 8; x++) {
    phosg::fwritex<phosg::le_uint32_t>(stream, this->a[x]);
  }
  phosg::fwritex<phosg::le_uint32_t>(stream, this->pc);
  phosg::fwritex<phosg::le_uint16_t>(stream, this->sr.u);
}

void M68KEmulator::Regs::set_by_name(const std::string& reg_name, uint32_t value) {
  if (reg_name.size() < 2) {
    throw std::invalid_argument("invalid register name");
  }
  uint8_t reg_num = strtoul(reg_name.data() + 1, nullptr, 10);
  if (reg_name.at(0) == 'a' || reg_name.at(0) == 'A') {
    this->a[reg_num] = value;
  } else if (reg_name.at(0) == 'd' || reg_name.at(0) == 'D') {
    this->d[reg_num].u = value;
  } else {
    throw std::invalid_argument("invalid register name");
  }
}

uint32_t M68KEmulator::Regs::get_reg_value(bool is_a_reg, uint8_t reg_num) {
  return is_a_reg ? this->a[reg_num] : this->d[reg_num].u;
}

void M68KEmulator::Regs::set_ccr_flags(int64_t x, int64_t n, int64_t z, int64_t v, int64_t c) {
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

  this->sr.u = (this->sr.u & mask) | replace;
}

void M68KEmulator::Regs::set_ccr_flags_integer_add(int32_t left_value, int32_t right_value, Size size) {
  left_value = sign_extend(left_value, size);
  right_value = sign_extend(right_value, size);
  int32_t result = sign_extend(left_value + right_value, size);

  bool overflow = (((left_value > 0) && (right_value > 0) && (result < 0)) ||
      ((left_value < 0) && (right_value < 0) && (result > 0)));

  // This looks kind of dumb, but it's necessary to force the compiler not to sign-extend the 32-bit ints when
  // converting to 64-bit
  uint64_t left_value_c = static_cast<uint32_t>(left_value);
  uint64_t right_value_c = static_cast<uint32_t>(right_value);
  bool carry = (left_value_c + right_value_c) > 0xFFFFFFFF;

  this->set_ccr_flags(-1, (result < 0), (result == 0), overflow, carry);
}

void M68KEmulator::Regs::set_ccr_flags_integer_subtract(int32_t left_value, int32_t right_value, Size size) {
  left_value = sign_extend(left_value, size);
  right_value = sign_extend(right_value, size);
  int32_t result = sign_extend(left_value - right_value, size);

  bool overflow = (((left_value ^ right_value) & (left_value ^ result)) < 0);
  bool carry = (static_cast<uint32_t>(left_value) < static_cast<uint32_t>(right_value));
  this->set_ccr_flags(-1, (result < 0), (result == 0), overflow, carry);
}

uint32_t M68KEmulator::Regs::pop_u32(std::shared_ptr<const MemoryContext> mem) {
  uint32_t ret = mem->read_u32b(this->a[7]);
  this->a[7] += 4;
  return ret;
}

int32_t M68KEmulator::Regs::pop_s32(std::shared_ptr<const MemoryContext> mem) {
  int32_t ret = mem->read_s32b(this->a[7]);
  this->a[7] += 4;
  return ret;
}

uint16_t M68KEmulator::Regs::pop_u16(std::shared_ptr<const MemoryContext> mem) {
  uint16_t ret = mem->read_u16b(this->a[7]);
  this->a[7] += 2;
  return ret;
}

int16_t M68KEmulator::Regs::pop_s16(std::shared_ptr<const MemoryContext> mem) {
  int16_t ret = mem->read_s16b(this->a[7]);
  this->a[7] += 2;
  return ret;
}

uint8_t M68KEmulator::Regs::pop_u8(std::shared_ptr<const MemoryContext> mem) {
  int8_t ret = mem->read_u16b(this->a[7]);
  this->a[7] += 2;
  return ret;
}

int8_t M68KEmulator::Regs::pop_s8(std::shared_ptr<const MemoryContext> mem) {
  int8_t ret = mem->read_s16b(this->a[7]);
  this->a[7] += 2;
  return ret;
}

void M68KEmulator::Regs::push_u32(std::shared_ptr<MemoryContext> mem, uint32_t v) {
  this->a[7] -= 4;
  this->write_stack_u32(mem, v);
}

void M68KEmulator::Regs::push_s32(std::shared_ptr<MemoryContext> mem, int32_t v) {
  this->a[7] -= 4;
  this->write_stack_s32(mem, v);
}

void M68KEmulator::Regs::push_u16(std::shared_ptr<MemoryContext> mem, uint16_t v) {
  this->a[7] -= 2;
  this->write_stack_u16(mem, v);
}

void M68KEmulator::Regs::push_s16(std::shared_ptr<MemoryContext> mem, int16_t v) {
  this->a[7] -= 2;
  this->write_stack_s16(mem, v);
}

void M68KEmulator::Regs::push_u8(std::shared_ptr<MemoryContext> mem, uint8_t v) {
  // Note: A7 must always be word-aligned, so `move.b -[A7], x` decrements by 2
  this->a[7] -= 2;
  this->write_stack_u16(mem, v);
}

void M68KEmulator::Regs::push_s8(std::shared_ptr<MemoryContext> mem, int8_t v) {
  // Note: A7 must always be word-aligned, so `move.b -[A7], x` decrements by 2
  this->a[7] -= 2;
  this->write_stack_s16(mem, v);
}

void M68KEmulator::Regs::write_stack_u32(std::shared_ptr<MemoryContext> mem, uint32_t v) {
  mem->write_u32b(this->a[7], v);
}

void M68KEmulator::Regs::write_stack_s32(std::shared_ptr<MemoryContext> mem, int32_t v) {
  mem->write_s32b(this->a[7], v);
}

void M68KEmulator::Regs::write_stack_u16(std::shared_ptr<MemoryContext> mem, uint16_t v) {
  mem->write_u16b(this->a[7], v);
}

void M68KEmulator::Regs::write_stack_s16(std::shared_ptr<MemoryContext> mem, int16_t v) {
  mem->write_s16b(this->a[7], v);
}

void M68KEmulator::Regs::write_stack_u8(std::shared_ptr<MemoryContext> mem, uint8_t v) {
  mem->write_u8(this->a[7], v);
}

void M68KEmulator::Regs::write_stack_s8(std::shared_ptr<MemoryContext> mem, int8_t v) {
  mem->write_s8(this->a[7], v);
}

M68KEmulator::M68KEmulator(std::shared_ptr<MemoryContext> mem) : EmulatorBase(mem) {}

M68KEmulator::Regs& M68KEmulator::registers() {
  return this->regs;
}

void M68KEmulator::print_state_header(FILE* stream) const {
  phosg::fwrite_fmt(stream, "\
---D0--- ---D1--- ---D2--- ---D3--- ---D4--- ---D5--- ---D6--- ---D7---  \
---A0--- ---A1--- ---A2--- ---A3--- ---A4--- ---A5--- ---A6--- -A7--SP-  \
CBITS ---PC--- = INSTRUCTION\n");
}

void M68KEmulator::print_state(FILE* stream) const {
  size_t pc_data_available = 0x10;
  while (!this->mem->exists(this->regs.pc, pc_data_available)) {
    pc_data_available -= 2;
  }
  const void* pc_data = this->mem->at<void>(this->regs.pc, pc_data_available);

  std::string disassembly;
  try {
    disassembly = this->disassemble_one(pc_data, pc_data_available, this->regs.pc);
  } catch (const std::exception& e) {
    disassembly = std::format(" (failed: {})", e.what());
  }

  phosg::fwrite_fmt(stream, "\
{:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}  \
{:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}  \
{}{}{}{}{} {:08X} ={}\n",
      this->regs.d[0].u, this->regs.d[1].u, this->regs.d[2].u, this->regs.d[3].u,
      this->regs.d[4].u, this->regs.d[5].u, this->regs.d[6].u, this->regs.d[7].u,
      this->regs.a[0], this->regs.a[1], this->regs.a[2], this->regs.a[3],
      this->regs.a[4], this->regs.a[5], this->regs.a[6], this->regs.a[7],
      (this->regs.sr.get_x() ? 'x' : '-'), (this->regs.sr.get_n() ? 'n' : '-'),
      (this->regs.sr.get_z() ? 'z' : '-'), (this->regs.sr.get_v() ? 'v' : '-'),
      (this->regs.sr.get_c() ? 'c' : '-'), this->regs.pc, disassembly);
}

bool M68KEmulator::check_condition(uint8_t condition) {
  // Bits in the CCR are xnzvc so e.g. 0x16 means x, z, and v are set
  const auto& sr = this->regs.sr;
  switch (condition) {
    case 0x00: // true
      return true;
    case 0x01: // false
      return false;
    case 0x02: // hi (high, unsigned greater; c=0 and z=0)
      return !sr.get_c() && !sr.get_z();
    case 0x03: // ls (low or same, unsigned less or equal; c=1 or z=1)
      return sr.get_c() || sr.get_z();
    case 0x04: // cc (carry clear; c=0)
      return !sr.get_c();
    case 0x05: // cs (carry set; c=1)
      return sr.get_c();
    case 0x06: // ne (not equal; z=0)
      return !sr.get_z();
    case 0x07: // eq (equal; z=1)
      return sr.get_z();
    case 0x08: // vc (overflow clear; v=0)
      return !sr.get_v();
    case 0x09: // vs (overflow set; v=1)
      return sr.get_v();
    case 0x0A: // pl (plus; n=0)
      return !sr.get_n();
    case 0x0B: // mi (minus; n=1)
      return sr.get_n();
    case 0x0C: // ge (greater or equal; n=v)
      return sr.get_n() == sr.get_v();
    case 0x0D: // lt (less; n!=v)
      return sr.get_n() != sr.get_v();
    case 0x0E: // gt (greater; n=v && z=0)
      return (sr.get_n() == sr.get_v()) && !sr.get_z();
    case 0x0F: // le (less or equal; n!=v || z=1)
      return (sr.get_n() != sr.get_v()) || sr.get_z();
    default:
      throw std::logic_error("invalid condition code");
  }
}

M68KEmulator::ResolvedAddress M68KEmulator::resolve_address(const DecodedAddress& addr, Size size) {
  using T = ResolvedAddress::Type;

  auto& a = this->regs.a;
  auto& d = this->regs.d;

  auto compute_index = [&]() -> int32_t {
    if (addr.suppress_index) {
      return 0;
    }
    int32_t index_value = (addr.index_is_a_reg ? a[addr.index_reg_num] : d[addr.index_reg_num].u);
    if (addr.index_is_word) {
      index_value = phosg::sign_extend<int32_t, uint16_t>(index_value);
    }
    return (index_value * addr.index_scale);
  };
  auto compute_a_base = [&]() -> uint32_t {
    return addr.suppress_base_reg ? 0 : a[addr.base_reg_num];
  };
  auto compute_pc_base = [&]() -> uint32_t {
    return addr.suppress_base_reg ? 0 : addr.base_pc;
  };

  switch (addr.mode) {
    case AM::D_REG: // D(base_reg_num)
      return ResolvedAddress{.type = T::D_REG, .where = addr.base_reg_num};
    case AM::A_REG: // A(base_reg_num)
      return ResolvedAddress{.type = T::A_REG, .where = addr.base_reg_num};
    case AM::MEM_A: // [A(base_reg_num)]
      return ResolvedAddress{.type = T::MEMORY, .where = a[addr.base_reg_num]};
    case AM::MEM_A_POSTINC: { // [A(base_reg_num)]+
      uint32_t& reg = a[addr.base_reg_num];
      uint32_t ret = reg;
      if (size == Size::LONG) {
        reg += 4;
      } else if ((addr.base_reg_num == 7) || (size == Size::WORD)) { // A7 must always be word-aligned
        reg += 2;
      } else if (size == Size::BYTE) {
        reg += 1;
      } else {
        throw std::logic_error("Invalid size in resolve_address");
      }
      return ResolvedAddress{.type = T::MEMORY, .where = ret};
    }
    case AM::MEM_A_PREDEC: { // -[A(base_reg_num)]
      uint32_t& reg = a[addr.base_reg_num];
      if (size == Size::LONG) {
        reg -= 4;
      } else if ((addr.base_reg_num == 7) || (size == Size::WORD)) { // A7 must always be word-aligned
        reg -= 2;
      } else if (size == Size::BYTE) {
        reg -= 1;
      } else {
        throw std::logic_error("Invalid size in resolve_address");
      }
      return ResolvedAddress{.type = T::MEMORY, .where = reg};
    }
    case AM::MEM_A_DISP: // [A(base_reg_num) + base_disp]
      return ResolvedAddress{.type = T::MEMORY, .where = a[addr.base_reg_num] + addr.base_disp};
    case AM::MEM_A_INDEX: // [A(base_reg_num) + X(index_reg_num).S * scale + base_disp]
      return ResolvedAddress{.type = T::MEMORY, .where = compute_a_base() + compute_index() + addr.base_disp};
    case AM::MEM_A_IND_POST: // [[An + base_disp] + X(index_reg_num).S * scale + outer_disp]
      return ResolvedAddress{
          .type = T::MEMORY,
          .where = this->read(compute_a_base() + addr.base_disp, Size::LONG) + compute_index() + addr.outer_disp};
    case AM::MEM_A_IND_PRE: // [[An + base_disp + X(index_reg_num).S * scale] + outer_disp]
      return ResolvedAddress{
          .type = T::MEMORY,
          .where = this->read(compute_a_base() + addr.base_disp + compute_index(), Size::LONG) + addr.outer_disp};
    case AM::MEM_ABSOLUTE: // [base_disp]
      return ResolvedAddress{.type = T::MEMORY, .where = static_cast<uint32_t>(addr.base_disp)};
    case AM::MEM_PC_DISP: // [base_pc + base_disp]
      return ResolvedAddress{.type = T::MEMORY, .where = addr.base_pc + addr.base_disp};
    case AM::MEM_PC_INDEX: // [base_pc + X(index_reg_num).S * scale + base_disp]
      return ResolvedAddress{.type = T::MEMORY, .where = compute_pc_base() + compute_index() + addr.base_disp};
    case AM::MEM_PC_IND_POST: // [[base_pc + base_disp] + X(index_reg_num).S * scale + outer_disp]
      return ResolvedAddress{
          .type = T::MEMORY,
          .where = this->read(compute_pc_base() + addr.base_disp, Size::LONG) + compute_index() + addr.outer_disp};
    case AM::MEM_PC_IND_PRE: // [[base_pc + base_disp + X(index_reg_num).S * scale] + outer_disp]
      return ResolvedAddress{
          .type = T::MEMORY,
          .where = this->read(compute_pc_base() + addr.base_disp + compute_index(), Size::LONG) + addr.outer_disp};
    case AM::IMM: // imm8/16/32 (in base_disp)
      return ResolvedAddress{.type = T::IMM, .where = static_cast<uint32_t>(addr.base_disp)};

    case AM::INVALID: // (invalid_reason not null)
      throw std::runtime_error(std::format("Cannot compute address of operand: {}", addr.invalid_reason));
    default:
      throw std::logic_error("Unknown addressing mode");
  }
}

uint32_t M68KEmulator::resolve_memory_address(const DecodedAddress& addr, Size size) {
  auto ret = this->resolve_address(addr, size);
  if (ret.type != ResolvedAddress::Type::MEMORY) {
    throw std::logic_error("A memory address is required");
  }
  return ret.where;
}

uint32_t M68KEmulator::read(const ResolvedAddress& addr, Size size) const {
  switch (addr.type) {
    case ResolvedAddress::Type::D_REG: {
      uint32_t v = this->regs.d[addr.where].u;
      if (size == Size::BYTE) {
        return v & 0x000000FF;
      } else if (size == Size::WORD) {
        return v & 0x0000FFFF;
      } else if (size == Size::LONG) {
        return v;
      } else {
        throw std::runtime_error("Invalid D register read size");
      }
    }
    case ResolvedAddress::Type::A_REG: {
      uint32_t v = this->regs.a[addr.where];
      if (size == Size::BYTE) {
        return v & 0x000000FF;
      } else if (size == Size::WORD) {
        return v & 0x0000FFFF;
      } else if (size == Size::LONG) {
        return v;
      } else {
        throw std::runtime_error("Invalid A register read size");
      }
    }
    case ResolvedAddress::Type::IMM:
      return addr.where;
    case ResolvedAddress::Type::MEMORY:
      return this->read(addr.where, size);
    default:
      throw std::logic_error("Invalid resolved address type");
  }
}

uint32_t M68KEmulator::read(uint32_t addr, Size size) const {
  uint32_t ret;
  if (size == Size::BYTE) {
    ret = this->mem->read_u8(addr);
    if (this->log_memory_access) {
      phosg::fwrite_fmt(stderr, "  Memory: read {:08X} = {:02X}\n", addr, ret);
    }
  } else if (size == Size::WORD) {
    ret = this->mem->read_u16b(addr);
    if (this->log_memory_access) {
      phosg::fwrite_fmt(stderr, "  Memory: read {:08X} = {:04X}\n", addr, ret);
    }
  } else if (size == Size::LONG) {
    ret = this->mem->read_u32b(addr);
    if (this->log_memory_access) {
      phosg::fwrite_fmt(stderr, "  Memory: read {:08X} = {:08X}\n", addr, ret);
    }
  } else {
    throw std::runtime_error("Invalid read size");
  }
  return ret;
}

void M68KEmulator::write(const ResolvedAddress& addr, uint32_t value, Size size) {
  switch (addr.type) {
    case ResolvedAddress::Type::D_REG: {
      uint32_t& reg = this->regs.d[addr.where].u;
      if (size == Size::BYTE) {
        reg = (reg & 0xFFFFFF00) | (value & 0x000000FF);
      } else if (size == Size::WORD) {
        reg = (reg & 0xFFFF0000) | (value & 0x0000FFFF);
      } else if (size == Size::LONG) {
        reg = value;
      } else {
        throw std::runtime_error("Invalid D register write size");
      }
      break;
    }
    case ResolvedAddress::Type::A_REG: {
      uint32_t& reg = this->regs.a[addr.where];
      if (size == Size::BYTE) {
        reg = (reg & 0xFFFFFF00) | (value & 0x000000FF);
      } else if (size == Size::WORD) {
        reg = (reg & 0xFFFF0000) | (value & 0x0000FFFF);
      } else if (size == Size::LONG) {
        reg = value;
      } else {
        throw std::runtime_error("Invalid A register write size");
      }
      break;
    }
    case ResolvedAddress::Type::IMM:
      throw std::runtime_error("Cannot write to immediate value");
    case ResolvedAddress::Type::MEMORY:
      this->write(addr.where, value, size);
      break;
    default:
      throw std::logic_error("Invalid resolved address type");
  }
}

void M68KEmulator::write(uint32_t addr, uint32_t value, Size size) {
  if (size == Size::BYTE) {
    if (this->log_memory_access) {
      phosg::fwrite_fmt(stderr, "  Memory: write {:08X} = {:02X}\n", addr, value);
    }
    this->mem->write_u8(addr, value);
  } else if (size == Size::WORD) {
    if (this->log_memory_access) {
      phosg::fwrite_fmt(stderr, "  Memory: write {:08X} = {:04X}\n", addr, value);
    }
    this->mem->write_u16b(addr, value);
  } else if (size == Size::LONG) {
    if (this->log_memory_access) {
      phosg::fwrite_fmt(stderr, "  Memory: write {:08X} = {:08X}\n", addr, value);
    }
    this->mem->write_u32b(addr, value);
  } else {
    throw std::runtime_error("incorrect size on write");
  }
}

uint16_t M68KEmulator::read_ins_u16(uint8_t) {
  uint16_t ret = this->mem->read<phosg::be_uint16_t>(this->regs.pc);
  this->regs.pc += 2;
  return ret;
}
int16_t M68KEmulator::read_ins_s16(uint8_t) {
  return static_cast<int16_t>(this->read_ins_u16());
}

uint32_t M68KEmulator::read_ins_u32(uint8_t) {
  uint32_t ret = this->mem->read<phosg::be_uint32_t>(this->regs.pc);
  this->regs.pc += 4;
  return ret;
}
int32_t M68KEmulator::read_ins_s32(uint8_t) {
  return static_cast<int32_t>(this->read_ins_u32());
}
uint32_t M68KEmulator::read_pc() {
  return this->regs.pc;
}

uint16_t M68KEmulator::DisassemblyState::read_ins_u16(uint8_t imm_bytes) {
  if (imm_bytes > 0) {
    this->imm_offsets.emplace(this->r.where() + (2 - imm_bytes), imm_bytes);
  }
  return this->r.get_u16b();
}
uint32_t M68KEmulator::DisassemblyState::read_ins_u32(uint8_t imm_bytes) {
  if (imm_bytes > 0) {
    this->imm_offsets.emplace(this->r.where() + (4 - imm_bytes), imm_bytes);
  }
  return this->r.get_u32b();
}
int16_t M68KEmulator::DisassemblyState::read_ins_s16(uint8_t imm_bytes) {
  if (imm_bytes > 0) {
    this->imm_offsets.emplace(this->r.where() + (2 - imm_bytes), imm_bytes);
  }
  return this->r.get_s16b();
}
int32_t M68KEmulator::DisassemblyState::read_ins_s32(uint8_t imm_bytes) {
  if (imm_bytes > 0) {
    this->imm_offsets.emplace(this->r.where() + (4 - imm_bytes), imm_bytes);
  }
  return this->r.get_s32b();
}
uint32_t M68KEmulator::DisassemblyState::read_pc() {
  return this->start_address + this->r.where();
}

std::string M68KEmulator::DisassemblyState::on_invalid(const char* what, const DecodedAddress* addr) {
  this->prev_was_valid = false;
  if (addr && what) {
    return std::format(".invalid   {} // {}", this->dasm_address(*addr, ValueType::INVALID), what);
  } else if (addr) {
    return std::format(".invalid   {}", this->dasm_address(*addr, ValueType::INVALID));
  } else if (what) {
    return std::format(".invalid   // {}", what);
  } else {
    throw std::logic_error("A reason must be provided to on_invalid");
  }
}
void M68KEmulator::on_invalid(const char* what, const DecodedAddress* addr) {
  if (what) {
    throw std::runtime_error(std::format("Invalid opcode: {}", what));
  } else if (addr) {
    throw std::runtime_error("Invalid address");
  } else {
    throw std::logic_error("A reason must be provided to on_invalid");
  }
}

std::string M68KEmulator::DisassemblyState::on_ori_sr_imm(Size size, uint16_t v) {
  return std::format("ori        {}, 0x{:0{}X}", (size == Size::BYTE) ? "ccr" : "sr", v, bytes_for_size(size) * 2);
}
void M68KEmulator::on_ori_sr_imm(Size size, uint16_t v) {
  if (size == Size::BYTE) {
    this->regs.sr.u |= (v & 0x00FF);
  } else {
    throw std::runtime_error("Cannot write to SR in user mode"); // this->regs.sr.u |= v;
  }
}

std::string M68KEmulator::DisassemblyState::on_andi_sr_imm(Size size, uint16_t v) {
  return std::format("andi       {}, 0x{:0{}X}", (size == Size::BYTE) ? "ccr" : "sr", v, bytes_for_size(size) * 2);
}
void M68KEmulator::on_andi_sr_imm(Size size, uint16_t v) {
  if (size == Size::BYTE) {
    this->regs.sr.u &= (v | 0xFF00);
  } else {
    throw std::runtime_error("Cannot write to SR in user mode"); // this->regs.sr.u &= v;
  }
}

std::string M68KEmulator::DisassemblyState::on_xori_sr_imm(Size size, uint16_t v) {
  return std::format("xori       {}, 0x{:0{}X}", (size == Size::BYTE) ? "ccr" : "sr", v, bytes_for_size(size) * 2);
}
void M68KEmulator::on_xori_sr_imm(Size size, uint16_t v) {
  if (size == Size::BYTE) {
    this->regs.sr.u ^= (v & 0x00FF);
  } else {
    throw std::runtime_error("Cannot write to SR in user mode"); // this->regs.sr.u ^= v;
  }
}

std::string M68KEmulator::DisassemblyState::on_movep(bool is_long, bool is_write, uint8_t d_reg, uint8_t a_reg, int16_t disp) {
  std::string ea_dasm;
  if (disp == 0) {
    ea_dasm = std::format("[A{}]", a_reg);
  } else if (disp < 0) {
    ea_dasm = std::format("[A{} - 0x{:X}]", a_reg, -disp);
  } else {
    ea_dasm = std::format("[A{} + 0x{:X}]", a_reg, disp);
  }
  return is_write
      ? std::format("movep.{}    {}, D{}", is_long ? 'l' : 'w', ea_dasm, d_reg)
      : std::format("movep.{}    D{}, {}", is_long ? 'l' : 'w', d_reg, ea_dasm);
}
void M68KEmulator::on_movep(bool is_long, bool is_write, uint8_t d_reg, uint8_t a_reg, int16_t disp) {
  auto& d_reg_val = this->regs.d[d_reg].u;
  uint32_t addr = this->regs.a[a_reg] + disp;
  if (is_write) {
    if (is_long) {
      this->write(addr, (d_reg_val >> 24) & 0xFF, Size::BYTE);
      this->write(addr + 2, (d_reg_val >> 16) & 0xFF, Size::BYTE);
      this->write(addr + 4, (d_reg_val >> 8) & 0xFF, Size::BYTE);
      this->write(addr + 6, d_reg_val & 0xFF, Size::BYTE);
    } else {
      this->write(addr, (d_reg_val >> 8) & 0xFF, Size::BYTE);
      this->write(addr + 2, d_reg_val & 0xFF, Size::BYTE);
    }
  } else {
    if (is_long) {
      d_reg_val = (this->read(addr, Size::BYTE) << 24) |
          (this->read(addr + 2, Size::BYTE) << 16) |
          (this->read(addr + 4, Size::BYTE) << 8) |
          this->read(addr + 6, Size::BYTE);
    } else {
      d_reg_val = (d_reg_val & 0xFFFF0000) | (this->read(addr, Size::BYTE) << 8) | this->read(addr + 2, Size::BYTE);
    }
  }
}

std::string M68KEmulator::DisassemblyState::on_moves(Size size, const DecodedAddress& addr, bool is_a_reg, uint8_t reg_num, bool is_write) {
  auto addr_str = this->dasm_address(addr, value_type_for_size(size));
  return is_write
      ? std::format("moves      {}, {}{}", addr_str, is_a_reg ? 'A' : 'D', reg_num)
      : std::format("moves      {}{}, {}", is_a_reg ? 'A' : 'D', reg_num, addr_str);
}
void M68KEmulator::on_moves(Size, const DecodedAddress&, bool, uint8_t, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_btst_bchg_bclr_bset(uint8_t what, const DecodedAddress& addr, uint8_t bit_reg_num, uint8_t imm) {
  constexpr std::array<const char*, 4> names{"btst", "bchg", "bclr", "bset"};
  return std::format("{:<10} {}, {}", names[what], this->dasm_address(addr, ValueType::BYTE),
      (bit_reg_num <= 7) ? std::format("D{}", bit_reg_num) : std::format("0x{:02X}", imm));
}
void M68KEmulator::on_btst_bchg_bclr_bset(uint8_t what, const DecodedAddress& addr, uint8_t bit_reg_num, uint8_t imm) {
  uint32_t bit_index = (bit_reg_num <= 7) ? this->regs.d[bit_reg_num].u : (imm & 0x00FF);
  uint32_t test_value = 1 << (bit_index & (addr.is_memory_mode() ? 0x07 : 0x1F));
  Size data_size = addr.is_memory_mode() ? Size::BYTE : Size::LONG;

  auto ea = this->resolve_address(addr, data_size);
  uint32_t mem_value = this->read(ea, data_size);

  this->regs.set_ccr_flags(-1, -1, (mem_value & test_value) ? 0 : 1, -1, -1);
  switch (what) {
    case 0:
      // Don't change the bit, just test it (already done above)
      break;
    case 1:
      this->write(ea, mem_value ^ test_value, data_size);
      break;
    case 2:
      this->write(ea, mem_value & (~test_value), data_size);
      break;
    case 3:
      this->write(ea, mem_value | test_value, data_size);
      break;
    default:
      throw std::logic_error("unhandled single-bit operand case");
  }
}

std::string M68KEmulator::DisassemblyState::on_ori(Size size, const DecodedAddress& addr, uint32_t value) {
  return std::format("ori.{}      {}, {}",
      char_for_size(size), this->dasm_address(addr, value_type_for_size(size)), format_immediate(value));
}
void M68KEmulator::on_ori(Size size, const DecodedAddress& addr, uint32_t value) {
  auto ea = this->resolve_address(addr, size);
  uint32_t mem_value = this->read(ea, size);
  mem_value |= value;
  this->write(ea, mem_value, size);
  this->regs.set_ccr_flags(-1, is_negative(mem_value, size), !mem_value, 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_andi(Size size, const DecodedAddress& addr, uint32_t value) {
  return std::format("andi.{}     {}, {}",
      char_for_size(size), this->dasm_address(addr, value_type_for_size(size)), format_immediate(value));
}
void M68KEmulator::on_andi(Size size, const DecodedAddress& addr, uint32_t value) {
  auto ea = this->resolve_address(addr, size);
  uint32_t mem_value = this->read(ea, size);
  mem_value &= value;
  this->write(ea, mem_value, size);
  this->regs.set_ccr_flags(-1, is_negative(mem_value, size), !mem_value, 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_subi(Size size, const DecodedAddress& addr, uint32_t value) {
  return std::format("subi.{}     {}, {}",
      char_for_size(size), this->dasm_address(addr, value_type_for_size(size)), format_immediate(value));
}
void M68KEmulator::on_subi(Size size, const DecodedAddress& addr, uint32_t value) {
  auto ea = this->resolve_address(addr, size);
  uint32_t mem_value = this->read(ea, size);
  this->regs.set_ccr_flags_integer_subtract(mem_value, value, size);
  this->regs.set_ccr_flags(this->regs.sr.get_c(), -1, -1, -1, -1);
  mem_value -= value;
  this->write(ea, mem_value, size);
}

std::string M68KEmulator::DisassemblyState::on_addi(Size size, const DecodedAddress& addr, uint32_t value) {
  return std::format("addi.{}     {}, {}",
      char_for_size(size), this->dasm_address(addr, value_type_for_size(size)), format_immediate(value));
}
void M68KEmulator::on_addi(Size size, const DecodedAddress& addr, uint32_t value) {
  auto ea = this->resolve_address(addr, size);
  uint32_t mem_value = this->read(ea, size);
  this->regs.set_ccr_flags_integer_add(mem_value, value, size);
  this->regs.set_ccr_flags(this->regs.sr.get_c(), -1, -1, -1, -1);
  mem_value += value;
  this->write(ea, mem_value, size);
}

std::string M68KEmulator::DisassemblyState::on_xori(Size size, const DecodedAddress& addr, uint32_t value) {
  return std::format("xori.{}     {}, {}",
      char_for_size(size), this->dasm_address(addr, value_type_for_size(size)), format_immediate(value));
}
void M68KEmulator::on_xori(Size size, const DecodedAddress& addr, uint32_t value) {
  auto ea = this->resolve_address(addr, size);
  uint32_t mem_value = this->read(ea, size);
  mem_value ^= value;
  this->write(ea, mem_value, size);
  this->regs.set_ccr_flags(-1, is_negative(mem_value, size), !mem_value, 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_cmpi(Size size, const DecodedAddress& addr, uint32_t value) {
  return std::format("cmpi.{}     {}, {}",
      char_for_size(size), this->dasm_address(addr, value_type_for_size(size)), format_immediate(value));
}
void M68KEmulator::on_cmpi(Size size, const DecodedAddress& addr, uint32_t value) {
  auto ea = this->resolve_address(addr, size);
  this->regs.set_ccr_flags_integer_subtract(this->read(ea, size), value, size);
}

std::string M68KEmulator::DisassemblyState::on_rtm() {
  return "rtm";
}
void M68KEmulator::on_rtm() {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_callm(const DecodedAddress& addr, uint8_t value) {
  return std::format("callm      {}, {}", value, this->dasm_address(addr, ValueType::INVALID), value);
}
void M68KEmulator::on_callm(const DecodedAddress&, uint8_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_cas2(
    Size size, bool mem1_is_a_reg, uint8_t mem1_reg, uint8_t compare1_reg, uint8_t update1_reg,
    bool mem2_is_a_reg, uint8_t mem2_reg, uint8_t compare2_reg, uint8_t update2_reg) {
  return std::format("cas2.{}     [{}{}]:[{}{}], D{}:D{}, D{}:D{}", char_for_size(size),
      mem1_is_a_reg ? 'A' : 'D', mem1_reg, compare1_reg, update1_reg,
      mem2_is_a_reg ? 'A' : 'D', mem2_reg, compare2_reg, update2_reg);
}
void M68KEmulator::on_cas2(Size, bool, uint8_t, uint8_t, uint8_t, bool, uint8_t, uint8_t, uint8_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_cas(Size size, const DecodedAddress& addr, uint8_t compare_reg, uint8_t update_reg) {
  return std::format("cas.{}      {}, D{}, D{}",
      char_for_size(size), this->dasm_address(addr, value_type_for_size(size)), compare_reg, update_reg);
}
void M68KEmulator::on_cas(Size, const DecodedAddress&, uint8_t, uint8_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_chk2_cmp2(Size size, const DecodedAddress& addr, bool is_a_reg, uint8_t reg_num, bool is_chk2) {
  auto addr_str = this->dasm_address(addr, value_type_for_size(size));
  return std::format("{:<10} {}{}, {}", is_chk2 ? "chk2" : "cmp2", is_a_reg ? 'A' : 'D', reg_num, addr_str);
}
void M68KEmulator::on_chk2_cmp2(Size, const DecodedAddress&, bool, uint8_t, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_movea(Size size, uint8_t dest_reg, const DecodedAddress& src_addr) {
  return std::format("movea.{:c}    A{}, {}",
      char_for_size(size), dest_reg, this->dasm_address(src_addr, value_type_for_size(size)));
}
void M68KEmulator::on_movea(Size size, uint8_t dest_reg, const DecodedAddress& src_addr) {
  // movea is more restrictive than move: byte reads are not allowed, and it's always a long write even if it's a word
  // read. Also, CCR flags are not affected, unlike with move.
  if (size == Size::BYTE) {
    throw std::runtime_error("invalid movea.b opcode");
  }
  auto src_ea = this->resolve_address(src_addr, size);
  this->regs.a[dest_reg] = sign_extend(this->read(src_ea, size), size);
}

std::string M68KEmulator::DisassemblyState::on_move(Size size, const DecodedAddress& dest_addr, const DecodedAddress& src_addr) {
  auto type = value_type_for_size(size);
  return std::format("move.{:c}     {}, {}",
      char_for_size(size), this->dasm_address(dest_addr, type), this->dasm_address(src_addr, type));
}
void M68KEmulator::on_move(Size size, const DecodedAddress& dest_addr, const DecodedAddress& src_addr) {
  auto src_ea = this->resolve_address(src_addr, size);
  auto dest_ea = this->resolve_address(dest_addr, size);
  uint32_t value = this->read(src_ea, size);
  this->write(dest_ea, value, size);
  this->regs.set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_movem_read(Size size, const DecodedAddress& addr, uint16_t reg_mask) {
  return std::format("movem.{}    {}, {}", char_for_size(size),
      this->dasm_reg_mask(reg_mask, false), this->dasm_address(addr, value_type_for_size(size)));
}
void M68KEmulator::on_movem_read(Size size, const DecodedAddress& addr, uint16_t reg_mask) {
  uint32_t ea = this->resolve_memory_address(addr, size);
  uint8_t bytes_per_value = bytes_for_size(size);

  auto read_value = [&]() -> uint32_t {
    uint32_t ret = this->read(ea, size);
    if (size == Size::WORD) {
      ret = phosg::sign_extend<uint32_t, uint16_t>(ret);
    }
    ea += bytes_per_value;
    return ret;
  };

  // Load the regs; the low mask bit is D0, the high bit is A7
  for (size_t x = 0; x < 8; x++) {
    if (reg_mask & (1 << x)) {
      this->regs.d[x].u = read_value();
    }
  }
  for (size_t x = 0; x < 8; x++) {
    if (reg_mask & (1 << (x + 8))) {
      this->regs.a[x] = read_value();
    }
  }

  // In postincrement mode, update the address register
  if (addr.mode == AM::MEM_A_POSTINC) {
    this->regs.a[addr.base_reg_num] = ea;
  }

  // Note: CCR not affected
}

std::string M68KEmulator::DisassemblyState::on_movem_write(Size size, const DecodedAddress& addr, uint16_t reg_mask) {
  return std::format("movem.{}    {}, {}", char_for_size(size),
      this->dasm_address(addr, value_type_for_size(size)),
      this->dasm_reg_mask(reg_mask, (addr.mode == AM::MEM_A_PREDEC)));
}
void M68KEmulator::on_movem_write(Size size, const DecodedAddress& addr, uint16_t reg_mask) {
  uint8_t bytes_per_value = bytes_for_size(size);

  // Predecrement mode is special-cased for this opcode; in this mode we write the registers in reverse order
  if (addr.mode == AM::MEM_A_PREDEC) {
    for (size_t x = 0; x < 8; x++) {
      if (reg_mask & (1 << x)) {
        this->regs.a[addr.base_reg_num] -= bytes_per_value;
        this->write(this->regs.a[addr.base_reg_num], this->regs.a[7 - x], size);
      }
    }
    for (size_t x = 0; x < 8; x++) {
      if (reg_mask & (1 << (x + 8))) {
        this->regs.a[addr.base_reg_num] -= bytes_per_value;
        this->write(this->regs.a[addr.base_reg_num], this->regs.d[7 - x].u, size);
      }
    }
  } else {
    uint32_t ea = this->resolve_memory_address(addr, size);
    for (size_t x = 0; x < 8; x++) {
      if (reg_mask & (1 << x)) {
        this->write(ea, this->regs.d[x].u, size);
        ea += bytes_per_value;
      }
    }
    for (size_t x = 0; x < 8; x++) {
      if (reg_mask & (1 << (x + 8))) {
        this->write(ea, this->regs.a[x], size);
        ea += bytes_per_value;
      }
    }
  }
  // Note: CCR not affected
}

std::string M68KEmulator::DisassemblyState::on_lea(uint8_t reg_num, const DecodedAddress& addr) {
  return std::format("lea.l      A{}, {}", reg_num, this->dasm_address(addr, ValueType::LONG));
}
void M68KEmulator::on_lea(uint8_t reg_num, const DecodedAddress& addr) {
  this->regs.a[reg_num] = this->resolve_memory_address(addr, Size::LONG);
  // Note: ccr not affected
}

std::string M68KEmulator::DisassemblyState::on_chk(Size size, const DecodedAddress& addr, uint8_t reg) {
  return std::format("chk.{}      D{}, {}",
      char_for_size(size), reg, this->dasm_address(addr, value_type_for_size(size)));
}
void M68KEmulator::on_chk(Size size, const DecodedAddress& addr, uint8_t reg) {
  auto ea = this->resolve_address(addr, size);
  int32_t bound = static_cast<int32_t>(this->read(ea, size));
  int32_t value = (size == Size::WORD) ? static_cast<int16_t>(this->regs.d[reg].u & 0xFFFF) : this->regs.d[reg].s;
  // TODO: Implement the 68k exception model instead of throwing here (and in other appropriate places)
  if (value < 0) {
    this->regs.set_ccr_flags(-1, 1, -1, -1, -1);
    throw std::runtime_error("chk.w: register value below zero (CHK exception, vector 6)");
  } else if (value > bound) {
    this->regs.set_ccr_flags(-1, 0, -1, -1, -1);
    throw std::runtime_error("chk.w: register value above bound (CHK exception, vector 6)");
  }
}

std::string M68KEmulator::DisassemblyState::on_ext_byte_word(uint8_t reg_num) {
  return std::format("ext.w      D{}", reg_num);
}
void M68KEmulator::on_ext_byte_word(uint8_t reg_num) {
  auto& reg = this->regs.d[reg_num].u;
  reg = (reg & 0xFFFF00FF) | ((reg & 0x00000080) ? 0x0000FF00 : 0x00000000);
  this->regs.set_ccr_flags(-1, is_negative(reg, Size::WORD), (reg == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_ext_word_long(uint8_t reg_num) {
  return std::format("ext.l      D{}", reg_num);
}
void M68KEmulator::on_ext_word_long(uint8_t reg_num) {
  auto& reg = this->regs.d[reg_num].u;
  reg = (reg & 0x0000FFFF) | ((reg & 0x00008000) ? 0xFFFF0000 : 0x00000000);
  this->regs.set_ccr_flags(-1, is_negative(reg, Size::LONG), (reg == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_ext_byte_long(uint8_t reg_num) {
  return std::format("extb.l     D{}", reg_num);
}
void M68KEmulator::on_ext_byte_long(uint8_t reg_num) {
  auto& reg = this->regs.d[reg_num].u;
  reg = (reg & 0x000000FF) | ((reg & 0x00000080) ? 0xFFFFFF00 : 0x00000000);
  this->regs.set_ccr_flags(-1, is_negative(reg, Size::LONG), (reg == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_move_dest_sr(const DecodedAddress& addr) {
  return std::format("move.w     {}, sr", this->dasm_address(addr, ValueType::WORD));
}
void M68KEmulator::on_move_dest_sr(const DecodedAddress&) {
  throw std::runtime_error("Cannot read from sr in user mode");
}

std::string M68KEmulator::DisassemblyState::on_move_dest_ccr(const DecodedAddress& addr) {
  return std::format("move.b     {}, ccr", this->dasm_address(addr, ValueType::BYTE));
}
void M68KEmulator::on_move_dest_ccr(const DecodedAddress& addr) {
  auto ea = this->resolve_address(addr, Size::BYTE);
  this->write(ea, this->regs.sr.u & 0x00FF, Size::BYTE);
}

std::string M68KEmulator::DisassemblyState::on_move_ccr_src(const DecodedAddress& addr) {
  return std::format("move.b     ccr, {}", this->dasm_address(addr, ValueType::BYTE));
}
void M68KEmulator::on_move_ccr_src(const DecodedAddress& addr) {
  auto ea = this->resolve_address(addr, Size::BYTE);
  this->regs.sr.u = (this->regs.sr.u & 0xFF00) | (this->read(ea, Size::BYTE) & 0x001F);
}

std::string M68KEmulator::DisassemblyState::on_move_sr_src(const DecodedAddress& addr) {
  return std::format("move.w     sr, {}", this->dasm_address(addr, ValueType::WORD));
}
void M68KEmulator::on_move_sr_src(const DecodedAddress&) {
  throw std::runtime_error("cannot write to sr in user mode");
}

std::string M68KEmulator::DisassemblyState::on_negx(Size size, const DecodedAddress& addr) {
  return std::format("negx.{}     {}", char_for_size(size), this->dasm_address(addr, value_type_for_size(size)));
}
void M68KEmulator::on_negx(Size size, const DecodedAddress& addr) {
  auto ea = this->resolve_address(addr, size);
  uint32_t src = this->read(ea, size);
  int32_t value = -static_cast<int32_t>(src) - this->regs.sr.get_x();
  this->write(ea, value, size);
  this->regs.set_ccr_flags(
      (value != 0), // X = same as C
      is_negative(value, size), // N = result is negative
      (value != 0) ? 0 : this->regs.sr.get_z(), // Cleared if result is nonzero; unchanged otherwise
      (-static_cast<int32_t>(src) == static_cast<int32_t>(src)), // V = overflow (0 and 0x80000000)
      (value != 0)); // C = borrow occurred (which always happens if value is nonzero)
}

std::string M68KEmulator::DisassemblyState::on_clr(Size size, const DecodedAddress& addr) {
  return std::format("clr.{}      {}", char_for_size(size), this->dasm_address(addr, value_type_for_size(size)));
}
void M68KEmulator::on_clr(Size size, const DecodedAddress& addr) {
  auto ea = this->resolve_address(addr, size);
  this->write(ea, 0, size);
  this->regs.set_ccr_flags(-1, 0, 1, 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_neg(Size size, const DecodedAddress& addr) {
  return std::format("neg.{}      {}", char_for_size(size), this->dasm_address(addr, value_type_for_size(size)));
}
void M68KEmulator::on_neg(Size size, const DecodedAddress& addr) {
  auto ea = this->resolve_address(addr, size);
  int32_t value = -static_cast<int32_t>(this->read(ea, size));
  this->write(ea, value, size);
  this->regs.set_ccr_flags(
      (value != 0),
      is_negative(value, size),
      (value == 0),
      (value == (1 << (bytes_for_size(size) * 8 - 1))),
      (value != 0));
}

std::string M68KEmulator::DisassemblyState::on_not(Size size, const DecodedAddress& addr) {
  return std::format("not.{}      {}", char_for_size(size), this->dasm_address(addr, value_type_for_size(size)));
}
void M68KEmulator::on_not(Size size, const DecodedAddress& addr) {
  auto ea = this->resolve_address(addr, size);
  uint32_t value = ~this->read(ea, size);
  // We clear the high bits here, even though those bits won't be written back to the destination, because the Z flag
  // must be computed based only on the affected bits
  if (size == Size::BYTE) {
    value &= 0xFF;
  } else if (size == Size::WORD) {
    value &= 0xFFFF;
  }
  this->write(ea, value, size);
  this->regs.set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_link(uint8_t a_reg_num, int32_t disp) {
  return std::format("link       A{}, {}", a_reg_num, phosg::hex(disp));
}
void M68KEmulator::on_link(uint8_t a_reg_num, int32_t disp) {
  this->regs.a[7] -= 4;
  this->write(this->regs.a[7], this->regs.a[a_reg_num], Size::LONG);
  this->regs.a[a_reg_num] = this->regs.a[7];
  this->regs.a[7] += disp;
  // Note: ccr not affected
}

std::string M68KEmulator::DisassemblyState::on_nbcd(const DecodedAddress& addr) {
  return std::format("nbcd.b     {}", this->dasm_address(addr, ValueType::BYTE));
}
void M68KEmulator::on_nbcd(const DecodedAddress&) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_swap(uint8_t d_reg_num) {
  return std::format("swap.w     D{}", d_reg_num);
}
void M68KEmulator::on_swap(uint8_t d_reg_num) {
  auto& reg = this->regs.d[d_reg_num].u;
  reg = (reg >> 16) | (reg << 16);
  this->regs.set_ccr_flags(-1, (reg & 0x80000000), (reg == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_bkpt(uint8_t v) {
  return std::format("bkpt       {}", v);
}
void M68KEmulator::on_bkpt(uint8_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_pea(const DecodedAddress& addr) {
  // Special-case `pea.l [absolute_addr]` since the 32-bit form is likely to contain an OSType, which we should ASCII-
  // decode if possible
  return (addr.mode == AM::MEM_ABSOLUTE)
      ? std::format("push.l     {}", format_immediate(addr.base_disp))
      : std::format("pea.l      {}", this->dasm_address(addr, ValueType::LONG));
}
void M68KEmulator::on_pea(const DecodedAddress& addr) {
  auto ea = this->resolve_memory_address(addr, Size::LONG);
  this->regs.a[7] -= 4;
  this->write(this->regs.a[7], ea, Size::LONG);
  // Note: ccr not affected
}

std::string M68KEmulator::DisassemblyState::on_tst(Size size, const DecodedAddress& addr) {
  return std::format("tst.{}      {}", char_for_size(size), this->dasm_address(addr, value_type_for_size(size)));
}
void M68KEmulator::on_tst(Size size, const DecodedAddress& addr) {
  auto ea = this->resolve_address(addr, size);
  uint32_t value = this->read(ea, size);
  this->regs.set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_tas(const DecodedAddress& addr) {
  return std::format("tas.b      {}", this->dasm_address(addr, ValueType::LONG));
}
void M68KEmulator::on_tas(const DecodedAddress&) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_bgnd() {
  return "bgnd";
}
void M68KEmulator::on_bgnd() {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_illegal() {
  return "illegal";
}
void M68KEmulator::on_illegal() {
  throw std::runtime_error("Illegal opcode");
}

std::string M68KEmulator::DisassemblyState::on_muls_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low) {
  return is_64
      ? std::format("muls.l     D{}:D{}, {}", reg_high, reg_low, this->dasm_address(addr, ValueType::LONG))
      : std::format("muls.l     D{}, {}", reg_low, this->dasm_address(addr, ValueType::LONG));
}
void M68KEmulator::on_muls_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low) {
  auto ea = this->resolve_address(addr, Size::LONG);
  int64_t src = static_cast<int32_t>(this->read(ea, Size::LONG));
  int64_t p = static_cast<int64_t>(this->regs.d[reg_low].s) * src;
  this->regs.d[reg_low].s = p;
  if (is_64) {
    this->regs.d[reg_high].s = p >> 32;
  }
  bool ovf = !is_64 && (phosg::sign_extend<int64_t, uint32_t>(p) != p);
  this->regs.set_ccr_flags(-1, is_64 ? (p < 0) : (static_cast<int32_t>(p) < 0), (p == 0), ovf, 0);
}

std::string M68KEmulator::DisassemblyState::on_mulu_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low) {
  return is_64
      ? std::format("mulu.l     D{}:D{}, {}", reg_high, reg_low, this->dasm_address(addr, ValueType::LONG))
      : std::format("mulu.l     D{}, {}", reg_low, this->dasm_address(addr, ValueType::LONG));
}
void M68KEmulator::on_mulu_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low) {
  auto ea = this->resolve_address(addr, Size::LONG);
  uint64_t src = this->read(ea, Size::LONG);
  uint64_t p = static_cast<uint64_t>(this->regs.d[reg_low].u) * src;
  this->regs.d[reg_low].u = p;
  if (is_64) {
    this->regs.d[reg_high].u = p >> 32;
  }
  bool ovf = !is_64 && ((p >> 32) != 0);
  this->regs.set_ccr_flags(-1, ((p >> (is_64 ? 63 : 31)) & 1), (p == 0), ovf, 0);
}

std::string M68KEmulator::DisassemblyState::on_divs_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low) {
  return is_64
      ? std::format("divsl.l    D{}:D{}, {}", reg_high, reg_low, this->dasm_address(addr, ValueType::LONG))
      : std::format("divs.l     D{}, {}", reg_low, this->dasm_address(addr, ValueType::LONG));
}
void M68KEmulator::on_divs_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low) {
  auto ea = this->resolve_address(addr, Size::LONG);
  int64_t src = static_cast<int32_t>(this->read(ea, Size::LONG));
  if (src == 0) {
    throw std::runtime_error("Extended integer division by zero");
  }
  int64_t dividend = is_64
      ? ((phosg::sign_extend<int64_t, uint32_t>(this->regs.d[reg_high].u) << 32) | this->regs.d[reg_low].u)
      : phosg::sign_extend<int64_t, uint32_t>(this->regs.d[reg_low].u);
  int64_t quotient = dividend / static_cast<int32_t>(src);
  this->regs.d[reg_high].s = dividend % static_cast<int32_t>(src);
  this->regs.d[reg_low].s = quotient;
  this->regs.set_ccr_flags(-1, (static_cast<int32_t>(quotient) < 0), (quotient == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_divu_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low) {
  return is_64
      ? std::format("divul.l    D{}:D{}, {}", reg_high, reg_low, this->dasm_address(addr, ValueType::LONG))
      : std::format("divu.l     D{}, {}", reg_low, this->dasm_address(addr, ValueType::LONG));
}
void M68KEmulator::on_divu_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low) {
  auto ea = this->resolve_address(addr, Size::LONG);
  uint64_t src = this->read(ea, Size::LONG);
  if (src == 0) {
    throw std::runtime_error("Extended integer division by zero");
  }
  uint64_t dividend = is_64
      ? ((static_cast<uint64_t>(this->regs.d[reg_high].u) << 32) | this->regs.d[reg_low].u)
      : static_cast<uint64_t>(this->regs.d[reg_low].u);
  uint64_t quotient = dividend / src;
  this->regs.d[reg_high].u = dividend % src;
  this->regs.d[reg_low].u = quotient;
  this->regs.set_ccr_flags(-1, ((quotient >> 31) & 1), (quotient == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_jsr_jmp(const DecodedAddress& addr, bool is_jsr) {
  int64_t target_address = this->compute_static_address(addr);
  if ((target_address >= 0) && !(target_address & 1)) {
    if (is_jsr) {
      this->branch_target_addresses[target_address] = true;
    } else {
      this->branch_target_addresses.emplace(target_address, false);
    }
  }
  this->prev_was_return = ((addr.mode == AM::MEM_A) && (addr.base_reg_num == 0)); // jmp A0
  return std::format("{:<10} {}", is_jsr ? "jsr" : "jmp", this->dasm_address(addr, ValueType::INVALID, false));
}
void M68KEmulator::on_jsr_jmp(const DecodedAddress& addr, bool is_jsr) {
  auto new_pc = this->resolve_memory_address(addr, Size::LONG);
  if (is_jsr) {
    this->regs.a[7] -= 4;
    this->write(this->regs.a[7], this->regs.pc, Size::LONG);
  }
  this->regs.pc = new_pc;
  // Note: ccr not affected
}

std::string M68KEmulator::DisassemblyState::on_trap(uint8_t num) {
  return std::format("trap       {}", num);
}
void M68KEmulator::on_trap(uint8_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_unlink(uint8_t a_reg_num) {
  return std::format("unlink     A{}", a_reg_num);
}
void M68KEmulator::on_unlink(uint8_t a_reg_num) {
  this->regs.a[7] = this->regs.a[a_reg_num];
  this->regs.a[a_reg_num] = this->read(this->regs.a[7], Size::LONG);
  this->regs.a[7] += 4;
  // Note: ccr not affected
}

std::string M68KEmulator::DisassemblyState::on_move_usp(bool is_read, uint8_t a_reg_num) {
  return is_read ? std::format("move       A{}, USP", a_reg_num) : std::format("move       USP, A{}", a_reg_num);
}
void M68KEmulator::on_move_usp(bool, uint8_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_reset() {
  return "reset";
}
void M68KEmulator::on_reset() {
  throw terminate_emulation();
}

std::string M68KEmulator::DisassemblyState::on_nop() {
  return "nop";
}
void M68KEmulator::on_nop() {}

std::string M68KEmulator::DisassemblyState::on_stop(uint16_t value) {
  return std::format("stop       0x{:04X}", value);
}
void M68KEmulator::on_stop(uint16_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_rte() {
  return "rte";
}
void M68KEmulator::on_rte() {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_rtd(int16_t disp) {
  this->prev_was_return = true;
  return std::format("rtd        {}", phosg::hex(disp));
}
void M68KEmulator::on_rtd(int16_t disp) {
  this->regs.pc = this->read(this->regs.a[7], Size::LONG);
  this->regs.a[7] += 4 + disp;
}

std::string M68KEmulator::DisassemblyState::on_rts() {
  this->prev_was_return = true;
  return "rts";
}
void M68KEmulator::on_rts() {
  this->regs.pc = this->read(this->regs.a[7], Size::LONG);
  this->regs.a[7] += 4;
}

std::string M68KEmulator::DisassemblyState::on_trapv() {
  return "trapv";
}
void M68KEmulator::on_trapv() {
  if (this->regs.sr.get_v()) {
    throw std::runtime_error("Unimplemented opcode");
  }
}

std::string M68KEmulator::DisassemblyState::on_rtr() {
  return "rtr";
}
void M68KEmulator::on_rtr() {
  // The supervisor portion (high byte) of SR is unaffected
  this->regs.sr.u = (this->regs.sr.u & 0xFF00) | (this->read(this->regs.a[7], Size::WORD) & 0x00FF);
  this->regs.pc = this->read(this->regs.a[7] + 2, Size::LONG);
  this->regs.a[7] += 6;
}

std::string M68KEmulator::DisassemblyState::on_movec(bool is_write, bool is_a_reg, uint8_t reg_num, uint16_t cr_num) {
  return is_write
      ? std::format("movec      CR{}, {:c}{}", cr_num, is_a_reg ? 'A' : 'D', reg_num)
      : std::format("movec      {:c}{}, CR{}", is_a_reg ? 'A' : 'D', reg_num, cr_num);
}
void M68KEmulator::on_movec(bool, bool, uint8_t, uint16_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_addq_subq(Size size, const DecodedAddress& addr, int64_t value) {
  return std::format("{}.{}     {}, {}", (value < 0) ? "subq" : "addq", char_for_size(size),
      this->dasm_address(addr, value_type_for_size(size)), (value < 0) ? (-value) : value);
}
void M68KEmulator::on_addq_subq(Size size, const DecodedAddress& addr, int64_t value) {
  if (addr.mode == AM::A_REG) {
    // When the destination is an address register, the entire 32-bit register is used regardless of the operation
    // size, and the CCR is not affected
    this->regs.a[addr.base_reg_num] += value;

  } else { // Not address register
    auto ea = this->resolve_address(addr, size);
    uint32_t mem_value = this->read(ea, size);
    if (value < 0) {
      this->regs.set_ccr_flags_integer_subtract(mem_value, -value, size);
    } else {
      this->regs.set_ccr_flags_integer_add(mem_value, value, size);
    }
    this->write(ea, mem_value + value, size);
    this->regs.set_ccr_flags(this->regs.sr.get_c(), -1, -1, -1, -1);
  }
}

std::string M68KEmulator::DisassemblyState::on_dbcc(uint8_t condition, uint8_t reg_num, int16_t disp) {
  const char* cond = string_for_condition.at(condition);
  uint32_t target_address = this->start_address + (this->r.where() - 2) + disp;
  if (!(target_address & 1)) {
    this->branch_target_addresses.emplace(target_address, false);
  }
  return (disp < 0)
      ? std::format("db{:<8} D{}, -0x{:X} /* {:08X} */", cond, reg_num, -(disp + 2), target_address)
      : std::format("db{:<8} D{}, +0x{:X} /* {:08X} */", cond, reg_num, disp + 2, target_address);
}
void M68KEmulator::on_dbcc(uint8_t condition, uint8_t reg_num, int16_t disp) {
  if (!this->check_condition(condition)) {
    // This is not a bug: dbCC actually does only check and affect the low 16 bits
    uint16_t target = this->regs.d[reg_num].u & 0x0000FFFF;
    target--;
    this->regs.d[reg_num].u = (this->regs.d[reg_num].u & 0xFFFF0000) | target;
    if (target != 0xFFFF) {
      this->regs.pc += disp - 2;
    }
  }
  // Note: ccr not affected
}

std::string M68KEmulator::DisassemblyState::on_trapcc(uint8_t condition, int64_t value) {
  const char* cond = string_for_condition.at(condition);
  return (value < 0) ? std::format("trap{}", cond) : std::format("trap{:<6} 0x{:08X}", cond, value);
}
void M68KEmulator::on_trapcc(uint8_t, int64_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_scc(uint8_t condition, const DecodedAddress& addr) {
  return std::format("s{:<9} {}", string_for_condition.at(condition), this->dasm_address(addr, ValueType::BYTE));
}
void M68KEmulator::on_scc(uint8_t condition, const DecodedAddress& addr) {
  auto ea = this->resolve_address(addr, Size::BYTE);
  this->write(ea, (this->check_condition(condition) ? 0xFF : 0x00), Size::BYTE);
  // Note: ccr not affected
}

std::string M68KEmulator::DisassemblyState::on_bra_bsr_bcc(uint8_t condition, int32_t disp, uint8_t disp_size) {
  uint32_t target_address = this->start_address + this->r.where() + disp - disp_size;
  std::string disp_str = (disp < 0)
      ? std::format("-0x{:X} /* {:08X} */", -disp - 2, target_address)
      : std::format("+0x{:X} /* {:08X} */", disp + 2, target_address);

  if (!(target_address & 1)) {
    if (condition == 1) {
      this->branch_target_addresses[target_address] = true;
    } else {
      this->branch_target_addresses.emplace(target_address, false);
    }
  }

  if (condition == 0) {
    return std::format("bra        {}", disp_str);
  } else if (condition == 1) {
    return std::format("bsr        {}", disp_str);
  } else {
    return std::format("b{:<9} {}", string_for_condition.at(condition), disp_str);
  }
}

std::string M68KEmulator::DisassemblyState::on_bra(int32_t disp, uint8_t disp_size) {
  return this->on_bra_bsr_bcc(0, disp, disp_size);
}
void M68KEmulator::on_bra(int32_t disp, uint8_t disp_size) {
  this->regs.pc += (disp - disp_size);
  // Note: ccr not affected
}

std::string M68KEmulator::DisassemblyState::on_bsr(int32_t disp, uint8_t disp_size) {
  return this->on_bra_bsr_bcc(1, disp, disp_size);
}
void M68KEmulator::on_bsr(int32_t disp, uint8_t disp_size) {
  this->regs.a[7] -= 4;
  this->write(this->regs.a[7], this->regs.pc, Size::LONG);
  this->regs.pc += (disp - disp_size);
  // Note: ccr not affected
}

std::string M68KEmulator::DisassemblyState::on_bcc(uint8_t condition, int32_t disp, uint8_t disp_size) {
  return this->on_bra_bsr_bcc(condition, disp, disp_size);
}
void M68KEmulator::on_bcc(uint8_t condition, int32_t disp, uint8_t disp_size) {
  if (this->check_condition(condition)) {
    this->regs.pc += (disp - disp_size);
  }
  // Note: ccr not affected
}

std::string M68KEmulator::DisassemblyState::on_moveq(uint8_t d_reg_num, int8_t value) {
  return std::format("moveq.l    D{}, {}", d_reg_num, value);
}
void M68KEmulator::on_moveq(uint8_t d_reg_num, int8_t value) {
  this->regs.d[d_reg_num].s = value;
  this->regs.set_ccr_flags(-1, (value < 0), (value == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_sbcd(bool is_mem, uint8_t dest_reg, uint8_t src_reg) {
  return is_mem
      ? std::format("sbcd       -[A{}], -[A{}]", dest_reg, src_reg)
      : std::format("sbcd       D{}, D{}", dest_reg, src_reg);
}
void M68KEmulator::on_sbcd(bool, uint8_t, uint8_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_pack(bool is_mem_predec, uint8_t dest_reg_num, uint8_t src_reg_num, int16_t ext) {
  return is_mem_predec
      ? std::format("pack       -[A{}], -[A{}], 0x{:04X}", dest_reg_num, src_reg_num, ext)
      : std::format("pack       D{}, D{}, 0x{:04X}", dest_reg_num, src_reg_num, ext);
}
void M68KEmulator::on_pack(bool, uint8_t, uint8_t, int16_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_unpack(bool is_mem_predec, uint8_t dest_reg_num, uint8_t src_reg_num, int16_t ext) {
  return is_mem_predec
      ? std::format("unpack     -[A{}], -[A{}], 0x{:04X}", dest_reg_num, src_reg_num, ext)
      : std::format("unpack     D{}, D{}, 0x{:04X}", dest_reg_num, src_reg_num, ext);
}
void M68KEmulator::on_unpack(bool, uint8_t, uint8_t, int16_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_divs_word(const DecodedAddress& addr, uint8_t d_reg_num) {
  return std::format("divs.w     D{}, {}", d_reg_num, this->dasm_address(addr, ValueType::WORD));
}
void M68KEmulator::on_divs_word(const DecodedAddress& addr, uint8_t d_reg_num) {
  auto ea = this->resolve_address(addr, Size::WORD);
  uint32_t value = this->read(ea, Size::WORD);
  int32_t quotient = this->regs.d[d_reg_num].s / static_cast<int16_t>(value);
  int32_t modulo = this->regs.d[d_reg_num].s % static_cast<int16_t>(value);
  this->regs.d[d_reg_num].s = (modulo << 16) | (quotient & 0xFFFF);
  this->regs.set_ccr_flags(-1, is_negative(quotient, Size::WORD), (quotient == 0), !!(quotient & 0xFFFF0000), 0);
}

std::string M68KEmulator::DisassemblyState::on_divu_word(const DecodedAddress& addr, uint8_t d_reg_num) {
  return std::format("divu.w     D{}, {}", d_reg_num, this->dasm_address(addr, ValueType::WORD));
}
void M68KEmulator::on_divu_word(const DecodedAddress& addr, uint8_t d_reg_num) {
  auto ea = this->resolve_address(addr, Size::WORD);
  uint32_t value = this->read(ea, Size::WORD);
  uint32_t quotient = this->regs.d[d_reg_num].u / value;
  uint32_t modulo = this->regs.d[d_reg_num].u % value;
  this->regs.d[d_reg_num].u = (modulo << 16) | (quotient & 0xFFFF);
  this->regs.set_ccr_flags(-1, 0, (quotient == 0), !!(quotient & 0xFFFF0000), 0);
}

std::string M68KEmulator::DisassemblyState::on_or(Size size, const DecodedAddress& addr, uint8_t d_reg_num, bool dest_is_memory) {
  std::string ea_dasm = this->dasm_address(addr, value_type_for_size(size));
  return dest_is_memory
      ? std::format("or.{}       {}, D{}", char_for_size(size), ea_dasm, d_reg_num)
      : std::format("or.{}       D{}, {}", char_for_size(size), d_reg_num, ea_dasm);
}
void M68KEmulator::on_or(Size size, const DecodedAddress& addr, uint8_t d_reg_num, bool dest_is_memory) {
  auto ea = this->resolve_address(addr, size);
  uint32_t value = this->read(ea, size) | this->regs.d[d_reg_num].u;
  if (dest_is_memory) {
    this->write(ea, value, size);
  } else {
    this->regs.d[d_reg_num].u = value;
  }
  // We clear the high bits here, even though those bits weren't written back to the destination, because the Z flag
  // must be computed based only on the affected bits
  if (size == Size::BYTE) {
    value &= 0xFF;
  } else if (size == Size::WORD) {
    value &= 0xFFFF;
  }
  this->regs.set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_addx_subx(Size size, bool is_memory, uint8_t dest_reg_num, uint8_t src_reg_num, bool is_add) {
  char ch = char_for_size(size);
  const char* op_name = is_add ? "add" : "sub";
  return is_memory
      ? std::format("{}x.{}     -[A{}], -[A{}]", op_name, ch, dest_reg_num, src_reg_num)
      : std::format("{}x.{}     D{}, D{}", op_name, ch, dest_reg_num, src_reg_num);
}
void M68KEmulator::on_addx_subx(Size size, bool is_memory, uint8_t dest_reg_num, uint8_t src_reg_num, bool is_add) {
  uint8_t size_bytes = bytes_for_size(size);
  uint32_t mask = (size == Size::LONG) ? 0xFFFFFFFF : ((1u << (8 * size_bytes)) - 1);
  uint32_t src_value;
  uint32_t dest_value;
  uint32_t dest_addr = 0;
  if (is_memory) { // addx.S/subx.S -[Ay], -[Ax]
    this->regs.a[src_reg_num] -= ((size == Size::BYTE) && (src_reg_num == 7)) ? 2 : size_bytes;
    src_value = this->read(this->regs.a[src_reg_num], size) & mask;
    this->regs.a[dest_reg_num] -= ((size == Size::BYTE) && (dest_reg_num == 7)) ? 2 : size_bytes;
    dest_addr = this->regs.a[dest_reg_num];
    dest_value = this->read(dest_addr, size) & mask;
  } else { // addx.S/subx.S Dy, Dx
    src_value = this->regs.d[src_reg_num].u & mask;
    dest_value = this->regs.d[dest_reg_num].u & mask;
  }

  bool x_in = this->regs.sr.get_x();
  uint32_t result;
  bool carry;
  bool overflow;
  if (is_add) {
    uint64_t sum = static_cast<uint64_t>(src_value) + dest_value + x_in;
    result = static_cast<uint32_t>(sum) & mask;
    carry = (sum > mask);
    // The reference sum must be computed in a wider type; in int32_t it wraps to exactly the truncated result for long-size operands, so V would never be set
    overflow = (static_cast<int64_t>(sign_extend(result, size)) != (static_cast<int64_t>(sign_extend(dest_value, size)) + static_cast<int64_t>(sign_extend(src_value, size)) + x_in));
  } else {
    carry = ((static_cast<uint64_t>(src_value) + x_in) > dest_value);
    result = static_cast<uint32_t>(dest_value - src_value - x_in) & mask;
    overflow = (static_cast<int64_t>(sign_extend(result, size)) != (static_cast<int64_t>(sign_extend(dest_value, size)) - static_cast<int64_t>(sign_extend(src_value, size)) - x_in));
  }

  if (is_memory) {
    this->write(dest_addr, result, size);
  } else {
    this->write(ResolvedAddress{.type = ResolvedAddress::Type::D_REG, .where = dest_reg_num}, result, size);
  }

  this->regs.set_ccr_flags(carry, is_negative(result, size) ? 1 : 0, (result != 0) ? 0 : -1, overflow ? 1 : 0, carry);
}

std::string M68KEmulator::DisassemblyState::on_adda_suba(bool is_long_op, const DecodedAddress& addr, uint8_t reg_num, bool is_add) {
  const char* op_name = is_add ? "add" : "sub";
  return std::format("{}a.{:c}     A{}, {}", op_name, (is_long_op ? 'l' : 'w'), reg_num,
      this->dasm_address(addr, is_long_op ? ValueType::LONG : ValueType::WORD));
}
void M68KEmulator::on_adda_suba(bool is_long_op, const DecodedAddress& addr, uint8_t reg_num, bool is_add) {
  uint32_t mem_value = is_long_op
      ? this->read(this->resolve_address(addr, Size::LONG), Size::LONG)
      : phosg::sign_extend<uint32_t, uint16_t>(this->read(this->resolve_address(addr, Size::WORD), Size::WORD));

  if (is_add) {
    this->regs.set_ccr_flags_integer_add(this->regs.a[reg_num], mem_value, Size::LONG);
    this->regs.a[reg_num] += mem_value;
  } else {
    this->regs.set_ccr_flags_integer_subtract(this->regs.a[reg_num], mem_value, Size::LONG);
    this->regs.a[reg_num] -= mem_value;
  }
  this->regs.set_ccr_flags(this->regs.sr.get_c(), -1, -1, -1, -1);
}

std::string M68KEmulator::DisassemblyState::on_add_sub(Size size, const DecodedAddress& addr, uint8_t reg_num, bool dest_is_memory, bool is_add) {
  std::string ea_dasm = this->dasm_address(addr, value_type_for_size(size));
  char ch = char_for_size(size);
  const char* op_name = is_add ? "add" : "sub";
  return dest_is_memory
      ? std::format("{}.{}      {}, D{}", op_name, ch, ea_dasm, reg_num)
      : std::format("{}.{}      D{}, {}", op_name, ch, reg_num, ea_dasm);
}
void M68KEmulator::on_add_sub(Size size, const DecodedAddress& addr, uint8_t reg_num, bool dest_is_memory, bool is_add) {
  auto ea = this->resolve_address(addr, size);
  uint32_t mem_value = this->read(ea, size);
  uint32_t reg_value = this->read(ResolvedAddress{.type = ResolvedAddress::Type::D_REG, .where = reg_num}, size);
  if (dest_is_memory) {
    if (is_add) {
      this->regs.set_ccr_flags_integer_add(mem_value, reg_value, size);
      mem_value += reg_value;
    } else {
      this->regs.set_ccr_flags_integer_subtract(mem_value, reg_value, size);
      mem_value -= reg_value;
    }
    this->write(ea, mem_value, size);
  } else {
    if (is_add) {
      this->regs.set_ccr_flags_integer_add(reg_value, mem_value, size);
      reg_value += mem_value;
    } else {
      this->regs.set_ccr_flags_integer_subtract(reg_value, mem_value, size);
      reg_value -= mem_value;
    }
    this->write(ResolvedAddress{.type = ResolvedAddress::Type::D_REG, .where = reg_num}, reg_value, size);
  }
  this->regs.set_ccr_flags(this->regs.sr.get_c(), -1, -1, -1, -1);
}

std::string M68KEmulator::DisassemblyState::on_syscall(uint16_t opcode) {
  if (this->is_mac_environment) {
    uint16_t syscall_number;
    bool auto_pop = false;
    uint8_t flags = 0;
    if (opcode & 0x0800) {
      syscall_number = opcode & 0x0BFF;
      auto_pop = opcode & 0x0400;
    } else {
      syscall_number = opcode & 0xFF;
      flags = (opcode >> 8) & 7;
    }

    std::string ret = "syscall    ";
    const auto* syscall_info = info_for_68k_trap(syscall_number, flags);
    if (syscall_info) {
      ret += syscall_info->name;
    } else {
      ret += std::format("0x{:03X}", syscall_number);
    }

    if (flags != syscall_info->flags) {
      ret += std::format(", flags={}", flags);
    }

    if (auto_pop) {
      ret += ", auto_pop";
    }

    if (syscall_info && syscall_info->signature_known) {
      ret += std::format(" /* {} */", syscall_info->str(true));
    }

    return ret;

  } else { // Not Mac environment
    this->prev_was_valid = false;
    return std::format(".invalid   0x{:04X}", opcode);
  }
}
void M68KEmulator::on_syscall(uint16_t opcode) {
  if (this->syscall_handler) {
    this->syscall_handler(*this, opcode);
  } else {
    throw std::runtime_error("Unimplemented opcode");
  }
}

std::string M68KEmulator::DisassemblyState::on_cmpm(Size size, uint8_t a_reg1, uint8_t a_reg2) {
  return std::format("cmpm.{}     [A{}]+, [A{}]+", char_for_size(size), a_reg1, a_reg2);
}
void M68KEmulator::on_cmpm(Size size, uint8_t a_reg1, uint8_t a_reg2) {
  auto right_addr = this->resolve_address(DecodedAddress{.mode = AM::MEM_A_POSTINC, .base_reg_num = a_reg2}, size);
  auto left_addr = this->resolve_address(DecodedAddress{.mode = AM::MEM_A_POSTINC, .base_reg_num = a_reg1}, size);
  this->regs.set_ccr_flags_integer_subtract(this->read(left_addr, size), this->read(right_addr, size), size);
}

std::string M68KEmulator::DisassemblyState::on_cmp(Size size, uint8_t d_reg_num, const DecodedAddress& addr) {
  return std::format("cmp.{}      D{}, {}",
      char_for_size(size), d_reg_num, this->dasm_address(addr, value_type_for_size(size)));
}
void M68KEmulator::on_cmp(Size size, uint8_t d_reg_num, const DecodedAddress& addr) {
  auto ea = this->resolve_address(addr, size);
  int32_t left_value = this->regs.d[d_reg_num].u;
  if (size == Size::BYTE) {
    left_value &= 0x000000FF;
  } else if (size == Size::WORD) {
    left_value &= 0x0000FFFF;
  }
  this->regs.set_ccr_flags_integer_subtract(left_value, this->read(ea, size), size);
}

std::string M68KEmulator::DisassemblyState::on_cmpa(bool is_long_op, uint8_t a_reg_num, const DecodedAddress& addr) {
  return std::format("cmpa.{:c}     A{}, {}", (is_long_op ? 'l' : 'w'), a_reg_num,
      this->dasm_address(addr, is_long_op ? ValueType::LONG : ValueType::WORD));
}
void M68KEmulator::on_cmpa(bool is_long_op, uint8_t a_reg_num, const DecodedAddress& addr) {
  Size size = is_long_op ? Size::LONG : Size::WORD;
  auto ea = this->resolve_address(addr, size);
  this->regs.set_ccr_flags_integer_subtract(
      this->regs.a[a_reg_num], sign_extend(this->read(ea, size), size), Size::LONG);
}

std::string M68KEmulator::DisassemblyState::on_xor(Size size, uint8_t reg_num, const DecodedAddress& addr) {
  return std::format("xor.{}      {}, D{}", char_for_size(size), this->dasm_address(addr, value_type_for_size(size)),
      reg_num);
}
void M68KEmulator::on_xor(Size size, uint8_t reg_num, const DecodedAddress& addr) {
  auto ea = this->resolve_address(addr, size);
  uint32_t v = this->read(ea, size) ^ this->regs.d[reg_num].u;
  if (size == Size::BYTE) {
    v &= 0xFF;
  } else if (size == Size::WORD) {
    v &= 0xFFFF;
  }
  this->write(ea, v, size);
  this->regs.set_ccr_flags(-1, is_negative(v, size), (v == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_muls_word(const DecodedAddress& addr, uint8_t reg_num) {
  return std::format("muls.w     D{}, {}", reg_num, this->dasm_address(addr, ValueType::WORD));
}
void M68KEmulator::on_muls_word(const DecodedAddress& addr, uint8_t reg_num) {
  auto ea = this->resolve_address(addr, Size::WORD);
  int32_t left = static_cast<int16_t>(this->regs.d[reg_num].u & 0x0000FFFF);
  int32_t right = static_cast<int16_t>(this->read(ea, Size::WORD));
  this->regs.d[reg_num].s = left * right;
  this->regs.set_ccr_flags(-1, (this->regs.d[reg_num].s < 0), (this->regs.d[reg_num].s == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_mulu_word(const DecodedAddress& addr, uint8_t reg_num) {
  return std::format("mulu.w     D{}, {}", reg_num, this->dasm_address(addr, ValueType::WORD));
}
void M68KEmulator::on_mulu_word(const DecodedAddress& addr, uint8_t reg_num) {
  auto ea = this->resolve_address(addr, Size::WORD);
  uint32_t left = this->regs.d[reg_num].u & 0x0000FFFF;
  uint32_t right = this->read(ea, Size::WORD);
  this->regs.d[reg_num].u = left * right;
  this->regs.set_ccr_flags(-1, is_negative(this->regs.d[reg_num].u, Size::LONG), (this->regs.d[reg_num].u == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_abcd(bool is_mem, uint8_t reg_x, uint8_t reg_y) {
  return is_mem
      ? std::format("abcd       -[A{}], -[A{}]", reg_x, reg_y)
      : std::format("abcd       D{}, D{}", reg_x, reg_y);
}
void M68KEmulator::on_abcd(bool, uint8_t, uint8_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_exg_d_d(uint8_t d_reg1, uint8_t d_reg2) {
  return std::format("exg        D{}, D{}", d_reg1, d_reg2);
}
void M68KEmulator::on_exg_d_d(uint8_t d_reg1, uint8_t d_reg2) {
  uint32_t tmp = this->regs.d[d_reg1].u;
  this->regs.d[d_reg1].u = this->regs.d[d_reg2].u;
  this->regs.d[d_reg2].u = tmp;
  // Note: ccr not affected
}

std::string M68KEmulator::DisassemblyState::on_exg_a_a(uint8_t a_reg1, uint8_t a_reg2) {
  return std::format("exg        A{}, A{}", a_reg1, a_reg2);
}
void M68KEmulator::on_exg_a_a(uint8_t a_reg1, uint8_t a_reg2) {
  uint32_t tmp = this->regs.a[a_reg1];
  this->regs.a[a_reg1] = this->regs.a[a_reg2];
  this->regs.a[a_reg2] = tmp;
  // Note: ccr not affected
}

std::string M68KEmulator::DisassemblyState::on_exg_d_a(uint8_t d_reg, uint8_t a_reg) {
  return std::format("exg        D{}, A{}", d_reg, a_reg);
}
void M68KEmulator::on_exg_d_a(uint8_t d_reg, uint8_t a_reg) {
  uint32_t tmp = this->regs.a[a_reg];
  this->regs.a[a_reg] = this->regs.d[d_reg].u;
  this->regs.d[d_reg].u = tmp;
  // Note: ccr not affected
}

std::string M68KEmulator::DisassemblyState::on_and(Size size, const DecodedAddress& addr, uint8_t reg_num, bool dest_is_mem) {
  char ch = char_for_size(size);
  return dest_is_mem
      ? std::format("and.{:c}      {}, D{}", ch, this->dasm_address(addr, value_type_for_size(size)), reg_num)
      : std::format("and.{:c}      D{}, {}", ch, reg_num, this->dasm_address(addr, value_type_for_size(size)));
}
void M68KEmulator::on_and(Size size, const DecodedAddress& addr, uint8_t reg_num, bool dest_is_mem) {
  ResolvedAddress reg{.type = ResolvedAddress::Type::D_REG, .where = reg_num};
  auto ea = this->resolve_address(addr, size);
  uint32_t value = this->read(ea, size) & this->read(reg, size);
  this->write(dest_is_mem ? ea : reg, value, size);
  this->regs.set_ccr_flags(-1, is_negative(value, size), (value == 0), 0, 0);
}

std::string M68KEmulator::DisassemblyState::on_bf_ops(
    uint8_t which,
    const DecodedAddress& addr,
    uint8_t x_reg,
    bool offset_is_reg,
    int32_t offset,
    bool width_is_reg,
    uint8_t width) {
  static constexpr std::array<const char*, 8> names{
      "bftst", "bfextu", "bfchg", "bfexts", "bfclr", "bfffo", "bfset", "bfins"};

  std::string ea_dasm = this->dasm_address(addr, ValueType::LONG);
  std::string offset_str = std::format("{}{}", offset_is_reg ? "D" : "", offset);
  // If immediate, 0 in the width field means 32
  std::string width_str;
  if (!width_is_reg && (width == 0)) {
    width_str = "32";
  } else {
    width_str = std::format("{}{}", width_is_reg ? "D" : "", width);
  }

  if (which & 1) {
    // bfins reads data from Dn; all the others write to Dn
    if (which == 0x0F) {
      return std::format("{:<10} {} {{{}:{}}}, D{}", names[which], ea_dasm, offset_str, width_str, x_reg);
    } else {
      return std::format("{:<10} D{}, {} {{{}:{}}}", names[which], x_reg, ea_dasm, offset_str, width_str);
    }
  } else {
    return std::format("{:<10} {} {{{}:{}}}", names[which], ea_dasm, offset_str, width_str);
  }
}
void M68KEmulator::on_bf_ops(
    uint8_t which,
    const DecodedAddress& addr,
    uint8_t dest_reg,
    bool offset_is_reg,
    int32_t offset,
    bool width_is_reg,
    uint8_t width) {
  if (offset_is_reg) {
    offset = this->regs.d[offset & 7].s;
  }
  if (width_is_reg) {
    width = this->regs.d[width & 7].u & 0x1F;
  }
  if (width == 0) {
    width = 32;
  }

  phosg::be_uint64_t orig_value;
  int64_t mem_addr = -1;
  uint8_t rel_offset;
  if (addr.mode == AM::D_REG) {
    // Bit fields can wrap around within the register (see figure 1-18 in the manual). However, the width still cannot
    // be greater than 32, so to handle this, we just duplicate the register's value into both halves of a uint64_t.
    rel_offset = offset & 0x1F;
    orig_value = (static_cast<uint64_t>(this->regs.d[addr.base_reg_num].u) << 32) | regs.d[addr.base_reg_num].u;
  } else {
    // Similar to above, the width cannot be greater than 32, so we extract as many bytes as required (which may be up
    // to 5) into a uint64_t, then get the field value from that.
    rel_offset = offset & 7;
    mem_addr = this->resolve_memory_address(addr, Size::BYTE) + (offset / 8);
    this->mem->memcpy(&orig_value, mem_addr, ((offset & 7) + width + 7) / 8);
  }
  uint32_t field_value = (orig_value >> (64 - rel_offset - width));

  // Most of these these opcodes set the following flags based on the field's value before it is modified (if the
  // opcode modifies it):
  //   X = unchanged
  //   N = set if MSB of field is set
  //   Z = set if all bits in field are zero
  //   V = cleared
  //   C = cleared
  // However, bfins sets these flags according to the inserted value, not the pre-modification value, so we have to
  // recompute the flags during execution below in that case.
  bool n_flag = (field_value >> (width - 1)) & 1;
  bool z_flag = (field_value == 0);

  bool should_write = false;
  switch (which) {
    case 0: // bftst: set flags only
      break;
    case 1: // bfextu: extract bit field (zero-extended) into D register
      this->regs.d[dest_reg].u = field_value;
      break;
    case 2: // bfchg: invert all bits in field
      field_value = ~field_value;
      should_write = true;
      break;
    case 3: // bfexts: extract bit field (sign-extended) into D register
      this->regs.d[dest_reg].u = field_value;
      if ((width < 32) && (field_value & (1UL << (width - 1)))) {
        this->regs.d[dest_reg].u |= (0xFFFFFFFF << width);
      }
      break;
    case 4: // bfclr: clear all bits in field
      field_value = 0;
      should_write = true;
      break;
    case 0xD: { // bfffo: find offset of the first set bit, counted from the field's own offset
      uint32_t z = 0;
      for (; (z < width) && !(field_value & (1UL << (width - z - 1))); z++) {
      }
      this->regs.d[dest_reg].s = offset + z;
      break;
    }
    case 6: // bfset: set all bits in the field
      field_value = 0xFFFFFFFF;
      should_write = true;
      break;
    case 7: { // bfins: write value from D register into the field
      field_value = this->regs.d[dest_reg].u;
      if (width < 32) {
        field_value &= ((1UL << width) - 1);
      }
      n_flag = (field_value >> (width - 1)) & 1;
      z_flag = (field_value == 0);
      should_write = true;
      break;
    }
  }

  if (should_write) {
    // Insert the new field value into the original 64-bit value so it can be written back easily
    uint64_t mask = ((1ULL << width) - 1) << (64 - rel_offset - width);
    phosg::be_uint64_t new_value = (orig_value & (~mask)) | ((static_cast<uint64_t>(field_value) << (rel_offset + width)) & mask);
    if (addr.mode == AM::D_REG) {
      // Take the 32 bits starting at offset and put them into the corresponding bits in the destination register,
      // wrapping around if necessary (which it will be, unless offset is 0)
      uint32_t low_mask = (1ULL << (32 - rel_offset)) - 1;
      this->regs.d[addr.base_reg_num].u = ((new_value >> 32) & low_mask) | (new_value & (~low_mask));
    } else {
      // Just write the appropriate number of bytes back to memory from the new value
      if (mem_addr < 0) {
        throw std::logic_error("Memory address was not computed before bit-field writeback");
      }
      uint64_t mask = ((1ULL << width) - 1) << (64 - rel_offset - width);
      phosg::be_uint64_t new_value = (orig_value & (~mask)) | ((static_cast<uint64_t>(field_value) << (rel_offset + width)) & mask);
      this->mem->memcpy(mem_addr, &new_value, (rel_offset + width + 7) / 8);
    }
  }

  this->regs.set_ccr_flags(-1, n_flag, z_flag, false, false);
}

std::string M68KEmulator::DisassemblyState::on_bit_shift_mem(uint8_t which, const DecodedAddress& addr) {
  static constexpr std::array<const char*, 8> names{
      "asr.w", "asl.w", "lsr.w", "lsl.w", "roxr.w", "roxl.w", "ror.w", "rol.w"};
  return std::format("{:<10} {}", names[which], this->dasm_address(addr, ValueType::WORD));
}
void M68KEmulator::on_bit_shift_mem(uint8_t which, const DecodedAddress& addr) {
  uint32_t ea = this->resolve_memory_address(addr, Size::WORD);
  uint16_t value = this->read(ea, Size::WORD) & 0xFFFF;
  bool is_left_shift = (which & 1);

  // Condition bits have the same behavior as in on_bit_shift_reg below
  int8_t c_flag = (is_left_shift ? (value >> 15) : value) & 1;
  int8_t x_flag = (which >= 6) ? -1 : c_flag;
  int8_t v_flag = 0;
  uint16_t x_in = this->regs.sr.get_x();

  switch (which) {
    case 0: // asr
      value = static_cast<int16_t>(value) >> 1;
      break;
    case 2: // lsr
      value >>= 1;
      break;
    case 1: // asl
      v_flag = (((value >> 15) ^ (value >> 14)) & 1); // asl sets V if the sign bit changes
      [[fallthrough]];
    case 3: // lsl
      value <<= 1;
      break;
    case 4: // roxr
      value = (value >> 1) | (x_in << 15);
      break;
    case 5: // roxl
      value = (value << 1) | x_in;
      break;
    case 6: // ror
      value = (value >> 1) | (value << 15);
      break;
    case 7: // rol
      value = (value << 1) | (value >> 15);
      break;
  }
  this->write(ea, value, Size::WORD);
  this->regs.set_ccr_flags(x_flag, (value >> 15) & 1, (value == 0), v_flag, c_flag);
}

std::string M68KEmulator::DisassemblyState::on_bit_shift_reg(
    uint8_t which, Size size, uint8_t reg_num, bool count_is_reg, uint8_t count) {
  static constexpr std::array<const char*, 8> names{"asr", "asl", "lsr", "lsl", "roxr", "roxl", "ror", "rol"};
  std::string name = std::format("{}.{}", names[which], char_for_size(size));
  return std::format("{:<10} D{}, {}{}", name, reg_num, count_is_reg ? "D" : "", count);
}
void M68KEmulator::on_bit_shift_reg(uint8_t which, Size size, uint8_t reg_num, bool count_is_reg, uint8_t count) {
  if (count_is_reg) {
    // Presumably this is mod 64 because of the roxl/roxr instructions, which can conceivably desire a count of 32.
    count = this->regs.d[count].u & 0x0000003F;
  }

  ResolvedAddress reg_addr{.type = ResolvedAddress::Type::D_REG, .where = reg_num};
  uint64_t value = this->read(reg_addr, size);
  uint8_t size_bits = bytes_for_size(size) * 8;
  uint64_t mask = (1ULL << size_bits) - 1;
  uint64_t msb = (1ULL << (size_bits - 1));

  // Condition bits:
  //   X = (rol/ror) unaffected; (others) last bit shifted/rotated out; unaffected for 0 shift count
  //   N = set if MSB of result is set; cleared otherwise
  //   Z = set if result is 0; cleared otherwise
  //   V = (asl/asr) set if MSB changes at any time during operation; cleared otherwise; (others) always cleared
  //   C = last bit shifted/rotated out; cleared for 0 shift except for roxl/roxr, which set it to X for 0 shift
  int8_t new_x = -1;
  int8_t new_v = 0; // Only nonzero for asl (sign bit cannot change during asr, and others all always clear V)
  int8_t new_c = -1;
  bool old_x = this->regs.sr.get_x();

  switch (which) {
    case 0: // asr
      if (count == 0) {
        new_c = 0;
      } else if (count < size_bits) {
        new_x = (value >> (count - 1)) & 1;
        new_c = new_x;
        value >>= count;
        if (value & (1ULL << (size_bits - count - 1))) { // Sign-extend
          value |= ~((1ULL << (size_bits - count)) - 1);
        }
      } else { // count >= size_bits: all relevant bits shifted out; result is all copies of the original sign bit
        new_x = (value & msb) ? 1 : 0;
        new_c = new_x;
        value = new_x ? mask : 0;
      }
      break;
    case 1: // asl
    case 3: // lsl
      if (count == 0) {
        new_c = 0;
      } else if (count < size_bits) {
        uint32_t sign_region = value >> (size_bits - count - 1);
        uint32_t sign_mask = (1ULL << (count + 1)) - 1;
        new_x = sign_region & 2;
        new_v = (which == 1) ? ((sign_region != 0) && (sign_region != sign_mask)) : 0;
        new_c = new_x;
        value <<= count;
      } else { // count >= size_bits: all relevant bits shifted out; result is zero
        // Last bit shifted out is 0 unless count == size_bits (then it's the lowest bit in value)
        new_x = (count == size_bits) ? (value & 1) : 0;
        new_v = (which == 1) ? (value != 0) : 0;
        new_c = new_x;
        value = 0;
      }
      break;
    case 2: // lsr
      if (count == 0) {
        new_c = 0;
      } else if (count < size_bits) {
        new_x = (value >> (count - 1)) & 1;
        new_c = new_x;
        value >>= count;
      } else { // count >= size_bits: all relevant bits shifted out; result is zero
        new_x = (count == size_bits) ? !!(value & msb) : 0;
        new_c = new_x;
        value = 0;
      }
      break;
    case 4: // roxr
    case 5: { // roxl
      // roxl is the reverse of roxr; the "base implementation" below is for roxr, so for roxl we just adjust the
      // rotate count and use the same logic.
      uint8_t limit = size_bits + 1;
      uint8_t effective_count = (count % limit);
      if (effective_count > 0) {
        if (which == 5) {
          effective_count = limit - effective_count;
        }
        new_x = (value >> (effective_count - 1)) & 1;
        value = (value >> effective_count) |
            (static_cast<uint64_t>(old_x) << (size_bits - effective_count)) |
            (value << (size_bits - effective_count + 1));
        new_c = new_x;
      } else {
        new_c = old_x;
      }
      break;
    }
    case 6: // ror
    case 7: { // rol
      uint8_t effective_count = (count % size_bits);
      if (effective_count > 0) {
        if (which == 7) {
          effective_count = size_bits - effective_count;
        }
        value = (value >> effective_count) | (value << (size_bits - effective_count));
      }
      // This is separate because count could be a multiple of size_bits; in that case, C shouldn't be unconditionally
      // cleared
      if (count > 0) {
        new_c = (which == 7) ? (value & 1) : !!(value & msb);
      } else {
        new_c = 0;
      }
      break;
    }
  }

  this->write(reg_addr, value, size);

  this->regs.set_ccr_flags(new_x, (value & msb) ? 1 : 0, ((value & mask) == 0), new_v, new_c);
}

std::string M68KEmulator::DisassemblyState::on_fmovecr(uint8_t f_reg, uint8_t offset) {
  static constexpr std::array<const char*, 0x40> names = {
      // clang-format off
      /* 00 */ "pi", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
      /* 08 */ nullptr, nullptr, nullptr, "log10(2)", "e", "log2(e)", "log10(e)", "0",
      /* 10 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
      /* 18 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
      /* 20 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
      /* 28 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
      /* 30 */ "ln(2)", "ln(10)", "10^0", "10^1", "10^2", "10^4", "10^8", "10^16",
      /* 38 */ "10^32", "10^64", "10^128", "10^256", "10^512", "10^1024", "10^2048", "10^4096",
      // clang-format on
  };
  const char* name = names.at(offset);
  return name
      ? std::format("fmovecr    FP{}, 0x{:02X} /* {} */", f_reg, offset, name)
      : std::format("fmovecr    FP{}, 0x{:02X}", f_reg, offset);
}
void M68KEmulator::on_fmovecr(uint8_t, uint8_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fmove_to_mem(
    const DecodedAddress& addr, uint8_t f_reg, ValueType format, uint8_t k) {
  if (format == ValueType::PACKED_DECIMAL_REAL_STATIC_K) {
    return std::format("fmove.p    {}, FP{}, D{}", this->dasm_address(addr, format), f_reg, k);
  } else if (format == ValueType::PACKED_DECIMAL_REAL_DYNAMIC_K) {
    return std::format("fmove.p    {}, FP{}, {}", this->dasm_address(addr, format), f_reg, k);
  } else {
    return std::format("fmove.{}    {}, FP{}", char_for_value_type(format), this->dasm_address(addr, format), f_reg);
  }
}
void M68KEmulator::on_fmove_to_mem(const DecodedAddress&, uint8_t, ValueType, uint8_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fmove(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fmove", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fmove(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fint(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fint", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fint(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsinh(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fsinh", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fsinh(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fintrz(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fintrz", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fintrz(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsqrt(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fsqrt", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fsqrt(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_flognp1(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("flognp1", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_flognp1(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fetoxm1(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fetoxm1", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fetoxm1(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_ftanh(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("ftanh", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_ftanh(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fatan(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fatan", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fatan(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fasin(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fasin", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fasin(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fatanh(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fatanh", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fatanh(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsin(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fsin", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fsin(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_ftan(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fsin", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_ftan(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fetox(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fetox", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fetox(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_ftwotox(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("ftwotox", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_ftwotox(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_ftentox(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("ftentox", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_ftentox(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_flogn(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("flogn", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_flogn(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_flog10(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("flog10", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_flog10(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_flog2(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("flog2", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_flog2(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fabs(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fabs", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fabs(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fcosh(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fcosh", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fcosh(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fneg(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fneg", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fneg(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_facos(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("facos", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_facos(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fcos(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fcos", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fcos(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fgetexp(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fgetexp", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fgetexp(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fgetman(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fgetman", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fgetman(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fdiv(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fdiv", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fdiv(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fmod(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fmod", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fmod(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fadd(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fadd", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fadd(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fmul(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fmul", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fmul(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsgldiv(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fsgldiv", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fsgldiv(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_frem(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("frem", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_frem(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fscale(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fscale", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fscale(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsglmul(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fsglmul", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fsglmul(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsub(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fsub", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fsub(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsincos(uint8_t sin_reg, uint8_t cos_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  if (!is_mem_read) {
    return std::format("fsincos.x  FP{}, FP{}, FP{}", sin_reg, cos_reg, src_spec);
  } else {
    M68KEmulator::ValueType value_type = static_cast<M68KEmulator::ValueType>(src_spec);
    return std::format("fsincos.{}  FP{}, FP{}, {}",
        char_for_value_type(value_type), sin_reg, cos_reg, this->dasm_address(addr, value_type));
  }
}
void M68KEmulator::on_fsincos(uint8_t, uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fcmp(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fcmp", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fcmp(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_ftst(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("ftst", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_ftst(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsmove(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fsmove", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fsmove(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fssqrt(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fssqrt", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fssqrt(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fdmove(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fdmove", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fdmove(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fdsqrt(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fdsqrt", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fdsqrt(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsabs(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fsabs", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fsabs(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsneg(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fsneg", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fsneg(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fdabs(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fdabs", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fdabs(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fdneg(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fdneg", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fdneg(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsdiv(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fsdiv", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fsdiv(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsadd(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fsadd", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fsadd(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsmul(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fsmul", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fsmul(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fddiv(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fddiv", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fddiv(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fdadd(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fdadd", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fdadd(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fdmul(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fdmul", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fdmul(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fssub(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fssub", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fssub(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fdsub(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  return this->dasm_float_mem_op("fdsub", f_reg, src_spec, addr, is_mem_read);
}
void M68KEmulator::on_fdsub(uint8_t, uint8_t, const DecodedAddress&, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fmovem_control_regs(const DecodedAddress& addr, uint8_t mask, bool is_write) {
  // TODO: Use the register names here instead of just the mask value
  return is_write
      ? std::format("fmovem     {}, 0x{:02X}", this->dasm_address(addr, ValueType::INVALID), mask)
      : std::format("fmovem     0x{:02X}, {}", mask, this->dasm_address(addr, ValueType::INVALID));
}
void M68KEmulator::on_fmovem_control_regs(const DecodedAddress&, uint8_t, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fmovem_data_regs(
    const DecodedAddress& addr, bool mask_is_d_reg, uint8_t mask, bool is_write) {
  std::string mask_str;
  if (mask_is_d_reg) {
    mask_str = std::format("D{}", (mask >> 4) & 7);
  } else if (addr.mode == AM::MEM_A_PREDEC) {
    for (int8_t z = 7; z >= 0; z--) {
      if (mask & (1 << z)) {
        mask_str += std::format("{}FP{}", mask_str.empty() ? "" : ",", z);
      }
    }
  } else {
    for (uint8_t z = 0; z < 8; z++) {
      if (mask & (0x80 >> z)) {
        mask_str += std::format("{}FP{}", mask_str.empty() ? "" : ",", z);
      }
    }
  }
  return is_write
      ? std::format("fmovem.x   {}, {}", this->dasm_address(addr, ValueType::EXTENDED), mask_str)
      : std::format("fmovem.x   {}, {}", mask_str, this->dasm_address(addr, ValueType::EXTENDED));
}
void M68KEmulator::on_fmovem_data_regs(const DecodedAddress&, bool, uint8_t, bool) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fdbcc(uint8_t condition, uint8_t reg, int16_t disp) {
  const char* cond = string_for_float_condition.at(condition);
  uint32_t target_address = this->start_address + (this->r.where() - 2) + disp;
  if (!(target_address & 1)) {
    this->branch_target_addresses.emplace(target_address, false);
  }
  return (disp < 0)
      ? std::format("fdb{:<7} D{}, -0x{:X} /* {:08X} */", cond, reg, -disp + 2, target_address)
      : std::format("fdb{:<7} D{}, +0x{:X} /* {:08X} */", cond, reg, disp + 2, target_address);
}
void M68KEmulator::on_fdbcc(uint8_t, uint8_t, int16_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_ftrapcc(uint8_t condition, int64_t value) {
  const char* cond = string_for_float_condition.at(condition);
  return (value < 0) ? std::format("trap{}", cond) : std::format("ftrap{:<5} 0x{:08X}", cond, value);
}
void M68KEmulator::on_ftrapcc(uint8_t, int64_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fscc(uint8_t condition, const DecodedAddress& addr) {
  return std::format("fs{:<8} {}", string_for_float_condition.at(condition), this->dasm_address(addr, ValueType::BYTE));
}
void M68KEmulator::on_fscc(uint8_t, const DecodedAddress&) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fbcc(uint8_t condition, int32_t disp, uint8_t disp_size) {
  uint32_t target_address = this->start_address + this->r.where() + disp - disp_size;
  std::string disp_str = (disp < 0)
      ? std::format("-0x{:X} /* {:08X} */", -disp - 2, target_address)
      : std::format("+0x{:X} /* {:08X} */", disp + 2, target_address);

  if (!(target_address & 1)) {
    this->branch_target_addresses.emplace(target_address, false);
  }
  return std::format("fb{:<8} {}", string_for_float_condition.at(condition), disp_str);
}
void M68KEmulator::on_fbcc(uint8_t, int32_t, uint8_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_fsave(const DecodedAddress& addr) {
  return std::format("fsave      {}", this->dasm_address(addr, ValueType::INVALID));
}
void M68KEmulator::on_fsave(const DecodedAddress&) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_frestore(const DecodedAddress& addr) {
  return std::format("frestore   {}", this->dasm_address(addr, ValueType::INVALID));
}
void M68KEmulator::on_frestore(const DecodedAddress&) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::on_coprocessor(uint16_t opcode) {
  return std::format(".cpuext    0x{:04X}", opcode);
}
void M68KEmulator::on_coprocessor(uint16_t) {
  throw std::runtime_error("Unimplemented opcode");
}

std::string M68KEmulator::DisassemblyState::dasm_reg_mask(uint16_t mask, bool reverse) {
  std::vector<uint8_t> d_ranges;
  std::vector<uint8_t> a_ranges;
  auto add_reg = [&](uint8_t num) {
    auto& ranges = (num & 8) ? a_ranges : d_ranges;
    num &= 7;
    if (!ranges.empty() && (ranges.back() == num - 1)) {
      ranges.back() = num;
    } else {
      ranges.emplace_back(num);
      ranges.emplace_back(num);
    }
  };

  std::string ret = "(";
  auto add_formatted_ranges = [&ret](char reg_type, const std::vector<uint8_t>& ranges) -> void {
    for (size_t z = 0; z < ranges.size(); z += 2) {
      uint8_t low = ranges[z];
      uint8_t high = ranges.at(z + 1);
      if (ret.size() > 1) {
        ret += ", ";
      }
      if (low == high) {
        ret += std::format("{:c}{}", reg_type, low);
      } else {
        ret += std::format("{:c}{}-{:c}{}", reg_type, low, reg_type, high);
      }
    }
  };

  if (reverse) {
    for (ssize_t x = 15; x >= 0; x--) {
      if (mask & (1 << x)) {
        add_reg(15 - x);
      }
    }
    std::reverse(d_ranges.begin(), d_ranges.end());
    std::reverse(a_ranges.begin(), a_ranges.end());
    add_formatted_ranges('A', a_ranges);
    add_formatted_ranges('D', d_ranges);

  } else {
    for (ssize_t x = 0; x < 16; x++) {
      if (mask & (1 << x)) {
        add_reg(x);
      }
    }
    add_formatted_ranges('D', d_ranges);
    add_formatted_ranges('A', a_ranges);
  }

  ret.push_back(')');
  return ret;
}

std::string M68KEmulator::DisassemblyState::dasm_float_mem_op(
    const char* name, uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read) {
  if (!is_mem_read) {
    std::string name_str = std::format("{}.x", name);
    return std::format("{:<10} FP{}, FP{}", name_str, f_reg, src_spec);
  } else {
    M68KEmulator::ValueType value_type = static_cast<M68KEmulator::ValueType>(src_spec);
    std::string name_str = std::format("{}.{}", name, char_for_value_type(value_type));
    return std::format("{:<10} FP{}, {}", name_str, f_reg, this->dasm_address(addr, value_type));
  }
}

int64_t M68KEmulator::DisassemblyState::compute_static_address(const DecodedAddress& addr) {
  switch (addr.mode) {
    case AM::MEM_PC_DISP: // [base_pc + base_disp]
      return (addr.base_pc + addr.base_disp) & 0xFFFFFFFF;
    case AM::MEM_PC_INDEX: // [base_pc + X(index_reg_num).S * scale + base_disp]
      if (!addr.suppress_index) {
        return -1;
      } else if (!addr.suppress_base_reg) {
        return (addr.base_pc + addr.base_disp) & 0xFFFFFFFF;
      } else { // Both suppressed
        return static_cast<uint32_t>(addr.base_disp);
      }
    case AM::MEM_PC_IND_POST: // [[base_pc + base_disp] + X(index_reg_num).S * scale + outer_disp]
    case AM::MEM_PC_IND_PRE: { // [[base_pc + base_disp + X(index_reg_num).S * scale] + outer_disp]
      int64_t inner_resolved_addr = -1;
      if (!addr.suppress_index) {
        return -1;
      } else if (!addr.suppress_base_reg) {
        inner_resolved_addr = (addr.base_pc + addr.base_disp) & 0xFFFFFFFF;
      } else { // Both suppressed
        inner_resolved_addr = static_cast<uint32_t>(addr.base_disp);
      }
      if (((inner_resolved_addr >= static_cast<int64_t>(this->start_address)) &&
              (static_cast<size_t>(inner_resolved_addr - this->start_address) <= (this->r.size() - 4)))) {
        return (this->r.pget_u32b(inner_resolved_addr) + addr.outer_disp) & 0xFFFFFFFF;
      } else {
        return -1;
      }
    }
    case AM::MEM_ABSOLUTE: // [base_disp]
      return static_cast<uint32_t>(addr.base_disp);
    default:
      return -1;
  }
}

std::string M68KEmulator::DisassemblyState::dasm_address(
    const DecodedAddress& addr, ValueType type, bool add_data_comments) {
  auto format_disp32 = [](int32_t disp) -> std::string {
    if (disp == 0) {
      return "";
    } else if (disp == -0x80000000LL) {
      return "-0x80000000";
    } else if (disp < -0xFFFF) {
      return std::format("-0x{:08X}", -disp);
    } else if (disp < -0xFF) {
      return std::format("-0x{:04X}", -disp);
    } else if (disp < 0) {
      return std::format("-0x{:02X}", -disp);
    } else if (disp > 0xFFFF) {
      return std::format("+0x{:08X}", disp);
    } else if (disp > 0xFF) {
      return std::format("+0x{:04X}", disp);
    } else {
      return std::format("+0x{:02X}", disp);
    }
  };

  auto merge_memory_reference = [](const std::vector<std::string>& tokens) -> std::string {
    if (tokens.size() == 1) {
      return std::format("[{}]", tokens[0]);
    }
    std::string ret = "[";
    for (size_t z = 0; z < tokens.size(); z++) {
      const auto& token = tokens[z];
      if (token.empty()) {
        continue;
      }
      if ((ret.size() > 1) && ((token[0] == '-') || (token[0] == '+'))) {
        ret += std::format(" {:c} {}", token[0], token.c_str() + 1);
      } else if (ret.size() > 1) {
        ret += std::format(" + {}", token);
      } else {
        ret += token;
      }
    }
    if (ret.size() == 1) {
      // This only happens if the base and index are suppressed and the displacement is 0; it's still valid though
      return "[0x00000000]";
    }
    ret.push_back(']');
    return ret;
  };

  auto format_index = [&addr]() -> std::string {
    std::string ret = std::format("{}{}{}", addr.index_is_a_reg ? 'A' : 'D', addr.index_reg_num, addr.index_is_word ? ".w" : "");
    if (addr.index_scale > 1) {
      ret += std::format(" * {}", addr.index_scale);
    }
    return ret;
  };

  auto addr_is_valid = [this](int64_t addr, size_t size = sizeof(phosg::be_uint32_t)) -> bool {
    return ((addr >= static_cast<int64_t>(this->start_address)) &&
        (static_cast<size_t>(addr - this->start_address) <= (this->r.size() - size)));
  };

  std::string ret;
  std::vector<std::string> comment_tokens;
  switch (addr.mode) {
    case AM::D_REG:
      ret = std::format("D{}", addr.base_reg_num);
      break;
    case AM::A_REG:
      ret = std::format("A{}", addr.base_reg_num);
      break;
    case AM::MEM_A:
      ret = std::format("[A{}]", addr.base_reg_num);
      break;
    case AM::MEM_A_POSTINC:
      ret = std::format("[A{}]+", addr.base_reg_num);
      break;
    case AM::MEM_A_PREDEC:
      ret = std::format("-[A{}]", addr.base_reg_num);
      break;
    case AM::MEM_A_DISP: // [A(base_reg_num) + base_disp]
      ret = merge_memory_reference({std::format("A{}", addr.base_reg_num), format_disp32(addr.base_disp)});
      // Special case: in the Classic Mac OS environment, the jump table is located at A5. So if displacement is
      // positive and aligned with a jump table entry, and the base reg is A5, write the export label name as well
      if (this->is_mac_environment &&
          (addr.base_reg_num == 5) &&
          (addr.base_disp >= 0x20) &&
          ((addr.base_disp & 7) == 2)) {
        size_t export_number = (addr.base_disp - 0x22) / 8;
        if (!this->jump_table) {
          comment_tokens.emplace_back(std::format("export_{}", export_number));
        } else if (export_number < this->jump_table->size()) {
          const auto& entry = (*this->jump_table)[export_number];
          comment_tokens.emplace_back(std::format("export_{}, CODE:{} @ {:08X}",
              export_number, entry.code_resource_id, entry.offset));
        } else {
          comment_tokens.emplace_back(std::format("export_{}, out of jump table range", export_number));
        }
      }
      break;
    case AM::MEM_PC_DISP: // [base_pc + base_disp]
      ret = merge_memory_reference({"PC", format_disp32(addr.base_disp)});
      break;
    case AM::MEM_A_INDEX: // [A(base_reg_num) + X(index_reg_num).S * scale + base_disp]
    case AM::MEM_PC_INDEX: { // [base_pc + X(index_reg_num).S * scale + base_disp]
      bool is_pc = (addr.mode == AM::MEM_PC_INDEX);
      if (addr.suppress_base_reg && addr.suppress_index) {
        ret = std::format("[0x{:08X}]", static_cast<uint32_t>(addr.base_disp));
      } else if (addr.suppress_index && is_pc) {
        ret = merge_memory_reference({"PC", format_disp32(addr.base_disp)});
      } else {
        ret = merge_memory_reference({
            addr.suppress_base_reg ? "" : (is_pc ? "PC" : std::format("A{}", addr.base_reg_num)),
            addr.suppress_index ? "" : format_index(),
            format_disp32(addr.base_disp),
        });
      }
      break;
    }
    case AM::MEM_A_IND_POST: // [[An + base_disp] + X(index_reg_num).S * scale + outer_disp]
    case AM::MEM_PC_IND_POST: { // [[base_pc + base_disp] + X(index_reg_num).S * scale + outer_disp]
      bool is_pc = (addr.mode == AM::MEM_PC_IND_POST);
      std::string inner_ref;
      if (addr.suppress_base_reg) {
        inner_ref = std::format("[0x{:08X}]", static_cast<uint32_t>(addr.base_disp));
      } else if (is_pc) {
        inner_ref = merge_memory_reference({"PC", format_disp32(addr.base_disp)});
      } else {
        inner_ref = merge_memory_reference({std::format("A{}", addr.base_reg_num), format_disp32(addr.base_disp)});
      }
      ret = merge_memory_reference({
          inner_ref,
          addr.suppress_index ? "" : format_index(),
          format_disp32(addr.outer_disp),
      });
      break;
    }
    case AM::MEM_A_IND_PRE: // [[An + base_disp + X(index_reg_num).S * scale] + outer_disp]
    case AM::MEM_PC_IND_PRE: { // [[base_pc + base_disp + X(index_reg_num).S * scale] + outer_disp]
      bool is_pc = (addr.mode == AM::MEM_PC_IND_PRE);
      std::string inner_ref;
      if (addr.suppress_base_reg && addr.suppress_index) {
        inner_ref = std::format("[0x{:08X}]", static_cast<uint32_t>(addr.base_disp));
      } else if (is_pc) {
        inner_ref = merge_memory_reference({"PC", format_index(), format_disp32(addr.base_disp)});
      } else {
        inner_ref = merge_memory_reference(
            {std::format("A{}", addr.base_reg_num), format_index(), format_disp32(addr.base_disp)});
      }
      ret = merge_memory_reference({inner_ref, format_disp32(addr.outer_disp)});
      break;
    }
    case AM::MEM_ABSOLUTE: // [base_disp]
      ret = std::format("[0x{:08X}]", static_cast<uint32_t>(addr.base_disp));
      break;
    case AM::IMM: // imm8/16/32 (in base_disp)
      // Note: this can add a comment string to ret, but we also never add anything to comment_tokens in this case, so
      // the result will never have two separate comments in it
      ret = format_immediate(addr.base_disp);
      break;
    case AM::INVALID: // (invalid_reason not null)
      this->prev_was_valid = false;
      ret = std::format("<< invalid address: {} >>", addr.invalid_reason);
      break;
    default:
      throw std::logic_error("Invalid address type");
  }

  int64_t static_addr = this->compute_static_address(addr);
  if (static_addr >= 0) {
    if (add_data_comments) {
      // The code and the lowmem globals occupy logically different address spaces which may overlap at disassembly time
      // (since the code is relocated and globals are not). We use the addressing mode to tell them apart
      if (this->is_mac_environment && (addr.mode == AM::MEM_ABSOLUTE)) {
        const char* name = name_for_lowmem_global(static_addr);
        if (name) {
          comment_tokens.emplace_back(name);
        }
      }
      if (addr.mode != AM::MEM_ABSOLUTE) {
        comment_tokens.emplace_back(std::format("0x{:08X}", static_addr));
        size_t offset = static_addr - this->start_address;
        switch (type) {
          case ValueType::BYTE:
            if (addr_is_valid(static_addr, 1)) {
              comment_tokens.emplace_back(std::format("value {}", format_immediate(this->r.pget_u8(offset), false)));
            }
            break;
          case ValueType::WORD:
            if (addr_is_valid(static_addr, 2)) {
              comment_tokens.emplace_back(std::format("value {}", format_immediate(this->r.pget_u16b(offset), false)));
            }
            break;
          case ValueType::LONG:
            if (addr_is_valid(static_addr, 4)) {
              comment_tokens.emplace_back(std::format("value {}", format_immediate(this->r.pget_u32b(offset), false)));
            }
            break;
          case ValueType::FLOAT:
            if (addr_is_valid(static_addr, 4)) {
              comment_tokens.emplace_back(std::format("value {:g}", this->r.pget_f32b(offset)));
            }
            break;
          case ValueType::DOUBLE:
            if (addr_is_valid(static_addr, 4)) {
              comment_tokens.emplace_back(std::format("value {:g}", this->r.pget_f64b(offset)));
            }
            break;
          case ValueType::EXTENDED:
            if (addr_is_valid(static_addr, 6)) {
              // Bits: (sign)x1 (exponent)x15 (unused)x16 (mantissa)x64
              // TODO: It'd be nice to format these as actual decimal values, but this is complicated since apparently
              // the mantissa is not required to be in the range [1, 2), so it's not analogous to an IEEE float with
              // more bits, for example
              uint16_t exponent_bits = this->r.pget_u16b(offset);
              uint64_t mantissa_bits = this->r.pget_u64b(offset);
              comment_tokens.emplace_back(std::format("extended: ({:c}) exponent=0x{:04X} mantissa=0x{:016X}",
                  (exponent_bits & 0x8000) ? '-' : '+', exponent_bits & 0x7FFF, mantissa_bits));
            }
            break;
          case ValueType::PACKED_DECIMAL_REAL:
            if (addr_is_valid(static_addr, 12)) {
              uint32_t high = this->r.pget_u32b(static_addr);
              uint64_t low = this->r.pget_u64b(static_addr);
              // Bits (square-bracketed groups are BCD digits): MGYY [EEEE]x3 [WWWW] (XXXX)x2 [IIII] [FFFF]x16
              //   M = mantissa sign
              //   G = exponent sign
              //   Y = control bits for special values (Inf, NaN, etc.)
              //   E = exponent digits
              //   W = overflow exponent digit (unused at read time)
              //   X = unused
              //   I = integer digit
              //   F = fraction digits
              // Special values:
              //   +/- Inf: M=SIGN G=1 Y=11 EEE=FFF I=? D=0000000000000000
              //   +/- NaN: M=SIGN G=1 Y=11 EEE=FFF I=? D=anything nonzero
              //   +/- zero: M=SIGN G=? Y=?? EEE=??? (but must be valid digits) I=0 D=0000000000000000
              if ((high & 0x7FFF0000) == 0x7FFF0000) {
                if (low == 0) {
                  return (high & 0x80000000) ? "-Infinity" : "+Infinity";
                } else {
                  return (high & 0x80000000) ? "-NaN" : "+NaN";
                }
              } else if (((high >> 16) & 0x0FFF) == 0) {
                return std::format("{}{:01X}.{:016X}", (high & 0x80000000) ? '-' : '+', high & 0x0000000F, low);
              } else {
                return std::format("{}{:01X}.{:016X}e{}{:03X}",
                    (high & 0x80000000) ? '-' : '+', high & 0x0000000F, low,
                    (high & 0x40000000) ? '-' : '+', (high >> 16) & 0x0FFF);
              }
            }
            break;
          default:
            break;
        }

        std::string estimated_pstring = estimate_pstring(this->r, offset);
        if (!estimated_pstring.empty()) {
          comment_tokens.emplace_back("pstring " + estimated_pstring);
        } else {
          std::string estimated_cstring = estimate_cstring(this->r, offset);
          if (!estimated_cstring.empty()) {
            comment_tokens.emplace_back("cstring " + estimated_cstring);
          }
        }
      }

    } else if (addr.mode != AM::MEM_ABSOLUTE) { // It's a jmp/jsr [PC + ...]; just put the target address
      comment_tokens.emplace_back(std::format("{:08X}", static_addr));
    }
  }

  if (!comment_tokens.empty()) {
    ret += std::format(" /* {} */", phosg::join(comment_tokens, "; "));
  }

  return ret;
}

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
  return (ch == '_') || (ch == '%') || (ch == '.') || (ch == ' ') ||
      (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

static bool try_decode_macsbug_symbol_part(phosg::StringReader& r, std::string& symbol, uint16_t symbol_length) {
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
  std::string symbol;
  uint16_t num_constants = 0;
};

static DecodedSymbol try_decode_macsbug_symbol(phosg::StringReader& r) {
  // All indented comments are from "Macsbug Reference and Debugging Guide", page 367, and "Building and Managing
  // Programs in MPW", page B-25f

  if (r.remaining() < 2) {
    return {};
  }

  uint32_t start = r.where();
  uint8_t symbol_0 = r.get_u8();
  uint8_t symbol_1 = r.get_u8();
  uint8_t symbol_0_low7 = symbol_0 & 0x7F;
  uint8_t symbol_1_low7 = symbol_1 & 0x7F;

  //    With fixed-length format, the first byte is in the range $20 through $7F. The high-order bit may or may not be
  //    set.

  std::string symbol;
  if (symbol_0_low7 >= 0x20 && symbol_0_low7 <= 0x7F) {
    //    The high-order bit of the second byte is set for 16-character names, clear for 8-character names. Fixed-
    //    length 16-character names are used in object Pascal to show class.method names instead of procedure names.
    //    The method name is contained in the first 8 bytes and the class name is in the second 8 bytes. MacsBug swaps
    //    the order and inserts the period before displaying the name.

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
    //    With variable-length format, the first byte is in the range $80 to $9F. Stripping the high-order bit produces
    //    a length in the range $00 through $1F. If the length is 0, the next byte contains the actual length, in the
    //    range $01 through $FF [otherwise the next byte is the name's first character]. Data after the name starts on
    //    a word boundary.

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

      //    Compilers can place a procedure’s constant data immediately after the procedure in memory. The first word
      //    after the name specifies how many bytes of constant data are present. If there are no constants, a length
      //    of 0 must be given.

      uint16_t num_constants = r.get_u16b();
      // TODO: unclear if this necessary, or if the size of the constants is always even
      if (num_constants & 1) {
        ++num_constants;
      }
      if (num_constants <= r.remaining()) {
        return {symbol, num_constants};
      }
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
      is_mac_environment(is_mac_environment),
      jump_table(jump_table) {}

std::string M68KEmulator::disassemble_one(DisassemblyState& s) {
  size_t opcode_offset = s.r.where();
  std::string opcode_disassembly;
  if (s.is_mac_environment && s.prev_was_return) {
    auto [symbol, num_constant_bytes] = try_decode_macsbug_symbol(s.r);
    if (!symbol.empty()) {
      // We have a MacsBug symbol plus additional constant data
      // TODO: decode type/length of symbol like ResEdit/Resorcerer do?
      opcode_disassembly = std::format("dc.b       \"{}\"", symbol);

      if (num_constant_bytes > 0) {
        // TODO: disassemble constants instead of skipping them
        opcode_disassembly += std::format(" + {} constant bytes", num_constant_bytes);
        s.r.skip(num_constant_bytes);
      }
    }
  }
  s.prev_was_return = false;

  if (opcode_disassembly.empty()) {
    // Didn't decode any MacsBug symbol: disassemble instruction
    s.opcode_start_address = s.start_address + s.r.where();
    try {
      opcode_disassembly = M68KEmulator::decode_instruction(s);
    } catch (const std::out_of_range&) {
      if (s.r.where() == opcode_offset) {
        // There must be at least 1 byte available since r.eof() was false
        s.r.get_u8();
      }
      s.prev_was_valid = false;
      opcode_disassembly = ".incomplete";
    }
  }

  std::string line;
  {
    std::string hex_data;
    size_t end_offset = s.r.where();
    if (end_offset <= opcode_offset) {
      throw std::logic_error(std::format("disassembly did not advance; used {:X}/{:X} bytes", s.r.where(), s.r.size()));
    }

    for (s.r.go(opcode_offset); s.r.where() < (end_offset & (~1));) {
      hex_data += std::format(" {:04X}", s.r.get_u16b());
    }
    if (end_offset & 1) {
      // This should only happen for .incomplete at the end of the stream
      hex_data += std::format(" {:02X}  ", s.r.get_u8());
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

M68KEmulator::DisassembleResult M68KEmulator::disassemble_one_structured(DisassemblyState& s) {
  DisassembleResult ret;

  size_t opcode_offset = s.r.where();
  s.opcode_start_address = s.start_address + opcode_offset;

  std::string disassembly;
  try {
    disassembly = M68KEmulator::decode_instruction(s);
  } catch (const std::out_of_range&) {
    if (s.r.where() == opcode_offset) {
      // There must be at least 1 byte available since r.eof() was false
      s.r.get_u8();
    }
    s.prev_was_valid = false;
    disassembly = ".incomplete";
  }
  ret.segments.emplace_back(DisassembleResult::Segment{
      .is_valid = s.prev_was_valid,
      .address = s.opcode_start_address,
      .size = s.r.where() - opcode_offset,
      .disassembly = std::move(disassembly),
      .imm_offsets = s.imm_offsets,
  });

  return ret;
}

std::string M68KEmulator::disassemble_one(
    const void* vdata,
    size_t size,
    uint32_t start_address,
    bool is_mac_environment,
    const std::vector<JumpTableEntry>* jump_table) {
  DisassemblyState s(vdata, size, start_address, is_mac_environment, jump_table);
  return M68KEmulator::disassemble_one(s);
}

M68KEmulator::DisassembleResult M68KEmulator::disassemble_one_structured(
    const void* vdata,
    size_t size,
    uint32_t start_address,
    bool is_mac_environment,
    const std::vector<JumpTableEntry>* jump_table) {
  DisassemblyState s(vdata, size, start_address, is_mac_environment, jump_table);
  return M68KEmulator::disassemble_one_structured(s);
}

std::string M68KEmulator::disassemble(
    const void* vdata,
    size_t size,
    uint32_t start_address,
    const std::multimap<uint32_t, std::string>* labels,
    bool is_mac_environment,
    const std::vector<JumpTableEntry>* jump_table) {
  static const std::multimap<uint32_t, std::string> empty_labels_map = {};
  if (!labels) {
    labels = &empty_labels_map;
  }

  std::map<uint32_t, std::pair<std::string, uint32_t>> lines; // {pc: (line, next_pc)}

  // Phase 1: Generate the disassembly for each opcode, and collect branch target addresses
  // TODO: Rewrite this to use a queue of pending PCs to disassemble instead of explicitly doing backups in a separate
  // phase. The code should look like:
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
    std::string line = std::format("{:08X} ", s.opcode_start_address);
    line += M68KEmulator::disassemble_one(s);
    line += '\n';
    uint32_t next_pc = s.r.where() + s.start_address;
    lines.emplace(s.opcode_start_address, std::make_pair(std::move(line), next_pc));
  }

  // Phase 2: Handle backups. Because opcodes can be different lengths in the 68K architecture, sometimes we mis-
  // disassemble an opcode because it starts during a previous "opcode" that is actually unused or data. To handle
  // this, we re-disassemble any branch targets and labels that are word-aligned, are within the address space, and do
  // not have an existing line.
  std::unordered_set<uint32_t> pending_start_addrs;
  for (const auto& target_it : s.branch_target_addresses) {
    uint32_t target_pc = target_it.first;
    if (!(target_pc & 1) && (target_pc >= s.start_address) && (target_pc < s.start_address + size) && !lines.count(target_pc)) {
      pending_start_addrs.emplace(target_pc);
    }
  }
  for (const auto& label_it : *labels) {
    uint32_t target_pc = label_it.first;
    if (!(target_pc & 1) && (target_pc >= s.start_address) && (target_pc < s.start_address + size) && !lines.count(target_pc)) {
      pending_start_addrs.emplace(target_pc);
    }
  }
  std::set<std::pair<uint32_t, uint32_t>> backup_branches; // {start_pc, end_pc}
  while (!pending_start_addrs.empty()) {
    auto pending_start_addrs_it = pending_start_addrs.begin();
    uint32_t branch_start_pc = *pending_start_addrs_it;
    pending_start_addrs.erase(pending_start_addrs_it);
    uint32_t pc = branch_start_pc;
    s.r.go(pc - s.start_address);

    s.prev_was_return = false;
    while (!lines.count(pc) && !s.r.eof()) {
      std::string line = std::format("{:08X} ", pc);
      std::map<uint32_t, bool> temp_branch_target_addresses;
      s.branch_target_addresses.swap(temp_branch_target_addresses);
      line += M68KEmulator::disassemble_one(s);
      s.branch_target_addresses.swap(temp_branch_target_addresses);
      line += '\n';
      uint32_t next_pc = s.r.where() + s.start_address;
      lines.emplace(pc, std::make_pair(std::move(line), next_pc));
      pc = next_pc;

      // If any new branch target addresses were generated, we may need to do more backups for them as well - we need
      // to add them to both sets.
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

  // Phase 3: generate output lines, including passed-in labels, branch target labels, and alternate branches
  size_t ret_bytes = 0;
  std::deque<std::string> ret_lines;
  auto branch_target_it = s.branch_target_addresses.lower_bound(s.start_address);
  auto label_it = labels->lower_bound(s.start_address);
  auto backup_branch_it = backup_branches.begin();

  auto add_line = [&](uint32_t pc, const std::string& line) {
    for (; label_it != labels->end() && label_it->first <= pc; label_it++) {
      std::string label;
      if (label_it->first != pc) {
        label = std::format("{}: // at {:08X} (misaligned)\n", label_it->second, label_it->first);
      } else {
        label = std::format("{}:\n", label_it->second);
      }
      ret_bytes += label.size();
      ret_lines.emplace_back(std::move(label));
    }
    for (; (branch_target_it != s.branch_target_addresses.end()) && (branch_target_it->first <= pc);
        branch_target_it++) {
      std::string label;
      const char* label_type = branch_target_it->second ? "fn" : "label";
      if (branch_target_it->first != pc) {
        label = std::format("{}{:08X}: // (misaligned)\n", label_type, branch_target_it->first);
      } else {
        label = std::format("{}{:08X}:\n", label_type, branch_target_it->first);
      }
      ret_bytes += label.size();
      ret_lines.emplace_back(std::move(label));
    }

    ret_bytes += line.size();
    // TODO: we can eliminate this copy by making ret_lines instead keep references into the lines map. We can't just
    // move the line contents into ret_lines here because disassembly lines may appear multiple times in the output.
    // (Technically this should not be true, but I'm too lazy to verify as such right now.)
    ret_lines.emplace_back(line);
  };

  for (auto line_it = lines.begin(); line_it != lines.end(); line_it = lines.find(line_it->second.second)) {
    uint32_t pc = line_it->first;
    std::string& line = line_it->second.first;

    // Write branches first, if there are any here
    for (; backup_branch_it != backup_branches.end() && backup_branch_it->first <= pc; backup_branch_it++) {
      uint32_t start_pc = backup_branch_it->first;
      uint32_t end_pc = backup_branch_it->second;
      auto orig_branch_target_it = branch_target_it;
      auto orig_label_it = label_it;
      branch_target_it = s.branch_target_addresses.lower_bound(start_pc);
      label_it = labels->lower_bound(start_pc);

      std::string branch_start_comment = std::format("// begin alternate branch {:08X}-{:08X}\n", start_pc, end_pc);
      ret_bytes += branch_start_comment.size();
      ret_lines.emplace_back(std::move(branch_start_comment));

      for (auto backup_line_it = lines.find(start_pc);
          (backup_line_it != lines.end()) && (backup_line_it->first != end_pc);
          backup_line_it = lines.find(backup_line_it->second.second)) {
        add_line(backup_line_it->first, backup_line_it->second.first);
      }

      std::string branch_end_comment = std::format("// end alternate branch {:08X}-{:08X}\n", start_pc, end_pc);
      ret_bytes += branch_end_comment.size();
      ret_lines.emplace_back(std::move(branch_end_comment));

      branch_target_it = orig_branch_target_it;
      label_it = orig_label_it;
    }

    add_line(pc, line);
  }

  // Phase 4: assemble the output lines into a single string and return it
  std::string ret;
  ret.reserve(ret_bytes);
  for (const auto& line : ret_lines) {
    ret += line;
  }
  return ret;
}

void M68KEmulator::execute_one() {
  // Call debug hook if present
  if (this->debug_hook) {
    this->debug_hook(*this);
  }

  // Call any timer interrupt functions scheduled for this cycle
  if (this->interrupt_manager) {
    this->interrupt_manager->on_cycle_start();
  }

  // Execute a cycle
  M68KEmulator::decode_instruction(*this);

  this->instructions_executed++;
}

void M68KEmulator::execute() {
  if (!this->interrupt_manager.get()) {
    this->interrupt_manager = std::make_shared<InterruptManager>();
  }

  for (;;) {
    try {
      this->execute_one();
    } catch (const terminate_emulation&) {
      break;
    }
  }
}

void M68KEmulator::import_state(FILE* stream) {
  uint8_t version = phosg::freadx<uint8_t>(stream);
  if (version != 0) {
    throw std::runtime_error("unknown format version");
  }
  this->regs.import_state(stream);
  this->mem->import_state(stream);
}

void M68KEmulator::export_state(FILE* stream) const {
  phosg::fwritex<uint8_t>(stream, 0); // version
  this->regs.export_state(stream);
  this->mem->export_state(stream);
}

M68KEmulator::AssembleResult M68KEmulator::assemble(
    const std::string&, std::function<std::string(const std::string&)>, uint32_t) {
  throw std::runtime_error("M68KEmulator::assemble is not implemented");
}

M68KEmulator::AssembleResult M68KEmulator::assemble(
    const std::string& text, const std::vector<std::string>& include_dirs, uint32_t start_address) {
  if (include_dirs.empty()) {
    return M68KEmulator::assemble(text, nullptr, start_address);

  } else {
    std::unordered_set<std::string> get_include_stack;
    std::function<std::string(const std::string&)> get_include = [&](const std::string& name) -> std::string {
      for (const auto& dir : include_dirs) {
        std::string filename = dir + "/" + name + ".inc.s";
        if (std::filesystem::is_regular_file(filename)) {
          if (!get_include_stack.emplace(name).second) {
            throw std::runtime_error("mutual recursion between includes: " + name);
          }
          const auto& ret = M68KEmulator::assemble(phosg::load_file(filename), get_include, start_address).code;
          get_include_stack.erase(name);
          return ret;
        }
        filename = dir + "/" + name + ".inc.bin";
        if (std::filesystem::is_regular_file(filename)) {
          return phosg::load_file(filename);
        }
      }
      throw std::runtime_error("data not found for include: " + name);
    };
    return M68KEmulator::assemble(text, get_include, start_address);
  }
}

} // namespace ResourceDASM
