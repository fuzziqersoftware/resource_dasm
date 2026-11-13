#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "Expression.hh"
#include "ExpressionFunctions.hh"

namespace ResourceDASM {
namespace Expression {

std::partial_ordering Value::operator<=>(const Value& other) const {
  if (this->is_int() && other.is_int()) {
    return this->as_int() <=> other.as_int();
  } else if (this->is_int() && other.is_float()) {
    return this->as_int() <=> other.as_float();
  } else if (this->is_float() && other.is_int()) {
    return this->as_float() <=> other.as_int();
  } else if (this->is_float() && other.is_float()) {
    return this->as_float() <=> other.as_float();
  } else if (this->is_string() && other.is_string()) {
    return this->as_string() <=> other.as_string();
  } else {
    return std::partial_ordering::unordered;
  }
}

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
    std::string ret = std::format("{}", this->as_float());
    if (isfinite(this->as_float()) && !ret.contains('.') && !ret.contains('e')) {
      ret += ".0";
    }
    return ret;
  } else if (this->is_string()) {
    return hex
        ? phosg::format_data_string(this->as_string(), phosg::FormatDataStringFlags::HEX_ONLY)
        : std::format("\"{}\"", phosg::escape_quotes(this->as_string()));
  } else {
    throw std::logic_error("Unknown value type");
  }
}

FunctionCallNode::FunctionCallNode(std::string_view function_name, std::vector<std::unique_ptr<Node>>&& args)
    : function_name(function_name), args(std::move(args)) {}

std::unique_ptr<Node> FunctionCallNode::clone() const {
  std::vector<std::unique_ptr<Node>> new_args;
  new_args.reserve(this->args.size());
  for (const auto& arg : this->args) {
    new_args.emplace_back(arg->clone());
  }
  return std::make_unique<FunctionCallNode>(this->function_name, std::move(new_args));
}

void FunctionCallNode::accept(Visitor* visitor) const {
  visitor->visit_function_call(this);
}

TernaryOperatorNode::TernaryOperatorNode(
    Type type, std::unique_ptr<Node>&& left, std::unique_ptr<Node>&& center, std::unique_ptr<Node>&& right)
    : type(type), left(std::move(left)), center(std::move(center)), right(std::move(right)) {}

std::unique_ptr<Node> TernaryOperatorNode::clone() const {
  return std::make_unique<TernaryOperatorNode>(
      this->type, this->left->clone(), this->center->clone(), this->right->clone());
}

void TernaryOperatorNode::accept(Visitor* visitor) const {
  visitor->visit_ternary_operator(this);
}

BinaryOperatorNode::BinaryOperatorNode(
    Type type, std::unique_ptr<Node>&& left, std::unique_ptr<Node>&& right)
    : type(type), left(std::move(left)), right(std::move(right)) {}

std::unique_ptr<Node> BinaryOperatorNode::clone() const {
  return std::make_unique<BinaryOperatorNode>(this->type, this->left->clone(), this->right->clone());
}

void BinaryOperatorNode::accept(Visitor* visitor) const {
  visitor->visit_binary_operator(this);
}

UnaryOperatorNode::UnaryOperatorNode(Type type, std::unique_ptr<Node>&& sub)
    : type(type), sub(std::move(sub)) {}

std::unique_ptr<Node> UnaryOperatorNode::clone() const {
  return std::make_unique<UnaryOperatorNode>(this->type, this->sub->clone());
}

void UnaryOperatorNode::accept(Visitor* visitor) const {
  visitor->visit_unary_operator(this);
}

ConstantNode::ConstantNode(Value value) : value(value) {}

std::unique_ptr<Node> ConstantNode::clone() const {
  return std::make_unique<ConstantNode>(this->value);
}

void ConstantNode::accept(Visitor* visitor) const {
  visitor->visit_constant(this);
}

EnvLookupNode::EnvLookupNode(const std::string& name) : name(name) {}
EnvLookupNode::EnvLookupNode(std::string_view name) : name(name) {}

std::unique_ptr<Node> EnvLookupNode::clone() const {
  return std::make_unique<EnvLookupNode>(this->name);
}

void EnvLookupNode::accept(Visitor* visitor) const {
  visitor->visit_env_lookup(this);
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

  // Check for ternary operator at the root level
  {
    size_t qmark_offset = 0;
    size_t colon_offset = 0;
    size_t paren_level = 0;
    for (size_t z = 0; z < text.size() - 1; z++) {
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
    ssize_t paren_level = 0;
    // We scan in reverse so we'll preserve operator order in expressions like "A - B - C" - that should parse as
    // (A - B) - C, not A - (B - C).
    for (ssize_t z = text.size() - 1; z >= 0; z--) {
      if (text[z] == ')') {
        paren_level++;
        continue;
      } else if (text[z] == '(') {
        paren_level--;
        if (paren_level < 0) {
          throw std::invalid_argument("unbalanced parentheses in expression");
        }
        continue;
      }
      // Only match an operator if it's outside of any parentheses and is not at the very beginning (if it is, then
      // it's a unary operator, which we will parse later)
      if (!paren_level && (z > 0)) {
        for (const auto& oper : operators) {
          // Awful hack (because I'm too lazy to add a tokenization step): if the operator is followed or preceded by
          // another copy of itself, don't match it (this prevents us from matching & when the token is actually &&)
          if ((text.size() > z + oper.first.size()) &&
              ((static_cast<size_t>(z) < oper.first.size()) || (text.compare(z - oper.first.size(), oper.first.size(), oper.first) != 0)) &&
              (text.compare(z, oper.first.size(), oper.first) == 0) &&
              (text.compare(z + oper.first.size(), oper.first.size(), oper.first) != 0)) {
            auto left = Node::parse(text.substr(0, z));
            auto right = Node::parse(text.substr(z + oper.first.size()));
            return std::make_unique<BinaryOperatorNode>(oper.second, std::move(left), std::move(right));
          }
        }
      }
    }
    if (paren_level != 0) {
      throw std::invalid_argument("unbalanced parentheses in expression");
    }
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

  // Check for lookups (must match the regex [A-Za-z_:.][A-Za-z0-9_:.]*, like var names in many programming languages)
  if (text.empty()) {
    throw std::invalid_argument("No expression to parse");
  }
  if (!isalpha(text[0]) && (text[0] != '_') && (text[0] != ':') && (text[0] != '.')) {
    throw std::invalid_argument(std::format("Expression does not begin with an identifier: {}", text));
  }
  size_t identifier_length = 0;
  for (identifier_length = 0; identifier_length < text.size(); identifier_length++) {
    if (!isalnum(text[identifier_length]) && (text[identifier_length] != '_') && (text[identifier_length] != ':') && (text[identifier_length] != '.')) {
      break;
    }
  }
  if (identifier_length == text.size()) {
    return std::make_unique<EnvLookupNode>(text);
  }
  if (text[identifier_length] == '(' && text.ends_with(')')) {
    std::vector<std::unique_ptr<Node>> arg_nodes;
    std::string_view args_str = text.substr(identifier_length + 1, text.size() - identifier_length - 2);
    if (!args_str.empty()) {
      for (const auto& arg : phosg::split_context_view(args_str, ',')) {
        arg_nodes.emplace_back(Node::parse(arg));
      }
    }
    return std::make_unique<FunctionCallNode>(text.substr(0, identifier_length), std::move(arg_nodes));
  }

  throw std::invalid_argument(std::format("Unparseable expression: {}", text));
}

bool Node::operator==(const Node& other) const {
  class EqualityVisitor : public Visitor {
  public:
    const Node* other;
    bool ret;

    EqualityVisitor(const Node* other) : other(other), ret(true) {}

    virtual void visit_function_call(const FunctionCallNode* node) {
      const auto* other_fc = dynamic_cast<const FunctionCallNode*>(this->other);
      if (!other_fc ||
          (other_fc->function_name != node->function_name) ||
          (other_fc->args.size() != node->args.size())) {
        this->ret = false;
      } else {
        for (size_t z = 0; z < node->args.size(); z++) {
          this->other = other_fc->args[z].get();
          node->args[z]->accept(this);
          if (!this->ret) {
            break;
          }
        }
      }
    }

    virtual void visit_ternary_operator(const TernaryOperatorNode* node) {
      const auto* other_tern = dynamic_cast<const TernaryOperatorNode*>(this->other);
      if (!other_tern || (other_tern->type != node->type)) {
        this->ret = false;
      } else {
        this->other = other_tern->left.get();
        node->left->accept(this);
        if (this->ret) {
          this->other = other_tern->center.get();
          node->center->accept(this);
          if (this->ret) {
            this->other = other_tern->right.get();
            node->right->accept(this);
          }
        }
      }
    }

    virtual void visit_binary_operator(const BinaryOperatorNode* node) {
      const auto* other_bin = dynamic_cast<const BinaryOperatorNode*>(this->other);
      if (!other_bin || (other_bin->type != node->type)) {
        this->ret = false;
      } else {
        this->other = other_bin->left.get();
        node->left->accept(this);
        if (this->ret) {
          this->other = other_bin->right.get();
          node->right->accept(this);
        }
      }
    }

    virtual void visit_unary_operator(const UnaryOperatorNode* node) {
      const auto* other_un = dynamic_cast<const UnaryOperatorNode*>(this->other);
      if (!other_un || (other_un->type != node->type)) {
        this->ret = false;
      } else {
        this->other = other_un->sub.get();
        node->sub->accept(this);
      }
    }

    virtual void visit_constant(const ConstantNode* node) {
      const auto* other_const = dynamic_cast<const ConstantNode*>(this->other);
      if (!other_const || (other_const->value != node->value)) {
        this->ret = false;
      }
    }

    virtual void visit_env_lookup(const EnvLookupNode* node) {
      const auto* other_lookup = dynamic_cast<const EnvLookupNode*>(this->other);
      if (!other_lookup || (other_lookup->name != node->name)) {
        this->ret = false;
      }
    }
  };

  EqualityVisitor visitor{&other};
  this->accept(&visitor);
  return visitor.ret;
}

bool Node::is_const(const EnvIsConst& env_is_const) const {
  class IsConstVisitor : public Visitor {
  public:
    const EnvIsConst& env_is_const;
    bool ret;

    IsConstVisitor(const EnvIsConst& env_is_const) : env_is_const(env_is_const), ret(true) {}

    virtual void visit_function_call(const FunctionCallNode* node) {
      // NOTE: For now, all functions are deterministic, so we assume the call itself is constant. I can't imagine this
      // would ever NOT be the case, but if it ever isn't, we should update this logic here.
      for (const auto& arg : node->args) {
        arg->accept(this);
        if (!this->ret) {
          break;
        }
      }
    }

    virtual void visit_ternary_operator(const TernaryOperatorNode* node) {
      node->left->accept(this);
      if (this->ret) {
        node->center->accept(this);
        if (this->ret) {
          node->right->accept(this);
        }
      }
    }

    virtual void visit_binary_operator(const BinaryOperatorNode* node) {
      node->left->accept(this);
      if (this->ret) {
        node->right->accept(this);
      }
    }

    virtual void visit_unary_operator(const UnaryOperatorNode* node) {
      node->sub->accept(this);
    }

    // visit_constant does nothing (constant nodes are constant)

    virtual void visit_env_lookup(const EnvLookupNode* node) {
      this->ret = this->env_is_const(node->name);
    }
  };

  IsConstVisitor visitor{env_is_const};
  this->accept(&visitor);
  return visitor.ret;
}

std::vector<const Node*> Node::find_env_lookup(const std::string& name) const {
  class FindEnvLookupVisitor : public Visitor {
  public:
    const std::string& name;
    std::vector<const Node*> found_path;
    std::vector<const Node*> current_path;

    FindEnvLookupVisitor(const std::string& name) : name(name) {}

    virtual void visit_function_call(const FunctionCallNode* node) {
      this->current_path.emplace_back(node);
      for (const auto& arg : node->args) {
        arg->accept(this);
      }
      this->current_path.pop_back();
    }

    virtual void visit_ternary_operator(const TernaryOperatorNode* node) {
      this->current_path.emplace_back(node);
      node->left->accept(this);
      node->center->accept(this);
      node->right->accept(this);
      this->current_path.pop_back();
    }

    virtual void visit_binary_operator(const BinaryOperatorNode* node) {
      this->current_path.emplace_back(node);
      node->left->accept(this);
      node->right->accept(this);
      this->current_path.pop_back();
    }

    virtual void visit_unary_operator(const UnaryOperatorNode* node) {
      this->current_path.emplace_back(node);
      node->sub->accept(this);
      this->current_path.pop_back();
    }

    // visit_constant does nothing (constant nodes have no children and cannot be found by this function)

    virtual void visit_env_lookup(const EnvLookupNode* node) {
      if (node->name == this->name) {
        if (!this->found_path.empty()) {
          throw std::runtime_error("Multiple env lookups were found");
        }
        this->found_path = this->current_path;
        this->found_path.emplace_back(node);
      }
    }
  };

  FindEnvLookupVisitor visitor{name};
  this->accept(&visitor);
  if (visitor.found_path.empty()) {
    throw std::runtime_error("Env lookup was not found");
  }
  return std::move(visitor.found_path);
}

static Value evaluate_binary_operator(BinaryOperatorNode::Type type, const Value& left_val, const Value& right_val) {
  auto for_truth_values = [&](auto combine) -> Value {
    return static_cast<int64_t>(combine(left_val.truth_value(), right_val.truth_value()));
  };
  auto for_ints = [&](auto combine) -> Value {
    if (left_val.is_int()) {
      return combine(left_val.as_int(), right_val.as_int());
    } else if (left_val.is_float()) {
      throw std::runtime_error("Integer values are required");
    } else {
      throw std::logic_error("Unknown value type");
    }
  };
  auto for_numeric_types = [&](auto combine) -> Value {
    if (left_val.is_int() && right_val.is_int()) {
      return combine(left_val.as_int(), right_val.as_int());
    } else if (left_val.is_int() && right_val.is_float()) {
      return combine(left_val.as_int(), right_val.as_float());
    } else if (left_val.is_float() && right_val.is_int()) {
      return combine(left_val.as_float(), right_val.as_int());
    } else if (left_val.is_float() && right_val.is_float()) {
      return combine(left_val.as_float(), right_val.as_float());
    } else if (left_val.is_string()) {
      throw std::runtime_error("Numeric values are required");
    } else {
      throw std::logic_error("Unknown value type");
    }
  };
  auto for_all_types = [&](auto combine) -> Value {
    if (left_val.is_int() && right_val.is_int()) {
      return combine(left_val.as_int(), right_val.as_int());
    } else if (left_val.is_int() && right_val.is_float()) {
      return combine(left_val.as_int(), right_val.as_float());
    } else if (left_val.is_float() && right_val.is_int()) {
      return combine(left_val.as_float(), right_val.as_int());
    } else if (left_val.is_float() && right_val.is_float()) {
      return combine(left_val.as_float(), right_val.as_float());
    } else if (left_val.is_string()) {
      return combine(left_val.as_string(), right_val.as_string());
    } else {
      throw std::logic_error("Unknown value type");
    }
  };
  auto for_all_types_boolean = [&](auto combine) -> Value {
    if (left_val.is_int() && right_val.is_int()) {
      return static_cast<int64_t>(combine(left_val.as_int(), right_val.as_int()));
    } else if (left_val.is_int() && right_val.is_float()) {
      return static_cast<int64_t>(combine(left_val.as_int(), right_val.as_float()));
    } else if (left_val.is_float() && right_val.is_int()) {
      return static_cast<int64_t>(combine(left_val.as_float(), right_val.as_int()));
    } else if (left_val.is_float() && right_val.is_float()) {
      return static_cast<int64_t>(combine(left_val.as_float(), right_val.as_float()));
    } else if (left_val.is_string()) {
      return static_cast<int64_t>(combine(left_val.as_string(), right_val.as_string()));
    } else {
      throw std::logic_error("Unknown value type");
    }
  };
  switch (type) {
    case BinaryOperatorNode::Type::LOGICAL_OR:
      return for_truth_values([](bool a, bool b) { return a || b; });
    case BinaryOperatorNode::Type::LOGICAL_AND:
      return for_truth_values([](bool a, bool b) { return a && b; });
    case BinaryOperatorNode::Type::BITWISE_OR:
      return for_ints([](int64_t a, int64_t b) { return a | b; });
    case BinaryOperatorNode::Type::BITWISE_AND:
      return for_ints([](int64_t a, int64_t b) { return a & b; });
    case BinaryOperatorNode::Type::BITWISE_XOR:
      return for_ints([](int64_t a, int64_t b) { return a ^ b; });
    case BinaryOperatorNode::Type::LEFT_SHIFT:
      return for_ints([](int64_t a, int64_t b) { return a << b; });
    case BinaryOperatorNode::Type::RIGHT_SHIFT:
      return for_ints([](int64_t a, int64_t b) { return a >> b; });
    case BinaryOperatorNode::Type::LESS_THAN:
      return for_all_types_boolean([](auto a, auto b) { return a < b; });
    case BinaryOperatorNode::Type::GREATER_THAN:
      return for_all_types_boolean([](auto a, auto b) { return a > b; });
    case BinaryOperatorNode::Type::LESS_OR_EQUAL:
      return for_all_types_boolean([](auto a, auto b) { return a <= b; });
    case BinaryOperatorNode::Type::GREATER_OR_EQUAL:
      return for_all_types_boolean([](auto a, auto b) { return a >= b; });
    case BinaryOperatorNode::Type::EQUAL:
      return for_all_types_boolean([](auto a, auto b) { return a == b; });
    case BinaryOperatorNode::Type::NOT_EQUAL:
      return for_all_types_boolean([](auto a, auto b) { return a != b; });
    case BinaryOperatorNode::Type::ADD:
      return for_all_types([](auto a, auto b) { return a + b; });
    case BinaryOperatorNode::Type::SUBTRACT:
      return for_numeric_types([](auto a, auto b) { return a - b; });
    case BinaryOperatorNode::Type::MULTIPLY:
      return for_numeric_types([](auto a, auto b) { return a * b; });
    case BinaryOperatorNode::Type::DIVIDE:
      return for_numeric_types([](auto a, auto b) { return a / b; });
    case BinaryOperatorNode::Type::MODULUS:
      return for_ints([](int64_t a, int64_t b) { return a % b; });
    default:
      throw std::logic_error("invalid binary operator type");
  }
}

static Value evaluate_unary_operator(UnaryOperatorNode::Type type, const Value& sub) {
  auto for_numeric_types = [&](auto combine) -> Value {
    if (sub.is_int()) {
      return combine(sub.as_int());
    } else if (sub.is_float()) {
      return combine(sub.as_float());
    } else if (sub.is_string()) {
      throw std::runtime_error("A numeric value is required");
    } else {
      throw std::logic_error("Unknown value type");
    }
  };
  switch (type) {
    case UnaryOperatorNode::Type::LOGICAL_NOT:
      return static_cast<int64_t>(!sub.truth_value());
    case UnaryOperatorNode::Type::BITWISE_NOT:
      return ~sub.as_int();
    case UnaryOperatorNode::Type::POSITIVE:
      return for_numeric_types([](auto val) { return +val; });
    case UnaryOperatorNode::Type::NEGATIVE:
      return for_numeric_types([](auto val) { return -val; });
    default:
      throw std::logic_error("invalid unary operator type");
  }
}

Value Node::evaluate(const EnvLookup& env, const FunctionCall& function_call) const {
  class EvaluateVisitor : public Visitor {
  public:
    const EnvLookup& env;
    const FunctionCall& function_call;
    Value last_value;

    EvaluateVisitor(const EnvLookup& env, const FunctionCall& function_call)
        : env(env), function_call(function_call), last_value(static_cast<int64_t>(0)) {}

    virtual void visit_function_call(const FunctionCallNode* node) {
      std::vector<Value> arg_values;
      arg_values.reserve(node->args.size());
      for (const auto& arg : node->args) {
        arg->accept(this);
        arg_values.emplace_back(std::move(this->last_value));
      }
      this->last_value = this->function_call
          ? this->function_call(node->function_name, arg_values)
          : FunctionDefinition::dispatch_call(node->function_name, arg_values);
    }

    virtual void visit_ternary_operator(const TernaryOperatorNode* node) {
      switch (node->type) {
        case TernaryOperatorNode::Type::CONDITION_SELECTOR:
          node->left->accept(this);
          if (this->last_value.truth_value()) {
            node->center->accept(this);
          } else {
            node->right->accept(this);
          }
          break;
        default:
          throw std::logic_error("invalid ternary operator type");
      }
    }

    virtual void visit_binary_operator(const BinaryOperatorNode* node) {
      node->left->accept(this);
      Value left_val = std::move(this->last_value);
      node->right->accept(this);
      this->last_value = evaluate_binary_operator(node->type, left_val, this->last_value);
    }

    virtual void visit_unary_operator(const UnaryOperatorNode* node) {
      node->sub->accept(this);
      this->last_value = evaluate_unary_operator(node->type, this->last_value);
    }

    virtual void visit_constant(const ConstantNode* node) {
      this->last_value = node->value;
    }

    virtual void visit_env_lookup(const EnvLookupNode* node) {
      if (!this->env) {
        throw std::runtime_error("Cannot look up dynamic value");
      }
      this->last_value = this->env(node->name);
    }
  };

  EvaluateVisitor visitor{env, function_call};
  this->accept(&visitor);
  return std::move(visitor.last_value);
}

std::string Node::str(const EnvLookup& env) const {
  class StringVisitor : public Visitor {
  public:
    const EnvLookup& env;
    std::string last_str;

    StringVisitor(const EnvLookup& env) : env(env) {}

    virtual void visit_function_call(const FunctionCallNode* node) {
      this->last_str = std::format("{}(", node->function_name);
      for (size_t z = 0; z < node->args.size(); z++) {
        if (z) {
          this->last_str += ", ";
        }
        this->last_str += node->args[z]->str(env);
      }
      this->last_str += ")";
    }

    virtual void visit_ternary_operator(const TernaryOperatorNode* node) {
      switch (node->type) {
        case TernaryOperatorNode::Type::CONDITION_SELECTOR: {
          node->left->accept(this);
          std::string left_str = std::move(this->last_str);
          node->center->accept(this);
          std::string center_str = std::move(this->last_str);
          node->right->accept(this);
          const std::string& right_str = this->last_str;
          this->last_str = std::format("({}) ? ({}) : ({})", left_str, center_str, right_str);
          break;
        }
        default:
          throw std::logic_error("Invalid ternary operator type");
      }
    }

    virtual void visit_binary_operator(const BinaryOperatorNode* node) {
      node->left->accept(this);
      std::string left_str = std::move(this->last_str);
      node->right->accept(this);
      const std::string& right_str = this->last_str;

      switch (node->type) {
        case BinaryOperatorNode::Type::LOGICAL_OR:
          this->last_str = std::format("({}) || ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::LOGICAL_AND:
          this->last_str = std::format("({}) && ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::BITWISE_OR:
          this->last_str = std::format("({}) | ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::BITWISE_AND:
          this->last_str = std::format("({}) & ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::BITWISE_XOR:
          this->last_str = std::format("({}) ^ ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::LEFT_SHIFT:
          this->last_str = std::format("({}) << ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::RIGHT_SHIFT:
          this->last_str = std::format("({}) >> ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::LESS_THAN:
          this->last_str = std::format("({}) < ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::GREATER_THAN:
          this->last_str = std::format("({}) > ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::LESS_OR_EQUAL:
          this->last_str = std::format("({}) <= ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::GREATER_OR_EQUAL:
          this->last_str = std::format("({}) >= ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::EQUAL:
          this->last_str = std::format("({}) == ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::NOT_EQUAL:
          this->last_str = std::format("({}) != ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::ADD:
          this->last_str = std::format("({}) + ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::SUBTRACT:
          this->last_str = std::format("({}) - ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::MULTIPLY:
          this->last_str = std::format("({}) * ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::DIVIDE:
          this->last_str = std::format("({}) / ({})", left_str, right_str);
          break;
        case BinaryOperatorNode::Type::MODULUS:
          this->last_str = std::format("({}) % ({})", left_str, right_str);
          break;
        default:
          throw std::logic_error("Invalid binary operator type");
      }
    }

    virtual void visit_unary_operator(const UnaryOperatorNode* node) {
      node->sub->accept(this);
      switch (node->type) {
        case UnaryOperatorNode::Type::LOGICAL_NOT:
          this->last_str = std::format("!({})", this->last_str);
          break;
        case UnaryOperatorNode::Type::BITWISE_NOT:
          this->last_str = std::format("~({})", this->last_str);
          break;
        case UnaryOperatorNode::Type::POSITIVE:
          this->last_str = std::format("+({})", this->last_str);
          break;
        case UnaryOperatorNode::Type::NEGATIVE:
          this->last_str = std::format("-({})", this->last_str);
          break;
        default:
          throw std::logic_error("invalid unary operator type");
      }
    }

    virtual void visit_constant(const ConstantNode* node) {
      this->last_str = node->value.str();
    }

    virtual void visit_env_lookup(const EnvLookupNode* node) {
      this->last_str = this->env ? this->env(node->name).str() : std::format("{}", node->name);
    }
  };

  StringVisitor visitor{env};
  this->accept(&visitor);
  return std::move(visitor.last_str);
}

std::unique_ptr<Node> Node::simplify(const OptionalEnvLookup& env, const OptionalFunctionCall& function_call) {
  class SimplifyVisitor : public Visitor {
  public:
    const OptionalEnvLookup& env;
    const OptionalFunctionCall& function_call;
    std::unique_ptr<Node> last_node;

    SimplifyVisitor(const OptionalEnvLookup& env, const OptionalFunctionCall& function_call)
        : env(env), function_call(function_call) {}

    virtual void visit_function_call(const FunctionCallNode* node) {
      bool all_values_const = true;
      std::vector<Value> arg_values;
      std::vector<std::unique_ptr<Node>> arg_nodes;
      arg_nodes.reserve(node->args.size());
      for (const auto& arg : node->args) {
        arg->accept(this);
        arg_nodes.emplace_back(std::move(this->last_node));
        if (all_values_const) {
          auto const_node = dynamic_cast<const ConstantNode*>(arg_nodes.back().get());
          if (!const_node) {
            arg_values.clear();
            all_values_const = false;
          } else {
            arg_values.emplace_back(const_node->value);
          }
        }
      }
      this->last_node.reset();
      if (all_values_const) {
        auto value = this->function_call
            ? this->function_call(node->function_name, arg_values)
            : FunctionDefinition::dispatch_call(node->function_name, arg_values);
        if (value.has_value()) {
          this->last_node = std::make_unique<ConstantNode>(std::move(*value));
        }
      }
      if (!this->last_node) {
        this->last_node = std::make_unique<FunctionCallNode>(node->function_name, std::move(arg_nodes));
      }
    }

    virtual void visit_ternary_operator(const TernaryOperatorNode* node) {
      switch (node->type) {
        case TernaryOperatorNode::Type::CONDITION_SELECTOR: {
          node->left->accept(this);
          auto left_const = dynamic_cast<const ConstantNode*>(this->last_node.get());
          if (left_const) {
            if (left_const->value.truth_value()) {
              node->center->accept(this);
            } else {
              node->right->accept(this);
            }
          } else {
            auto left = std::move(this->last_node);
            node->center->accept(this);
            auto center = std::move(this->last_node);
            node->right->accept(this);
            this->last_node = std::make_unique<TernaryOperatorNode>(
                node->type, std::move(left), std::move(center), std::move(this->last_node));
          }
          break;
        }
        default:
          throw std::logic_error("invalid ternary operator type");
      }
    }

    virtual void visit_binary_operator(const BinaryOperatorNode* node) {
      node->left->accept(this);
      auto left = std::move(this->last_node);
      node->right->accept(this);
      auto right = std::move(this->last_node);

      this->last_node.reset();
      const auto* left_const = dynamic_cast<ConstantNode*>(left.get());
      if (left_const) {
        const auto* right_const = dynamic_cast<ConstantNode*>(right.get());
        if (right_const) {
          this->last_node = std::make_unique<ConstantNode>(evaluate_binary_operator(
              node->type, left_const->value, right_const->value));
        }
      }
      if (!this->last_node) {
        this->last_node = std::make_unique<BinaryOperatorNode>(node->type, std::move(left), std::move(right));
      }
    }

    virtual void visit_unary_operator(const UnaryOperatorNode* node) {
      node->sub->accept(this);
      const auto* sub_const = dynamic_cast<ConstantNode*>(this->last_node.get());
      if (sub_const) {
        this->last_node = std::make_unique<ConstantNode>(evaluate_unary_operator(node->type, sub_const->value));
      } else {
        this->last_node = std::make_unique<UnaryOperatorNode>(node->type, std::move(this->last_node));
      }
    }

    virtual void visit_constant(const ConstantNode* node) {
      this->last_node = node->clone();
    }

    virtual void visit_env_lookup(const EnvLookupNode* node) {
      this->last_node.reset();
      if (this->env) {
        auto value = this->env(node->name);
        if (value.has_value()) {
          this->last_node = std::make_unique<ConstantNode>(std::move(*value));
        }
      }
      if (!this->last_node) {
        this->last_node = std::make_unique<EnvLookupNode>(node->name);
      }
    }
  };

  SimplifyVisitor visitor{env, function_call};
  this->accept(&visitor);
  return std::move(visitor.last_node);
}

std::unique_ptr<Node> Node::solve(const std::string& target_name, const std::string& result_name) const {
  // Step 1: walk tree, find all references to target_name; fail if there isn't exactly one
  // Step 2: walk down path from root to target_name, inverting every operation

  // Example: expr = ((a * 2) + (b * 3)) / ((c * 4) + (d * 5)), solve for d
  // Path down tree: division (denominator), addition (second addend), multiplication (first multiplicand)
  // ((a * 2) + (b * 3)) / ((c * 4) + (d * 5)) = result_name
  // ((a * 2) + (b * 3)) / result_name = (c * 4) + (d * 5)
  // (((a * 2) + (b * 3)) / result_name) - (c * 4) = (d * 5)
  // ((((a * 2) + (b * 3)) / result_name) - (c * 4)) / 5 = d

  auto path = this->find_env_lookup(target_name);
  if (path.empty()) {
    throw std::logic_error("find_env_lookup returned an empty path");
  }

  std::unique_ptr<Node> ret_expr = std::make_unique<EnvLookupNode>(result_name);
  for (size_t z = 0; z < path.size() - 1; z++) {
    const Node* node = path[z];
    if (dynamic_cast<const FunctionCallNode*>(node)) {
      throw std::runtime_error("Cannot invert function call in expression");
    }
    if (dynamic_cast<const TernaryOperatorNode*>(node)) {
      throw std::runtime_error("Cannot invert ternary operator in expression");
    }
    if (dynamic_cast<const ConstantNode*>(node)) {
      throw std::logic_error("find_env_lookup found a constant node");
    }
    if (dynamic_cast<const EnvLookupNode*>(node)) {
      throw std::logic_error("find_env_lookup found an env lookup not at the end of the path");
    }

    const Node* next_node = path[z + 1];

    const auto* unary = dynamic_cast<const UnaryOperatorNode*>(node);
    if (unary) {
      using T = UnaryOperatorNode::Type;
      switch (unary->type) {
        case T::LOGICAL_NOT:
        case T::BITWISE_NOT:
        case T::POSITIVE:
        case T::NEGATIVE:
          // All unary operators are their own inverse (except POSITIVE, which essentially does nothing), so we can
          // just create the exact same node on the other side of the equation
          if (next_node != unary->sub.get()) {
            throw std::logic_error("Path through unary operator node does not visit its child");
          }
          ret_expr = std::make_unique<UnaryOperatorNode>(unary->type, std::move(ret_expr));
          break;
        default:
          throw std::logic_error("Unknown unary operator");
      }
    }

    const auto* binary = dynamic_cast<const BinaryOperatorNode*>(node);
    if (binary) {
      bool next_is_left = (next_node == binary->left.get());
      if (!next_is_left && (next_node != binary->right.get())) {
        throw std::logic_error("Path through binary operator node does not visit either of its children");
      }

      using T = BinaryOperatorNode::Type;
      switch (binary->type) {
        case T::LOGICAL_OR:
        case T::LOGICAL_AND:
        case T::BITWISE_OR:
        case T::BITWISE_AND:
        case T::LEFT_SHIFT:
        case T::RIGHT_SHIFT:
        case T::LESS_THAN:
        case T::GREATER_THAN:
        case T::LESS_OR_EQUAL:
        case T::GREATER_OR_EQUAL:
        case T::EQUAL:
        case T::NOT_EQUAL:
        case T::MODULUS:
          throw std::runtime_error("Cannot invert binary operator");
        case T::BITWISE_XOR:
          ret_expr = next_is_left
              ? std::make_unique<BinaryOperatorNode>(T::BITWISE_XOR, std::move(ret_expr), binary->right->clone())
              : std::make_unique<BinaryOperatorNode>(T::BITWISE_XOR, binary->left->clone(), std::move(ret_expr));
          break;
        case T::ADD:
          ret_expr = next_is_left
              ? std::make_unique<BinaryOperatorNode>(T::SUBTRACT, std::move(ret_expr), binary->right->clone())
              : std::make_unique<BinaryOperatorNode>(T::SUBTRACT, std::move(ret_expr), binary->left->clone());
          break;
        case T::SUBTRACT:
          ret_expr = next_is_left
              ? std::make_unique<BinaryOperatorNode>(T::ADD, std::move(ret_expr), binary->right->clone())
              : std::make_unique<BinaryOperatorNode>(T::SUBTRACT, binary->left->clone(), std::move(ret_expr));
          break;
        case T::MULTIPLY:
          ret_expr = next_is_left
              ? std::make_unique<BinaryOperatorNode>(T::DIVIDE, std::move(ret_expr), binary->right->clone())
              : std::make_unique<BinaryOperatorNode>(T::DIVIDE, std::move(ret_expr), binary->left->clone());
          break;
        case T::DIVIDE:
          ret_expr = next_is_left
              ? std::make_unique<BinaryOperatorNode>(T::MULTIPLY, std::move(ret_expr), binary->right->clone())
              : std::make_unique<BinaryOperatorNode>(T::DIVIDE, binary->left->clone(), std::move(ret_expr));
          break;
        default:
          throw std::logic_error("Unknown binary operator");
      }
    }
  }

  return ret_expr;
}

} // namespace Expression
} // namespace ResourceDASM
