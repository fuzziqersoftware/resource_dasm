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

class MCS6502Emulator : public EmulatorBase<MCS6502Emulator> {
public:
  static constexpr bool is_little_endian = true;

  enum Condition {
    C = 0x01,
    Z = 0x02,
    I = 0x04,
    D = 0x08,
    B = 0x10,
    __UNUSED__ = 0x20, // Always 1
    V = 0x40,
    N = 0x80,
  };

  struct Regs {
    uint8_t a = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t s = 0xFD;
    uint16_t pc = 0;
    struct P {
      uint8_t u = 0x20;
      inline bool get_c() const { return this->u & Condition::C; };
      inline bool get_z() const { return this->u & Condition::Z; };
      inline bool get_i() const { return this->u & Condition::I; };
      inline bool get_d() const { return this->u & Condition::D; };
      inline bool get_b() const { return this->u & Condition::B; };
      inline bool get_v() const { return this->u & Condition::V; };
      inline bool get_n() const { return this->u & Condition::N; };
      inline void set_c(bool value) { this->u = value ? (this->u | Condition::C) : (this->u & (~Condition::C)); };
      inline void set_z(bool value) { this->u = value ? (this->u | Condition::Z) : (this->u & (~Condition::Z)); };
      inline void set_i(bool value) { this->u = value ? (this->u | Condition::I) : (this->u & (~Condition::I)); };
      inline void set_d(bool value) { this->u = value ? (this->u | Condition::D) : (this->u & (~Condition::D)); };
      inline void set_b(bool value) { this->u = value ? (this->u | Condition::B) : (this->u & (~Condition::B)); };
      inline void set_v(bool value) { this->u = value ? (this->u | Condition::V) : (this->u & (~Condition::V)); };
      inline void set_n(bool value) { this->u = value ? (this->u | Condition::N) : (this->u & (~Condition::N)); };
    };
    P p;

    void import_state(FILE* stream);
    void export_state(FILE* stream) const;

    void set_by_name(const std::string& reg_name, uint32_t value);

    inline uint32_t get_sp() const {
      return 0x00000100 | this->s;
    }
    inline void set_sp(uint32_t sp) {
      this->s = sp;
    }
  };

  using EmulatorBase::EmulatorBase;
  virtual ~MCS6502Emulator() = default;

  virtual void import_state(FILE* stream);
  virtual void export_state(FILE* stream) const;

  inline Regs& registers() {
    return this->regs;
  }
  inline const Regs& registers() const {
    return this->regs;
  }

  virtual void print_state_header(FILE* stream) const;
  virtual void print_state(FILE* stream) const;

  struct DecodedAddress {
    enum class Mode : uint8_t {
      IMM = 0, // disp8 (no memory access); 2 cycles
      ZERO_PAGE, // [disp8]; 3 cycles
      ZERO_PAGE_X, // [(x + disp8) & 0xFF]; 4 cycles
      ZERO_PAGE_Y, // [(y + disp8) & 0xFF]; 4 cycles
      ABSOLUTE, // [disp16]; 4 cycles
      ABSOLUTE_X, // [x + disp16]; 4 cycles + 1 if page boundary crossed
      ABSOLUTE_Y, // [y + disp16]; 4 cycles + 1 if page boundary crossed
      INDIRECT_X, // [[x + disp8]]; 6 cycles
      INDIRECT_Y, // [[y] + disp8]; 5 cycles + 1 if page boundary crossed
      A, // a (no memory access)
      INVALID, // (invalid_reason not null)
    };
    Mode mode;
    uint16_t disp = 0;
    const char* invalid_reason = nullptr;

    std::string str() const;
  };
  using AM = DecodedAddress::Mode;

  struct DisassemblyState {
    using DecodeReturnT = std::string;

    phosg::StringReader r;
    uint32_t start_address = 0;
    uint32_t opcode_start_address = 0;
    std::map<uint32_t, bool> branch_target_addresses;
    bool prev_was_valid = true;

    DisassemblyState(const void* data, size_t size, uint32_t start_address);

    inline void add_branch_target_address(uint32_t addr, bool is_call) {
      if (is_call) {
        this->branch_target_addresses[addr] = true;
      } else {
        this->branch_target_addresses.emplace(addr, false);
      }
    }
    inline void add_jump_target_address(const DecodedAddress& addr, bool is_call) {
      if (addr.mode == AM::IMM) {
        this->add_branch_target_address(addr.disp, is_call);
      } else if (addr.mode == AM::ABSOLUTE) {
        try {
          if ((addr.disp & 0xFF) == 0xFF) {
            this->add_branch_target_address(
                this->r.pget_u8(addr.disp) | (this->r.get_u8(addr.disp & 0xFF00) << 8), is_call);
          } else {
            this->add_branch_target_address(this->r.pget_u16l(addr.disp), is_call);
          }
        } catch (const std::out_of_range&) {
        }
      }
    }

    inline uint8_t read_ins_u8() {
      return this->r.get_u8();
    }
    inline int8_t read_ins_s8() {
      return this->r.get_s8();
    }
    inline uint16_t read_ins_u16() {
      return this->r.get_u16l();
    }

    std::string on_invalid(const char* invalid_reason);
    std::string on_invalid(const DecodedAddress& addr);

    std::string on_adc(const DecodedAddress& addr);
    std::string on_and(const DecodedAddress& addr);
    std::string on_asl(const DecodedAddress& addr);
    std::string on_b_mnemonics(uint8_t condition, int8_t delta);
    std::string on_bit(const DecodedAddress& addr);
    std::string on_brk();
    std::string on_clc();
    std::string on_cld();
    std::string on_cli();
    std::string on_clv();
    std::string on_cmp(const DecodedAddress& addr);
    std::string on_cpx(const DecodedAddress& addr);
    std::string on_cpy(const DecodedAddress& addr);
    std::string on_dec(const DecodedAddress& addr);
    std::string on_dex();
    std::string on_dey();
    std::string on_inc(const DecodedAddress& addr);
    std::string on_inx();
    std::string on_iny();
    std::string on_jmp(const DecodedAddress& addr);
    std::string on_jsr(const DecodedAddress& addr);
    std::string on_lda(const DecodedAddress& addr);
    std::string on_ldx(const DecodedAddress& addr);
    std::string on_ldy(const DecodedAddress& addr);
    std::string on_lsr(const DecodedAddress& addr);
    std::string on_nop();
    std::string on_ora(const DecodedAddress& addr);
    std::string on_pha();
    std::string on_php();
    std::string on_pla();
    std::string on_plp();
    std::string on_rol(const DecodedAddress& addr);
    std::string on_ror(const DecodedAddress& addr);
    std::string on_rti();
    std::string on_rts();
    std::string on_sbc(const DecodedAddress& addr);
    std::string on_sec();
    std::string on_sed();
    std::string on_sei();
    std::string on_sta(const DecodedAddress& addr);
    std::string on_stx(const DecodedAddress& addr);
    std::string on_sty(const DecodedAddress& addr);
    std::string on_tax();
    std::string on_tay();
    std::string on_tsx();
    std::string on_txa();
    std::string on_txs();
    std::string on_tya();
    std::string on_xor(const DecodedAddress& addr);
  };

  static std::string disassemble_one(DisassemblyState& s);
  static DisassembleResult disassemble_one_structured(DisassemblyState& s);
  static std::string disassemble_one(const void* vdata, size_t size, uint32_t start_address = 0);
  static DisassembleResult disassemble_one_structured(const void* vdata, size_t size, uint32_t start_address = 0);
  static std::string disassemble(
      const void* vdata,
      size_t size,
      uint32_t start_address = 0,
      const std::multimap<uint32_t, std::string>* labels = nullptr);

  using EmulatorBase<MCS6502Emulator>::assemble;
  static AssembleResult assemble(
      const std::string& text,
      std::function<std::string(const std::string&)> get_include = nullptr,
      uint32_t start_address = 0);

  inline void set_debug_hook(std::function<void(MCS6502Emulator&)> hook) {
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

  std::function<void(MCS6502Emulator&)> debug_hook;
  std::shared_ptr<InterruptManager> interrupt_manager;

  template <typename VisitorT>
  static VisitorT::DecodeReturnT decode_instruction(VisitorT& visitor);

  uint8_t read_ins_u8();
  int8_t read_ins_s8();
  uint16_t read_ins_u16();

  uint16_t read_u16_from_zero_page(uint8_t addr) const;

  uint8_t read_from_address(const DecodedAddress& addr) const;
  void write_to_address(const DecodedAddress& addr, uint8_t value);

  inline void push8(uint8_t v) {
    this->mem->write_u8(0x0100 | this->regs.s, v);
    this->regs.s--;
  }
  inline uint8_t pop8() {
    this->regs.s++;
    return this->mem->read_u8(0x0100 | this->regs.s);
  }
  inline void push16(uint16_t v) {
    this->push8((v >> 8) & 0xFF);
    this->push8(v & 0xFF);
  }
  inline uint16_t pop16() {
    uint8_t low_byte = this->pop8();
    return (this->pop8() << 8) | low_byte;
  }

  void on_invalid(const char* invalid_reason);
  void on_invalid(const DecodedAddress& addr);

  void on_adc_sbc(const DecodedAddress& addr, bool is_sbc);
  void on_adc(const DecodedAddress& addr);
  void on_and(const DecodedAddress& addr);
  void on_asl(const DecodedAddress& addr);
  void on_b_mnemonics(uint8_t condition, int8_t delta);
  void on_bit(const DecodedAddress& addr);
  void on_brk();
  void on_clc();
  void on_cld();
  void on_cli();
  void on_clv();
  void on_cmp_values(uint8_t reg_val, const DecodedAddress& addr);
  void on_cmp(const DecodedAddress& addr);
  void on_cpx(const DecodedAddress& addr);
  void on_cpy(const DecodedAddress& addr);
  void on_dec(const DecodedAddress& addr);
  void on_dex();
  void on_dey();
  void on_inc(const DecodedAddress& addr);
  void on_inx();
  void on_iny();
  void on_jmp(const DecodedAddress& addr);
  void on_jsr(const DecodedAddress& addr);
  void on_lda(const DecodedAddress& addr);
  void on_ldx(const DecodedAddress& addr);
  void on_ldy(const DecodedAddress& addr);
  void on_lsr(const DecodedAddress& addr);
  void on_nop();
  void on_ora(const DecodedAddress& addr);
  void on_pha();
  void on_php();
  void on_pla();
  void on_plp();
  void on_rol(const DecodedAddress& addr);
  void on_ror(const DecodedAddress& addr);
  void on_rti();
  void on_rts();
  void on_sbc(const DecodedAddress& addr);
  void on_sec();
  void on_sed();
  void on_sei();
  void on_sta(const DecodedAddress& addr);
  void on_stx(const DecodedAddress& addr);
  void on_sty(const DecodedAddress& addr);
  void on_tax();
  void on_tay();
  void on_tsx();
  void on_txa();
  void on_txs();
  void on_tya();
  void on_xor(const DecodedAddress& addr);
};

} // namespace ResourceDASM
