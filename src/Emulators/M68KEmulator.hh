#pragma once

#include <stdint.h>
#include <stdio.h>

#include <functional>
#include <map>
#include <phosg/Strings.hh>
#include <set>
#include <string>

#include "EmulatorBase.hh"
#include "InterruptManager.hh"
#include "MemoryContext.hh"

namespace ResourceDASM {

struct JumpTableEntry {
  int16_t code_resource_id; // Entry not valid if this is zero
  uint16_t offset; // Offset from end of CODE resource header
};

class M68KEmulator : public EmulatorBase {
public:
  static constexpr bool is_little_endian = false;

  enum class Size : uint8_t {
    BYTE = 0,
    WORD = 1,
    LONG = 2,
    INVALID = 3,
  };

  enum class ValueType {
    // Note: the values here correspond to the values in the Source Specifier (U) field in float opcodes.
    LONG = 0, // 'l'
    FLOAT = 1, // 's'
    EXTENDED = 2, // 'x'
    PACKED_DECIMAL_REAL = 3, // 'p'
    PACKED_DECIMAL_REAL_STATIC_K = 3, // 'p {k}'
    WORD = 4, // 'w'
    DOUBLE = 5, // 'd'
    BYTE = 6, // 'b'
    PACKED_DECIMAL_REAL_DYNAMIC_K = 7, // 'p {Dn}'
    INVALID = 7,
  };
  enum Condition {
    C = 0x01,
    V = 0x02,
    Z = 0x04,
    N = 0x08,
    X = 0x10,
  };

  struct Regs {
    union {
      uint32_t u;
      int32_t s;
    } d[8];
    uint32_t a[8];
    uint32_t pc;
    struct SR {
      uint16_t u; // Note: low byte of this is the ccr (condition code register)
      inline bool get_x() const { return this->u & Condition::X; };
      inline bool get_n() const { return this->u & Condition::N; };
      inline bool get_z() const { return this->u & Condition::Z; };
      inline bool get_v() const { return this->u & Condition::V; };
      inline bool get_c() const { return this->u & Condition::C; };
      inline void set_x(bool value) { this->u = value ? (this->u | Condition::X) : (this->u & (~Condition::X)); };
      inline void set_n(bool value) { this->u = value ? (this->u | Condition::N) : (this->u & (~Condition::N)); };
      inline void set_z(bool value) { this->u = value ? (this->u | Condition::Z) : (this->u & (~Condition::Z)); };
      inline void set_v(bool value) { this->u = value ? (this->u | Condition::V) : (this->u & (~Condition::V)); };
      inline void set_c(bool value) { this->u = value ? (this->u | Condition::C) : (this->u & (~Condition::C)); };
    };
    SR sr;

    Regs();

    void import_state(FILE* stream);
    void export_state(FILE* stream) const;

    void set_by_name(const std::string& reg_name, uint32_t value);

    inline uint32_t get_sp() const {
      return this->a[7];
    }
    inline void set_sp(uint32_t sp) {
      this->a[7] = sp;
    }

    uint32_t get_reg_value(bool is_a_reg, uint8_t reg_num);

    void set_ccr_flags(int64_t x, int64_t n, int64_t z, int64_t v, int64_t c);
    void set_ccr_flags_integer_add(int32_t left_value, int32_t right_value, Size size);
    void set_ccr_flags_integer_subtract(int32_t left_value, int32_t right_value, Size size);

    uint32_t pop_u32(std::shared_ptr<const MemoryContext> mem);
    int32_t pop_s32(std::shared_ptr<const MemoryContext> mem);
    uint16_t pop_u16(std::shared_ptr<const MemoryContext> mem);
    int16_t pop_s16(std::shared_ptr<const MemoryContext> mem);
    uint8_t pop_u8(std::shared_ptr<const MemoryContext> mem);
    int8_t pop_s8(std::shared_ptr<const MemoryContext> mem);

    void push_u32(std::shared_ptr<MemoryContext> mem, uint32_t v);
    void push_s32(std::shared_ptr<MemoryContext> mem, int32_t v);
    void push_u16(std::shared_ptr<MemoryContext> mem, uint16_t v);
    void push_s16(std::shared_ptr<MemoryContext> mem, int16_t v);
    void push_u8(std::shared_ptr<MemoryContext> mem, uint8_t v);
    void push_s8(std::shared_ptr<MemoryContext> mem, int8_t v);

    void write_stack_u32(std::shared_ptr<MemoryContext> mem, uint32_t v);
    void write_stack_s32(std::shared_ptr<MemoryContext> mem, int32_t v);
    void write_stack_u16(std::shared_ptr<MemoryContext> mem, uint16_t v);
    void write_stack_s16(std::shared_ptr<MemoryContext> mem, int16_t v);
    void write_stack_u8(std::shared_ptr<MemoryContext> mem, uint8_t v);
    void write_stack_s8(std::shared_ptr<MemoryContext> mem, int8_t v);
  };

  explicit M68KEmulator(std::shared_ptr<MemoryContext> mem);
  virtual ~M68KEmulator() = default;

  virtual void import_state(FILE* stream);
  virtual void export_state(FILE* stream) const;

  Regs& registers();

  virtual void print_state_header(FILE* stream) const;
  virtual void print_state(FILE* stream) const;

  struct DecodedAddress {
    enum class Mode : uint8_t {
      // clang-format off
      //                       D M C A  M Xn EXTS   = WHAT
      D_REG = 0,            // *     *  0 Dn        = D(base_reg_num)
      A_REG = 1,            //       *  1 An        = A(base_reg_num)
      MEM_A = 2,            // * * * *  2 An        = [A(base_reg_num)]
      MEM_A_POSTINC = 3,    // * *   *  3 An        = [A(base_reg_num)]+
      MEM_A_PREDEC = 4,     // * *   *  4 An        = -[A(base_reg_num)]
      MEM_A_DISP = 5,       // * * * *  5 An disp16 = [A(base_reg_num) + base_disp]
      MEM_A_INDEX = 6,      // * * * *  6 An BRIEF  = [A(base_reg_num) + X(index_reg_num).S * scale + base_disp]
                            // * * * *  6 An FULL   = [A(base_reg_num) + X(index_reg_num).S * scale + base_disp]
      MEM_A_IND_POST = 7,   // * * * *  6 An FULL   = [[An + base_disp] + X(index_reg_num).S * scale + outer_disp]
      MEM_A_IND_PRE = 8,    // * * * *  6 An FULL   = [[An + base_disp + X(index_reg_num).S * scale] + outer_disp]
      MEM_ABSOLUTE = 9,     // * * * *  7 0  disp16 = [base_disp]
                            // * * * *  7 1  disp32 = [base_disp]
      MEM_PC_DISP = 10,     // * * *    7 2  disp16 = [base_pc + base_disp]
      MEM_PC_INDEX = 11,    // * * *    7 3  BRIEF  = [base_pc + X(index_reg_num).S * scale + base_disp]
                            // * * *    7 3  FULL   = [base_pc + X(index_reg_num).S * scale + base_disp]
      MEM_PC_IND_POST = 12, // * * * *  7 3  FULL   = [[base_pc + base_disp] + X(index_reg_num).S * scale + outer_disp]
      MEM_PC_IND_PRE = 13,  // * * * *  7 3  FULL   = [[base_pc + base_disp + X(index_reg_num).S * scale] + outer_disp]
      IMM = 14,             // * *      7 4  imm    = imm8/16/32 (in base_disp)
      INVALID = 15,         //          7 5-7       = (invalid_reason not null)
      // clang-format on
    };

    static constexpr uint16_t DATA_FLAGS = 0x7FFD;
    static constexpr uint16_t MEMORY_FLAGS = 0x7FFC;
    static constexpr uint16_t CONTROL_FLAGS = 0x3FE4;
    static constexpr uint16_t ALTERABLE_FLAGS = 0x33FF;

    Mode mode;
    uint8_t base_reg_num = 0xFF;
    uint8_t index_reg_num = 0xFF;
    uint8_t index_scale = 0;
    bool index_is_a_reg = false;
    bool index_is_word = false; // Long if false
    bool suppress_base_reg = false;
    bool suppress_index = false;
    uint32_t base_pc = 0;
    int32_t base_disp = 0;
    int32_t outer_disp = 0;
    const char* invalid_reason = nullptr;

    constexpr bool is_valid() const {
      return (this->mode != Mode::INVALID);
    }
    constexpr bool is_data_mode() const {
      return this->DATA_FLAGS & (1 << static_cast<size_t>(this->mode));
    }
    constexpr bool is_memory_mode() const {
      return this->MEMORY_FLAGS & (1 << static_cast<size_t>(this->mode));
    }
    constexpr bool is_control_mode() const {
      return this->CONTROL_FLAGS & (1 << static_cast<size_t>(this->mode));
    }
    constexpr bool is_alterable_mode() const {
      return this->ALTERABLE_FLAGS & (1 << static_cast<size_t>(this->mode));
    }
    constexpr bool is_data_alterable_mode() const {
      return (this->DATA_FLAGS & this->ALTERABLE_FLAGS) & (1 << static_cast<size_t>(this->mode));
    }
    constexpr bool is_memory_alterable_mode() const {
      return (this->MEMORY_FLAGS & this->ALTERABLE_FLAGS) & (1 << static_cast<size_t>(this->mode));
    }
    constexpr bool is_control_alterable_mode() const {
      return (this->CONTROL_FLAGS & this->ALTERABLE_FLAGS) & (1 << static_cast<size_t>(this->mode));
    }
  };
  using AM = DecodedAddress::Mode;

  struct DisassemblyState {
    phosg::StringReader r;
    uint32_t start_address = 0;
    uint32_t opcode_start_address = 0;
    std::map<uint32_t, bool> branch_target_addresses;
    std::set<std::pair<uint32_t, size_t>> imm_offsets;
    bool prev_was_return = false;
    bool prev_was_valid = true;
    bool is_mac_environment = true;
    const std::vector<JumpTableEntry>* jump_table;

    DisassemblyState(
        const void* data,
        size_t size,
        uint32_t start_address,
        bool is_mac_environment,
        const std::vector<JumpTableEntry>* jump_table);

    static std::string dasm_reg_mask(uint16_t mask, bool reverse);

    int64_t compute_static_address(const DecodedAddress& addr); // Returns -1 if not static
    std::string dasm_address(const DecodedAddress& addr, ValueType type, bool add_data_comments = true);

    std::string dasm_float_mem_op(
        const char* name, uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);

    // Visitor implementation for disassembly
    using DecodeReturnT = std::string;

    uint16_t read_ins_u16(uint8_t imm_bytes = 0);
    uint32_t read_ins_u32(uint8_t imm_bytes = 0);
    int16_t read_ins_s16(uint8_t imm_bytes = 0);
    int32_t read_ins_s32(uint8_t imm_bytes = 0);
    uint32_t read_pc();

    std::string on_invalid(const char* what, const DecodedAddress* addr = nullptr);

    std::string on_ori_sr_imm(Size size, uint16_t v);
    std::string on_andi_sr_imm(Size size, uint16_t v);
    std::string on_xori_sr_imm(Size size, uint16_t v);
    std::string on_movep(bool is_long, bool is_write, uint8_t d_reg, uint8_t a_reg, int16_t disp);
    std::string on_moves(Size size, const DecodedAddress& addr, bool is_a_reg, uint8_t reg_num, bool is_write);
    std::string on_btst_bchg_bclr_bset(uint8_t what, const DecodedAddress& addr, uint8_t bit_reg_num, uint8_t imm);
    std::string on_ori(Size size, const DecodedAddress& addr, uint32_t value);
    std::string on_andi(Size size, const DecodedAddress& addr, uint32_t value);
    std::string on_subi(Size size, const DecodedAddress& addr, uint32_t value);
    std::string on_addi(Size size, const DecodedAddress& addr, uint32_t value);
    std::string on_xori(Size size, const DecodedAddress& addr, uint32_t value);
    std::string on_cmpi(Size size, const DecodedAddress& addr, uint32_t value);
    std::string on_rtm();
    std::string on_callm(const DecodedAddress& addr, uint8_t value);
    std::string on_cas2(Size size, bool mem1_is_a_reg, uint8_t mem1_reg, uint8_t compare1_reg, uint8_t update1_reg, bool mem2_is_a_reg, uint8_t mem2_reg, uint8_t compare2_reg, uint8_t update2_reg);
    std::string on_cas(Size size, const DecodedAddress& addr, uint8_t compare_reg, uint8_t update_reg);
    std::string on_chk2_cmp2(Size size, const DecodedAddress& addr, bool is_a_reg, uint8_t reg_num, bool is_chk2);
    std::string on_movea(Size size, uint8_t dest_reg, const DecodedAddress& src_addr);
    std::string on_move(Size size, const DecodedAddress& dest_addr, const DecodedAddress& src_addr);
    std::string on_movem_read(Size size, const DecodedAddress& addr, uint16_t reg_mask);
    std::string on_movem_write(Size size, const DecodedAddress& addr, uint16_t reg_mask);
    std::string on_lea(uint8_t reg_num, const DecodedAddress& addr);
    std::string on_chk(Size size, const DecodedAddress& addr, uint8_t reg);
    std::string on_ext_byte_word(uint8_t reg_num);
    std::string on_ext_word_long(uint8_t reg_num);
    std::string on_ext_byte_long(uint8_t reg_num);
    std::string on_move_dest_sr(const DecodedAddress& addr);
    std::string on_move_dest_ccr(const DecodedAddress& addr);
    std::string on_move_ccr_src(const DecodedAddress& addr);
    std::string on_move_sr_src(const DecodedAddress& addr);
    std::string on_negx(Size size, const DecodedAddress& addr);
    std::string on_clr(Size size, const DecodedAddress& addr);
    std::string on_neg(Size size, const DecodedAddress& addr);
    std::string on_not(Size size, const DecodedAddress& addr);
    std::string on_link(uint8_t a_reg_num, int32_t disp);
    std::string on_nbcd(const DecodedAddress& addr);
    std::string on_swap(uint8_t d_reg_num);
    std::string on_bkpt(uint8_t v);
    std::string on_pea(const DecodedAddress& addr);
    std::string on_tst(Size size, const DecodedAddress& addr);
    std::string on_tas(const DecodedAddress& addr);
    std::string on_bgnd();
    std::string on_illegal();
    std::string on_muls_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low);
    std::string on_mulu_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low);
    std::string on_divs_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low);
    std::string on_divu_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low);
    std::string on_jsr_jmp(const DecodedAddress& addr, bool is_jsr);
    std::string on_trap(uint8_t num);
    std::string on_unlink(uint8_t a_reg_num);
    std::string on_move_usp(bool is_read, uint8_t a_reg_num);
    std::string on_reset();
    std::string on_nop();
    std::string on_stop(uint16_t value);
    std::string on_rte();
    std::string on_rtd(int16_t disp);
    std::string on_rts();
    std::string on_trapv();
    std::string on_rtr();
    std::string on_movec(bool is_write, bool is_a_reg, uint8_t reg_num, uint16_t cr_num);
    std::string on_addq_subq(Size size, const DecodedAddress& addr, int64_t value);
    std::string on_dbcc(uint8_t condition, uint8_t reg_num, int16_t disp);
    std::string on_trapcc(uint8_t condition, int64_t value);
    std::string on_scc(uint8_t condition, const DecodedAddress& addr);
    std::string on_bra_bsr_bcc(uint8_t condition, int32_t disp, uint8_t disp_size);
    std::string on_bra(int32_t disp, uint8_t disp_size);
    std::string on_bsr(int32_t disp, uint8_t disp_size);
    std::string on_bcc(uint8_t condition, int32_t disp, uint8_t disp_size);
    std::string on_moveq(uint8_t d_reg_num, int8_t value);
    std::string on_sbcd(bool is_mem, uint8_t dest_reg, uint8_t src_reg);
    std::string on_pack(bool is_mem_predec, uint8_t dest_reg_num, uint8_t src_reg_num, int16_t ext);
    std::string on_unpack(bool is_mem_predec, uint8_t dest_reg_num, uint8_t src_reg_num, int16_t ext);
    std::string on_divs_word(const DecodedAddress& addr, uint8_t d_reg_num);
    std::string on_divu_word(const DecodedAddress& addr, uint8_t d_reg_num);
    std::string on_or(Size size, const DecodedAddress& addr, uint8_t d_reg_num, bool dest_is_memory);
    std::string on_addx_subx(Size size, bool is_memory, uint8_t dest_reg_num, uint8_t src_reg_num, bool is_add);
    std::string on_adda_suba(bool is_long_op, const DecodedAddress& addr, uint8_t reg_num, bool is_add);
    std::string on_add_sub(Size size, const DecodedAddress& addr, uint8_t reg_num, bool dest_is_memory, bool is_add);
    std::string on_syscall(uint16_t opcode);
    std::string on_cmpm(Size size, uint8_t a_reg1, uint8_t a_reg2);
    std::string on_cmp(Size size, uint8_t d_reg_num, const DecodedAddress& addr);
    std::string on_cmpa(bool is_long_op, uint8_t a_reg_num, const DecodedAddress& addr);
    std::string on_xor(Size size, uint8_t reg_num, const DecodedAddress& addr);
    std::string on_muls_word(const DecodedAddress& addr, uint8_t reg_num);
    std::string on_mulu_word(const DecodedAddress& addr, uint8_t reg_num);
    std::string on_abcd(bool is_mem, uint8_t reg_x, uint8_t reg_y);
    std::string on_exg_d_d(uint8_t d_reg1, uint8_t d_reg2);
    std::string on_exg_a_a(uint8_t a_reg1, uint8_t a_reg2);
    std::string on_exg_d_a(uint8_t d_reg, uint8_t a_reg);
    std::string on_and(Size size, const DecodedAddress& addr, uint8_t reg_num, bool dest_is_mem);
    std::string on_bf_ops(uint8_t which, const DecodedAddress& addr, uint8_t x_reg, bool offset_is_reg, int32_t offset, bool width_is_reg, uint8_t width);
    std::string on_bit_shift_mem(uint8_t which, const DecodedAddress& addr);
    std::string on_bit_shift_reg(uint8_t which, Size size, uint8_t reg_num, bool count_is_reg, uint8_t count);
    std::string on_fmovecr(uint8_t f_reg, uint8_t offset);
    std::string on_fmove_to_mem(const DecodedAddress& addr, uint8_t f_reg, ValueType format, uint8_t k);
    std::string on_fmove(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fint(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fsinh(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fintrz(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fsqrt(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_flognp1(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fetoxm1(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_ftanh(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fatan(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fasin(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fatanh(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fsin(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_ftan(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fetox(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_ftwotox(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_ftentox(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_flogn(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_flog10(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_flog2(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fabs(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fcosh(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fneg(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_facos(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fcos(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fgetexp(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fgetman(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fdiv(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fmod(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fadd(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fmul(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fsgldiv(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_frem(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fscale(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fsglmul(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fsub(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fsincos(uint8_t sin_reg, uint8_t cos_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fcmp(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_ftst(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fsmove(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fssqrt(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fdmove(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fdsqrt(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fsabs(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fsneg(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fdabs(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fdneg(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fsdiv(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fsadd(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fsmul(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fddiv(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fdadd(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fdmul(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fssub(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fdsub(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
    std::string on_fmovem_control_regs(const DecodedAddress& addr, uint8_t mask, bool is_write);
    std::string on_fmovem_data_regs(const DecodedAddress& addr, bool mask_is_d_reg, uint8_t mask, bool is_write);
    std::string on_fdbcc(uint8_t condition, uint8_t reg, int16_t disp);
    std::string on_ftrapcc(uint8_t condition, int64_t value);
    std::string on_fscc(uint8_t condition, const DecodedAddress& addr);
    std::string on_fbcc(uint8_t condition, int32_t disp, uint8_t disp_size);
    std::string on_fsave(const DecodedAddress& addr);
    std::string on_frestore(const DecodedAddress& addr);
    std::string on_coprocessor(uint16_t opcode);
  };

  static std::string disassemble_one(DisassemblyState& s);
  static DisassembleResult disassemble_one_structured(DisassemblyState& s);
  static std::string disassemble_one(
      const void* vdata,
      size_t size,
      uint32_t start_address = 0,
      bool is_mac_environment = true,
      const std::vector<JumpTableEntry>* jump_table = nullptr);
  static DisassembleResult disassemble_one_structured(
      const void* vdata,
      size_t size,
      uint32_t start_address = 0,
      bool is_mac_environment = true,
      const std::vector<JumpTableEntry>* jump_table = nullptr);
  static std::string disassemble(
      const void* vdata,
      size_t size,
      uint32_t start_address = 0,
      const std::multimap<uint32_t, std::string>* labels = nullptr,
      bool is_mac_environment = true,
      const std::vector<JumpTableEntry>* jump_table = nullptr);

  static AssembleResult assemble(
      const std::string& text,
      std::function<std::string(const std::string&)> get_include = nullptr,
      uint32_t start_address = 0);
  static AssembleResult assemble(
      const std::string& text,
      const std::vector<std::string>& include_dirs,
      uint32_t start_address = 0);

  inline void set_syscall_handler(std::function<void(M68KEmulator&, uint16_t)> handler) {
    this->syscall_handler = handler;
  }

  inline void set_debug_hook(std::function<void(M68KEmulator&)> hook) {
    this->debug_hook = hook;
  }

  inline void set_interrupt_manager(std::shared_ptr<InterruptManager> im) {
    this->interrupt_manager = im;
  }

  virtual void execute_one();
  virtual void execute();

private:
  using DecodeReturnT = void;

  Regs regs;

  std::function<void(M68KEmulator&, uint16_t)> syscall_handler;
  std::function<void(M68KEmulator&)> debug_hook;
  std::shared_ptr<InterruptManager> interrupt_manager;

  template <typename VisitorT>
  static VisitorT::DecodeReturnT decode_instruction(VisitorT& visitor);

  // Visitor implementation for execution

  uint16_t read_ins_u16(uint8_t imm_bytes = 0);
  uint32_t read_ins_u32(uint8_t imm_bytes = 0);
  int16_t read_ins_s16(uint8_t imm_bytes = 0);
  int32_t read_ins_s32(uint8_t imm_bytes = 0);
  uint32_t read_pc();

  void on_invalid(const char* what, const DecodedAddress* addr = nullptr);

  void on_ori_sr_imm(Size size, uint16_t v);
  void on_andi_sr_imm(Size size, uint16_t v);
  void on_xori_sr_imm(Size size, uint16_t v);
  void on_movep(bool is_long, bool is_write, uint8_t d_reg, uint8_t a_reg, int16_t disp);
  void on_moves(Size size, const DecodedAddress& addr, bool is_a_reg, uint8_t reg_num, bool is_write);
  void on_btst_bchg_bclr_bset(uint8_t what, const DecodedAddress& addr, uint8_t bit_reg_num, uint8_t imm);
  void on_ori(Size size, const DecodedAddress& addr, uint32_t value);
  void on_andi(Size size, const DecodedAddress& addr, uint32_t value);
  void on_subi(Size size, const DecodedAddress& addr, uint32_t value);
  void on_addi(Size size, const DecodedAddress& addr, uint32_t value);
  void on_xori(Size size, const DecodedAddress& addr, uint32_t value);
  void on_cmpi(Size size, const DecodedAddress& addr, uint32_t value);
  void on_rtm();
  void on_callm(const DecodedAddress& addr, uint8_t value);
  void on_cas2(Size size, bool mem1_is_a_reg, uint8_t mem1_reg, uint8_t compare1_reg, uint8_t update1_reg, bool mem2_is_a_reg, uint8_t mem2_reg, uint8_t compare2_reg, uint8_t update2_reg);
  void on_cas(Size size, const DecodedAddress& addr, uint8_t compare_reg, uint8_t update_reg);
  void on_chk2_cmp2(Size size, const DecodedAddress& addr, bool is_a_reg, uint8_t reg_num, bool is_chk2);
  void on_movea(Size size, uint8_t dest_reg, const DecodedAddress& src_addr);
  void on_move(Size size, const DecodedAddress& dest_addr, const DecodedAddress& src_addr);
  void on_movem_read(Size size, const DecodedAddress& addr, uint16_t reg_mask);
  void on_movem_write(Size size, const DecodedAddress& addr, uint16_t reg_mask);
  void on_lea(uint8_t reg_num, const DecodedAddress& addr);
  void on_chk(Size size, const DecodedAddress& addr, uint8_t reg);
  void on_ext_byte_word(uint8_t reg_num);
  void on_ext_word_long(uint8_t reg_num);
  void on_ext_byte_long(uint8_t reg_num);
  void on_move_dest_sr(const DecodedAddress& addr);
  void on_move_dest_ccr(const DecodedAddress& addr);
  void on_move_ccr_src(const DecodedAddress& addr);
  void on_move_sr_src(const DecodedAddress& addr);
  void on_negx(Size size, const DecodedAddress& addr);
  void on_clr(Size size, const DecodedAddress& addr);
  void on_neg(Size size, const DecodedAddress& addr);
  void on_not(Size size, const DecodedAddress& addr);
  void on_link(uint8_t a_reg_num, int32_t disp);
  void on_nbcd(const DecodedAddress& addr);
  void on_swap(uint8_t d_reg_num);
  void on_bkpt(uint8_t v);
  void on_pea(const DecodedAddress& addr);
  void on_tst(Size size, const DecodedAddress& addr);
  void on_tas(const DecodedAddress& addr);
  void on_bgnd();
  void on_illegal();
  void on_muls_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low);
  void on_mulu_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low);
  void on_divs_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low);
  void on_divu_long(bool is_64, const DecodedAddress& addr, uint8_t reg_high, uint8_t reg_low);
  void on_jsr_jmp(const DecodedAddress& addr, bool is_jsr);
  void on_trap(uint8_t num);
  void on_unlink(uint8_t a_reg_num);
  void on_move_usp(bool is_read, uint8_t a_reg_num);
  void on_reset();
  void on_nop();
  void on_stop(uint16_t value);
  void on_rte();
  void on_rtd(int16_t disp);
  void on_rts();
  void on_trapv();
  void on_rtr();
  void on_movec(bool is_write, bool is_a_reg, uint8_t reg_num, uint16_t cr_num);
  void on_addq_subq(Size size, const DecodedAddress& addr, int64_t value);
  void on_dbcc(uint8_t condition, uint8_t reg_num, int16_t disp);
  void on_trapcc(uint8_t condition, int64_t value);
  void on_scc(uint8_t condition, const DecodedAddress& addr);
  void on_bra(int32_t disp, uint8_t disp_size);
  void on_bsr(int32_t disp, uint8_t disp_size);
  void on_bcc(uint8_t condition, int32_t disp, uint8_t disp_size);
  void on_moveq(uint8_t d_reg_num, int8_t value);
  void on_sbcd(bool is_mem, uint8_t dest_reg, uint8_t src_reg);
  void on_pack(bool is_mem_predec, uint8_t dest_reg_num, uint8_t src_reg_num, int16_t ext);
  void on_unpack(bool is_mem_predec, uint8_t dest_reg_num, uint8_t src_reg_num, int16_t ext);
  void on_divs_word(const DecodedAddress& addr, uint8_t d_reg_num);
  void on_divu_word(const DecodedAddress& addr, uint8_t d_reg_num);
  void on_or(Size size, const DecodedAddress& addr, uint8_t d_reg_num, bool dest_is_memory);
  void on_addx_subx(Size size, bool is_memory, uint8_t dest_reg_num, uint8_t src_reg_num, bool is_add);
  void on_adda_suba(bool is_long_op, const DecodedAddress& addr, uint8_t reg_num, bool is_add);
  void on_add_sub(Size size, const DecodedAddress& addr, uint8_t dest_reg_num, bool dest_is_memory, bool is_add);
  void on_syscall(uint16_t opcode);
  void on_cmpm(Size size, uint8_t a_reg1, uint8_t a_reg2);
  void on_cmp(Size size, uint8_t d_reg_num, const DecodedAddress& addr);
  void on_cmpa(bool is_long_op, uint8_t a_reg_num, const DecodedAddress& addr);
  void on_xor(Size size, uint8_t reg_num, const DecodedAddress& addr);
  void on_muls_word(const DecodedAddress& addr, uint8_t reg_num);
  void on_mulu_word(const DecodedAddress& addr, uint8_t reg_num);
  void on_abcd(bool is_mem, uint8_t reg_x, uint8_t reg_y);
  void on_exg_d_d(uint8_t d_reg1, uint8_t d_reg2);
  void on_exg_a_a(uint8_t a_reg1, uint8_t a_reg2);
  void on_exg_d_a(uint8_t d_reg, uint8_t a_reg);
  void on_and(Size size, const DecodedAddress& addr, uint8_t reg_num, bool dest_is_mem);
  void on_bf_ops(uint8_t which, const DecodedAddress& addr, uint8_t x_reg, bool offset_is_reg, int32_t offset, bool width_is_reg, uint8_t width);
  void on_bit_shift_mem(uint8_t which, const DecodedAddress& addr);
  void on_bit_shift_reg(uint8_t which, Size size, uint8_t reg_num, bool count_is_reg, uint8_t count);
  void on_fmovecr(uint8_t f_reg, uint8_t offset);
  void on_fmove_to_mem(const DecodedAddress& addr, uint8_t f_reg, ValueType format, uint8_t k);
  void on_fmove(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fint(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fsinh(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fintrz(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fsqrt(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_flognp1(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fetoxm1(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_ftanh(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fatan(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fasin(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fatanh(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fsin(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_ftan(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fetox(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_ftwotox(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_ftentox(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_flogn(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_flog10(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_flog2(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fabs(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fcosh(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fneg(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_facos(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fcos(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fgetexp(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fgetman(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fdiv(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fmod(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fadd(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fmul(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fsgldiv(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_frem(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fscale(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fsglmul(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fsub(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fsincos(uint8_t sin_reg, uint8_t cos_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fcmp(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_ftst(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fsmove(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fssqrt(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fdmove(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fdsqrt(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fsabs(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fsneg(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fdabs(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fdneg(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fsdiv(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fsadd(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fsmul(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fddiv(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fdadd(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fdmul(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fssub(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fdsub(uint8_t f_reg, uint8_t src_spec, const DecodedAddress& addr, bool is_mem_read);
  void on_fmovem_control_regs(const DecodedAddress& addr, uint8_t mask, bool is_write);
  void on_fmovem_data_regs(const DecodedAddress& addr, bool mask_is_d_reg, uint8_t mask, bool is_write);
  void on_fdbcc(uint8_t condition, uint8_t reg, int16_t disp);
  void on_ftrapcc(uint8_t condition, int64_t value);
  void on_fscc(uint8_t condition, const DecodedAddress& addr);
  void on_fbcc(uint8_t condition, int32_t disp, uint8_t disp_size);
  void on_fsave(const DecodedAddress& addr);
  void on_frestore(const DecodedAddress& addr);
  void on_coprocessor(uint16_t opcode);

  struct ResolvedAddress {
    enum class Type {
      D_REG = 0,
      A_REG,
      MEMORY,
      IMM,
    };
    Type type;
    uint32_t where;
  };
  ResolvedAddress resolve_address(const DecodedAddress& addr, Size size);
  uint32_t resolve_memory_address(const DecodedAddress& addr, Size size);

  uint32_t read(const ResolvedAddress& addr, Size size) const;
  uint32_t read(uint32_t addr, Size size) const;
  void write(const ResolvedAddress& addr, uint32_t value, Size size);
  void write(uint32_t addr, uint32_t value, Size size);

  bool check_condition(uint8_t condition);
};

} // namespace ResourceDASM
