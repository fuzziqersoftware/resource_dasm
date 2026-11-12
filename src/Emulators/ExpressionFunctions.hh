#pragma once

#include <stdio.h>

#include <functional>
#include <string>
#include <unordered_map>

#include "Expression.hh"

namespace ResourceDASM {
namespace Expression {

struct FunctionDefinition {
  static std::unordered_map<std::string, const FunctionDefinition*> all_functions;

  std::string name;
  size_t min_args;
  ssize_t max_args; // -1 = no maximum
  Value (*fn)(const std::vector<Value>& args);

  FunctionDefinition(
      const std::string& name, size_t min_args, ssize_t max_args, Value (*fn)(const std::vector<Value>& args));

  static Value dispatch_call(const std::string& name, const std::vector<Value>& args);
};

} // namespace Expression
} // namespace ResourceDASM
