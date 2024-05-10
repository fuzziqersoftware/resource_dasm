#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <deque>
#include <forward_list>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <set>
#include <string>

#include "SH4Emulator.hh"

using namespace std;

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

static constexpr int32_t op_get_simm4(uint16_t op) {
  int32_t ret = op & 0x000F;
  return (ret & 0x08) ? (ret | 0xFFFFFFF0) : ret;
}
static constexpr int32_t op_get_uimm8(uint16_t op) {
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
    if (starts_with(text, "fr")) {
      try {
        this->reg_num = stoul(text.substr(2));
        this->type = Type::FR_REGISTER;
        check_range_t<uint8_t>(this->reg_num, 0, 15);
        return;
      } catch (const invalid_argument&) {
      }
    } else if (starts_with(text, "dr")) {
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
    } else if (starts_with(text, "xd")) {
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
    } else if (starts_with(text, "fv")) {
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

  if (starts_with(text, "-[r") && ends_with(text, "]")) {
    try {
      this->reg_num = stoul(text.substr(3, text.size() - 4));
      this->type = Type::PREDEC_MEMORY_REFERENCE;
      check_range_t<uint8_t>(this->reg_num, 0, 15);
      return;
    } catch (const invalid_argument&) {
    }
  } else if (starts_with(text, "[r") && ends_with(text, "]+")) {
    try {
      this->reg_num = stoul(text.substr(2, text.size() - 4));
      this->type = Type::POSTINC_MEMORY_REFERENCE;
      check_range_t<uint8_t>(this->reg_num, 0, 15);
      return;
    } catch (const invalid_argument&) {
    }
  } else if (starts_with(text, "[") && ends_with(text, "]")) {
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
      if (starts_with(expr1, "r")) {
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
      if (starts_with(expr1, "0x")) {
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
        this->value = (is_subtract ? (-1) : 1) * stol(index_expr, nullptr, 0);
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
      this->reg_num = stol((expr1_is_reg ? expr1 : expr2).substr(1));
      return;
    }
  }

  // Check for immediate values
  try {
    size_t end_pos = 0;
    this->value = stol(text, &end_pos, 0);
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
    throw runtime_error(string_printf("incorrect type for argument %zu (expected %s, received %s)",
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
  if (ends_with(message, ", ")) {
    message.resize(message.size() - 2);
  }
  message.push_back(')');
  throw runtime_error(message);
}

static std::string dasm_disp(uint8_t base_reg_num, int32_t disp) {
  if (disp == 0) {
    // TODO: Remove the + 0 here.
    return string_printf("[r%hhu + 0]", base_reg_num);
  } else if (disp > 0) {
    return string_printf("[r%hhu + 0x%" PRIX32 "]", base_reg_num, disp);
  } else {
    return string_printf("[r%hhu - 0x%" PRIX32 "]", base_reg_num, -disp);
  }
}

static std::string dasm_disp_gbr(int32_t disp) {
  if (disp == 0) {
    return "[gbr]";
  } else if (disp > 0) {
    return string_printf("[gbr + 0x%" PRIX32 "]", disp);
  } else {
    return string_printf("[gbr - 0x%" PRIX32 "]", -disp);
  }
}

static std::string dasm_b_target(uint32_t pc, int32_t disp) {
  disp += 4;
  if (disp == 0) {
    return string_printf("+0x0 // %08" PRIX32, pc + disp);
  } else if (disp > 0) {
    return string_printf("+0x%" PRIX32 " // %08" PRIX32, disp, pc + disp);
  } else {
    return string_printf("-0x%" PRIX32 " // %08" PRIX32, -disp, pc + disp);
  }
}

static std::string dasm_imm(int32_t value) {
  if (value < 0) {
    return string_printf("-0x%" PRIX32, -value);
  } else {
    return string_printf("0x%" PRIX32, value);
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
            return string_printf("stc     r%hhu, %s", reg1, reg_names[reg2]);
          } else if (reg2 & 8) {
            // 0000nnnn1mmm0010 stc    rn, rmb
            return string_printf("stc     r%hhu, r%hhub", reg1, static_cast<uint8_t>(reg2 & 7));
          }
          break;
        }
        case 0x3:
          switch (op_get_r2(op)) {
            case 0x0: // 0000nnnn00000011 calls  (pc + 4 + rn)
              return string_printf("calls   npc + r%hhu // 0x%08" PRIX32 " + r%hhu", op_get_r1(op), s.pc + 4, op_get_r1(op));
            case 0x2: // 0000nnnn00100011 bs     (pc + 4 + rn)
              return string_printf("bs      npc + r%hhu // 0x%08" PRIX32 " + r%hhu", op_get_r1(op), s.pc + 4, op_get_r1(op));
            case 0x8: // 0000nnnn10000011 pref   [rn]  # prefetch
              return string_printf("pref    [r%hhu]", op_get_r1(op));
            case 0x9: // 0000nnnn10010011 ocbi   [rn]  # dcbi
              return string_printf("ocbi    [r%hhu]", op_get_r1(op));
            case 0xA: // 0000nnnn10100011 ocbp   [rn]  # dcbf
              return string_printf("ocbp    [r%hhu]", op_get_r1(op));
            case 0xB: // 0000nnnn10110011 ocbwb  [rn]  # dcbst?
              return string_printf("ocbwb   [r%hhu]", op_get_r1(op));
            case 0xC: // 0000nnnn11000011 movca.l [rn], r0
              return string_printf("movca.l [r%hhu], r0", op_get_r1(op));
          }
          break;
        case 0x4: // 0000nnnnmmmm0100 mov.b  [r0 + rn], rm
          return string_printf("mov.b   [r%hhu + r0], r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x5: // 0000nnnnmmmm0101 mov.w  [r0 + rn], rm
          return string_printf("mov.w   [r%hhu + r0], r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x6: // 0000nnnnmmmm0110 mov.l  [r0 + rn], rm
          return string_printf("mov.l   [r%hhu + r0], r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x7: // 0000nnnnmmmm0111 mul.l  rn, rm // macl = rn * rm
          return string_printf("mul.l   r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
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
              return string_printf("movt    r%hhu, t", op_get_r1(op));
          }
          break;
        case 0xA:
          switch (op_get_r2(op)) {
            case 0x0: // 0000nnnn00001010 sts    rn, mach
              return string_printf("sts     r%hhu, mach", op_get_r1(op));
            case 0x1: // 0000nnnn00011010 sts    rn, macl
              return string_printf("sts     r%hhu, macl", op_get_r1(op));
            case 0x2: // 0000nnnn00101010 sts    rn, pr
              return string_printf("sts     r%hhu, pr", op_get_r1(op));
            case 0x3: // 0000nnnn00111010 stc    rn, sgr
              return string_printf("stc     r%hhu, sgr", op_get_r1(op));
            case 0x5: // 0000nnnn01011010 sts    rn, fpul
              return string_printf("sts     r%hhu, fpul", op_get_r1(op));
            case 0x6: // 0000nnnn01101010 sts    rn, fpscr
              return string_printf("sts     r%hhu, fpscr", op_get_r1(op));
            case 0xF: // 0000nnnn11111010 stc    rn, dbr
              return string_printf("stc     r%hhu, dbr", op_get_r1(op));
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
          return string_printf("mov.b   r%hhu, [r%hhu + r0]", op_get_r1(op), op_get_r2(op));
        case 0xD: // 0000nnnnmmmm1101 mov.w  rn, [r0 + rm]  # sign-ext
          return string_printf("mov.w   r%hhu, [r%hhu + r0]", op_get_r1(op), op_get_r2(op));
        case 0xE: // 0000nnnnmmmm1110 mov.l  rn, [r0 + rm]
          return string_printf("mov.l   r%hhu, [r%hhu + r0]", op_get_r1(op), op_get_r2(op));
        case 0xF: // 0000nnnnmmmm1111 mac.l  [rn]+, [rm]+  # mac = [rn] * [rm] + mac
          return string_printf("mac.l   [r%hhu]+, [r%hhu]+", op_get_r1(op), op_get_r2(op));
      }
      break;

    case 0x1: { // 0001nnnnmmmmdddd mov.l  [rn + 4 * d], rm
      auto ref_str = dasm_disp(op_get_r1(op), op_get_simm4(op) * 4);
      return string_printf("mov.l   %s, r%hhu", ref_str.c_str(), op_get_r2(op));
    }

    case 0x2:
      switch (op_get_r3(op)) {
        case 0x0: // 0010nnnnmmmm0000 mov.b  [rn], rm
          return string_printf("mov.b   [r%hhu], r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x1: // 0010nnnnmmmm0001 mov.w  [rn], rm
          return string_printf("mov.w   [r%hhu], r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x2: // 0010nnnnmmmm0010 mov.l  [rn], rm
          return string_printf("mov.l   [r%hhu], r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x4: // 0010nnnnmmmm0100 mov.b  -[rn], rm
          return string_printf("mov.b   -[r%hhu], r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x5: // 0010nnnnmmmm0101 mov.w  -[rn], rm
          return string_printf("mov.w   -[r%hhu], r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x6: // 0010nnnnmmmm0110 mov.l  -[rn], rm
          return string_printf("mov.l   -[r%hhu], r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x7: // 0010nnnnmmmm0111 div0s  rn, rm
          return string_printf("div0s   r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x8: // 0010nnnnmmmm1000 test   rn, rm
          return string_printf("test    r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x9: // 0010nnnnmmmm1001 and    rn, rm
          return string_printf("and     r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0xA: // 0010nnnnmmmm1010 xor    rn, rm
          return string_printf("xor     r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0xB: // 0010nnnnmmmm1011 or     rn, rm
          return string_printf("or      r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0xC: // 0010nnnnmmmm1100 cmpstr rn, rm  # any bytes are equal
          return string_printf("cmpstr  r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0xD: // 0010nnnnmmmm1101 xtrct  rn, rm  # rm.rn middle 32 bits -> rn
          return string_printf("xtrct   r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0xE: // 0010nnnnmmmm1110 mulu.w rn, rm // macl = rn * rm
          return string_printf("mulu.w  r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0xF: // 0010nnnnmmmm1111 muls.w rn, rm // macl = rn * rm
          return string_printf("muls.w  r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
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
        ret += string_printf("r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        return ret;
      }
      break;
    }

    case 0x4:
      switch (op_get_r3(op)) {
        case 0x0:
          switch (op_get_r2(op)) {
            case 0x0: // 0100nnnn00000000 shl    rn
              return string_printf("shl     r%hhu", op_get_r1(op));
            case 0x1: // 0100nnnn00010000 dec    rn
              return string_printf("dec     r%hhu", op_get_r1(op));
            case 0x2: // 0100nnnn00100000 shal   rn
              return string_printf("shal    r%hhu", op_get_r1(op));
          }
          break;
        case 0x1:
          switch (op_get_r2(op)) {
            case 0x0: // 0100nnnn00000001 shr    rn
              return string_printf("shr     r%hhu", op_get_r1(op));
            case 0x1: // 0100nnnn00010001 cmpge  rn, 0
              return string_printf("cmpge   r%hhu, 0", op_get_r1(op));
            case 0x2: // 0100nnnn00100001 shar   rn
              return string_printf("shar    r%hhu", op_get_r1(op));
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
            return string_printf("st%c.l   -[r%hhu], %s", ((reg2 & 3) == 3) ? 'c' : 's', op_get_r1(op), reg_name);
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
            return string_printf("stc.l   -[r%hhu], %s", op_get_r1(op), reg_names[reg2]);
          } else if (reg2 & 8) {
            // 0100nnnn1mmm0011 stc.l  -[rn], rmb
            return string_printf("stc.l   -[r%hhu], r%hhub", op_get_r1(op), static_cast<uint8_t>(reg2 & 7));
          }
          break;
        }
        case 0x4:
          if (!(op_get_r2(op) & 0xD)) {
            // 0100nnnn00000100 rol    rn
            // 0100nnnn00100100 rcl    rn
            return string_printf("r%cl     r%hhu", op_get_r2(op) ? 'c' : 'o', op_get_r1(op));
          }
          break;
        case 0x5:
          switch (op_get_r2(op)) {
            case 0x0: // 0100nnnn00000101 ror    rn
              return string_printf("ror     r%hhu", op_get_r1(op));
            case 0x1: // 0100nnnn00010101 cmpgt  rn, 0
              return string_printf("cmpgt   r%hhu, 0", op_get_r1(op));
            case 0x2: // 0100nnnn00100101 rcr    rn
              return string_printf("rcr     r%hhu", op_get_r1(op));
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
            return string_printf("ld%c%s   %s, [r%hhu]+", (reg2 & 8) ? 'c' : 's', (reg2 & 4) ? ".l" : "  ", reg_name, op_get_r1(op));
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
            return string_printf("ldc.l   %s, [r%hhu]+", reg_names[reg2], op_get_r1(op));
          } else if (reg2 & 8) {
            // 0100mmmm1nnn0111 ldc.l  rnb, [rm]+
            return string_printf("ldc.l   r%hhub, [r%hhu]+", static_cast<uint8_t>(reg2 & 7), op_get_r1(op));
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
            return string_printf("sh%c     r%hhu, %hhu", is_shr ? 'r' : 'l', op_get_r1(op), shifts[reg2]);
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
            return string_printf("ld%c     %s, r%hhu", (reg2 & 8) ? 'c' : 's', reg_name, op_get_r1(op));
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
            return string_printf("%s   [r%hhu]", names[reg2], op_get_r1(op));
          }
          break;
        }
        case 0xC: // 0100nnnnmmmm1100 shad   rn, rm
        case 0xD: // 0100nnnnmmmm1101 shld   rn, rm
          return string_printf("sh%cd    r%hhu, r%hhu", (op_get_r3(op) & 1) ? 'l' : 'a', op_get_r1(op), op_get_r2(op));
        case 0xE: {
          static const array<const char*, 5> reg_names = {"sr", "gbr", "vbr", "ssr", "spc"};
          uint8_t reg2 = op_get_r2(op);
          if (reg2 < reg_names.size()) {
            // 0100mmmm00001110 ldc    sr, rm
            // 0100mmmm00011110 ldc    gbr, rm
            // 0100mmmm00101110 ldc    vbr, rm
            // 0100mmmm00111110 ldc    ssr, rm
            // 0100mmmm01001110 ldc    spc, rm
            return string_printf("ldc     %s, r%hhu", reg_names[reg2], op_get_r1(op));
          } else if (reg2 & 8) {
            // 0100mmmm1nnn1110 ldc    rnb, rm
            return string_printf("ldc     r%hhub, r%hhu", static_cast<uint8_t>(reg2 & 7), op_get_r1(op));
          }
          break;
        }
        case 0xF: // 0100nnnnmmmm1111 mac.w  [rn]+, [rm]+ // mac = [rn] * [rm] + mac
          return string_printf("mac.w   [r%hhu]+, [r%hhu]+", op_get_r1(op), op_get_r2(op));
      }
      break;

    case 0x5: { // 0101nnnnmmmmdddd mov.l  rn, [rm + 4 * d]
      return string_printf("mov.l   r%hhu, ", op_get_r1(op)) + dasm_disp(op_get_r2(op), op_get_simm4(op) * 4);
    }

    case 0x6:
      switch (op_get_r3(op)) {
        case 0x0: // 0110nnnnmmmm0000 mov.b  rn, [rm]  # sign-ext
          return string_printf("mov.b   r%hhu, [r%hhu]", op_get_r1(op), op_get_r2(op));
        case 0x1: // 0110nnnnmmmm0001 mov.w  rn, [rm]  # sign-ext
          return string_printf("mov.w   r%hhu, [r%hhu]", op_get_r1(op), op_get_r2(op));
        case 0x2: // 0110nnnnmmmm0010 mov.l  rn, [rm]
          return string_printf("mov.l   r%hhu, [r%hhu]", op_get_r1(op), op_get_r2(op));
        case 0x3: // 0110nnnnmmmm0011 mov    rn, rm
          return string_printf("mov     r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x4: // 0110nnnnmmmm0100 mov.b  rn, [rm]+  # sign-ext
          return string_printf("mov.b   r%hhu, [r%hhu]+", op_get_r1(op), op_get_r2(op));
        case 0x5: // 0110nnnnmmmm0101 mov.w  rn, [rm]+  # sign-ext
          return string_printf("mov.w   r%hhu, [r%hhu]+", op_get_r1(op), op_get_r2(op));
        case 0x6: // 0110nnnnmmmm0110 mov.l  rn, [rm]+
          return string_printf("mov.l   r%hhu, [r%hhu]+", op_get_r1(op), op_get_r2(op));
        case 0x7: // 0110nnnnmmmm0111 not    rn, rm
          return string_printf("not     r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x8: // 0110nnnnmmmm1000 swap.b rn, rm  # swap lower 2 bytes
          return string_printf("swap.b  r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0x9: // 0110nnnnmmmm1001 swap.w rn, rm  # swap words
          return string_printf("swap.w  r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0xA: // 0110nnnnmmmm1010 negc   rn, rm
          return string_printf("negc    r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0xB: // 0110nnnnmmmm1011 neg    rn, rm
          return string_printf("neg     r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0xC: // 0110nnnnmmmm1100 extu.b rn, rm
          return string_printf("extu.b  r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0xD: // 0110nnnnmmmm1101 extu.w rn, rm
          return string_printf("extu.w  r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0xE: // 0110nnnnmmmm1110 exts.b rn, rm
          return string_printf("exts.b  r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
        case 0xF: // 0110nnnnmmmm1111 exts.w rn, rm
          return string_printf("exts.w  r%hhu, r%hhu", op_get_r1(op), op_get_r2(op));
      }
      break;

    case 0x7: // 0111nnnniiiiiiii add    rn, imm
      return string_printf("add     r%hhu, ", op_get_r1(op)) + dasm_imm(op_get_simm8(op));

    case 0x8:
      switch (op_get_r1(op)) {
        case 0x0: // 10000000nnnndddd mov.b  [rn + d], r0
          return "mov.b   " + dasm_disp(op_get_r2(op), op_get_simm4(op)) + ", r0";
        case 0x1: // 10000001nnnndddd mov.w  [rn + 2 * d], r0
          return "mov.w   " + dasm_disp(op_get_r2(op), 2 * op_get_simm4(op)) + ", r0";
        case 0x4: // 10000100mmmmdddd mov.b  r0, [rm + d]  # sign-ext
          return "mov.b   r0, " + dasm_disp(op_get_r2(op), op_get_simm4(op));
        case 0x5: // 10000101mmmmdddd mov.w  r0, [rm + 2 * d]  # sign-ext
          return "mov.w   r0, " + dasm_disp(op_get_r2(op), 2 * op_get_simm4(op));
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
      uint32_t referenced_pc = s.pc + 4 + 2 * op_get_simm8(op);
      string value_suffix;
      try {
        value_suffix = string_printf(" /* 0x%04hX */", s.r.pget_u16l(referenced_pc - s.start_pc));
      } catch (const out_of_range&) {
        value_suffix = " /* reference out of range */";
      }
      return string_printf("mov.w   r%hhu, [0x%08" PRIX32 "]%s", op_get_r1(op), referenced_pc, value_suffix.c_str());
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
          return "mov.b   " + dasm_disp_gbr(op_get_simm8(op)) + ", r0";
        case 0x1: // 11000001dddddddd mov.w  [gbr + 2 * d], r0
          return "mov.w   " + dasm_disp_gbr(2 * op_get_simm8(op)) + ", r0";
        case 0x2: // 11000010dddddddd mov.l  [gbr + 4 * d], r0
          return "mov.l   " + dasm_disp_gbr(4 * op_get_simm8(op)) + ", r0";
        case 0x3: // 11000011iiiiiiii trapa  imm
          return "trapa   " + dasm_imm(op_get_uimm8(op));
        case 0x4: // 11000100dddddddd mov.b  r0, [gbr + d]  # sign-ext
          return "mov.b   r0, " + dasm_disp_gbr(op_get_simm8(op));
        case 0x5: // 11000101dddddddd mov.w  r0, [gbr + 2 * d]  # sign-ext
          return "mov.w   r0, " + dasm_disp_gbr(2 * op_get_simm8(op));
        case 0x6: // 11000110dddddddd mov.l  r0, [gbr + 4 * d]
          return "mov.l   r0, " + dasm_disp_gbr(4 * op_get_simm8(op));
        case 0x7: // 11000111dddddddd mova   r0, [(pc & ~3) + 4 + disp * 4]
          return string_printf("mova    r0, [0x%08" PRIX32 "]", static_cast<uint32_t>(s.pc & (~3)) + 4 + 4 * op_get_simm8(op));
        case 0x8: // 11001000iiiiiiii test   r0, imm
        case 0x9: // 11001001iiiiiiii and    r0, imm
        case 0xA: // 11001010iiiiiiii xor    r0, imm
        case 0xB: { // 11001011iiiiiiii or     r0, imm
          static const array<const char*, 4> names = {"test", "and ", "xor ", "or  "};
          return string_printf("%s    r0, ", names[op_get_r1(op) & 3]) + dasm_imm(op_get_uimm8(op));
        }
        case 0xC: // 11001100iiiiiiii test.b [r0 + gbr], imm
        case 0xD: // 11001101iiiiiiii and.b  [r0 + gbr], imm
        case 0xE: // 11001110iiiiiiii xor.b  [r0 + gbr], imm
        case 0xF: { // 11001111iiiiiiii or.b   [r0 + gbr], imm
          static const array<const char*, 4> names = {"test.b", "and.b ", "xor.b ", "or.b  "};
          return string_printf("%s  [gbr + r0], ", names[op_get_r1(op) & 3]) + dasm_imm(op_get_uimm8(op));
        }
      }
      break;

    case 0xD: { // 1101nnnndddddddd mov.l  rn, [(pc & ~3) + 4 + d * 4]
      uint32_t referenced_pc = (s.pc & (~3)) + 4 + 4 * op_get_simm8(op);
      string value_suffix;
      try {
        value_suffix = string_printf(" /* 0x%08" PRIX32 " */", s.r.pget_u32l(referenced_pc - s.start_pc));
      } catch (const out_of_range&) {
        value_suffix = " /* reference out of range */";
      }
      return string_printf("mov.l   r%hhu, [0x%08" PRIX32 "]%s", op_get_r1(op), referenced_pc, value_suffix.c_str());
    }

    case 0xE: // 1110nnnniiiiiiii mov    rn, imm
      return string_printf("mov     r%hhu, ", op_get_r1(op)) + dasm_imm(op_get_simm8(op));

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
          return string_printf("%s  %cr%hhu, %cr%hhu",
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
              return string_printf("fmov    xd%hhu, [r%hhu%s]", static_cast<uint8_t>(op_get_r1(op) & 0xE), op_get_r2(op), suffix);
            } else {
              return string_printf("fmov    dr%hhu, [r%hhu%s]", op_get_r1(op), op_get_r2(op), suffix);
            }
          } else {
            return string_printf("fmov.s  fr%hhu, [r%hhu%s]", op_get_r1(op), op_get_r2(op), suffix);
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
              return string_printf("fmov    [r%hhu%s], xd%hhu", op_get_r1(op), suffix, static_cast<uint8_t>(op_get_r2(op) & 0xE));
            } else {
              return string_printf("fmov    [r%hhu%s], dr%hhu", op_get_r1(op), suffix, op_get_r2(op));
            }
          } else {
            return string_printf("fmov.s  [r%hhu%s], fr%hhu", op_get_r1(op), suffix, op_get_r2(op));
          }
          break;
        }
        case 0x9:
          // 1111nnn0mmmm1001 fmov   drn, [rm]+
          // 1111nnn1mmmm1001 fmov   xdn, [rm]+
          // 1111nnnnmmmm1001 fmov.s frn, [rm]+
          if (s.double_precision) {
            if (op & 0x0100) {
              return string_printf("fmov    xd%hhu, [r%hhu]+", static_cast<uint8_t>(op_get_r1(op) & 0xE), op_get_r2(op));
            } else {
              return string_printf("fmov    dr%hhu, [r%hhu]+", op_get_r1(op), op_get_r2(op));
            }
          } else {
            return string_printf("fmov.s  fr%hhu, [r%hhu]+", op_get_r1(op), op_get_r2(op));
          }
          break;
        case 0xB:
          // 1111nnnnmmm01011 fmov   -[rn], drm
          // 1111nnnnmmm11011 fmov   -[rn], xdm
          // 1111nnnnmmmm1011 fmov.s -[rn], frm
          if (s.double_precision) {
            if (op & 0x0010) {
              return string_printf("fmov    -[r%hhu], xd%hhu", op_get_r1(op), static_cast<uint8_t>(op_get_r2(op) & 0xE));
            } else {
              return string_printf("fmov    -[r%hhu], dr%hhu", op_get_r1(op), op_get_r2(op));
            }
          } else {
            return string_printf("fmov.s  -[r%hhu], fr%hhu", op_get_r1(op), op_get_r2(op));
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
            return string_printf("fmov    %s%hhu, %s%hhu",
                (reg1 & 1) ? "xd" : "dr", static_cast<uint8_t>(reg1 & 0xE),
                (reg2 & 1) ? "xd" : "dr", static_cast<uint8_t>(reg2 & 0xE));
          } else {
            // 1111nnnnmmmm1100 fmov   frn, frm
            return string_printf("fmov    fr%hhu, fr%hhu", op_get_r1(op), op_get_r2(op));
          }
          break;
        case 0xD:
          switch (op_get_r2(op)) {
            case 0x0: // 1111nnnn00001101 fsts   frm, fpul
              return string_printf("fsts    fr%hhu, fpul", op_get_r1(op));
            case 0x1: // 1111mmmm00011101 flds   fpul, frm
              return string_printf("flds    fpul, fr%hhu", op_get_r1(op));
            case 0x2:
              // 1111nnn000101101 float  drn, fpul
              // 1111nnnn00101101 float  frn, fpul
              if (s.double_precision && (op & 0x0100)) {
                break;
              }
              return string_printf("float   %cr%hhu, fpul", s.double_precision ? 'd' : 'f', op_get_r1(op));
            case 0x3:
              // 1111mmm000111101 ftrc   fpul, drn
              // 1111mmmm00111101 ftrc   fpul, frm
              if (s.double_precision && (op & 0x0100)) {
                break;
              }
              return string_printf("ftrc    fpul, %cr%hhu", s.double_precision ? 'd' : 'f', op_get_r1(op));
            case 0x4:
              // 1111nnn001001101 fneg   drn
              // 1111nnnn01001101 fneg   frn
              if (s.double_precision && (op & 0x0100)) {
                break;
              }
              return string_printf("fneg    %cr%hhu", s.double_precision ? 'd' : 'f', op_get_r1(op));
            case 0x5:
              // 1111nnn001011101 fabs   drn
              // 1111nnnn01011101 fabs   frn
              if (s.double_precision && (op & 0x0100)) {
                break;
              }
              return string_printf("fabs    %cr%hhu", s.double_precision ? 'd' : 'f', op_get_r1(op));
            case 0x6:
              // 1111nnn001101101 fsqrt  drn
              // 1111nnnn01101101 fsqrt  frn
              if (s.double_precision && (op & 0x0100)) {
                break;
              }
              return string_printf("fsqrt   %cr%hhu", s.double_precision ? 'd' : 'f', op_get_r1(op));
            case 0x8: // 1111nnnn10001101 fldi0  frn
              return string_printf("fldi0   fr%hhu", op_get_r1(op));
            case 0x9: // 1111nnnn10011101 fldi1  frn
              return string_printf("fldi1   fr%hhu", op_get_r1(op));
            case 0xA: // 1111nnn010101101 fcnvsd drn, fpul
              if (!s.double_precision || (op & 0x0100)) {
                break;
              }
              return string_printf("fcnvsd  dr%hhu, fpul", op_get_r1(op));
            case 0xB: // 1111mmm010111101 fcnvds fpul, drm
              if (!s.double_precision || (op & 0x0100)) {
                break;
              }
              return string_printf("fcnvds  fpul, dr%hhu", op_get_r1(op));
            case 0xE: // 1111nnmm11101101 fipr   fvn, fvm  # fs(n+3) = dot(fvn, fvm)
              return string_printf("fipr    fv%hhu, fv%hhu", static_cast<uint8_t>(op_get_r1(op) & 0xC), static_cast<uint8_t>((op_get_r1(op) << 2) & 0xC));
            case 0xF: {
              uint8_t reg1 = op_get_r1(op);
              if ((reg1 & 0x3) == 0x1) {
                // 1111nn0111111101 ftrv   fvn, xmtrx
                return string_printf("ftrv    fv%hhu, xmtrx", static_cast<uint8_t>(reg1 & 0xC));
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
          return string_printf("fmac    fr%hhu, fr%hhu", op_get_r1(op), op_get_r2(op));
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
  bool is_add = starts_with(si.op_name, "add");
  bool is_sub = starts_with(si.op_name, "sub");
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

  bool is_postinc = ends_with(si.op_name, ".l");
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
    check_range_t(si.args[0].value, -8 * (1 << size), 7 * (1 << size));
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
    check_range_t(si.args[0].value, -0x80 * (1 << size), 0x7F * (1 << size));
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
    check_range_t(si.args[1].value, -8 * (1 << size), 7 * (1 << size));
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
    check_range_t(si.args[1].value, -0x80 * (1 << size), 0x7F * (1 << size));
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
        : this->label_offsets.at(si.args[0].label_name);
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

  bool is_predec = ends_with(si.op_name, ".l");
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

  bool is_predec = ends_with(si.op_name, ".l");
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

string SH4Emulator::disassemble_one(uint32_t pc, uint16_t op, bool double_precision) {
  le_uint16_t mem = op;
  DisassemblyState s = {
      .pc = pc,
      .start_pc = pc,
      .double_precision = double_precision,
      .labels = nullptr,
      .branch_target_addresses = {},
      .r = StringReader(&mem, sizeof(mem)),
  };
  return SH4Emulator::disassemble_one(s, op);
}

string SH4Emulator::disassemble(
    const void* data,
    size_t size,
    uint32_t start_pc,
    const multimap<uint32_t, string>* in_labels,
    bool double_precision) {
  static const multimap<uint32_t, string> empty_labels_map = {};

  DisassemblyState s = {
      .pc = start_pc,
      .start_pc = start_pc,
      .double_precision = double_precision,
      .labels = (in_labels ? in_labels : &empty_labels_map),
      .branch_target_addresses = {},
      .r = StringReader(data, size),
  };

  const le_uint16_t* opcodes = reinterpret_cast<const le_uint16_t*>(data);

  // Phase 1: generate the disassembly for each opcode, and collect branch
  // target addresses
  size_t line_count = size / 2;
  forward_list<string> lines;
  auto add_line_it = lines.before_begin();
  for (size_t x = 0; x < line_count; x++, s.pc += 2) {
    uint32_t opcode = opcodes[x];
    string line = string_printf("%08X  %04X  ", s.pc, opcode);
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
        label = string_printf("%s: // at %08" PRIX32 " (misaligned)\n",
            label_it->second.c_str(), label_it->first);
      } else {
        label = string_printf("%s:\n", label_it->second.c_str());
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
        label = string_printf("%s%08" PRIX32 ": // (misaligned)\n",
            label_type, branch_target_addresses_it->first);
      } else {
        label = string_printf("%s%08" PRIX32 ":\n",
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
        if (isfile(filename)) {
          if (!get_include_stack.emplace(name).second) {
            throw runtime_error("mutual recursion between includes: " + name);
          }
          const auto& ret = SH4Emulator::assemble(load_file(filename), get_include, start_address).code;
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
    return SH4Emulator::assemble(text, get_include, start_address);
  }
}

void SH4Emulator::Assembler::assemble(const string& text, function<string(const string&)> get_include) {
  string effective_text = text;
  strip_multiline_comments(effective_text);

  // First pass: generate args and labels and collect metadata
  StringReader r(effective_text);
  size_t line_num = 0;
  size_t stream_offset = 0;
  while (!r.eof()) {
    string line = r.get_line();
    line_num++;

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
    } else if (ends_with(line, ":")) {
      line.pop_back();
      strip_trailing_whitespace(line);
      if (!label_offsets.emplace(line, stream_offset).second) {
        throw runtime_error(string_printf("(line %zu) duplicate label: %s", line_num, line.c_str()));
      }

    } else {
      // Get the opcode name and arguments
      vector<string> tokens = split(line, ' ', 1);
      if (tokens.size() == 0) {
        throw logic_error(string_printf("(line %zu) no tokens in non-empty line", line_num));
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

      const StreamItem& si = this->stream.emplace_back(
          StreamItem{stream_offset, line_num, op_name, std::move(args)});
      if (si.op_name == ".include") {
        si.check_arg_types({ArgType::BRANCH_TARGET});
        const string& inc_name = si.args[0].label_name;
        if (!get_include) {
          throw runtime_error(string_printf("(line %zu) includes are not available", line_num));
        }
        string contents;
        try {
          const string& contents = this->includes_cache.at(inc_name);
          stream_offset += (contents.size() + 1) & (~1);
        } catch (const out_of_range&) {
          try {
            contents = get_include(inc_name);
          } catch (const exception& e) {
            throw runtime_error(string_printf("(line %zu) failed to get include data: %s", line_num, e.what()));
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
  }

  // Second pass: generate opcodes
  for (const auto& si : this->stream) {
    if (si.op_name == ".include") {
      si.check_arg_types({ArgType::BRANCH_TARGET});
      try {
        const string& include_contents = this->includes_cache.at(si.args[0].label_name);
        this->code.write(include_contents);
        while (this->code.size() & 1) {
          this->code.put_u8(0);
        }
      } catch (const out_of_range&) {
        throw logic_error(string_printf("(line %zu) include data missing from cache", line_num));
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
      try {
        this->code.put_u32l(this->label_offsets.at(si.args[0].label_name));
      } catch (const exception& e) {
        throw runtime_error(string_printf("(line %zu) failed: %s", si.line_num, e.what()));
      }

    } else if (si.op_name == ".binary") {
      si.check_arg_types({ArgType::RAW});
      string data = parse_data_string(si.args[0].label_name);
      data.resize((data.size() + 1) & (~1), '\0');
      this->code.write(data);

    } else {
      try {
        auto fn = this->assemble_functions.at(si.op_name);
        this->code.put_u16l((this->*fn)(si));
      } catch (const exception& e) {
        throw runtime_error(string_printf("(line %zu) failed: %s", si.line_num, e.what()));
      }
    }
  }
}
