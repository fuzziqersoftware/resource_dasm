#pragma once

#include <stdint.h>

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <phosg/Strings.hh>
#include <set>
#include <vector>

#include "EmulatorBase.hh"
#include "InterruptManager.hh"
#include "MemoryContext.hh"

namespace ResourceDASM {

using namespace phosg;

// Data addressing modes:
// Rn => rn
// @Rn => [rn]
// @Rn+ => [rn]+
// @-Rn => -[rn]
// @(disp, Rn) => [rn + OS * disp] (disp is 4-bit zero-extended then multiplied by operand size)
// @(R0, Rn) => [rn + r0]
// @(disp, GBR) => [rbg + OS * disp] (disp is 8-bit zero-extended then multiplied by operand size)
// @(R0, GBR) => [gbr + r0]
// @(disp, PC) => [pc + OS * disp + 4] (disp is 8-bit zero-extended then multiplied by operand size. with a 32-bit operand, the lowest 2 bits of PC are masked)

// Branch target modes:
// disp => [pc + 2 * disp + 4] (disp is 8-bit SIGN-extended then multiplied by 2)
// disp => [pc + 2 * disp + 4] (disp is 12-bit SIGN-extended then multiplied by 2)
// Rn => [pc + rn + 4]

// Immediates:
// #imm => imm (8-bit imm zero-extended for tst, and, or, xor)
// #imm => imm (8-bit imm sign-extended for mov, add, cmp/eq)
// #imm => imm (8-bit imm zero-extended and multiplied by 4 for trapa)

// Calling convention:
// r0-r7       Caller-save
//   r0-r3     Return value
//   r2        Large struct return
//   r4-r7     Function call parameters (>4 via stack)
// r8-r15      Callee-save
//   r12       Global context pointer
//   r14       Frame pointer
//   r15       Stack pointer
// fs0-fs11    Caller-save
//   fs0-fs3   Return values
//   fs4-fs11  Function call parameters
// fs12-fs15   Callee-save
// mach        Caller-save
// macl        Caller-save
// pr          Caller-save
// fpscr       PR bit callee-save; FR, SZ bits zero at call/ret time
// fpul        Caller-save
// sr          Bits S, M, Q, T are caller save
// gbr         Reserved

// TODO: We probably should support "traditional" flavor disassembly too

class SH4Emulator : public EmulatorBase {
public:
  static constexpr bool is_little_endian = false;

  explicit SH4Emulator(std::shared_ptr<MemoryContext> mem);
  virtual ~SH4Emulator() = default;

  virtual void import_state(FILE* stream);
  virtual void export_state(FILE* stream) const;

  virtual void print_state_header(FILE* stream) const;
  virtual void print_state(FILE* stream) const;

  inline void set_debug_hook(std::function<void(SH4Emulator&)> hook) {
    this->debug_hook = hook;
  }

  virtual void execute();
  virtual void execute_one(uint16_t op);

  struct Regs {
    union {
      uint32_t u;
      int32_t s;
    } r[16];
    uint32_t sr; // -MBE------------F-----mqiiii--ST
    uint32_t ssr;
    uint32_t gbr;
    int64_t mac;
    uint32_t pr;
    uint32_t pc;
    uint32_t spc;
    uint32_t sgr;
    uint32_t vbr;
    union {
      uint32_t fpul_i;
      float fpul_f;
    };
    uint32_t fpscr;
    uint32_t dbr;

    union {
      float f[32];
      double d[16];
    };

    enum PendingBranchType {
      NONE = 0,
      BRANCH,
      BRANCH_IMMEDIATELY,
      CALL,
      RETURN,
    };
    PendingBranchType pending_branch_type;
    uint32_t pending_branch_target; // Ignored for RETURN

    void set_by_name(const std::string& name, uint32_t value);

    inline uint32_t get_sp() const {
      return this->r[15].u;
    }
    inline void set_sp(uint32_t sp) {
      this->r[15].u = sp;
    }

    void assert_no_branch_pending() const;
    void enqueue_branch(PendingBranchType type, uint32_t target);

    inline bool fpscr_fr() const {
      return this->fpscr & 0x00200000;
    }
    inline bool fpscr_sz() const {
      return this->fpscr & 0x00100000;
    }
    inline bool fpscr_pr() const {
      return this->fpscr & 0x00080000;
    }
    inline bool fpscr_dn() const {
      return this->fpscr & 0x00040000;
    }
    inline bool fpscr_rm() const {
      return this->fpscr & 3;
    }

    inline bool s() const {
      return this->sr & 2;
    }
    inline void replace_s(bool s) {
      if (s) {
        this->sr |= 0x00000002;
      } else {
        this->sr &= 0xFFFFFFFD;
      }
    }

    inline bool t() const {
      return this->sr & 1;
    }
    inline void replace_t(bool t) {
      if (t) {
        this->sr |= 0x00000001;
      } else {
        this->sr &= 0xFFFFFFFE;
      }
    }

    inline bool q() const {
      return (this->sr >> 8) & 1;
    }
    inline void replace_q(bool q) {
      if (q) {
        this->sr |= 0x00000100;
      } else {
        this->sr &= 0xFFFFFEFF;
      }
    }

    inline bool m() const {
      return (this->sr >> 9) & 1;
    }
    inline void replace_m(bool m) {
      if (m) {
        this->sr |= 0x00000200;
      } else {
        this->sr &= 0xFFFFFDFF;
      }
    }

    inline void replace_mqt(bool m, bool q, bool t) {
      this->sr = (this->sr & 0xFFFFFCFE) | (m ? 0x00000200 : 0) | (q ? 0x00000100 : 0) | (t ? 0x00000001 : 0);
    }

    Regs();
  };

  static std::string disassemble_one(uint32_t pc, uint16_t op, bool double_precision = false);
  static std::string disassemble(
      const void* data,
      size_t size,
      uint32_t pc = 0,
      const std::multimap<uint32_t, std::string>* labels = nullptr,
      bool double_precision = false);

  static EmulatorBase::AssembleResult assemble(const std::string& text,
      std::function<std::string(const std::string&)> get_include = nullptr,
      uint32_t start_address = 0);
  static EmulatorBase::AssembleResult assemble(const std::string& text,
      const std::vector<std::string>& include_dirs,
      uint32_t start_address = 0);

  inline Regs& registers() {
    return this->regs;
  }

private:
  Regs regs;
  std::function<void(SH4Emulator&)> debug_hook;

  static inline void assert_aligned(uint32_t addr, uint32_t alignment) {
    if (addr & (alignment - 1)) {
      throw std::runtime_error("misaligned memory access");
    }
  }

  virtual void execute_one_0(uint16_t op);
  virtual void execute_one_1(uint16_t op);
  virtual void execute_one_2(uint16_t op);
  virtual void execute_one_3(uint16_t op);
  virtual void execute_one_4(uint16_t op);
  virtual void execute_one_5(uint16_t op);
  virtual void execute_one_6(uint16_t op);
  virtual void execute_one_7(uint16_t op);
  virtual void execute_one_8(uint16_t op);
  virtual void execute_one_9(uint16_t op);
  virtual void execute_one_A_B(uint16_t op);
  virtual void execute_one_C(uint16_t op);
  virtual void execute_one_D(uint16_t op);
  virtual void execute_one_E(uint16_t op);
  virtual void execute_one_F(uint16_t op);

  struct DisassemblyState {
    uint32_t pc;
    uint32_t start_pc;
    bool double_precision;
    const std::multimap<uint32_t, std::string>* labels;
    std::map<uint32_t, bool> branch_target_addresses;
    StringReader r;
  };

  static std::string disassemble_one(DisassemblyState& s, uint16_t op);

  struct Assembler {
    struct Argument {
      enum class Type {
        UNKNOWN = 0,
        INT_REGISTER, // r3 (reg_num)
        BANK_INT_REGISTER, // r3b (reg_num)
        MEMORY_REFERENCE, // [r3] (reg_num)
        PREDEC_MEMORY_REFERENCE, // -[r3] (reg_num)
        POSTINC_MEMORY_REFERENCE, // [r3]+ (reg_num)
        REG_R0_MEMORY_REFERENCE, // [r0 + r3] or [r3 + r0] (one reg must be r0, the other is reg_num)
        GBR_R0_MEMORY_REFERENCE, // [gbr + r0] or [r0 + gbr]
        REG_DISP_MEMORY_REFERENCE, // [r3 + disp] (reg_num, disp)
        GBR_DISP_MEMORY_REFERENCE, // [gbr + disp] (disp)
        PC_MEMORY_REFERENCE, // [0x80001800] OR [label] (value OR label_name; use value iff label_name is blank)
        PC_INDEX_MEMORY_REFERENCE, // [label + rn] (label_name, reg_num)
        PC_REG_OFFSET, // label + rn (label_name, reg_num) (used for calls/bs)
        FR_DR_REGISTER, // artificial; matches both FR_REGISTER and DR_REGISTER
        DR_XD_REGISTER, // artificial; matches FD_REGISTER and XD_REGISTER
        FR_DR_XD_REGISTER, // artificial; matches FR_REGISTER, DR_REGISTER, and XD_REGISTER
        FR_REGISTER, // fs3 (reg_num)
        DR_REGISTER, // fd3 (reg_num)
        FV_REGISTER, // fv3 (reg_num)
        XD_REGISTER, // xd3 (reg_num)
        XMTRX, // xmtrx
        IMMEDIATE, // 7 (disp)
        SR, // sr
        MACH, // mach
        MACL, // macl
        GBR, // gbr
        VBR, // vbr
        DBR, // dbr
        PR, // pr
        SSR, // ssr
        SGR, // sgr
        SPC, // spc
        FPUL, // fpul
        FPSCR, // fpscr
        T, // t
        BRANCH_TARGET, // label (label_name)

        // label_name is set to the literal string passed as an argument to the
        // opcode. In this case, there is always only one argument, even if the
        // string contains commas. This is only used for the .binary directive.
        RAW,
      };
      Type type;
      uint8_t reg_num;
      int32_t value;
      std::string label_name;

      Argument(const std::string& text, bool raw = false);
      static const char* name_for_argument_type(Type type);
    };

    using ArgType = Argument::Type;

    struct StreamItem {
      size_t offset;
      size_t line_num;
      std::string op_name;
      std::vector<Argument> args;

      void check_arg_types(std::initializer_list<ArgType> types) const;
      bool check_2_same_float_regs() const; // Returns true if DR regs
      [[nodiscard]] bool arg_types_match(std::initializer_list<ArgType> types) const;
      [[noreturn]] void throw_invalid_arguments() const;
    };
    uint32_t start_address;
    std::deque<StreamItem> stream;
    std::unordered_map<std::string, uint32_t> label_offsets;
    std::unordered_map<std::string, std::string> includes_cache;
    std::unordered_map<std::string, std::string> metadata_keys;

    StringWriter code;

    typedef uint16_t (Assembler::*AssembleFunction)(const StreamItem& si) const;
    static const std::unordered_map<std::string, AssembleFunction> assemble_functions;

    uint16_t asm_add_addc_addv_sub_subc_subv(const StreamItem& si) const;
    uint16_t asm_and_or(const StreamItem& si) const;
    uint16_t asm_and_b_or_b(const StreamItem& si) const;
    uint16_t asm_bs_calls(const StreamItem& si) const;
    uint16_t asm_bt_bf_bts_bfs(const StreamItem& si) const;
    uint16_t asm_clrt(const StreamItem& si) const;
    uint16_t asm_sett(const StreamItem& si) const;
    uint16_t asm_clrmac(const StreamItem& si) const;
    uint16_t asm_clrs(const StreamItem& si) const;
    uint16_t asm_sets(const StreamItem& si) const;
    uint16_t asm_cmp_mnemonics(const StreamItem& si) const;
    uint16_t asm_dec(const StreamItem& si) const;
    uint16_t asm_div0s(const StreamItem& si) const;
    uint16_t asm_div0u(const StreamItem& si) const;
    uint16_t asm_div1(const StreamItem& si) const;
    uint16_t asm_dmuls_dmulu(const StreamItem& si) const;
    uint16_t asm_exts_extu(const StreamItem& si) const;
    uint16_t asm_fabs(const StreamItem& si) const;
    uint16_t asm_fadd(const StreamItem& si) const;
    uint16_t asm_fcmp_mnemonics(const StreamItem& si) const; // fcmpe, fcmpeq, fcmpgt
    uint16_t asm_fcnvds(const StreamItem& si) const;
    uint16_t asm_fcnvsd(const StreamItem& si) const;
    uint16_t asm_fdiv(const StreamItem& si) const;
    uint16_t asm_fipr(const StreamItem& si) const;
    uint16_t asm_fldi0_fldi1(const StreamItem& si) const;
    uint16_t asm_flds(const StreamItem& si) const;
    uint16_t asm_fsts(const StreamItem& si) const;
    uint16_t asm_float(const StreamItem& si) const;
    uint16_t asm_fmac(const StreamItem& si) const;
    uint16_t asm_fmov_fmov_s(const StreamItem& si) const;
    uint16_t asm_fmul(const StreamItem& si) const;
    uint16_t asm_fneg(const StreamItem& si) const;
    uint16_t asm_frchg_fschg(const StreamItem& si) const;
    uint16_t asm_fsqrt(const StreamItem& si) const;
    uint16_t asm_fsub(const StreamItem& si) const;
    uint16_t asm_ftrc(const StreamItem& si) const;
    uint16_t asm_ftrv(const StreamItem& si) const;
    uint16_t asm_ldc_ldc_l(const StreamItem& si) const;
    uint16_t asm_lds_lds_l(const StreamItem& si) const;
    uint16_t asm_ldtlb(const StreamItem& si) const;
    uint16_t asm_mac_w_mac_l(const StreamItem& si) const;
    uint16_t asm_mov(const StreamItem& si) const;
    uint16_t asm_mov_b_w_l(const StreamItem& si) const;
    uint16_t asm_movca_l(const StreamItem& si) const;
    uint16_t asm_mova(const StreamItem& si) const;
    uint16_t asm_movt(const StreamItem& si) const;
    uint16_t asm_mul_l(const StreamItem& si) const;
    uint16_t asm_muls_w_mulu_w(const StreamItem& si) const;
    uint16_t asm_neg_negc(const StreamItem& si) const;
    uint16_t asm_not(const StreamItem& si) const;
    uint16_t asm_nop(const StreamItem& si) const;
    uint16_t asm_ocbi_ocbp_ocbwb_pref(const StreamItem& si) const;
    uint16_t asm_rcl_rcr_rol_ror(const StreamItem& si) const;
    uint16_t asm_rets(const StreamItem& si) const;
    uint16_t asm_sleep(const StreamItem& si) const;
    uint16_t asm_rte(const StreamItem& si) const;
    uint16_t asm_shad_shld(const StreamItem& si) const;
    uint16_t asm_shal_shar(const StreamItem& si) const;
    uint16_t asm_shl_shr(const StreamItem& si) const;
    uint16_t asm_stc_stc_l(const StreamItem& si) const;
    uint16_t asm_sts_sts_l(const StreamItem& si) const;
    uint16_t asm_swap_b_w(const StreamItem& si) const;
    uint16_t asm_tas_b(const StreamItem& si) const;
    uint16_t asm_test_xor(const StreamItem& si) const;
    uint16_t asm_test_b_xor_b(const StreamItem& si) const;
    uint16_t asm_trapa(const StreamItem& si) const;
    uint16_t asm_xtrct(const StreamItem& si) const;

    void assemble(
        const std::string& text,
        std::function<std::string(const std::string&)> get_include);

    uint16_t assemble_one(const StreamItem& si);
  };
};

} // namespace ResourceDASM
