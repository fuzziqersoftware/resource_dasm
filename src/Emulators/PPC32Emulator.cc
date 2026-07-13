#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <deque>
#include <filesystem>
#include <forward_list>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <phosg/Tools.hh>
#include <set>
#include <string>

#include "PPC32Emulator.hh"

namespace ResourceDASM {

const char* PPC32Emulator::Assembler::Argument::name_for_type(Type type) {
  switch (type) {
    case Type::INT_REGISTER:
      return "INT_REGISTER";
    case Type::FLOAT_REGISTER:
      return "FLOAT_REGISTER";
    case Type::SPECIAL_REGISTER:
      return "SPECIAL_REGISTER";
    case Type::TIME_REGISTER:
      return "TIME_REGISTER";
    case Type::CONDITION_FIELD:
      return "CONDITION_FIELD";
    case Type::CONDITION_BIT:
      return "CONDITION_BIT";
    case Type::IMMEDIATE:
      return "IMMEDIATE";
    case Type::IMM_MEMORY_REFERENCE:
      return "IMM_MEMORY_REFERENCE";
    case Type::REG_MEMORY_REFERENCE:
      return "REG_MEMORY_REFERENCE";
    case Type::LIKELY:
      return "LIKELY";
    case Type::RAW:
      return "RAW";
    default:
      return "__UNKNOWN__";
  }
}

template <typename T>
void check_range_t(T value, int64_t min, int64_t max) {
  if (value < min) {
    throw std::runtime_error(std::format("value {} before beginning of range [{}, {}]", value, min, max));
  }
  if (value > max) {
    throw std::runtime_error(std::format("value {} beyond end of range [{}, {}]", value, min, max));
  }
}

static inline uint8_t op_get_op(uint32_t op) {
  return ((op >> 26) & 0x0000003F);
}
__attribute__((unused)) static inline uint32_t op_set_op(uint32_t v) {
  check_range_t(v, 0, 0x3F);
  return (v & 0x0000003F) << 26;
}

static inline uint8_t op_get_crf1(uint32_t op) {
  return ((op >> 23) & 0x00000007);
}
static inline uint32_t op_set_crf1(uint32_t v) {
  check_range_t(v, 0, 7);
  return (v & 0x00000007) << 23;
}

static inline uint8_t op_get_crf2(uint32_t op) {
  return ((op >> 18) & 0x00000007);
};
static inline uint32_t op_set_crf2(uint32_t v) {
  check_range_t(v, 0, 7);
  return (v & 0x00000007) << 18;
}

static inline uint8_t op_get_reg1(uint32_t op) {
  return ((op >> 21) & 0x0000001F);
}
static inline uint32_t op_set_reg1(uint32_t v) {
  check_range_t(v, 0, 0x1F);
  return (v & 0x0000001F) << 21;
}

static inline uint8_t op_get_reg2(uint32_t op) {
  return ((op >> 16) & 0x0000001F);
}
static inline uint32_t op_set_reg2(uint32_t v) {
  check_range_t(v, 0, 0x1F);
  return (v & 0x0000001F) << 16;
}

static inline uint8_t op_get_reg3(uint32_t op) {
  return ((op >> 11) & 0x0000001F);
}
static inline uint32_t op_set_reg3(uint32_t v) {
  check_range_t(v, 0, 0x1F);
  return (v & 0x0000001F) << 11;
}

static inline uint8_t op_get_reg4(uint32_t op) {
  return ((op >> 6) & 0x0000001F);
}
static inline uint32_t op_set_reg4(uint32_t v) {
  check_range_t(v, 0, 0x1F);
  return (v & 0x0000001F) << 6;
}

static inline uint8_t op_get_reg5(uint32_t op) {
  return ((op >> 1) & 0x0000001F);
}
static inline uint32_t op_set_reg5(uint32_t v) {
  check_range_t(v, 0, 0x1F);
  return (v & 0x0000001F) << 1;
}

static inline uint8_t op_get_bi(uint32_t op) {
  return ((op >> 16) & 0x0000001F);
}
static inline uint32_t op_set_bi(uint32_t v) {
  check_range_t(v, 0, 0x1F);
  return (v & 0x0000001F) << 16;
}

static inline bool op_get_b_abs(uint32_t op) {
  return ((op >> 1) & 0x00000001);
}
static inline uint32_t op_set_b_abs(uint32_t v) {
  check_range_t(v, 0, 1);
  return v ? 0x00000002 : 0x00000000;
}

static inline bool op_get_b_link(uint32_t op) {
  return (op & 0x00000001);
}
static inline uint32_t op_set_b_link(uint32_t v) {
  check_range_t(v, 0, 1);
  return v ? 0x00000001 : 0x00000000;
}

static inline uint16_t op_get_spr(uint32_t op) {
  return ((op >> 16) & 0x1F) | ((op >> 6) & 0x3E0);
}
static inline uint32_t op_set_spr(uint32_t v) {
  check_range_t(v, 0, 0x3FF);
  return ((v & 0x1F) << 16) | ((v & 0x3E0) << 6);
}

static inline bool op_get_u(uint32_t op) {
  return ((op >> 26) & 0x00000001);
}
__attribute__((unused)) static inline uint32_t op_set_u(uint32_t v) {
  check_range_t(v, 0, 1);
  return (v & 0x00000001) << 26;
}

static inline bool op_get_rec4(uint32_t op) {
  return ((op >> 26) & 0x00000001);
}
static inline uint32_t op_set_rec4(uint32_t v) {
  check_range_t(v, 0, 1);
  return (v & 0x00000001) << 26;
}

static inline uint16_t op_get_subopcode(uint32_t op) {
  return ((op >> 1) & 0x000003FF);
}
static inline uint32_t op_set_subopcode(uint32_t v) {
  check_range_t(v, 0, 0x3FF);
  return (v & 0x000003FF) << 1;
}

static inline uint8_t op_get_short_subopcode(uint32_t op) {
  return ((op >> 1) & 0x0000001F);
}
__attribute__((unused)) static inline uint32_t op_set_short_subopcode(uint32_t v) {
  check_range_t(v, 0, 0x1F);
  return (v & 0x0000001F) << 1;
}

static inline bool op_get_o(uint32_t op) {
  return ((op >> 10) & 1);
}
static inline uint32_t op_set_o(uint32_t v) {
  check_range_t(v, 0, 1);
  return (v & 1) << 10;
}

static inline bool op_get_rec(uint32_t op) {
  return (op & 0x00000001);
}
static inline uint32_t op_set_rec(uint32_t v) {
  check_range_t(v, 0, 1);
  return v & 1;
}

static inline uint16_t op_get_uimm(uint32_t op) {
  return (op & 0x0000FFFF);
}
static inline int16_t op_get_simm(uint32_t op) {
  return (op & 0x0000FFFF);
}

static inline uint32_t op_set_imm(int32_t v) {
  check_range_t(v, -0x8000, 0xFFFF);
  return (v & 0x0000FFFF);
}
static inline uint32_t op_set_uimm(uint32_t v) {
  check_range_t(v, 0, 0xFFFF);
  return (v & 0x0000FFFF);
}
static inline uint32_t op_set_simm(int32_t v) {
  check_range_t(v, -0x8000, 0x7FFF);
  return (v & 0x0000FFFF);
}

static inline int32_t op_get_imm_ext(uint32_t op) {
  uint32_t ret = op_get_uimm(op);
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
static inline uint32_t op_set_b_target(int32_t v) {
  check_range_t(v, -0x02000000, 0x01FFFFFC);
  return (v & 0x03FFFFFC);
}

struct BranchBOField {
  uint8_t u;

  inline bool skip_condition() const {
    return (u >> 4) & 0x01;
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
static uint32_t op_set_bo(uint8_t bo) {
  return (bo & 0x1F) << 21;
}

const char* mnemonic_for_bc(uint8_t bo, uint8_t bi) {
  uint16_t as = ((bo & 0x1E) << 8) | (bi & 3);
  switch (as) {
    case 0x0000:
      return "dnzf";
    case 0x0400:
      return "ge";
    case 0x0401:
      return "le";
    case 0x0402:
      return "ne";
    case 0x0403:
      return "ns";
    case 0x0800:
      return "dnzt";
    case 0x0A00:
      return "dzt";
    case 0x0C00:
      return "lt";
    case 0x0C01:
      return "gt";
    case 0x0C02:
      return "eq";
    case 0x0C03:
      return "so";
    case 0x1000:
      return "dnz";
    case 0x1200:
      return "dz";
    case 0x1400:
      return "";
    default:
      return nullptr;
  }
}

// Returns a pair <bo, bi>
std::pair<uint8_t, uint8_t> bc_for_mnemonic(const std::string& name) {
  static const std::unordered_map<std::string, std::pair<uint8_t, uint8_t>> names({
      {"ge", std::make_pair(0x04, 0x00)},
      {"le", std::make_pair(0x04, 0x01)},
      {"ne", std::make_pair(0x04, 0x02)},
      {"ns", std::make_pair(0x04, 0x03)},
      {"lt", std::make_pair(0x0C, 0x00)},
      {"gt", std::make_pair(0x0C, 0x01)},
      {"eq", std::make_pair(0x0C, 0x02)},
      {"so", std::make_pair(0x0C, 0x03)},
      {"dnzf", std::make_pair(0x00, 0x00)},
      {"dnzt", std::make_pair(0x08, 0x00)},
      {"dnz", std::make_pair(0x10, 0x00)},
      {"dzf", std::make_pair(0x02, 0x00)},
      {"dzt", std::make_pair(0x0A, 0x00)},
      {"dz", std::make_pair(0x12, 0x00)},
      {"", std::make_pair(0x14, 0x00)},
  });
  return names.at(name);
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

uint32_t spr_for_name(const std::string& name) {
  std::unordered_map<std::string, uint16_t> names({
      {"xer", 1},
      {"lr", 8},
      {"ctr", 9},
      {"dsisr", 18},
      {"dar", 19},
      {"dec", 22},
      {"sdr1", 25},
      {"srr0", 26},
      {"srr1", 27},
      {"sprg0", 272},
      {"sprg1", 273},
      {"sprg2", 274},
      {"sprg3", 275},
      {"ear", 282},
      {"pvr", 287},
      {"ibat0u", 528},
      {"ibat0l", 529},
      {"ibat1u", 530},
      {"ibat1l", 531},
      {"ibat2u", 532},
      {"ibat2l", 533},
      {"ibat3u", 534},
      {"ibat3l", 535},
      {"dbat0u", 536},
      {"dbat0l", 537},
      {"dbat1u", 538},
      {"dbat1l", 539},
      {"dbat2u", 540},
      {"dbat2l", 541},
      {"dbat3u", 542},
      {"dbat3l", 543},
      {"dabr", 1013},
  });
  return names.at(name);
}

void PPC32Emulator::set_time_base(uint64_t time_base) {
  this->regs.tbr = time_base;
}

void PPC32Emulator::set_time_base(const std::vector<uint64_t>& time_overrides) {
  this->time_overrides.clear();
  this->time_overrides.insert(this->time_overrides.end(), time_overrides.begin(), time_overrides.end());
}

bool PPC32Emulator::should_branch(uint32_t op) {
  BranchBOField bo = op_get_bo(op);
  if (!bo.skip_ctr()) {
    this->regs.ctr--;
  }
  bool ctr_ok = bo.skip_ctr() || ((this->regs.ctr == 0) == bo.branch_if_ctr_zero());
  bool cond_ok = bo.skip_condition() ||
      (((this->regs.cr.u >> (31 - op_get_bi(op))) & 1) == bo.branch_condition_value());
  return ctr_ok && cond_ok;
}

static ssize_t reg_num_for_name(const std::string& name, const std::string& prefix, ssize_t num_regs_in_class) {
  if (name.starts_with(prefix) && (name.size() >= (prefix.size() + 1)) && isdigit(name[prefix.size()])) {
    size_t end_pos;
    ssize_t reg_num = stoll(name.substr(prefix.size()), &end_pos, 10);
    if ((end_pos == name.size() - prefix.size()) && (reg_num >= 0) && (reg_num < num_regs_in_class)) {
      return reg_num;
    }
  }
  return -1;
}

static ssize_t reg_num_for_node(const Expression::Node& node, const std::string& prefix, ssize_t num_regs_in_class) {
  const auto* lookup_node = dynamic_cast<const Expression::EnvLookupNode*>(&node);
  return lookup_node ? reg_num_for_name(lookup_node->name, prefix, num_regs_in_class) : -1;
}

static ssize_t upd_reg_num_for_node(const Expression::Node& node) {
  const auto* call_node = dynamic_cast<const Expression::FunctionCallNode*>(&node);
  return (call_node && (call_node->function_name == "upd") && (call_node->args.size() == 1))
      ? reg_num_for_node(*call_node->args[0], "r", 32)
      : -1;
}

static bool is_const_expression(const Expression::Node& node) {
  return node.is_const([](const std::string& name) -> bool {
    if ((reg_num_for_name(name, "r", 32) != -1) ||
        (reg_num_for_name(name, "f", 32) != -1) ||
        (reg_num_for_name(name, "spr", 1024) != -1) ||
        (reg_num_for_name(name, "tpr", 1024) != -1)) {
      return false;
    }
    static const std::unordered_set<std::string> reg_names{
        "xer", "lr", "ctr", "dsisr", "dar", "dec", "sdr1", "srr0", "srr1", "sprg0", "sprg1", "sprg2", "sprg3", "ear",
        "pvr", "ibat0u", "ibat0l", "ibat1u", "ibat1l", "ibat2u", "ibat2l", "ibat3u", "ibat3l", "dbat0u", "dbat0l",
        "dbat1u", "dbat1l", "dbat2u", "dbat2l", "dbat3u", "dbat3l", "dabr"};
    return !reg_names.contains(name);
  });
}

PPC32Emulator::Assembler::Argument::Argument(const std::string& text, bool raw)
    : type(Type::INT_REGISTER), reg_num(0), reg_num2(0) {
  if (text.empty()) {
    throw std::runtime_error("argument text is blank");
  }
  if (raw) {
    this->type = Type::RAW;
    this->raw_data = text;
    return;
  }

  // Int registers (r0-r31 or sp)
  if (text[0] == 'r') {
    try {
      this->reg_num = stoul(text.substr(1));
      this->type = Type::INT_REGISTER;
      return;
    } catch (const std::invalid_argument&) {
    }
  }
  if (text == "sp") {
    this->reg_num = 1;
    this->type = Type::INT_REGISTER;
    return;
  }

  // Float registers (f0-f31)
  if (text[0] == 'f') {
    try {
      this->reg_num = stoul(text.substr(1));
      this->type = Type::FLOAT_REGISTER;
      return;
    } catch (const std::invalid_argument&) {
    }
  }

  // Condition register fields/bits (crf0-7, crb0-31)
  if (text.starts_with("crf")) {
    try {
      this->reg_num = stoul(text.substr(3));
      this->type = Type::CONDITION_FIELD;
      return;
    } catch (const std::invalid_argument&) {
    }
  }
  if (text.starts_with("crb")) {
    try {
      this->reg_num = stoul(text.substr(3));
      this->type = Type::CONDITION_BIT;
      return;
    } catch (const std::invalid_argument&) {
    }
  }
  if (text.starts_with("cr")) {
    try {
      this->reg_num = stoul(text.substr(2));
      this->type = Type::CONDITION_FIELD;
      return;
    } catch (const std::invalid_argument&) {
    }
  }

  // Time base registers (tbr0-1023)
  if (text.starts_with("tbr")) {
    try {
      this->reg_num = stoul(text.substr(3));
      this->type = Type::TIME_REGISTER;
      return;
    } catch (const std::invalid_argument&) {
    }
  }

  // Special-purpose registers (spr0-1023 or mnemonic)
  if (text.starts_with("spr")) {
    try {
      this->reg_num = stoul(text.substr(3));
      this->type = Type::SPECIAL_REGISTER;
      return;
    } catch (const std::invalid_argument&) {
    }
  }

  if (text[0] == '[') {
    // Strip off the []
    if (text.size() < 4) {
      throw std::runtime_error("memory reference is too short");
    }
    if (!text.ends_with("]")) {
      throw std::runtime_error("memory reference is not terminated");
    }

    auto expr = Expression::Node::parse(text.substr(1, text.size() - 2));

    // All possible cases:
    //   1. [rN]
    //   2. [upd(rN)] (this is kind of dumb, but we support it anyway)
    //   3. [rN + W] or [W + rN]
    //   4. [rN - W]
    //   5. [rA + rB]
    //   6. [upd(rA) + rB] or [rA + upd(rB)]

    ssize_t reg_num = reg_num_for_node(*expr, "r", 32);
    if (reg_num >= 0) {
      // Case 1: [rN]
      this->reg_num = reg_num;
      this->reg_num2 = 0;
      this->type = Type::IMM_MEMORY_REFERENCE;
      return;
    }

    reg_num = upd_reg_num_for_node(*expr);
    if (reg_num >= 0) {
      // Case 2: [upd(rN)]
      this->reg_num = reg_num;
      this->reg_num2 = 0;
      this->reg_updated = true;
      this->type = Type::IMM_MEMORY_REFERENCE;
      return;
    }

    auto* bin_node = dynamic_cast<Expression::BinaryOperatorNode*>(expr.get());
    if (!bin_node ||
        ((bin_node->type != Expression::BinaryOperatorNode::Type::ADD) &&
            (bin_node->type != Expression::BinaryOperatorNode::Type::SUBTRACT))) {
      throw std::runtime_error("Invalid memory reference expression: root operation must be ADD or SUBTRACT");
    }

    if (bin_node->type == Expression::BinaryOperatorNode::Type::SUBTRACT) {
      // Case 4: [rN - W]
      // There is no reversal of this case: the register cannot be negated, so it must be the minuend.
      ssize_t reg_num = reg_num_for_node(*bin_node->left, "r", 32);
      if (reg_num < 0) {
        throw std::runtime_error("Invalid memory reference expression: minuend is not a register");
      }
      if (!is_const_expression(*bin_node->right)) {
        throw std::runtime_error("Invalid memory reference expression: subtrahend is not a constant expression");
      }
      this->reg_num = reg_num;
      this->reg_num2 = 0;
      this->value_expr = std::make_unique<Expression::UnaryOperatorNode>(
          Expression::UnaryOperatorNode::Type::NEGATIVE, std::move(bin_node->right));
      this->type = Type::IMM_MEMORY_REFERENCE;
      return;
    }

    ssize_t left_reg_num = reg_num_for_node(*bin_node->left, "r", 32);
    ssize_t right_reg_num = reg_num_for_node(*bin_node->right, "r", 32);
    ssize_t left_upd_reg_num = upd_reg_num_for_node(*bin_node->left);
    ssize_t right_upd_reg_num = upd_reg_num_for_node(*bin_node->right);
    ssize_t left_is_const = is_const_expression(*bin_node->left);
    ssize_t right_is_const = is_const_expression(*bin_node->right);

    if ((left_reg_num >= 0) && right_is_const) {
      // Case 3a: [rN + W]
      this->reg_num = left_reg_num;
      this->reg_num2 = 0;
      this->value_expr = std::move(bin_node->right);
      this->type = Type::IMM_MEMORY_REFERENCE;
      return;
    }
    if ((right_reg_num >= 0) && left_is_const) {
      // Case 3b: [W + rN]
      this->reg_num = right_reg_num;
      this->reg_num2 = 0;
      this->value_expr = std::move(bin_node->left);
      this->type = Type::IMM_MEMORY_REFERENCE;
      return;
    }

    if ((right_reg_num >= 0) && (left_reg_num >= 0)) {
      // Case 5: [rA + rB]
      this->reg_num = left_reg_num;
      this->reg_num2 = right_reg_num;
      this->type = Type::REG_MEMORY_REFERENCE;
      return;
    }

    if ((left_upd_reg_num >= 0) && (right_reg_num >= 0)) {
      // Case 6a: [upd(rA) + rB]
      this->reg_num = left_upd_reg_num;
      this->reg_num2 = right_reg_num;
      this->reg_updated = true;
      this->type = Type::REG_MEMORY_REFERENCE;
      return;
    }
    if ((right_upd_reg_num >= 0) && (left_reg_num >= 0)) {
      // Case 6b: [rA + upd(rB)]
      this->reg_num = right_upd_reg_num;
      this->reg_num2 = left_reg_num;
      this->reg_updated = true;
      this->type = Type::REG_MEMORY_REFERENCE;
      return;
    }

    throw std::runtime_error("Invalid memory reference expression");
  }

  // (likely) tag
  if (text == "(likely)") {
    this->type = Type::LIKELY;
    return;
  }

  // Anything else
  try {
    this->value_expr = Expression::Node::parse(text);
    this->type = Type::IMMEDIATE;
    return;
  } catch (const std::invalid_argument&) {
    throw std::runtime_error("Unparseable argument");
  }
}

const std::vector<PPC32Emulator::Assembler::Argument>& PPC32Emulator::Assembler::StreamItem::check_args(
    const std::vector<ArgType>& types) const {
  if (this->args.size() < types.size()) {
    throw std::runtime_error("not enough arguments to opcode");
  }
  if (this->args.size() > types.size()) {
    throw std::runtime_error("too many arguments to opcode");
  }
  for (size_t x = 0; x < types.size(); x++) {
    // Make REG_MEMORY_REFERENCE also match IMM_MEMORY_REFERENCE if there's no value expression (for indexed opcodes)
    if ((this->args[x].type == types[x]) ||
        (this->args[x].type == ArgType::IMM_MEMORY_REFERENCE &&
            !this->args[x].value_expr && types[x] == ArgType::REG_MEMORY_REFERENCE)) {
      continue;
    }
    throw std::runtime_error(std::format("incorrect type for argument {} (expected {}, received {})",
        x, Argument::name_for_type(types[x]), Argument::name_for_type(this->args[x].type)));
  }
  return this->args;
}

void PPC32Emulator::exec_unimplemented(uint32_t op) {
  std::string dasm = this->disassemble_one(this->regs.pc, op);
  throw std::runtime_error(std::format("unimplemented opcode: {:08X} {}", op, dasm));
}

std::string PPC32Emulator::dasm_unimplemented(DisassemblyState&, uint32_t) {
  return "<<unimplemented>>";
}

void PPC32Emulator::exec_invalid(uint32_t) {
  // TODO: this should trigger an interrupt probably
  throw std::runtime_error("invalid opcode");
}

std::string PPC32Emulator::dasm_invalid(DisassemblyState&, uint32_t) {
  return ".invalid";
}

uint32_t PPC32Emulator::Assembler::asm_5reg(
    uint32_t base_opcode, uint8_t r1, uint8_t r2, uint8_t r3, uint8_t r4, uint8_t r5, bool rec) {
  return base_opcode |
      op_set_reg1(r1) |
      op_set_reg2(r2) |
      op_set_reg3(r3) |
      op_set_reg4(r4) |
      op_set_reg5(r5) |
      op_set_rec(rec);
}

void PPC32Emulator::exec_0C_twi(uint32_t op) {
  this->exec_unimplemented(op); // 000011 TTTTT AAAAA IIIIIIIIIIIIIIII
}

std::string PPC32Emulator::dasm_0C_twi(DisassemblyState&, uint32_t op) {
  uint8_t to = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm_ext(op);
  return std::format("twi       {}, r{}, {}", to, ra, imm);
}

uint32_t PPC32Emulator::Assembler::asm_twi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::IMMEDIATE, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return 0x0C000000 |
      op_set_reg1(this->resolve_immediate(a[0])) |
      op_set_reg2(a[1].reg_num) |
      op_set_simm(this->resolve_immediate(a[2]));
}

void PPC32Emulator::exec_1C_mulli(uint32_t op) {
  // 000111 DDDDD AAAAA IIIIIIIIIIIIIIII
  this->regs.r[op_get_reg1(op)].s = this->regs.r[op_get_reg2(op)].s * op_get_imm_ext(op);
}

std::string PPC32Emulator::dasm_1C_mulli(DisassemblyState&, uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm_ext(op);
  return std::format("mulli     r{}, r{}, {}", rd, ra, imm);
}

uint32_t PPC32Emulator::Assembler::asm_mulli(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return 0x1C000000 |
      op_set_reg1(a[0].reg_num) |
      op_set_reg2(a[1].reg_num) |
      op_set_simm(this->resolve_immediate(a[2]));
}

void PPC32Emulator::exec_20_subfic(uint32_t op) {
  // 001000 DDDDD AAAAA IIIIIIIIIIIIIIII
  // rD = ~rA + EXTS(SIMM) + 1  (== SIMM - rA), and set XER[CA] from the carry-out.
  uint8_t rd = op_get_reg1(op), ra = op_get_reg2(op);
  uint64_t sum = (uint64_t)(~this->regs.r[ra].u)
               + (uint64_t)(uint32_t)(int32_t)op_get_imm_ext(op) + 1;
  this->regs.r[rd].u = (uint32_t)sum;
  this->regs.xer.set_ca((sum >> 32) & 1);
}

std::string PPC32Emulator::dasm_20_subfic(DisassemblyState&, uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_imm_ext(op);
  return std::format("subfic    r{}, r{}, {}", rd, ra, imm);
}

uint32_t PPC32Emulator::Assembler::asm_subfic(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return 0x20000000 | op_set_reg1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_simm(this->resolve_immediate(a[2]));
}

void PPC32Emulator::exec_28_cmpli(uint32_t op) {
  // 001010 CCC 0 L AAAAA IIIIIIIIIIIIIIII
  if (op & 0x00600000) {
    throw std::runtime_error("invalid 28 (cmpli) opcode");
  }
  uint8_t a_reg = op_get_reg2(op);
  uint32_t imm = op_get_uimm(op);
  uint8_t crf_num = op_get_crf1(op);
  this->regs.set_crf_int_result(crf_num, this->regs.r[a_reg].u - imm);
}

std::string PPC32Emulator::dasm_28_cmpli(DisassemblyState&, uint32_t op) {
  if (op & 0x00600000) {
    return ".invalid  cmpli";
  }
  uint8_t crf = op_get_crf1(op);
  uint8_t ra = op_get_reg2(op);
  uint16_t imm = op_get_uimm(op);
  if (crf) {
    return std::format("cmplwi    cr{}, r{}, {}", crf, ra, imm);
  } else {
    return std::format("cmplwi    r{}, {}", ra, imm);
  }
}

uint32_t PPC32Emulator::Assembler::asm_cmpli_cmplwi(const StreamItem& si) {
  if (si.args.size() == 3) {
    const auto& a = si.check_args({ArgType::CONDITION_FIELD, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
    return 0x28000000 | op_set_crf1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_uimm(this->resolve_immediate(a[2]));
  } else {
    const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::IMMEDIATE});
    return 0x28000000 | op_set_crf1(0) | op_set_reg2(a[0].reg_num) | op_set_uimm(this->resolve_immediate(a[1]));
  }
}

void PPC32Emulator::exec_2C_cmpi(uint32_t op) {
  // 001011 CCC 0 L AAAAA IIIIIIIIIIIIIIII
  if (op & 0x00600000) {
    throw std::runtime_error("invalid 2C (cmpi) opcode");
  }
  uint8_t a_reg = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  uint8_t crf_num = op_get_crf1(op);
  this->regs.set_crf_int_result(crf_num, this->regs.r[a_reg].s - imm);
}

std::string PPC32Emulator::dasm_2C_cmpi(DisassemblyState&, uint32_t op) {
  if (op & 0x00600000) {
    return ".invalid  cmpi";
  }
  uint8_t crf = op_get_crf1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_simm(op);
  if (crf) {
    return std::format("cmpwi     cr{}, r{}, {}", crf, ra, imm);
  } else {
    return std::format("cmpwi     r{}, {}", ra, imm);
  }
}

uint32_t PPC32Emulator::Assembler::asm_cmpi_cmpwi(const StreamItem& si) {
  if (si.args.size() == 3) {
    const auto& a = si.check_args({ArgType::CONDITION_FIELD, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
    return 0x2C000000 | op_set_crf1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_simm(this->resolve_immediate(a[2]));
  } else {
    const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::IMMEDIATE});
    return 0x2C000000 | op_set_crf1(0) | op_set_reg2(a[0].reg_num) | op_set_simm(this->resolve_immediate(a[1]));
  }
}

void PPC32Emulator::exec_30_34_addic(uint32_t op) {
  // 00110 R DDDDD AAAAA IIIIIIIIIIIIIIII
  uint8_t rd = op_get_reg1(op);
  int32_t a = this->regs.r[op_get_reg2(op)].s;
  int32_t b = op_get_imm_ext(op);
  int32_t r = a + b;
  this->regs.r[rd].s = r;
  // If the operands have opposite signs, the carry bit cannot be set. If the operands have the same sign and the
  // result has the opposite sign, then the carry bit should be set.
  this->regs.xer.set_ca(((a < 0) == (b < 0)) && ((r < 0) != (a < 0)));
  if (op_get_rec4(op)) {
    this->regs.set_crf_int_result(0, this->regs.r[rd].s);
  }
}

std::string PPC32Emulator::dasm_30_34_addic(DisassemblyState&, uint32_t op) {
  bool rec = op_get_rec4(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (imm < 0) {
    return std::format("subic{}    r{}, r{}, {}", rec ? '.' : ' ', rd, ra, -imm);
  } else {
    return std::format("addic{}    r{}, r{}, {}", rec ? '.' : ' ', rd, ra, imm);
  }
}

uint32_t PPC32Emulator::Assembler::asm_addic_subic(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  int64_t value = this->resolve_immediate(a[2]);
  return 0x30000000 |
      op_set_rec4(si.is_rec()) |
      op_set_reg1(a[0].reg_num) |
      op_set_reg2(a[1].reg_num) |
      op_set_simm(si.op_name.starts_with("sub") ? -value : value);
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

std::string PPC32Emulator::dasm_38_addi(DisassemblyState&, uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_simm(op);
  if (ra == 0) {
    return std::format("li        r{}, {}", rd, imm);
  } else if (imm >= 0) {
    return std::format("addi      r{}, r{}, {}", rd, ra, imm);
  } else {
    return std::format("subi      r{}, r{}, {}", rd, ra, -imm);
  }
}

uint32_t PPC32Emulator::Assembler::asm_li_lis(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  bool is_s = si.op_name.ends_with("s");
  int64_t value = this->resolve_immediate(a[1]);
  return 0x38000000 |
      op_set_rec4(is_s) |
      op_set_reg1(a[0].reg_num) |
      op_set_reg2(0) |
      (is_s ? op_set_imm(value) : op_set_simm(value));
}

uint32_t PPC32Emulator::Assembler::asm_addi_subi_addis_subis(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  int64_t value = this->resolve_immediate(a[2]);
  return 0x38000000 |
      op_set_rec4(si.op_name.ends_with("s")) |
      op_set_reg1(a[0].reg_num) |
      op_set_reg2(a[1].reg_num) |
      op_set_simm(si.op_name.starts_with("sub") ? -value : value);
}

void PPC32Emulator::exec_3C_addis(uint32_t op) {
  // 001111 DDDDD AAAAA IIIIIIIIIIIIIIII
  uint8_t a_reg = op_get_reg2(op);
  uint8_t d_reg = op_get_reg1(op);
  if (a_reg == 0) {
    this->regs.r[d_reg].s = op_get_uimm(op) << 16;
  } else {
    this->regs.r[d_reg].s = this->regs.r[a_reg].s + (op_get_uimm(op) << 16);
  }
}

std::string PPC32Emulator::dasm_3C_addis(DisassemblyState&, uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_simm(op);
  if (ra == 0) {
    return std::format("lis       r{}, {}", rd, imm);
  } else if (imm >= 0) {
    return std::format("addis     r{}, r{}, {}", rd, ra, imm);
  } else {
    return std::format("subis     r{}, r{}, {}", rd, ra, -imm);
  }
}

// Note: the assembler handles addis in the same function as addi/subi (above)

void PPC32Emulator::exec_40_bc(uint32_t op) {
  // 010000 OOOOO IIIII DDDDDDDDDDDDDD A L

  // TODO: The manual appears to show that this happens even if the branch isn't taken, so it should be OK to do it
  // first. Is this actually true?
  if (op_get_b_link(op)) {
    this->regs.lr = this->regs.pc + 4;
  }

  auto bo = op_get_bo(op);
  if (!bo.skip_ctr()) {
    this->regs.ctr--;
  }
  bool ctr_ok = bo.skip_ctr() || ((this->regs.ctr == 0) == bo.branch_if_ctr_zero());
  bool cond_ok = bo.skip_condition() || (((this->regs.cr.u >> (31 - op_get_bi(op))) & 1) == bo.branch_condition_value());
  // Note: we subtract 4 here to correct for the fact that we always add 4 after every opcode, even if it overwrote pc
  if (ctr_ok && cond_ok) {
    if (op_get_b_abs(op)) {
      this->regs.pc = (op_get_imm_ext(op) & (~3)) - 4;
    } else {
      this->regs.pc = (this->regs.pc + (op_get_imm_ext(op) & (~3))) - 4;
    }
  }
}

std::string PPC32Emulator::dasm_40_bc(DisassemblyState& s, uint32_t op) {
  BranchBOField bo = op_get_bo(op);
  uint8_t bi = op_get_bi(op);
  bool absolute = op_get_b_abs(op);
  bool link = op_get_b_link(op);
  int32_t offset = op_get_imm_ext(op) & 0xFFFFFFFC;
  uint32_t target_addr = (absolute ? 0 : s.pc) + offset;

  // bc opcodes are less likely to be patched during loading because the offset field is only 14 bits (so the target
  // module would have to be pretty close in memory), but we'll handle them the same as 48 (b) anyway
  if (offset != 0) {
    auto& refs = s.branch_refs[target_addr];
    if (link) {
      refs.call_addrs.emplace(s.pc);
    } else {
      refs.branch_addrs.emplace(s.pc);
    }
  }

  const char* suffix = "";
  if (absolute && link) {
    suffix = "la";
  } else if (absolute) {
    suffix = "a";
  } else if (link) {
    suffix = "l";
  }

  uint16_t as = ((bo.u & 0x1E) << 5) | (bi & 3);
  if (as & 0x0080) {
    as &= 0x03BF;
  }
  if (as & 0x0200) {
    as &= 0x02FF;
  }

  std::string ret = "b";
  const char* mnemonic = mnemonic_for_bc(bo.u, bi);
  if (mnemonic && mnemonic[0]) { // Don't let empty mnemonic (always branch) disassemble as "b" - force "bc" instead
    ret += mnemonic;
    ret += suffix;
    ret.resize(10, ' ');
    if (bo.u & 1) {
      ret += "(likely), ";
    }
    if (bi & 0x1C) {
      ret += std::format("cr{}, ", (bi >> 2) & 7);
    }
  } else {
    ret += 'c';
    ret += suffix;
    ret.resize(10, ' ');
    ret += std::format("{}, {}, ", bo.u, bi);
  }

  if (absolute) {
    ret += std::format("0x{:08X}", target_addr);
  } else if (offset < 0) {
    ret += std::format("-0x{:08X} /* {:08X} */", -offset, target_addr);
  } else {
    ret += std::format("+0x{:08X} /* {:08X} */", offset, target_addr);
  }

  return ret;
}

uint32_t PPC32Emulator::Assembler::compute_branch_target(const Argument& target_arg, uint32_t si_address) const {
  if (target_arg.type == ArgType::IMMEDIATE) {
    int64_t value = this->resolve_immediate(target_arg);
    // If there's a unary operator in the expression, then it should be something like +0x40 or -0x1C; treat this as a
    // destination relative to the current opcode
    if (dynamic_cast<const Expression::UnaryOperatorNode*>(target_arg.value_expr.get())) {
      return si_address + value;
    } else {
      return value;
    }
  } else {
    throw std::logic_error("Invalid branch target type");
  }
}

int64_t PPC32Emulator::Assembler::resolve_immediate(const Argument& arg) const {
  if (!arg.value_expr) {
    if ((arg.type == Argument::Type::IMM_MEMORY_REFERENCE) || (arg.type == Argument::Type::REG_MEMORY_REFERENCE)) {
      return 0;
    } else {
      throw std::logic_error("Cannot resolve missing immediate value");
    }
  }

  if (!this->expr_env_lookup_fn) {
    this->expr_env_lookup_fn = [this](const std::string& name) -> int64_t {
      try {
        return this->label_addresses.at(name);
      } catch (const std::out_of_range&) {
        throw std::runtime_error("Unknown label: " + name);
      }
    };
  }
  if (!this->expr_function_call_fn) {
    this->expr_function_call_fn = [](const std::string& name, const std::vector<Expression::Value>& args) -> Expression::Value {
      if (name == "high_word" && args.size() == 1) {
        return static_cast<int64_t>((args[0].as_int() >> 16) & 0xFFFF);
      } else if (name == "low_word" && args.size() == 1) {
        return static_cast<int64_t>(args[0].as_int() & 0xFFFF);
      } else if (name == "addr_high" && args.size() == 1) {
        int64_t addr = args[0].as_int();
        return ((addr >> 16) + ((addr & 0x8000) ? 1 : 0)) & 0xFFFF;
      } else if (name == "addr_low" && args.size() == 1) {
        int64_t addr = args[0].as_int();
        return (addr & 0xFFFF) - ((addr & 0x8000) ? 0x10000 : 0);
      } else {
        return Expression::FunctionCallNode::default_handler(name, args);
      }
    };
  }

  auto ret = arg.value_expr->evaluate(this->expr_env_lookup_fn, this->expr_function_call_fn);
  if (!ret.is_int()) {
    throw std::runtime_error(std::format("Expression value ({}) is not an integer", ret.str()));
  }
  return ret.as_int();
}

uint32_t PPC32Emulator::Assembler::asm_bc(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::IMMEDIATE, ArgType::IMMEDIATE, ArgType::IMMEDIATE});

  bool absolute = false;
  bool link = false;
  std::string mnemonic = si.op_name.substr(2);
  if (mnemonic.ends_with("a")) {
    absolute = true;
    mnemonic.pop_back();
  }
  if (mnemonic.ends_with("l")) {
    link = true;
    mnemonic.pop_back();
  }

  uint32_t target = this->compute_branch_target(a[2], si.address);
  int32_t delta = absolute ? target : (target - si.address);
  if (delta < -0x8000 || delta > 0x7FFF) {
    throw std::runtime_error("conditional branch distance too long");
  }
  return 0x40000000 |
      op_set_bo(this->resolve_immediate(a[0])) |
      op_set_bi(this->resolve_immediate(a[1])) |
      op_set_simm(delta) |
      op_set_b_abs(absolute) |
      op_set_b_link(link);
}

uint32_t PPC32Emulator::Assembler::asm_bc_mnemonic(const StreamItem& si) {
  bool likely = false;
  uint8_t crf = 0;
  const Argument* target_arg;
  if (si.args.size() == 3) {
    const auto& a = si.check_args({ArgType::LIKELY, ArgType::CONDITION_FIELD, ArgType::IMMEDIATE});
    likely = true;
    crf = a[1].reg_num;
    target_arg = &a[2];
  } else if (si.args.size() == 2) {
    if (si.args[0].type == ArgType::LIKELY) {
      likely = true;
      const auto& a = si.check_args({ArgType::LIKELY, ArgType::IMMEDIATE});
      target_arg = &a[1];
    } else {
      const auto& a = si.check_args({ArgType::CONDITION_FIELD, ArgType::IMMEDIATE});
      crf = a[0].reg_num;
      target_arg = &a[1];
    }
  } else {
    const auto& a = si.check_args({ArgType::IMMEDIATE});
    target_arg = &a[0];
  }

  bool absolute = false;
  bool link = false;
  std::string mnemonic = si.op_name.substr(1);
  if (mnemonic.ends_with("a")) {
    absolute = true;
    mnemonic.pop_back();
  }
  if (mnemonic.ends_with("l")) {
    link = true;
    mnemonic.pop_back();
  }
  auto bc = bc_for_mnemonic(mnemonic);

  uint32_t target = this->compute_branch_target(*target_arg, si.address);
  int32_t delta = absolute ? target : (target - si.address);
  if (delta < -0x8000 || delta > 0x7FFF) {
    throw std::runtime_error("conditional branch distance too long");
  }
  return 0x40000000 |
      op_set_bo(bc.first | (likely ? 1 : 0)) |
      op_set_bi(bc.second + 4 * crf) |
      op_set_simm(delta) |
      op_set_b_abs(absolute) |
      op_set_b_link(link);
}

void PPC32Emulator::exec_44_sc(uint32_t op) {
  // 010001 00000000000000000000000010
  if (this->syscall_handler) {
    this->syscall_handler(*this);
  } else {
    this->exec_unimplemented(op);
  }
}

std::string PPC32Emulator::dasm_44_sc(DisassemblyState&, uint32_t op) {
  return (op == 0x44000002) ? "sc" : ".invalid  sc";
}

uint32_t PPC32Emulator::Assembler::asm_sc(const StreamItem& si) {
  si.check_args({});
  return 0x44000002;
}

void PPC32Emulator::exec_48_b(uint32_t op) {
  // 010010 TTTTTTTTTTTTTTTTTTTTTTTT A L

  if (op_get_b_link(op)) {
    this->regs.lr = this->regs.pc + 4;
  }

  // Note: we subtract 4 here to correct for the fact that we always add 4 after every opcode, even if it overwrote pc
  if (op_get_b_abs(op)) {
    this->regs.pc = op_get_b_target(op) - 4;
  } else {
    this->regs.pc = this->regs.pc + op_get_b_target(op) - 4;
  }
}

std::string PPC32Emulator::dasm_48_b(DisassemblyState& s, uint32_t op) {
  bool absolute = op_get_b_abs(op);
  bool link = op_get_b_link(op);
  int32_t offset = op_get_b_target(op);
  uint32_t target_addr = (absolute ? 0 : s.pc) + offset;
  // If offset == 0, it's probably an unlinked branch (which would be patched by the loader before execution), so don't
  // autocreate a label in that case
  if (offset != 0) {
    auto& refs = s.branch_refs[target_addr];
    if (link) {
      refs.call_addrs.emplace(s.pc);
    } else {
      refs.branch_addrs.emplace(s.pc);
    }
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
    return std::format("b{}       0x{:08X} /* ", suffix, target_addr);
  } else if (offset < 0) {
    return std::format("b{}       -0x{:08X} /* {:08X} */", suffix, -offset, target_addr);
  } else {
    return std::format("b{}       +0x{:08X} /* {:08X} */", suffix, offset, target_addr);
  }
}

uint32_t PPC32Emulator::Assembler::asm_b_mnemonic(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::IMMEDIATE});

  bool absolute = false;
  bool link = false;
  std::string mnemonic = si.op_name.substr(1);
  if (mnemonic.ends_with("a")) {
    absolute = true;
    mnemonic.pop_back();
  }
  if (mnemonic.ends_with("l")) {
    link = true;
    mnemonic.pop_back();
  }
  if (!mnemonic.empty()) {
    throw std::logic_error("invalid suffix on branch instruction");
  }

  uint32_t target = this->compute_branch_target(a[0], si.address);
  int32_t delta = absolute ? target : (target - si.address);
  if (delta < -0x2000000 || delta > 0x1FFFFFF) {
    throw std::runtime_error(std::format(
        "unconditional branch distance too long (address={:08X}, delta={:08X})", si.address, delta));
  }

  return 0x48000000 | op_set_b_target(delta) | op_set_b_abs(absolute) | op_set_b_link(link);
}

void PPC32Emulator::exec_4C(uint32_t op) {
  switch (op_get_subopcode(op)) {
    case 0x000:
      this->exec_4C_000_mcrf(op);
      break;
    case 0x010:
    case 0x210:
      this->exec_4C_010_bclr_4C_210_bcctr(op);
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
    default:
      throw std::runtime_error("invalid 4C subopcode");
  }
}

std::string PPC32Emulator::dasm_4C(DisassemblyState& s, uint32_t op) {
  switch (op_get_subopcode(op)) {
    case 0x000:
      return PPC32Emulator::dasm_4C_000_mcrf(s, op);
    case 0x010:
    case 0x210:
      return PPC32Emulator::dasm_4C_010_bclr_4C_210_bcctr(s, op);
    case 0x021:
      return PPC32Emulator::dasm_4C_021_crnor(s, op);
    case 0x032:
      return PPC32Emulator::dasm_4C_032_rfi(s, op);
    case 0x081:
      return PPC32Emulator::dasm_4C_081_crandc(s, op);
    case 0x096:
      return PPC32Emulator::dasm_4C_096_isync(s, op);
    case 0x0C1:
      return PPC32Emulator::dasm_4C_0C1_crxor(s, op);
    case 0x0E1:
      return PPC32Emulator::dasm_4C_0E1_crnand(s, op);
    case 0x101:
      return PPC32Emulator::dasm_4C_101_crand(s, op);
    case 0x121:
      return PPC32Emulator::dasm_4C_121_creqv(s, op);
    case 0x1A1:
      return PPC32Emulator::dasm_4C_1A1_crorc(s, op);
    case 0x1C1:
      return PPC32Emulator::dasm_4C_1C1_cror(s, op);
    default:
      return ".invalid  4C";
  }
}

// CR-bit helpers: PPC numbers CR bits 0..31 with 0 = MSB, so bit N lives at
// (cr.u >> (31 - N)) & 1 (matches the bclr condition eval elsewhere).
static inline bool cr_get_bit(uint32_t cr, uint8_t n) {
  return (cr >> (31 - n)) & 1;
}
static inline uint32_t cr_set_bit(uint32_t cr, uint8_t n, bool v) {
  uint32_t mask = 1u << (31 - n);
  return v ? (cr | mask) : (cr & ~mask);
}

void PPC32Emulator::exec_4C_000_mcrf(uint32_t op) {
  // mcrf crfD, crfS: copy 4-bit CR field S into field D
  uint8_t d = op_get_crf1(op);
  uint8_t s = op_get_crf2(op);
  uint8_t val = (this->regs.cr.u >> (28 - (s << 2))) & 0xF;
  this->regs.cr.replace_field(d, val);
}

std::string PPC32Emulator::dasm_4C_000_mcrf(DisassemblyState&, uint32_t op) {
  if (op & 0x0063F801) {
    return std::format(".invalid  << mcrf cr{}, cr{} with reserved bits set >>", op_get_crf1(op), op_get_crf2(op));
  } else {
    return std::format("mcrf      cr{}, cr{}", op_get_crf1(op), op_get_crf2(op));
  }
}

uint32_t PPC32Emulator::Assembler::asm_mcrf(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_FIELD, ArgType::CONDITION_FIELD});
  return 0x4C000000 | op_set_crf1(a[0].reg_num) | op_set_crf2(a[1].reg_num);
}

void PPC32Emulator::exec_4C_010_bclr_4C_210_bcctr(uint32_t op) {
  // 010011 OOOOO IIIII 00000 0000010000 L
  // 010011 OOOOO IIIII 00000 1000010000 L
  bool is_ctr = (op & 0x00000400);
  if (op_get_b_link(op)) {
    if (is_ctr) {
      this->regs.ctr = this->regs.pc + 4;
    } else {
      this->regs.lr = this->regs.pc + 4;
    }
  }
  if (this->should_branch(op)) {
    this->regs.pc = ((is_ctr ? this->regs.ctr : this->regs.lr) & 0xFFFFFFFC) - 4;
  }
}

std::string PPC32Emulator::dasm_4C_010_bclr_4C_210_bcctr(DisassemblyState&, uint32_t op) {
  BranchBOField bo = op_get_bo(op);
  uint8_t bi = op_get_bi(op);
  bool l = op_get_b_link(op);
  const char* reg_name = (op & 0x00000400) ? "ctr" : "lr";

  if (op & 0x0000F800) {
    return std::format(".invalid  << bc{}{} {}, {} with reserved bits set >>", reg_name, l ? 'l' : ' ', bo.u, bi);
  }

  const char* mnemonic = mnemonic_for_bc(bo.u, bi);
  std::string ret = "b";
  if (mnemonic) {
    ret += mnemonic;
    ret += reg_name;
    if (l) {
      ret += 'l';
    }
    ret.resize(10, ' ');
    if (bo.u & 1) {
      ret += "(likely)";
    }
    if (bi & 0x1C) {
      ret += std::format("{}cr{}", (bo.u & 1) ? ", " : "", (bi >> 2) & 7);
    }
    phosg::strip_trailing_whitespace(ret);
  } else {
    ret = std::format("bc{}{}     {}, {}", reg_name, l ? 'l' : ' ', bo.u, bi);
  }
  return ret;
}

uint32_t PPC32Emulator::Assembler::asm_bclr_bcctr(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::IMMEDIATE, ArgType::IMMEDIATE});

  bool link = false;
  std::string mnemonic = si.op_name.substr(1);
  if (mnemonic.ends_with("l")) {
    link = true;
    mnemonic.pop_back();
  }
  bool is_ctr = mnemonic.ends_with("ctr");
  if (!is_ctr && !mnemonic.ends_with("lr")) {
    throw std::logic_error("bclr/bcctr assembler called for incorrect instruction");
  }

  return 0x4C000020 |
      (is_ctr ? 0x00000400 : 0) |
      op_set_bo(this->resolve_immediate(a[0])) |
      op_set_bi(this->resolve_immediate(a[1])) |
      op_set_b_link(link);
}

uint32_t PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic(const StreamItem& si) {
  uint8_t crf = 0;
  bool likely = false;
  if (si.args.size() == 2) {
    const auto& a = si.check_args({ArgType::LIKELY, ArgType::CONDITION_FIELD});
    likely = true;
    crf = a[1].reg_num;
  } else if (si.args.size() == 1) {
    if (si.args[0].type == ArgType::LIKELY) {
      likely = true;
    } else {
      const auto& a = si.check_args({ArgType::CONDITION_FIELD});
      crf = a[0].reg_num;
    }
  } else {
    si.check_args({});
  }

  bool link = false;
  std::string mnemonic = si.op_name.substr(1);
  if (mnemonic.ends_with("l")) {
    link = true;
    mnemonic.pop_back();
  }
  bool is_ctr = mnemonic.ends_with("ctr");
  if (!is_ctr && !mnemonic.ends_with("lr")) {
    throw std::logic_error("bclr/bcctr mnemonic assembler called for incorrect instruction");
  }
  mnemonic.resize(mnemonic.size() - (is_ctr ? 3 : 2));
  auto bc = bc_for_mnemonic(mnemonic);

  return 0x4C000020 |
      (is_ctr ? 0x00000400 : 0) |
      op_set_bo(bc.first | (likely ? 1 : 0)) |
      op_set_bi(bc.second + 4 * crf) |
      op_set_b_link(link);
}

void PPC32Emulator::exec_4C_021_crnor(uint32_t op) {
  // crbD = f(crbA, crbB)
  bool a = cr_get_bit(this->regs.cr.u, op_get_reg2(op));
  bool b = cr_get_bit(this->regs.cr.u, op_get_reg3(op));
  this->regs.cr.u = cr_set_bit(this->regs.cr.u, op_get_reg1(op), !(a || b));
}

std::string PPC32Emulator::dasm_4C_021_crnor(DisassemblyState&, uint32_t op) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  if (op & 1) {
    return std::format(".invalid  << crnor crb{}, crb{}, crb{} with reserved bit set >>", d, a, b);
  } else {
    return std::format("crnor     crb{}, crb{}, crb{}", d, a, b);
  }
}

uint32_t PPC32Emulator::Assembler::asm_crnor(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_BIT, ArgType::CONDITION_BIT, ArgType::CONDITION_BIT});
  return 0x4C000042 | op_set_reg1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_reg3(a[2].reg_num);
}

void PPC32Emulator::exec_4C_032_rfi(uint32_t op) {
  this->exec_unimplemented(op); // 010011 00000 00000 00000 0000110010 0
}

std::string PPC32Emulator::dasm_4C_032_rfi(DisassemblyState&, uint32_t op) {
  return (op == 0x4C000064) ? "rfi" : ".invalid  rfi";
}

uint32_t PPC32Emulator::Assembler::asm_rfi(const StreamItem& si) {
  si.check_args({});
  return 0x4C000064;
}

void PPC32Emulator::exec_4C_081_crandc(uint32_t op) {
  // crbD = f(crbA, crbB)
  bool a = cr_get_bit(this->regs.cr.u, op_get_reg2(op));
  bool b = cr_get_bit(this->regs.cr.u, op_get_reg3(op));
  this->regs.cr.u = cr_set_bit(this->regs.cr.u, op_get_reg1(op), (a && !b));
}

std::string PPC32Emulator::dasm_4C_081_crandc(DisassemblyState&, uint32_t op) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  if (op & 1) {
    return std::format(".invalid  << crandc crb{}, crb{}, crb{} with reserved bit set >>", d, a, b);
  } else {
    return std::format("crandc    crb{}, crb{}, crb{}", d, a, b);
  }
}

uint32_t PPC32Emulator::Assembler::asm_crandc(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_BIT, ArgType::CONDITION_BIT, ArgType::CONDITION_BIT});
  return 0x4C000102 | op_set_reg1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_reg3(a[2].reg_num);
}

void PPC32Emulator::exec_4C_096_isync(uint32_t) {
  // 010011 00000 00000 00000 0010010110 0
  // We don't emulate pipelining or a multiprocessor environment, so we simply ignore this opcode.
}

std::string PPC32Emulator::dasm_4C_096_isync(DisassemblyState&, uint32_t op) {
  return (op == 0x4C00012C) ? "isync" : ".invalid  isync";
}

uint32_t PPC32Emulator::Assembler::asm_isync(const StreamItem& si) {
  si.check_args({});
  return 0x4C00012C;
}

void PPC32Emulator::exec_4C_0C1_crxor(uint32_t op) {
  // crbD = f(crbA, crbB)
  bool a = cr_get_bit(this->regs.cr.u, op_get_reg2(op));
  bool b = cr_get_bit(this->regs.cr.u, op_get_reg3(op));
  this->regs.cr.u = cr_set_bit(this->regs.cr.u, op_get_reg1(op), (a ^ b));
}

std::string PPC32Emulator::dasm_4C_0C1_crxor(DisassemblyState&, uint32_t op) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  if (op & 1) {
    return std::format(".invalid  << crxor crb{}, crb{}, crb{} with reserved bit set >>", d, a, b);
  } else {
    return std::format("crxor     crb{}, crb{}, crb{}", d, a, b);
  }
}

uint32_t PPC32Emulator::Assembler::asm_crxor(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_BIT, ArgType::CONDITION_BIT, ArgType::CONDITION_BIT});
  return 0x4C000182 | op_set_reg1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_reg3(a[2].reg_num);
}

void PPC32Emulator::exec_4C_0E1_crnand(uint32_t op) {
  // crbD = f(crbA, crbB)
  bool a = cr_get_bit(this->regs.cr.u, op_get_reg2(op));
  bool b = cr_get_bit(this->regs.cr.u, op_get_reg3(op));
  this->regs.cr.u = cr_set_bit(this->regs.cr.u, op_get_reg1(op), !(a && b));
}

std::string PPC32Emulator::dasm_4C_0E1_crnand(DisassemblyState&, uint32_t op) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  if (op & 1) {
    return std::format(".invalid  << crnand crb{}, crb{}, crb{} with reserved bit set >>", d, a, b);
  } else {
    return std::format("crnand    crb{}, crb{}, crb{}", d, a, b);
  }
}

uint32_t PPC32Emulator::Assembler::asm_crnand(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_BIT, ArgType::CONDITION_BIT, ArgType::CONDITION_BIT});
  return 0x4C0001C2 | op_set_reg1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_reg3(a[2].reg_num);
}

void PPC32Emulator::exec_4C_101_crand(uint32_t op) {
  // crbD = f(crbA, crbB)
  bool a = cr_get_bit(this->regs.cr.u, op_get_reg2(op));
  bool b = cr_get_bit(this->regs.cr.u, op_get_reg3(op));
  this->regs.cr.u = cr_set_bit(this->regs.cr.u, op_get_reg1(op), (a && b));
}

std::string PPC32Emulator::dasm_4C_101_crand(DisassemblyState&, uint32_t op) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  if (op & 1) {
    return std::format(".invalid  << crand crb{}, crb{}, crb{} with reserved bit set >>", d, a, b);
  } else {
    return std::format("crand     crb{}, crb{}, crb{}", d, a, b);
  }
}

uint32_t PPC32Emulator::Assembler::asm_crand(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_BIT, ArgType::CONDITION_BIT, ArgType::CONDITION_BIT});
  return 0x4C000202 | op_set_reg1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_reg3(a[2].reg_num);
}

void PPC32Emulator::exec_4C_121_creqv(uint32_t op) {
  // crbD = f(crbA, crbB)
  bool a = cr_get_bit(this->regs.cr.u, op_get_reg2(op));
  bool b = cr_get_bit(this->regs.cr.u, op_get_reg3(op));
  this->regs.cr.u = cr_set_bit(this->regs.cr.u, op_get_reg1(op), (a == b));
}

std::string PPC32Emulator::dasm_4C_121_creqv(DisassemblyState&, uint32_t op) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  if (op & 1) {
    return std::format(".invalid  << creqv crb{}, crb{}, crb{} with reserved bit set >>", d, a, b);
  } else {
    return std::format("creqv     crb{}, crb{}, crb{}", d, a, b);
  }
}

uint32_t PPC32Emulator::Assembler::asm_creqv(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_BIT, ArgType::CONDITION_BIT, ArgType::CONDITION_BIT});
  return 0x4C000242 | op_set_reg1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_reg3(a[2].reg_num);
}

void PPC32Emulator::exec_4C_1A1_crorc(uint32_t op) {
  // crbD = f(crbA, crbB)
  bool a = cr_get_bit(this->regs.cr.u, op_get_reg2(op));
  bool b = cr_get_bit(this->regs.cr.u, op_get_reg3(op));
  this->regs.cr.u = cr_set_bit(this->regs.cr.u, op_get_reg1(op), (a || !b));
}

std::string PPC32Emulator::dasm_4C_1A1_crorc(DisassemblyState&, uint32_t op) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  if (op & 1) {
    return std::format(".invalid  << crorc crb{}, crb{}, crb{} with reserved bit set >>", d, a, b);
  } else {
    return std::format("crorc     crb{}, crb{}, crb{}", d, a, b);
  }
}

uint32_t PPC32Emulator::Assembler::asm_crorc(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_BIT, ArgType::CONDITION_BIT, ArgType::CONDITION_BIT});
  return 0x4C000342 | op_set_reg1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_reg3(a[2].reg_num);
}

void PPC32Emulator::exec_4C_1C1_cror(uint32_t op) {
  // crbD = f(crbA, crbB)
  bool a = cr_get_bit(this->regs.cr.u, op_get_reg2(op));
  bool b = cr_get_bit(this->regs.cr.u, op_get_reg3(op));
  this->regs.cr.u = cr_set_bit(this->regs.cr.u, op_get_reg1(op), (a || b));
}

std::string PPC32Emulator::dasm_4C_1C1_cror(DisassemblyState&, uint32_t op) {
  uint8_t d = op_get_reg1(op);
  uint8_t a = op_get_reg2(op);
  uint8_t b = op_get_reg3(op);
  if (op & 1) {
    return std::format(".invalid  << cror crb{}, crb{}, crb{} with reserved bit set >>", d, a, b);
  } else {
    return std::format("cror      crb{}, crb{}, crb{}", d, a, b);
  }
}

uint32_t PPC32Emulator::Assembler::asm_cror(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_BIT, ArgType::CONDITION_BIT, ArgType::CONDITION_BIT});
  return 0x4C000382 | op_set_reg1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_reg3(a[2].reg_num);
}

void PPC32Emulator::exec_50_54_rlwimi_rlwinm(uint32_t op) {
  // 01010Z SSSSS AAAAA <<<<< MMMMM NNNNN R (same as rlwinm)
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t sh = op_get_reg3(op);
  uint8_t ms = op_get_reg4(op);
  uint8_t me = op_get_reg5(op);
  bool rec = op_get_rec(op);

  uint32_t v = (this->regs.r[rs].u << sh) | (this->regs.r[rs].u >> (32 - sh));
  uint32_t mask = (0xFFFFFFFF >> ms) & (0xFFFFFFFF << (31 - me));
  if (op & 0x04000000) { // rlwinm
    this->regs.r[ra].u = v & mask;
  } else { // rlwimi
    this->regs.r[ra].u = (this->regs.r[ra].u & ~mask) | (v & mask);
  }
  if (rec) {
    this->regs.set_crf_int_result(0, this->regs.r[ra].s);
  }
}

std::string PPC32Emulator::dasm_50_rlwimi(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t sh = op_get_reg3(op);
  uint8_t ms = op_get_reg4(op);
  uint8_t me = op_get_reg5(op);
  bool rec = op_get_rec(op);
  return std::format("rlwimi{}   r{}, r{}, {}, {}, {}", (rec ? '.' : ' '), ra, rs, sh, ms, me);
}

uint32_t PPC32Emulator::Assembler::asm_rlwimi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE, ArgType::IMMEDIATE, ArgType::IMMEDIATE});
  return this->asm_5reg(
      0x50000000,
      a[1].reg_num,
      a[0].reg_num,
      this->resolve_immediate(a[2]),
      this->resolve_immediate(a[3]),
      this->resolve_immediate(a[4]),
      si.is_rec());
}

uint32_t PPC32Emulator::Assembler::asm_inslwi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE, ArgType::IMMEDIATE});
  int64_t a3val = this->resolve_immediate(a[3]);
  return this->asm_5reg(
      0x50000000, a[1].reg_num, a[0].reg_num, 32 - a3val, a3val, this->resolve_immediate(a[2]) + a3val - 1, si.is_rec());
}

uint32_t PPC32Emulator::Assembler::asm_insrwi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE, ArgType::IMMEDIATE});
  int64_t a2val = this->resolve_immediate(a[2]);
  int64_t a3val = this->resolve_immediate(a[3]);
  return this->asm_5reg(
      0x50000000, a[1].reg_num, a[0].reg_num, 32 - (a2val + a3val), a3val, a2val + a3val - 1, si.is_rec());
}

std::string PPC32Emulator::dasm_54_rlwinm(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t sh = op_get_reg3(op);
  uint8_t ms = op_get_reg4(op);
  uint8_t me = op_get_reg5(op);
  bool rec = op_get_rec(op);
  return std::format("rlwinm{}   r{}, r{}, {}, {}, {}", rec ? '.' : ' ', ra, rs, sh, ms, me);
}

uint32_t PPC32Emulator::Assembler::asm_rlwinm(const StreamItem& si) {
  const auto& a = si.check_args(
      {ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE, ArgType::IMMEDIATE, ArgType::IMMEDIATE});
  return asm_5reg(
      0x54000000,
      a[1].reg_num,
      a[0].reg_num,
      this->resolve_immediate(a[2]),
      this->resolve_immediate(a[3]),
      this->resolve_immediate(a[4]),
      si.is_rec());
}

uint32_t PPC32Emulator::Assembler::asm_extlwi(const StreamItem& si) {
  const auto& a = si.check_args(
      {ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE, ArgType::IMMEDIATE});
  return asm_5reg(
      0x54000000, a[1].reg_num, a[0].reg_num, this->resolve_immediate(a[3]), 0, this->resolve_immediate(a[2]) - 1, si.is_rec());
}

uint32_t PPC32Emulator::Assembler::asm_extrwi(const StreamItem& si) {
  const auto& a = si.check_args(
      {ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE, ArgType::IMMEDIATE});
  int64_t a2val = this->resolve_immediate(a[2]);
  return asm_5reg(
      0x54000000, a[1].reg_num, a[0].reg_num, a2val + this->resolve_immediate(a[3]), 32 - a2val, 31, si.is_rec());
}

uint32_t PPC32Emulator::Assembler::asm_rotlwi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return asm_5reg(0x54000000, a[1].reg_num, a[0].reg_num, this->resolve_immediate(a[2]), 0, 31, si.is_rec());
}

uint32_t PPC32Emulator::Assembler::asm_rotrwi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return asm_5reg(0x54000000, a[1].reg_num, a[0].reg_num, 32 - this->resolve_immediate(a[2]), 0, 31, si.is_rec());
}

uint32_t PPC32Emulator::Assembler::asm_slwi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  int64_t a2val = this->resolve_immediate(a[2]);
  return asm_5reg(0x54000000, a[1].reg_num, a[0].reg_num, a2val, 0, 31 - a2val, si.is_rec());
}

uint32_t PPC32Emulator::Assembler::asm_srwi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  int64_t a2val = this->resolve_immediate(a[2]);
  return asm_5reg(0x54000000, a[1].reg_num, a[0].reg_num, 32 - a2val, a2val, 31, si.is_rec());
}

uint32_t PPC32Emulator::Assembler::asm_clrlwi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return asm_5reg(0x54000000, a[1].reg_num, a[0].reg_num, 0, this->resolve_immediate(a[2]), 31, si.is_rec());
}

uint32_t PPC32Emulator::Assembler::asm_clrrwi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return asm_5reg(0x54000000, a[1].reg_num, a[0].reg_num, 0, 0, 31 - this->resolve_immediate(a[2]), si.is_rec());
}

uint32_t PPC32Emulator::Assembler::asm_clrlslwi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE, ArgType::IMMEDIATE});
  int64_t a3val = this->resolve_immediate(a[3]);
  return asm_5reg(
      0x54000000, a[1].reg_num, a[0].reg_num, a3val, this->resolve_immediate(a[2]) - a3val, 31 - a3val, si.is_rec());
}

void PPC32Emulator::exec_5C_rlwnm(uint32_t op) {
  this->exec_unimplemented(op); // 010111 SSSSS AAAAA BBBBB MMMMM NNNNN R
}

std::string PPC32Emulator::dasm_5C_rlwnm(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  uint8_t ms = op_get_reg4(op);
  uint8_t me = op_get_reg5(op);
  bool rec = op_get_rec(op);
  return std::format("rlwnm{}    r{}, r{}, r{}, {}, {}", (rec ? '.' : ' '), ra, rs, rb, ms, me);
}

uint32_t PPC32Emulator::Assembler::asm_rlwnm(const StreamItem& si) {
  const auto& a = si.check_args(
      {ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE, ArgType::IMMEDIATE});
  return this->asm_5reg(
      0x5C000000,
      a[1].reg_num,
      a[0].reg_num,
      a[2].reg_num,
      this->resolve_immediate(a[3]),
      this->resolve_immediate(a[4]),
      si.is_rec());
}

uint32_t PPC32Emulator::Assembler::asm_rotlw(const StreamItem& si) {
  const auto& a = si.check_args(
      {ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE, ArgType::IMMEDIATE});
  return this->asm_5reg(0x5C000000, a[1].reg_num, a[0].reg_num, a[2].reg_num, 0, 31, si.is_rec());
}

void PPC32Emulator::exec_60_ori(uint32_t op) {
  // 011000 SSSSS AAAAA IIIIIIIIIIIIIIII
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint16_t imm = op_get_uimm(op);
  this->regs.r[ra].u = this->regs.r[rs].u | imm;
}

std::string PPC32Emulator::dasm_60_ori(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint16_t imm = op_get_uimm(op);
  if (imm != 0 || rs != ra) {
    return std::format("ori       r{}, r{}, 0x{:04X}", ra, rs, imm);
  } else if (rs != 0) {
    return std::format("nop       r{}", rs);
  } else {
    return "nop";
  }
}

uint32_t PPC32Emulator::Assembler::asm_nop(const StreamItem& si) {
  if (si.args.size() == 1) {
    const auto& a = si.check_args({ArgType::INT_REGISTER});
    return 0x60000000 | op_set_reg1(a[0].reg_num) | op_set_reg2(a[0].reg_num);
  } else {
    si.check_args({});
    return 0x60000000;
  }
}

uint32_t PPC32Emulator::Assembler::asm_ori(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return 0x60000000 | op_set_reg2(a[0].reg_num) | op_set_reg1(a[1].reg_num) | op_set_uimm(this->resolve_immediate(a[2]));
}

void PPC32Emulator::exec_64_oris(uint32_t op) {
  // 011001 SSSSS AAAAA IIIIIIIIIIIIIIII
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint32_t imm = op_get_uimm(op);
  this->regs.r[ra].u = this->regs.r[rs].u | (imm << 16);
}

std::string PPC32Emulator::dasm_64_oris(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint16_t imm = op_get_uimm(op);
  return std::format("oris      r{}, r{}, 0x{:04X}", ra, rs, imm);
}

uint32_t PPC32Emulator::Assembler::asm_oris(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return 0x64000000 | op_set_reg2(a[0].reg_num) | op_set_reg1(a[1].reg_num) | op_set_uimm(this->resolve_immediate(a[2]));
}

void PPC32Emulator::exec_68_xori(uint32_t op) {
  // 011010 SSSSS AAAAA IIIIIIIIIIIIIIII
  this->regs.r[op_get_reg2(op)].u = this->regs.r[op_get_reg1(op)].u ^ op_get_uimm(op);
}

std::string PPC32Emulator::dasm_68_xori(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint16_t imm = op_get_uimm(op);
  return std::format("xori      r{}, r{}, 0x{:04X}", ra, rs, imm);
}

uint32_t PPC32Emulator::Assembler::asm_xori(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return 0x68000000 | op_set_reg2(a[0].reg_num) | op_set_reg1(a[1].reg_num) | op_set_uimm(this->resolve_immediate(a[2]));
}

void PPC32Emulator::exec_6C_xoris(uint32_t op) {
  // 011011 SSSSS AAAAA IIIIIIIIIIIIIIII
  this->regs.r[op_get_reg2(op)].u = this->regs.r[op_get_reg1(op)].u ^ (((uint32_t)op_get_uimm(op)) << 16);
}

std::string PPC32Emulator::dasm_6C_xoris(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint16_t imm = op_get_uimm(op);
  return std::format("xoris     r{}, r{}, 0x{:04X}", ra, rs, imm);
}

uint32_t PPC32Emulator::Assembler::asm_xoris(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return 0x6C000000 | op_set_reg2(a[0].reg_num) | op_set_reg1(a[1].reg_num) | op_set_uimm(this->resolve_immediate(a[2]));
}

void PPC32Emulator::exec_70_andi_rec(uint32_t op) {
  this->exec_unimplemented(op); // 011100 SSSSS AAAAA IIIIIIIIIIIIIIII
}

std::string PPC32Emulator::dasm_70_andi_rec(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint16_t imm = op_get_uimm(op);
  return std::format("andi.     r{}, r{}, 0x{:04X}", ra, rs, imm);
}

uint32_t PPC32Emulator::Assembler::asm_andi_rec(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return 0x70000000 | op_set_reg2(a[0].reg_num) | op_set_reg1(a[1].reg_num) | op_set_uimm(this->resolve_immediate(a[2]));
}

void PPC32Emulator::exec_74_andis_rec(uint32_t op) {
  this->exec_unimplemented(op); // 011101 SSSSS AAAAA IIIIIIIIIIIIIIII
}

std::string PPC32Emulator::dasm_74_andis_rec(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint16_t imm = op_get_uimm(op);
  return std::format("andis.    r{}, r{}, 0x{:04X}", ra, rs, imm);
}

uint32_t PPC32Emulator::Assembler::asm_andis_rec(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return 0x74000000 | op_set_reg2(a[0].reg_num) | op_set_reg1(a[1].reg_num) | op_set_uimm(this->resolve_immediate(a[2]));
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
    case 0x2D7:
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
      throw std::runtime_error("invalid 7C subopcode");
  }
}

std::string PPC32Emulator::dasm_7C(DisassemblyState& s, uint32_t op) {
  switch (op_get_subopcode(op)) {
    case 0x000:
      return PPC32Emulator::dasm_7C_000_cmp(s, op);
    case 0x004:
      return PPC32Emulator::dasm_7C_004_tw(s, op);
    case 0x008:
      return PPC32Emulator::dasm_7C_008_208_subfc(s, op);
    case 0x00A:
      return PPC32Emulator::dasm_7C_00A_20A_addc(s, op);
    case 0x00B:
      return PPC32Emulator::dasm_7C_00B_mulhwu(s, op);
    case 0x013:
      return PPC32Emulator::dasm_7C_013_mfcr(s, op);
    case 0x014:
      return PPC32Emulator::dasm_7C_014_lwarx(s, op);
    case 0x017:
      return PPC32Emulator::dasm_7C_017_lwzx(s, op);
    case 0x018:
      return PPC32Emulator::dasm_7C_018_slw(s, op);
    case 0x01A:
      return PPC32Emulator::dasm_7C_01A_cntlzw(s, op);
    case 0x01C:
      return PPC32Emulator::dasm_7C_01C_and(s, op);
    case 0x020:
      return PPC32Emulator::dasm_7C_020_cmpl(s, op);
    case 0x028:
      return PPC32Emulator::dasm_7C_028_228_subf(s, op);
    case 0x036:
      return PPC32Emulator::dasm_7C_036_dcbst(s, op);
    case 0x037:
      return PPC32Emulator::dasm_7C_037_lwzux(s, op);
    case 0x03C:
      return PPC32Emulator::dasm_7C_03C_andc(s, op);
    case 0x04B:
      return PPC32Emulator::dasm_7C_04B_mulhw(s, op);
    case 0x053:
      return PPC32Emulator::dasm_7C_053_mfmsr(s, op);
    case 0x056:
      return PPC32Emulator::dasm_7C_056_dcbf(s, op);
    case 0x057:
      return PPC32Emulator::dasm_7C_057_lbzx(s, op);
    case 0x068:
    case 0x268:
      return PPC32Emulator::dasm_7C_068_268_neg(s, op);
    case 0x077:
      return PPC32Emulator::dasm_7C_077_lbzux(s, op);
    case 0x07C:
      return PPC32Emulator::dasm_7C_07C_nor(s, op);
    case 0x088:
    case 0x288:
      return PPC32Emulator::dasm_7C_088_288_subfe(s, op);
    case 0x08A:
    case 0x28A:
      return PPC32Emulator::dasm_7C_08A_28A_adde(s, op);
    case 0x090:
      return PPC32Emulator::dasm_7C_090_mtcrf(s, op);
    case 0x092:
      return PPC32Emulator::dasm_7C_092_mtmsr(s, op);
    case 0x096:
      return PPC32Emulator::dasm_7C_096_stwcx_rec(s, op);
    case 0x097:
      return PPC32Emulator::dasm_7C_097_stwx(s, op);
    case 0x0B7:
      return PPC32Emulator::dasm_7C_0B7_stwux(s, op);
    case 0x0C8:
    case 0x2C8:
      return PPC32Emulator::dasm_7C_0C8_2C8_subfze(s, op);
    case 0x0CA:
    case 0x2CA:
      return PPC32Emulator::dasm_7C_0CA_2CA_addze(s, op);
    case 0x0D2:
      return PPC32Emulator::dasm_7C_0D2_mtsr(s, op);
    case 0x0D7:
      return PPC32Emulator::dasm_7C_0D7_stbx(s, op);
    case 0x0E8:
    case 0x2E8:
      return PPC32Emulator::dasm_7C_0E8_2E8_subfme(s, op);
    case 0x0EA:
    case 0x2EA:
      return PPC32Emulator::dasm_7C_0EA_2EA_addme(s, op);
    case 0x0EB:
    case 0x2EB:
      return PPC32Emulator::dasm_7C_0EB_2EB_mullw(s, op);
    case 0x0F2:
      return PPC32Emulator::dasm_7C_0F2_mtsrin(s, op);
    case 0x0F6:
      return PPC32Emulator::dasm_7C_0F6_dcbtst(s, op);
    case 0x0F7:
      return PPC32Emulator::dasm_7C_0F7_stbux(s, op);
    case 0x10A:
    case 0x30A:
      return PPC32Emulator::dasm_7C_10A_30A_add(s, op);
    case 0x116:
      return PPC32Emulator::dasm_7C_116_dcbt(s, op);
    case 0x117:
      return PPC32Emulator::dasm_7C_117_lhzx(s, op);
    case 0x11C:
      return PPC32Emulator::dasm_7C_11C_eqv(s, op);
    case 0x132:
      return PPC32Emulator::dasm_7C_132_tlbie(s, op);
    case 0x136:
      return PPC32Emulator::dasm_7C_136_eciwx(s, op);
    case 0x137:
      return PPC32Emulator::dasm_7C_137_lhzux(s, op);
    case 0x13C:
      return PPC32Emulator::dasm_7C_13C_xor(s, op);
    case 0x153:
      return PPC32Emulator::dasm_7C_153_mfspr(s, op);
    case 0x157:
      return PPC32Emulator::dasm_7C_157_lhax(s, op);
    case 0x172:
      return PPC32Emulator::dasm_7C_172_tlbia(s, op);
    case 0x173:
      return PPC32Emulator::dasm_7C_173_mftb(s, op);
    case 0x177:
      return PPC32Emulator::dasm_7C_177_lhaux(s, op);
    case 0x197:
      return PPC32Emulator::dasm_7C_197_sthx(s, op);
    case 0x19C:
      return PPC32Emulator::dasm_7C_19C_orc(s, op);
    case 0x1B6:
      return PPC32Emulator::dasm_7C_1B6_ecowx(s, op);
    case 0x1B7:
      return PPC32Emulator::dasm_7C_1B7_sthux(s, op);
    case 0x1BC:
      return PPC32Emulator::dasm_7C_1BC_or(s, op);
    case 0x1CB:
    case 0x3CB:
      return PPC32Emulator::dasm_7C_1CB_3CB_divwu(s, op);
    case 0x1D3:
      return PPC32Emulator::dasm_7C_1D3_mtspr(s, op);
    case 0x1D6:
      return PPC32Emulator::dasm_7C_1D6_dcbi(s, op);
    case 0x1DC:
      return PPC32Emulator::dasm_7C_1DC_nand(s, op);
    case 0x1EB:
    case 0x3EB:
      return PPC32Emulator::dasm_7C_1EB_3EB_divw(s, op);
    case 0x200:
      return PPC32Emulator::dasm_7C_200_mcrxr(s, op);
    case 0x215:
      return PPC32Emulator::dasm_7C_215_lswx(s, op);
    case 0x216:
      return PPC32Emulator::dasm_7C_216_lwbrx(s, op);
    case 0x217:
      return PPC32Emulator::dasm_7C_217_lfsx(s, op);
    case 0x218:
      return PPC32Emulator::dasm_7C_218_srw(s, op);
    case 0x236:
      return PPC32Emulator::dasm_7C_236_tlbsync(s, op);
    case 0x237:
      return PPC32Emulator::dasm_7C_237_lfsux(s, op);
    case 0x253:
      return PPC32Emulator::dasm_7C_253_mfsr(s, op);
    case 0x255:
      return PPC32Emulator::dasm_7C_255_lswi(s, op);
    case 0x256:
      return PPC32Emulator::dasm_7C_256_sync(s, op);
    case 0x257:
      return PPC32Emulator::dasm_7C_257_lfdx(s, op);
    case 0x277:
      return PPC32Emulator::dasm_7C_277_lfdux(s, op);
    case 0x293:
      return PPC32Emulator::dasm_7C_293_mfsrin(s, op);
    case 0x295:
      return PPC32Emulator::dasm_7C_295_stswx(s, op);
    case 0x296:
      return PPC32Emulator::dasm_7C_296_stwbrx(s, op);
    case 0x297:
      return PPC32Emulator::dasm_7C_297_stfsx(s, op);
    case 0x2B7:
      return PPC32Emulator::dasm_7C_2B7_stfsux(s, op);
    case 0x2D5:
      return PPC32Emulator::dasm_7C_2D5_stswi(s, op);
    case 0x2D7:
      return PPC32Emulator::dasm_7C_2E7_stfdx(s, op);
    case 0x2F6:
      return PPC32Emulator::dasm_7C_2F6_dcba(s, op);
    case 0x2F7:
      return PPC32Emulator::dasm_7C_2F7_stfdux(s, op);
    case 0x316:
      return PPC32Emulator::dasm_7C_316_lhbrx(s, op);
    case 0x318:
      return PPC32Emulator::dasm_7C_318_sraw(s, op);
    case 0x338:
      return PPC32Emulator::dasm_7C_338_srawi(s, op);
    case 0x356:
      return PPC32Emulator::dasm_7C_356_eieio(s, op);
    case 0x396:
      return PPC32Emulator::dasm_7C_396_sthbrx(s, op);
    case 0x39A:
      return PPC32Emulator::dasm_7C_39A_extsh(s, op);
    case 0x3BA:
      return PPC32Emulator::dasm_7C_3BA_extsb(s, op);
    case 0x3D6:
      return PPC32Emulator::dasm_7C_3D6_icbi(s, op);
    case 0x3D7:
      return PPC32Emulator::dasm_7C_3D7_stfiwx(s, op);
    case 0x3F6:
      return PPC32Emulator::dasm_7C_3F6_dcbz(s, op);
    default:
      return ".invalid  7C";
  }
}

std::string PPC32Emulator::dasm_7C_lx_stx(
    uint32_t op, const char* base_name, bool is_store, bool is_update, bool is_float) {
  uint8_t rsd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);

  std::string ra_str;
  if (is_update) {
    ra_str = std::format("upd(r{})", ra);
  } else if (ra != 0) {
    ra_str = std::format("r{}", ra);
  }

  char data_reg_ch = is_float ? 'f' : 'r';
  std::string args_str;
  if (is_store) {
    if (ra_str.empty()) {
      args_str = std::format("[r{}], {}{}", rb, data_reg_ch, rsd);
    } else {
      args_str = std::format("[{} + r{}], {}{}", ra_str, rb, data_reg_ch, rsd);
    }
  } else if (ra_str.empty()) {
    args_str = std::format("{}{}, [r{}]", data_reg_ch, rsd, rb);
  } else {
    args_str = std::format("{}{}, [{} + r{}]", data_reg_ch, rsd, ra_str, rb);
  }

  if (op & 1) {
    return std::format(".invalid  << {} {} with reserved bits set >>", base_name, args_str);
  } else {
    return std::format("{:<10}{}", base_name, args_str);
  }
}

std::string PPC32Emulator::dasm_7C_a_b(uint32_t op, const char* base_name) {
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (op & 0x03E00001) {
    return std::format(".invalid  << {} r{}, r{} with reserved bits set >>", base_name, ra, rb);
  } else {
    return std::format("{:<10}r{}, r{}", base_name, ra, rb);
  }
}

uint32_t PPC32Emulator::Assembler::asm_7C_a_b(const StreamItem& si, uint32_t subopcode) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return 0x7C000000 | op_set_reg2(a[0].reg_num) | op_set_reg3(a[1].reg_num) | op_set_subopcode(subopcode);
}

std::string PPC32Emulator::dasm_7C_d_a_b(uint32_t op, const char* base_name) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (op & 1) {
    return std::format(".invalid  << {} r{}, r{}, r{} with reserved bits set >>", base_name, rd, ra, rb);
  } else {
    return std::format("{:<10}r{}, r{}, r{}", base_name, rd, ra, rb);
  }
}

uint32_t PPC32Emulator::Assembler::asm_7C_d_a_b(const StreamItem& si, uint32_t subopcode) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return 0x7C000000 |
      op_set_reg1(a[0].reg_num) |
      op_set_reg2(a[1].reg_num) |
      op_set_reg3(a[2].reg_num) |
      op_set_subopcode(subopcode);
}

std::string PPC32Emulator::dasm_7C_d_a_b_r(uint32_t op, const char* base_name) {
  bool rec = op_get_rec(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  std::string name = base_name;
  if (rec) {
    name += '.';
  }
  return std::format("{:<10}r{}, r{}, r{}", name, rd, ra, rb);
}

uint32_t PPC32Emulator::Assembler::asm_7C_d_a_b_r(const StreamItem& si, uint32_t subopcode) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return 0x7C000000 |
      op_set_reg1(a[0].reg_num) |
      op_set_reg2(a[1].reg_num) |
      op_set_reg3(a[2].reg_num) |
      op_set_subopcode(subopcode) |
      op_set_rec(si.is_rec());
}

std::string PPC32Emulator::dasm_7C_s_a_b(uint32_t op, const char* base_name) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (op & 1) {
    return std::format(".invalid  << {} r{}, r{}, r{} with reserved bits set >>", base_name, ra, rs, rb);
  } else {
    return std::format("{:<10}r{}, r{}, r{}", base_name, ra, rs, rb);
  }
}

uint32_t PPC32Emulator::Assembler::asm_7C_s_a_b(const StreamItem& si, uint32_t subopcode) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return 0x7C000000 |
      op_set_reg1(a[1].reg_num) |
      op_set_reg2(a[0].reg_num) |
      op_set_reg3(a[2].reg_num) |
      op_set_subopcode(subopcode);
}

std::string PPC32Emulator::dasm_7C_s_a_r(uint32_t op, const char* base_name) {
  bool rec = op_get_rec(op);
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  std::string name = base_name;
  if (rec) {
    name += '.';
  }
  if (op & 0x0000F800) {
    return std::format(".invalid  << {} r{}, r{} with reserved bits set >>", name, ra, rs);
  } else {
    return std::format("{:<10}r{}, r{}", name, ra, rs);
  }
}

uint32_t PPC32Emulator::Assembler::asm_7C_s_a_r(const StreamItem& si, uint32_t subopcode) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return 0x7C000000 |
      op_set_reg1(a[1].reg_num) |
      op_set_reg2(a[0].reg_num) |
      op_set_subopcode(subopcode) |
      op_set_rec(si.is_rec());
}

std::string PPC32Emulator::dasm_7C_s_a_b_r(uint32_t op, const char* base_name) {
  bool rec = op_get_rec(op);
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  std::string name = base_name;
  if (rec) {
    name += '.';
  }
  return std::format("{:<10}r{}, r{}, r{}", name, ra, rs, rb);
}

uint32_t PPC32Emulator::Assembler::asm_7C_s_a_b_r(const StreamItem& si, uint32_t subopcode) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return 0x7C000000 |
      op_set_reg1(a[1].reg_num) |
      op_set_reg2(a[0].reg_num) |
      op_set_reg3(a[2].reg_num) |
      op_set_subopcode(subopcode) |
      op_set_rec(si.is_rec());
}

std::string PPC32Emulator::dasm_7C_d_a_o_r(uint32_t op, const char* base_name) {
  bool rec = op_get_rec(op);
  bool o = op_get_o(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  std::string name = base_name;
  if (o) {
    name += 'o';
  }
  if (rec) {
    name += '.';
  }
  if (op & 0x0000F800) {
    return std::format(".invalid  << {} r{}, r{} with reserved bits set >>", name, rd, ra);
  } else {
    return std::format("{:<10}r{}, r{}", name, rd, ra);
  }
}

uint32_t PPC32Emulator::Assembler::asm_7C_d_a_o_r(const StreamItem& si, uint32_t subopcode) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return 0x7C000000 |
      op_set_reg1(a[0].reg_num) |
      op_set_reg2(a[1].reg_num) |
      op_set_subopcode(subopcode) |
      op_set_o(si.op_name.ends_with("o.") || si.op_name.ends_with("o")) |
      op_set_rec(si.is_rec());
}

std::string PPC32Emulator::dasm_7C_d_a_b_o_r(uint32_t op, const char* base_name) {
  bool rec = op_get_rec(op);
  bool o = op_get_o(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  std::string name = base_name;
  if (o) {
    name += 'o';
  }
  if (rec) {
    name += '.';
  }
  return std::format("{:<10}r{}, r{}, r{}", name, rd, ra, rb);
}

uint32_t PPC32Emulator::Assembler::asm_7C_d_a_b_o_r(const StreamItem& si, uint32_t subopcode) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return 0x7C000000 |
      op_set_reg1(a[0].reg_num) |
      op_set_reg2(a[1].reg_num) |
      op_set_reg3(a[2].reg_num) |
      op_set_subopcode(subopcode) |
      op_set_o(si.op_name.ends_with("o.") || si.op_name.ends_with("o")) |
      op_set_rec(si.is_rec());
}

void PPC32Emulator::exec_7C_000_cmp(uint32_t op) {
  // 011111 DDD 0 L AAAAA BBBBB 0000000000 0

  uint8_t a_reg = op_get_reg2(op);
  uint8_t b_reg = op_get_reg3(op);
  uint8_t crf_num = op_get_crf1(op);
  uint8_t crf_res = this->regs.xer.get_so() ? 1 : 0;
  if (this->regs.r[a_reg].s < this->regs.r[b_reg].s) {
    crf_res |= 8;
  } else if (this->regs.r[a_reg].s > this->regs.r[b_reg].s) {
    crf_res |= 4;
  } else {
    crf_res |= 2;
  }
  this->regs.cr.replace_field(crf_num, crf_res);
}

std::string PPC32Emulator::dasm_7C_000_cmp(DisassemblyState&, uint32_t op) {
  uint8_t crf = op_get_crf1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (op & 0x00600001) {
    return std::format(".invalid  << cmp cr{}, r{}, r{} with reserved bits set >>", crf, ra, rb);
  } else if (crf) {
    return std::format("cmp       cr{}, r{}, r{}", crf, ra, rb);
  } else {
    return std::format("cmp       r{}, r{}", ra, rb);
  }
}

uint32_t PPC32Emulator::Assembler::asm_cmp(const StreamItem& si) {
  if (si.args.size() == 3) {
    const auto& a = si.check_args({ArgType::CONDITION_FIELD, ArgType::INT_REGISTER, ArgType::INT_REGISTER});
    return 0x7C000000 | op_set_crf1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_reg3(a[2].reg_num);
  } else {
    const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
    return 0x7C000000 | op_set_reg2(a[0].reg_num) | op_set_reg3(a[1].reg_num);
  }
}

void PPC32Emulator::exec_7C_004_tw(uint32_t op) {
  this->exec_unimplemented(op); // 011111 TTTTT AAAAA BBBBB 0000000100 0
}

std::string PPC32Emulator::dasm_7C_004_tw(DisassemblyState&, uint32_t op) {
  uint8_t to = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (op & 1) {
    return std::format(".invalid  << tw {}, r{}, r{} with reserved bits set >>", to, ra, rb);
  } else {
    return std::format("tw        {}, r{}, r{}", to, ra, rb);
  }
}

uint32_t PPC32Emulator::Assembler::asm_tw(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::IMMEDIATE, ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return 0x7C000008 | op_set_reg1(this->resolve_immediate(a[0])) | op_set_reg2(a[1].reg_num) | op_set_reg3(a[2].reg_num);
}

void PPC32Emulator::exec_7C_008_208_subfc(uint32_t op) {
  // 011111 DDDDD AAAAA BBBBB O 000001000 R
  if (op_get_o(op)) {
    throw std::runtime_error("overflow bits not implemented");
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
    this->regs.set_crf_int_result(0, this->regs.r[rd].s);
  }
}

std::string PPC32Emulator::dasm_7C_008_208_subfc(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "subfc");
}

uint32_t PPC32Emulator::Assembler::asm_subfc(const StreamItem& si) {
  return this->asm_7C_d_a_b_o_r(si, 0x008);
}

void PPC32Emulator::exec_7C_00A_20A_addc(uint32_t op) {
  if(op_get_o(op)) throw std::runtime_error("overflow not implemented");
  uint8_t d=op_get_reg1(op),a=op_get_reg2(op),b=op_get_reg3(op);(void)a;(void)b;
  uint64_t sum = (uint64_t)this->regs.r[a].u + this->regs.r[b].u;
  this->regs.r[d].u = (uint32_t)sum;
  this->regs.xer.set_ca((sum>>32)&1);
  if(op_get_rec(op)) this->regs.set_crf_int_result(0, this->regs.r[d].s);
}

std::string PPC32Emulator::dasm_7C_00A_20A_addc(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "addc");
}

uint32_t PPC32Emulator::Assembler::asm_addc(const StreamItem& si) {
  return this->asm_7C_d_a_b_o_r(si, 0x00A);
}

void PPC32Emulator::exec_7C_00B_mulhwu(uint32_t op) {
  uint8_t rd=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op);
  uint64_t prod=(uint64_t)this->regs.r[ra].u * (uint64_t)this->regs.r[rb].u;
  this->regs.r[rd].u = (uint32_t)(prod>>32);
  if(op&1) this->regs.set_crf_int_result(0, this->regs.r[rd].s);
}

std::string PPC32Emulator::dasm_7C_00B_mulhwu(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_b_r(op, "mulhwu");
}

uint32_t PPC32Emulator::Assembler::asm_mulhwu(const StreamItem& si) {
  return this->asm_7C_d_a_b_r(si, 0x00B);
}

void PPC32Emulator::exec_7C_013_mfcr(uint32_t op) {
  // 011111 DDDDD 00000 00000 0000010011 0
  this->regs.r[op_get_reg1(op)].u = this->regs.cr.u;
}

std::string PPC32Emulator::dasm_7C_013_mfcr(DisassemblyState&, uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  if (op & 0x001FF801) {
    return std::format(".invalid  << mfcr r{} with reserved bits set >>", rd);
  } else {
    return std::format("mfcr      r{}", rd);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mfcr(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER});
  return 0x7C000026 | op_set_reg1(a[0].reg_num);
}

void PPC32Emulator::exec_7C_014_lwarx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 0000010100 0
}

std::string PPC32Emulator::dasm_7C_014_lwarx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lwarx", false, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lwarx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x014, false, false, false);
}

void PPC32Emulator::exec_7C_017_lwzx(uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->regs.r[rd].u = this->mem->read<phosg::be_uint32_t>(this->regs.debug.addr);
}

std::string PPC32Emulator::dasm_7C_017_lwzx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lwzx", false, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lwzx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x017, false, false, false);
}

void PPC32Emulator::exec_7C_018_slw(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0000011000 R
}

std::string PPC32Emulator::dasm_7C_018_slw(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "slw");
}

uint32_t PPC32Emulator::Assembler::asm_slw(const StreamItem& si) {
  return this->asm_7C_s_a_b_r(si, 0x018);
}

void PPC32Emulator::exec_7C_01A_cntlzw(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA 00000 0000011010 R
}

std::string PPC32Emulator::dasm_7C_01A_cntlzw(DisassemblyState&, uint32_t op) {
  bool rec = op_get_rec(op);
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  if (op & 0x0000F800) {
    return std::format(".invalid  << cntlzw{} r{}, r{} with reserved bits set >>", rec ? '.' : ' ', ra, rs);
  } else {
    return std::format("cntlzw{}   r{}, r{}", rec ? '.' : ' ', ra, rs);
  }
}

uint32_t PPC32Emulator::Assembler::asm_cntlzw(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return 0x7C000034 | op_set_reg1(a[1].reg_num) | op_set_reg2(a[0].reg_num) | op_set_rec(si.op_name.ends_with('.'));
}

void PPC32Emulator::exec_7C_01C_and(uint32_t op) {
  // 011111 SSSSS AAAAA BBBBB 0000011100 R
  uint8_t s_reg = op_get_reg1(op);
  uint8_t a_reg = op_get_reg2(op);
  uint8_t b_reg = op_get_reg3(op);
  this->regs.r[a_reg].u = this->regs.r[s_reg].u & this->regs.r[b_reg].u;
  if (op_get_rec(op)) {
    this->regs.set_crf_int_result(0, this->regs.r[a_reg].s);
  }
}

std::string PPC32Emulator::dasm_7C_01C_and(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "and");
}

uint32_t PPC32Emulator::Assembler::asm_and(const StreamItem& si) {
  return this->asm_7C_s_a_b_r(si, 0x01C);
}

void PPC32Emulator::exec_7C_020_cmpl(uint32_t op) {
  // 011111 DDD 0 L AAAAA BBBBB 0000100000 0

  uint8_t a_reg = op_get_reg2(op);
  uint8_t b_reg = op_get_reg3(op);
  uint8_t crf_num = op_get_crf1(op);
  uint8_t crf_res = this->regs.xer.get_so() ? 1 : 0;
  if (this->regs.r[a_reg].u < this->regs.r[b_reg].u) {
    crf_res |= 8;
  } else if (this->regs.r[a_reg].u > this->regs.r[b_reg].u) {
    crf_res |= 4;
  } else {
    crf_res |= 2;
  }
  this->regs.cr.replace_field(crf_num, crf_res);
}

std::string PPC32Emulator::dasm_7C_020_cmpl(DisassemblyState&, uint32_t op) {
  uint8_t crf = op_get_crf1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (op & 0x00600001) {
    return std::format(".invalid  << cmpl cr{}, r{}, r{} with reserved bits set >>", crf, ra, rb);
  } else if (crf) {
    return std::format("cmpl      cr{}, r{}, r{}", crf, ra, rb);
  } else {
    return std::format("cmpl      r{}, r{}", ra, rb);
  }
}

uint32_t PPC32Emulator::Assembler::asm_cmpl(const StreamItem& si) {
  if (si.args.size() == 3) {
    const auto& a = si.check_args({ArgType::CONDITION_FIELD, ArgType::INT_REGISTER, ArgType::INT_REGISTER});
    return 0x7C000040 | op_set_crf1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_reg3(a[2].reg_num);
  } else {
    const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
    return 0x7C000040 | op_set_reg2(a[0].reg_num) | op_set_reg3(a[1].reg_num);
  }
}

void PPC32Emulator::exec_7C_028_228_subf(uint32_t op) {
  // 011111 DDDDD AAAAA BBBBB O 000101000 R
  uint8_t d_reg = op_get_reg1(op);
  uint8_t a_reg = op_get_reg2(op);
  uint8_t b_reg = op_get_reg3(op);
  bool o = op_get_o(op);
  bool rec = op_get_rec(op);
  if (o) {
    throw std::runtime_error("subfo is not implemented");
  }
  this->regs.r[d_reg].u = ~this->regs.r[a_reg].u + this->regs.r[b_reg].u + 1;
  if (rec) {
    this->regs.set_crf_int_result(0, this->regs.r[d_reg].s);
  }
}

std::string PPC32Emulator::dasm_7C_028_228_subf(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "subf");
}

uint32_t PPC32Emulator::Assembler::asm_subf(const StreamItem& si) {
  return this->asm_7C_d_a_b_o_r(si, 0x028);
}

uint32_t PPC32Emulator::Assembler::asm_sub(const StreamItem& si) {
  // This is the same as subf, but the a/b registers are swapped
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return 0x7C000000 |
      op_set_reg1(a[0].reg_num) |
      op_set_reg2(a[2].reg_num) |
      op_set_reg3(a[1].reg_num) |
      op_set_subopcode(0x28) |
      op_set_o(si.op_name.ends_with("o.") || si.is_rec()) |
      op_set_rec(si.is_rec());
}

void PPC32Emulator::exec_7C_036_dcbst(uint32_t) {
  // 011111 00000 AAAAA BBBBB 0000110110 0
  // We don't emulate the data cache, so we simply ignore this opcode
}

std::string PPC32Emulator::dasm_7C_036_dcbst(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcbst");
}

uint32_t PPC32Emulator::Assembler::asm_dcbst(const StreamItem& si) {
  return this->asm_7C_a_b(si, 0x036);
}

void PPC32Emulator::exec_7C_037_lwzux(uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->regs.r[rd].u = this->mem->read<phosg::be_uint32_t>(this->regs.debug.addr);
  this->regs.r[ra].u = this->regs.debug.addr;
}

std::string PPC32Emulator::dasm_7C_037_lwzux(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lwzux", false, true, false);
}

uint32_t PPC32Emulator::Assembler::asm_lwzux(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x037, false, true, false);
}

void PPC32Emulator::exec_7C_03C_andc(uint32_t op) {
  uint8_t a=op_get_reg1(op),ra=op_get_reg2(op),b=op_get_reg3(op);
  (void)a;(void)b;
  this->regs.r[ra].u = this->regs.r[a].u & ~this->regs.r[b].u;
  if(op_get_rec(op)) this->regs.set_crf_int_result(0, this->regs.r[ra].s);
}

std::string PPC32Emulator::dasm_7C_03C_andc(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "andc");
}

uint32_t PPC32Emulator::Assembler::asm_andc(const StreamItem& si) {
  return this->asm_7C_s_a_b_r(si, 0x03C);
}

void PPC32Emulator::exec_7C_04B_mulhw(uint32_t op) {
  uint8_t rd=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op);
  int64_t prod=(int64_t)this->regs.r[ra].s * (int64_t)this->regs.r[rb].s;
  this->regs.r[rd].u = (uint32_t)(((uint64_t)prod)>>32);
  if(op&1) this->regs.set_crf_int_result(0, this->regs.r[rd].s);
}

std::string PPC32Emulator::dasm_7C_04B_mulhw(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_b_r(op, "mulhw");
}

uint32_t PPC32Emulator::Assembler::asm_mulhw(const StreamItem& si) {
  return this->asm_7C_d_a_b_r(si, 0x04B);
}

void PPC32Emulator::exec_7C_053_mfmsr(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD 00000 00000 0001010011 0
}

std::string PPC32Emulator::dasm_7C_053_mfmsr(DisassemblyState&, uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  if (op & 0x001FF801) {
    return std::format(".invalid  << mfmsr r{} with reserved bits set >>", rd);
  } else {
    return std::format("mfmsr     r{}", rd);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mfmsr(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER});
  return 0x7C0000A6 | op_set_reg1(a[0].reg_num);
}

void PPC32Emulator::exec_7C_056_dcbf(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 0001010110 0
}

std::string PPC32Emulator::dasm_7C_056_dcbf(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcbf");
}

uint32_t PPC32Emulator::Assembler::asm_dcbf(const StreamItem& si) {
  return this->asm_7C_a_b(si, 0x056);
}

void PPC32Emulator::exec_7C_057_lbzx(uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->regs.r[rd].u = static_cast<uint32_t>(this->mem->read<uint8_t>(this->regs.debug.addr));
}

std::string PPC32Emulator::dasm_7C_057_lbzx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lbzx", false, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lbzx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x057, false, false, false);
}

void PPC32Emulator::exec_7C_068_268_neg(uint32_t op) {
  if (op_get_o(op)) {
    throw std::runtime_error("overflow bits not implemented");
  }

  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  this->regs.r[rd].u = (~this->regs.r[ra].u) + 1;
  if (op_get_rec(op)) {
    this->regs.set_crf_int_result(0, this->regs.r[rd].s);
  }
}

std::string PPC32Emulator::dasm_7C_068_268_neg(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_o_r(op, "neg");
}

uint32_t PPC32Emulator::Assembler::asm_neg(const StreamItem& si) {
  return this->asm_7C_d_a_o_r(si, 0x068);
}

void PPC32Emulator::exec_7C_077_lbzux(uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->regs.r[rd].u = static_cast<uint32_t>(this->mem->read<uint8_t>(this->regs.debug.addr));
  this->regs.r[ra].u = this->regs.debug.addr;
}

std::string PPC32Emulator::dasm_7C_077_lbzux(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lbzux", false, true, false);
}

uint32_t PPC32Emulator::Assembler::asm_lbzux(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x077, false, true, false);
}

void PPC32Emulator::exec_7C_07C_nor(uint32_t op) {
  uint8_t a=op_get_reg1(op),ra=op_get_reg2(op),b=op_get_reg3(op);
  (void)a;(void)b;
  this->regs.r[ra].u = ~(this->regs.r[a].u | this->regs.r[b].u);
  if(op_get_rec(op)) this->regs.set_crf_int_result(0, this->regs.r[ra].s);
}

std::string PPC32Emulator::dasm_7C_07C_nor(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "nor");
}

uint32_t PPC32Emulator::Assembler::asm_nor(const StreamItem& si) {
  return this->asm_7C_s_a_b_r(si, 0x07C);
}

void PPC32Emulator::exec_7C_088_288_subfe(uint32_t op) {
  if (op_get_o(op)) {
    // TODO: I'm too lazy to implement this right now
    throw std::runtime_error("overflow bits not implemented");
  }

  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.r[rd].u = (~this->regs.r[ra].u) + this->regs.r[rb].u + this->regs.xer.get_ca();
  if (this->regs.r[rd].s < 0) {
    this->regs.xer.u |= 0x20000000; // xer[ca] = 1
  } else {
    this->regs.xer.u &= ~0x20000000; // xer[ca] = 0
  }
  if (op_get_rec(op)) {
    this->regs.set_crf_int_result(0, this->regs.r[rd].s);
  }
}

std::string PPC32Emulator::dasm_7C_088_288_subfe(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "subfe");
}

uint32_t PPC32Emulator::Assembler::asm_subfe(const StreamItem& si) {
  return this->asm_7C_d_a_b_o_r(si, 0x088);
}

void PPC32Emulator::exec_7C_08A_28A_adde(uint32_t op) {
  if(op_get_o(op)) throw std::runtime_error("overflow not implemented");
  uint8_t d=op_get_reg1(op),a=op_get_reg2(op),b=op_get_reg3(op);(void)a;(void)b;
  uint64_t sum = (uint64_t)this->regs.r[a].u + this->regs.r[b].u + this->regs.xer.get_ca();
  this->regs.r[d].u = (uint32_t)sum;
  this->regs.xer.set_ca((sum>>32)&1);
  if(op_get_rec(op)) this->regs.set_crf_int_result(0, this->regs.r[d].s);
}

std::string PPC32Emulator::dasm_7C_08A_28A_adde(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "adde");
}

uint32_t PPC32Emulator::Assembler::asm_adde(const StreamItem& si) {
  return this->asm_7C_d_a_b_o_r(si, 0x08A);
}

void PPC32Emulator::exec_7C_090_mtcrf(uint32_t op) {
  // 011111 SSSSS 0 CCCCCCCC 0 0010010000 0
  uint32_t rs = this->regs.r[op_get_reg1(op)].u;
  uint8_t crm = (op >> 12) & 0xFF;
  uint32_t mask = 0;
  for (int i = 0; i < 8; i++) {
    if (crm & (0x80 >> i)) {
      mask |= (0xF << (28 - i * 4));
    }
  }
  this->regs.cr.u = (this->regs.cr.u & ~mask) | (rs & mask);
}

std::string PPC32Emulator::dasm_7C_090_mtcrf(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t crm = (op >> 12) & 0xFF;
  if (op & 0x00100801) {
    return std::format(".invalid  << mtcrf 0x{:02X}, r{} with reserved bits set >>", crm, rs);
  } else if (crm == 0xFF) {
    return std::format("mtcr      r{}", rs);
  } else {
    return std::format("mtcrf     0x{:02X}, r{}", crm, rs);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mtcr_mtcrf(const StreamItem& si) {
  if (si.args.size() == 2) {
    const auto& a = si.check_args({ArgType::IMMEDIATE, ArgType::INT_REGISTER});
    return 0x7C000120 | ((this->resolve_immediate(a[0]) & 0xFF) << 12) | op_set_reg1(a[1].reg_num);
  } else {
    const auto& a = si.check_args({ArgType::INT_REGISTER});
    return 0x7C0FF120 | op_set_reg1(a[0].reg_num);
  }
}

void PPC32Emulator::exec_7C_092_mtmsr(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS 00000 00000 0010010010 0
}

std::string PPC32Emulator::dasm_7C_092_mtmsr(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  if (op & 0x001FF801) {
    return std::format(".invalid  << mtmsr r{} with reserved bits set >>", rs);
  } else {
    return std::format("mtmsr     r{}", rs);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mtmsr(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER});
  return 0x7C000124 | op_set_reg1(a[0].reg_num);
}

void PPC32Emulator::exec_7C_096_stwcx_rec(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0010010110 1
}

std::string PPC32Emulator::dasm_7C_096_stwcx_rec(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "stwcx.", true, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_stwcx_rec(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x096, true, false, false);
}

void PPC32Emulator::exec_7C_097_stwx(uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->mem->write<phosg::be_uint32_t>(this->regs.debug.addr, this->regs.r[rs].u);
}

std::string PPC32Emulator::dasm_7C_097_stwx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "stwx", true, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_stwx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x097, true, false, false);
}

void PPC32Emulator::exec_7C_0B7_stwux(uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->mem->write<phosg::be_uint32_t>(this->regs.debug.addr, this->regs.r[rs].u);
  this->regs.r[ra].u = this->regs.debug.addr;
}

std::string PPC32Emulator::dasm_7C_0B7_stwux(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "stwux", true, true, false);
}

uint32_t PPC32Emulator::Assembler::asm_stwux(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x0B7, true, true, false);
}

void PPC32Emulator::exec_7C_0C8_2C8_subfze(uint32_t op) {
  if(op_get_o(op)) throw std::runtime_error("overflow not implemented");
  uint8_t d=op_get_reg1(op),a=op_get_reg2(op);(void)a;
  uint64_t sum = (uint64_t)(~this->regs.r[a].u) + this->regs.xer.get_ca();
  this->regs.r[d].u = (uint32_t)sum;
  this->regs.xer.set_ca((sum>>32)&1);
  if(op_get_rec(op)) this->regs.set_crf_int_result(0, this->regs.r[d].s);
}

std::string PPC32Emulator::dasm_7C_0C8_2C8_subfze(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_o_r(op, "subfze");
}

uint32_t PPC32Emulator::Assembler::asm_subfze(const StreamItem& si) {
  return this->asm_7C_d_a_o_r(si, 0x0C8);
}

void PPC32Emulator::exec_7C_0CA_2CA_addze(uint32_t op) {
  if(op_get_o(op)) throw std::runtime_error("overflow not implemented");
  uint8_t d=op_get_reg1(op),a=op_get_reg2(op);(void)a;
  uint64_t sum = (uint64_t)this->regs.r[a].u + this->regs.xer.get_ca();
  this->regs.r[d].u = (uint32_t)sum;
  this->regs.xer.set_ca((sum>>32)&1);
  if(op_get_rec(op)) this->regs.set_crf_int_result(0, this->regs.r[d].s);
}

std::string PPC32Emulator::dasm_7C_0CA_2CA_addze(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_o_r(op, "addze");
}

uint32_t PPC32Emulator::Assembler::asm_addze(const StreamItem& si) {
  return this->asm_7C_d_a_o_r(si, 0x0CA);
}

void PPC32Emulator::exec_7C_0D2_mtsr(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS 0 RRRR 00000 0011010010 0
}

std::string PPC32Emulator::dasm_7C_0D2_mtsr(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t sr = op_get_reg2(op) & 0x0F;
  if (op & 0x0010F801) {
    return std::format(".invalid  << mtsr {}, r{} with reserved bits set >>", sr, rs);
  } else {
    return std::format("mtsr      {}, r{}", sr, rs);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mtsr(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::IMMEDIATE, ArgType::INT_REGISTER});
  return 0x7C0001A4 | op_set_reg1(a[1].reg_num) | op_set_reg2(this->resolve_immediate(a[0]));
}

void PPC32Emulator::exec_7C_0D7_stbx(uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->mem->write<uint8_t>(this->regs.debug.addr, this->regs.r[rs].u);
}

std::string PPC32Emulator::dasm_7C_0D7_stbx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "stbx", true, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_stbx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x0D7, true, false, false);
}

void PPC32Emulator::exec_7C_0E8_2E8_subfme(uint32_t op) {
  if(op_get_o(op)) throw std::runtime_error("overflow not implemented");
  uint8_t d=op_get_reg1(op),a=op_get_reg2(op);(void)a;
  uint64_t sum = (uint64_t)(~this->regs.r[a].u) + 0xFFFFFFFFULL + this->regs.xer.get_ca();
  this->regs.r[d].u = (uint32_t)sum;
  this->regs.xer.set_ca((sum>>32)&1);
  if(op_get_rec(op)) this->regs.set_crf_int_result(0, this->regs.r[d].s);
}

std::string PPC32Emulator::dasm_7C_0E8_2E8_subfme(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_o_r(op, "subfme");
}

uint32_t PPC32Emulator::Assembler::asm_subfme(const StreamItem& si) {
  return this->asm_7C_d_a_o_r(si, 0x0E8);
}

void PPC32Emulator::exec_7C_0EA_2EA_addme(uint32_t op) {
  if(op_get_o(op)) throw std::runtime_error("overflow not implemented");
  uint8_t d=op_get_reg1(op),a=op_get_reg2(op);(void)a;
  uint64_t sum = (uint64_t)this->regs.r[a].u + 0xFFFFFFFFULL + this->regs.xer.get_ca();
  this->regs.r[d].u = (uint32_t)sum;
  this->regs.xer.set_ca((sum>>32)&1);
  if(op_get_rec(op)) this->regs.set_crf_int_result(0, this->regs.r[d].s);
}

std::string PPC32Emulator::dasm_7C_0EA_2EA_addme(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_o_r(op, "addme");
}

uint32_t PPC32Emulator::Assembler::asm_addme(const StreamItem& si) {
  return this->asm_7C_d_a_o_r(si, 0x0EA);
}

void PPC32Emulator::exec_7C_0EB_2EB_mullw(uint32_t op) {
  uint8_t rd=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op);
  this->regs.r[rd].s = this->regs.r[ra].s * this->regs.r[rb].s;
  if(op&1) this->regs.set_crf_int_result(0, this->regs.r[rd].s);
}

std::string PPC32Emulator::dasm_7C_0EB_2EB_mullw(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "mullw");
}

uint32_t PPC32Emulator::Assembler::asm_mullw(const StreamItem& si) {
  return this->asm_7C_d_a_b_o_r(si, 0x0EB);
}

void PPC32Emulator::exec_7C_0F2_mtsrin(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS 00000 BBBBB 0011110010 0
}

std::string PPC32Emulator::dasm_7C_0F2_mtsrin(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t rb = op_get_reg3(op);
  if (op & 0x001F0001) {
    return std::format(".invalid  << mtsr r{}, r{} with reserved bits set >>", rb, rs);
  } else {
    return std::format("mtsrin    r{}, r{}", rb, rs);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mtsrin(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return 0x7C0001E4 | op_set_reg1(a[1].reg_num) | op_set_reg3(a[0].reg_num);
}

void PPC32Emulator::exec_7C_0F6_dcbtst(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 0011110110 0
}

std::string PPC32Emulator::dasm_7C_0F6_dcbtst(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcbtst");
}

uint32_t PPC32Emulator::Assembler::asm_dcbtst(const StreamItem& si) {
  return this->asm_7C_a_b(si, 0x0F6);
}

void PPC32Emulator::exec_7C_0F7_stbux(uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->mem->write<uint8_t>(this->regs.debug.addr, this->regs.r[rs].u);
  this->regs.r[ra].u = this->regs.debug.addr;
}

std::string PPC32Emulator::dasm_7C_0F7_stbux(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "stbux", true, true, false);
}

uint32_t PPC32Emulator::Assembler::asm_stbux(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x0F7, true, true, false);
}

void PPC32Emulator::exec_7C_10A_30A_add(uint32_t op) {
  // 011111 DDDDD AAAAA BBBBB O 100001010 R
  if (op_get_o(op)) {
    throw std::runtime_error("overflow bits not implemented");
  }

  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.r[rd].s = this->regs.r[ra].s + this->regs.r[rb].s;
  if (op_get_rec(op)) {
    this->regs.set_crf_int_result(0, this->regs.r[rd].s);
  }
}

std::string PPC32Emulator::dasm_7C_10A_30A_add(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "add");
}

uint32_t PPC32Emulator::Assembler::asm_add(const StreamItem& si) {
  return this->asm_7C_d_a_b_o_r(si, 0x10A);
}

void PPC32Emulator::exec_7C_116_dcbt(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 0100010110 0
}

std::string PPC32Emulator::dasm_7C_116_dcbt(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcbt");
}

uint32_t PPC32Emulator::Assembler::asm_dcbt(const StreamItem& si) {
  return this->asm_7C_a_b(si, 0x116);
}

void PPC32Emulator::exec_7C_117_lhzx(uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->regs.r[rd].u = static_cast<uint32_t>(this->mem->read<phosg::be_uint16_t>(this->regs.debug.addr));
}

std::string PPC32Emulator::dasm_7C_117_lhzx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lhzx", false, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lhzx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x117, false, false, false);
}

void PPC32Emulator::exec_7C_11C_eqv(uint32_t op) {
  uint8_t a=op_get_reg1(op),ra=op_get_reg2(op),b=op_get_reg3(op);
  (void)a;(void)b;
  this->regs.r[ra].u = ~(this->regs.r[a].u ^ this->regs.r[b].u);
  if(op_get_rec(op)) this->regs.set_crf_int_result(0, this->regs.r[ra].s);
}

std::string PPC32Emulator::dasm_7C_11C_eqv(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "eqv");
}

uint32_t PPC32Emulator::Assembler::asm_eqv(const StreamItem& si) {
  return this->asm_7C_s_a_b_r(si, 0x11C);
}

void PPC32Emulator::exec_7C_132_tlbie(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 00000 BBBBB 0100110010 0
}

std::string PPC32Emulator::dasm_7C_132_tlbie(DisassemblyState&, uint32_t op) {
  uint8_t rb = op_get_reg3(op);
  if (op & 0x03FF0001) {
    return std::format(".invalid  << tlbie r{} with reserved bits set >>", rb);
  } else {
    return std::format("tlbie     r{}", rb);
  }
}

uint32_t PPC32Emulator::Assembler::asm_tlbie(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER});
  return 0x7C000264 | op_set_reg3(a[0].reg_num);
}

void PPC32Emulator::exec_7C_136_eciwx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 0100110110 0
}

std::string PPC32Emulator::dasm_7C_136_eciwx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_b(op, "eciwx");
}

uint32_t PPC32Emulator::Assembler::asm_eciwx(const StreamItem& si) {
  return this->asm_7C_d_a_b(si, 0x136);
}

void PPC32Emulator::exec_7C_137_lhzux(uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->regs.r[rd].u = static_cast<uint32_t>(this->mem->read<phosg::be_uint16_t>(this->regs.debug.addr));
  this->regs.r[ra].u = this->regs.debug.addr;
}

std::string PPC32Emulator::dasm_7C_137_lhzux(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lhzux", false, true, false);
}

uint32_t PPC32Emulator::Assembler::asm_lhzux(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x137, false, true, false);
}

void PPC32Emulator::exec_7C_13C_xor(uint32_t op) {
  uint8_t a=op_get_reg1(op),ra=op_get_reg2(op),b=op_get_reg3(op);
  (void)a;(void)b;
  this->regs.r[ra].u = this->regs.r[a].u ^ this->regs.r[b].u;
  if(op_get_rec(op)) this->regs.set_crf_int_result(0, this->regs.r[ra].s);
}

std::string PPC32Emulator::dasm_7C_13C_xor(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "xor");
}

uint32_t PPC32Emulator::Assembler::asm_xor(const StreamItem& si) {
  return this->asm_7C_s_a_b_r(si, 0x13C);
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

std::string PPC32Emulator::dasm_7C_153_mfspr(DisassemblyState&, uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint16_t spr = op_get_spr(op);
  const char* name = name_for_spr(spr);
  if (op & 1) {
    return std::format(".invalid  << mfspr r{}, spr{} with reserved bits set >>", rd, spr);
  } else if (name) {
    return std::format("mf{:<8}r{}", name, rd);
  } else {
    return std::format("mfspr     r{}, spr{}", rd, spr);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mfspr_mnemonic(const StreamItem& si) {
  if (si.op_name == "mfspr") {
    const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::SPECIAL_REGISTER});
    return 0x7C0002A6 | op_set_reg1(a[0].reg_num) | op_set_spr(a[1].reg_num);
  } else {
    const auto& a = si.check_args({ArgType::INT_REGISTER});
    if (!si.op_name.starts_with("mf")) {
      throw std::logic_error("mfspr mnemonic assembler called for non-mf opcode");
    }
    return 0x7C0002A6 | op_set_reg1(a[0].reg_num) | op_set_spr(spr_for_name(si.op_name.substr(2)));
  }
}

void PPC32Emulator::exec_7C_157_lhax(uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  // lhax is not an update form, so it must not write back to rA.
  this->regs.r[rd].u = phosg::sign_extend<uint32_t, uint16_t>(this->mem->read<phosg::be_uint16_t>(this->regs.debug.addr));
}

std::string PPC32Emulator::dasm_7C_157_lhax(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lhax", false, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lhax(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x157, false, false, false);
}

void PPC32Emulator::exec_7C_172_tlbia(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 00000 00000 0101110010 0
}

std::string PPC32Emulator::dasm_7C_172_tlbia(DisassemblyState&, uint32_t op) {
  return (op == 0x7C0002E4) ? "tlbia" : ".invalid  << tlbia with reserved bits set >>";
}

uint32_t PPC32Emulator::Assembler::asm_tlbia(const StreamItem& si) {
  si.check_args({});
  return 0x7C0002E4;
}

void PPC32Emulator::exec_7C_173_mftb(uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint16_t tbr = op_get_spr(op);
  if (tbr == 268) {
    this->regs.r[rd].u = this->regs.tbr & 0xFFFFFFFF;
  } else if (tbr == 269) {
    this->regs.r[rd].u = (this->regs.tbr >> 32) & 0xFFFFFFFF;
  } else {
    throw std::runtime_error("invalid TBR index");
  }
}

std::string PPC32Emulator::dasm_7C_173_mftb(DisassemblyState&, uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint16_t tbr = op_get_spr(op);
  if (op & 1) {
    return std::format(".invalid  << mftb r{}, tbr{} with reserved bits set >>", rd, tbr);
  } else if (tbr == 268) {
    return std::format("mftb      r{}", rd);
  } else if (tbr == 269) {
    return std::format("mftbu     r{}", rd);
  } else {
    return std::format("mftb      r{}, tbr{}", rd, tbr);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mftb(const StreamItem& si) {
  uint8_t reg;
  uint16_t tbr;
  if (si.args.size() == 2) {
    const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::TIME_REGISTER});
    reg = a[0].reg_num;
    tbr = a[1].reg_num;
  } else {
    const auto& a = si.check_args({ArgType::INT_REGISTER});
    reg = a[0].reg_num;
    tbr = (si.op_name == "mftbu") ? 269 : 268;
  }
  return 0x7C0002E6 | op_set_reg1(reg) | op_set_spr(tbr);
}

void PPC32Emulator::exec_7C_177_lhaux(uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->regs.r[rd].u = phosg::sign_extend<uint32_t, uint16_t>(this->mem->read<phosg::be_uint16_t>(this->regs.debug.addr));
  this->regs.r[ra].u = this->regs.debug.addr;
}

std::string PPC32Emulator::dasm_7C_177_lhaux(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lhaux", false, true, false);
}

uint32_t PPC32Emulator::Assembler::asm_lhaux(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x177, false, true, false);
}

void PPC32Emulator::exec_7C_197_sthx(uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->mem->write_u16b(this->regs.r[rb].u + (ra ? this->regs.r[ra].u : 0), this->regs.r[rs].u);
}

std::string PPC32Emulator::dasm_7C_197_sthx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "sthx", true, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_sthx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x197, true, false, false);
}

void PPC32Emulator::exec_7C_19C_orc(uint32_t op) {
  uint8_t a=op_get_reg1(op),ra=op_get_reg2(op),b=op_get_reg3(op);
  (void)a;(void)b;
  this->regs.r[ra].u = this->regs.r[a].u | ~this->regs.r[b].u;
  if(op_get_rec(op)) this->regs.set_crf_int_result(0, this->regs.r[ra].s);
}

std::string PPC32Emulator::dasm_7C_19C_orc(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_b_r(op, "orc");
}

uint32_t PPC32Emulator::Assembler::asm_orc(const StreamItem& si) {
  return this->asm_7C_d_a_b_r(si, 0x19C);
}

void PPC32Emulator::exec_7C_1B6_ecowx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 0110110110 0
}

std::string PPC32Emulator::dasm_7C_1B6_ecowx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "ecowx");
}

uint32_t PPC32Emulator::Assembler::asm_ecowx(const StreamItem& si) {
  return this->asm_7C_s_a_b(si, 0x1B6);
}

void PPC32Emulator::exec_7C_1B7_sthux(uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[rs].u) + this->regs.r[rb].u;
  this->mem->write<phosg::be_uint16_t>(this->regs.debug.addr, this->regs.r[rs].u);
  this->regs.r[ra].u = this->regs.debug.addr;
}

std::string PPC32Emulator::dasm_7C_1B7_sthux(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "sthux", true, true, false);
}

uint32_t PPC32Emulator::Assembler::asm_sthux(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x1B7, true, true, false);
}

void PPC32Emulator::exec_7C_1BC_or(uint32_t op) {
  // 011111 SSSSS AAAAA BBBBB 0110111100 R
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.r[ra].u = this->regs.r[rs].u | this->regs.r[rb].u;
  if (op_get_rec(op)) {
    this->regs.set_crf_int_result(0, this->regs.r[ra].s);
  }
}

std::string PPC32Emulator::dasm_7C_1BC_or(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (rs == rb) {
    return std::format("mr{}       r{}, r{}", op_get_rec(op) ? '.' : ' ', ra, rs);
  } else {
    return PPC32Emulator::dasm_7C_s_a_b_r(op, "or");
  }
}

uint32_t PPC32Emulator::Assembler::asm_or(const StreamItem& si) {
  if (si.op_name.starts_with("mr")) {
    const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
    return 0x7C000378 |
        op_set_reg1(a[1].reg_num) |
        op_set_reg2(a[0].reg_num) |
        op_set_reg3(a[1].reg_num) |
        op_set_rec(si.is_rec());
  } else {
    const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::INT_REGISTER});
    return 0x7C000378 |
        op_set_reg1(a[1].reg_num) |
        op_set_reg2(a[0].reg_num) |
        op_set_reg3(a[2].reg_num) |
        op_set_rec(si.is_rec());
  }
}

void PPC32Emulator::exec_7C_1CB_3CB_divwu(uint32_t op) {
  uint8_t rd=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op);
  uint32_t d=this->regs.r[rb].u;
  this->regs.r[rd].u = d ? (this->regs.r[ra].u / d) : 0;
  if(op&1) this->regs.set_crf_int_result(0, this->regs.r[rd].s);
}

std::string PPC32Emulator::dasm_7C_1CB_3CB_divwu(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "divwu");
}

uint32_t PPC32Emulator::Assembler::asm_divwu(const StreamItem& si) {
  return this->asm_7C_d_a_b_o_r(si, 0x1CB);
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

std::string PPC32Emulator::dasm_7C_1D3_mtspr(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint16_t spr = op_get_spr(op);
  const char* name = name_for_spr(spr);
  if (op & 1) {
    return std::format(".invalid  << mtspr spr{}, r{} with reserved bits set >>", spr, rs);
  } else if (name) {
    return std::format("mt{:<8}r{}", name, rs);
  } else {
    return std::format("mtspr     spr{}, r{}", spr, rs);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mtspr_mnemonic(const StreamItem& si) {
  if (si.op_name == "mtspr") {
    const auto& a = si.check_args({ArgType::SPECIAL_REGISTER, ArgType::INT_REGISTER});
    return 0x7C0003A6 | op_set_reg1(a[1].reg_num) | op_set_spr(a[0].reg_num);
  } else {
    const auto& a = si.check_args({ArgType::INT_REGISTER});
    if (!si.op_name.starts_with("mt")) {
      throw std::logic_error("mtspr mnemonic assembler called for non-mt opcode");
    }
    return 0x7C0003A6 | op_set_reg1(a[0].reg_num) | op_set_spr(spr_for_name(si.op_name.substr(2)));
  }
}

void PPC32Emulator::exec_7C_1D6_dcbi(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 0111010110 0
}

std::string PPC32Emulator::dasm_7C_1D6_dcbi(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcbi");
}

uint32_t PPC32Emulator::Assembler::asm_dcbi(const StreamItem& si) {
  return this->asm_7C_a_b(si, 0x1D6);
}

void PPC32Emulator::exec_7C_1DC_nand(uint32_t op) {
  uint8_t a=op_get_reg1(op),ra=op_get_reg2(op),b=op_get_reg3(op);
  (void)a;(void)b;
  this->regs.r[ra].u = ~(this->regs.r[a].u & this->regs.r[b].u);
  if(op_get_rec(op)) this->regs.set_crf_int_result(0, this->regs.r[ra].s);
}

std::string PPC32Emulator::dasm_7C_1DC_nand(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "nand");
}

uint32_t PPC32Emulator::Assembler::asm_nand(const StreamItem& si) {
  return this->asm_7C_s_a_b_r(si, 0x1DC);
}

void PPC32Emulator::exec_7C_1EB_3EB_divw(uint32_t op) {
  uint8_t rd=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op);
  int32_t n=this->regs.r[ra].s, d=this->regs.r[rb].s;
  this->regs.r[rd].s = (d==0 || (n==INT32_MIN && d==-1)) ? 0 : (n / d);
  if(op&1) this->regs.set_crf_int_result(0, this->regs.r[rd].s);
}

std::string PPC32Emulator::dasm_7C_1EB_3EB_divw(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_d_a_b_o_r(op, "divw");
}

uint32_t PPC32Emulator::Assembler::asm_divw(const StreamItem& si) {
  return this->asm_7C_d_a_b_o_r(si, 0x1EB);
}

void PPC32Emulator::exec_7C_200_mcrxr(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDD 00 00000 00000 1000000000 0
}

std::string PPC32Emulator::dasm_7C_200_mcrxr(DisassemblyState&, uint32_t op) {
  uint8_t crf = op_get_crf1(op);
  if (op & 0x007FF801) {
    return std::format(".invalid  << mcrxr cr{} with reserved bits set >>", crf);
  } else {
    return std::format("mcrxr     cr{}", crf);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mcrxr(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_FIELD});
  return 0x7C000400 | op_set_crf1(a[0].reg_num);
}

void PPC32Emulator::exec_7C_215_lswx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 1000010101 0
}

std::string PPC32Emulator::dasm_7C_215_lswx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lswx", false, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lswx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x215, false, false, false);
}

void PPC32Emulator::exec_7C_216_lwbrx(uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->regs.r[rd].u = this->mem->read<phosg::le_uint32_t>(this->regs.debug.addr);
}

std::string PPC32Emulator::dasm_7C_216_lwbrx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lwbrx", false, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lwbrx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x216, false, false, false);
}

void PPC32Emulator::exec_7C_217_lfsx(uint32_t op) {
  uint8_t d=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op); uint32_t ea=(ra?this->regs.r[ra].u:0)+this->regs.r[rb].u;
  uint32_t bits=this->mem->read<phosg::be_uint32_t>(ea); float fv; memcpy(&fv,&bits,4); this->regs.f[d].f=(double)fv;
}

std::string PPC32Emulator::dasm_7C_217_lfsx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lfsx", false, false, true);
}

uint32_t PPC32Emulator::Assembler::asm_lfsx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x217, false, false, true);
}

void PPC32Emulator::exec_7C_218_srw(uint32_t op) {
  // srw rA, rS, rB: logical shift right of rS by (rB & 0x3F); shift>=32 -> 0.
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  bool rec = op_get_rec(op);
  uint32_t n = this->regs.r[rb].u & 0x3F;
  uint32_t result = (n < 32) ? (this->regs.r[rs].u >> n) : 0;
  this->regs.r[ra].u = result;
  if (rec) {
    this->regs.set_crf_int_result(0, this->regs.r[ra].s);
  }
}

std::string PPC32Emulator::dasm_7C_218_srw(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_s_a_b(op, "srw");
}

uint32_t PPC32Emulator::Assembler::asm_srw(const StreamItem& si) {
  return this->asm_7C_s_a_b(si, 0x218);
}

void PPC32Emulator::exec_7C_236_tlbsync(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 00000 00000 1000110110 0
}

std::string PPC32Emulator::dasm_7C_236_tlbsync(DisassemblyState&, uint32_t op) {
  return (op == 0x7C00046C) ? "tlbsync" : ".invalid  << tlbsync with reserved bits set >>";
}

uint32_t PPC32Emulator::Assembler::asm_tlbsync(const StreamItem&) {
  return 0x7C00046C;
}

void PPC32Emulator::exec_7C_237_lfsux(uint32_t op) {
  uint8_t d=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op); uint32_t ea=(ra?this->regs.r[ra].u:0)+this->regs.r[rb].u;
  uint32_t bits=this->mem->read<phosg::be_uint32_t>(ea); float fv; memcpy(&fv,&bits,4); this->regs.f[d].f=(double)fv;
  if(ra) this->regs.r[ra].u=ea;
}

std::string PPC32Emulator::dasm_7C_237_lfsux(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lfsux", false, true, true);
}

uint32_t PPC32Emulator::Assembler::asm_lfsux(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x237, false, true, true);
}

void PPC32Emulator::exec_7C_253_mfsr(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD 0 RRRR 00000 1001010011 0
}

std::string PPC32Emulator::dasm_7C_253_mfsr(DisassemblyState&, uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t sr = op_get_reg2(op) & 0x0F;
  if (op & 0x0010F801) {
    return std::format(".invalid  << mfsr r{}, {} with reserved bits set >>", rd, sr);
  } else {
    return std::format("mfsr      r{}, {}", rd, sr);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mfsr(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return 0x7C0004A6 | op_set_reg1(a[0].reg_num) | op_set_reg2(this->resolve_immediate(a[1]));
}

void PPC32Emulator::exec_7C_255_lswi(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA NNNNN 1001010101 0
}

std::string PPC32Emulator::dasm_7C_255_lswi(DisassemblyState&, uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t n = op_get_reg3(op);
  if (n == 0) {
    n = 32;
  }
  if (op & 1) {
    return std::format(".invalid  << lswi r{}, r{}, {} with reserved bits set >>", rd, ra, n);
  } else {
    return std::format("lswi      r{}, r{}, {}", rd, ra, n);
  }
}

uint32_t PPC32Emulator::Assembler::asm_lswi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  int64_t a2val = this->resolve_immediate(a[2]);
  return 0x7C0004AA | op_set_reg1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_reg3((a2val == 32) ? 0 : a2val);
}

void PPC32Emulator::exec_7C_256_sync(uint32_t) {
  // 011111 00000 00000 00000 1001010110 0
  // We don't emulate pipelining, so this instruction does nothing.
}

std::string PPC32Emulator::dasm_7C_256_sync(DisassemblyState&, uint32_t op) {
  return (op == 0x7C0004AC) ? "sync" : ".invalid  << sync with reserved bits set >>";
}

uint32_t PPC32Emulator::Assembler::asm_sync(const StreamItem& si) {
  si.check_args({});
  return 0x7C0004AC;
}

void PPC32Emulator::exec_7C_257_lfdx(uint32_t op) {
  uint8_t d=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op); uint32_t ea=(ra?this->regs.r[ra].u:0)+this->regs.r[rb].u;
  uint64_t hi=this->mem->read<phosg::be_uint32_t>(ea), lo=this->mem->read<phosg::be_uint32_t>(ea+4); this->regs.f[d].i=(hi<<32)|lo;
}

std::string PPC32Emulator::dasm_7C_257_lfdx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lfdx", false, false, true);
}

uint32_t PPC32Emulator::Assembler::asm_lfdx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x257, false, false, true);
}

void PPC32Emulator::exec_7C_277_lfdux(uint32_t op) {
  uint8_t d=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op); uint32_t ea=(ra?this->regs.r[ra].u:0)+this->regs.r[rb].u;
  uint64_t hi=this->mem->read<phosg::be_uint32_t>(ea), lo=this->mem->read<phosg::be_uint32_t>(ea+4); this->regs.f[d].i=(hi<<32)|lo;
  if(ra) this->regs.r[ra].u=ea;
}

std::string PPC32Emulator::dasm_7C_277_lfdux(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lfdux", false, true, true);
}

uint32_t PPC32Emulator::Assembler::asm_lfdux(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x277, false, true, true);
}

void PPC32Emulator::exec_7C_293_mfsrin(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD 00000 BBBBB 1010010011 0
}

std::string PPC32Emulator::dasm_7C_293_mfsrin(DisassemblyState&, uint32_t op) {
  uint8_t rd = op_get_reg1(op);
  uint8_t rb = op_get_reg3(op);
  if (op & 0x001F0001) {
    return std::format(".invalid  << mfsrin r{}, r{} with reserved bits set >>", rd, rb);
  } else {
    return std::format("mfsrin    r{}, r{}", rd, rb);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mfsrin(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER});
  return 0x7C000526 | op_set_reg1(a[0].reg_num) | op_set_reg3(a[1].reg_num);
}

void PPC32Emulator::exec_7C_295_stswx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 1010010101 0
}

std::string PPC32Emulator::dasm_7C_295_stswx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "stswx", true, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_stswx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x295, true, false, false);
}

void PPC32Emulator::exec_7C_296_stwbrx(uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + this->regs.r[rb].u;
  this->mem->write<phosg::le_uint32_t>(this->regs.debug.addr, this->regs.r[rs].u);
}

std::string PPC32Emulator::dasm_7C_296_stwbrx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "stwbrx", true, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_stwbrx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x296, true, false, false);
}

void PPC32Emulator::exec_7C_297_stfsx(uint32_t op) {
  uint8_t d=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op); uint32_t ea=(ra?this->regs.r[ra].u:0)+this->regs.r[rb].u;
  float fv=(float)this->regs.f[d].f; uint32_t bits; memcpy(&bits,&fv,4); this->mem->write<phosg::be_uint32_t>(ea,bits);
}

std::string PPC32Emulator::dasm_7C_297_stfsx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "stfsx", true, false, true);
}

uint32_t PPC32Emulator::Assembler::asm_stfsx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x297, true, false, true);
}

void PPC32Emulator::exec_7C_2B7_stfsux(uint32_t op) {
  uint8_t d=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op); uint32_t ea=(ra?this->regs.r[ra].u:0)+this->regs.r[rb].u;
  float fv=(float)this->regs.f[d].f; uint32_t bits; memcpy(&bits,&fv,4); this->mem->write<phosg::be_uint32_t>(ea,bits);
  if(ra) this->regs.r[ra].u=ea;
}

std::string PPC32Emulator::dasm_7C_2B7_stfsux(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "stfsux", true, true, true);
}

uint32_t PPC32Emulator::Assembler::asm_stfsux(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x2B7, true, true, true);
}

void PPC32Emulator::exec_7C_2E5_stswi(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA NNNNN 1011010101 0
}

std::string PPC32Emulator::dasm_7C_2D5_stswi(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t n = op_get_reg3(op);
  if (n == 0) {
    n = 32;
  }
  if (op & 1) {
    return std::format(".invalid  << stswi r{}, r{}, {} with reserved bits set >>", ra, rs, n);
  } else {
    return std::format("stswi     r{}, r{}, {}", ra, rs, n);
  }
}

uint32_t PPC32Emulator::Assembler::asm_stswi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  int64_t a2val = this->resolve_immediate(a[2]);
  return 0x7C0005AA | op_set_reg1(a[1].reg_num) | op_set_reg2(a[0].reg_num) | op_set_reg3((a2val == 32) ? 0 : a2val);
}

void PPC32Emulator::exec_7C_2E7_stfdx(uint32_t op) {
  uint8_t d=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op); uint32_t ea=(ra?this->regs.r[ra].u:0)+this->regs.r[rb].u;
  uint64_t v=this->regs.f[d].i; this->mem->write<phosg::be_uint32_t>(ea,v>>32); this->mem->write<phosg::be_uint32_t>(ea+4,v&0xFFFFFFFF);
}

std::string PPC32Emulator::dasm_7C_2E7_stfdx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "stfdx", true, false, true);
}

uint32_t PPC32Emulator::Assembler::asm_stfdx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x2E7, true, false, true);
}

void PPC32Emulator::exec_7C_2F6_dcba(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 1011110110 0
}

std::string PPC32Emulator::dasm_7C_2F6_dcba(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcba");
}

uint32_t PPC32Emulator::Assembler::asm_dcba(const StreamItem& si) {
  return this->asm_7C_a_b(si, 0x2F6);
}

void PPC32Emulator::exec_7C_2F7_stfdux(uint32_t op) {
  uint8_t d=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op); uint32_t ea=(ra?this->regs.r[ra].u:0)+this->regs.r[rb].u;
  uint64_t v=this->regs.f[d].i; this->mem->write<phosg::be_uint32_t>(ea,v>>32); this->mem->write<phosg::be_uint32_t>(ea+4,v&0xFFFFFFFF);
  if(ra) this->regs.r[ra].u=ea;
}

std::string PPC32Emulator::dasm_7C_2F7_stfdux(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "stfdux", true, true, true);
}

uint32_t PPC32Emulator::Assembler::asm_stfdux(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x2F7, true, true, true);
}

void PPC32Emulator::exec_7C_316_lhbrx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 DDDDD AAAAA BBBBB 1100010110 0
}

std::string PPC32Emulator::dasm_7C_316_lhbrx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "lhbrx", false, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lhbrx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x316, false, false, false);
}

void PPC32Emulator::exec_7C_318_sraw(uint32_t op) {
  // sraw rA, rS, rB: arithmetic shift right of rS by (rB & 0x3F); XER[CA] = rS<0 &&
  // any 1-bits shifted out (the sraw/addze signed-divide idiom depends on CA).
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  bool rec = op_get_rec(op);
  uint32_t n = this->regs.r[rb].u & 0x3F;
  int32_t s = this->regs.r[rs].s;
  uint32_t result; bool ca;
  if (n == 0) { result = (uint32_t)s; ca = false; }
  else if (n >= 32) { result = (s < 0) ? 0xFFFFFFFF : 0; ca = (s < 0); }
  else {
    result = (uint32_t)(s >> n);
    uint32_t mask = (1u << n) - 1;
    ca = (s < 0) && ((this->regs.r[rs].u & mask) != 0);
  }
  this->regs.r[ra].u = result;
  this->regs.xer.set_ca(ca);
  if (rec) {
    this->regs.set_crf_int_result(0, this->regs.r[ra].s);
  }
}

std::string PPC32Emulator::dasm_7C_318_sraw(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_s_a_b_r(op, "sraw");
}

uint32_t PPC32Emulator::Assembler::asm_sraw(const StreamItem& si) {
  return this->asm_7C_s_a_b_r(si, 0x318);
}

void PPC32Emulator::exec_7C_338_srawi(uint32_t op) {
  // 011111 SSSSS AAAAA <<<<< 1100111000 R
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t sh = op_get_reg3(op);
  bool rec = op_get_rec(op);

  uint32_t v = this->regs.r[rs].u;
  if (v & 0x80000000) {
    v = (v >> sh) | (0xFFFFFFFF << (32 - sh));
  } else {
    v = v >> sh;
  }
  this->regs.r[ra].u = v;
  if (rec) {
    this->regs.set_crf_int_result(0, this->regs.r[ra].s);
  }
}

std::string PPC32Emulator::dasm_7C_338_srawi(DisassemblyState&, uint32_t op) {
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t sh = op_get_reg3(op);
  return std::format("srawi{:c}    r{}, r{}, {}", op_get_rec(op) ? '.' : ' ', ra, rs, sh);
}

uint32_t PPC32Emulator::Assembler::asm_srawi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::INT_REGISTER, ArgType::INT_REGISTER, ArgType::IMMEDIATE});
  return 0x7C000670 |
      op_set_reg1(a[1].reg_num) |
      op_set_reg2(a[0].reg_num) |
      op_set_reg3(this->resolve_immediate(a[2])) |
      op_set_rec(si.op_name.ends_with('.'));
}

void PPC32Emulator::exec_7C_356_eieio(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 00000 00000 1101010110 0
}

std::string PPC32Emulator::dasm_7C_356_eieio(DisassemblyState&, uint32_t op) {
  return (op == 0x7C0006AC) ? "eieio" : ".invalid  << eieio with reserved bits set >>";
}

uint32_t PPC32Emulator::Assembler::asm_eieio(const StreamItem& si) {
  si.check_args({});
  return 0x7C0006AC;
}

void PPC32Emulator::exec_7C_396_sthbrx(uint32_t op) {
  this->exec_unimplemented(op); // 011111 SSSSS AAAAA BBBBB 1110010110 0
}

std::string PPC32Emulator::dasm_7C_396_sthbrx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "sthbrx", true, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_sthbrx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x396, true, false, false);
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
    this->regs.set_crf_int_result(0, this->regs.r[ra].s);
  }
}

std::string PPC32Emulator::dasm_7C_39A_extsh(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_s_a_r(op, "extsh");
}

uint32_t PPC32Emulator::Assembler::asm_extsh(const StreamItem& si) {
  return this->asm_7C_s_a_r(si, 0x39A);
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
    this->regs.set_crf_int_result(0, this->regs.r[ra].s);
  }
}

std::string PPC32Emulator::dasm_7C_3BA_extsb(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_s_a_r(op, "extsb");
}

uint32_t PPC32Emulator::Assembler::asm_extsb(const StreamItem& si) {
  return this->asm_7C_s_a_r(si, 0x3BA);
}

void PPC32Emulator::exec_7C_3D6_icbi(uint32_t) {
  // 011111 00000 AAAAA BBBBB 1111010110 0
  // We don't emulate the instruction cache, so we simply ignore this opcode
}

std::string PPC32Emulator::dasm_7C_3D6_icbi(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_a_b(op, "icbi");
}

uint32_t PPC32Emulator::Assembler::asm_icbi(const StreamItem& si) {
  return this->asm_7C_a_b(si, 0x3D6);
}

void PPC32Emulator::exec_7C_3D7_stfiwx(uint32_t op) {
  uint8_t d=op_get_reg1(op),ra=op_get_reg2(op),rb=op_get_reg3(op); uint32_t ea=(ra?this->regs.r[ra].u:0)+this->regs.r[rb].u;
  uint64_t v=this->regs.f[d].i; this->mem->write<phosg::be_uint32_t>(ea,v&0xFFFFFFFF);
}

std::string PPC32Emulator::dasm_7C_3D7_stfiwx(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_lx_stx(op, "stfiwx", true, false, true);
}

uint32_t PPC32Emulator::Assembler::asm_stfiwx(const StreamItem& si) {
  return this->asm_load_store_indexed(si, 0x3D7, true, false, true);
}

void PPC32Emulator::exec_7C_3F6_dcbz(uint32_t op) {
  this->exec_unimplemented(op); // 011111 00000 AAAAA BBBBB 1111110110 0
}

std::string PPC32Emulator::dasm_7C_3F6_dcbz(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_7C_a_b(op, "dcbz");
}

uint32_t PPC32Emulator::Assembler::asm_dcbz(const StreamItem& si) {
  return this->asm_7C_a_b(si, 0x3F6);
}

std::string PPC32Emulator::dasm_memory_reference_imm_offset(const DisassemblyState& s, uint8_t ra, int16_t imm) {
  std::string annotation;
  if ((ra == 2) && (s.import_names != nullptr)) {
    size_t import_index = (imm + 0x8000) / 4;
    if (import_index < s.import_names->size()) {
      annotation = std::format(" /* import {} => {} */", import_index, (*s.import_names)[import_index]);
    }
  }

  if (imm < 0) {
    return std::format("[r{} - 0x{:04X}{}]", ra, -imm, annotation);
  } else if (imm > 0) {
    return std::format("[r{} + 0x{:04X}{}]", ra, imm, annotation);
  } else {
    return std::format("[r{}{}]", ra, annotation);
  }
}

std::string PPC32Emulator::dasm_load_store_imm_u(
    const DisassemblyState& s, uint32_t op, const char* base_name, bool is_store, bool data_reg_is_f) {
  bool u = op_get_u(op);
  uint8_t rsd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_simm(op);

  std::string ret = base_name;
  if (u) {
    ret += 'u';
  }
  ret.resize(10, ' ');

  std::string mem_str = PPC32Emulator::dasm_memory_reference_imm_offset(s, ra, imm);
  char rsd_type = data_reg_is_f ? 'f' : 'r';
  if (is_store) {
    return ret + mem_str + std::format(", {}{}", rsd_type, rsd);
  } else {
    return ret + std::format("{}{}, ", rsd_type, rsd) + mem_str;
  }
}

std::string PPC32Emulator::dasm_load_store_imm(
    const DisassemblyState& s, uint32_t op, const char* base_name, bool is_store) {
  uint8_t rsd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int16_t imm = op_get_simm(op);

  std::string ret = base_name;
  ret.resize(10, ' ');

  std::string mem_str = PPC32Emulator::dasm_memory_reference_imm_offset(s, ra, imm);
  if (is_store) {
    return ret + mem_str + std::format(", r{}", rsd);
  } else {
    return ret + std::format("r{}, ", rsd) + mem_str;
  }
}

uint32_t PPC32Emulator::Assembler::asm_load_store_imm(
    const StreamItem& si, uint32_t base_opcode, bool is_store, bool is_float) {
  auto data_reg_type = is_float ? ArgType::FLOAT_REGISTER : ArgType::INT_REGISTER;
  const Argument* mem_arg;
  const Argument* data_arg;
  if (is_store) {
    const auto& a = si.check_args({ArgType::IMM_MEMORY_REFERENCE, data_reg_type});
    mem_arg = &a[0];
    data_arg = &a[1];
  } else {
    const auto& a = si.check_args({data_reg_type, ArgType::IMM_MEMORY_REFERENCE});
    mem_arg = &a[1];
    data_arg = &a[0];
  }

  return base_opcode |
      op_set_reg1(data_arg->reg_num) |
      op_set_reg2(mem_arg->reg_num) |
      op_set_simm(this->resolve_immediate(*mem_arg));
}

uint32_t PPC32Emulator::Assembler::asm_load_store_indexed(
    const StreamItem& si, uint32_t subopcode, bool is_store, bool is_update, bool is_float) {
  auto data_reg_type = is_float ? ArgType::FLOAT_REGISTER : ArgType::INT_REGISTER;
  const Argument* mem_arg;
  const Argument* data_arg;
  if (is_store) {
    const auto& a = si.check_args({ArgType::REG_MEMORY_REFERENCE, data_reg_type});
    mem_arg = &a[0];
    data_arg = &a[1];
  } else {
    const auto& a = si.check_args({data_reg_type, ArgType::REG_MEMORY_REFERENCE});
    mem_arg = &a[1];
    data_arg = &a[0];
  }

  if (is_update != mem_arg->reg_updated) {
    throw std::runtime_error("invalid memory reference update specification for opcode");
  }

  if (mem_arg->type == ArgType::IMM_MEMORY_REFERENCE) {
    return 0x7C000000 |
        op_set_reg1(data_arg->reg_num) |
        op_set_reg3(mem_arg->reg_num) |
        op_set_subopcode(subopcode);
  } else {
    return 0x7C000000 |
        op_set_reg1(data_arg->reg_num) |
        op_set_reg2(mem_arg->reg_num) |
        op_set_reg3(mem_arg->reg_num2) |
        op_set_subopcode(subopcode);
  }
}

void PPC32Emulator::exec_80_84_lwz_lwzu(uint32_t op) {
  // 10000 U DDDDD AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  // Only the update form (lwzu) is invalid with rA == 0 or rA == rD; plain lwz
  // with rA == 0 is legal absolute addressing.
  if (u && ((ra == 0) || (ra == rd))) {
    throw std::runtime_error("invalid opcode: lwz(u) [r0 + X], rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->regs.r[rd].u = this->mem->read<phosg::be_uint32_t>(this->regs.debug.addr);
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

std::string PPC32Emulator::dasm_80_84_lwz_lwzu(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::dasm_load_store_imm_u(s, op, "lwz", false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lwz(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0x80000000, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lwzu(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0x84000000, false, false);
}

void PPC32Emulator::exec_88_8C_lbz_lbzu(uint32_t op) {
  // 10001 U DDDDD AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (u && ((ra == 0) || (ra == rd))) {
    throw std::runtime_error("invalid opcode: lhau rX, [r0 + Z] or rX == rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->regs.r[rd].u = static_cast<uint32_t>(this->mem->read<uint8_t>(this->regs.debug.addr));
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

std::string PPC32Emulator::dasm_88_8C_lbz_lbzu(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::dasm_load_store_imm_u(s, op, "lbz", false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lbz(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0x88000000, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lbzu(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0x8C000000, false, false);
}

void PPC32Emulator::exec_90_94_stw_stwu(uint32_t op) {
  // 10010 U SSSSS AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (u && (ra == 0)) {
    throw std::runtime_error("invalid opcode: stwu [r0 + X], rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->mem->write<phosg::be_uint32_t>(this->regs.debug.addr, this->regs.r[rs].u);
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

std::string PPC32Emulator::dasm_90_94_stw_stwu(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::dasm_load_store_imm_u(s, op, "stw", true, false);
}

uint32_t PPC32Emulator::Assembler::asm_stw(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0x90000000, true, false);
}

uint32_t PPC32Emulator::Assembler::asm_stwu(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0x94000000, true, false);
}

void PPC32Emulator::exec_98_9C_stb_stbu(uint32_t op) {
  // 10011 U SSSSS AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (u && (ra == 0)) {
    throw std::runtime_error("invalid opcode: stbu [r0 + X], rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->mem->write<uint8_t>(this->regs.debug.addr, this->regs.r[rs].u & 0xFF);
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

std::string PPC32Emulator::dasm_98_9C_stb_stbu(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::dasm_load_store_imm_u(s, op, "stb", true, false);
}

uint32_t PPC32Emulator::Assembler::asm_stb(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0x98000000, true, false);
}

uint32_t PPC32Emulator::Assembler::asm_stbu(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0x9C000000, true, false);
}

void PPC32Emulator::exec_A0_A4_lhz_lhzu(uint32_t op) {
  // 10100 U DDDDD AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (u && ((ra == 0) || (ra == rd))) {
    throw std::runtime_error("invalid opcode: lhzu rX, [r0 + Z] or rX == rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->regs.r[rd].u = static_cast<uint32_t>(this->mem->read<phosg::be_uint16_t>(this->regs.debug.addr));
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

std::string PPC32Emulator::dasm_A0_A4_lhz_lhzu(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::dasm_load_store_imm_u(s, op, "lhz", false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lhz(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xA0000000, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lhzu(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xA4000000, false, false);
}

void PPC32Emulator::exec_A8_AC_lha_lhau(uint32_t op) {
  // 10101 U DDDDD AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (u && ((ra == 0) || (ra == rd))) {
    throw std::runtime_error("invalid opcode: lhau rX, [r0 + Z] or rX == rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->regs.r[rd].s = static_cast<int32_t>(this->mem->read<phosg::be_int16_t>(this->regs.debug.addr));
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

std::string PPC32Emulator::dasm_A8_AC_lha_lhau(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::dasm_load_store_imm_u(s, op, "lha", false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lha(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xA8000000, false, false);
}

uint32_t PPC32Emulator::Assembler::asm_lhau(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xAC000000, false, false);
}

void PPC32Emulator::exec_B0_B4_sth_sthu(uint32_t op) {
  // 10110 U SSSSS AAAAA dddddddddddddddd
  bool u = op_get_u(op);
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (u && (ra == 0)) {
    throw std::runtime_error("invalid opcode: sthu [r0 + X], rY");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  this->mem->write<phosg::be_uint16_t>(this->regs.debug.addr, this->regs.r[rs].u & 0xFFFF);
  if (u) {
    this->regs.r[ra].u = this->regs.debug.addr;
  }
}

std::string PPC32Emulator::dasm_B0_B4_sth_sthu(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::dasm_load_store_imm_u(s, op, "sth", true, false);
}

uint32_t PPC32Emulator::Assembler::asm_sth(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xB0000000, true, false);
}

uint32_t PPC32Emulator::Assembler::asm_sthu(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xB4000000, true, false);
}

void PPC32Emulator::exec_B8_lmw(uint32_t op) {
  // 101110 DDDDD AAAAA dddddddddddddddd
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  if (ra >= rd) {
    throw std::runtime_error("invalid lmw opcode");
  }
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  for (uint32_t addr = this->regs.debug.addr; rd < 32; rd++, addr += 4) {
    this->regs.r[rd].u = this->mem->read<phosg::be_uint32_t>(addr);
  }
}

std::string PPC32Emulator::dasm_B8_lmw(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::dasm_load_store_imm(s, op, "lmw", false);
}

uint32_t PPC32Emulator::Assembler::asm_lmw(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xB8000000, false, false);
}

void PPC32Emulator::exec_BC_stmw(uint32_t op) {
  // 101111 SSSSS AAAAA dddddddddddddddd
  uint8_t rs = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  int32_t imm = op_get_imm_ext(op);
  this->regs.debug.addr = (ra == 0 ? 0 : this->regs.r[ra].u) + imm;
  for (uint32_t addr = this->regs.debug.addr; rs < 32; rs++, addr += 4) {
    this->mem->write<phosg::be_uint32_t>(addr, this->regs.r[rs].u);
  }
}

std::string PPC32Emulator::dasm_BC_stmw(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::dasm_load_store_imm(s, op, "stmw", true);
}

uint32_t PPC32Emulator::Assembler::asm_stmw(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xBC000000, true, false);
}

void PPC32Emulator::exec_C0_C4_lfs_lfsu(uint32_t op) {
  bool u = op_get_u(op);
  uint8_t rd=op_get_reg1(op), ra=op_get_reg2(op); int32_t imm=op_get_imm_ext(op);
  uint32_t ea=(ra==0?0:this->regs.r[ra].u)+imm;
  uint32_t bits=this->mem->read<phosg::be_uint32_t>(ea); float fv; memcpy(&fv,&bits,4);
  this->regs.f[rd].f=(double)fv;
  if(u && ra) this->regs.r[ra].u=ea;
}

std::string PPC32Emulator::dasm_C0_C4_lfs_lfsu(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::dasm_load_store_imm_u(s, op, "lfs", false, true);
}

uint32_t PPC32Emulator::Assembler::asm_lfs(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xC0000000, false, true);
}

uint32_t PPC32Emulator::Assembler::asm_lfsu(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xC4000000, false, true);
}

void PPC32Emulator::exec_C8_CC_lfd_lfdu(uint32_t op) {
  bool u = op_get_u(op);
  uint8_t rd=op_get_reg1(op), ra=op_get_reg2(op); int32_t imm=op_get_imm_ext(op);
  uint32_t ea=(ra==0?0:this->regs.r[ra].u)+imm;
  uint64_t hi=this->mem->read<phosg::be_uint32_t>(ea), lo=this->mem->read<phosg::be_uint32_t>(ea+4);
  this->regs.f[rd].i=(hi<<32)|lo;
  if(u && ra) this->regs.r[ra].u=ea;
}

std::string PPC32Emulator::dasm_C8_CC_lfd_lfdu(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::dasm_load_store_imm_u(s, op, "lfd", false, true);
}

uint32_t PPC32Emulator::Assembler::asm_lfd(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xC8000000, false, true);
}

uint32_t PPC32Emulator::Assembler::asm_lfdu(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xCC000000, false, true);
}

void PPC32Emulator::exec_D0_D4_stfs_stfsu(uint32_t op) {
  bool u = op_get_u(op);
  uint8_t rs=op_get_reg1(op), ra=op_get_reg2(op); int32_t imm=op_get_imm_ext(op);
  uint32_t ea=(ra==0?0:this->regs.r[ra].u)+imm;
  float fv=(float)this->regs.f[rs].f; uint32_t bits; memcpy(&bits,&fv,4);
  this->mem->write<phosg::be_uint32_t>(ea, bits);
  if(u && ra) this->regs.r[ra].u=ea;
}

std::string PPC32Emulator::dasm_D0_D4_stfs_stfsu(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::dasm_load_store_imm_u(s, op, "stfs", true, true);
}

uint32_t PPC32Emulator::Assembler::asm_stfs(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xD0000000, true, true);
}

uint32_t PPC32Emulator::Assembler::asm_stfsu(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xD4000000, true, true);
}

void PPC32Emulator::exec_D8_DC_stfd_stfdu(uint32_t op) {
  bool u = op_get_u(op);
  uint8_t rs=op_get_reg1(op), ra=op_get_reg2(op); int32_t imm=op_get_imm_ext(op);
  uint32_t ea=(ra==0?0:this->regs.r[ra].u)+imm;
  uint64_t v=this->regs.f[rs].i;
  this->mem->write<phosg::be_uint32_t>(ea, v>>32); this->mem->write<phosg::be_uint32_t>(ea+4, v&0xFFFFFFFF);
  if(u && ra) this->regs.r[ra].u=ea;
}

std::string PPC32Emulator::dasm_D8_DC_stfd_stfdu(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::dasm_load_store_imm_u(s, op, "stfd", true, true);
}

uint32_t PPC32Emulator::Assembler::asm_stfd(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xD8000000, true, true);
}

uint32_t PPC32Emulator::Assembler::asm_stfdu(const StreamItem& si) {
  return this->asm_load_store_imm(si, 0xDC000000, true, true);
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
      throw std::runtime_error("invalid EC subopcode");
  }
}

std::string PPC32Emulator::dasm_EC(DisassemblyState& s, uint32_t op) {
  switch (op_get_short_subopcode(op)) {
    case 0x12:
      return PPC32Emulator::dasm_EC_12_fdivs(s, op);
    case 0x14:
      return PPC32Emulator::dasm_EC_14_fsubs(s, op);
    case 0x15:
      return PPC32Emulator::dasm_EC_15_fadds(s, op);
    case 0x16:
      return PPC32Emulator::dasm_EC_16_fsqrts(s, op);
    case 0x18:
      return PPC32Emulator::dasm_EC_18_fres(s, op);
    case 0x19:
      return PPC32Emulator::dasm_EC_19_fmuls(s, op);
    case 0x1C:
      return PPC32Emulator::dasm_EC_1C_fmsubs(s, op);
    case 0x1D:
      return PPC32Emulator::dasm_EC_1D_fmadds(s, op);
    case 0x1E:
      return PPC32Emulator::dasm_EC_1E_fnmsubs(s, op);
    case 0x1F:
      return PPC32Emulator::dasm_EC_1F_fnmadds(s, op);
    default:
      return ".invalid  EC";
  }
}

std::string PPC32Emulator::dasm_EC_FC_d_b_r(uint32_t op, const char* base_name) {
  bool r = op_get_rec(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t rb = op_get_reg3(op);
  std::string name = base_name;
  if (r) {
    name += '.';
  }
  if (op & 0x001F07C0) {
    return std::format(".invalid  << {} f{}, f{} with reserved bits set >>", name, rd, rb);
  } else {
    return std::format("{:<10}f{}, f{}", name, rd, rb);
  }
}

std::string PPC32Emulator::dasm_EC_FC_d_a_b_r(uint32_t op, const char* base_name) {
  bool r = op_get_rec(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  std::string name = base_name;
  if (r) {
    name += '.';
  }
  if (op & 0x000007C0) {
    return std::format(".invalid  << {} f{}, f{}, f{} with reserved bits set >>", name, rd, ra, rb);
  } else {
    return std::format("{:<10}f{}, f{}, f{}", name, rd, ra, rb);
  }
}

std::string PPC32Emulator::dasm_EC_FC_d_a_c_r(uint32_t op, const char* base_name) {
  bool r = op_get_rec(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rc = op_get_reg4(op);
  std::string name = base_name;
  if (r) {
    name += '.';
  }
  if (op & 0x0000F800) {
    return std::format(".invalid  << {} f{}, f{}, f{} with reserved bits set >>", name, rd, ra, rc);
  } else {
    return std::format("{:<10}f{}, f{}, f{}", name, rd, ra, rc);
  }
}

std::string PPC32Emulator::dasm_EC_FC_d_a_b_c_r(uint32_t op, const char* base_name) {
  bool r = op_get_rec(op);
  uint8_t rd = op_get_reg1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  uint8_t rc = op_get_reg4(op);
  std::string ret = base_name;
  if (r) {
    ret += '.';
  }
  ret.resize(10, ' ');
  return ret + std::format("f{}, f{}, f{}, f{}", rd, ra, rb, rc);
}

void PPC32Emulator::exec_EC_12_fdivs(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = (float)(this->regs.f[op_get_reg2(op)].f / this->regs.f[op_get_reg3(op)].f);
}

std::string PPC32Emulator::dasm_EC_12_fdivs(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_r(op, "fdivs");
}

uint32_t PPC32Emulator::Assembler::asm_fdivs(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xEC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, 0x00, 0x12, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_EC_14_fsubs(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = (float)(this->regs.f[op_get_reg2(op)].f - this->regs.f[op_get_reg3(op)].f);
}

std::string PPC32Emulator::dasm_EC_14_fsubs(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_r(op, "fsubs");
}

uint32_t PPC32Emulator::Assembler::asm_fsubs(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xEC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, 0x00, 0x14, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_EC_15_fadds(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = (float)(this->regs.f[op_get_reg2(op)].f + this->regs.f[op_get_reg3(op)].f);
}

std::string PPC32Emulator::dasm_EC_15_fadds(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_r(op, "fadds");
}

uint32_t PPC32Emulator::Assembler::asm_fadds(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xEC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, 0x00, 0x15, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_EC_16_fsqrts(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = (float)sqrt(this->regs.f[op_get_reg3(op)].f);
}

std::string PPC32Emulator::dasm_EC_16_fsqrts(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fsqrts");
}

uint32_t PPC32Emulator::Assembler::asm_fsqrts(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xEC000000, a[0].reg_num, 0x00, a[1].reg_num, 0x00, 0x16, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_EC_18_fres(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = (float)(1.0/this->regs.f[op_get_reg3(op)].f);
}

std::string PPC32Emulator::dasm_EC_18_fres(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fres");
}

uint32_t PPC32Emulator::Assembler::asm_fres(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xEC000000, a[0].reg_num, 0x00, a[1].reg_num, 0x00, 0x18, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_EC_19_fmuls(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = (float)(this->regs.f[op_get_reg2(op)].f * this->regs.f[(op>>6)&0x1F].f);
}

std::string PPC32Emulator::dasm_EC_19_fmuls(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_c_r(op, "fmuls");
}

uint32_t PPC32Emulator::Assembler::asm_fmuls(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xEC000000, a[0].reg_num, a[1].reg_num, 0x00, a[2].reg_num, 0x19, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_EC_1C_fmsubs(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = (float)(this->regs.f[op_get_reg2(op)].f*this->regs.f[(op>>6)&0x1F].f - this->regs.f[op_get_reg3(op)].f);
}

std::string PPC32Emulator::dasm_EC_1C_fmsubs(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fmsubs");
}

uint32_t PPC32Emulator::Assembler::asm_fmsubs(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(
      0xEC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, a[3].reg_num, 0x1C, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_EC_1D_fmadds(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = (float)(this->regs.f[op_get_reg2(op)].f*this->regs.f[(op>>6)&0x1F].f + this->regs.f[op_get_reg3(op)].f);
}

std::string PPC32Emulator::dasm_EC_1D_fmadds(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fmadds");
}

uint32_t PPC32Emulator::Assembler::asm_fmadds(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(
      0xEC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, a[3].reg_num, 0x1D, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_EC_1E_fnmsubs(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = (float)(-(this->regs.f[op_get_reg2(op)].f*this->regs.f[(op>>6)&0x1F].f - this->regs.f[op_get_reg3(op)].f));
}

std::string PPC32Emulator::dasm_EC_1E_fnmsubs(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fnmsubs");
}

uint32_t PPC32Emulator::Assembler::asm_fnmsubs(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(
      0xEC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, a[3].reg_num, 0x1E, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_EC_1F_fnmadds(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = (float)(-(this->regs.f[op_get_reg2(op)].f*this->regs.f[(op>>6)&0x1F].f + this->regs.f[op_get_reg3(op)].f));
}

std::string PPC32Emulator::dasm_EC_1F_fnmadds(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fnmadds");
}

uint32_t PPC32Emulator::Assembler::asm_fnmadds(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(
      0xEC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, a[3].reg_num, 0x1F, si.op_name.ends_with("."));
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
        throw std::runtime_error("invalid FC subopcode");
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
        throw std::runtime_error("invalid FC subopcode");
    }
  }
}

std::string PPC32Emulator::dasm_FC(DisassemblyState& s, uint32_t op) {
  uint8_t short_sub = op_get_short_subopcode(op);
  if (short_sub & 0x10) {
    switch (short_sub) {
      case 0x12:
        return PPC32Emulator::dasm_FC_12_fdiv(s, op);
      case 0x14:
        return PPC32Emulator::dasm_FC_14_fsub(s, op);
      case 0x15:
        return PPC32Emulator::dasm_FC_15_fadd(s, op);
      case 0x16:
        return PPC32Emulator::dasm_FC_16_fsqrt(s, op);
      case 0x17:
        return PPC32Emulator::dasm_FC_17_fsel(s, op);
      case 0x19:
        return PPC32Emulator::dasm_FC_19_fmul(s, op);
      case 0x1A:
        return PPC32Emulator::dasm_FC_1A_frsqrte(s, op);
      case 0x1C:
        return PPC32Emulator::dasm_FC_1C_fmsub(s, op);
      case 0x1D:
        return PPC32Emulator::dasm_FC_1D_fmadd(s, op);
      case 0x1E:
        return PPC32Emulator::dasm_FC_1E_fnmsub(s, op);
      case 0x1F:
        return PPC32Emulator::dasm_FC_1F_fnmadd(s, op);
      default:
        return ".invalid  FC, 1";
    }
  } else {
    switch (op_get_subopcode(op)) {
      case 0x000:
        return PPC32Emulator::dasm_FC_000_fcmpu(s, op);
      case 0x00C:
        return PPC32Emulator::dasm_FC_00C_frsp(s, op);
      case 0x00E:
        return PPC32Emulator::dasm_FC_00E_fctiw(s, op);
      case 0x00F:
        return PPC32Emulator::dasm_FC_00F_fctiwz(s, op);
      case 0x020:
        return PPC32Emulator::dasm_FC_020_fcmpo(s, op);
      case 0x026:
        return PPC32Emulator::dasm_FC_026_mtfsb1(s, op);
      case 0x028:
        return PPC32Emulator::dasm_FC_028_fneg(s, op);
      case 0x040:
        return PPC32Emulator::dasm_FC_040_mcrfs(s, op);
      case 0x046:
        return PPC32Emulator::dasm_FC_046_mtfsb0(s, op);
      case 0x048:
        return PPC32Emulator::dasm_FC_048_fmr(s, op);
      case 0x086:
        return PPC32Emulator::dasm_FC_086_mtfsfi(s, op);
      case 0x088:
        return PPC32Emulator::dasm_FC_088_fnabs(s, op);
      case 0x108:
        return PPC32Emulator::dasm_FC_108_fabs(s, op);
      case 0x247:
        return PPC32Emulator::dasm_FC_247_mffs(s, op);
      case 0x2C7:
        return PPC32Emulator::dasm_FC_2C7_mtfsf(s, op);
      default:
        return ".invalid  FC, 0";
    }
  }
}

void PPC32Emulator::exec_FC_12_fdiv(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = this->regs.f[op_get_reg2(op)].f / this->regs.f[op_get_reg3(op)].f;
}

std::string PPC32Emulator::dasm_FC_12_fdiv(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_r(op, "fdiv");
}

uint32_t PPC32Emulator::Assembler::asm_fdiv(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, 0x00, 0x12, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_14_fsub(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = this->regs.f[op_get_reg2(op)].f - this->regs.f[op_get_reg3(op)].f;
}

std::string PPC32Emulator::dasm_FC_14_fsub(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_r(op, "fsub");
}

uint32_t PPC32Emulator::Assembler::asm_fsub(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, 0x00, 0x14, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_15_fadd(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = this->regs.f[op_get_reg2(op)].f + this->regs.f[op_get_reg3(op)].f;
}

std::string PPC32Emulator::dasm_FC_15_fadd(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_r(op, "fadd");
}

uint32_t PPC32Emulator::Assembler::asm_fadd(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, 0x00, 0x15, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_16_fsqrt(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = sqrt(this->regs.f[op_get_reg3(op)].f);
}

std::string PPC32Emulator::dasm_FC_16_fsqrt(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fsqrt");
}

uint32_t PPC32Emulator::Assembler::asm_fsqrt(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, 0x00, a[1].reg_num, 0x00, 0x16, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_17_fsel(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = (this->regs.f[op_get_reg2(op)].f>=0.0)?this->regs.f[(op>>6)&0x1F].f:this->regs.f[op_get_reg3(op)].f;
}

std::string PPC32Emulator::dasm_FC_17_fsel(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fsel");
}

uint32_t PPC32Emulator::Assembler::asm_fsel(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(
      0xFC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, a[3].reg_num, 0x17, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_19_fmul(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = this->regs.f[op_get_reg2(op)].f * this->regs.f[(op>>6)&0x1F].f;
}

std::string PPC32Emulator::dasm_FC_19_fmul(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_c_r(op, "fmul");
}

uint32_t PPC32Emulator::Assembler::asm_fmul(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, a[1].reg_num, 0x00, a[2].reg_num, 0x19, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_1A_frsqrte(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = 1.0/sqrt(this->regs.f[op_get_reg3(op)].f);
}

std::string PPC32Emulator::dasm_FC_1A_frsqrte(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "frsqrte");
}

uint32_t PPC32Emulator::Assembler::asm_frsqrte(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, 0x00, a[1].reg_num, 0x00, 0x1A, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_1C_fmsub(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = this->regs.f[op_get_reg2(op)].f*this->regs.f[(op>>6)&0x1F].f - this->regs.f[op_get_reg3(op)].f;
}

std::string PPC32Emulator::dasm_FC_1C_fmsub(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fmsub");
}

uint32_t PPC32Emulator::Assembler::asm_fmsub(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(
      0xFC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, a[3].reg_num, 0x1C, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_1D_fmadd(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = this->regs.f[op_get_reg2(op)].f*this->regs.f[(op>>6)&0x1F].f + this->regs.f[op_get_reg3(op)].f;
}

std::string PPC32Emulator::dasm_FC_1D_fmadd(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fmadd");
}

uint32_t PPC32Emulator::Assembler::asm_fmadd(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(
      0xFC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, a[3].reg_num, 0x1D, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_1E_fnmsub(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = -(this->regs.f[op_get_reg2(op)].f*this->regs.f[(op>>6)&0x1F].f - this->regs.f[op_get_reg3(op)].f);
}

std::string PPC32Emulator::dasm_FC_1E_fnmsub(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fnmsub");
}

uint32_t PPC32Emulator::Assembler::asm_fnmsub(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(
      0xFC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, a[3].reg_num, 0x1E, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_1F_fnmadd(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = -(this->regs.f[op_get_reg2(op)].f*this->regs.f[(op>>6)&0x1F].f + this->regs.f[op_get_reg3(op)].f);
}

std::string PPC32Emulator::dasm_FC_1F_fnmadd(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_a_b_c_r(op, "fnmadd");
}

uint32_t PPC32Emulator::Assembler::asm_fnmadd(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(
      0xFC000000, a[0].reg_num, a[1].reg_num, a[2].reg_num, a[3].reg_num, 0x1F, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_000_fcmpu(uint32_t op) {
  { uint8_t crf=op_get_crf1(op); double a=this->regs.f[op_get_reg2(op)].f, b=this->regs.f[op_get_reg3(op)].f;
    uint8_t c; if(a<b)c=8; else if(a>b)c=4; else if(a==b)c=2; else c=1;
    this->regs.cr.replace_field(crf,c); }
}

std::string PPC32Emulator::dasm_FC_000_fcmpu(DisassemblyState&, uint32_t op) {
  uint8_t crf = op_get_crf1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (op & 0x00600001) {
    return std::format(".invalid  << fcmpu cr{}, f{}, f{} with reserved bits set >>", crf, ra, rb);
  } else if (crf) {
    return std::format("fcmpu     cr{}, f{}, f{}", crf, ra, rb);
  } else {
    return std::format("fcmpu     f{}, f{}", ra, rb);
  }
}

uint32_t PPC32Emulator::Assembler::asm_fcmpu(const StreamItem& si) {
  if (si.args.size() == 3) {
    const auto& a = si.check_args({ArgType::CONDITION_FIELD, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
    return 0xFC000000 | op_set_crf1(a[0].reg_num) | op_set_reg2(a[1].reg_num) | op_set_reg3(a[2].reg_num);
  } else {
    const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
    return 0xFC000000 | op_set_crf1(0) | op_set_reg2(a[0].reg_num) | op_set_reg3(a[1].reg_num);
  }
}

void PPC32Emulator::exec_FC_00C_frsp(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = (double)(float)this->regs.f[op_get_reg3(op)].f;
}

std::string PPC32Emulator::dasm_FC_00C_frsp(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "frsp");
}

uint32_t PPC32Emulator::Assembler::asm_frsp(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, 0x00, a[1].reg_num, 0x00, 0x0C, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_00E_fctiw(uint32_t op) {
  { double v=this->regs.f[op_get_reg3(op)].f; int32_t r=(int32_t)llround(v); this->regs.f[op_get_reg1(op)].i=(uint32_t)r; }
}

std::string PPC32Emulator::dasm_FC_00E_fctiw(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fctiw");
}

uint32_t PPC32Emulator::Assembler::asm_fctiw(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, 0x00, a[1].reg_num, 0x00, 0x0E, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_00F_fctiwz(uint32_t op) {
  { double v=this->regs.f[op_get_reg3(op)].f; int32_t r=(int32_t)v; this->regs.f[op_get_reg1(op)].i=(uint32_t)r; }
}

std::string PPC32Emulator::dasm_FC_00F_fctiwz(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fctiwz");
}

uint32_t PPC32Emulator::Assembler::asm_fctiwz(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, 0x00, a[1].reg_num, 0x00, 0x0F, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_020_fcmpo(uint32_t op) {
  { uint8_t crf=op_get_crf1(op); double a=this->regs.f[op_get_reg2(op)].f, b=this->regs.f[op_get_reg3(op)].f;
    uint8_t c; if(a<b)c=8; else if(a>b)c=4; else if(a==b)c=2; else c=1;
    this->regs.cr.replace_field(crf,c); }
}

std::string PPC32Emulator::dasm_FC_020_fcmpo(DisassemblyState&, uint32_t op) {
  uint8_t crf = op_get_crf1(op);
  uint8_t ra = op_get_reg2(op);
  uint8_t rb = op_get_reg3(op);
  if (op & 0x00600001) {
    return std::format(".invalid  << fcmpo cr{}, f{}, f{} with reserved bits set >>", crf, ra, rb);
  } else if (crf) {
    return std::format("fcmpo     cr{}, f{}, f{}", crf, ra, rb);
  } else {
    return std::format("fcmpo     f{}, f{}", ra, rb);
  }
}

uint32_t PPC32Emulator::Assembler::asm_fcmpo(const StreamItem& si) {
  if (si.args.size() == 3) {
    const auto& a = si.check_args({ArgType::CONDITION_FIELD, ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
    return 0xFC000000 |
        op_set_crf1(a[0].reg_num) |
        op_set_reg2(a[1].reg_num) |
        op_set_reg3(a[2].reg_num) |
        op_set_reg4(1);
  } else {
    const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
    return 0xFC000000 | op_set_crf1(0) | op_set_reg2(a[0].reg_num) | op_set_reg3(a[1].reg_num) | op_set_reg4(1);
  }
}

void PPC32Emulator::exec_FC_026_mtfsb1(uint32_t op) {
  (void)op; // FPSCR bit set: no-op for host math
}

std::string PPC32Emulator::dasm_FC_026_mtfsb1(DisassemblyState&, uint32_t op) {
  bool rec = op_get_rec(op);
  uint8_t crb = op_get_reg1(op);
  if (op & 0x001FF800) {
    return std::format(".invalid  << mtfsb1{} crb{} with reserved bits set >>", rec ? '.' : ' ', crb);
  } else {
    return std::format("mtfsb1{}   crb{}", rec ? '.' : ' ', crb);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mtfsb1(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_BIT});
  return this->asm_5reg(0xFC000000, a[0].reg_num, 0x00, 0x00, 0x01, 0x06, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_028_fneg(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = -this->regs.f[op_get_reg3(op)].f;
}

std::string PPC32Emulator::dasm_FC_028_fneg(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fneg");
}

uint32_t PPC32Emulator::Assembler::asm_fneg(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, 0x00, a[1].reg_num, 0x01, 0x08, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_040_mcrfs(uint32_t op) {
  this->regs.cr.replace_field(op_get_crf1(op), 0);
}

std::string PPC32Emulator::dasm_FC_040_mcrfs(DisassemblyState&, uint32_t op) {
  uint8_t crf = op_get_crf1(op);
  uint8_t fpscrf = op_get_crf2(op);
  if (op & 0x0063F801) {
    return std::format(".invalid  << mcrfs cr{}, cr{} with reserved bits set >>", crf, fpscrf);
  } else {
    return std::format("mcrfs     cr{}, cr{}", crf, fpscrf);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mcrfs(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_FIELD, ArgType::CONDITION_FIELD});
  return this->asm_5reg(0xFC000000, a[0].reg_num << 2, a[1].reg_num << 2, 0x00, 0x02, 0x00, false);
}

void PPC32Emulator::exec_FC_046_mtfsb0(uint32_t op) {
  (void)op; // FPSCR bit clear: no-op for host math
}

std::string PPC32Emulator::dasm_FC_046_mtfsb0(DisassemblyState&, uint32_t op) {
  bool rec = op_get_rec(op);
  uint8_t crb = op_get_reg1(op);
  if (op & 0x001FF800) {
    return std::format(".invalid  << mtfsb0{} crb{} with reserved bits set >>", rec ? '.' : ' ', crb);
  } else {
    return std::format("mtfsb0{}   crb{}", rec ? '.' : ' ', crb);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mtfsb0(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_BIT});
  return this->asm_5reg(0xFC000000, a[0].reg_num, 0x00, 0x00, 0x02, 0x06, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_048_fmr(uint32_t op) {
  this->regs.f[op_get_reg1(op)].i = this->regs.f[op_get_reg3(op)].i;
}

std::string PPC32Emulator::dasm_FC_048_fmr(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fmr");
}

uint32_t PPC32Emulator::Assembler::asm_fmr(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, 0x00, a[1].reg_num, 0x02, 0x08, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_086_mtfsfi(uint32_t op) {
  (void)op; // FPSCR field immediate: no-op
}

std::string PPC32Emulator::dasm_FC_086_mtfsfi(DisassemblyState&, uint32_t op) {
  bool rec = op_get_rec(op);
  uint8_t crf = op_get_crf1(op);
  uint8_t imm = (op >> 12) & 0x0F;
  if (op & 0x007F0800) {
    return std::format(".invalid  << mtfsfi{} cr{}, 0x{:X} with reserved bits set >>", rec ? '.' : ' ', crf, imm);
  } else {
    return std::format("mtfsfi{}   cr{}, 0x{:X}", rec ? '.' : ' ', crf, imm);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mtfsfi(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::CONDITION_FIELD, ArgType::IMMEDIATE});
  return this->asm_5reg(
      0xFC000000, a[0].reg_num << 2, 0x00, this->resolve_immediate(a[1]) << 1, 0x04, 0x06, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_088_fnabs(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = -fabs(this->regs.f[op_get_reg3(op)].f);
}

std::string PPC32Emulator::dasm_FC_088_fnabs(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fnabs");
}

uint32_t PPC32Emulator::Assembler::asm_fnabs(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, 0x00, a[1].reg_num, 0x04, 0x08, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_108_fabs(uint32_t op) {
  this->regs.f[op_get_reg1(op)].f = fabs(this->regs.f[op_get_reg3(op)].f);
}

std::string PPC32Emulator::dasm_FC_108_fabs(DisassemblyState&, uint32_t op) {
  return PPC32Emulator::dasm_EC_FC_d_b_r(op, "fabs");
}

uint32_t PPC32Emulator::Assembler::asm_fabs(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER, ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, 0x00, a[1].reg_num, 0x08, 0x08, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_247_mffs(uint32_t op) {
  this->regs.f[op_get_reg1(op)].i = this->regs.fpscr;
}

std::string PPC32Emulator::dasm_FC_247_mffs(DisassemblyState&, uint32_t op) {
  bool rec = op_get_rec(op);
  uint8_t rd = op_get_reg1(op);
  if (op & 0x001FF800) {
    return std::format(".invalid  << mffs{} f{} with reserved bits set >>", rec ? '.' : ' ', rd);
  } else {
    return std::format("mffs{}     f{}", rec ? '.' : ' ', rd);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mffs(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::FLOAT_REGISTER});
  return this->asm_5reg(0xFC000000, a[0].reg_num, 0x00, 0x00, 0x12, 0x07, si.op_name.ends_with("."));
}

void PPC32Emulator::exec_FC_2C7_mtfsf(uint32_t op) {
  this->regs.fpscr = (uint32_t)this->regs.f[op_get_reg3(op)].i;
}

std::string PPC32Emulator::dasm_FC_2C7_mtfsf(DisassemblyState&, uint32_t op) {
  bool rec = op_get_rec(op);
  uint8_t rb = op_get_reg3(op);
  uint8_t fm = (op >> 17) & 0xFF;
  if (op & 0x02010000) {
    return std::format(".invalid  << mtfsf{} 0x{:02X}, f{} with reserved bits set >>", rec ? '.' : ' ', fm, rb);
  } else {
    return std::format("mtfsf{}    0x{:02X}, f{}", rec ? '.' : ' ', fm, rb);
  }
}

uint32_t PPC32Emulator::Assembler::asm_mtfsf(const StreamItem& si) {
  const auto& a = si.check_args({ArgType::IMMEDIATE, ArgType::FLOAT_REGISTER});
  return 0xFC00058E | ((this->resolve_immediate(a[0]) & 0xFF) << 17) | op_set_reg3(a[1].reg_num) | op_set_rec(si.is_rec());
}

uint32_t PPC32Emulator::Assembler::asm_data(const StreamItem& si) {
  si.check_args({ArgType::IMMEDIATE});
  return this->resolve_immediate(si.args[0]);
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
    /* 50 */ {&PPC32Emulator::exec_50_54_rlwimi_rlwinm, &PPC32Emulator::dasm_50_rlwimi},
    /* 54 */ {&PPC32Emulator::exec_50_54_rlwimi_rlwinm, &PPC32Emulator::dasm_54_rlwinm},
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

const std::unordered_map<std::string, PPC32Emulator::Assembler::AssembleFunction>
    PPC32Emulator::Assembler::assemble_functions = {
        {"twi", &PPC32Emulator::Assembler::asm_twi},
        {"mulli", &PPC32Emulator::Assembler::asm_mulli},
        {"subfic", &PPC32Emulator::Assembler::asm_subfic},
        {"cmpli", &PPC32Emulator::Assembler::asm_cmpli_cmplwi},
        {"cmplwi", &PPC32Emulator::Assembler::asm_cmpli_cmplwi},
        {"cmpi", &PPC32Emulator::Assembler::asm_cmpi_cmpwi},
        {"cmpwi", &PPC32Emulator::Assembler::asm_cmpi_cmpwi},
        {"addic", &PPC32Emulator::Assembler::asm_addic_subic},
        {"addic.", &PPC32Emulator::Assembler::asm_addic_subic},
        {"subic", &PPC32Emulator::Assembler::asm_addic_subic},
        {"subic.", &PPC32Emulator::Assembler::asm_addic_subic},
        {"li", &PPC32Emulator::Assembler::asm_li_lis},
        {"lis", &PPC32Emulator::Assembler::asm_li_lis},
        {"addi", &PPC32Emulator::Assembler::asm_addi_subi_addis_subis},
        {"subi", &PPC32Emulator::Assembler::asm_addi_subi_addis_subis},
        {"addis", &PPC32Emulator::Assembler::asm_addi_subi_addis_subis},
        {"subis", &PPC32Emulator::Assembler::asm_addi_subi_addis_subis},
        {"bc", &PPC32Emulator::Assembler::asm_bc},
        {"bcl", &PPC32Emulator::Assembler::asm_bc},
        {"bca", &PPC32Emulator::Assembler::asm_bc},
        {"bcla", &PPC32Emulator::Assembler::asm_bc},
        {"bge", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"ble", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bne", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bns", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"blt", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bgt", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"beq", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bso", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdnz", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdnzf", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdnzt", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdz", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdzf", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdzt", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bgea", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"blea", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bnea", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bnsa", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"blta", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bgta", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"beqa", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bsoa", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdnza", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdnzfa", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdnzta", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdza", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdzfa", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdzta", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bgel", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"blel", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bnel", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bnsl", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bltl", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bgtl", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"beql", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bsol", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdnzl", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdnzfl", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdnztl", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdzl", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdzfl", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdztl", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bgela", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"blela", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bnela", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bnsla", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bltla", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bgtla", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"beqla", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bsola", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdnzla", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdnzfla", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdnztla", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdzla", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdzfla", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"bdztla", &PPC32Emulator::Assembler::asm_bc_mnemonic},
        {"sc", &PPC32Emulator::Assembler::asm_sc},
        {"b", &PPC32Emulator::Assembler::asm_b_mnemonic},
        {"bl", &PPC32Emulator::Assembler::asm_b_mnemonic},
        {"ba", &PPC32Emulator::Assembler::asm_b_mnemonic},
        {"bla", &PPC32Emulator::Assembler::asm_b_mnemonic},
        {"mcrf", &PPC32Emulator::Assembler::asm_mcrf},
        {"bclr", &PPC32Emulator::Assembler::asm_bclr_bcctr},
        {"bclrl", &PPC32Emulator::Assembler::asm_bclr_bcctr},
        {"bgelr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"blelr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bnelr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bnslr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bltlr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bgtlr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"beqlr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bsolr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdnzlr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdnzflr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdnztlr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdzlr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdzflr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdztlr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"blr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bgelrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"blelrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bnelrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bnslrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bltlrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bgtlrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"beqlrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bsolrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdnzlrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdnzflrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdnztlrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdzlrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdzflrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdztlrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"blrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"crnor", &PPC32Emulator::Assembler::asm_crnor},
        {"rfi", &PPC32Emulator::Assembler::asm_rfi},
        {"crandc", &PPC32Emulator::Assembler::asm_crandc},
        {"isync", &PPC32Emulator::Assembler::asm_isync},
        {"crxor", &PPC32Emulator::Assembler::asm_crxor},
        {"crnand", &PPC32Emulator::Assembler::asm_crnand},
        {"crand", &PPC32Emulator::Assembler::asm_crand},
        {"creqv", &PPC32Emulator::Assembler::asm_creqv},
        {"crorc", &PPC32Emulator::Assembler::asm_crorc},
        {"cror", &PPC32Emulator::Assembler::asm_cror},
        {"bcctr", &PPC32Emulator::Assembler::asm_bclr_bcctr},
        {"bcctrl", &PPC32Emulator::Assembler::asm_bclr_bcctr},
        {"bgectr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"blectr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bnectr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bnsctr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bltctr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bgtctr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"beqctr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bsoctr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdnzctr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdnzfctr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdnztctr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdzctr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdzfctr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdztctr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bctr", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bgectrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"blectrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bnectrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bnsctrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bltctrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bgtctrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"beqctrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bsoctrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdnzctrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdnzfctrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdnztctrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdzctrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdzfctrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bdztctrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"bctrl", &PPC32Emulator::Assembler::asm_bclr_bcctr_mnemonic},
        {"rlwimi", &PPC32Emulator::Assembler::asm_rlwimi},
        {"rlwimi.", &PPC32Emulator::Assembler::asm_rlwimi},
        {"inslwi", &PPC32Emulator::Assembler::asm_inslwi},
        {"inslwi.", &PPC32Emulator::Assembler::asm_inslwi},
        {"insrwi", &PPC32Emulator::Assembler::asm_insrwi},
        {"insrwi.", &PPC32Emulator::Assembler::asm_insrwi},
        {"rlwinm", &PPC32Emulator::Assembler::asm_rlwinm},
        {"rlwinm.", &PPC32Emulator::Assembler::asm_rlwinm},
        {"extlwi", &PPC32Emulator::Assembler::asm_extlwi},
        {"extlwi.", &PPC32Emulator::Assembler::asm_extlwi},
        {"extrwi", &PPC32Emulator::Assembler::asm_extrwi},
        {"extrwi.", &PPC32Emulator::Assembler::asm_extrwi},
        {"rotlwi", &PPC32Emulator::Assembler::asm_rotlwi},
        {"rotlwi.", &PPC32Emulator::Assembler::asm_rotlwi},
        {"rotrwi", &PPC32Emulator::Assembler::asm_rotrwi},
        {"rotrwi.", &PPC32Emulator::Assembler::asm_rotrwi},
        {"slwi", &PPC32Emulator::Assembler::asm_slwi},
        {"slwi.", &PPC32Emulator::Assembler::asm_slwi},
        {"srwi", &PPC32Emulator::Assembler::asm_srwi},
        {"srwi.", &PPC32Emulator::Assembler::asm_srwi},
        {"clrlwi", &PPC32Emulator::Assembler::asm_clrlwi},
        {"clrlwi.", &PPC32Emulator::Assembler::asm_clrlwi},
        {"clrrwi", &PPC32Emulator::Assembler::asm_clrrwi},
        {"clrrwi.", &PPC32Emulator::Assembler::asm_clrrwi},
        {"clrlslwi", &PPC32Emulator::Assembler::asm_clrlslwi},
        {"clrlslwi.", &PPC32Emulator::Assembler::asm_clrlslwi},
        {"rlwnm", &PPC32Emulator::Assembler::asm_rlwnm},
        {"rlwnm.", &PPC32Emulator::Assembler::asm_rlwnm},
        {"rotlw", &PPC32Emulator::Assembler::asm_rotlw},
        {"rotlw.", &PPC32Emulator::Assembler::asm_rotlw},
        {"nop", &PPC32Emulator::Assembler::asm_nop},
        {"ori", &PPC32Emulator::Assembler::asm_ori},
        {"oris", &PPC32Emulator::Assembler::asm_oris},
        {"xori", &PPC32Emulator::Assembler::asm_xori},
        {"xoris", &PPC32Emulator::Assembler::asm_xoris},
        {"andi.", &PPC32Emulator::Assembler::asm_andi_rec},
        {"andis.", &PPC32Emulator::Assembler::asm_andis_rec},
        {"cmp", &PPC32Emulator::Assembler::asm_cmp},
        {"tw", &PPC32Emulator::Assembler::asm_tw},
        {"subfc", &PPC32Emulator::Assembler::asm_subfc},
        {"subfco", &PPC32Emulator::Assembler::asm_subfc},
        {"subfc.", &PPC32Emulator::Assembler::asm_subfc},
        {"subfco.", &PPC32Emulator::Assembler::asm_subfc},
        {"addc", &PPC32Emulator::Assembler::asm_addc},
        {"addco", &PPC32Emulator::Assembler::asm_addc},
        {"addc.", &PPC32Emulator::Assembler::asm_addc},
        {"addco.", &PPC32Emulator::Assembler::asm_addc},
        {"mulhwu", &PPC32Emulator::Assembler::asm_mulhwu},
        {"mulhwu.", &PPC32Emulator::Assembler::asm_mulhwu},
        {"mfcr", &PPC32Emulator::Assembler::asm_mfcr},
        {"lwarx", &PPC32Emulator::Assembler::asm_lwarx},
        {"lwzx", &PPC32Emulator::Assembler::asm_lwzx},
        {"slw", &PPC32Emulator::Assembler::asm_slw},
        {"slw.", &PPC32Emulator::Assembler::asm_slw},
        {"cntlzw", &PPC32Emulator::Assembler::asm_cntlzw},
        {"cntlzw.", &PPC32Emulator::Assembler::asm_cntlzw},
        {"and", &PPC32Emulator::Assembler::asm_and},
        {"and.", &PPC32Emulator::Assembler::asm_and},
        {"cmpl", &PPC32Emulator::Assembler::asm_cmpl},
        {"subf", &PPC32Emulator::Assembler::asm_subf},
        {"subfo", &PPC32Emulator::Assembler::asm_subf},
        {"subf.", &PPC32Emulator::Assembler::asm_subf},
        {"subfo.", &PPC32Emulator::Assembler::asm_subf},
        {"sub", &PPC32Emulator::Assembler::asm_sub},
        {"subo", &PPC32Emulator::Assembler::asm_sub},
        {"sub.", &PPC32Emulator::Assembler::asm_sub},
        {"subo.", &PPC32Emulator::Assembler::asm_sub},
        {"dcbst", &PPC32Emulator::Assembler::asm_dcbst},
        {"lwzux", &PPC32Emulator::Assembler::asm_lwzux},
        {"andc", &PPC32Emulator::Assembler::asm_andc},
        {"andc.", &PPC32Emulator::Assembler::asm_andc},
        {"mulhw", &PPC32Emulator::Assembler::asm_mulhw},
        {"mulhw.", &PPC32Emulator::Assembler::asm_mulhw},
        {"mfmsr", &PPC32Emulator::Assembler::asm_mfmsr},
        {"dcbf", &PPC32Emulator::Assembler::asm_dcbf},
        {"lbzx", &PPC32Emulator::Assembler::asm_lbzx},
        {"neg", &PPC32Emulator::Assembler::asm_neg},
        {"nego", &PPC32Emulator::Assembler::asm_neg},
        {"neg.", &PPC32Emulator::Assembler::asm_neg},
        {"nego.", &PPC32Emulator::Assembler::asm_neg},
        {"lbzux", &PPC32Emulator::Assembler::asm_lbzux},
        {"nor", &PPC32Emulator::Assembler::asm_nor},
        {"nor.", &PPC32Emulator::Assembler::asm_nor},
        {"subfe", &PPC32Emulator::Assembler::asm_subfe},
        {"subfeo", &PPC32Emulator::Assembler::asm_subfe},
        {"subfe.", &PPC32Emulator::Assembler::asm_subfe},
        {"subfeo.", &PPC32Emulator::Assembler::asm_subfe},
        {"adde", &PPC32Emulator::Assembler::asm_adde},
        {"addeo", &PPC32Emulator::Assembler::asm_adde},
        {"adde.", &PPC32Emulator::Assembler::asm_adde},
        {"addeo.", &PPC32Emulator::Assembler::asm_adde},
        {"mtcr", &PPC32Emulator::Assembler::asm_mtcr_mtcrf},
        {"mtcrf", &PPC32Emulator::Assembler::asm_mtcr_mtcrf},
        {"mtmsr", &PPC32Emulator::Assembler::asm_mtmsr},
        {"stwcx.", &PPC32Emulator::Assembler::asm_stwcx_rec},
        {"stwx", &PPC32Emulator::Assembler::asm_stwx},
        {"stwux", &PPC32Emulator::Assembler::asm_stwux},
        {"subfze", &PPC32Emulator::Assembler::asm_subfze},
        {"subfzeo", &PPC32Emulator::Assembler::asm_subfze},
        {"subfze.", &PPC32Emulator::Assembler::asm_subfze},
        {"subfzeo.", &PPC32Emulator::Assembler::asm_subfze},
        {"addze", &PPC32Emulator::Assembler::asm_addze},
        {"addzeo", &PPC32Emulator::Assembler::asm_addze},
        {"addze.", &PPC32Emulator::Assembler::asm_addze},
        {"addzeo.", &PPC32Emulator::Assembler::asm_addze},
        {"mtsr", &PPC32Emulator::Assembler::asm_mtsr},
        {"stbx", &PPC32Emulator::Assembler::asm_stbx},
        {"subfme", &PPC32Emulator::Assembler::asm_subfme},
        {"subfmeo", &PPC32Emulator::Assembler::asm_subfme},
        {"subfme.", &PPC32Emulator::Assembler::asm_subfme},
        {"subfmeo.", &PPC32Emulator::Assembler::asm_subfme},
        {"addme", &PPC32Emulator::Assembler::asm_addme},
        {"addmeo", &PPC32Emulator::Assembler::asm_addme},
        {"addme.", &PPC32Emulator::Assembler::asm_addme},
        {"addmeo.", &PPC32Emulator::Assembler::asm_addme},
        {"mullw", &PPC32Emulator::Assembler::asm_mullw},
        {"mullwo", &PPC32Emulator::Assembler::asm_mullw},
        {"mullw.", &PPC32Emulator::Assembler::asm_mullw},
        {"mullwo.", &PPC32Emulator::Assembler::asm_mullw},
        {"mtsrin", &PPC32Emulator::Assembler::asm_mtsrin},
        {"dcbtst", &PPC32Emulator::Assembler::asm_dcbtst},
        {"stbux", &PPC32Emulator::Assembler::asm_stbux},
        {"add", &PPC32Emulator::Assembler::asm_add},
        {"addo", &PPC32Emulator::Assembler::asm_add},
        {"add.", &PPC32Emulator::Assembler::asm_add},
        {"addo.", &PPC32Emulator::Assembler::asm_add},
        {"dcbt", &PPC32Emulator::Assembler::asm_dcbt},
        {"lhzx", &PPC32Emulator::Assembler::asm_lhzx},
        {"eqv", &PPC32Emulator::Assembler::asm_eqv},
        {"eqv.", &PPC32Emulator::Assembler::asm_eqv},
        {"tlbie", &PPC32Emulator::Assembler::asm_tlbie},
        {"tlbsync", &PPC32Emulator::Assembler::asm_tlbsync},
        {"eciwx", &PPC32Emulator::Assembler::asm_eciwx},
        {"lhzux", &PPC32Emulator::Assembler::asm_lhzux},
        {"xor", &PPC32Emulator::Assembler::asm_xor},
        {"xor.", &PPC32Emulator::Assembler::asm_xor},
        {"mfxer", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mflr", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfctr", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfdsisr", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfdar", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfdec", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfsdr1", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfsrr0", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfsrr1", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfsprg0", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfsprg1", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfsprg2", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfsprg3", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfear", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfpvr", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfibat0u", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfibat0l", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfibat1u", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfibat1l", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfibat2u", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfibat2l", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfibat3u", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfibat3l", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfdbat0u", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfdbat0l", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfdbat1u", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfdbat1l", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfdbat2u", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfdbat2l", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfdbat3u", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfdbat3l", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfdabr", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"mfspr", &PPC32Emulator::Assembler::asm_mfspr_mnemonic},
        {"lhax", &PPC32Emulator::Assembler::asm_lhax},
        {"tlbia", &PPC32Emulator::Assembler::asm_tlbia},
        {"mftb", &PPC32Emulator::Assembler::asm_mftb},
        {"mftbu", &PPC32Emulator::Assembler::asm_mftb},
        {"lhaux", &PPC32Emulator::Assembler::asm_lhaux},
        {"sthx", &PPC32Emulator::Assembler::asm_sthx},
        {"orc", &PPC32Emulator::Assembler::asm_orc},
        {"orc.", &PPC32Emulator::Assembler::asm_orc},
        {"ecowx", &PPC32Emulator::Assembler::asm_ecowx},
        {"sthux", &PPC32Emulator::Assembler::asm_sthux},
        {"or", &PPC32Emulator::Assembler::asm_or},
        {"or.", &PPC32Emulator::Assembler::asm_or},
        {"mr", &PPC32Emulator::Assembler::asm_or},
        {"mr.", &PPC32Emulator::Assembler::asm_or},
        {"divwu", &PPC32Emulator::Assembler::asm_divwu},
        {"divwuo", &PPC32Emulator::Assembler::asm_divwu},
        {"divwu.", &PPC32Emulator::Assembler::asm_divwu},
        {"divwuo.", &PPC32Emulator::Assembler::asm_divwu},
        {"mtxer", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtlr", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtctr", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtdsisr", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtdar", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtdec", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtsdr1", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtsrr0", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtsrr1", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtsprg0", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtsprg1", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtsprg2", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtsprg3", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtear", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtpvr", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtibat0u", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtibat0l", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtibat1u", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtibat1l", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtibat2u", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtibat2l", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtibat3u", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtibat3l", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtdbat0u", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtdbat0l", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtdbat1u", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtdbat1l", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtdbat2u", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtdbat2l", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtdbat3u", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtdbat3l", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtdabr", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"mtspr", &PPC32Emulator::Assembler::asm_mtspr_mnemonic},
        {"dcbi", &PPC32Emulator::Assembler::asm_dcbi},
        {"nand", &PPC32Emulator::Assembler::asm_nand},
        {"nand.", &PPC32Emulator::Assembler::asm_nand},
        {"divw", &PPC32Emulator::Assembler::asm_divw},
        {"divwo", &PPC32Emulator::Assembler::asm_divw},
        {"divw.", &PPC32Emulator::Assembler::asm_divw},
        {"divwo.", &PPC32Emulator::Assembler::asm_divw},
        {"mcrxr", &PPC32Emulator::Assembler::asm_mcrxr},
        {"mcrxro", &PPC32Emulator::Assembler::asm_mcrxr},
        {"mcrxr.", &PPC32Emulator::Assembler::asm_mcrxr},
        {"mcrxro.", &PPC32Emulator::Assembler::asm_mcrxr},
        {"lswx", &PPC32Emulator::Assembler::asm_lswx},
        {"lwbrx", &PPC32Emulator::Assembler::asm_lwbrx},
        {"lfsx", &PPC32Emulator::Assembler::asm_lfsx},
        {"srw", &PPC32Emulator::Assembler::asm_srw},
        {"lfsux", &PPC32Emulator::Assembler::asm_lfsux},
        {"mfsr", &PPC32Emulator::Assembler::asm_mfsr},
        {"lswi", &PPC32Emulator::Assembler::asm_lswi},
        {"sync", &PPC32Emulator::Assembler::asm_sync},
        {"lfdx", &PPC32Emulator::Assembler::asm_lfdx},
        {"lfdux", &PPC32Emulator::Assembler::asm_lfdux},
        {"mfsrin", &PPC32Emulator::Assembler::asm_mfsrin},
        {"stswx", &PPC32Emulator::Assembler::asm_stswx},
        {"stwbrx", &PPC32Emulator::Assembler::asm_stwbrx},
        {"stfsx", &PPC32Emulator::Assembler::asm_stfsx},
        {"stfsux", &PPC32Emulator::Assembler::asm_stfsux},
        {"stswi", &PPC32Emulator::Assembler::asm_stswi},
        {"stfdx", &PPC32Emulator::Assembler::asm_stfdx},
        {"dcba", &PPC32Emulator::Assembler::asm_dcba},
        {"stfdux", &PPC32Emulator::Assembler::asm_stfdux},
        {"lhbrx", &PPC32Emulator::Assembler::asm_lhbrx},
        {"sraw", &PPC32Emulator::Assembler::asm_sraw},
        {"sraw.", &PPC32Emulator::Assembler::asm_sraw},
        {"srawi", &PPC32Emulator::Assembler::asm_srawi},
        {"srawi.", &PPC32Emulator::Assembler::asm_srawi},
        {"eieio", &PPC32Emulator::Assembler::asm_eieio},
        {"sthbrx", &PPC32Emulator::Assembler::asm_sthbrx},
        {"extsh", &PPC32Emulator::Assembler::asm_extsh},
        {"extsh.", &PPC32Emulator::Assembler::asm_extsh},
        {"extsb", &PPC32Emulator::Assembler::asm_extsb},
        {"extsb.", &PPC32Emulator::Assembler::asm_extsb},
        {"icbi", &PPC32Emulator::Assembler::asm_icbi},
        {"stfiwx", &PPC32Emulator::Assembler::asm_stfiwx},
        {"dcbz", &PPC32Emulator::Assembler::asm_dcbz},
        {"lwz", &PPC32Emulator::Assembler::asm_lwz},
        {"lwzu", &PPC32Emulator::Assembler::asm_lwzu},
        {"lbz", &PPC32Emulator::Assembler::asm_lbz},
        {"lbzu", &PPC32Emulator::Assembler::asm_lbzu},
        {"stw", &PPC32Emulator::Assembler::asm_stw},
        {"stwu", &PPC32Emulator::Assembler::asm_stwu},
        {"stb", &PPC32Emulator::Assembler::asm_stb},
        {"stbu", &PPC32Emulator::Assembler::asm_stbu},
        {"lhz", &PPC32Emulator::Assembler::asm_lhz},
        {"lhzu", &PPC32Emulator::Assembler::asm_lhzu},
        {"lha", &PPC32Emulator::Assembler::asm_lha},
        {"lhau", &PPC32Emulator::Assembler::asm_lhau},
        {"sth", &PPC32Emulator::Assembler::asm_sth},
        {"sthu", &PPC32Emulator::Assembler::asm_sthu},
        {"lmw", &PPC32Emulator::Assembler::asm_lmw},
        {"stmw", &PPC32Emulator::Assembler::asm_stmw},
        {"lfs", &PPC32Emulator::Assembler::asm_lfs},
        {"lfsu", &PPC32Emulator::Assembler::asm_lfsu},
        {"lfd", &PPC32Emulator::Assembler::asm_lfd},
        {"lfdu", &PPC32Emulator::Assembler::asm_lfdu},
        {"stfs", &PPC32Emulator::Assembler::asm_stfs},
        {"stfsu", &PPC32Emulator::Assembler::asm_stfsu},
        {"stfd", &PPC32Emulator::Assembler::asm_stfd},
        {"stfdu", &PPC32Emulator::Assembler::asm_stfdu},
        {"fdivs", &PPC32Emulator::Assembler::asm_fdivs},
        {"fdivs.", &PPC32Emulator::Assembler::asm_fdivs},
        {"fsubs", &PPC32Emulator::Assembler::asm_fsubs},
        {"fsubs.", &PPC32Emulator::Assembler::asm_fsubs},
        {"fadds", &PPC32Emulator::Assembler::asm_fadds},
        {"fadds.", &PPC32Emulator::Assembler::asm_fadds},
        {"fsqrts", &PPC32Emulator::Assembler::asm_fsqrts},
        {"fsqrts.", &PPC32Emulator::Assembler::asm_fsqrts},
        {"fres", &PPC32Emulator::Assembler::asm_fres},
        {"fres.", &PPC32Emulator::Assembler::asm_fres},
        {"fmuls", &PPC32Emulator::Assembler::asm_fmuls},
        {"fmuls.", &PPC32Emulator::Assembler::asm_fmuls},
        {"fmsubs", &PPC32Emulator::Assembler::asm_fmsubs},
        {"fmsubs.", &PPC32Emulator::Assembler::asm_fmsubs},
        {"fmadds", &PPC32Emulator::Assembler::asm_fmadds},
        {"fmadds.", &PPC32Emulator::Assembler::asm_fmadds},
        {"fnmsubs", &PPC32Emulator::Assembler::asm_fnmsubs},
        {"fnmsubs.", &PPC32Emulator::Assembler::asm_fnmsubs},
        {"fnmadds", &PPC32Emulator::Assembler::asm_fnmadds},
        {"fnmadds.", &PPC32Emulator::Assembler::asm_fnmadds},
        {"fdiv", &PPC32Emulator::Assembler::asm_fdiv},
        {"fdiv.", &PPC32Emulator::Assembler::asm_fdiv},
        {"fsub", &PPC32Emulator::Assembler::asm_fsub},
        {"fsub.", &PPC32Emulator::Assembler::asm_fsub},
        {"fadd", &PPC32Emulator::Assembler::asm_fadd},
        {"fadd.", &PPC32Emulator::Assembler::asm_fadd},
        {"fsqrt", &PPC32Emulator::Assembler::asm_fsqrt},
        {"fsqrt.", &PPC32Emulator::Assembler::asm_fsqrt},
        {"fsel", &PPC32Emulator::Assembler::asm_fsel},
        {"fsel.", &PPC32Emulator::Assembler::asm_fsel},
        {"fmul", &PPC32Emulator::Assembler::asm_fmul},
        {"fmul.", &PPC32Emulator::Assembler::asm_fmul},
        {"frsqrte", &PPC32Emulator::Assembler::asm_frsqrte},
        {"frsqrte.", &PPC32Emulator::Assembler::asm_frsqrte},
        {"fmsub", &PPC32Emulator::Assembler::asm_fmsub},
        {"fmsub.", &PPC32Emulator::Assembler::asm_fmsub},
        {"fmadd", &PPC32Emulator::Assembler::asm_fmadd},
        {"fmadd.", &PPC32Emulator::Assembler::asm_fmadd},
        {"fnmsub", &PPC32Emulator::Assembler::asm_fnmsub},
        {"fnmsub.", &PPC32Emulator::Assembler::asm_fnmsub},
        {"fnmadd", &PPC32Emulator::Assembler::asm_fnmadd},
        {"fnmadd.", &PPC32Emulator::Assembler::asm_fnmadd},
        {"fcmpu", &PPC32Emulator::Assembler::asm_fcmpu},
        {"frsp", &PPC32Emulator::Assembler::asm_frsp},
        {"frsp.", &PPC32Emulator::Assembler::asm_frsp},
        {"fctiw", &PPC32Emulator::Assembler::asm_fctiw},
        {"fctiw.", &PPC32Emulator::Assembler::asm_fctiw},
        {"fctiwz", &PPC32Emulator::Assembler::asm_fctiwz},
        {"fctiwz.", &PPC32Emulator::Assembler::asm_fctiwz},
        {"fcmpo", &PPC32Emulator::Assembler::asm_fcmpo},
        {"mtfsb1", &PPC32Emulator::Assembler::asm_mtfsb1},
        {"mtfsb1.", &PPC32Emulator::Assembler::asm_mtfsb1},
        {"fneg", &PPC32Emulator::Assembler::asm_fneg},
        {"fneg.", &PPC32Emulator::Assembler::asm_fneg},
        {"mcrfs", &PPC32Emulator::Assembler::asm_mcrfs},
        {"mtfsb0", &PPC32Emulator::Assembler::asm_mtfsb0},
        {"mtfsb0.", &PPC32Emulator::Assembler::asm_mtfsb0},
        {"fmr", &PPC32Emulator::Assembler::asm_fmr},
        {"fmr.", &PPC32Emulator::Assembler::asm_fmr},
        {"mtfsfi", &PPC32Emulator::Assembler::asm_mtfsfi},
        {"mtfsfi.", &PPC32Emulator::Assembler::asm_mtfsfi},
        {"fnabs", &PPC32Emulator::Assembler::asm_fnabs},
        {"fnabs.", &PPC32Emulator::Assembler::asm_fnabs},
        {"fabs", &PPC32Emulator::Assembler::asm_fabs},
        {"fabs.", &PPC32Emulator::Assembler::asm_fabs},
        {"mffs", &PPC32Emulator::Assembler::asm_mffs},
        {"mffs.", &PPC32Emulator::Assembler::asm_mffs},
        {"mtfsf", &PPC32Emulator::Assembler::asm_mtfsf},
        {"mtfsf.", &PPC32Emulator::Assembler::asm_mtfsf},
        {".data", &PPC32Emulator::Assembler::asm_data},
};

PPC32Emulator::Regs::Regs() {
  memset(this, 0, sizeof(*this));
  this->tbr_ticks_per_cycle = 1;
}

void PPC32Emulator::Regs::set_by_name(const std::string& reg_name, uint32_t value) {
  if (reg_name.size() < 2) {
    throw std::invalid_argument("invalid register name");
  }
  // TODO: add ability to set f0-f31

  std::string name_lower = phosg::tolower(reg_name);

  if (name_lower == "cr") {
    this->cr.u = value;
  } else if (name_lower == "fpscr") {
    this->fpscr = value;
  } else if (name_lower == "xer") {
    this->xer.u = value;
  } else if (name_lower == "lr") {
    this->lr = value;
  } else if (name_lower == "ctr") {
    this->ctr = value;
  } else if (name_lower == "tbr") {
    this->tbr = value;
  } else if (name_lower == "pc") {
    this->pc = value;
  } else if (reg_name[0] == 'r') {
    int64_t reg_num = strtol(reg_name.data() + 1, nullptr, 10);
    if (reg_num < 0 || reg_num > 31) {
      throw std::invalid_argument("invalid register number");
    }
    this->r[reg_num].u = value;
  } else {
    throw std::invalid_argument("invalid register name");
  }
}

void PPC32Emulator::Regs::print_header(FILE* stream) {
  phosg::fwrite_fmt(stream,
      "---r0---/---r1---/---r2---/---r3---/---r4---/---r5---/---r6---/---r7---/---r8---/---r9---/--r10---/--r11---/"
      "--r12---/--r13---/--r14---/--r15---/--r16---/--r17---/--r18---/--r19---/--r20---/--r21---/--r22---/--r23---/"
      "--r24---/--r25---/--r26---/--r27---/--r28---/--r29---/--r30---/--r31--- ---CR--- ---LR--- --CTR--- ---PC---");
}

void PPC32Emulator::Regs::print(FILE* stream) const {
  for (size_t x = 0; x < 32; x++) {
    if (x != 0) {
      fputc('/', stream);
    }
    phosg::fwrite_fmt(stream, "{:08X}", this->r[x].u);
  }

  // Uncomment to add floats (not very useful for debugging currently)
  // phosg::fwrite_fmt(stream, "{:g}", this->f[0].f);
  // for (size_t x = 1; x < 32; x++) {
  //   phosg::fwrite_fmt(stream, "/{:g}", this->f[x].f);
  // }

  phosg::fwrite_fmt(stream, " {:08X}", this->cr.u);
  // phosg::fwrite_fmt(stream, " fpscr/{:08X}", this->fpscr);
  // phosg::fwrite_fmt(stream, " xer/{:08X}", this->xer.u);
  phosg::fwrite_fmt(stream, " {:08X}", this->lr);
  phosg::fwrite_fmt(stream, " {:08X}", this->ctr);
  // phosg::fwrite_fmt(stream, " tbr/{:016X}", this->tbr);
  phosg::fwrite_fmt(stream, " {:08X}", this->pc);
  // phosg::fwrite_fmt(stream, " addr/{:08X}", this->debug.addr);
}

void PPC32Emulator::Regs::set_crf_int_result(uint8_t crf_num, int32_t a) {
  uint8_t crf_res = this->xer.get_so() ? 1 : 0;
  if (a < 0) {
    crf_res |= 8;
  } else if (a > 0) {
    crf_res |= 4;
  } else {
    crf_res |= 2;
  }
  this->cr.replace_field(crf_num, crf_res);
}

PPC32Emulator::PPC32Emulator(std::shared_ptr<MemoryContext> mem) : EmulatorBase(mem) {}

void PPC32Emulator::import_state(FILE*) {
  throw std::runtime_error("PPC32Emulator::import_state is not implemented");
}

void PPC32Emulator::export_state(FILE*) const {
  throw std::runtime_error("PPC32Emulator::export_state is not implemented");
}

void PPC32Emulator::print_state_header(FILE* stream) const {
  this->regs.print_header(stream);
  phosg::fwrite_fmt(stream, " = OPCODE\n");
}

void PPC32Emulator::print_state(FILE* stream) const {
  this->regs.print(stream);
  try {
    uint32_t opcode = this->mem->read_u32b(this->regs.pc);
    phosg::fwrite_fmt(stream, " = {:08X} {}\n", opcode, this->disassemble_one(this->regs.pc, opcode));
  } catch (const std::exception& e) {
    phosg::fwrite_fmt(stream, " = (failed: {})\n", e.what());
  }
}

void PPC32Emulator::execute() {
  if (!this->interrupt_manager.get()) {
    this->interrupt_manager = std::make_shared<InterruptManager>();
  }

  for (;;) {
    try {
      if (this->debug_hook) {
        this->debug_hook(*this);
      }

      this->interrupt_manager->on_cycle_start();

      uint32_t full_op = this->mem->read<phosg::be_uint32_t>(this->regs.pc);
      uint8_t op = op_get_op(full_op);
      auto fn = this->fns[op].exec;
      (this->*fn)(full_op);
      this->regs.pc += 4;
      this->regs.tbr += this->regs.tbr_ticks_per_cycle;
      this->instructions_executed++;

    } catch (const terminate_emulation&) {
      break;
    }
  }
}

std::string PPC32Emulator::disassemble_one(uint32_t pc, uint32_t op) {
  DisassemblyState s = {pc, nullptr, {}, nullptr};
  return PPC32Emulator::fns[op_get_op(op)].dasm(s, op);
}

std::string PPC32Emulator::disassemble_one(DisassemblyState& s, uint32_t op) {
  return PPC32Emulator::fns[op_get_op(op)].dasm(s, op);
}

std::string PPC32Emulator::disassemble(
    const void* data,
    size_t size,
    uint32_t start_pc,
    const std::multimap<uint32_t, std::string>* in_labels,
    const std::vector<std::string>* import_names) {
  static const std::multimap<uint32_t, std::string> empty_labels_map = {};

  DisassemblyState s = {
      .pc = start_pc,
      .labels = (in_labels ? in_labels : &empty_labels_map),
      .branch_refs = {},
      .import_names = import_names,
  };

  const phosg::be_uint32_t* opcodes = reinterpret_cast<const phosg::be_uint32_t*>(data);

  // Phase 1: generate the disassembly for each opcode, and collect branch target addresses
  size_t line_count = size / 4;
  std::forward_list<std::string> lines;
  auto add_line_it = lines.before_begin();
  for (size_t x = 0; x < line_count; x++, s.pc += 4) {
    uint32_t opcode = opcodes[x];
    std::string line = std::format("{:08X}  {:08X}  ", s.pc, opcode);
    line += PPC32Emulator::disassemble_one(s, opcode);
    line += '\n';
    add_line_it = lines.emplace_after(add_line_it, std::move(line));
  }

  // Phase 2: add labels from the passed-in labels dict and from disassembled branch opcodes; while doing so, count the
  // number of bytes in the output.
  s.pc = start_pc;
  size_t ret_bytes = 0;
  auto branch_refs_it = s.branch_refs.lower_bound(start_pc);
  auto label_it = s.labels->lower_bound(start_pc);
  for (auto prev_line_it = lines.before_begin(), line_it = lines.begin();
      line_it != lines.end();
      prev_line_it = line_it++, s.pc += 4) {
    for (; label_it != s.labels->end() && label_it->first <= s.pc + 3; label_it++) {
      std::string label;
      if (label_it->first != s.pc) {
        label = std::format("{}: // at {:08X} (misaligned)\n", label_it->second, label_it->first);
      } else {
        label = std::format("{}:\n", label_it->second);
      }
      ret_bytes += label.size();
      prev_line_it = lines.emplace_after(prev_line_it, std::move(label));
    }
    for (; branch_refs_it != s.branch_refs.end() && branch_refs_it->first <= s.pc; branch_refs_it++) {
      auto label = EmulatorBase::format_label(s.pc, branch_refs_it->first, branch_refs_it->second);
      ret_bytes += label.size();
      prev_line_it = lines.emplace_after(prev_line_it, std::move(label));
    }

    ret_bytes += line_it->size();
  }

  // Phase 3: assemble the output lines into a single string and return it
  std::string ret;
  ret.reserve(ret_bytes);
  for (const auto& line : lines) {
    ret += line;
  }
  return ret;
}

PPC32Emulator::DisassembleResult PPC32Emulator::disassemble_structured(
    const void* data,
    size_t size,
    uint32_t start_pc,
    const std::multimap<uint32_t, std::string>* in_labels,
    const std::vector<std::string>* import_names) {
  static const std::multimap<uint32_t, std::string> empty_labels_map = {};

  DisassemblyState s = {
      .pc = start_pc,
      .labels = (in_labels ? in_labels : &empty_labels_map),
      .branch_refs = {},
      .import_names = import_names,
  };
  const phosg::be_uint32_t* opcodes = reinterpret_cast<const phosg::be_uint32_t*>(data);

  DisassembleResult res;
  size_t line_count = (size & (~3)) / 4;
  for (size_t x = 0; x < line_count; x++, s.pc += 4) {
    auto& segment = res.segments.emplace_back();
    segment.address = s.pc;
    segment.size = 4;
    segment.disassembly = PPC32Emulator::disassemble_one(s, opcodes[x]);
  }

  res.import_labels(*s.labels, &s.branch_refs);
  return res;
}

PPC32Emulator::AssembleResult PPC32Emulator::assemble(
    const std::string& text, std::function<std::string(const std::string&)> get_include, uint32_t start_address) {
  Assembler a;
  a.start_address = start_address;
  a.assemble(text, get_include);

  AssembleResult res;
  res.code = std::move(a.code.str());
  res.label_offsets = std::move(a.label_offsets);
  res.label_addresses = std::move(a.label_addresses);
  res.metadata_keys = std::move(a.metadata_keys);
  return res;
}

PPC32Emulator::AssembleResult PPC32Emulator::assemble(
    const std::string& text, const std::vector<std::string>& include_dirs, uint32_t start_address) {
  if (include_dirs.empty()) {
    return PPC32Emulator::assemble(text, nullptr, start_address);

  } else {
    std::unordered_set<std::string> get_include_stack;
    std::function<std::string(const std::string&)> get_include = [&](const std::string& name) -> std::string {
      for (const auto& dir : include_dirs) {
        std::string filename = dir + "/" + name + ".inc.s";
        if (std::filesystem::is_regular_file(filename)) {
          if (!get_include_stack.emplace(name).second) {
            throw std::runtime_error("mutual recursion between includes: " + name);
          }
          const auto& ret = PPC32Emulator::assemble(phosg::load_file(filename), get_include, start_address).code;
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
    return PPC32Emulator::assemble(text, get_include, start_address);
  }
}

void PPC32Emulator::Assembler::assemble(
    const std::string& text, std::function<std::string(const std::string&)> get_include) {
  std::string effective_text = text;
  phosg::strip_comments_inplace(effective_text);

  // First pass: generate args and labels and collect metadata
  phosg::StringReader r(effective_text);
  size_t line_num = 0;
  size_t stream_offset = 0;
  uint32_t si_address = this->start_address;
  while (!r.eof()) {
    std::string line = r.get_line();
    line_num++;

    try {
      // Strip comments and whitespace
      size_t comment_pos = std::min<size_t>(std::min<size_t>(line.find("//"), line.find('#')), line.find(';'));
      if (comment_pos != std::string::npos) {
        line = line.substr(0, comment_pos);
      }
      phosg::strip_whitespace(line);

      // If the line is blank, skip it
      if (line.empty()) {
        continue;
      }

      // If the line ends with :, it's a label
      if (line.ends_with(":")) {
        line.pop_back();
        phosg::strip_trailing_whitespace(line);
        if (!this->label_offsets.emplace(line, stream_offset).second) {
          throw std::runtime_error("duplicate label: " + line);
        }
        if (!this->label_addresses.emplace(line, si_address).second) {
          throw std::runtime_error("duplicate label: " + line);
        }
        continue;
      }

      // Get the opcode name and arguments
      std::vector<std::string> tokens = phosg::split(line, ' ', 1);
      if (tokens.size() == 0) {
        throw std::logic_error("no tokens in non-empty line");
      }
      const std::string& op_name = tokens[0];

      std::vector<Argument> args;
      if (tokens.size() == 2) {
        std::string& args_str = tokens[1];
        phosg::strip_leading_whitespace(args_str);
        if (op_name == ".meta") {
          size_t equals_pos = args_str.find('=');
          if (equals_pos == std::string::npos) {
            this->metadata_keys.emplace(args_str, "");
          } else {
            this->metadata_keys.emplace(
                args_str.substr(0, equals_pos), phosg::parse_data_string(args_str.substr(equals_pos + 1)));
          }
          continue;
        } else if ((op_name == ".binary") || (op_name == ".include")) {
          args.emplace_back(args_str, true);
        } else if (op_name == ".address") {
          args.emplace_back(args_str);
        } else {
          std::vector<std::string> arg_strs = phosg::split(args_str, ',');
          for (auto& arg_str : arg_strs) {
            phosg::strip_leading_whitespace(arg_str);
            phosg::strip_trailing_whitespace(arg_str);
            args.emplace_back(arg_str);
          }
        }
      }

      if (op_name == ".address") {
        const auto& arg = args.at(0);
        if (arg.type == ArgType::IMMEDIATE) {
          si_address = this->resolve_immediate(args.at(0));
        } else {
          throw std::runtime_error("missing or invalid argument to .address directive");
        }
        continue;
      }
      if (op_name == ".label") {
        if (args.size() != 2) {
          throw std::runtime_error("incorrect argument count in .label directive");
        }
        const auto& name_arg = args.at(0);
        if ((name_arg.type != Argument::Type::IMMEDIATE) || !name_arg.value_expr) {
          throw std::runtime_error("invalid name in .label directive");
        }
        const auto* lookup_node = dynamic_cast<const Expression::EnvLookupNode*>(name_arg.value_expr.get());
        if (!lookup_node) {
          throw std::runtime_error("invalid name in .label directive");
        }
        const auto& value_arg = args.at(1);
        if (value_arg.type != ArgType::IMMEDIATE) {
          throw std::runtime_error("missing or invalid address in .label directive");
        }
        this->label_addresses.emplace(lookup_node->name, this->resolve_immediate(value_arg));
        continue;
      }

      const StreamItem& si = this->stream.emplace_back(
          StreamItem{stream_offset, si_address, line_num, op_name, std::move(args)});
      if (si.op_name == ".include") {
        const auto& a = si.check_args({ArgType::RAW});
        const std::string& inc_name = a[0].raw_data;
        if (!get_include) {
          throw std::runtime_error("includes are not available");
        }
        std::string contents;
        try {
          const std::string& contents = this->includes_cache.at(inc_name);
          stream_offset += (contents.size() + 3) & (~3);
          si_address += (contents.size() + 3) & (~3);
        } catch (const std::out_of_range&) {
          try {
            contents = get_include(inc_name);
          } catch (const std::exception& e) {
            throw std::runtime_error(std::format("failed to get include data: {}", e.what()));
          }
          stream_offset += (contents.size() + 3) & (~3);
          si_address += (contents.size() + 3) & (~3);
          this->includes_cache.emplace(inc_name, std::move(contents));
        }

      } else if ((si.op_name == ".zero") && !si.args.empty()) {
        const auto& a = si.check_args({ArgType::IMMEDIATE});
        int64_t a0val = this->resolve_immediate(a[0]);
        if (a0val & 3) {
          throw std::runtime_error(".zero directive must specify a multiple of 4 bytes");
        }
        stream_offset += a0val;
        si_address += a0val;

      } else if ((si.op_name == ".binary") && !si.args.empty()) {
        const auto& a = si.check_args({ArgType::RAW});
        // TODO: It's not great that we call parse_data_string here just to get the length of the result data. Find a
        // way to not have to do this.
        std::string data = phosg::parse_data_string(a[0].raw_data);
        stream_offset += (data.size() + 3) & (~3);
        si_address += (data.size() + 3) & (~3);

      } else {
        stream_offset += 4;
        si_address += 4;
      }
    } catch (const std::exception& e) {
      throw std::runtime_error(std::format("(line {}) {}", line_num, e.what()));
    }
  }

  // Second pass: generate opcodes
  for (const auto& si : this->stream) {
    try {
      if (si.op_name == ".include") {
        const auto& a = si.check_args({ArgType::RAW});
        try {
          const std::string& include_contents = this->includes_cache.at(a[0].raw_data);
          this->code.write(include_contents);
          while (this->code.size() & 3) {
            this->code.put_u8(0);
          }
        } catch (const std::out_of_range&) {
          throw std::logic_error("include data missing from cache");
        }

      } else if (si.op_name == ".zero") {
        if (si.args.empty()) {
          this->code.put_u32(0x00000000);
        } else {
          const auto& a = si.check_args({ArgType::IMMEDIATE});
          int64_t a0val = this->resolve_immediate(a[0]);
          if (a0val & 3) {
            throw std::logic_error(".zero directive must specify a multiple of 4 bytes");
          }
          for (ssize_t x = 0; x < a0val; x += 4) {
            this->code.put_u32(0x00000000);
          }
        }

      } else if (si.op_name == ".binary") {
        const auto& a = si.check_args({ArgType::RAW});
        std::string data = phosg::parse_data_string(a[0].raw_data);
        data.resize((data.size() + 3) & (~3), '\0');
        this->code.write(data);

      } else {
        AssembleFunction fn;
        try {
          fn = this->assemble_functions.at(si.op_name);
        } catch (const std::out_of_range&) {
          throw std::runtime_error(std::format("invalid opcode name: {}", si.op_name));
        }
        this->code.put_u32b((this->*fn)(si));
      }
    } catch (const std::exception& e) {
      throw std::runtime_error(std::format("(line {}) {}", si.line_num, e.what()));
    }
  }
}

bool PPC32Emulator::test_assembler(size_t num_threads, uint32_t start_opcode, bool stop_on_failure, bool verbose) {
  std::array<std::atomic<size_t>, 0x40> errors_histogram;
  for (size_t z = 0; z < errors_histogram.size(); z++) {
    errors_histogram[z] = 0;
  }

  auto check_opcode = [&](uint64_t opcode, size_t) -> bool {
    std::string disassembly = PPC32Emulator::disassemble_one(0, opcode);
    if (disassembly.starts_with(".invalid")) {
      return false;
    }
    std::string assembled;
    try {
      assembled = PPC32Emulator::assemble(disassembly).code;
    } catch (const std::exception& e) {
      phosg::fwrite_fmt(stderr, "[{:08X}] \"{}\" (assembly failed: {})\n", opcode, disassembly, e.what());
      errors_histogram[(opcode >> 26) & 0x3F]++;
      return stop_on_failure;
    }
    if (assembled.size() != 4) {
      phosg::fwrite_fmt(stderr, "[{:08X}] \"{}\" (assembly produced incorrect data size)\n", opcode, disassembly);
      phosg::print_data(stderr, assembled);
      errors_histogram[(opcode >> 26) & 0x3F]++;
      return stop_on_failure;
    }
    phosg::StringReader r(assembled);
    uint32_t assembled_opcode = r.get_u32b();
    if (assembled_opcode != opcode) {
      phosg::fwrite_fmt(stderr, "[{:08X}] \"{}\" (assembly produced incorrect opcode {:08X})\n",
          opcode, disassembly, assembled_opcode);
      errors_histogram[(opcode >> 26) & 0x3F]++;
      return stop_on_failure;
    }
    if (verbose) {
      phosg::fwrite_fmt(stderr, "[{:08X}] \"{}\" (correct)\n", opcode, disassembly);
    }
    return false;
  };

  uint64_t failed_opcode = phosg::parallel_blocks<uint64_t>(
      check_opcode, start_opcode, 0x100000000, 0x1000, num_threads,
      [](uint64_t, uint64_t, uint64_t current_value, uint64_t start_time) -> void {
        uint64_t elapsed_time = phosg::now() - start_time;
        std::string elapsed_str = phosg::format_duration(elapsed_time);
        phosg::fwritex(stderr, std::format("\x1B[K... {:08X}: {} ({})\r", current_value, PPC32Emulator::disassemble_one(0, current_value), elapsed_str));
      });

  for (size_t z = 0; z < 0x40; z++) {
    size_t count = errors_histogram[z];
    if (count) {
      phosg::fwrite_fmt(stderr, "{:08X} => {} (0x{:X}) errors\n", (z << 26), count, count);
    }
  }

  if (failed_opcode < 0x100000000) {
    std::string disassembly = PPC32Emulator::disassemble_one(0, failed_opcode);
    phosg::fwrite_fmt(stderr, "Failed on {:08X}: {}\n", failed_opcode, disassembly);
    auto assembled = PPC32Emulator::assemble(disassembly);
    phosg::print_data(stderr, assembled.code);
    if (assembled.code.size() == 4) {
      phosg::fwrite_fmt(stderr, "Failure: resulting data does not match original opcode\n");
    } else {
      phosg::fwrite_fmt(stderr, "Failure: resulting data size is not 4 bytes\n");
    }
    return false;
  } else {
    return true;
  }
}

} // namespace ResourceDASM
