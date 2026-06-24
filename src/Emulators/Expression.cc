#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "Expression.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {
namespace Expression {

bool Value::is_int() const {
  return std::holds_alternative<int64_t>(this->v);
}
bool Value::is_float() const {
  return std::holds_alternative<double>(this->v);
}
bool Value::is_string() const {
  return std::holds_alternative<std::string>(this->v);
}

int64_t Value::as_int() const {
  if (!this->is_int()) {
    throw std::runtime_error(std::format("Expected int; received {}", this->str()));
  }
  return std::get<int64_t>(this->v);
}
double Value::as_float() const {
  if (!this->is_float()) {
    throw std::runtime_error(std::format("Expected float; received {}", this->str()));
  }
  return std::get<double>(this->v);
}
std::string Value::as_string() const {
  if (!this->is_string()) {
    throw std::runtime_error(std::format("Expected float; received {}", this->str()));
  }
  return std::get<std::string>(this->v);
}

bool Value::truth_value() const {
  if (this->is_int()) {
    return (this->as_int() != 0);
  } else if (this->is_float()) {
    return (this->as_float() != 0.0);
  } else if (this->is_string()) {
    return !this->as_string().empty();
  } else {
    throw std::logic_error("Unknown value type");
  }
}

std::string Value::str(bool hex) const {
  if (this->is_int()) {
    return hex ? phosg::hex(this->as_int()) : std::format("{}", this->as_int());
  } else if (this->is_float()) {
    return std::format("{}", this->as_float());
  } else if (this->is_string()) {
    return hex
        ? phosg::format_data_string(this->as_string(), nullptr, FormatDataStringFlags::HEX_ONLY)
        : std::format("\"{}\"", phosg::escape_quotes(this->as_string()));
  } else {
    throw std::logic_error("Unknown value type");
  }
}

FunctionCallNode::FunctionCallNode(std::string_view function_name, std::vector<std::unique_ptr<Node>>&& args)
    : function_name(function_name), args(std::move(args)) {}

bool FunctionCallNode::operator==(const Node& other) const {
  const auto* other_fc = dynamic_cast<const FunctionCallNode*>(&other);
  if (!other_fc || (other_fc->function_name != this->function_name)) {
    return false;
  }
  if (this->args.size() != other_fc->args.size()) {
    return false;
  }
  for (size_t z = 0; z < this->args.size(); z++) {
    if (*this->args[z] != *other_fc->args[z]) {
      return false;
    }
  }
  return true;
}

bool FunctionCallNode::is_const(const EnvIsConst& env_is_const) const {
  for (const auto& arg : this->args) {
    if (!arg->is_const(env_is_const)) {
      return false;
    }
  }
  return true;
}

Value FunctionCallNode::evaluate(const EnvLookup& env, const FunctionCall& function_call) const {
  std::vector<Value> arg_values;
  arg_values.reserve(this->args.size());
  for (const auto& arg : this->args) {
    arg_values.emplace_back(arg->evaluate(env, function_call));
  }
  if (!function_call) {
    return this->default_handler(this->function_name, arg_values);
  } else {
    return function_call(this->function_name, arg_values);
  }
}

std::string FunctionCallNode::str() const {
  std::string ret = std::format("{}(", this->function_name);
  for (size_t z = 0; z < this->args.size(); z++) {
    if (z) {
      ret += ", ";
    }
    ret += this->args[z]->str();
  }
  ret += ")";
  return ret;
}

Value FunctionCallNode::default_handler(const std::string& name, const std::vector<Value>& args) {
  if (name == "bswap16" && args.size() == 1) {
    return static_cast<int64_t>(bswap16(static_cast<uint16_t>(args[0].as_int())));
  } else if (name == "bswap32" && args.size() == 1) {
    return static_cast<int64_t>(bswap32(static_cast<uint32_t>(args[0].as_int())));
  } else if (name == "encode_float" && args.size() == 1) {
    if (args[0].is_float()) {
      return static_cast<int64_t>(bit_cast<uint32_t>(static_cast<float>(args[0].as_float())));
    } else {
      return static_cast<int64_t>(bit_cast<uint32_t>(static_cast<float>(args[0].as_int())));
    }
  } else if (name == "decode_float" && args.size() == 1) {
    if (args[0].is_float()) {
      return args[0];
    } else {
      return static_cast<double>(bit_cast<float>(static_cast<uint32_t>(args[0].as_int())));
    }
  } else {
    throw std::runtime_error("Undefined function: " + name);
  }
}

Expression::TernaryOperatorNode::TernaryOperatorNode(
    Type type, std::unique_ptr<Node>&& left, std::unique_ptr<Node>&& center, std::unique_ptr<Node>&& right)
    : type(type), left(std::move(left)), center(std::move(center)), right(std::move(right)) {}

bool Expression::TernaryOperatorNode::operator==(const Node& other) const {
  const auto* other_tern = dynamic_cast<const TernaryOperatorNode*>(&other);
  return other_tern &&
      (other_tern->type == this->type) &&
      (*other_tern->left == *this->left) &&
      (*other_tern->center == *this->center) &&
      (*other_tern->right == *this->right);
}

bool TernaryOperatorNode::is_const(const EnvIsConst& env_is_const) const {
  return this->left->is_const(env_is_const) &&
      this->center->is_const(env_is_const) &&
      this->right->is_const(env_is_const);
}

Value Expression::TernaryOperatorNode::evaluate(const EnvLookup& lookup, const FunctionCall& function_call) const {
  switch (this->type) {
    case Type::CONDITION_SELECTOR:
      return this->left->evaluate(lookup, function_call).truth_value()
          ? this->center->evaluate(lookup, function_call)
          : this->right->evaluate(lookup, function_call);
    default:
      throw std::logic_error("invalid ternary operator type");
  }
}

std::string Expression::TernaryOperatorNode::str() const {
  switch (this->type) {
    case Type::CONDITION_SELECTOR:
      return std::format("({}) ? ({}) : ({})", this->left->str(), this->center->str(), this->right->str());
    default:
      throw std::logic_error("invalid binary operator type");
  }
}

Expression::BinaryOperatorNode::BinaryOperatorNode(
    Type type, std::unique_ptr<Node>&& left, std::unique_ptr<Node>&& right)
    : type(type), left(std::move(left)), right(std::move(right)) {}

bool Expression::BinaryOperatorNode::operator==(const Node& other) const {
  const auto* other_bin = dynamic_cast<const BinaryOperatorNode*>(&other);
  return other_bin &&
      (other_bin->type == this->type) &&
      (*other_bin->left == *this->left) &&
      (*other_bin->right == *this->right);
}

bool BinaryOperatorNode::is_const(const EnvIsConst& env_is_const) const {
  return this->left->is_const(env_is_const) && this->right->is_const(env_is_const);
}

Value Expression::BinaryOperatorNode::evaluate(const EnvLookup& env, const FunctionCall& fc) const {
  auto for_numeric_types = [&](auto combine) -> Value {
    auto left_val = this->left->evaluate(env, fc);
    if (left_val.is_int()) {
      return combine(left_val.as_int(), this->right->evaluate(env, fc).as_int());
    } else if (left_val.is_float()) {
      return combine(left_val.as_float(), this->right->evaluate(env, fc).as_float());
    } else if (left_val.is_string()) {
      throw std::runtime_error("A numeric value is required");
    } else {
      throw std::logic_error("Unknown value type");
    }
  };
  auto for_all_types = [&](auto combine) -> Value {
    auto left_val = this->left->evaluate(env, fc);
    if (left_val.is_int()) {
      return combine(left_val.as_int(), this->right->evaluate(env, fc).as_int());
    } else if (left_val.is_float()) {
      return combine(left_val.as_float(), this->right->evaluate(env, fc).as_float());
    } else if (left_val.is_string()) {
      return combine(left_val.as_float(), this->right->evaluate(env, fc).as_float());
    } else {
      throw std::logic_error("Unknown value type");
    }
  };
  auto for_all_types_boolean = [&](auto combine) -> Value {
    auto left_val = this->left->evaluate(env, fc);
    if (left_val.is_int()) {
      return static_cast<int64_t>(combine(left_val.as_int(), this->right->evaluate(env, fc).as_int()));
    } else if (left_val.is_float()) {
      return static_cast<int64_t>(combine(left_val.as_float(), this->right->evaluate(env, fc).as_float()));
    } else if (left_val.is_string()) {
      return static_cast<int64_t>(combine(left_val.as_float(), this->right->evaluate(env, fc).as_float()));
    } else {
      throw std::logic_error("Unknown value type");
    }
  };
  switch (this->type) {
    case Type::LOGICAL_OR:
      return static_cast<int64_t>(this->left->evaluate(env, fc).truth_value() || this->right->evaluate(env, fc).truth_value());
    case Type::LOGICAL_AND:
      return static_cast<int64_t>(this->left->evaluate(env, fc).truth_value() && this->right->evaluate(env, fc).truth_value());
    case Type::BITWISE_OR:
      return this->left->evaluate(env, fc).as_int() | this->right->evaluate(env, fc).as_int();
    case Type::BITWISE_AND:
      return this->left->evaluate(env, fc).as_int() & this->right->evaluate(env, fc).as_int();
    case Type::BITWISE_XOR:
      return this->left->evaluate(env, fc).as_int() ^ this->right->evaluate(env, fc).as_int();
    case Type::LEFT_SHIFT:
      return this->left->evaluate(env, fc).as_int() << this->right->evaluate(env, fc).as_int();
    case Type::RIGHT_SHIFT:
      return this->left->evaluate(env, fc).as_int() >> this->right->evaluate(env, fc).as_int();
    case Type::LESS_THAN:
      return for_all_types_boolean([](auto a, auto b) { return a < b; });
    case Type::GREATER_THAN:
      return for_all_types_boolean([](auto a, auto b) { return a > b; });
    case Type::LESS_OR_EQUAL:
      return for_all_types_boolean([](auto a, auto b) { return a <= b; });
    case Type::GREATER_OR_EQUAL:
      return for_all_types_boolean([](auto a, auto b) { return a >= b; });
    case Type::EQUAL:
      return for_all_types_boolean([](auto a, auto b) { return a == b; });
    case Type::NOT_EQUAL:
      return for_all_types_boolean([](auto a, auto b) { return a != b; });
    case Type::ADD:
      return for_all_types([](auto a, auto b) { return a + b; });
    case Type::SUBTRACT:
      return for_numeric_types([](auto a, auto b) { return a - b; });
    case Type::MULTIPLY:
      return for_numeric_types([](auto a, auto b) { return a * b; });
    case Type::DIVIDE:
      return for_numeric_types([](auto a, auto b) { return a / b; });
    case Type::MODULUS:
      return static_cast<int64_t>(this->left->evaluate(env, fc).as_int() && this->right->evaluate(env, fc).as_int());
    default:
      throw std::logic_error("invalid binary operator type");
  }
}

std::string Expression::BinaryOperatorNode::str() const {
  switch (this->type) {
    case Type::LOGICAL_OR:
      return std::format("({}) || ({})", this->left->str(), this->right->str());
    case Type::LOGICAL_AND:
      return std::format("({}) && ({})", this->left->str(), this->right->str());
    case Type::BITWISE_OR:
      return std::format("({}) | ({})", this->left->str(), this->right->str());
    case Type::BITWISE_AND:
      return std::format("({}) & ({})", this->left->str(), this->right->str());
    case Type::BITWISE_XOR:
      return std::format("({}) ^ ({})", this->left->str(), this->right->str());
    case Type::LEFT_SHIFT:
      return std::format("({}) << ({})", this->left->str(), this->right->str());
    case Type::RIGHT_SHIFT:
      return std::format("({}) >> ({})", this->left->str(), this->right->str());
    case Type::LESS_THAN:
      return std::format("({}) < ({})", this->left->str(), this->right->str());
    case Type::GREATER_THAN:
      return std::format("({}) > ({})", this->left->str(), this->right->str());
    case Type::LESS_OR_EQUAL:
      return std::format("({}) <= ({})", this->left->str(), this->right->str());
    case Type::GREATER_OR_EQUAL:
      return std::format("({}) >= ({})", this->left->str(), this->right->str());
    case Type::EQUAL:
      return std::format("({}) == ({})", this->left->str(), this->right->str());
    case Type::NOT_EQUAL:
      return std::format("({}) != ({})", this->left->str(), this->right->str());
    case Type::ADD:
      return std::format("({}) + ({})", this->left->str(), this->right->str());
    case Type::SUBTRACT:
      return std::format("({}) - ({})", this->left->str(), this->right->str());
    case Type::MULTIPLY:
      return std::format("({}) * ({})", this->left->str(), this->right->str());
    case Type::DIVIDE:
      return std::format("({}) / ({})", this->left->str(), this->right->str());
    case Type::MODULUS:
      return std::format("({}) % ({})", this->left->str(), this->right->str());
    default:
      throw std::logic_error("invalid binary operator type");
  }
}

Expression::UnaryOperatorNode::UnaryOperatorNode(Type type, std::unique_ptr<Node>&& sub)
    : type(type), sub(std::move(sub)) {}

bool Expression::UnaryOperatorNode::operator==(const Node& other) const {
  const auto* other_un = dynamic_cast<const UnaryOperatorNode*>(&other);
  return other_un && (other_un->type == this->type) && (*other_un->sub == *this->sub);
}

bool UnaryOperatorNode::is_const(const EnvIsConst& env_is_const) const {
  return this->sub->is_const(env_is_const);
}

Value Expression::UnaryOperatorNode::evaluate(const EnvLookup& env, const FunctionCall& fc) const {
  auto for_numeric_types = [&](auto combine) -> Value {
    auto val = this->sub->evaluate(env, fc);
    if (val.is_int()) {
      return combine(val.as_int());
    } else if (val.is_float()) {
      return combine(val.as_float());
    } else if (val.is_string()) {
      throw std::runtime_error("A numeric value is required");
    } else {
      throw std::logic_error("Unknown value type");
    }
  };
  switch (this->type) {
    case Type::LOGICAL_NOT:
      return static_cast<int64_t>(!this->sub->evaluate(env, fc).truth_value());
    case Type::BITWISE_NOT:
      return ~this->sub->evaluate(env, fc).as_int();
    case Type::POSITIVE:
      return for_numeric_types([](auto val) { return +val; });
    case Type::NEGATIVE:
      return for_numeric_types([](auto val) { return -val; });
    default:
      throw std::logic_error("invalid unary operator type");
  }
}

std::string Expression::UnaryOperatorNode::str() const {
  switch (this->type) {
    case Type::LOGICAL_NOT:
      return std::format("!({})", this->sub->str());
    case Type::BITWISE_NOT:
      return std::format("~({})", this->sub->str());
    case Type::POSITIVE:
      return std::format("+({})", this->sub->str());
    case Type::NEGATIVE:
      return std::format("-({})", this->sub->str());
    default:
      throw std::logic_error("invalid unary operator type");
  }
}

Expression::ConstantNode::ConstantNode(Value value) : value(value) {}

bool Expression::ConstantNode::operator==(const Node& other) const {
  const auto* other_const = dynamic_cast<const ConstantNode*>(&other);
  return other_const && (other_const->value == this->value);
}

bool ConstantNode::is_const(const EnvIsConst&) const {
  return true;
}

Value Expression::ConstantNode::evaluate(const EnvLookup&, const FunctionCall&) const {
  return this->value;
}

std::string Expression::ConstantNode::str() const {
  return this->value.str();
}

Expression::EnvLookupNode::EnvLookupNode(const std::string& name) : name(name) {}
Expression::EnvLookupNode::EnvLookupNode(std::string_view name) : name(name) {}

bool Expression::EnvLookupNode::operator==(const Node& other) const {
  const auto* other_lookup = dynamic_cast<const EnvLookupNode*>(&other);
  return other_lookup && (other_lookup->name == this->name);
}

bool EnvLookupNode::is_const(const EnvIsConst& env_is_const) const {
  return env_is_const(this->name);
}

Value Expression::EnvLookupNode::evaluate(const EnvLookup& lookup, const FunctionCall&) const {
  if (!lookup) {
    throw std::runtime_error("Cannot look up dynamic value");
  }
  return lookup(this->name);
}

std::string Expression::EnvLookupNode::str() const {
  return std::format("{}", this->name);
}

std::unique_ptr<Node> Node::parse(std::string_view text) {
  // Strip off spaces and fully-enclosing parentheses
  for (;;) {
    size_t starting_size = text.size();
    while (text.at(0) == ' ') {
      text = text.substr(1);
    }
    while (text.at(text.size() - 1) == ' ') {
      text = text.substr(0, text.size() - 1);
    }
    if (text.at(0) == '(' && text.at(text.size() - 1) == ')') {
      // It doesn't suffice to just check the first and last characters, since text could be like "(a) && (b)".
      // Instead, we ignore the first and last characters, and don't strip anything if the internal parentheses are
      // unbalanced (that is, paren_level ever goes below 1).
      size_t paren_level = 1;
      for (size_t z = 1; z < text.size() - 1; z++) {
        if (text[z] == '(') {
          paren_level++;
        } else if (text[z] == ')') {
          paren_level--;
          if (paren_level == 0) {
            break;
          }
        }
      }
      if (paren_level > 0) {
        text = text.substr(1, text.size() - 2);
      }
    }
    if (text.size() == starting_size) {
      break;
    }
  }
  if (text.empty()) {
    throw std::invalid_argument("invalid expression");
  }

  // Check for unary operators
  if (text[0] == '!') {
    return std::make_unique<UnaryOperatorNode>(UnaryOperatorNode::Type::LOGICAL_NOT, Node::parse(text.substr(1)));
  } else if (text[0] == '~') {
    return std::make_unique<UnaryOperatorNode>(UnaryOperatorNode::Type::BITWISE_NOT, Node::parse(text.substr(1)));
  } else if (text[0] == '+') {
    return std::make_unique<UnaryOperatorNode>(UnaryOperatorNode::Type::POSITIVE, Node::parse(text.substr(1)));
  } else if (text[0] == '-') {
    return std::make_unique<UnaryOperatorNode>(UnaryOperatorNode::Type::NEGATIVE, Node::parse(text.substr(1)));
  }

  // Check for binary operators at the root level
  using BinType = BinaryOperatorNode::Type;
  static const std::vector<std::vector<std::pair<std::string, BinaryOperatorNode::Type>>> binary_operator_levels = {
      {{std::make_pair("||", BinType::LOGICAL_OR)}},
      {{std::make_pair("&&", BinType::LOGICAL_AND)}},
      {{std::make_pair("|", BinType::BITWISE_OR)}},
      {{std::make_pair("^", BinType::BITWISE_XOR)}},
      {{std::make_pair("&", BinType::BITWISE_AND)}},
      {{std::make_pair("==", BinType::EQUAL)}, {std::make_pair("!=", BinType::NOT_EQUAL)}},
      {{std::make_pair("<=", BinType::LESS_OR_EQUAL)}, {std::make_pair(">=", BinType::GREATER_OR_EQUAL)}, {std::make_pair("<", BinType::LESS_THAN)}, {std::make_pair(">", BinType::GREATER_THAN)}},
      {{std::make_pair("<<", BinType::LEFT_SHIFT)}, {std::make_pair(">>", BinType::RIGHT_SHIFT)}},
      {{std::make_pair("+", BinType::ADD)}, {std::make_pair("-", BinType::SUBTRACT)}},
      {{std::make_pair("*", BinType::MULTIPLY)}, {std::make_pair("/", BinType::DIVIDE)}, {std::make_pair("%", BinType::MODULUS)}},
  };
  for (const auto& operators : binary_operator_levels) {
    size_t paren_level = 0;
    for (size_t z = 0; z < text.size() - 1; z++) {
      if (text[z] == '(') {
        paren_level++;
        continue;
      } else if (text[z] == ')') {
        paren_level--;
        continue;
      }
      if (!paren_level) {
        for (const auto& oper : operators) {
          // Awful hack (because I'm too lazy to add a tokenization step): if the operator is followed or preceded by
          // another copy of itself, don't match it (this prevents us from matching & when the token is actually &&)
          if ((text.size() > z + oper.first.size()) &&
              ((z < oper.first.size()) || (text.compare(z - oper.first.size(), oper.first.size(), oper.first) != 0)) &&
              (text.compare(z, oper.first.size(), oper.first) == 0) &&
              (text.compare(z + oper.first.size(), oper.first.size(), oper.first) != 0)) {
            auto left = Node::parse(text.substr(0, z));
            auto right = Node::parse(text.substr(z + oper.first.size()));
            return std::make_unique<BinaryOperatorNode>(oper.second, std::move(left), std::move(right));
          }
        }
      }
    }
  }

  // Check for ternary operator at the root level
  {
    size_t qmark_offset = 0;
    size_t colon_offset = 0;
    size_t paren_level = 0;
    // Start at z = 1, since there must be something before the operator
    for (size_t z = 1; z < text.size() - 1; z++) {
      if (text[z] == '(') {
        paren_level++;
        continue;
      } else if (text[z] == ')') {
        paren_level--;
        continue;
      } else if (!paren_level && text[z] == '?' && !qmark_offset) {
        qmark_offset = z;
      } else if (!paren_level && text[z] == ':' && !colon_offset) {
        colon_offset = z;
      }
      if (qmark_offset > 0 && colon_offset > 0 && colon_offset > qmark_offset) {
        auto left = Node::parse(text.substr(0, qmark_offset));
        auto center = Node::parse(text.substr(qmark_offset + 1, colon_offset - qmark_offset - 1));
        auto right = Node::parse(text.substr(colon_offset + 1));
        return std::make_unique<TernaryOperatorNode>(
            TernaryOperatorNode::Type::CONDITION_SELECTOR, std::move(left), std::move(center), std::move(right));
      }
    }
  }

  // Check for constants
  if (text == "true") {
    return std::make_unique<ConstantNode>(static_cast<int64_t>(1));
  }
  if (text == "false") {
    return std::make_unique<ConstantNode>(static_cast<int64_t>(0));
  }
  if (isdigit(text[0])) {
    try {
      size_t endpos;
      if (text.contains('.')) {
        double v = std::stod(std::string(text), &endpos);
        if (endpos == text.size()) {
          return std::make_unique<ConstantNode>(v);
        }
      } else {
        int64_t v = std::stoll(std::string(text), &endpos, 0);
        if (endpos == text.size()) {
          return std::make_unique<ConstantNode>(v);
        }
      }
    } catch (const std::exception&) {
    }
  }

  // Check for lookups (must match the regex [A-Za-z_:][A-Za-z0-9_:]*, like var names in many programming languages)
  if (text.empty()) {
    throw std::invalid_argument("No expression to parse");
  }
  if (!isalpha(text[0]) && (text[0] != '_') && (text[0] != ':')) {
    throw std::invalid_argument("Expression does not begin with an identifier");
  }
  size_t identifier_length = 0;
  for (identifier_length = 0; identifier_length < text.size(); identifier_length++) {
    if (!isalnum(text[identifier_length]) && (text[identifier_length] != '_') && (text[identifier_length] != ':')) {
      break;
    }
  }
  if (identifier_length == text.size()) {
    return std::make_unique<EnvLookupNode>(text);
  }
  if (text[identifier_length] == '(' && text.ends_with(')')) {
    std::vector<std::unique_ptr<Node>> arg_nodes;
    std::string_view args_str = text.substr(identifier_length + 1, text.size() - identifier_length - 2);
    for (const auto& arg : phosg::split_context_view(args_str, ',')) {
      arg_nodes.emplace_back(Node::parse(arg));
    }
    return std::make_unique<FunctionCallNode>(text.substr(0, identifier_length), std::move(arg_nodes));
  }

  throw std::invalid_argument("Unparseable expression");
}

} // namespace Expression
} // namespace ResourceDASM
