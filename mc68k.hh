#pragma once

#include <stdio.h>
#include <stdint.h>

#include <map>
#include <phosg/Strings.hh>
#include <string>


enum class Size {
  BYTE = 0,
  WORD = 1,
  LONG = 2,
};

enum class TSize {
  WORD = 0,
  LONG = 1,
};

enum class DSize {
  BYTE = 1,
  LONG = 2,
  WORD = 3,
};

enum Condition {
  C = 0x01,
  V = 0x02,
  Z = 0x04,
  N = 0x08,
  X = 0x10,
};


enum class DebuggingMode {
  Disabled = 0,
  Passive,
  Interactive,
};


struct MC68KEmulator {
  std::map<uint32_t, std::string> memory_regions;

  uint32_t a[8];
  uint32_t d[8];
  uint32_t pc;
  union {
    uint8_t ccr;
    uint16_t sr;
  };

  bool execute;
  DebuggingMode debug;

  std::string* trap_call_region;
  std::unordered_map<uint16_t, uint32_t> trap_to_call_addr;

  MC68KEmulator();

  void print_state(FILE* stream, bool print_memory = false);

  uint32_t get_reg_value(bool is_a_reg, uint8_t reg_num);
  void set_ccr_flags(int64_t x, int64_t n, int64_t z, int64_t v, int64_t c);
  void set_ccr_flags_integer_add(int32_t left_value, int32_t right_value, Size size);
  void set_ccr_flags_integer_subtract(int32_t left_value, int32_t right_value, Size size);

  bool address_is_register(void* addr);
  uint32_t read(uint32_t addr, Size size);
  uint32_t read(void* addr, Size size);
  void write(uint32_t addr, uint32_t value, Size size);
  void write(void* addr, uint32_t value, Size size);

  void* translate_address(uint32_t addr);
  uint16_t fetch_instruction_word(bool advance = true);
  int16_t fetch_instruction_word_signed(bool advance = true);
  uint32_t fetch_instruction_data(Size size, bool advance = true);
  int32_t fetch_instruction_data_signed(Size size, bool advance = true);

  uint32_t resolve_address_extension(uint16_t ext);
  uint32_t resolve_address_control(uint8_t M, uint8_t Xn);
  void* resolve_address(uint8_t M, uint8_t Xn, Size size);

  bool check_condition(uint8_t condition_code);

  void opcode_unimplemented(uint16_t opcode);
  void opcode_0123(uint16_t op);
  void opcode_4(uint16_t opcode);
  void opcode_5(uint16_t opcode);
  void opcode_6(uint16_t opcode);
  void opcode_7(uint16_t opcode);
  void opcode_8(uint16_t opcode);
  void opcode_9D(uint16_t opcode);
  void opcode_A(uint16_t opcode);
  void opcode_B(uint16_t opcode);
  void opcode_C(uint16_t opcode);
  void opcode_E(uint16_t opcode);

  void execute_next_opcode();
  void execute_forever();
};
