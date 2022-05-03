#pragma once

#include <stdio.h>
#include <stdint.h>

#include <functional>
#include <map>
#include <set>
#include <phosg/Strings.hh>
#include <string>

#include "MemoryContext.hh"
#include "InterruptManager.hh"
#include "EmulatorBase.hh"



struct X86Registers {
  union IntReg {
    le_uint32_t u;
    le_int32_t s;
    le_uint16_t u16;
    le_int16_t s16;
    struct {
      uint8_t u8;
      uint8_t u8h;
    } __attribute__((packed));
    struct {
      int8_t s8;
      int8_t s8h;
    } __attribute__((packed));
  } __attribute__((packed));
  union {
    struct {
      IntReg eax;
      IntReg ecx;
      IntReg edx;
      IntReg ebx;
      IntReg esp;
      IntReg ebp;
      IntReg esi;
      IntReg edi;
    } __attribute__((packed));
    IntReg regs[8];
  } __attribute__((packed));

  uint32_t eflags;
  union {
    uint32_t eip;
    uint32_t pc;
  } __attribute__((packed));

  X86Registers();

  void set_by_name(const std::string& reg_name, uint32_t value);

  uint8_t& reg8(uint8_t which);
  le_uint16_t& reg16(uint8_t which);
  le_uint32_t& reg32(uint8_t which);

  static constexpr uint32_t CF = 0x0001;
  static constexpr uint32_t PF = 0x0004;
  static constexpr uint32_t AF = 0x0010;
  static constexpr uint32_t ZF = 0x0040;
  static constexpr uint32_t SF = 0x0080;
  static constexpr uint32_t IF = 0x0200;
  static constexpr uint32_t DF = 0x0400;
  static constexpr uint32_t OF = 0x0800;
  static constexpr uint32_t default_int_flags = CF | PF | AF | ZF | SF | OF;

  bool flag(uint32_t mask) const;
  void replace_flag(uint32_t mask, bool value);
  static std::string flags_str(uint32_t eflags);
  std::string flags_str() const;

  bool check_condition(uint8_t cc) const;

  void import_state(FILE* stream);
  void export_state(FILE* stream) const;

  template <typename T>
  void set_flags_integer_result(T res, uint32_t apply_mask = default_int_flags);
  template <typename T>
  void set_flags_bitwise_result(T res, uint32_t apply_mask = default_int_flags);
  template <typename T>
  T set_flags_integer_add(T a, T b, uint32_t apply_mask = default_int_flags);
  template <typename T>
  T set_flags_integer_subtract(T a, T b, uint32_t apply_mask = default_int_flags);
};


class X86Emulator : public EmulatorBase {
public:
  explicit X86Emulator(std::shared_ptr<MemoryContext> mem);
  virtual ~X86Emulator() = default;

  virtual void import_state(FILE* stream);
  virtual void export_state(FILE* stream) const;

  inline X86Registers& registers() {
    return this->regs;
  }

  virtual void print_state_header(FILE* stream);
  virtual void print_state(FILE* stream);

  static std::string disassemble(
      const void* vdata,
      size_t size,
      uint32_t start_address = 0,
      const std::multimap<uint32_t, std::string>* labels = nullptr);

  inline void set_debug_hook(
      std::function<void(X86Emulator&, X86Registers&)> hook) {
    this->debug_hook = hook;
  }

  inline void set_audit(bool audit) {
    this->audit = audit;
    if (this->audit) {
      this->audit_results.resize(0x200);
    } else {
      this->audit_results.clear();
    }
  }

  struct Overrides {
    enum class Segment {
      NONE = 0,
      CS,
      DS,
      ES,
      FS,
      GS,
      SS,
    };

    bool should_clear;
    Segment segment;
    bool operand_size;
    bool address_size;
    bool wait;
    bool lock;
    // All opcodes for which rep/repe/repne (F2/F3) applies:
    // 6C/6D ins (rep)
    // 6E/6F outs (rep)
    // A4/A5 movs (rep)
    // AA/AB stos (rep)
    // AC/AD lods (rep)
    // A6/A7 cmps (repe/repne)
    // AE/AF scas (repe/repne)
    bool repeat_nz;
    bool repeat_z;

    Overrides() noexcept;
    std::string str() const;
    void on_opcode_complete();
    const char* overridden_segment_name() const;
  };

  struct AuditResult {
    uint64_t cycle_num;
    std::string opcode;
    std::string disassembly;
    Overrides overrides;
    X86Registers regs_before;
    X86Registers regs_after;
    std::vector<MemoryAccess> mem_accesses;
  };

  const std::vector<std::vector<AuditResult>>& get_audit_results() const;

  virtual void execute();

protected:
  X86Registers regs;

  bool audit;
  std::vector<std::vector<AuditResult>> audit_results;
  AuditResult* current_audit_result;

  Overrides overrides;
  std::function<void(X86Emulator&, X86Registers&)> debug_hook;

  struct DisassemblyState {
    StringReader r;
    uint32_t start_address;
    uint8_t opcode;
    Overrides overrides;
    std::map<uint32_t, bool> branch_target_addresses;

    uint8_t standard_operand_size() const;
  };

  struct OpcodeImplementation {
    void (X86Emulator::*exec)(uint8_t);
    std::string (*dasm)(DisassemblyState& s);

    OpcodeImplementation() : exec(nullptr), dasm(nullptr) { }
    OpcodeImplementation(
        void (X86Emulator::*exec)(uint8_t),
        std::string (*dasm)(DisassemblyState& s))
      : exec(exec), dasm(dasm) { }
  };
  static const OpcodeImplementation fns[0x100];
  static const OpcodeImplementation fns_0F[0x100];

  template <typename T>
  T fetch_instruction_data() {
    T ret = this->mem->read<T>(this->regs.eip);
    this->regs.eip += sizeof(T);
    return ret;
  }

  inline uint8_t fetch_instruction_byte() {
    return this->fetch_instruction_data<uint8_t>();
  }

  inline uint16_t fetch_instruction_word() {
    return this->fetch_instruction_data<le_uint16_t>();
  }

  inline uint32_t fetch_instruction_dword() {
    return this->fetch_instruction_data<le_uint32_t>();
  }

  template <typename T>
  void push(T value) {
    this->regs.esp.u -= sizeof(T);
    this->report_mem_access(this->regs.esp.u, bits_for_type<T>, true);
    this->mem->write<T>(this->regs.esp.u, value);
  }

  template <typename T>
  T pop() {
    this->report_mem_access(this->regs.esp.u, bits_for_type<T>, false);
    T ret = this->mem->read<T>(this->regs.esp.u);
    this->regs.esp.u += 4;
    return ret;
  }

  struct DecodedRM {
    int8_t non_ea_reg;
    int8_t ea_reg; // -1 = no reg
    int8_t ea_index_reg; // -1 = no reg (also ea_index_scale should be -1 or 0)
    int8_t ea_index_scale; // -1 (ea_reg is not to be dereferenced), 0 (no index reg), 1, 2, 4, or 8
    int32_t ea_disp;

    std::string ea_str(uint8_t operand_size) const;
    std::string non_ea_str(uint8_t operand_size) const;
    std::string str(uint8_t operand_size, bool ea_first) const;
    std::string str(uint8_t ea_operand_size, uint8_t non_ea_operand_size, bool ea_first) const;
  };
  DecodedRM fetch_and_decode_rm();
  static DecodedRM fetch_and_decode_rm(StringReader& r);

  uint32_t resolve_mem_ea(const DecodedRM& rm);
  uint8_t& resolve_non_ea_w8(const DecodedRM& rm);
  le_uint16_t& resolve_non_ea_w16(const DecodedRM& rm);
  le_uint32_t& resolve_non_ea_w32(const DecodedRM& rm);
  const uint8_t& resolve_non_ea_r8(const DecodedRM& rm);
  const le_uint16_t& resolve_non_ea_r16(const DecodedRM& rm);
  const le_uint32_t& resolve_non_ea_r32(const DecodedRM& rm);
  uint8_t& resolve_ea_w8(const DecodedRM& rm);
  le_uint16_t& resolve_ea_w16(const DecodedRM& rm);
  le_uint32_t& resolve_ea_w32(const DecodedRM& rm);
  const uint8_t& resolve_ea_r8(const DecodedRM& rm);
  const le_uint16_t& resolve_ea_r16(const DecodedRM& rm);
  const le_uint32_t& resolve_ea_r32(const DecodedRM& rm);

  static std::string disassemble_one(DisassemblyState& s);

  template <typename T>
  void exec_integer_math_inner(uint8_t what, T& dest, T src);
  template <typename T>
  void exec_F6_F7_misc_math_inner(uint8_t what, T& value);
  template <typename T>
  void exec_bit_test_ops(uint8_t what, T& v, uint8_t bit_number);
  template <typename T>
  void exec_bit_shifts_inner(uint8_t what, T& value, uint8_t distance);
  template <typename T>
  void exec_shld_shrd_inner(bool is_right_shift, T& dest_value, T incoming_value, uint8_t distance);
  template <typename T>
  void exec_movs_inner();
  template <typename T>
  void exec_rep_movs_inner();

  void               exec_0F_extensions(uint8_t);
  static std::string dasm_0F_extensions(DisassemblyState& s);
  void               exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math(uint8_t opcode);
  static std::string dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math(DisassemblyState& s);
  void               exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math(uint8_t opcode);
  static std::string dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math(DisassemblyState& s);
  void               exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math(uint8_t opcode);
  static std::string dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math(DisassemblyState& s);
  void               exec_26_es(uint8_t);
  static std::string dasm_26_es(DisassemblyState& s);
  void               exec_2E_cs(uint8_t);
  static std::string dasm_2E_cs(DisassemblyState& s);
  void               exec_36_ss(uint8_t);
  static std::string dasm_36_ss(DisassemblyState& s);
  void               exec_3E_ds(uint8_t);
  static std::string dasm_3E_ds(DisassemblyState& s);
  void               exec_40_to_47_inc(uint8_t opcode);
  void               exec_48_to_4F_dec(uint8_t opcode);
  static std::string dasm_40_to_4F_inc_dec(DisassemblyState& s);
  void               exec_50_to_57_push(uint8_t opcode);
  void               exec_58_to_5F_pop(uint8_t opcode);
  static std::string dasm_50_to_5F_push_pop(DisassemblyState& s);
  void               exec_64_fs(uint8_t);
  static std::string dasm_64_fs(DisassemblyState& s);
  void               exec_65_gs(uint8_t);
  static std::string dasm_65_gs(DisassemblyState& s);
  void               exec_66_operand_size(uint8_t);
  static std::string dasm_66_operand_size(DisassemblyState& s);
  void               exec_68_push(uint8_t);
  static std::string dasm_68_push(DisassemblyState& s);
  void               exec_80_to_83_imm_math(uint8_t opcode);
  static std::string dasm_80_to_83_imm_math(DisassemblyState& s);
  void               exec_84_85_test_rm(uint8_t opcode);
  static std::string dasm_84_85_test_rm(DisassemblyState& s);
  void               exec_86_87_xchg_rm(uint8_t opcode);
  static std::string dasm_86_87_xchg_rm(DisassemblyState& s);
  void               exec_88_to_8B_mov_rm(uint8_t opcode);
  static std::string dasm_88_to_8B_mov_rm(DisassemblyState& s);
  void               exec_8D_lea(uint8_t);
  static std::string dasm_8D_lea(DisassemblyState& s);
  void               exec_8F_pop_rm(uint8_t opcode);
  static std::string dasm_8F_pop_rm(DisassemblyState& s);
  void               exec_90_to_97_xchg(uint8_t opcode);
  static std::string dasm_90_to_97_xchg(DisassemblyState& s);
  void               exec_98_cbw_cwde(uint8_t);
  static std::string dasm_98_cbw_cwde(DisassemblyState& s);
  void               exec_99_cwd_cdq(uint8_t);
  static std::string dasm_99_cwd_cdq(DisassemblyState& s);
  void               exec_9C_pushf_pushfd(uint8_t);
  static std::string dasm_9C_pushf_pushfd(DisassemblyState& s);
  void               exec_9D_popf_popfd(uint8_t);
  static std::string dasm_9D_popf_popfd(DisassemblyState& s);
  void               exec_9F_lahf(uint8_t);
  static std::string dasm_9F_lahf(DisassemblyState&);
  void               exec_A4_A5_movs(uint8_t opcode);
  static std::string dasm_A4_A5_movs(DisassemblyState& s);
  void               exec_A8_A9_test_eax_imm(uint8_t opcode);
  static std::string dasm_A8_A9_test_eax_imm(DisassemblyState& s);
  void               exec_B0_to_B7_mov_imm_8(uint8_t opcode);
  void               exec_B8_to_BF_mov_imm_16_32(uint8_t opcode);
  static std::string dasm_B0_to_BF_mov_imm(DisassemblyState& s);
  void               exec_C0_C1_bit_shifts(uint8_t opcode);
  static std::string dasm_C0_C1_bit_shifts(DisassemblyState& s);
  void               exec_C2_C3_ret(uint8_t opcode);
  static std::string dasm_C2_C3_ret(DisassemblyState& s);
  void               exec_C6_C7_mov_rm_imm(uint8_t opcode);
  static std::string dasm_C6_C7_mov_rm_imm(DisassemblyState& s);
  void               exec_D0_to_D3_bit_shifts(uint8_t opcode);
  static std::string dasm_D0_to_D3_bit_shifts(DisassemblyState& s);
  void               exec_E8_E9_call_jmp(uint8_t opcode);
  static std::string dasm_E8_E9_call_jmp(DisassemblyState& s);
  void               exec_F2_F3_repz_repnz(uint8_t opcode);
  static std::string dasm_F2_F3_repz_repnz(DisassemblyState& s);
  void               exec_F5_cmc(uint8_t);
  static std::string dasm_F5_cmc(DisassemblyState&);
  void               exec_F6_F7_misc_math(uint8_t opcode);
  static std::string dasm_F6_F7_misc_math(DisassemblyState& s);
  void               exec_F8_clc(uint8_t);
  static std::string dasm_F8_clc(DisassemblyState&);
  void               exec_F9_stc(uint8_t);
  static std::string dasm_F9_stc(DisassemblyState&);
  void               exec_FA_cli(uint8_t);
  static std::string dasm_FA_cli(DisassemblyState&);
  void               exec_FB_sti(uint8_t);
  static std::string dasm_FB_sti(DisassemblyState&);
  void               exec_FC_cld(uint8_t);
  static std::string dasm_FC_cld(DisassemblyState&);
  void               exec_FD_std(uint8_t);
  static std::string dasm_FD_std(DisassemblyState&);
  void               exec_FE_FF_inc_dec_misc(uint8_t opcode);
  static std::string dasm_FE_FF_inc_dec_misc(DisassemblyState& s);

  void               exec_0F_31_rdtsc(uint8_t opcode);
  static std::string dasm_0F_31_rdtsc(DisassemblyState& s);
  void               exec_0F_40_to_4F_cmov_rm(uint8_t opcode);
  static std::string dasm_0F_40_to_4F_cmov_rm(DisassemblyState& s);
  void               exec_0F_80_to_8F_jcc(uint8_t opcode);
  static std::string dasm_0F_80_to_8F_jcc(DisassemblyState& s);
  void               exec_0F_90_to_9F_setcc_rm(uint8_t opcode);
  static std::string dasm_0F_90_to_9F_setcc_rm(DisassemblyState& s);
  void               exec_0F_A4_A5_AC_AD_shld_shrd(uint8_t opcode);
  static std::string dasm_0F_A4_A5_AC_AD_shld_shrd(DisassemblyState& s);
  void               exec_0F_A3_AB_B3_BB_bit_tests(uint8_t opcode);
  static std::string dasm_0F_A3_AB_B3_BB_bit_tests(DisassemblyState& s);
  void               exec_0F_B6_B7_BE_BF_movzx_movsx(uint8_t opcode);
  static std::string dasm_0F_B6_B7_BE_BF_movzx_movsx(DisassemblyState& s);
  void               exec_0F_BA_bit_tests(uint8_t);
  static std::string dasm_0F_BA_bit_tests(DisassemblyState& s);
  void               exec_0F_BC_BD_bsf_bsr(uint8_t opcode);
  static std::string dasm_0F_BC_BD_bsf_bsr(DisassemblyState& s);
  void               exec_0F_C0_C1_xadd_rm(uint8_t opcode);
  static std::string dasm_0F_C0_C1_xadd_rm(DisassemblyState& s);
  void               exec_0F_C8_to_CF_bswap(uint8_t opcode);
  static std::string dasm_0F_C8_to_CF_bswap(DisassemblyState& s);

  void               exec_unimplemented(uint8_t opcode);
  static std::string dasm_unimplemented(DisassemblyState& s);
  void               exec_0F_unimplemented(uint8_t opcode);
  static std::string dasm_0F_unimplemented(DisassemblyState& s);
};
