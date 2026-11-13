#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "ExpressionFunctions.hh"

namespace ResourceDASM {
namespace Expression {

std::unordered_map<std::string, const FunctionDefinition*> FunctionDefinition::all_functions;

FunctionDefinition::FunctionDefinition(
    const std::string& name, size_t min_args, ssize_t max_args, Value (*fn)(const std::vector<Value>& args))
    : name(name), min_args(min_args), max_args(max_args), fn(std::move(fn)) {
  this->all_functions.emplace(name, this);
}

Value FunctionDefinition::dispatch_call(const std::string& name, const std::vector<Value>& args) {
  if (auto it = FunctionDefinition::all_functions.find(name); it != FunctionDefinition::all_functions.end()) {
    const auto* def = it->second;
    if (args.size() < def->min_args) {
      throw std::runtime_error(std::format("Not enough arguments to function {}", name));
    }
    if ((def->max_args >= 0) && (static_cast<ssize_t>(args.size()) > def->max_args)) {
      throw std::runtime_error(std::format("Too many arguments to function {}", name));
    }
    return def->fn(args);
  } else {
    throw std::runtime_error(std::format("No function named {} exists", name));
  }
}

FunctionDefinition f_bswap16("bswap16", 1, 1, [](const std::vector<Value>& args) -> Value {
  return static_cast<int64_t>(phosg::bswap16(static_cast<uint16_t>(args[0].as_int())));
});

FunctionDefinition f_bswap32("bswap32", 1, 1, [](const std::vector<Value>& args) -> Value {
  return static_cast<int64_t>(phosg::bswap32(static_cast<uint32_t>(args[0].as_int())));
});

FunctionDefinition f_bswap64("bswap64", 1, 1, [](const std::vector<Value>& args) -> Value {
  return static_cast<int64_t>(phosg::bswap64(args[0].as_int()));
});

FunctionDefinition f_sign_extend("sign_extend", 2, 2, [](const std::vector<Value>& args) -> Value {
  int64_t v = args[0].as_int();
  int64_t bits = args[1].as_int();
  return (v & (1 << (bits - 1))) ? (v | ~((1 << bits) - 1)) : v;
});

FunctionDefinition f_sqrt("sqrt", 1, 1, [](const std::vector<Value>& args) -> Value {
  return sqrt(args[0].is_float() ? args[0].as_float() : args[0].as_int());
});

FunctionDefinition f_min("min", 1, -1, [](const std::vector<Value>& args) -> Value {
  Value ret = args[0];
  for (size_t z = 1; z < args.size(); z++) {
    if (args[z] < ret) {
      ret = args[z];
    }
  }
  return ret;
});

FunctionDefinition f_max("max", 1, -1, [](const std::vector<Value>& args) -> Value {
  Value ret = args[0];
  for (size_t z = 1; z < args.size(); z++) {
    if (args[z] > ret) {
      ret = args[z];
    }
  }
  return ret;
});

FunctionDefinition f_clamp("clamp", 3, 3, [](const std::vector<Value>& args) -> Value {
  if (args[1] > args[2]) {
    throw std::runtime_error("Invalid bounds in clamp()");
  }
  if (args[0] < args[1]) {
    return args[1];
  } else if (args[0] > args[2]) {
    return args[2];
  } else {
    return args[0];
  }
});

FunctionDefinition f_floor("floor", 1, 1, [](const std::vector<Value>& args) -> Value {
  if (args[0].is_float()) {
    constexpr double int_min = static_cast<double>(std::numeric_limits<int64_t>::min());
    constexpr double int_max = static_cast<double>(std::numeric_limits<int64_t>::max());
    if (!isfinite(args[0].as_float()) || (args[0].as_float() < int_min) || (args[0].as_float() >= int_max)) {
      throw std::runtime_error("Floating-point value out of range for 64-bit integer");
    }
    return static_cast<int64_t>(floor(args[0].as_float()));
  } else if (args[0].is_int()) {
    return args[0];
  } else {
    throw std::logic_error("Unknown value type");
  }
});

FunctionDefinition f_ceil("ceil", 1, 1, [](const std::vector<Value>& args) -> Value {
  if (args[0].is_float()) {
    constexpr double int_min = static_cast<double>(std::numeric_limits<int64_t>::min());
    constexpr double int_max = static_cast<double>(std::numeric_limits<int64_t>::max());
    if (!isfinite(args[0].as_float()) || (args[0].as_float() < int_min) || (args[0].as_float() >= int_max)) {
      throw std::runtime_error("Floating-point value out of range for 64-bit integer");
    }
    return static_cast<int64_t>(ceil(args[0].as_float()));
  } else if (args[0].is_int()) {
    return args[0];
  } else {
    throw std::logic_error("Unknown value type");
  }
});

FunctionDefinition f_int("int", 1, 1, [](const std::vector<Value>& args) -> Value {
  if (args[0].is_float()) {
    constexpr double int_min = static_cast<double>(std::numeric_limits<int64_t>::min());
    constexpr double int_max = static_cast<double>(std::numeric_limits<int64_t>::max());
    if (!isfinite(args[0].as_float()) || (args[0].as_float() < int_min) || (args[0].as_float() >= int_max)) {
      throw std::runtime_error("Floating-point value out of range for 64-bit integer");
    }
    return static_cast<int64_t>(args[0].as_float());
  } else if (args[0].is_int()) {
    return args[0];
  } else if (args[0].is_string()) {
    return std::stoll(args[0].as_string());
  } else {
    throw std::logic_error("Unknown value type");
  }
});

FunctionDefinition f_float("float", 1, 1, [](const std::vector<Value>& args) -> Value {
  if (args[0].is_float()) {
    return args[0];
  } else if (args[0].is_int()) {
    return static_cast<double>(args[0].as_int());
  } else if (args[0].is_string()) {
    return std::stod(args[0].as_string());
  } else {
    throw std::logic_error("Unknown value type");
  }
});

FunctionDefinition f_str("str", 1, 1, [](const std::vector<Value>& args) -> Value {
  if (args[0].is_float()) {
    return std::format("{:g}", args[0].as_float());
  } else if (args[0].is_int()) {
    return std::format("{}", args[0].as_int());
  } else if (args[0].is_string()) {
    return args[0];
  } else {
    throw std::logic_error("Unknown value type");
  }
});

FunctionDefinition f_hex("hex", 1, 1, [](const std::vector<Value>& args) -> Value {
  if (args.size() == 1) {
    return std::format("{:X}", args[0].as_int());
  } else if (args.size() == 2) {
    return std::format("{:0{}X}", args[0].as_int(), args[1].as_int());
  } else {
    throw std::runtime_error("Incorrect argument count for hex()");
  }
});

FunctionDefinition f_encode_float("encode_float", 1, 1, [](const std::vector<Value>& args) -> Value {
  if (args[0].is_float()) {
    return static_cast<int64_t>(std::bit_cast<uint32_t>(static_cast<float>(args[0].as_float())));
  } else {
    return static_cast<int64_t>(std::bit_cast<uint32_t>(static_cast<float>(args[0].as_int())));
  }
});

FunctionDefinition f_decode_float("decode_float", 1, 1, [](const std::vector<Value>& args) -> Value {
  if (args[0].is_float()) {
    return args[0];
  } else {
    return static_cast<double>(std::bit_cast<float>(static_cast<uint32_t>(args[0].as_int())));
  }
});

} // namespace Expression
} // namespace ResourceDASM
