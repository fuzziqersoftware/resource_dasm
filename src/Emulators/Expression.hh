#pragma once

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include <functional>
#include <phosg/Strings.hh>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ResourceDASM {
namespace Expression {

class Visitor;

struct Value {
  std::variant<int64_t, double, std::string> v;

  inline Value(int64_t v) : v(std::in_place_type<int64_t>, v) {}
  inline Value(double v) : v(std::in_place_type<double>, v) {}
  inline Value(const std::string& v) : v(std::in_place_type<std::string>, v) {}
  inline Value(std::string_view v) : v(std::in_place_type<std::string>, v) {}

  inline bool operator==(const Value& other) const {
    return (this->operator<=>(other) == std::partial_ordering::equivalent);
  }
  std::partial_ordering operator<=>(const Value& other) const;

  bool is_int() const;
  bool is_float() const;
  bool is_string() const;

  int64_t as_int() const;
  double as_float() const;
  std::string as_string() const;

  bool truth_value() const;

  std::string str(bool hex = false) const;
};

using EnvIsConst = std::function<bool(const std::string&)>;
using EnvLookup = std::function<Value(const std::string&)>;
using OptionalEnvLookup = std::function<std::optional<Value>(const std::string&)>;
using FunctionCall = std::function<Value(const std::string&, const std::vector<Value>& args)>;
using OptionalFunctionCall = std::function<std::optional<Value>(const std::string&, const std::vector<Value>& args)>;

class Node {
public:
  static std::unique_ptr<Node> parse(std::string_view text);

  virtual ~Node() = default;

  virtual std::unique_ptr<Node> clone() const = 0;
  virtual void accept(Visitor* visitor) const = 0;

  bool operator==(const Node& other) const;

  bool is_const(const EnvIsConst& env_is_const) const;
  std::vector<const Node*> find_env_lookup(const std::string& name) const;
  Value evaluate(const EnvLookup& env = nullptr, const FunctionCall& function_call = nullptr) const;
  std::string str(const EnvLookup& env = nullptr) const;

  // Same as evaluate(), but instead returns a new Node (tree) after collapsing all constant-value subtrees into
  // constant nodes. Unlike evaluate(), lookups and function calls may return an empty std::optional, in which case the
  // node is not simplified (but in the case of a function call, its arguments may be).
  std::unique_ptr<Node> simplify(
      const OptionalEnvLookup& env = nullptr, const OptionalFunctionCall& function_call = nullptr);

  // Returns a new expression which is the inverse of *this relative to the target name. Logically, this is the same as
  // writing *this as one side of an equation and result_name as the other side, then solving for target_name. This
  // function is fairly simple, however, and doesn't handle e.g. expressions where target_name appears multiple times,
  // or expressions where the path to target_name includes a function call or noninvertible operator.
  std::unique_ptr<Node> solve(const std::string& target_name, const std::string& result_name) const;

protected:
  Node() = default;
};

class FunctionCallNode : public Node {
public:
  FunctionCallNode(std::string_view function_name, std::vector<std::unique_ptr<Node>>&& args);
  virtual ~FunctionCallNode() = default;
  virtual std::unique_ptr<Node> clone() const;
  virtual void accept(Visitor* visitor) const;

  std::string function_name;
  std::vector<std::unique_ptr<Node>> args;
};

class TernaryOperatorNode : public Node {
public:
  enum class Type {
    CONDITION_SELECTOR = 0,
  };
  TernaryOperatorNode(
      Type type, std::unique_ptr<Node>&& left, std::unique_ptr<Node>&& center, std::unique_ptr<Node>&& right);
  virtual ~TernaryOperatorNode() = default;
  virtual std::unique_ptr<Node> clone() const;
  virtual void accept(Visitor* visitor) const;

  Type type;
  std::unique_ptr<Node> left;
  std::unique_ptr<Node> center;
  std::unique_ptr<Node> right;
};

class BinaryOperatorNode : public Node {
public:
  enum class Type {
    LOGICAL_OR = 0,
    LOGICAL_AND,
    BITWISE_OR,
    BITWISE_AND,
    BITWISE_XOR,
    LEFT_SHIFT,
    RIGHT_SHIFT,
    LESS_THAN,
    GREATER_THAN,
    LESS_OR_EQUAL,
    GREATER_OR_EQUAL,
    EQUAL,
    NOT_EQUAL,
    ADD,
    SUBTRACT,
    MULTIPLY,
    DIVIDE,
    MODULUS,
  };
  BinaryOperatorNode(Type type, std::unique_ptr<Node>&& left, std::unique_ptr<Node>&& right);
  virtual ~BinaryOperatorNode() = default;
  virtual std::unique_ptr<Node> clone() const;
  virtual void accept(Visitor* visitor) const;

  Type type;
  std::unique_ptr<Node> left;
  std::unique_ptr<Node> right;
};

class UnaryOperatorNode : public Node {
public:
  enum class Type {
    LOGICAL_NOT = 0,
    BITWISE_NOT,
    POSITIVE,
    NEGATIVE,
  };
  UnaryOperatorNode(Type type, std::unique_ptr<Node>&& sub);
  virtual ~UnaryOperatorNode() = default;
  virtual std::unique_ptr<Node> clone() const;
  virtual void accept(Visitor* visitor) const;

  Type type;
  std::unique_ptr<Node> sub;
};

class ConstantNode : public Node {
public:
  ConstantNode(Value value);
  virtual ~ConstantNode() = default;
  virtual std::unique_ptr<Node> clone() const;
  virtual void accept(Visitor* visitor) const;

  Value value;
};

class EnvLookupNode : public Node {
public:
  EnvLookupNode(const std::string& name);
  EnvLookupNode(std::string_view name);
  virtual ~EnvLookupNode() = default;
  virtual std::unique_ptr<Node> clone() const;
  virtual void accept(Visitor* visitor) const;

  std::string name;
};

class Visitor {
public:
  // These functions return true if traversal should continue, or false to stop early
  virtual void visit_function_call(const FunctionCallNode*) {}
  virtual void visit_ternary_operator(const TernaryOperatorNode*) {}
  virtual void visit_binary_operator(const BinaryOperatorNode*) {}
  virtual void visit_unary_operator(const UnaryOperatorNode*) {}
  virtual void visit_constant(const ConstantNode*) {}
  virtual void visit_env_lookup(const EnvLookupNode*) {}
};

} // namespace Expression
} // namespace ResourceDASM
