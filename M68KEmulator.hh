#pragma once

#include <stdio.h>
#include <stdint.h>

#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <phosg/Strings.hh>
#include <string>

#include "MemoryContext.hh"
#include "InterruptManager.hh"


struct M68KRegisters {
  uint32_t a[8];
  union {
    uint32_t u;
    int32_t s;
  } d[8];
  uint32_t pc;
  union {
    uint8_t ccr;
    uint16_t sr;
  };

  struct {
    uint32_t read_addr;
    uint32_t write_addr;
  } debug;

  M68KRegisters();

  uint32_t get_reg_value(bool is_a_reg, uint8_t reg_num);

  void set_ccr_flags(int64_t x, int64_t n, int64_t z, int64_t v, int64_t c);
  void set_ccr_flags_integer_add(int32_t left_value, int32_t right_value, uint8_t size);
  void set_ccr_flags_integer_subtract(int32_t left_value, int32_t right_value, uint8_t size);
};


class M68KEmulator {
public:
  explicit M68KEmulator(std::shared_ptr<MemoryContext> mem);
  ~M68KEmulator() = default;

  std::shared_ptr<MemoryContext> memory();

  void print_state_header(FILE* stream);
  void print_state(FILE* stream);

  static std::string disassemble_one(
      StringReader& r,
      uint32_t start_address,
      std::unordered_set<uint32_t>& branch_target_addresses);
  static std::string disassemble_one(
      const void* vdata,
      size_t size,
      uint32_t start_address);
  static std::string disassemble(
      const void* vdata,
      size_t size,
      uint32_t start_address,
      const std::unordered_multimap<uint32_t, std::string>* labels);

  static std::string disassemble(const void* data, size_t size, uint32_t pc = 0);

  void set_syscall_handler(
      std::function<bool(M68KEmulator&, M68KRegisters&, uint16_t)> handler);
  void set_debug_hook(
      std::function<bool(M68KEmulator&, M68KRegisters&)> hook);
  void set_interrupt_manager(std::shared_ptr<InterruptManager> im);

  void execute(const M68KRegisters& regs);

private:
  bool should_exit;
  M68KRegisters regs;
  std::shared_ptr<MemoryContext> mem;

  std::function<bool(M68KEmulator&, M68KRegisters&, uint16_t)> syscall_handler;
  std::function<bool(M68KEmulator&, M68KRegisters&)> debug_hook;
  std::shared_ptr<InterruptManager> interrupt_manager;

  void (M68KEmulator::*exec_fns[0x10])(uint16_t);
  static const std::vector<std::string (*)(StringReader& r, uint32_t start_address, std::unordered_set<uint32_t>& branch_target_addresses)> dasm_fns;

  struct ResolvedAddress {
    enum class Location {
      MEMORY = 0,
      D_REGISTER = 1,
      A_REGISTER = 2,
      SR = 3,
    };

    uint32_t addr;
    Location location;

    bool is_register() const;
  };

  uint32_t read(const ResolvedAddress& addr, uint8_t size);
  uint32_t read(uint32_t addr, uint8_t size);
  void write(const ResolvedAddress& addr, uint32_t value, uint8_t size);
  void write(uint32_t addr, uint32_t value, uint8_t size);

  uint16_t fetch_instruction_word(bool advance = true);
  int16_t fetch_instruction_word_signed(bool advance = true);
  uint32_t fetch_instruction_data(uint8_t size, bool advance = true);
  int32_t fetch_instruction_data_signed(uint8_t size, bool advance = true);

  uint32_t resolve_address_extension(uint16_t ext);
  uint32_t resolve_address_control(uint8_t M, uint8_t Xn);
  uint32_t resolve_address_jump(uint8_t M, uint8_t Xn);
  ResolvedAddress resolve_address(uint8_t M, uint8_t Xn, uint8_t size);

  static std::string dasm_reg_mask(uint16_t mask, bool reverse);
  static std::string dasm_address_extension(StringReader& r, uint16_t ext, int8_t An);
  static std::string dasm_address(StringReader& r, uint32_t opcode_start_address,
      uint8_t M, uint8_t Xn, uint8_t size, std::unordered_set<uint32_t>* branch_target_addresses);

  bool check_condition(uint8_t condition);

  void exec_unimplemented(uint16_t opcode);
  static std::string dasm_unimplemented(StringReader& r, uint32_t start_address, std::unordered_set<uint32_t>& branch_target_addresses);

  void exec_0123(uint16_t opcode);
  static std::string dasm_0123(StringReader& r, uint32_t start_address,
      std::unordered_set<uint32_t>& branch_target_addresses);

  void exec_4(uint16_t opcode);
  static std::string dasm_4(StringReader& r, uint32_t start_address,
      std::unordered_set<uint32_t>& branch_target_addresses);

  void exec_5(uint16_t opcode);
  static std::string dasm_5(StringReader& r, uint32_t start_address,
      std::unordered_set<uint32_t>& branch_target_addresses);

  void exec_6(uint16_t opcode);
  static std::string dasm_6(StringReader& r, uint32_t start_address,
      std::unordered_set<uint32_t>& branch_target_addresses);

  void exec_7(uint16_t opcode);
  static std::string dasm_7(StringReader& r, uint32_t start_address,
      std::unordered_set<uint32_t>& branch_target_addresses);

  void exec_8(uint16_t opcode);
  static std::string dasm_8(StringReader& r, uint32_t start_address,
      std::unordered_set<uint32_t>& branch_target_addresses);

  void exec_9D(uint16_t opcode);
  static std::string dasm_9D(StringReader& r, uint32_t start_address,
      std::unordered_set<uint32_t>& branch_target_addresses);

  void exec_A(uint16_t opcode);
  static std::string dasm_A(StringReader& r, uint32_t start_address,
      std::unordered_set<uint32_t>& branch_target_addresses);

  void exec_B(uint16_t opcode);
  static std::string dasm_B(StringReader& r, uint32_t start_address,
      std::unordered_set<uint32_t>& branch_target_addresses);

  void exec_C(uint16_t opcode);
  static std::string dasm_C(StringReader& r, uint32_t start_address,
      std::unordered_set<uint32_t>& branch_target_addresses);

  void exec_E(uint16_t opcode);
  static std::string dasm_E(StringReader& r, uint32_t start_address,
      std::unordered_set<uint32_t>& branch_target_addresses);

  void exec_F(uint16_t opcode);
  static std::string dasm_F(StringReader& r, uint32_t start_address,
      std::unordered_set<uint32_t>& branch_target_addresses);

  void execute_next_opcode();
};
