#pragma once

#include <stdint.h>
#include <stdio.h>

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <phosg/Strings.hh>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>

#include "EmulatorBase.hh"
#include "InterruptManager.hh"
#include "MemoryContext.hh"

namespace ResourceDASM {

using namespace phosg;

class X86Emulator : public EmulatorBase {
public:
  static constexpr bool is_little_endian = true;

  enum class Segment {
    NONE = 0,
    CS,
    DS,
    ES,
    FS,
    GS,
    SS,
  };
  static const char* name_for_segment(Segment segment);

  struct Overrides {
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

  class Regs {
  public:
    union IntReg {
      le_uint32_t u;
      le_int32_t s;
      le_uint16_t u16;
      le_int16_t s16;
      struct U8Fields {
        uint8_t l;
        uint8_t h;
      } __attribute__((packed));
      U8Fields u8;
      struct S8Fields {
        int8_t l;
        int8_t h;
      } __attribute__((packed));
      S8Fields s8;
    } __attribute__((packed));

    union XMMReg {
      // Because these are little-endian, the highest value is in the last entry;
      // that is, u64[1] is the high half, and u8[15] is the highest byte.
      uint8_t u8[16];
      le_uint16_t u16[8];
      le_uint32_t u32[4];
      le_uint64_t u64[2];
      int8_t s8[16];
      le_int16_t s16[8];
      le_int32_t s32[4];
      le_int64_t s64[2];
      le_float f32[4];
      le_double f64[2];

      XMMReg();
      XMMReg(uint32_t v);
      XMMReg(uint64_t v);
      XMMReg& operator=(uint32_t v);
      XMMReg& operator=(uint64_t v);

      operator uint32_t() const;
      operator uint64_t() const;

      inline void clear() {
        this->u64[0] = 0;
        this->u64[1] = 0;
      }

      template <typename T>
      T* as() {
        static_assert(sizeof(T) <= sizeof(XMMReg), "partition type is too large");
        static_assert((sizeof(XMMReg) % sizeof(T)) == 0,
            "partition type does not evenly divide xmm reg data");
        return reinterpret_cast<T*>(&this->u8[0]);
      }

      template <typename T>
      const T& lowest() const {
        static_assert(sizeof(T) <= sizeof(XMMReg), "partition type is too large");
        static_assert((sizeof(XMMReg) % sizeof(T)) == 0,
            "partition type does not evenly divide xmm reg data");
        return *reinterpret_cast<const T*>(&this->u8[0]);
      }
      template <typename T>
      const T& highest() const {
        static_assert(sizeof(T) <= sizeof(XMMReg), "partition type is too large");
        static_assert((sizeof(XMMReg) % sizeof(T)) == 0,
            "partition type does not evenly divide xmm reg data");
        const T* data = reinterpret_cast<const T*>(&this->u8[0]);
        return data[(sizeof(XMMReg) / sizeof(T)) - 1];
      }
    } __attribute__((packed));

    union {
      uint32_t eip;
      uint32_t pc;
    };

    Regs();

    inline IntReg& eax() { return this->regs[0]; }
    inline const IntReg& eax() const { return this->regs[0]; }
    inline IntReg& ecx() { return this->regs[1]; }
    inline const IntReg& ecx() const { return this->regs[1]; }
    inline IntReg& edx() { return this->regs[2]; }
    inline const IntReg& edx() const { return this->regs[2]; }
    inline IntReg& ebx() { return this->regs[3]; }
    inline const IntReg& ebx() const { return this->regs[3]; }
    inline IntReg& esp() { return this->regs[4]; }
    inline const IntReg& esp() const { return this->regs[4]; }
    inline IntReg& ebp() { return this->regs[5]; }
    inline const IntReg& ebp() const { return this->regs[5]; }
    inline IntReg& esi() { return this->regs[6]; }
    inline const IntReg& esi() const { return this->regs[6]; }
    inline IntReg& edi() { return this->regs[7]; }
    inline const IntReg& edi() const { return this->regs[7]; }

    uint8_t& reg8(uint8_t which);
    le_uint16_t& reg16(uint8_t which);
    le_uint32_t& reg32(uint8_t which);
    const uint8_t& reg8(uint8_t which) const;
    const le_uint16_t& reg16(uint8_t which) const;
    const le_uint32_t& reg32(uint8_t which) const;

    le_uint32_t& xmm32(uint8_t which);
    le_uint64_t& xmm64(uint8_t which);
    XMMReg& xmm128(uint8_t which);
    const le_uint32_t& xmm32(uint8_t which) const;
    const le_uint64_t& xmm64(uint8_t which) const;
    const XMMReg& xmm128(uint8_t which) const;

    uint32_t read(uint8_t which, uint8_t size) const;
    XMMReg read_xmm(uint8_t which, uint8_t size) const;

    template <typename T>
    T read(uint8_t which) const {
      if (bits_for_type<T> == 8) {
        return this->reg8(which);
      } else if (bits_for_type<T> == 16) {
        return this->reg16(which).load();
      } else if (bits_for_type<T> == 32) {
        return this->reg32(which).load();
      } else {
        throw std::logic_error("invalid register size");
      }
    }
    template <typename T>
    T read_xmm(uint8_t which) const {
      if (bits_for_type<T> == 32) {
        return this->xmm32(which).load();
      } else if (bits_for_type<T> == 64) {
        return this->xmm64(which).load();
      } else if (bits_for_type<T> == 128) {
        return this->xmm128(which).lowest<T>();
      } else {
        throw std::logic_error("invalid register size");
      }
    }
    template <typename T>
    void write(uint8_t which, T value) {
      if (bits_for_type<T> == 8) {
        this->reg8(which) = value;
      } else if (bits_for_type<T> == 16) {
        this->reg16(which).store(value);
      } else if (bits_for_type<T> == 32) {
        this->reg32(which).store(value);
      } else {
        throw std::logic_error("invalid register size");
      }
    }
    template <typename T>
    void write_xmm(uint8_t which, T value) {
      if (bits_for_type<T> == 32) {
        this->xmm32(which).store(value);
      } else if (bits_for_type<T> == 64) {
        this->xmm64(which).store(value);
      } else if (bits_for_type<T> == 128) {
        this->xmm128(which) = value;
      } else {
        throw std::logic_error("invalid register size");
      }
    }

    inline uint8_t read8(uint8_t which) const { return this->read<uint8_t>(which); }
    inline uint16_t read16(uint8_t which) const { return this->read<le_uint16_t>(which); }
    inline uint32_t read32(uint8_t which) const { return this->read<le_uint32_t>(which); }
    inline uint32_t read_xmm32(uint8_t which) const { return this->read_xmm<le_uint32_t>(which); }
    inline uint64_t read_xmm64(uint8_t which) const { return this->read_xmm<le_uint64_t>(which); }
    inline XMMReg read_xmm128(uint8_t which) const { return this->read_xmm<XMMReg>(which); }
    inline void write8(uint8_t which, uint8_t v) { this->write<uint8_t>(which, v); }
    inline void write16(uint8_t which, le_uint16_t v) { this->write<le_uint16_t>(which, v); }
    inline void write32(uint8_t which, le_uint32_t v) { this->write<le_uint32_t>(which, v); }
    inline void write_xmm32(uint8_t which, uint32_t v) { this->write_xmm<le_uint32_t>(which, v); }
    inline void write_xmm64(uint8_t which, uint64_t v) { this->write_xmm<le_uint64_t>(which, v); }
    inline void write_xmm128(uint8_t which, const XMMReg& v) { this->write_xmm<XMMReg>(which, v); }

    inline uint8_t r_al() const { return this->read<uint8_t>(0); }
    inline uint8_t r_cl() const { return this->read<uint8_t>(1); }
    inline uint8_t r_dl() const { return this->read<uint8_t>(2); }
    inline uint8_t r_bl() const { return this->read<uint8_t>(3); }
    inline uint8_t r_ah() const { return this->read<uint8_t>(4); }
    inline uint8_t r_ch() const { return this->read<uint8_t>(5); }
    inline uint8_t r_dh() const { return this->read<uint8_t>(6); }
    inline uint8_t r_bh() const { return this->read<uint8_t>(7); }
    inline uint16_t r_ax() const { return this->read<le_uint16_t>(0); }
    inline uint16_t r_cx() const { return this->read<le_uint16_t>(1); }
    inline uint16_t r_dx() const { return this->read<le_uint16_t>(2); }
    inline uint16_t r_bx() const { return this->read<le_uint16_t>(3); }
    inline uint16_t r_sp() const { return this->read<le_uint16_t>(4); }
    inline uint16_t r_bp() const { return this->read<le_uint16_t>(5); }
    inline uint16_t r_si() const { return this->read<le_uint16_t>(6); }
    inline uint16_t r_di() const { return this->read<le_uint16_t>(7); }
    inline uint32_t r_eax() const { return this->read<le_uint32_t>(0); }
    inline uint32_t r_ecx() const { return this->read<le_uint32_t>(1); }
    inline uint32_t r_edx() const { return this->read<le_uint32_t>(2); }
    inline uint32_t r_ebx() const { return this->read<le_uint32_t>(3); }
    inline uint32_t r_esp() const { return this->read<le_uint32_t>(4); }
    inline uint32_t r_ebp() const { return this->read<le_uint32_t>(5); }
    inline uint32_t r_esi() const { return this->read<le_uint32_t>(6); }
    inline uint32_t r_edi() const { return this->read<le_uint32_t>(7); }

    inline void w_al(uint8_t v) { this->write<uint8_t>(0, v); }
    inline void w_cl(uint8_t v) { this->write<uint8_t>(1, v); }
    inline void w_dl(uint8_t v) { this->write<uint8_t>(2, v); }
    inline void w_bl(uint8_t v) { this->write<uint8_t>(3, v); }
    inline void w_ah(uint8_t v) { this->write<uint8_t>(4, v); }
    inline void w_ch(uint8_t v) { this->write<uint8_t>(5, v); }
    inline void w_dh(uint8_t v) { this->write<uint8_t>(6, v); }
    inline void w_bh(uint8_t v) { this->write<uint8_t>(7, v); }
    inline void w_ax(uint16_t v) { this->write<le_uint16_t>(0, v); }
    inline void w_cx(uint16_t v) { this->write<le_uint16_t>(1, v); }
    inline void w_dx(uint16_t v) { this->write<le_uint16_t>(2, v); }
    inline void w_bx(uint16_t v) { this->write<le_uint16_t>(3, v); }
    inline void w_sp(uint16_t v) { this->write<le_uint16_t>(4, v); }
    inline void w_bp(uint16_t v) { this->write<le_uint16_t>(5, v); }
    inline void w_si(uint16_t v) { this->write<le_uint16_t>(6, v); }
    inline void w_di(uint16_t v) { this->write<le_uint16_t>(7, v); }
    inline void w_eax(uint32_t v) { this->write<le_uint32_t>(0, v); }
    inline void w_ecx(uint32_t v) { this->write<le_uint32_t>(1, v); }
    inline void w_edx(uint32_t v) { this->write<le_uint32_t>(2, v); }
    inline void w_ebx(uint32_t v) { this->write<le_uint32_t>(3, v); }
    inline void w_esp(uint32_t v) { this->write<le_uint32_t>(4, v); }
    inline void w_ebp(uint32_t v) { this->write<le_uint32_t>(5, v); }
    inline void w_esi(uint32_t v) { this->write<le_uint32_t>(6, v); }
    inline void w_edi(uint32_t v) { this->write<le_uint32_t>(7, v); }

    inline uint32_t read_eflags() const {
      return this->eflags;
    }
    inline void write_eflags(uint32_t v) {
      this->eflags = v;
    }

    void set_by_name(const std::string& reg_name, uint32_t value);

    inline uint32_t get_sp() const {
      return this->r_esp();
    }
    inline void set_sp(uint32_t sp) {
      this->w_esp(sp);
    }

    static constexpr uint32_t CF = 0x0001;
    static constexpr uint32_t PF = 0x0004;
    static constexpr uint32_t AF = 0x0010;
    static constexpr uint32_t ZF = 0x0040;
    static constexpr uint32_t SF = 0x0080;
    static constexpr uint32_t IF = 0x0200;
    static constexpr uint32_t DF = 0x0400;
    static constexpr uint32_t OF = 0x0800;
    static constexpr uint32_t default_int_flags = CF | PF | AF | ZF | SF | OF;

    bool read_flag(uint32_t mask);
    void replace_flag(uint32_t mask, bool value);

    static std::string flags_str(uint32_t eflags);
    std::string flags_str() const;

    template <typename T, std::enable_if_t<std::is_unsigned<T>::value, bool> = true>
    void set_flags_integer_result(T res, uint32_t apply_mask = Regs::default_int_flags);
    template <typename T, std::enable_if_t<std::is_unsigned<T>::value, bool> = true>
    void set_flags_bitwise_result(T res, uint32_t apply_mask = Regs::default_int_flags);
    template <typename T, std::enable_if_t<std::is_unsigned<T>::value, bool> = true>
    T set_flags_integer_add(T a, T b, uint32_t apply_mask = Regs::default_int_flags);
    template <typename T, std::enable_if_t<std::is_unsigned<T>::value, bool> = true>
    T set_flags_integer_add_with_carry(T a, T b, uint32_t apply_mask = Regs::default_int_flags);
    template <typename T, std::enable_if_t<std::is_unsigned<T>::value, bool> = true>
    T set_flags_integer_subtract(T a, T b, uint32_t apply_mask = Regs::default_int_flags);
    template <typename T, std::enable_if_t<std::is_unsigned<T>::value, bool> = true>
    T set_flags_integer_subtract_with_borrow(T a, T b, uint32_t apply_mask = Regs::default_int_flags);

    bool check_condition(uint8_t cc);

    void import_state(FILE* stream);
    void export_state(FILE* stream) const;

  private:
    IntReg regs[8];
    XMMReg xmm[8];

    uint32_t eflags;
  };

  explicit X86Emulator(std::shared_ptr<MemoryContext> mem);
  virtual ~X86Emulator() = default;

  virtual void import_state(FILE* stream);
  virtual void export_state(FILE* stream) const;

  inline Regs& registers() {
    return this->regs;
  }

  virtual void print_state_header(FILE* stream) const;
  virtual void print_state(FILE* stream) const;

  static std::string disassemble(
      const void* vdata,
      size_t size,
      uint32_t start_address = 0,
      const std::multimap<uint32_t, std::string>* labels = nullptr);

  static AssembleResult assemble(const std::string& text,
      std::function<std::string(const std::string&)> get_include = nullptr,
      uint32_t start_address = 0);
  static AssembleResult assemble(const std::string& text,
      const std::vector<std::string>& include_dirs,
      uint32_t start_address = 0);

  // NOTE: If the storage size of this enum changes, the format versions
  // implemented in import_state and export_state must also change.
  enum class Behavior : uint8_t {
    // Default behavior is to emulate an x86 CPU implemented according to
    // Intel's manuals. All unspecified behaviors do nothing; for example, flags
    // whose values are technically undefined after certain opcodes are never
    // affected in this mode (they retain their previous values).
    SPECIFICATION = 0,
    // Behave like the CPU emulator implemented in Windows 11 for ARM64 systems.
    // This CPU emulator has some supposedly nonstandard behaviors; for example,
    // bit shift opcodes do not set the result status flags (SF, ZF, PF) whereas
    // the manual says they should.
    WINDOWS_ARM_EMULATOR,
  };

  inline Behavior get_behavior() const {
    return this->behavior;
  }
  inline void set_behavior(Behavior b) {
    this->behavior = b;
  }
  virtual void set_behavior_by_name(const std::string& name);

  virtual void set_time_base(uint64_t time_base);
  virtual void set_time_base(const std::vector<uint64_t>& time_overrides);

  inline void set_syscall_handler(
      std::function<void(X86Emulator&, uint8_t)> handler) {
    this->syscall_handler = handler;
  }

  inline void set_debug_hook(
      std::function<void(X86Emulator&)> hook) {
    this->debug_hook = hook;
  }

  virtual void execute();

  template <typename T>
  void push(T value) {
    this->regs.w_esp(this->regs.r_esp() - sizeof(T));
    this->w_mem<T>(this->regs.r_esp(), value);
  }

  template <typename T>
  T pop() {
    uint32_t addr = this->regs.r_esp();
    T ret = this->r_mem<T>(addr);
    this->regs.w_esp(addr + sizeof(T));
    return ret;
  }

protected:
  Regs prev_regs;
  Regs regs;
  Behavior behavior;
  uint64_t tsc_offset;
  std::deque<uint64_t> tsc_overrides;

  Overrides overrides;
  std::function<void(X86Emulator&, uint8_t)> syscall_handler;
  std::function<void(X86Emulator&)> debug_hook;

  mutable bool execution_labels_computed;
  mutable std::multimap<uint32_t, std::string> execution_labels;

  void compute_execution_labels() const;

  struct DecodedRM {
    int8_t non_ea_reg;
    int8_t ea_reg; // -1 = no reg
    int8_t ea_index_reg; // -1 = no reg (also ea_index_scale should be -1 or 0)
    int8_t ea_index_scale; // -1 (ea_reg is not to be dereferenced), 0 (no index reg), 1, 2, 4, or 8
    int32_t ea_disp;

    DecodedRM() = default;
    DecodedRM(int8_t ea_reg, int32_t ea_disp);

    bool has_mem_ref() const;

    enum StrFlags {
      EA_FIRST = 0x01,
      EA_ST = 0x02,
      NON_EA_ST = 0x04,
      EA_XMM = 0x08,
      NON_EA_XMM = 0x10,
      SUPPRESS_OPERAND_SIZE = 0x20,
      SUPPRESS_ADDRESS_TOKEN = 0x40,
    };

    std::string ea_str(uint8_t operand_size, uint8_t flags, Segment override_segment) const;
    std::string non_ea_str(uint8_t operand_size, uint8_t flags) const;
  };

  using RMF = DecodedRM::StrFlags;

  struct DisassemblyState {
    StringReader r;
    uint32_t start_address;
    uint8_t opcode;
    Overrides overrides;
    std::map<uint32_t, bool> branch_target_addresses;
    const std::multimap<uint32_t, std::string>* labels;
    // If not null, the emulator pointer is used for resolving EA addresses
    // based on the emulator's current state (for use in the interactive
    // debugging shell)
    const X86Emulator* emu;

    uint8_t standard_operand_size() const;

    std::string rm_ea_str(const DecodedRM& rm, uint8_t operand_size, uint8_t flags) const;
    std::string rm_non_ea_str(const DecodedRM& rm, uint8_t operand_size, uint8_t flags) const;
    std::string rm_str(const DecodedRM& rm, uint8_t operand_size, uint8_t flags) const;
    std::string rm_str(const DecodedRM& rm, uint8_t ea_operand_size, uint8_t non_ea_operand_size, uint8_t flags) const;

    std::string annotation_for_rm_ea(const DecodedRM& rm, int64_t operand_size, uint8_t flags = 0) const;
  };

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

  DecodedRM fetch_and_decode_rm();
  static DecodedRM fetch_and_decode_rm(StringReader& r);

  uint32_t get_segment_offset() const;
  uint32_t resolve_mem_ea(const DecodedRM& rm) const;

  template <typename T>
  T read_non_ea(const DecodedRM& rm) {
    return this->regs.read<T>(rm.non_ea_reg);
  }
  template <typename T>
  T read_non_ea_xmm(const DecodedRM& rm) {
    return this->regs.read_xmm<T>(rm.non_ea_reg);
  }
  template <typename T>
  void write_non_ea(const DecodedRM& rm, T v) {
    this->regs.write<T>(rm.non_ea_reg, v);
  }
  template <typename T>
  void write_non_ea_xmm(const DecodedRM& rm, T v) {
    this->regs.write_xmm<T>(rm.non_ea_reg, v);
  }

  template <typename T>
  T read_ea(const DecodedRM& rm) {
    return (rm.ea_index_scale < 0)
        ? this->regs.read<T>(rm.ea_reg)
        : this->r_mem<T>(this->resolve_mem_ea(rm));
  }
  template <typename T>
  T read_ea_xmm(const DecodedRM& rm) {
    return (rm.ea_index_scale < 0)
        ? this->regs.read_xmm<T>(rm.ea_reg)
        : this->r_mem<T>(this->resolve_mem_ea(rm));
  }
  template <typename T>
  void write_ea(const DecodedRM& rm, T v) {
    if (rm.ea_index_scale < 0) {
      this->regs.write<T>(rm.ea_reg, v);
    } else {
      this->w_mem<T>(this->resolve_mem_ea(rm), v);
    }
  }
  template <typename T>
  void write_ea_xmm(const DecodedRM& rm, T v) {
    if (rm.ea_index_scale < 0) {
      this->regs.write_xmm<T>(rm.ea_reg, v);
    } else {
      this->w_mem<T>(this->resolve_mem_ea(rm), v);
    }
  }

  inline uint8_t r_non_ea8(const DecodedRM& rm) { return this->read_non_ea<uint8_t>(rm); }
  inline uint16_t r_non_ea16(const DecodedRM& rm) { return this->read_non_ea<le_uint16_t>(rm); }
  inline uint32_t r_non_ea32(const DecodedRM& rm) { return this->read_non_ea<le_uint32_t>(rm); }
  inline uint32_t r_non_ea_xmm32(const DecodedRM& rm) { return this->read_non_ea_xmm<le_uint32_t>(rm); }
  inline uint64_t r_non_ea_xmm64(const DecodedRM& rm) { return this->read_non_ea_xmm<le_uint64_t>(rm); }
  inline Regs::XMMReg r_non_ea_xmm128(const DecodedRM& rm) { return this->read_non_ea_xmm<Regs::XMMReg>(rm); }
  inline void w_non_ea8(const DecodedRM& rm, uint8_t v) { this->write_non_ea<uint8_t>(rm, v); }
  inline void w_non_ea16(const DecodedRM& rm, uint16_t v) { this->write_non_ea<le_uint16_t>(rm, v); }
  inline void w_non_ea32(const DecodedRM& rm, uint32_t v) { this->write_non_ea<le_uint32_t>(rm, v); }
  inline void w_non_ea_xmm32(const DecodedRM& rm, uint32_t v) { this->write_non_ea_xmm<le_uint32_t>(rm, v); }
  inline void w_non_ea_xmm64(const DecodedRM& rm, uint64_t v) { this->write_non_ea_xmm<le_uint64_t>(rm, v); }
  inline void w_non_ea_xmm128(const DecodedRM& rm, const Regs::XMMReg& v) { this->write_non_ea_xmm<Regs::XMMReg>(rm, v); }
  inline uint8_t r_ea8(const DecodedRM& rm) { return this->read_ea<uint8_t>(rm); }
  inline uint16_t r_ea16(const DecodedRM& rm) { return this->read_ea<le_uint16_t>(rm); }
  inline uint32_t r_ea32(const DecodedRM& rm) { return this->read_ea<le_uint32_t>(rm); }
  inline uint32_t r_ea_xmm32(const DecodedRM& rm) { return this->read_ea_xmm<le_uint32_t>(rm); }
  inline uint64_t r_ea_xmm64(const DecodedRM& rm) { return this->read_ea_xmm<le_uint64_t>(rm); }
  inline Regs::XMMReg r_ea_xmm128(const DecodedRM& rm) { return this->read_ea_xmm<Regs::XMMReg>(rm); }
  inline void w_ea8(const DecodedRM& rm, uint8_t v) { this->write_ea<uint8_t>(rm, v); }
  inline void w_ea16(const DecodedRM& rm, uint16_t v) { this->write_ea<le_uint16_t>(rm, v); }
  inline void w_ea32(const DecodedRM& rm, uint32_t v) { this->write_ea<le_uint32_t>(rm, v); }
  inline void w_ea_xmm32(const DecodedRM& rm, uint32_t v) { this->write_ea_xmm<le_uint32_t>(rm, v); }
  inline void w_ea_xmm64(const DecodedRM& rm, uint64_t v) { this->write_ea_xmm<le_uint64_t>(rm, v); }
  inline void w_ea_xmm128(const DecodedRM& rm, const Regs::XMMReg& v) { this->write_ea_xmm<Regs::XMMReg>(rm, v); }

  template <typename T>
  T r_mem(uint32_t addr) {
    return this->mem->read<T>(addr);
  }
  template <typename T>
  void w_mem(uint32_t addr, T value) {
    this->mem->write<T>(addr, value);
  }

  static std::string disassemble_one(DisassemblyState& s);

  template <typename T>
  T exec_integer_math_logic(uint8_t what, T dest, T src);
  template <typename T, typename LET = little_endian<T>>
  T exec_F6_F7_misc_math_logic(uint8_t what, T value);
  template <typename T>
  T exec_bit_test_ops_logic(uint8_t what, T v, uint8_t bit_number);
  template <typename T>
  T exec_bit_shifts_logic(uint8_t what, T value, uint8_t distance, bool distance_is_cl);
  template <typename T>
  T exec_shld_shrd_logic(bool is_right_shift, T dest_value, T incoming_value, uint8_t distance, bool distance_is_cl);
  template <typename T, typename LET = little_endian<T>>
  void exec_string_op_logic(uint8_t opcode);
  template <typename T, typename LET = little_endian<T>>
  void exec_rep_string_op_logic(uint8_t opcode);

  void exec_0F_extensions(uint8_t);
  static std::string dasm_0F_extensions(DisassemblyState& s);
  void exec_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math(uint8_t opcode);
  static std::string dasm_0x_1x_2x_3x_x0_x1_x8_x9_mem_reg_math(DisassemblyState& s);
  void exec_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math(uint8_t opcode);
  static std::string dasm_0x_1x_2x_3x_x2_x3_xA_xB_reg_mem_math(DisassemblyState& s);
  void exec_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math(uint8_t opcode);
  static std::string dasm_0x_1x_2x_3x_x4_x5_xC_xD_eax_imm_math(DisassemblyState& s);
  void exec_06_0E_16_1E_0FA0_0FA8_push_segment_reg(uint8_t opcode);
  static std::string dasm_06_0E_16_1E_0FA0_0FA8_push_segment_reg(DisassemblyState& s);
  void exec_07_17_1F_0FA1_0FA9_pop_segment_reg(uint8_t opcode);
  static std::string dasm_07_17_1F_0FA1_0FA9_pop_segment_reg(DisassemblyState& s);
  void exec_26_es(uint8_t);
  static std::string dasm_26_es(DisassemblyState& s);
  void exec_27_daa(uint8_t);
  static std::string dasm_27_daa(DisassemblyState& s);
  void exec_2E_cs(uint8_t);
  static std::string dasm_2E_cs(DisassemblyState& s);
  void exec_36_ss(uint8_t);
  static std::string dasm_36_ss(DisassemblyState& s);
  void exec_37_aaa(uint8_t);
  static std::string dasm_37_aaa(DisassemblyState& s);
  void exec_3E_ds(uint8_t);
  static std::string dasm_3E_ds(DisassemblyState& s);
  void exec_40_to_47_inc(uint8_t opcode);
  void exec_48_to_4F_dec(uint8_t opcode);
  static std::string dasm_40_to_4F_inc_dec(DisassemblyState& s);
  void exec_50_to_57_push(uint8_t opcode);
  void exec_58_to_5F_pop(uint8_t opcode);
  static std::string dasm_50_to_5F_push_pop(DisassemblyState& s);
  void exec_60_pusha(uint8_t);
  static std::string dasm_60_pusha(DisassemblyState& s);
  void exec_61_popa(uint8_t);
  static std::string dasm_61_popa(DisassemblyState& s);
  void exec_64_fs(uint8_t);
  static std::string dasm_64_fs(DisassemblyState& s);
  void exec_65_gs(uint8_t);
  static std::string dasm_65_gs(DisassemblyState& s);
  void exec_66_operand_size(uint8_t);
  static std::string dasm_66_operand_size(DisassemblyState& s);
  void exec_68_6A_push(uint8_t);
  static std::string dasm_68_6A_push(DisassemblyState& s);
  void exec_69_6B_imul(uint8_t);
  static std::string dasm_69_6B_imul(DisassemblyState& s);
  void exec_70_to_7F_jcc(uint8_t opcode);
  static std::string dasm_70_to_7F_jcc(DisassemblyState& s);
  void exec_80_to_83_imm_math(uint8_t opcode);
  static std::string dasm_80_to_83_imm_math(DisassemblyState& s);
  void exec_84_85_test_rm(uint8_t opcode);
  static std::string dasm_84_85_test_rm(DisassemblyState& s);
  void exec_86_87_xchg_rm(uint8_t opcode);
  static std::string dasm_86_87_xchg_rm(DisassemblyState& s);
  void exec_88_to_8B_mov_rm(uint8_t opcode);
  static std::string dasm_88_to_8B_mov_rm(DisassemblyState& s);
  void exec_8D_lea(uint8_t);
  static std::string dasm_8D_lea(DisassemblyState& s);
  void exec_8F_pop_rm(uint8_t opcode);
  static std::string dasm_8F_pop_rm(DisassemblyState& s);
  void exec_90_to_97_xchg_eax(uint8_t opcode);
  static std::string dasm_90_to_97_xchg_eax(DisassemblyState& s);
  void exec_98_cbw_cwde(uint8_t);
  static std::string dasm_98_cbw_cwde(DisassemblyState& s);
  void exec_99_cwd_cdq(uint8_t);
  static std::string dasm_99_cwd_cdq(DisassemblyState& s);
  void exec_9C_pushf_pushfd(uint8_t);
  static std::string dasm_9C_pushf_pushfd(DisassemblyState& s);
  void exec_9D_popf_popfd(uint8_t);
  static std::string dasm_9D_popf_popfd(DisassemblyState& s);
  void exec_9F_lahf(uint8_t);
  static std::string dasm_9F_lahf(DisassemblyState&);
  void exec_A0_A1_A2_A3_mov_eax_memabs(uint8_t opcode);
  static std::string dasm_A0_A1_A2_A3_mov_eax_memabs(DisassemblyState& s);
  void exec_A4_to_A7_AA_to_AF_string_ops(uint8_t opcode);
  static std::string dasm_A4_to_A7_AA_to_AF_string_ops(DisassemblyState& s);
  void exec_A8_A9_test_eax_imm(uint8_t opcode);
  static std::string dasm_A8_A9_test_eax_imm(DisassemblyState& s);
  void exec_B0_to_BF_mov_imm(uint8_t opcode);
  static std::string dasm_B0_to_BF_mov_imm(DisassemblyState& s);
  void exec_C0_C1_bit_shifts(uint8_t opcode);
  static std::string dasm_C0_C1_bit_shifts(DisassemblyState& s);
  void exec_C2_C3_CA_CB_ret(uint8_t opcode);
  static std::string dasm_C2_C3_CA_CB_ret(DisassemblyState& s);
  void exec_C6_C7_mov_rm_imm(uint8_t opcode);
  static std::string dasm_C6_C7_mov_rm_imm(DisassemblyState& s);
  void exec_C8_enter(uint8_t opcode);
  static std::string dasm_C8_enter(DisassemblyState& s);
  void exec_C9_leave(uint8_t);
  static std::string dasm_C9_leave(DisassemblyState& s);
  void exec_CC_CD_int(uint8_t opcode);
  static std::string dasm_CC_CD_int(DisassemblyState& s);
  void exec_CE_into(uint8_t opcode);
  static std::string dasm_CE_into(DisassemblyState& s);
  void exec_CF_iret(uint8_t opcode);
  static std::string dasm_CF_iret(DisassemblyState& s);
  void exec_D0_to_D3_bit_shifts(uint8_t opcode);
  static std::string dasm_D0_to_D3_bit_shifts(DisassemblyState& s);
  void exec_D4_amx_aam(uint8_t);
  static std::string dasm_D4_amx_aam(DisassemblyState& s);
  void exec_D5_adx_aad(uint8_t);
  static std::string dasm_D5_adx_aad(DisassemblyState& s);
  void exec_D8_DC_float_basic_math(uint8_t opcode);
  void exec_D9_DD_float_moves_and_analytical_math(uint8_t opcode);
  void exec_DA_DB_float_cmov_and_int_math(uint8_t opcode);
  void exec_DE_float_misc1(uint8_t opcode);
  void exec_DF_float_misc2(uint8_t opcode);
  static std::string dasm_D8_DC_float_basic_math(DisassemblyState& s);
  static std::string dasm_D9_DD_float_moves_and_analytical_math(DisassemblyState& s);
  static std::string dasm_DA_DB_float_cmov_and_int_math(DisassemblyState& s);
  static std::string dasm_DE_float_misc1(DisassemblyState& s);
  static std::string dasm_DF_float_misc2(DisassemblyState& s);
  void exec_E4_E5_EC_ED_in(uint8_t opcode);
  static std::string dasm_E4_E5_EC_ED_in(DisassemblyState& s);
  void exec_E6_E7_EE_EF_out(uint8_t opcode);
  static std::string dasm_E6_E7_EE_EF_out(DisassemblyState& s);
  void exec_E8_E9_call_jmp(uint8_t opcode);
  static std::string dasm_E8_E9_call_jmp(DisassemblyState& s);
  void exec_EB_jmp(uint8_t opcode);
  static std::string dasm_EB_jmp(DisassemblyState& s);
  void exec_F2_F3_repz_repnz(uint8_t opcode);
  static std::string dasm_F2_F3_repz_repnz(DisassemblyState& s);
  void exec_F5_cmc(uint8_t);
  static std::string dasm_F5_cmc(DisassemblyState&);
  void exec_F6_F7_misc_math(uint8_t opcode);
  static std::string dasm_F6_F7_misc_math(DisassemblyState& s);
  void exec_F8_clc(uint8_t);
  static std::string dasm_F8_clc(DisassemblyState&);
  void exec_F9_stc(uint8_t);
  static std::string dasm_F9_stc(DisassemblyState&);
  void exec_FA_cli(uint8_t);
  static std::string dasm_FA_cli(DisassemblyState&);
  void exec_FB_sti(uint8_t);
  static std::string dasm_FB_sti(DisassemblyState&);
  void exec_FC_cld(uint8_t);
  static std::string dasm_FC_cld(DisassemblyState&);
  void exec_FD_std(uint8_t);
  static std::string dasm_FD_std(DisassemblyState&);
  void exec_FE_FF_inc_dec_misc(uint8_t opcode);
  static std::string dasm_FE_FF_inc_dec_misc(DisassemblyState& s);

  void exec_0F_10_11_mov_xmm(uint8_t opcode);
  static std::string dasm_0F_10_11_mov_xmm(DisassemblyState& s);
  void exec_0F_18_to_1F_prefetch_or_nop(uint8_t opcode);
  static std::string dasm_0F_18_to_1F_prefetch_or_nop(DisassemblyState& s);
  void exec_0F_31_rdtsc(uint8_t opcode);
  static std::string dasm_0F_31_rdtsc(DisassemblyState& s);
  void exec_0F_40_to_4F_cmov_rm(uint8_t opcode);
  static std::string dasm_0F_40_to_4F_cmov_rm(DisassemblyState& s);
  void exec_0F_7E_7F_mov_xmm(uint8_t opcode);
  static std::string dasm_0F_7E_7F_mov_xmm(DisassemblyState& s);
  void exec_0F_80_to_8F_jcc(uint8_t opcode);
  static std::string dasm_0F_80_to_8F_jcc(DisassemblyState& s);
  void exec_0F_90_to_9F_setcc_rm(uint8_t opcode);
  static std::string dasm_0F_90_to_9F_setcc_rm(DisassemblyState& s);
  void exec_0F_A2_cpuid(uint8_t opcode);
  static std::string dasm_0F_A2_cpuid(DisassemblyState& s);
  void exec_0F_A3_AB_B3_BB_bit_tests(uint8_t opcode);
  static std::string dasm_0F_A3_AB_B3_BB_bit_tests(DisassemblyState& s);
  void exec_0F_A4_A5_AC_AD_shld_shrd(uint8_t opcode);
  static std::string dasm_0F_A4_A5_AC_AD_shld_shrd(DisassemblyState& s);
  void exec_0F_AF_imul(uint8_t opcode);
  static std::string dasm_0F_AF_imul(DisassemblyState& s);
  void exec_0F_B6_B7_BE_BF_movzx_movsx(uint8_t opcode);
  static std::string dasm_0F_B6_B7_BE_BF_movzx_movsx(DisassemblyState& s);
  void exec_0F_BA_bit_tests(uint8_t);
  static std::string dasm_0F_BA_bit_tests(DisassemblyState& s);
  void exec_0F_BC_BD_bsf_bsr(uint8_t opcode);
  static std::string dasm_0F_BC_BD_bsf_bsr(DisassemblyState& s);
  void exec_0F_C0_C1_xadd_rm(uint8_t opcode);
  static std::string dasm_0F_C0_C1_xadd_rm(DisassemblyState& s);
  void exec_0F_C8_to_CF_bswap(uint8_t opcode);
  static std::string dasm_0F_C8_to_CF_bswap(DisassemblyState& s);
  void exec_0F_D6_movq_variants(uint8_t opcode);
  static std::string dasm_0F_D6_movq_variants(DisassemblyState& s);

  void exec_unimplemented(uint8_t opcode);
  static std::string dasm_unimplemented(DisassemblyState& s);
  void exec_0F_unimplemented(uint8_t opcode);
  static std::string dasm_0F_unimplemented(DisassemblyState& s);

  struct OpcodeImplementation {
    void (X86Emulator::*exec)(uint8_t);
    std::string (*dasm)(DisassemblyState& s);

    OpcodeImplementation()
        : exec(nullptr),
          dasm(nullptr) {}
    OpcodeImplementation(
        void (X86Emulator::*exec)(uint8_t),
        std::string (*dasm)(DisassemblyState& s))
        : exec(exec),
          dasm(dasm) {}
  };
  static const OpcodeImplementation fns[0x100];
  static const OpcodeImplementation fns_0F[0x100];

  struct Assembler {
    struct Argument {
      enum Type {
        INT_REGISTER = 0x01, // "eax", "ecx", etc. (reg_num)
        FLOAT_REGISTER = 0x02, // "st0", "st1", etc. (reg_num); plain "st" parsed as "st0"
        XMM_REGISTER = 0x04, // "xmm0", "xmm1", etc. (reg_num)

        IMMEDIATE = 0x08, // "{}" or "0x{:X}", optionally preceded by a + or - (value, scale)

        // reg_num = base reg, reg_num2 = index reg (if scale != 0), value = displacement
        MEMORY_REFERENCE = 0x10, // "dword [reg]", "byte [reg + {}]", etc.

        BRANCH_TARGET = 0x20, // label_name

        // label_name is set to the literal string passed as an argument to the
        // opcode. In this case, there is always only one argument, even if the
        // string contains commas. This is only used for the .binary directive.
        RAW = 0x40,

        // Convenience masks used in check_arg_types
        MEM_OR_IREG_OR_IMM = MEMORY_REFERENCE | INT_REGISTER | IMMEDIATE,
        MEM_OR_IREG = MEMORY_REFERENCE | INT_REGISTER,
        MEM_OR_FREG = MEMORY_REFERENCE | FLOAT_REGISTER,
        MEM_OR_XMMREG = MEMORY_REFERENCE | XMM_REGISTER,
        MEM_OR_REG = MEMORY_REFERENCE | INT_REGISTER | FLOAT_REGISTER | XMM_REGISTER,
      };
      Type type;
      uint8_t operand_size = 0; // 0 = unspecified; otherwise 1, 2, 4, or 8
      uint8_t reg_num = 0;
      uint8_t reg_num2 = 0;
      uint8_t scale = 0; // 0 = no scale reg; otherwise 1, 2, 4, or 8; for IMMEDIATE this is nonzero if there was a preceding + or -
      uint64_t value = 0;
      std::string label_name;

      Argument(const std::string& text, bool raw = false);

      bool is_reg_ref() const;
      std::string str() const;
    };

    using T = Argument::Type;

    struct StreamItem {
      size_t offset = 0;
      size_t address = 0;
      size_t fixed_address = 0;
      size_t index = 0;
      size_t line_num = 0;
      std::string op_name;
      std::vector<Argument> args;
      std::string assembled_data;
      bool has_code_delta = false;
      bool allow_short_jmp = true;
      std::unordered_set<std::string> label_names;

      std::string str() const;

      uint8_t resolve_operand_size(StringWriter& w, size_t max_args = 0) const;
      void check_arg_types(std::initializer_list<Argument::Type> types) const;
      [[nodiscard]] bool arg_types_match(std::initializer_list<Argument::Type> types) const;
      void check_arg_operand_sizes(std::initializer_list<uint8_t> operand_sizes) const;
      void check_arg_fixed_registers(std::initializer_list<uint8_t> reg_nums) const;
      void check_arg_is_st(size_t arg_num, uint8_t which) const;
      uint8_t require_16_or_32(StringWriter& w, size_t max_args = 0) const;
      uint8_t require_arg_16_or_32(size_t arg_index) const;
      uint8_t require_arg_32_or_64(size_t arg_index) const;
      uint8_t require_arg_16_or_32_or_64(size_t arg_index) const;
      uint8_t get_size_mnemonic_suffix(const std::string& base_name) const;
      uint8_t require_size_mnemonic_suffix(StringWriter& w, const std::string& base_name) const;
    };
    uint32_t start_address = 0;
    std::vector<StreamItem> stream;
    std::unordered_map<std::string, size_t> label_si_indexes;
    std::unordered_map<std::string, size_t> fixed_labels;
    std::unordered_map<std::string, std::string> includes_cache;
    std::unordered_map<std::string, std::string> metadata_keys;

    typedef void (Assembler::*AssembleFunction)(StringWriter& w, StreamItem& si) const;
    static const std::unordered_map<std::string, AssembleFunction> assemble_functions;

    AssembleResult assemble(
        const std::string& text,
        std::function<std::string(const std::string&)> get_include);

    void encode_imm(StringWriter& w, uint64_t value, uint8_t operand_size) const;
    void encode_rm(StringWriter& w, const Argument& mem_ref, const Argument& reg_ref) const;
    void encode_rm(StringWriter& w, const Argument& mem_ref, uint8_t op_type) const;
    uint32_t compute_branch_delta_from_arg0(const StreamItem& si) const;

    void asm_aaa_aas_aad_aam(StringWriter& w, StreamItem& si) const;
    void asm_add_or_adc_sbb_and_sub_xor_cmp(StringWriter& w, StreamItem& si) const;
    void asm_amx_adx(StringWriter& w, StreamItem& si) const;
    void asm_bsf_bsr(StringWriter& w, StreamItem& si) const;
    void asm_bswap(StringWriter& w, StreamItem& si) const;
    void asm_bt_bts_btr_btc(StringWriter& w, StreamItem& si) const;
    void asm_call_jmp(StringWriter& w, StreamItem& si) const;
    void asm_cbw_cwde(StringWriter& w, StreamItem& si) const;
    void asm_clc(StringWriter& w, StreamItem& si) const;
    void asm_cld(StringWriter& w, StreamItem& si) const;
    void asm_cli(StringWriter& w, StreamItem& si) const;
    void asm_cmc(StringWriter& w, StreamItem& si) const;
    void asm_cmov_mnemonics(StringWriter& w, StreamItem& si) const;
    void asm_ins_outs_movs_cmps_stos_lods_scas_mnemonics(StringWriter& w, StreamItem& si) const;
    void asm_cmpxchg(StringWriter& w, StreamItem& si) const;
    void asm_cmpxchg8b(StringWriter& w, StreamItem& si) const;
    void asm_cpuid(StringWriter& w, StreamItem& si) const;
    void asm_crc32(StringWriter& w, StreamItem& si) const;
    void asm_cs(StringWriter& w, StreamItem& si) const;
    void asm_cwd_cdq(StringWriter& w, StreamItem& si) const;
    void asm_daa(StringWriter& w, StreamItem& si) const;
    void asm_das(StringWriter& w, StreamItem& si) const;
    void asm_inc_dec(StringWriter& w, StreamItem& si) const;
    void asm_div_idiv(StringWriter& w, StreamItem& si) const;
    void asm_ds(StringWriter& w, StreamItem& si) const;
    void asm_enter(StringWriter& w, StreamItem& si) const;
    void asm_es(StringWriter& w, StreamItem& si) const;
    void asm_fs(StringWriter& w, StreamItem& si) const;
    void asm_gs(StringWriter& w, StreamItem& si) const;
    void asm_hlt(StringWriter& w, StreamItem& si) const;
    void asm_imul_mul(StringWriter& w, StreamItem& si) const;
    void asm_in_out(StringWriter& w, StreamItem& si) const;
    void asm_int(StringWriter& w, StreamItem& si) const;
    void asm_iret(StringWriter& w, StreamItem& si) const;
    void asm_j_mnemonics(StringWriter& w, StreamItem& si) const;
    void asm_jcxz_jecxz_loop_mnemonics(StringWriter& w, StreamItem& si) const;
    void asm_lahf_sahf(StringWriter& w, StreamItem& si) const;
    void asm_lea(StringWriter& w, StreamItem& si) const;
    void asm_leave(StringWriter& w, StreamItem& si) const;
    void asm_lock(StringWriter& w, StreamItem& si) const;
    void asm_mov(StringWriter& w, StreamItem& si) const;
    void asm_movbe(StringWriter& w, StreamItem& si) const;
    void asm_movsx_movzx(StringWriter& w, StreamItem& si) const;
    void asm_neg_not(StringWriter& w, StreamItem& si) const;
    void asm_nop(StringWriter& w, StreamItem& si) const;
    void asm_pop_push(StringWriter& w, StreamItem& si) const;
    void asm_popa_popad(StringWriter& w, StreamItem& si) const;
    void asm_popcnt(StringWriter& w, StreamItem& si) const;
    void asm_popf_popfd(StringWriter& w, StreamItem& si) const;
    void asm_pusha_pushad(StringWriter& w, StreamItem& si) const;
    void asm_pushf_pushfd(StringWriter& w, StreamItem& si) const;
    void asm_rol_ror_rcl_rcr_shl_sal_shr_sar(StringWriter& w, StreamItem& si) const;
    void asm_rdtsc(StringWriter& w, StreamItem& si) const;
    void asm_rep_mnemomics(StringWriter& w, StreamItem& si) const;
    void asm_ret(StringWriter& w, StreamItem& si) const;
    void asm_salc_setalc(StringWriter& w, StreamItem& si) const;
    void asm_set_mnemonics(StringWriter& w, StreamItem& si) const;
    void asm_shld_shrd(StringWriter& w, StreamItem& si) const;
    void asm_ss(StringWriter& w, StreamItem& si) const;
    void asm_stc(StringWriter& w, StreamItem& si) const;
    void asm_std(StringWriter& w, StreamItem& si) const;
    void asm_sti(StringWriter& w, StreamItem& si) const;
    void asm_test(StringWriter& w, StreamItem& si) const;
    void asm_xadd(StringWriter& w, StreamItem& si) const;
    void asm_xchg(StringWriter& w, StreamItem& si) const;

    void asm_fxsave_fxrstor(StringWriter& w, StreamItem& si) const;
    void asm_fsave_fnsave_frstor(StringWriter& w, StreamItem& si) const;
    void asm_fstenv_fnstenv_fldenv(StringWriter& w, StreamItem& si) const;
    void asm_fstcw_fnstcw_fldcw(StringWriter& w, StreamItem& si) const;
    void asm_fstsw_fnstsw(StringWriter& w, StreamItem& si) const;
    void asm_fwait(StringWriter& w, StreamItem& si) const;
    void asm_fclex_fnclex(StringWriter& w, StreamItem& si) const;
    void asm_finit_fninit(StringWriter& w, StreamItem& si) const;
    void asm_fadd(StringWriter& w, StreamItem& si) const;
    void asm_faddp(StringWriter& w, StreamItem& si) const;
    void asm_fmul(StringWriter& w, StreamItem& si) const;
    void asm_fmulp(StringWriter& w, StreamItem& si) const;
    void asm_fcom_fcomp(StringWriter& w, StreamItem& si) const;
    void asm_fcomi_fcomip(StringWriter& w, StreamItem& si) const;
    void asm_fcompp(StringWriter& w, StreamItem& si) const;
    void asm_fsub_fsubr(StringWriter& w, StreamItem& si) const;
    void asm_fsubp_fsubrp(StringWriter& w, StreamItem& si) const;
    void asm_fdiv_fdivr(StringWriter& w, StreamItem& si) const;
    void asm_fdivp_fdivrp(StringWriter& w, StreamItem& si) const;
    void asm_fld(StringWriter& w, StreamItem& si) const;
    void asm_fld1(StringWriter& w, StreamItem& si) const;
    void asm_fldl2t(StringWriter& w, StreamItem& si) const;
    void asm_fldl2e(StringWriter& w, StreamItem& si) const;
    void asm_fldpi(StringWriter& w, StreamItem& si) const;
    void asm_fldlg2(StringWriter& w, StreamItem& si) const;
    void asm_fldln2(StringWriter& w, StreamItem& si) const;
    void asm_fldz(StringWriter& w, StreamItem& si) const;
    void asm_fxch(StringWriter& w, StreamItem& si) const;
    void asm_fst_fstp(StringWriter& w, StreamItem& si) const;
    void asm_fnop(StringWriter& w, StreamItem& si) const;
    void asm_fchs(StringWriter& w, StreamItem& si) const;
    void asm_fabs(StringWriter& w, StreamItem& si) const;
    void asm_ftst(StringWriter& w, StreamItem& si) const;
    void asm_fxam(StringWriter& w, StreamItem& si) const;
    void asm_f2xm1(StringWriter& w, StreamItem& si) const;
    void asm_fyl2x(StringWriter& w, StreamItem& si) const;
    void asm_fptan(StringWriter& w, StreamItem& si) const;
    void asm_fpatan(StringWriter& w, StreamItem& si) const;
    void asm_fxtract(StringWriter& w, StreamItem& si) const;
    void asm_fprem1(StringWriter& w, StreamItem& si) const;
    void asm_fdecstp(StringWriter& w, StreamItem& si) const;
    void asm_fincstp(StringWriter& w, StreamItem& si) const;
    void asm_fprem(StringWriter& w, StreamItem& si) const;
    void asm_fyl2xp1(StringWriter& w, StreamItem& si) const;
    void asm_fsqrt(StringWriter& w, StreamItem& si) const;
    void asm_fsincos(StringWriter& w, StreamItem& si) const;
    void asm_frndint(StringWriter& w, StreamItem& si) const;
    void asm_fscale(StringWriter& w, StreamItem& si) const;
    void asm_fsin(StringWriter& w, StreamItem& si) const;
    void asm_fcos(StringWriter& w, StreamItem& si) const;
    void asm_fcmov_mnemonics(StringWriter& w, StreamItem& si) const;
    void asm_fiadd_fimul_ficom_ficomp_fisub_fisubr_fidiv_fidivr(StringWriter& w, StreamItem& si) const;
    void asm_fucompp(StringWriter& w, StreamItem& si) const;
    void asm_fild(StringWriter& w, StreamItem& si) const;
    void asm_fist_fistp_fisttp(StringWriter& w, StreamItem& si) const;
    void asm_fneni(StringWriter& w, StreamItem& si) const;
    void asm_fndisi(StringWriter& w, StreamItem& si) const;
    void asm_fnsetpm(StringWriter& w, StreamItem& si) const;
    void asm_ffree_ffreep(StringWriter& w, StreamItem& si) const;
    void asm_fucom_fucomi_fucomp_fucomip(StringWriter& w, StreamItem& si) const;
    void asm_fbld(StringWriter& w, StreamItem& si) const;
    void asm_fbstp(StringWriter& w, StreamItem& si) const;

    void asm_dir_offsetof(StringWriter& w, StreamItem& si) const;
    void asm_dir_addressof(StringWriter& w, StreamItem& si) const;
    void asm_dir_deltaof(StringWriter& w, StreamItem& si) const;
  };
};

} // namespace ResourceDASM
