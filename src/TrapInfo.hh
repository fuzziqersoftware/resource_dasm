#pragma once

#include <stdint.h>

#include <memory>
#include <vector>

namespace ResourceDASM {

struct TrapInfo {
  struct Argument {
    uint8_t a_reg = 0xFF; // 0xFF = not in D reg
    uint8_t d_reg = 0xFF; // 0xFF = not in A reg
    uint8_t stack_offset = 0; // Only used if both a_reg and d_reg are 0xFF
    uint8_t value_size = 0;
    const char* type_name = nullptr;
    const char* name = nullptr;

    static inline Argument a(uint8_t which, const char* type_name, const char* name = nullptr) {
      return Argument{which, 0xFF, 0xFF, 4, type_name, name};
    }
    static inline Argument d(uint8_t which, const char* type_name, const char* name = nullptr) {
      return Argument{0xFF, which, 0xFF, 4, type_name, name};
    }
    static inline Argument stack16(uint8_t offset, const char* type_name, const char* name = nullptr) {
      return Argument{0xFF, 0xFF, offset, 2, type_name, name};
    }
    static inline Argument stack32(uint8_t offset, const char* type_name, const char* name = nullptr) {
      return Argument{0xFF, 0xFF, offset, 4, type_name, name};
    }
    static inline Argument stack64(uint8_t offset, const char* type_name, const char* name = nullptr) {
      return Argument{0xFF, 0xFF, offset, 8, type_name, name};
    }

    std::string str() const;
  };

  struct Condition {
    uint8_t bits = 0;
    uint8_t d_reg = 0xFF; // 0xFF = not in D reg
    uint8_t a_reg = 0xFF; // 0xFF = not in A reg
    uint16_t stack_offset = 0; // Only used if d_reg and a_reg are 0xFF
    uint32_t value = 0;

    static inline Condition d16(uint8_t which, uint32_t value) {
      return Condition{16, which, 0xFF, 0xFFFF, value};
    }
    static inline Condition d32(uint8_t which, uint32_t value) {
      return Condition{32, which, 0xFF, 0xFFFF, value};
    }
    static inline Condition a32(uint8_t which, uint32_t value) {
      return Condition{32, 0xFF, which, 0xFFFF, value};
    }
    static inline Condition stack16(uint8_t offset, uint32_t value) {
      return Condition{16, 0xFF, 0xFF, offset, value};
    }
    static inline Condition stack32(uint8_t offset, uint32_t value) {
      return Condition{32, 0xFF, 0xFF, offset, value};
    }

    inline bool operator<(const Condition& other) const {
      return (this->value < other.value);
    }

    std::string str() const;
  };

  uint16_t trap_num;
  std::vector<Condition> conditions;
  uint8_t flags;

  const char* name;
  std::vector<Argument> args;
  std::vector<Argument> return_values;
  bool signature_known;

  inline TrapInfo(
      uint16_t trap_num,
      std::initializer_list<Condition> conditions,
      uint8_t flags,
      const char* name,
      std::initializer_list<Argument> args,
      std::initializer_list<Argument> return_values)
      : trap_num(trap_num),
        conditions(conditions),
        flags(flags),
        name(name),
        args(args),
        return_values(return_values),
        signature_known(true) {}
  inline TrapInfo(uint16_t trap_num, std::initializer_list<Condition> conditions, uint8_t flags, const char* name)
      : trap_num(trap_num), conditions(conditions), flags(flags), name(name), signature_known(false) {}

  bool operator<(const TrapInfo& other) const;

  std::string str(bool args_only = false) const;
};

void assert_trap_infos_ordered();
const TrapInfo* info_for_68k_trap(uint16_t trap_num, uint8_t flags = 0);

} // namespace ResourceDASM
