#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <deque>
#include <unordered_map>
#include <unordered_set>

#include "mc68k.hh"

using namespace std;



static const uint8_t SIZE_BYTE = static_cast<uint8_t>(Size::BYTE);
static const uint8_t SIZE_WORD = static_cast<uint8_t>(Size::WORD);
static const uint8_t SIZE_LONG = static_cast<uint8_t>(Size::LONG);
static const uint8_t TSIZE_WORD = static_cast<uint8_t>(TSize::WORD);
static const uint8_t TSIZE_LONG = static_cast<uint8_t>(TSize::LONG);
static const uint8_t DSIZE_BYTE = static_cast<uint8_t>(DSize::BYTE);
static const uint8_t DSIZE_WORD = static_cast<uint8_t>(DSize::WORD);
static const uint8_t DSIZE_LONG = static_cast<uint8_t>(DSize::LONG);



static const unordered_map<uint8_t, char> char_for_size({
  {SIZE_BYTE, 'b'},
  {SIZE_WORD, 'w'},
  {SIZE_LONG, 'l'},
  {3, '?'},
});

static const unordered_map<uint8_t, char> char_for_tsize({
  {TSIZE_WORD, 'w'},
  {TSIZE_LONG, 'l'},
});

static const unordered_map<uint8_t, char> char_for_dsize({
  {DSIZE_BYTE, 'b'},
  {DSIZE_WORD, 'w'},
  {DSIZE_LONG, 'l'},
  {3, '?'},
});

static const unordered_map<uint8_t, uint8_t> size_for_tsize({
  {TSIZE_WORD, SIZE_WORD},
  {TSIZE_LONG, SIZE_LONG},
});

static const unordered_map<uint8_t, uint8_t> size_for_dsize({
  {DSIZE_BYTE, SIZE_BYTE},
  {DSIZE_WORD, SIZE_WORD},
  {DSIZE_LONG, SIZE_LONG},
});

static const unordered_map<uint8_t, const char*> string_for_condition({
  {0x00, "t "},
  {0x01, "f "},
  {0x02, "hi"},
  {0x03, "ls"},
  {0x04, "cc"},
  {0x05, "cs"},
  {0x06, "ne"},
  {0x07, "eq"},
  {0x08, "vc"},
  {0x09, "vs"},
  {0x0A, "pl"},
  {0x0B, "mi"},
  {0x0C, "ge"},
  {0x0D, "lt"},
  {0x0E, "gt"},
  {0x0F, "le"},
});



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



int64_t read_immediate(StringReader& r, uint8_t s) {
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

inline bool maybe_char(uint8_t ch) {
  return (ch == 0) || (ch == '\t') || (ch == '\r') || (ch == '\n') || ((ch >= 0x20) && (ch <= 0x7E));
}

string format_immediate(int64_t value) {
  string hex_repr = string_printf("0x%" PRIX64, value);

  string char_repr;
  for (ssize_t shift = 56; shift >= 0; shift-= 8) {
    uint8_t byte = (value >> shift) & 0xFF;
    if (!maybe_char(byte)) {
      return hex_repr;
    }
    if (char_repr.empty() && (byte == 0)) {
      continue; // ignore leading \0 bytes
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

  return string_printf("%s /* \'%s\' */", hex_repr.c_str(), char_repr.c_str());
}



string disassemble_opcode_F(StringReader& r, uint32_t start_address, unordered_set<uint32_t>& branch_target_addresses) {
  uint16_t opcode = r.get_u16r();
  return string_printf(".extension 0x%03hX // unimplemented", opcode & 0x0FFF);
}

string disassemble_reg_mask(uint16_t mask, bool reverse) {
  if (mask == 0) {
    return "<none>";
  }

  string ret;
  if (reverse) {
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

  } else {
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
  }

  ret.resize(ret.size() - 1); // remove the last ','
  return ret;
}

string disassemble_address_extension(StringReader& r, uint16_t ext, int8_t An) {
  bool index_is_a_reg = ext & 0x8000;
  uint8_t index_reg_num = static_cast<uint8_t>((ext >> 12) & 7);
  bool index_is_word = !(ext & 0x0800); // true = signed word, false = long
  uint8_t scale = 1 << ((ext >> 9) & 3);

  string ret;

  if (!(ext & 0x0100)) {
    // brief extension word
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

  // full extension word
  // page 43 in the programmers' manual
  bool include_base_register = !(ext & 0x0080);
  bool include_index_register = !(ext & 0x0040);
  // 1 = null displacement, 2 = word displacement, 3 = long displacement
  uint8_t base_displacement_size = (ext & 0x0030) >> 4;
  uint8_t index_indirect_select = ext & 7;

  // include_index_register, index_indirect_select, result
  // true, 0, No Memory Indirect Action
  // true, 1, Indirect Preindexed with Null Outer Displacement
  // true, 2, Indirect Preindexed with Word Outer Displacement
  // true, 3, Indirect Preindexed with Long Outer Displacement
  // true, 4, Reserved
  // true, 5, Indirect Postindexed with Null Outer Displacement
  // true, 6, Indirect Postindexed with Word Outer Displacement
  // true, 7, Indirect Postindexed with Long Outer Displacement
  // false, 0, No Memory Indirect Action
  // false, 1, Memory Indirect with Null Outer Displacement
  // false, 2, Memory Indirect with Word Outer Displacement
  // false, 3, Memory Indirect with Long Outer Displacement
  // false, 4, Reserved
  // false, 5, Reserved
  // false, 6, Reserved
  // false, 7, Reserved

  // no memory indirect action is like this (I'm guessing; manual is confusing):
  //   [base_disp + index_reg.SIZE * SCALE]
  // indirect preindexed is like this:
  //   [[An + base_disp + index_reg.SIZE * SCALE] + outer_disp]
  // indirect postindexed is like this:
  //   [[An + base_disp] + index_reg.SIZE * SCALE + outer_disp]
  // memory indirect is like this (I'm guessing; this isn't in the manual):
  //   [[An + base_disp] + outer_disp]
  // note that An is determined by the caller (it's not part of the extension).
  // An can also be -1, which means PC.

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

string estimate_pstring(const StringReader& r, uint32_t addr) {
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

string disassemble_address(StringReader& r, uint32_t opcode_start_address,
    uint8_t M, uint8_t Xn, uint8_t size, unordered_set<uint32_t>* branch_target_addresses) {
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
      return disassemble_address_extension(r, ext, Xn);
    }
    case 7: {
      switch (Xn) {
        case 0: {
          uint32_t address = r.get_u16r();
          if (address & 0x00008000) {
            address |= 0xFFFF0000;
          }
          return string_printf("[0x%08" PRIX32 "]", address);
        }
        case 1: {
          uint32_t address = r.get_u32r();
          return string_printf("[0x%08" PRIX32 "]", address);
        }
        case 2: {
          int16_t displacement = r.get_s16r();
          uint32_t target_address = opcode_start_address + displacement + 2;
          if (branch_target_addresses) {
            branch_target_addresses->emplace(target_address);
          }
          if (displacement == 0) {
            return string_printf("[PC] /* label%08" PRIX32 " */", target_address);
          } else {
            string offset_str = (displacement > 0) ? string_printf(" + 0x%" PRIX16, displacement) : string_printf(" - 0x%" PRIX16, -displacement);
            string estimated_pstring = estimate_pstring(r, target_address);
            if (estimated_pstring.size()) {
              return string_printf("[PC%s /* label%08" PRIX32 ", pstring %s */]", offset_str.c_str(), target_address, estimated_pstring.c_str());
            } else {
              return string_printf("[PC%s /* label%08" PRIX32 " */]", offset_str.c_str(), target_address);
            }
          }
        }
        case 3: {
          uint16_t ext = r.get_u16r();
          return disassemble_address_extension(r, ext, -1);
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

string disassemble_opcode_0123(StringReader& r, uint32_t start_address, unordered_set<uint32_t>& branch_target_addresses) {
  // 1, 2, 3 are actually also handled by 0 (this is the only case where the i
  // field is split)
  uint32_t opcode_start_address = start_address + r.where();
  uint16_t op = r.get_u16r();
  uint8_t i = op_get_i(op);
  if (i) {
    uint8_t size = size_for_dsize.at(i);
    if (op_get_b(op) == 1) {
      // movea isn't valid with the byte operand size. we'll disassemble it
      // anyway, but complain at the end of the line
      if (i == SIZE_BYTE) {
        return "movea.b    <<invalid>>";
      }

      uint8_t source_M = op_get_c(op);
      uint8_t source_Xn = op_get_d(op);
      string source_addr = disassemble_address(r, opcode_start_address, source_M, source_Xn, size, NULL);

      uint8_t An = op_get_a(op);
      if (i == SIZE_BYTE) {
        return string_printf(".invalid   A%d, %s // movea not valid with byte operand size",
            An, source_addr.c_str());
      } else {
        return string_printf("movea.%c    A%d, %s", char_for_dsize.at(i), An, source_addr.c_str());
      }

    } else {
      // note: empirically the order seems to be source addr first, then dest
      // addr. this is relevant when both contain displacements or extensions
      uint8_t source_M = op_get_c(op);
      uint8_t source_Xn = op_get_d(op);
      string source_addr = disassemble_address(r, opcode_start_address, source_M, source_Xn, size, NULL);

      // note: this isn't a bug; the instruction format actually is <r1><m1><m2><r2>
      uint8_t dest_M = op_get_b(op);
      uint8_t dest_Xn = op_get_a(op);
      string dest_addr = disassemble_address(r, opcode_start_address, dest_M, dest_Xn, size, NULL);

      return string_printf("move.%c     %s, %s", char_for_dsize.at(i),
          dest_addr.c_str(), source_addr.c_str());
    }
  }

  // note: i == 0 if we get here
  // 0000000000000000
  // iiiiaaabbbcccddd
  //   zz   gss  vvvv
  //          t
  //     kkkkyyyyyyyy

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

    string addr = disassemble_address(r, opcode_start_address, M, Xn, s, NULL);
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

  string addr = disassemble_address(r, opcode_start_address, M, Xn, s, NULL);
  string imm = format_immediate(read_immediate(r, s));
  return string_printf("%s %s, %s%s", operation.c_str(), addr.c_str(),
      imm.c_str(), invalid_str);
}

string disassemble_opcode_4(StringReader& r, uint32_t start_address, unordered_set<uint32_t>& branch_target_addresses) {
  uint32_t opcode_start_address = start_address + r.where();
  uint16_t op = r.get_u16r();
  uint8_t g = op_get_g(op);

  if (g == 0) {
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
          return ".invalid   // invalid special operation";
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
      string addr = disassemble_address(r, opcode_start_address, op_get_c(op), op_get_d(op), SIZE_LONG, NULL);

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
            string addr = disassemble_address(r, opcode_start_address, M, op_get_d(op), size_for_tsize.at(t), NULL);
            string reg_mask = disassemble_reg_mask(r.get_u16r(), false);
            return string_printf("movem.%c    %s, %s", char_for_tsize.at(t), addr.c_str(), reg_mask.c_str());
          }
        }
        if (b == 0) {
          string addr = disassemble_address(r, opcode_start_address, M, op_get_d(op), SIZE_BYTE, NULL);
          return string_printf("nbcd.b     %s", addr.c_str());
        }
        // b == 1
        if (M == 0) {
          return string_printf("swap.w     D%d", op_get_d(op));
        }
        string addr = disassemble_address(r, opcode_start_address, M, op_get_d(op), SIZE_LONG, NULL);
        return string_printf("pea.l      %s", addr.c_str());

      } else if (a == 5) {
        if (b == 3) {
          string addr = disassemble_address(r, opcode_start_address, op_get_c(op), op_get_d(op), SIZE_LONG, NULL);
          return string_printf("tas.b      %s", addr.c_str());
        }

        string addr = disassemble_address(r, opcode_start_address, op_get_c(op), op_get_d(op), b, NULL);
        return string_printf("tst.%c      %s", char_for_size.at(b), addr.c_str());

      } else if (a == 6) {
        uint8_t t = op_get_t(op);
        string addr = disassemble_address(r, opcode_start_address, op_get_c(op), op_get_d(op), size_for_tsize.at(t), NULL);
        string reg_mask = disassemble_reg_mask(r.get_u16r(), true);
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
            return string_printf("move.usp   A%d, %s", op_get_d(op), (c & 1) ? "store" : "load");
          }

        } else if (b == 2) {
          string addr = disassemble_address(r, opcode_start_address, op_get_c(op), op_get_d(op), b, &branch_target_addresses);
          return string_printf("jsr        %s", addr.c_str());

        } else if (b == 3) {
          string addr = disassemble_address(r, opcode_start_address, op_get_c(op), op_get_d(op), SIZE_LONG, &branch_target_addresses);
          return string_printf("jmp        %s", addr.c_str());
        }
      }

      return ".invalid   // invalid opcode 4";
    }

  } else { // g == 1
    uint8_t b = op_get_b(op);
    if (b == 7) {
      string addr = disassemble_address(r, opcode_start_address, op_get_c(op), op_get_d(op), SIZE_LONG, NULL);
      return string_printf("lea.l      A%d, %s", op_get_a(op), addr.c_str());

    } else if (b == 5) {
      string addr = disassemble_address(r, opcode_start_address, op_get_c(op), op_get_d(op), SIZE_WORD, NULL);
      return string_printf("chk.w      D%d, %s", op_get_a(op), addr.c_str());

    } else {
      string addr = disassemble_address(r, opcode_start_address, op_get_c(op), op_get_d(op), SIZE_LONG, NULL);
      return string_printf(".invalid   %d, %s // invalid opcode 4 with b == %d",
          op_get_a(op), addr.c_str(), b);
    }
  }

  return ".invalid   // invalid opcode 4";
}

string disassemble_opcode_5(StringReader& r, uint32_t start_address, unordered_set<uint32_t>& branch_target_addresses) {
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
      branch_target_addresses.emplace(target_address);
      if (displacement < 0) {
        return string_printf("db%s       D%d, -0x%" PRIX16 " /* label%08" PRIX32 " */",
            cond, Xn, -displacement + 2, target_address);
      } else {
        return string_printf("db%s       D%d, +0x%" PRIX16 " /* label%08" PRIX32 " */",
            cond, Xn, displacement + 2, target_address);
      }
    }
    string addr = disassemble_address(r, opcode_start_address, M, Xn, SIZE_BYTE, &branch_target_addresses);
    return string_printf("s%s        %s", cond, addr.c_str(), &branch_target_addresses);
  }

  uint8_t size = op_get_s(op);
  string addr = disassemble_address(r, opcode_start_address, M, Xn, size, NULL);
  uint8_t value = op_get_a(op);
  if (value == 0) {
    value = 8;
  }
  return string_printf("%s.%c     %s, %d", op_get_g(op) ? "subq" : "addq",
      char_for_size.at(size), addr.c_str(), value);
}

string disassemble_opcode_6(StringReader& r, uint32_t start_address, unordered_set<uint32_t>& branch_target_addresses) {
  // TODO in what situation is the optional word displacement used?
  uint16_t op = r.get_u16r();
  uint32_t pc_base = start_address + r.where();

  int64_t displacement = static_cast<int8_t>(op_get_y(op));
  if (displacement == 0) {
    displacement = r.get_s16r();
  } else if (displacement == -1) {
    displacement = r.get_s32r();
  }

  // according to the programmer's manual, the displacement is relative to
  // (pc + 2) regardless of whether there's an extended displacement
  string displacement_str;
  uint32_t target_address = pc_base + displacement;
  branch_target_addresses.emplace(target_address);
  if (displacement < 0) {
    displacement_str = string_printf("-0x%" PRIX64 " /* label%08" PRIX32 " */",
        -displacement - 2, target_address);
  } else {
    displacement_str = string_printf("+0x%" PRIX64 " /* label%08" PRIX32 " */",
        displacement + 2, target_address);
  }

  uint8_t k = op_get_k(op);
  if (k == 0) {
    return "bra        " + displacement_str;
  }
  if (k == 1) {
    return "bsr        " + displacement_str;
  }
  return string_printf("b%s        %s", string_for_condition.at(k), displacement_str.c_str());
}

string disassemble_opcode_7(StringReader& r, uint32_t start_address, unordered_set<uint32_t>& branch_target_addresses) {
  uint16_t op = r.get_u16r();
  int32_t value = static_cast<int32_t>(static_cast<int8_t>(op_get_y(op)));
  return string_printf("moveq.l    D%d, 0x%02X", op_get_a(op), value);
}

string disassemble_opcode_8(StringReader& r, uint32_t start_address, unordered_set<uint32_t>& branch_target_addresses) {
  uint16_t op = r.get_u16r();
  uint8_t a = op_get_a(op);
  uint8_t opmode = op_get_b(op);
  uint8_t M = op_get_c(op);
  uint8_t Xn = op_get_d(op);

  if ((opmode & 3) == 3) {
    char s = (opmode & 4) ? 's' : 'u';
    string ea_dasm = disassemble_address(r, start_address, M, Xn, SIZE_WORD, NULL);
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

  string ea_dasm = disassemble_address(r, start_address, M, Xn, opmode & 3, NULL);
  if (opmode & 4) {
    return string_printf("or.%c       %s, D%hhu", char_for_size.at(opmode & 3),
        ea_dasm.c_str(), a);
  } else {
    return string_printf("or.%c       D%hhu, %s", char_for_size.at(opmode & 3),
        a, ea_dasm.c_str());
  }
}

string disassemble_opcode_B(StringReader& r, uint32_t start_address, unordered_set<uint32_t>& branch_target_addresses) {
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
    string ea_dasm = disassemble_address(r, opcode_start_address, M, Xn, opmode, NULL);
    return string_printf("cmp.%c      D%hhu, %s", char_for_size.at(opmode),
        dest, ea_dasm.c_str());
  }

  if ((opmode & 3) == 3) {
    if (opmode & 4) {
      string ea_dasm = disassemble_address(r, opcode_start_address, M, Xn, SIZE_LONG, NULL);
      return string_printf("cmpa.l     A%hhu, %s", dest, ea_dasm.c_str());
    } else {
      string ea_dasm = disassemble_address(r, opcode_start_address, M, Xn, SIZE_WORD, NULL);
      return string_printf("cmpa.w     A%hhu, %s", dest, ea_dasm.c_str());
    }
  }

  string ea_dasm = disassemble_address(r, opcode_start_address, M, Xn, opmode & 3, NULL);
  return string_printf("xor.%c      %s, D%hhu", char_for_size.at(opmode & 3),
      ea_dasm.c_str(), dest);
}

string disassemble_opcode_9D(StringReader& r, uint32_t start_address, unordered_set<uint32_t>& branch_target_addresses) {
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
      string ea_dasm = disassemble_address(r, opcode_start_address, M, Xn, SIZE_LONG, NULL);
      return string_printf("%s.l      A%hhu, %s", op_name, dest, ea_dasm.c_str());
    } else {
      string ea_dasm = disassemble_address(r, opcode_start_address, M, Xn, SIZE_WORD, NULL);
      return string_printf("%s.w      A%hhu, %s", op_name, dest, ea_dasm.c_str());
    }
  }

  string ea_dasm = disassemble_address(r, opcode_start_address, M, Xn, opmode & 3, NULL);
  char ch = char_for_size.at(opmode & 3);
  if (opmode & 4) {
    return string_printf("%s.%c      %s, D%hhu", op_name, ch, ea_dasm.c_str(), dest);
  } else {
    return string_printf("%s.%c      D%hhu, %s", op_name, ch, dest, ea_dasm.c_str());
  }
}

string disassemble_opcode_A(StringReader& r, uint32_t start_address, unordered_set<uint32_t>& branch_target_addresses) {
  static const unordered_map<uint16_t, const char*> trap_names({
    // os traps
    {0x00, "_Open"},
    {0x01, "_Close"},
    {0x02, "_Read"},
    {0x03, "_Write"},
    {0x04, "_Control"},
    {0x05, "_Status"},
    {0x06, "_KillIO"},
    {0x07, "_GetVolInfo"},
    {0x08, "_Create"},
    {0x09, "_Delete"},
    {0x0A, "_OpenRF"},
    {0x0B, "_Rename"},
    {0x0C, "_GetFileInfo"},
    {0x0D, "_SetFileInfo"},
    {0x0E, "_UnmountVol"},
    {0x0F, "_MountVol"},
    {0x10, "_Allocate"},
    {0x11, "_GetEOF"},
    {0x12, "_SetEOF"},
    {0x13, "_FlushVol"},
    {0x14, "_GetVol"},
    {0x15, "_SetVol"},
    {0x16, "_InitQueue"},
    {0x17, "_Eject"},
    {0x18, "_GetFPos"},
    {0x19, "_InitZone"},
    {0x1A, "_GetZone"},
    {0x1B, "_SetZone"},
    {0x1C, "_FreeMem"},
    {0x1D, "_MaxMem"},
    {0x1E, "_NewPtr"},
    {0x1F, "_DisposPtr"},
    {0x20, "_SetPtrSize"},
    {0x21, "_GetPtrSize"},
    {0x22, "_NewHandle"},
    {0x23, "_DisposHandle"},
    {0x24, "_SetHandleSize"},
    {0x25, "_GetHandleSize"},
    {0x26, "_HandleZone"},
    {0x27, "_ReallocHandle"},
    {0x28, "_RecoverHandle"},
    {0x29, "_HLock"},
    {0x2A, "_HUnlock"},
    {0x2B, "_EmptyHandle"},
    {0x2C, "_InitApplZone"},
    {0x2D, "_SetApplLimit"},
    {0x2E, "_BlockMove"},
    {0x2F, "_PostEvent"},
    {0x2F, "_PPostEvent"},
    {0x30, "_OSEventAvail"},
    {0x31, "_GetOSEvent"},
    {0x32, "_FlushEvents"},
    {0x33, "_VInstall"},
    {0x34, "_VRemove"},
    {0x35, "_Offline"},
    {0x36, "_MoreMasters"},
    {0x38, "_WriteParam"},
    {0x39, "_ReadDateTime"},
    {0x3A, "_SetDateTime"},
    {0x3B, "_Delay"},
    {0x3C, "_CmpString"},
    {0x3D, "_DrvrInstall"},
    {0x3E, "_DrvrRemove"},
    {0x3F, "_InitUtil"},
    {0x40, "_ResrvMem"},
    {0x41, "_SetFilLock"},
    {0x42, "_RstFilLock"},
    {0x43, "_SetFilType"},
    {0x44, "_SetFPos"},
    {0x45, "_FlushFile"},
    {0x46, "_GetTrapAddress"},
    {0x47, "_SetTrapAddress"},
    {0x48, "_PtrZone"},
    {0x49, "_HPurge"},
    {0x4A, "_HNoPurge"},
    {0x4B, "_SetGrowZone"},
    {0x4C, "_CompactMem"},
    {0x4D, "_PurgeMem"},
    {0x4E, "_AddDrive"},
    {0x4F, "_RDrvrInstall"},
    {0x50, "_RelString"},
    {0x54, "_UprString"},
    {0x55, "_StripAddress"},
    {0x57, "_SetAppBase"},
    {0x5D, "_SwapMMUMode"},
    {0x60, "_HFSDispatch"},
    {0x61, "_MaxBlock"},
    {0x62, "_PurgeSpace"},
    {0x63, "_MaxApplZone"},
    {0x64, "_MoveHHi"},
    {0x65, "_StackSpace"},
    {0x66, "_NewEmptyHandle"},
    {0x67, "_HSetRBit"},
    {0x68, "_HClrRBit"},
    {0x69, "_HGetState"},
    {0x6A, "_HSetState"},
    {0x6E, "_SlotManager"},
    {0x6F, "_SlotVInstall"},
    {0x70, "_SlotVRemove"},
    {0x71, "_AttachVBL"},
    {0x72, "_DoVBLTask"},
    {0x75, "_SIntInstall"},
    {0x76, "_SIntRemove"},
    {0x77, "_CountADBs"},
    {0x78, "_GetIndADB"},
    {0x79, "_GetADBInfo"},
    {0x7A, "_SetADBInfo"},
    {0x7B, "_ADBReInit"},
    {0x7C, "_ADBOp"},
    {0x7D, "_GetDefaultStartup"},
    {0x7E, "_SetDefaultStartup"},
    {0x7F, "_InternalWait"},
    {0x80, "_GetVideoDefault"},
    {0x81, "_SetVideoDefault"},
    {0x82, "_DTInstall"},
    {0x83, "_SetOSDefault"},
    {0x84, "_GetOSDefault"},
    {0x90, "_SysEnvirons"},

    // toolbox traps
    {0x808, "_InitProcMenu"},
    {0x809, "_GetCVariant"},
    {0x80A, "_GetWVariant"},
    {0x80B, "_PopUpMenuSelect"},
    {0x80C, "_RGetResource"},
    {0x80D, "_Count1Resources"},
    {0x80E, "_Get1IxResource"},
    {0x80F, "_Get1IxType"},
    {0x810, "_Unique1ID"},
    {0x811, "_TESelView"},
    {0x812, "_TEPinScroll"},
    {0x813, "_TEAutoView"},
    {0x815, "_SCSIDispatch"},
    {0x816, "_Pack8"},
    {0x817, "_CopyMask"},
    {0x818, "_FixAtan2"},
    {0x81C, "_Count1Types"},
    {0x81F, "_Get1Resource"},
    {0x820, "_Get1NamedResource"},
    {0x821, "_MaxSizeRsrc"},
    {0x826, "_InsMenuItem"},
    {0x827, "_HideDItem"},
    {0x828, "_ShowDItem"},
    {0x82B, "_Pack9"},
    {0x82C, "_Pack10"},
    {0x82D, "_Pack11"},
    {0x82E, "_Pack12"},
    {0x82F, "_Pack13"},
    {0x830, "_Pack14"},
    {0x831, "_Pack15"},
    {0x834, "_SetFScaleDisable"},
    {0x835, "_FontMetrics"},
    {0x837, "_MeasureText"},
    {0x838, "_CalcMask"},
    {0x839, "_SeedFill"},
    {0x83A, "_ZoomWindow"},
    {0x83B, "_TrackBox"},
    {0x83C, "_TEGetOffset"},
    {0x83D, "_TEDispatch"},
    {0x83E, "_TEStyleNew"},
    {0x83F, "_Long2Fix"},
    {0x840, "_Fix2Long"},
    {0x841, "_Fix2Frac"},
    {0x842, "_Frac2Fix"},
    {0x843, "_Fix2X"},
    {0x844, "_X2Fix"},
    {0x845, "_Frac2X"},
    {0x846, "_X2Frac"},
    {0x847, "_FracCos"},
    {0x848, "_FracSin"},
    {0x849, "_FracSqrt"},
    {0x84A, "_FracMul"},
    {0x84B, "_FracDiv"},
    {0x84D, "_FixDiv"},
    {0x84E, "_GetItemCmd"},
    {0x84F, "_SetItemCmd"},
    {0x850, "_InitCursor"},
    {0x851, "_SetCursor"},
    {0x852, "_HideCursor"},
    {0x853, "_ShowCursor"},
    {0x855, "_ShieldCursor"},
    {0x856, "_ObscureCursor"},
    {0x858, "_BitAnd"},
    {0x859, "_BitXor"},
    {0x85A, "_BitNot"},
    {0x85B, "_BitOr"},
    {0x85C, "_BitShift"},
    {0x85D, "_BitTst"},
    {0x85E, "_BitSet"},
    {0x85F, "_BitClr"},
    {0x861, "_Random"},
    {0x862, "_ForeColor"},
    {0x863, "_BackColor"},
    {0x864, "_ColorBit"},
    {0x865, "_GetPixel"},
    {0x866, "_StuffHex"},
    {0x867, "_LongMul"},
    {0x868, "_FixMul"},
    {0x869, "_FixRatio"},
    {0x86A, "_HiWord"},
    {0x86B, "_LoWord"},
    {0x86C, "_FixRound"},
    {0x86D, "_InitPort"},
    {0x86E, "_InitGraf"},
    {0x86F, "_OpenPort"},
    {0x870, "_LocalToGlobal"},
    {0x871, "_GlobalToLocal"},
    {0x872, "_GrafDevice"},
    {0x873, "_SetPort"},
    {0x874, "_GetPort"},
    {0x875, "_SetPBits"},
    {0x876, "_PortSize"},
    {0x877, "_MovePortTo"},
    {0x878, "_SetOrigin"},
    {0x879, "_SetClip"},
    {0x87A, "_GetClip"},
    {0x87B, "_ClipRect"},
    {0x87C, "_BackPat"},
    {0x87D, "_CloseCPort"},
    {0x87D, "_ClosePort"},
    {0x87E, "_AddPt"},
    {0x87F, "_SubPt"},
    {0x880, "_SetPt"},
    {0x881, "_EqualPt"},
    {0x882, "_StdText"},
    {0x883, "_DrawChar"},
    {0x884, "_DrawString"},
    {0x885, "_DrawText"},
    {0x886, "_TextWidth"},
    {0x887, "_TextFont"},
    {0x888, "_TextFace"},
    {0x889, "_TextMode"},
    {0x88A, "_TextSize"},
    {0x88B, "_GetFontInfo"},
    {0x88C, "_StringWidth"},
    {0x88D, "_CharWidth"},
    {0x88E, "_SpaceExtra"},
    {0x890, "_StdLine"},
    {0x891, "_LineTo"},
    {0x892, "_Line"},
    {0x893, "_MoveTo"},
    {0x894, "_Move"},
    {0x895, "_Shutdown"},
    {0x896, "_HidePen"},
    {0x897, "_ShowPen"},
    {0x898, "_GetPenState"},
    {0x899, "_SetPenState"},
    {0x89A, "_GetPen"},
    {0x89B, "_PenSize"},
    {0x89C, "_PenMode"},
    {0x89D, "_PenPat"},
    {0x89E, "_PenNormal"},
    {0x8A0, "_StdRect"},
    {0x8A1, "_FrameRect"},
    {0x8A2, "_PaintRect"},
    {0x8A3, "_EraseRect"},
    {0x8A4, "_InverRect"},
    {0x8A5, "_FillRect"},
    {0x8A6, "_EqualRect"},
    {0x8A7, "_SetRect"},
    {0x8A8, "_OffsetRect"},
    {0x8A9, "_InsetRect"},
    {0x8AA, "_SectRect"},
    {0x8AB, "_UnionRect"},
    {0x8AC, "_Pt2Rect"},
    {0x8AD, "_PtInRect"},
    {0x8AE, "_EmptyRect"},
    {0x8AF, "_StdRRect"},
    {0x8B1, "_PaintRoundRect"},
    {0x8B2, "_EraseRoundRect"},
    {0x8B3, "_InverRoundRect"},
    {0x8B4, "_FillRoundRect"},
    {0x8B5, "_ScriptUtil"},
    {0x8B6, "_StdOval"},
    {0x8B7, "_FrameOval"},
    {0x8B8, "_PaintOval"},
    {0x8B9, "_EraseOval"},
    {0x8BA, "_InvertOval"},
    {0x8BB, "_FillOval"},
    {0x8BC, "_SlopeFromAngle"},
    {0x8BD, "_StdArc"},
    {0x8BE, "_FrameArc"},
    {0x8BF, "_PaintArc"},
    {0x8C0, "_EraseArc"},
    {0x8C1, "_InvertArc"},
    {0x8C2, "_FillArc"},
    {0x8C3, "_PtToAngle"},
    {0x8C4, "_AngleFromSlope"},
    {0x8C5, "_StdPoly"},
    {0x8C6, "_FramePoly"},
    {0x8C7, "_PaintPoly"},
    {0x8C8, "_ErasePoly"},
    {0x8C9, "_InvertPoly"},
    {0x8CA, "_FillPoly"},
    {0x8CB, "_OpenPoly"},
    {0x8CC, "_ClosePgon"},
    {0x8CD, "_KillPoly"},
    {0x8CE, "_OffsetPoly"},
    {0x8CF, "_PackBits"},
    {0x8D0, "_UnpackBits"},
    {0x8D1, "_StdRgn"},
    {0x8D2, "_FrameRgn"},
    {0x8D3, "_PaintRgn"},
    {0x8D4, "_EraseRgn"},
    {0x8D5, "_InverRgn"},
    {0x8D6, "_FillRgn"},
    {0x8D8, "_NewRgn"},
    {0x8D9, "_DisposRgn"},
    {0x8DA, "_OpenRgn"},
    {0x8DB, "_CloseRgn"},
    {0x8DC, "_CopyRgn"},
    {0x8DD, "_SetEmptyRgn"},
    {0x8DE, "_SetRecRgn"},
    {0x8DF, "_RectRgn"},
    {0x8E0, "_OfsetRgn"},
    {0x8E1, "_InsetRgn"},
    {0x8E2, "_EmptyRgn"},
    {0x8E3, "_EqualRgn"},
    {0x8E4, "_SectRgn"},
    {0x8E5, "_UnionRgn"},
    {0x8E6, "_DiffRgn"},
    {0x8E7, "_XorRgn"},
    {0x8E8, "_PtInRgn"},
    {0x8E9, "_RectInRgn"},
    {0x8EA, "_SetStdProcs"},
    {0x8EB, "_StdBits"},
    {0x8EC, "_CopyBits"},
    {0x8ED, "_StdTxMeas"},
    {0x8EE, "_StdGetPic"},
    {0x8EF, "_ScrollRect"},
    {0x8F0, "_StdPutPic"},
    {0x8F1, "_StdComment"},
    {0x8F2, "_PicComment"},
    {0x8F3, "_OpenPicture"},
    {0x8F4, "_ClosePicture"},
    {0x8F5, "_KillPicture"},
    {0x8F6, "_DrawPicture"},
    {0x8F8, "_ScalePt"},
    {0x8F9, "_MapPt"},
    {0x8FA, "_MapRect"},
    {0x8FB, "_MapRgn"},
    {0x8FC, "_MapPoly"},
    {0x8FE, "_InitFonts"},
    {0x8FF, "_GetFName"},
    {0x900, "_GetFNum"},
    {0x901, "_FMSwapFont"},
    {0x902, "_RealFont"},
    {0x903, "_SetFontLock"},
    {0x904, "_DrawGrowIcon"},
    {0x905, "_DragGrayRgn"},
    {0x906, "_NewString"},
    {0x907, "_SetString"},
    {0x908, "_ShowHide"},
    {0x909, "_CalcVis"},
    {0x90A, "_CalcVBehind"},
    {0x90B, "_ClipAbove"},
    {0x90C, "_PaintOne"},
    {0x90D, "_PaintBehind"},
    {0x90E, "_SaveOld"},
    {0x90F, "_DrawNew"},
    {0x910, "_GetWMgrPort"},
    {0x911, "_CheckUpdate"},
    {0x912, "_InitWindows"},
    {0x913, "_NewWindow"},
    {0x914, "_DisposWindow"},
    {0x915, "_ShowWindow"},
    {0x916, "_HideWindow"},
    {0x917, "_GetWRefCon"},
    {0x918, "_SetWRefCon"},
    {0x919, "_GetWTitle"},
    {0x91A, "_SetWTitle"},
    {0x91B, "_MoveWindow"},
    {0x91C, "_HiliteWindow"},
    {0x91D, "_SizeWindow"},
    {0x91E, "_TrackGoAway"},
    {0x91F, "_SelectWindow"},
    {0x920, "_BringToFront"},
    {0x921, "_SendBehind"},
    {0x922, "_BeginUpdate"},
    {0x923, "_EndUpdate"},
    {0x924, "_FrontWindow"},
    {0x925, "_DragWindow"},
    {0x926, "_DragTheRgn"},
    {0x927, "_InvalRgn"},
    {0x928, "_InvalRect"},
    {0x929, "_ValidRgn"},
    {0x92A, "_ValidRect"},
    {0x92B, "_GrowWindow"},
    {0x92C, "_FindWindow"},
    {0x92D, "_CloseWindow"},
    {0x92E, "_SetWindowPic"},
    {0x92F, "_GetWindowPic"},
    {0x930, "_InitMenus"},
    {0x931, "_NewMenu"},
    {0x932, "_DisposMenu"},
    {0x933, "_AppendMenu"},
    {0x934, "_ClearMenuBar"},
    {0x935, "_InsertMenu"},
    {0x936, "_DeleteMenu"},
    {0x937, "_DrawMenuBar"},
    {0x938, "_HiliteMenu"},
    {0x939, "_EnableItem"},
    {0x93A, "_DisableItem"},
    {0x93B, "_GetMenuBar"},
    {0x93C, "_SetMenuBar"},
    {0x93D, "_MenuSelect"},
    {0x93E, "_MenuKey"},
    {0x93F, "_GetItmIcon"},
    {0x940, "_SetItmIcon"},
    {0x941, "_GetItmStyle"},
    {0x942, "_SetItmStyle"},
    {0x943, "_GetItmMark"},
    {0x944, "_SetItmMark"},
    {0x945, "_CheckItem"},
    {0x946, "_GetItem"},
    {0x947, "_SetItem"},
    {0x948, "_CalcMenuSize"},
    {0x949, "_GetMHandle"},
    {0x94A, "_SetMFlash"},
    {0x94B, "_PlotIcon"},
    {0x94C, "_FlashMenuBar"},
    {0x94D, "_AddResMenu"},
    {0x94E, "_PinRect"},
    {0x94F, "_DeltaPoint"},
    {0x950, "_CountMItems"},
    {0x951, "_InsertResMenu"},
    {0x952, "_DelMenuItem"},
    {0x953, "_UpdtControl"},
    {0x954, "_NewControl"},
    {0x955, "_DisposControl"},
    {0x956, "_KillControls"},
    {0x957, "_ShowControl"},
    {0x958, "_HideControl"},
    {0x959, "_MoveControl"},
    {0x95A, "_GetCRefCon"},
    {0x95B, "_SetCRefCon"},
    {0x95C, "_SizeControl"},
    {0x95D, "_HiliteControl"},
    {0x95E, "_GetCTitle"},
    {0x95F, "_SetCTitle"},
    {0x960, "_GetCtlValue"},
    {0x961, "_GetMinCtl"},
    {0x962, "_GetMaxCtl"},
    {0x963, "_SetCtlValue"},
    {0x964, "_SetMinCtl"},
    {0x965, "_SetMaxCtl"},
    {0x966, "_TestControl"},
    {0x967, "_DragControl"},
    {0x968, "_TrackControl"},
    {0x969, "_DrawControls"},
    {0x96A, "_GetCtlAction"},
    {0x96B, "_SetCtlAction"},
    {0x96C, "_FindControl"},
    {0x96D, "_Draw1Control"},
    {0x96E, "_Dequeue"},
    {0x96F, "_Enqueue"},
    {0x970, "_GetNextEvent"},
    {0x971, "_EventAvail"},
    {0x972, "_GetMouse"},
    {0x973, "_StillDown"},
    {0x974, "_Button"},
    {0x975, "_TickCount"},
    {0x976, "_GetKeys"},
    {0x977, "_WaitMouseUp"},
    {0x978, "_UpdtDialog"},
    {0x979, "_CouldDialog"},
    {0x97A, "_FreeDialog"},
    {0x97B, "_InitDialogs"},
    {0x97C, "_GetNewDialog"},
    {0x97D, "_NewDialog"},
    {0x97E, "_SelIText"},
    {0x97F, "_IsDialogEvent"},
    {0x980, "_DialogSelect"},
    {0x981, "_DrawDialog"},
    {0x982, "_CloseDialog"},
    {0x983, "_DisposDialog"},
    {0x984, "_FindDItem"},
    {0x985, "_Alert"},
    {0x986, "_StopAlert"},
    {0x987, "_NoteAlert"},
    {0x988, "_CautionAlert"},
    {0x989, "_CouldAlert"},
    {0x98A, "_FreeAlert"},
    {0x98B, "_ParamText"},
    {0x98C, "_ErrorSound"},
    {0x98D, "_GetDItem"},
    {0x98E, "_SetDItem"},
    {0x98F, "_SetIText"},
    {0x990, "_GetIText"},
    {0x991, "_ModalDialog"},
    {0x992, "_DetachResource"},
    {0x993, "_SetResPurge"},
    {0x994, "_CurResFile"},
    {0x995, "_InitResources"},
    {0x996, "_RsrcZoneInit"},
    {0x997, "_OpenResFile"},
    {0x998, "_UseResFile"},
    {0x999, "_UpdateResFile"},
    {0x99A, "_CloseResFile"},
    {0x99B, "_SetResLoad"},
    {0x99C, "_CountResources"},
    {0x99D, "_GetIndResource"},
    {0x99E, "_CountTypes"},
    {0x99F, "_GetIndType"},
    {0x9A0, "_GetResource"},
    {0x9A1, "_GetNamedResource"},
    {0x9A2, "_LoadResource"},
    {0x9A3, "_ReleaseResource"},
    {0x9A4, "_HomeResFile"},
    {0x9A5, "_SizeRsrc"},
    {0x9A6, "_GetResAttrs"},
    {0x9A7, "_SetResAttrs"},
    {0x9A8, "_GetResInfo"},
    {0x9A9, "_SetResInfo"},
    {0x9AA, "_ChangedResource"},
    {0x9AB, "_AddResource"},
    {0x9AC, "_AddReference"},
    {0x9AD, "_RmveResource"},
    {0x9AE, "_RmveReference"},
    {0x9AF, "_ResError"},
    {0x9B0, "_FrameRoundRect"},
    {0x9B0, "_WriteResource"},
    {0x9B1, "_CreateResFile"},
    {0x9B2, "_SystemEvent"},
    {0x9B3, "_SystemClick"},
    {0x9B4, "_SystemTask"},
    {0x9B5, "_SystemMenu"},
    {0x9B6, "_OpenDeskAcc"},
    {0x9B7, "_CloseDeskAcc"},
    {0x9B8, "_GetPattern"},
    {0x9B9, "_GetCursor"},
    {0x9BA, "_GetString"},
    {0x9BB, "GetIcon"},
    {0x9BC, "_GetPicture"},
    {0x9BD, "_GetNewWindow"},
    {0x9BE, "_GetNewControl"},
    {0x9BF, "_GetRMenu"},
    {0x9C0, "_GetNewMBar"},
    {0x9C1, "_UniqueID"},
    {0x9C2, "_SysEdit"},
    {0x9C3, "_KeyTrans"},
    {0x9C4, "_OpenRFPerm"},
    {0x9C5, "_RsrcMapEntry"},
    {0x9C6, "_Secs2Date"},
    {0x9C7, "_Date2Sec"},
    {0x9C8, "_SysBeep"},
    {0x9C9, "_SysError"},
    {0x9CB, "_TEGetText"},
    {0x9CC, "_TEInit"},
    {0x9CD, "_TEDispose"},
    {0x9CE, "_TextBox"},
    {0x9CF, "_TESetText"},
    {0x9D0, "_TECalText"},
    {0x9D1, "_TESetSelect"},
    {0x9D2, "_TENew"},
    {0x9D3, "_TEUpdate"},
    {0x9D4, "_TEClick"},
    {0x9D5, "_TECopy"},
    {0x9D6, "_TECut"},
    {0x9D7, "_TEDelete"},
    {0x9D8, "_TEActivate"},
    {0x9D9, "_TEDeactivate"},
    {0x9DA, "_TEIdle"},
    {0x9DB, "_TEPaste"},
    {0x9DC, "_TEKey"},
    {0x9DD, "_TEScroll"},
    {0x9DE, "_TEInsert"},
    {0x9DF, "_TESetJust"},
    {0x9E0, "_Munger"},
    {0x9E1, "_HandToHand"},
    {0x9E2, "_PtrToXHand"},
    {0x9E3, "_PtrToHand"},
    {0x9E4, "_HandAndHand"},
    {0x9E5, "_InitPack"},
    {0x9E6, "_InitAllPacks"},
    {0x9E7, "_Pack0"},
    {0x9E8, "_Pack1"},
    {0x9E9, "_Pack2"},
    {0x9EA, "_Pack3"},
    {0x9EB, "_FP68K"},
    {0x9EB, "_Pack4"},
    {0x9EC, "_Elems68K"},
    {0x9EC, "_Pack5"},
    {0x9ED, "_Pack6"},
    {0x9EE, "_Pack7"},
    {0x9EF, "_PtrAndHand"},
    {0x9F0, "_LoadSeg"},
    {0x9F1, "_UnloadSeg"},
    {0x9F2, "_Launch"},
    {0x9F3, "_Chain"},
    {0x9F4, "_ExitToShell"},
    {0x9F5, "_GetAppParms"},
    {0x9F6, "_GetResFileAttrs"},
    {0x9F7, "_SetResFileAttrs"},
    {0x9F9, "_InfoScrap"},
    {0x9FA, "_UnlodeScrap"},
    {0x9FB, "_LodeScrap"},
    {0x9FC, "_ZeroScrap"},
    {0x9FD, "_GetScrap"},
    {0x9FE, "_PutScrap"},
    {0xA00, "_OpenCport"},
    {0xA01, "_InitCport"},
    {0xA03, "_NewPixMap"},
    {0xA04, "_DisposPixMap"},
    {0xA05, "_CopyPixMap"},
    {0xA06, "_SetCPortPix"},
    {0xA07, "_NewPixPat"},
    {0xA08, "_DisposPixPat"},
    {0xA09, "_CopyPixPat"},
    {0xA0A, "_PenPixPat"},
    {0xA0B, "_BackPixPat"},
    {0xA0C, "_GetPixPat"},
    {0xA0D, "_MakeRGBPat"},
    {0xA0E, "_FillCRect"},
    {0xA0F, "_FillCOval"},
    {0xA10, "_FillCRoundRect"},
    {0xA11, "_FillCArc"},
    {0xA12, "_FillCRgn"},
    {0xA13, "_FillCPoly"},
    {0xA14, "_RGBForeColor"},
    {0xA15, "_RGBBackColor"},
    {0xA16, "_SetCPixel"},
    {0xA17, "_GetCPixel"},
    {0xA18, "_GetCTable"},
    {0xA19, "_GetForeColor"},
    {0xA1A, "_GetBackColor"},
    {0xA1B, "_GetCCursor"},
    {0xA1C, "_SetCCursor"},
    {0xA1D, "_AllocCursor"},
    {0xA1E, "_GetCIcon"},
    {0xA1F, "_PlotCIcon"},
    {0xA21, "_OpColor"},
    {0xA22, "_HiliteColor"},
    {0xA23, "_CharExtra"},
    {0xA24, "_DisposCTable"},
    {0xA25, "_DisposCIcon"},
    {0xA26, "_DisposCCursor"},
    {0xA27, "_GetMaxDevice"},
    {0xA29, "_GetDeviceList"},
    {0xA2A, "_GetMainDevice"},
    {0xA2B, "_GetNextDevice"},
    {0xA2C, "_TestDeviceAttribute"},
    {0xA2D, "_SetDeviceAttribute"},
    {0xA2E, "_InitGDevice"},
    {0xA2F, "_NewGDevice"},
    {0xA30, "_DisposGDevice"},
    {0xA31, "_SetGDevice"},
    {0xA32, "_GetGDevice"},
    {0xA33, "_Color2Index"},
    {0xA34, "_Index2Color"},
    {0xA35, "_InvertColor"},
    {0xA36, "_RealColor"},
    {0xA37, "_GetSubTable"},
    {0xA39, "_MakeITable"},
    {0xA3A, "_AddSearch"},
    {0xA3B, "_AddComp"},
    {0xA3C, "_SetClientID"},
    {0xA3D, "_ProtectEntry"},
    {0xA3E, "_ReserveEntry"},
    {0xA3F, "_SetEntries"},
    {0xA40, "_QDError"},
    {0xA41, "_SetWinColor"},
    {0xA42, "_GetAuxWin"},
    {0xA43, "_SetCtlColor"},
    {0xA44, "_GetAuxCtl"},
    {0xA45, "_NewCWindow"},
    {0xA46, "_GetNewCWindow"},
    {0xA47, "_SetDeskCPat"},
    {0xA48, "_GetCWMgrPort"},
    {0xA49, "_SaveEntries"},
    {0xA4A, "_RestoreEntries"},
    {0xA4B, "_NewCDialog"},
    {0xA4C, "_DelSearch"},
    {0xA4D, "_DelComp"},
    {0xA4F, "_CalcCMask"},
    {0xA50, "_SeedCFill"},
    {0xA60, "_DelMCEntries"},
    {0xA61, "_GetMCInfo"},
    {0xA62, "_SetMCInfo"},
    {0xA63, "_DispMCEntries"},
    {0xA64, "_GetMCEntry"},
    {0xA65, "_SetMCEntries"},
    {0xA66, "_MenuChoice"},
  });

  uint16_t op = r.get_u16r();

  uint16_t trap_number;
  bool auto_pop = false;
  uint8_t flags = 0;
  if (op & 0x0800) {
    trap_number = op & 0x0BFF;
    auto_pop = op & 0x0400;
  } else {
    trap_number = op & 0xFF;
    flags = (op >> 9) & 3;
  }

  string ret = "trap       ";
  try {
    ret += trap_names.at(trap_number);
  } catch (const out_of_range&) {
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

string disassemble_opcode_C(StringReader& r, uint32_t start_address, unordered_set<uint32_t>& branch_target_addresses) {
  uint16_t op = r.get_u16r();
  uint8_t a = op_get_a(op);
  uint8_t b = op_get_b(op);
  uint8_t c = op_get_c(op);
  uint8_t d = op_get_d(op);

  if (b < 3) { // and.S DREG, ADDR
    string ea_dasm = disassemble_address(r, start_address, c, d, b, NULL);
    return string_printf("and.%c      D%hhu, %s", char_for_size.at(b), a,
        ea_dasm.c_str());

  } else if (b == 3) { // mulu.w DREG, ADDR (word * word = long form)
    string ea_dasm = disassemble_address(r, start_address, c, d, b, NULL);
    return string_printf("mulu.w     D%hhu, %s", a, ea_dasm.c_str());

  } else if (b == 4) {
    if (c == 0) { // abcd DREG, DREG
      return string_printf("abcd       D%hhu, D%hhu", a, d);
    } else if (c == 1) { // abcd -[AREG], -[AREG]
      return string_printf("abcd       -[A%hhu], -[A%hhu]", a, d);
    } else { // and.S ADDR, DREG
      string ea_dasm = disassemble_address(r, start_address, c, d, b, NULL);
      return string_printf("and.%c      %s, D%hhu", char_for_size.at(b),
          ea_dasm.c_str(), a);
    }

  } else if (b == 5) {
    if (c == 0) { // exg DREG, DREG
      return string_printf("exg        D%hhu, D%hhu", a, d);
    } else if (c == 1) { // exg AREG, AREG
      return string_printf("exg        A%hhu, A%hhu", a, d);
    } else { // and.S ADDR, DREG
      string ea_dasm = disassemble_address(r, start_address, c, d, b, NULL);
      return string_printf("and.%c      %s, D%hhu", char_for_size.at(b),
          ea_dasm.c_str(), a);
    }

  } else if (b == 6) {
    if (c == 1) { // exg AREG, DREG
      return string_printf("exg        A%hhu, D%hhu", a, d);
    } else { // and.S ADDR, DREG
      string ea_dasm = disassemble_address(r, start_address, c, d, b, NULL);
      return string_printf("and.%c      %s, D%hhu", char_for_size.at(b),
          ea_dasm.c_str(), a);
    }

  } else if (b == 7) { // muls DREG, ADDR (word * word = long form)
    string ea_dasm = disassemble_address(r, start_address, c, d, b, NULL);
    return string_printf("muls.w     D%hhu, %s", a, ea_dasm.c_str());
  }

  // this should be impossible; we covered all possible values for b and all
  // branches unconditionally return
  throw logic_error("no cases matched for 1100bbb opcode");
}

string disassemble_opcode_E(StringReader& r, uint32_t start_address, unordered_set<uint32_t>& branch_target_addresses) {
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
      uint16_t ext = r.get_u16();
      string ea_dasm = disassemble_address(r, start_address, M, Xn, SIZE_LONG, NULL);
      string offset_str = (ext & 0x0800) ?
          string_printf("D%hu", (ext & 0x01C0) >> 6) :
          string_printf("%hu", (ext & 0x07C0) >> 6);
      // if immediate, 0 in the width field means 32
      string width_str;
      if ((ext & 0x003F) == 0x0000) {
        width_str = "32";
      } else {
        width_str = (ext & 0x0020) ? string_printf("D%hu", (ext & 0x0007) >> 6)
            : string_printf("%hu", (ext & 0x001F) >> 6);
      }

      if (k & 1) {
        uint8_t Dn = (ext >> 12) & 7;
        return string_printf("%s     %s {%s:%s}, D%hhu", op_name,
            ea_dasm.c_str(), offset_str.c_str(), width_str.c_str(), Dn);
      } else {
        return string_printf("%s     %s {%s:%s}", op_name, ea_dasm.c_str(),
            offset_str.c_str(), width_str.c_str());
      }
    }
    string ea_dasm = disassemble_address(r, start_address, M, Xn, SIZE_WORD, NULL);
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

// unimplemented stuff:
// 1000 OR/DIV/SBCD
// 1010 (Unassigned, Reserved)
// 1011 CMP/EOR
// 1100 AND/MUL/ABCD/EXG
// 1110 Shift/Rotate/Bit Field
// 1111 Coprocessor Interface/MC68040 and CPU32 Extensions

string (*dasm_functions[16])(StringReader& r, uint32_t start_address, unordered_set<uint32_t>& branch_target_addresses) = {
  disassemble_opcode_0123,
  disassemble_opcode_0123,
  disassemble_opcode_0123,
  disassemble_opcode_0123,
  disassemble_opcode_4,
  disassemble_opcode_5,
  disassemble_opcode_6,
  disassemble_opcode_7,
  disassemble_opcode_8,
  disassemble_opcode_9D,
  disassemble_opcode_A,
  disassemble_opcode_B,
  disassemble_opcode_C,
  disassemble_opcode_9D,
  disassemble_opcode_E,
  disassemble_opcode_F,
};

////////////////////////////////////////////////////////////////////////////////

string MC68KEmulator::disassemble_one(StringReader& r, uint32_t start_address,
    unordered_set<uint32_t>& branch_target_addresses) {
  size_t opcode_offset = r.where();
  string opcode_disassembly;
  try {
    uint8_t op_high = r.get_u8(false);
    opcode_disassembly = (dasm_functions[(op_high >> 4) & 0x000F])(r,
        start_address, branch_target_addresses);
  } catch (const out_of_range&) {
    if (r.where() == opcode_offset) {
      // there must be at least 1 byte available since r.eof() was false
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
      // this should only happen for .incomplete at the end of the stream
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

string MC68KEmulator::disassemble_one(const void* vdata, size_t size,
    uint32_t start_address) {
  StringReader r(vdata, size);
  unordered_set<uint32_t> branch_target_addresses;
  return MC68KEmulator::disassemble_one(r, start_address, branch_target_addresses);
}


string MC68KEmulator::disassemble(const void* vdata, size_t size,
    uint32_t start_address, const unordered_multimap<uint32_t, string>* labels) {
  unordered_set<uint32_t> branch_target_addresses;
  deque<pair<uint32_t, string>> ret_lines; // (addr, line) pairs
  size_t ret_bytes = 0;

  StringReader r(vdata, size);
  while (!r.eof()) {
    uint32_t opcode_offset = r.where();
    string line = string_printf("%08" PRIX64 " ", start_address + opcode_offset);
    line += MC68KEmulator::disassemble_one(r, start_address, branch_target_addresses);
    line += '\n';

    ret_bytes += line.size();
    ret_lines.emplace_back(make_pair(opcode_offset + start_address, move(line)));
  }

  string ret;
  ret.reserve(ret_bytes);
  for (const auto& it : ret_lines) {
    uint32_t opcode_address = it.first;
    string line = it.second;
    if (labels) {
      auto label_its = labels->equal_range(opcode_address);
      for (; label_its.first != label_its.second; label_its.first++) {
        ret += string_printf("%s:\n", label_its.first->second.c_str());
      }
    }
    if (branch_target_addresses.count(opcode_address)) {
      ret += string_printf("label%08" PRIX32 ":\n", opcode_address);
    }
    ret += line;
  }
  return ret;
}
