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

struct Value {
  std::variant<int64_t, double, std::string> v;

  inline Value(int64_t v) : v(std::in_place_type<int64_t>, v) {}
  inline Value(double v) : v(std::in_place_type<double>, v) {}
  inline Value(const std::string& v) : v(std::in_place_type<std::string>, v) {}
  inline Value(std::string_view v) : v(std::in_place_type<std::string>, v) {}

  bool operator==(const Value& other) const = default;
  bool operator!=(const Value& other) const = default;

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
using FunctionCall = std::function<Value(const std::string&, const std::vector<Value>& args)>;

class Node {
public:
  static std::unique_ptr<Node> parse(std::string_view text);

  virtual ~Node() = default;
  virtual bool operator==(const Node& other) const = 0;
  inline bool operator!=(const Node& other) const {
    return !this->operator==(other);
  }
  virtual bool is_const(const EnvIsConst& env_is_const) const = 0;
  virtual Value evaluate(const EnvLookup& env = nullptr, const FunctionCall& function_call = nullptr) const = 0;
  virtual std::string str() const = 0;

protected:
  Node() = default;
};

class FunctionCallNode : public Node {
public:
  FunctionCallNode(std::string_view function_name, std::vector<std::unique_ptr<Node>>&& args);
  virtual ~FunctionCallNode() = default;
  virtual bool operator==(const Node& other) const;
  virtual bool is_const(const EnvIsConst& env_is_const) const;
  virtual Value evaluate(const EnvLookup& env = nullptr, const FunctionCall& function_call = nullptr) const;
  virtual std::string str() const;

  static Value default_handler(const std::string& name, const std::vector<Value>& args);

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
  virtual bool operator==(const Node& other) const;
  virtual bool is_const(const EnvIsConst& env_is_const) const;
  virtual Value evaluate(const EnvLookup& env = nullptr, const FunctionCall& function_call = nullptr) const;
  virtual std::string str() const;

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
  virtual bool operator==(const Node& other) const;
  virtual bool is_const(const EnvIsConst& env_is_const) const;
  virtual Value evaluate(const EnvLookup& env = nullptr, const FunctionCall& function_call = nullptr) const;
  virtual std::string str() const;

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
  virtual bool operator==(const Node& other) const;
  virtual bool is_const(const EnvIsConst& env_is_const) const;
  virtual Value evaluate(const EnvLookup& env = nullptr, const FunctionCall& function_call = nullptr) const;
  virtual std::string str() const;

  Type type;
  std::unique_ptr<Node> sub;
};

class ConstantNode : public Node {
public:
  ConstantNode(Value value);
  virtual ~ConstantNode() = default;
  virtual bool operator==(const Node& other) const;
  virtual bool is_const(const EnvIsConst& env_is_const) const;
  virtual Value evaluate(const EnvLookup& env = nullptr, const FunctionCall& function_call = nullptr) const;
  virtual std::string str() const;

  Value value;
};

class EnvLookupNode : public Node {
public:
  EnvLookupNode(const std::string& name);
  EnvLookupNode(std::string_view name);
  virtual ~EnvLookupNode() = default;
  virtual bool operator==(const Node& other) const;
  virtual bool is_const(const EnvIsConst& env_is_const) const;
  virtual Value evaluate(const EnvLookup& env = nullptr, const FunctionCall& function_call = nullptr) const;
  virtual std::string str() const;

  std::string name;
};

} // namespace Expression
} // namespace ResourceDASM
