#pragma once

#include <stdint.h>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include <set>
#include <phosg/Strings.hh>

#include "EmulatorBase.hh"
#include "MemoryContext.hh"
#include "InterruptManager.hh"


struct PPC32CR {
  uint32_t u;

  inline void replace_field(uint8_t index, uint8_t value) {
    uint8_t shift = (28 - (4 * index));
    this->u = (this->u & ~(0xF << shift)) | ((value & 0xF) << shift);
  }

  inline bool get_lt(uint8_t index) {
    return (this->u >> (28 - (index << 2) + 3)) & 1;
  }
  inline bool get_gt(uint8_t index) {
    return (this->u >> (28 - (index << 2) + 2)) & 1;
  }
  inline bool get_z(uint8_t index) {
    return (this->u >> (28 - (index << 2) + 1)) & 1;
  }
  inline bool get_so(uint8_t index) {
    return (this->u >> (28 - (index << 2) + 0)) & 1;
  }
};

struct PPC32XER {
  uint32_t u;

  inline void replace_field(uint8_t index, uint8_t value) {
    this->u = (this->u & ~(0xF << (7 - index))) | (value << (7 - index));
  }

  inline bool get_so() {
    return (this->u >> 31) & 1;
  }
  inline bool get_ov() {
    return (this->u >> 30) & 1;
  }
  inline bool get_ca() {
    return (this->u >> 29) & 1;
  }
  inline uint8_t get_byte_count() {
    return this->u & 0xFF;
  }
};

struct PPC32Registers {
  union {
    uint32_t u;
    int32_t s;
  } r[32];
  union {
    uint64_t i;
    double f;
  } f[32];
  PPC32CR cr;
  uint32_t fpscr;
  PPC32XER xer;
  uint32_t lr;
  uint32_t ctr;
  uint64_t tbr;
  uint64_t tbr_ticks_per_cycle;
  uint32_t pc;

  struct {
    uint32_t addr;
  } debug;

  PPC32Registers();

  void set_by_name(const std::string& reg_name, uint32_t value);

  static void print_header(FILE* stream);
  void print(FILE* stream) const;
};

class PPC32Emulator : public EmulatorBase {
public:
  explicit PPC32Emulator(std::shared_ptr<MemoryContext> mem);
  virtual ~PPC32Emulator() = default;

  inline PPC32Registers& registers() {
    return this->regs;
  }

  inline void set_syscall_handler(std::function<void(PPC32Emulator&)> handler) {
    this->syscall_handler = handler;
  }

  inline void set_debug_hook(std::function<void(PPC32Emulator&)> hook) {
    this->debug_hook = hook;
  }

  inline void set_interrupt_manager(std::shared_ptr<InterruptManager> im) {
    this->interrupt_manager = im;
  }

  virtual void print_source_trace(FILE* stream, const std::string& what, size_t max_depth = 0) const;

  virtual void execute();

  static std::string disassemble_one(uint32_t pc, uint32_t op);
  static std::string disassemble(
      const void* data,
      size_t size,
      uint32_t pc = 0,
      const std::multimap<uint32_t, std::string>* labels = nullptr);

  struct AssembleResult {
    std::string code;
    std::unordered_map<std::string, uint32_t> label_offsets;
  };
  static AssembleResult assemble(const std::string& text);

  virtual void import_state(FILE* stream);
  virtual void export_state(FILE* stream) const;

  virtual void print_state_header(FILE* stream);
  virtual void print_state(FILE* stream);

private:
  PPC32Registers regs;
  std::function<void(PPC32Emulator&)> syscall_handler;
  std::function<void(PPC32Emulator&)> debug_hook;
  std::shared_ptr<InterruptManager> interrupt_manager;

  struct DisassemblerState {
    uint32_t pc;
    const std::multimap<uint32_t, std::string>* labels;
    std::map<uint32_t, bool> branch_target_addresses;
  };

  struct OpcodeImplementation {
    void (PPC32Emulator::*exec)(uint32_t);
    std::string (*dasm)(DisassemblerState&, uint32_t);
  };
  static const OpcodeImplementation fns[0x40];

  static std::string disassemble_one(DisassemblerState& s, uint32_t op);

  bool should_branch(uint32_t op);
  void set_cr_bits_int(uint8_t crf, int32_t value);

  void exec_unimplemented(uint32_t op);
  static std::string dasm_unimplemented(DisassemblerState& s, uint32_t op);
  void exec_invalid(uint32_t op);
  static std::string dasm_invalid(DisassemblerState& s, uint32_t op);
  void exec_0C_twi(uint32_t op);
  static std::string dasm_0C_twi(DisassemblerState& s, uint32_t op);
  void exec_1C_mulli(uint32_t op);
  static std::string dasm_1C_mulli(DisassemblerState& s, uint32_t op);
  void exec_20_subfic(uint32_t op);
  static std::string dasm_20_subfic(DisassemblerState& s, uint32_t op);
  void exec_28_cmpli(uint32_t op);
  static std::string dasm_28_cmpli(DisassemblerState& s, uint32_t op);
  void exec_2C_cmpi(uint32_t op);
  static std::string dasm_2C_cmpi(DisassemblerState& s, uint32_t op);
  void exec_30_34_addic(uint32_t op);
  static std::string dasm_30_34_addic(DisassemblerState& s, uint32_t op);
  void exec_38_addi(uint32_t op);
  static std::string dasm_38_addi(DisassemblerState& s, uint32_t op);
  void exec_3C_addis(uint32_t op);
  static std::string dasm_3C_addis(DisassemblerState& s, uint32_t op);
  void exec_40_bc(uint32_t op);
  static std::string dasm_40_bc(DisassemblerState& s, uint32_t op);
  void exec_44_sc(uint32_t op);
  static std::string dasm_44_sc(DisassemblerState& s, uint32_t op);
  void exec_48_b(uint32_t op);
  static std::string dasm_48_b(DisassemblerState& s, uint32_t op);
  void exec_4C(uint32_t op);
  static std::string dasm_4C(DisassemblerState& s, uint32_t op);
  void exec_4C_000_mcrf(uint32_t op);
  static std::string dasm_4C_000_mcrf(DisassemblerState& s, uint32_t op);
  void exec_4C_010_bclr(uint32_t op);
  static std::string dasm_4C_010_bclr(DisassemblerState& s, uint32_t op);
  void exec_4C_021_crnor(uint32_t op);
  static std::string dasm_4C_021_crnor(DisassemblerState& s, uint32_t op);
  void exec_4C_032_rfi(uint32_t op);
  static std::string dasm_4C_032_rfi(DisassemblerState& s, uint32_t op);
  void exec_4C_081_crandc(uint32_t op);
  static std::string dasm_4C_081_crandc(DisassemblerState& s, uint32_t op);
  void exec_4C_096_isync(uint32_t op);
  static std::string dasm_4C_096_isync(DisassemblerState& s, uint32_t op);
  void exec_4C_0C1_crxor(uint32_t op);
  static std::string dasm_4C_0C1_crxor(DisassemblerState& s, uint32_t op);
  void exec_4C_0E1_crnand(uint32_t op);
  static std::string dasm_4C_0E1_crnand(DisassemblerState& s, uint32_t op);
  void exec_4C_101_crand(uint32_t op);
  static std::string dasm_4C_101_crand(DisassemblerState& s, uint32_t op);
  void exec_4C_121_creqv(uint32_t op);
  static std::string dasm_4C_121_creqv(DisassemblerState& s, uint32_t op);
  void exec_4C_1A1_crorc(uint32_t op);
  static std::string dasm_4C_1A1_crorc(DisassemblerState& s, uint32_t op);
  void exec_4C_1C1_cror(uint32_t op);
  static std::string dasm_4C_1C1_cror(DisassemblerState& s, uint32_t op);
  void exec_4C_210_bcctr(uint32_t op);
  static std::string dasm_4C_210_bcctr(DisassemblerState& s, uint32_t op);
  void exec_50_rlwimi(uint32_t op);
  static std::string dasm_50_rlwimi(DisassemblerState& s, uint32_t op);
  void exec_54_rlwinm(uint32_t op);
  static std::string dasm_54_rlwinm(DisassemblerState& s, uint32_t op);
  void exec_5C_rlwnm(uint32_t op);
  static std::string dasm_5C_rlwnm(DisassemblerState& s, uint32_t op);
  void exec_60_ori(uint32_t op);
  static std::string dasm_60_ori(DisassemblerState& s, uint32_t op);
  void exec_64_oris(uint32_t op);
  static std::string dasm_64_oris(DisassemblerState& s, uint32_t op);
  void exec_68_xori(uint32_t op);
  static std::string dasm_68_xori(DisassemblerState& s, uint32_t op);
  void exec_6C_xoris(uint32_t op);
  static std::string dasm_6C_xoris(DisassemblerState& s, uint32_t op);
  void exec_70_andi_rec(uint32_t op);
  static std::string dasm_70_andi_rec(DisassemblerState& s, uint32_t op);
  void exec_74_andis_rec(uint32_t op);
  static std::string dasm_74_andis_rec(DisassemblerState& s, uint32_t op);
  void exec_7C(uint32_t op);
  static std::string dasm_7C(DisassemblerState& s, uint32_t op);
  static std::string dasm_7C_a_b(uint32_t op, const char* base_name);
  static std::string dasm_7C_d_a_b(uint32_t op, const char* base_name);
  static std::string dasm_7C_d_a_b_r(uint32_t op, const char* base_name);
  static std::string dasm_7C_s_a_b(uint32_t op, const char* base_name);
  static std::string dasm_7C_s_a_r(uint32_t op, const char* base_name);
  static std::string dasm_7C_s_a_b_r(uint32_t op, const char* base_name);
  static std::string dasm_7C_d_a_o_r(uint32_t op, const char* base_name);
  static std::string dasm_7C_d_a_b_o_r(uint32_t op, const char* base_name);
  void exec_7C_000_cmp(uint32_t op);
  static std::string dasm_7C_000_cmp(DisassemblerState& s, uint32_t op);
  void exec_7C_004_tw(uint32_t op);
  static std::string dasm_7C_004_tw(DisassemblerState& s, uint32_t op);
  void exec_7C_008_208_subfc(uint32_t op);
  static std::string dasm_7C_008_208_subfc(DisassemblerState& s, uint32_t op);
  void exec_7C_00A_20A_addc(uint32_t op);
  static std::string dasm_7C_00A_20A_addc(DisassemblerState& s, uint32_t op);
  void exec_7C_00B_mulhwu(uint32_t op);
  static std::string dasm_7C_00B_mulhwu(DisassemblerState& s, uint32_t op);
  void exec_7C_013_mfcr(uint32_t op);
  static std::string dasm_7C_013_mfcr(DisassemblerState& s, uint32_t op);
  void exec_7C_014_lwarx(uint32_t op);
  static std::string dasm_7C_014_lwarx(DisassemblerState& s, uint32_t op);
  void exec_7C_017_lwzx(uint32_t op);
  static std::string dasm_7C_017_lwzx(DisassemblerState& s, uint32_t op);
  void exec_7C_018_slw(uint32_t op);
  static std::string dasm_7C_018_slw(DisassemblerState& s, uint32_t op);
  void exec_7C_01A_cntlzw(uint32_t op);
  static std::string dasm_7C_01A_cntlzw(DisassemblerState& s, uint32_t op);
  void exec_7C_01C_and(uint32_t op);
  static std::string dasm_7C_01C_and(DisassemblerState& s, uint32_t op);
  void exec_7C_020_cmpl(uint32_t op);
  static std::string dasm_7C_020_cmpl(DisassemblerState& s, uint32_t op);
  void exec_7C_028_228_subf(uint32_t op);
  static std::string dasm_7C_028_228_subf(DisassemblerState& s, uint32_t op);
  void exec_7C_036_dcbst(uint32_t op);
  static std::string dasm_7C_036_dcbst(DisassemblerState& s, uint32_t op);
  void exec_7C_037_lwzux(uint32_t op);
  static std::string dasm_7C_037_lwzux(DisassemblerState& s, uint32_t op);
  void exec_7C_03C_andc(uint32_t op);
  static std::string dasm_7C_03C_andc(DisassemblerState& s, uint32_t op);
  void exec_7C_04B_mulhw(uint32_t op);
  static std::string dasm_7C_04B_mulhw(DisassemblerState& s, uint32_t op);
  void exec_7C_053_mfmsr(uint32_t op);
  static std::string dasm_7C_053_mfmsr(DisassemblerState& s, uint32_t op);
  void exec_7C_056_dcbf(uint32_t op);
  static std::string dasm_7C_056_dcbf(DisassemblerState& s, uint32_t op);
  void exec_7C_057_lbzx(uint32_t op);
  static std::string dasm_7C_057_lbzx(DisassemblerState& s, uint32_t op);
  void exec_7C_068_268_neg(uint32_t op);
  static std::string dasm_7C_068_268_neg(DisassemblerState& s, uint32_t op);
  void exec_7C_077_lbzux(uint32_t op);
  static std::string dasm_7C_077_lbzux(DisassemblerState& s, uint32_t op);
  void exec_7C_07C_nor(uint32_t op);
  static std::string dasm_7C_07C_nor(DisassemblerState& s, uint32_t op);
  void exec_7C_088_288_subfe(uint32_t op);
  static std::string dasm_7C_088_288_subfe(DisassemblerState& s, uint32_t op);
  void exec_7C_08A_28A_adde(uint32_t op);
  static std::string dasm_7C_08A_28A_adde(DisassemblerState& s, uint32_t op);
  void exec_7C_090_mtcrf(uint32_t op);
  static std::string dasm_7C_090_mtcrf(DisassemblerState& s, uint32_t op);
  void exec_7C_092_mtmsr(uint32_t op);
  static std::string dasm_7C_092_mtmsr(DisassemblerState& s, uint32_t op);
  void exec_7C_096_stwcx_rec(uint32_t op);
  static std::string dasm_7C_096_stwcx_rec(DisassemblerState& s, uint32_t op);
  void exec_7C_097_stwx(uint32_t op);
  static std::string dasm_7C_097_stwx(DisassemblerState& s, uint32_t op);
  void exec_7C_0B7_stwux(uint32_t op);
  static std::string dasm_7C_0B7_stwux(DisassemblerState& s, uint32_t op);
  void exec_7C_0C8_2C8_subfze(uint32_t op);
  static std::string dasm_7C_0C8_2C8_subfze(DisassemblerState& s, uint32_t op);
  void exec_7C_0CA_2CA_addze(uint32_t op);
  static std::string dasm_7C_0CA_2CA_addze(DisassemblerState& s, uint32_t op);
  void exec_7C_0D2_mtsr(uint32_t op);
  static std::string dasm_7C_0D2_mtsr(DisassemblerState& s, uint32_t op);
  void exec_7C_0D7_stbx(uint32_t op);
  static std::string dasm_7C_0D7_stbx(DisassemblerState& s, uint32_t op);
  void exec_7C_0E8_2E8_subfme(uint32_t op);
  static std::string dasm_7C_0E8_2E8_subfme(DisassemblerState& s, uint32_t op);
  void exec_7C_0EA_2EA_addme(uint32_t op);
  static std::string dasm_7C_0EA_2EA_addme(DisassemblerState& s, uint32_t op);
  void exec_7C_0EB_2EB_mullw(uint32_t op);
  static std::string dasm_7C_0EB_2EB_mullw(DisassemblerState& s, uint32_t op);
  void exec_7C_0F2_mtsrin(uint32_t op);
  static std::string dasm_7C_0F2_mtsrin(DisassemblerState& s, uint32_t op);
  void exec_7C_0F6_dcbtst(uint32_t op);
  static std::string dasm_7C_0F6_dcbtst(DisassemblerState& s, uint32_t op);
  void exec_7C_0F7_stbux(uint32_t op);
  static std::string dasm_7C_0F7_stbux(DisassemblerState& s, uint32_t op);
  void exec_7C_10A_30A_add(uint32_t op);
  static std::string dasm_7C_10A_30A_add(DisassemblerState& s, uint32_t op);
  void exec_7C_116_dcbt(uint32_t op);
  static std::string dasm_7C_116_dcbt(DisassemblerState& s, uint32_t op);
  void exec_7C_117_lhzx(uint32_t op);
  static std::string dasm_7C_117_lhzx(DisassemblerState& s, uint32_t op);
  void exec_7C_11C_eqv(uint32_t op);
  static std::string dasm_7C_11C_eqv(DisassemblerState& s, uint32_t op);
  void exec_7C_132_tlbie(uint32_t op);
  static std::string dasm_7C_132_tlbie(DisassemblerState& s, uint32_t op);
  void exec_7C_136_eciwx(uint32_t op);
  static std::string dasm_7C_136_eciwx(DisassemblerState& s, uint32_t op);
  void exec_7C_137_lhzux(uint32_t op);
  static std::string dasm_7C_137_lhzux(DisassemblerState& s, uint32_t op);
  void exec_7C_13C_xor(uint32_t op);
  static std::string dasm_7C_13C_xor(DisassemblerState& s, uint32_t op);
  void exec_7C_153_mfspr(uint32_t op);
  static std::string dasm_7C_153_mfspr(DisassemblerState& s, uint32_t op);
  void exec_7C_157_lhax(uint32_t op);
  static std::string dasm_7C_157_lhax(DisassemblerState& s, uint32_t op);
  void exec_7C_172_tlbia(uint32_t op);
  static std::string dasm_7C_172_tlbia(DisassemblerState& s, uint32_t op);
  void exec_7C_173_mftb(uint32_t op);
  static std::string dasm_7C_173_mftb(DisassemblerState& s, uint32_t op);
  void exec_7C_177_lhaux(uint32_t op);
  static std::string dasm_7C_177_lhaux(DisassemblerState& s, uint32_t op);
  void exec_7C_197_sthx(uint32_t op);
  static std::string dasm_7C_197_sthx(DisassemblerState& s, uint32_t op);
  void exec_7C_19C_orc(uint32_t op);
  static std::string dasm_7C_19C_orc(DisassemblerState& s, uint32_t op);
  void exec_7C_1B6_ecowx(uint32_t op);
  static std::string dasm_7C_1B6_ecowx(DisassemblerState& s, uint32_t op);
  void exec_7C_1B7_sthux(uint32_t op);
  static std::string dasm_7C_1B7_sthux(DisassemblerState& s, uint32_t op);
  void exec_7C_1BC_or(uint32_t op);
  static std::string dasm_7C_1BC_or(DisassemblerState& s, uint32_t op);
  void exec_7C_1CB_3CB_divwu(uint32_t op);
  static std::string dasm_7C_1CB_3CB_divwu(DisassemblerState& s, uint32_t op);
  void exec_7C_1D3_mtspr(uint32_t op);
  static std::string dasm_7C_1D3_mtspr(DisassemblerState& s, uint32_t op);
  void exec_7C_1D6_dcbi(uint32_t op);
  static std::string dasm_7C_1D6_dcbi(DisassemblerState& s, uint32_t op);
  void exec_7C_1DC_nand(uint32_t op);
  static std::string dasm_7C_1DC_nand(DisassemblerState& s, uint32_t op);
  void exec_7C_1EB_3EB_divw(uint32_t op);
  static std::string dasm_7C_1EB_3EB_divw(DisassemblerState& s, uint32_t op);
  void exec_7C_200_mcrxr(uint32_t op);
  static std::string dasm_7C_200_mcrxr(DisassemblerState& s, uint32_t op);
  void exec_7C_215_lswx(uint32_t op);
  static std::string dasm_7C_215_lswx(DisassemblerState& s, uint32_t op);
  void exec_7C_216_lwbrx(uint32_t op);
  static std::string dasm_7C_216_lwbrx(DisassemblerState& s, uint32_t op);
  void exec_7C_217_lfsx(uint32_t op);
  static std::string dasm_7C_217_lfsx(DisassemblerState& s, uint32_t op);
  void exec_7C_218_srw(uint32_t op);
  static std::string dasm_7C_218_srw(DisassemblerState& s, uint32_t op);
  void exec_7C_236_tlbsync(uint32_t op);
  static std::string dasm_7C_236_tlbsync(DisassemblerState& s, uint32_t op);
  void exec_7C_237_lfsux(uint32_t op);
  static std::string dasm_7C_237_lfsux(DisassemblerState& s, uint32_t op);
  void exec_7C_253_mfsr(uint32_t op);
  static std::string dasm_7C_253_mfsr(DisassemblerState& s, uint32_t op);
  void exec_7C_255_lswi(uint32_t op);
  static std::string dasm_7C_255_lswi(DisassemblerState& s, uint32_t op);
  void exec_7C_256_sync(uint32_t op);
  static std::string dasm_7C_256_sync(DisassemblerState& s, uint32_t op);
  void exec_7C_257_lfdx(uint32_t op);
  static std::string dasm_7C_257_lfdx(DisassemblerState& s, uint32_t op);
  void exec_7C_277_lfdux(uint32_t op);
  static std::string dasm_7C_277_lfdux(DisassemblerState& s, uint32_t op);
  void exec_7C_293_mfsrin(uint32_t op);
  static std::string dasm_7C_293_mfsrin(DisassemblerState& s, uint32_t op);
  void exec_7C_295_stswx(uint32_t op);
  static std::string dasm_7C_295_stswx(DisassemblerState& s, uint32_t op);
  void exec_7C_296_stwbrx(uint32_t op);
  static std::string dasm_7C_296_stwbrx(DisassemblerState& s, uint32_t op);
  void exec_7C_297_stfsx(uint32_t op);
  static std::string dasm_7C_297_stfsx(DisassemblerState& s, uint32_t op);
  void exec_7C_2B7_stfsux(uint32_t op);
  static std::string dasm_7C_2B7_stfsux(DisassemblerState& s, uint32_t op);
  void exec_7C_2E5_stswi(uint32_t op);
  static std::string dasm_7C_2E5_stswi(DisassemblerState& s, uint32_t op);
  void exec_7C_2E7_stfdx(uint32_t op);
  static std::string dasm_7C_2E7_stfdx(DisassemblerState& s, uint32_t op);
  void exec_7C_2F6_dcba(uint32_t op);
  static std::string dasm_7C_2F6_dcba(DisassemblerState& s, uint32_t op);
  void exec_7C_2F7_stfdux(uint32_t op);
  static std::string dasm_7C_2F7_stfdux(DisassemblerState& s, uint32_t op);
  void exec_7C_316_lhbrx(uint32_t op);
  static std::string dasm_7C_316_lhbrx(DisassemblerState& s, uint32_t op);
  void exec_7C_318_sraw(uint32_t op);
  static std::string dasm_7C_318_sraw(DisassemblerState& s, uint32_t op);
  void exec_7C_338_srawi(uint32_t op);
  static std::string dasm_7C_338_srawi(DisassemblerState& s, uint32_t op);
  void exec_7C_356_eieio(uint32_t op);
  static std::string dasm_7C_356_eieio(DisassemblerState& s, uint32_t op);
  void exec_7C_396_sthbrx(uint32_t op);
  static std::string dasm_7C_396_sthbrx(DisassemblerState& s, uint32_t op);
  void exec_7C_39A_extsh(uint32_t op);
  static std::string dasm_7C_39A_extsh(DisassemblerState& s, uint32_t op);
  void exec_7C_3BA_extsb(uint32_t op);
  static std::string dasm_7C_3BA_extsb(DisassemblerState& s, uint32_t op);
  void exec_7C_3D6_icbi(uint32_t op);
  static std::string dasm_7C_3D6_icbi(DisassemblerState& s, uint32_t op);
  void exec_7C_3D7_stfiwx(uint32_t op);
  static std::string dasm_7C_3D7_stfiwx(DisassemblerState& s, uint32_t op);
  void exec_7C_3F6_dcbz(uint32_t op);
  static std::string dasm_7C_3F6_dcbz(DisassemblerState& s, uint32_t op);
  static std::string dasm_load_store_imm_u(uint32_t op, const char* base_name, bool is_store, bool data_reg_is_f);
  static std::string dasm_load_store_imm(uint32_t op, const char* base_name, bool is_store);
  void exec_80_84_lwz_lwzu(uint32_t op);
  static std::string dasm_80_84_lwz_lwzu(DisassemblerState& s, uint32_t op);
  void exec_88_8C_lbz_lbzu(uint32_t op);
  static std::string dasm_88_8C_lbz_lbzu(DisassemblerState& s, uint32_t op);
  void exec_90_94_stw_stwu(uint32_t op);
  static std::string dasm_90_94_stw_stwu(DisassemblerState& s, uint32_t op);
  void exec_98_9C_stb_stbu(uint32_t op);
  static std::string dasm_98_9C_stb_stbu(DisassemblerState& s, uint32_t op);
  void exec_A0_A4_lhz_lhzu(uint32_t op);
  static std::string dasm_A0_A4_lhz_lhzu(DisassemblerState& s, uint32_t op);
  void exec_A8_AC_lha_lhau(uint32_t op);
  static std::string dasm_A8_AC_lha_lhau(DisassemblerState& s, uint32_t op);
  void exec_B0_B4_sth_sthu(uint32_t op);
  static std::string dasm_B0_B4_sth_sthu(DisassemblerState& s, uint32_t op);
  void exec_B8_lmw(uint32_t op);
  static std::string dasm_B8_lmw(DisassemblerState& s, uint32_t op);
  void exec_BC_stmw(uint32_t op);
  static std::string dasm_BC_stmw(DisassemblerState& s, uint32_t op);
  void exec_C0_C4_lfs_lfsu(uint32_t op);
  static std::string dasm_C0_C4_lfs_lfsu(DisassemblerState& s, uint32_t op);
  void exec_C8_CC_lfd_lfdu(uint32_t op);
  static std::string dasm_C8_CC_lfd_lfdu(DisassemblerState& s, uint32_t op);
  void exec_D0_D4_stfs_stfsu(uint32_t op);
  static std::string dasm_D0_D4_stfs_stfsu(DisassemblerState& s, uint32_t op);
  void exec_D8_DC_stfd_stfdu(uint32_t op);
  static std::string dasm_D8_DC_stfd_stfdu(DisassemblerState& s, uint32_t op);
  void exec_EC(uint32_t op);
  static std::string dasm_EC(DisassemblerState& s, uint32_t op);
  static std::string dasm_EC_FC_d_b_r(uint32_t op, const char* base_name);
  static std::string dasm_EC_FC_d_a_b_r(uint32_t op, const char* base_name);
  static std::string dasm_EC_FC_d_a_c_r(uint32_t op, const char* base_name);
  static std::string dasm_EC_FC_d_a_b_c_r(uint32_t op, const char* base_name);
  void exec_EC_12_fdivs(uint32_t op);
  static std::string dasm_EC_12_fdivs(DisassemblerState& s, uint32_t op);
  void exec_EC_14_fsubs(uint32_t op);
  static std::string dasm_EC_14_fsubs(DisassemblerState& s, uint32_t op);
  void exec_EC_15_fadds(uint32_t op);
  static std::string dasm_EC_15_fadds(DisassemblerState& s, uint32_t op);
  void exec_EC_16_fsqrts(uint32_t op);
  static std::string dasm_EC_16_fsqrts(DisassemblerState& s, uint32_t op);
  void exec_EC_18_fres(uint32_t op);
  static std::string dasm_EC_18_fres(DisassemblerState& s, uint32_t op);
  void exec_EC_19_fmuls(uint32_t op);
  static std::string dasm_EC_19_fmuls(DisassemblerState& s, uint32_t op);
  void exec_EC_1C_fmsubs(uint32_t op);
  static std::string dasm_EC_1C_fmsubs(DisassemblerState& s, uint32_t op);
  void exec_EC_1D_fmadds(uint32_t op);
  static std::string dasm_EC_1D_fmadds(DisassemblerState& s, uint32_t op);
  void exec_EC_1E_fnmsubs(uint32_t op);
  static std::string dasm_EC_1E_fnmsubs(DisassemblerState& s, uint32_t op);
  void exec_EC_1F_fnmadds(uint32_t op);
  static std::string dasm_EC_1F_fnmadds(DisassemblerState& s, uint32_t op);
  void exec_FC(uint32_t op);
  static std::string dasm_FC(DisassemblerState& s, uint32_t op);
  void exec_FC_12_fdiv(uint32_t op);
  static std::string dasm_FC_12_fdiv(DisassemblerState& s, uint32_t op);
  void exec_FC_14_fsub(uint32_t op);
  static std::string dasm_FC_14_fsub(DisassemblerState& s, uint32_t op);
  void exec_FC_15_fadd(uint32_t op);
  static std::string dasm_FC_15_fadd(DisassemblerState& s, uint32_t op);
  void exec_FC_16_fsqrt(uint32_t op);
  static std::string dasm_FC_16_fsqrt(DisassemblerState& s, uint32_t op);
  void exec_FC_17_fsel(uint32_t op);
  static std::string dasm_FC_17_fsel(DisassemblerState& s, uint32_t op);
  void exec_FC_19_fmul(uint32_t op);
  static std::string dasm_FC_19_fmul(DisassemblerState& s, uint32_t op);
  void exec_FC_1A_frsqrte(uint32_t op);
  static std::string dasm_FC_1A_frsqrte(DisassemblerState& s, uint32_t op);
  void exec_FC_1C_fmsub(uint32_t op);
  static std::string dasm_FC_1C_fmsub(DisassemblerState& s, uint32_t op);
  void exec_FC_1D_fmadd(uint32_t op);
  static std::string dasm_FC_1D_fmadd(DisassemblerState& s, uint32_t op);
  void exec_FC_1E_fnmsub(uint32_t op);
  static std::string dasm_FC_1E_fnmsub(DisassemblerState& s, uint32_t op);
  void exec_FC_1F_fnmadd(uint32_t op);
  static std::string dasm_FC_1F_fnmadd(DisassemblerState& s, uint32_t op);
  void exec_FC_000_fcmpu(uint32_t op);
  static std::string dasm_FC_000_fcmpu(DisassemblerState& s, uint32_t op);
  void exec_FC_00C_frsp(uint32_t op);
  static std::string dasm_FC_00C_frsp(DisassemblerState& s, uint32_t op);
  void exec_FC_00E_fctiw(uint32_t op);
  static std::string dasm_FC_00E_fctiw(DisassemblerState& s, uint32_t op);
  void exec_FC_00F_fctiwz(uint32_t op);
  static std::string dasm_FC_00F_fctiwz(DisassemblerState& s, uint32_t op);
  void exec_FC_020_fcmpo(uint32_t op);
  static std::string dasm_FC_020_fcmpo(DisassemblerState& s, uint32_t op);
  void exec_FC_026_mtfsb1(uint32_t op);
  static std::string dasm_FC_026_mtfsb1(DisassemblerState& s, uint32_t op);
  void exec_FC_028_fneg(uint32_t op);
  static std::string dasm_FC_028_fneg(DisassemblerState& s, uint32_t op);
  void exec_FC_040_mcrfs(uint32_t op);
  static std::string dasm_FC_040_mcrfs(DisassemblerState& s, uint32_t op);
  void exec_FC_046_mtfsb0(uint32_t op);
  static std::string dasm_FC_046_mtfsb0(DisassemblerState& s, uint32_t op);
  void exec_FC_048_fmr(uint32_t op);
  static std::string dasm_FC_048_fmr(DisassemblerState& s, uint32_t op);
  void exec_FC_086_mtfsfi(uint32_t op);
  static std::string dasm_FC_086_mtfsfi(DisassemblerState& s, uint32_t op);
  void exec_FC_088_fnabs(uint32_t op);
  static std::string dasm_FC_088_fnabs(DisassemblerState& s, uint32_t op);
  void exec_FC_108_fabs(uint32_t op);
  static std::string dasm_FC_108_fabs(DisassemblerState& s, uint32_t op);
  void exec_FC_247_mffs(uint32_t op);
  static std::string dasm_FC_247_mffs(DisassemblerState& s, uint32_t op);
  void exec_FC_2C7_mtfsf(uint32_t op);
  static std::string dasm_FC_2C7_mtfsf(DisassemblerState& s, uint32_t op);

  struct Assembler {
    struct Argument {
      enum class Type {
        INT_REGISTER = 0, // "r%d" - only reg_num used
        FLOAT_REGISTER, // "f%d" - only reg_num used
        SPECIAL_REGISTER, // "lr", "ctr", etc. or "spr%d" - only reg_num used
        TIME_REGISTER, // "tbr%d" - only reg_num used
        CONDITION_FIELD, // "crf%d" - only reg_num used
        CONDITION_BIT, // "crb%d" - only reg_num used
        IMMEDIATE, // "%d" or "0x%x" - only value used
        MEMORY_REFERENCE, // "[r%d]", "[r%d + %d]", etc. - both fields used
        BRANCH_TARGET, // integer or immediate - value OR label_name used
      };
      Type type;
      uint16_t reg_num;
      uint32_t value;
      std::string label_name;

      Argument(const std::string& text);
    };

    using ArgType = Argument::Type;

    struct StreamItem {
      size_t offset;
      size_t line_num;
      std::string op_name;
      std::vector<Argument> args;

      // TODO: use a (variadic) template for this so we don't have to construct
      // vectors all the time
      const std::vector<Argument>& check_args(const std::vector<ArgType>& types) const;
      inline bool is_rec() const {
        return ends_with(this->op_name, ".");
      }
    };
    std::deque<StreamItem> stream;
    std::unordered_map<std::string, uint32_t> label_offsets;

    typedef uint32_t (Assembler::*AssembleFunction)(const StreamItem& si);
    static const std::unordered_map<std::string, AssembleFunction> assemble_functions;
    StringWriter code;

    void assemble(const std::string& text);

    uint32_t asm_5reg(uint32_t base_opcode, uint8_t r1, uint8_t r2, uint8_t r3,
        uint8_t r4, uint8_t r5, bool rec);

    uint32_t asm_twi(const StreamItem& si);
    uint32_t asm_mulli(const StreamItem& si);
    uint32_t asm_subfic(const StreamItem& si);
    uint32_t asm_cmpli_cmplwi(const StreamItem& si);
    uint32_t asm_cmpi_cmpwi(const StreamItem& si);
    uint32_t asm_addic_subic(const StreamItem& si);
    uint32_t asm_li_lis(const StreamItem& si);
    uint32_t asm_addi_subi_addis_subis(const StreamItem& si);
    uint32_t asm_bc_mnemonic(const StreamItem& si);
    uint32_t asm_sc(const StreamItem& si);
    uint32_t asm_b_mnemonic(const StreamItem& si);
    uint32_t asm_mcrf(const StreamItem& si);
    uint32_t asm_bclr_mnemonic(const StreamItem& si);
    uint32_t asm_crnor(const StreamItem& si);
    uint32_t asm_rfi(const StreamItem& si);
    uint32_t asm_crandc(const StreamItem& si);
    uint32_t asm_isync(const StreamItem& si);
    uint32_t asm_crxor(const StreamItem& si);
    uint32_t asm_crnand(const StreamItem& si);
    uint32_t asm_crand(const StreamItem& si);
    uint32_t asm_creqv(const StreamItem& si);
    uint32_t asm_crorc(const StreamItem& si);
    uint32_t asm_cror(const StreamItem& si);
    uint32_t asm_bcctr_mnemonic(const StreamItem& si);
    uint32_t asm_rlwimi(const StreamItem& si);
    uint32_t asm_inslwi(const StreamItem& si);
    uint32_t asm_insrwi(const StreamItem& si);
    uint32_t asm_rlwinm(const StreamItem& si);
    uint32_t asm_extlwi(const StreamItem& si);
    uint32_t asm_extrwi(const StreamItem& si);
    uint32_t asm_rotlwi(const StreamItem& si);
    uint32_t asm_rotrwi(const StreamItem& si);
    uint32_t asm_slwi(const StreamItem& si);
    uint32_t asm_srwi(const StreamItem& si);
    uint32_t asm_clrlwi(const StreamItem& si);
    uint32_t asm_clrrwi(const StreamItem& si);
    uint32_t asm_clrlslwi(const StreamItem& si);
    uint32_t asm_rlwnm(const StreamItem& si);
    uint32_t asm_rotlw(const StreamItem& si);
    uint32_t asm_nop(const StreamItem& si);
    uint32_t asm_ori(const StreamItem& si);
    uint32_t asm_oris(const StreamItem& si);
    uint32_t asm_xori(const StreamItem& si);
    uint32_t asm_xoris(const StreamItem& si);
    uint32_t asm_andi_rec(const StreamItem& si);
    uint32_t asm_andis_rec(const StreamItem& si);
    uint32_t asm_7C_a_b(const StreamItem& si, uint32_t subopcode);
    uint32_t asm_7C_d_a_b(const StreamItem& si, uint32_t subopcode);
    uint32_t asm_7C_d_a_b_r(const StreamItem& si, uint32_t subopcode);
    uint32_t asm_7C_s_a_b(const StreamItem& si, uint32_t subopcode);
    uint32_t asm_7C_s_a_r(const StreamItem& si, uint32_t subopcode);
    uint32_t asm_7C_s_a_b_r(const StreamItem& si, uint32_t subopcode);
    uint32_t asm_7C_d_a_o_r(const StreamItem& si, uint32_t subopcode);
    uint32_t asm_7C_d_a_b_o_r(const StreamItem& si, uint32_t subopcode);
    uint32_t asm_cmp(const StreamItem& si);
    uint32_t asm_tw(const StreamItem& si);
    uint32_t asm_subfc(const StreamItem& si);
    uint32_t asm_addc(const StreamItem& si);
    uint32_t asm_mulhwu(const StreamItem& si);
    uint32_t asm_mfcr(const StreamItem& si);
    uint32_t asm_lwarx(const StreamItem& si);
    uint32_t asm_lwzx(const StreamItem& si);
    uint32_t asm_slw(const StreamItem& si);
    uint32_t asm_cntlzw(const StreamItem& si);
    uint32_t asm_and(const StreamItem& si);
    uint32_t asm_cmpl(const StreamItem& si);
    uint32_t asm_subf(const StreamItem& si);
    uint32_t asm_sub(const StreamItem& si);
    uint32_t asm_dcbst(const StreamItem& si);
    uint32_t asm_lwzux(const StreamItem& si);
    uint32_t asm_andc(const StreamItem& si);
    uint32_t asm_mulhw(const StreamItem& si);
    uint32_t asm_mfmsr(const StreamItem& si);
    uint32_t asm_dcbf(const StreamItem& si);
    uint32_t asm_lbzx(const StreamItem& si);
    uint32_t asm_neg(const StreamItem& si);
    uint32_t asm_lbzux(const StreamItem& si);
    uint32_t asm_nor(const StreamItem& si);
    uint32_t asm_subfe(const StreamItem& si);
    uint32_t asm_adde(const StreamItem& si);
    uint32_t asm_mtcr_mtcrf(const StreamItem& si);
    uint32_t asm_mtmsr(const StreamItem& si);
    uint32_t asm_stwcx_rec(const StreamItem& si);
    uint32_t asm_stwx(const StreamItem& si);
    uint32_t asm_stwux(const StreamItem& si);
    uint32_t asm_subfze(const StreamItem& si);
    uint32_t asm_addze(const StreamItem& si);
    uint32_t asm_mtsr(const StreamItem& si);
    uint32_t asm_stbx(const StreamItem& si);
    uint32_t asm_subfme(const StreamItem& si);
    uint32_t asm_addme(const StreamItem& si);
    uint32_t asm_mullw(const StreamItem& si);
    uint32_t asm_mtsrin(const StreamItem& si);
    uint32_t asm_dcbtst(const StreamItem& si);
    uint32_t asm_stbux(const StreamItem& si);
    uint32_t asm_add(const StreamItem& si);
    uint32_t asm_dcbt(const StreamItem& si);
    uint32_t asm_lhzx(const StreamItem& si);
    uint32_t asm_eqv(const StreamItem& si);
    uint32_t asm_tlbie(const StreamItem& si);
    uint32_t asm_eciwx(const StreamItem& si);
    uint32_t asm_lhzux(const StreamItem& si);
    uint32_t asm_xor(const StreamItem& si);
    uint32_t asm_mfspr_mnemonic(const StreamItem& si);
    uint32_t asm_lhax(const StreamItem& si);
    uint32_t asm_tlbia(const StreamItem& si);
    uint32_t asm_mftb(const StreamItem& si);
    uint32_t asm_lhaux(const StreamItem& si);
    uint32_t asm_sthx(const StreamItem& si);
    uint32_t asm_orc(const StreamItem& si);
    uint32_t asm_ecowx(const StreamItem& si);
    uint32_t asm_sthux(const StreamItem& si);
    uint32_t asm_or(const StreamItem& si);
    uint32_t asm_divwu(const StreamItem& si);
    uint32_t asm_mtspr_mnemonic(const StreamItem& si);
    uint32_t asm_dcbi(const StreamItem& si);
    uint32_t asm_nand(const StreamItem& si);
    uint32_t asm_divw(const StreamItem& si);
    uint32_t asm_mcrxr(const StreamItem& si);
    uint32_t asm_lswx(const StreamItem& si);
    uint32_t asm_lwbrx(const StreamItem& si);
    uint32_t asm_lfsx(const StreamItem& si);
    uint32_t asm_srw(const StreamItem& si);
    uint32_t asm_lfsux(const StreamItem& si);
    uint32_t asm_mfsr(const StreamItem& si);
    uint32_t asm_lswi(const StreamItem& si);
    uint32_t asm_sync(const StreamItem& si);
    uint32_t asm_lfdx(const StreamItem& si);
    uint32_t asm_lfdux(const StreamItem& si);
    uint32_t asm_mfsrin(const StreamItem& si);
    uint32_t asm_stswx(const StreamItem& si);
    uint32_t asm_stwbrx(const StreamItem& si);
    uint32_t asm_stfsx(const StreamItem& si);
    uint32_t asm_stfsux(const StreamItem& si);
    uint32_t asm_stswi(const StreamItem& si);
    uint32_t asm_stfdx(const StreamItem& si);
    uint32_t asm_dcba(const StreamItem& si);
    uint32_t asm_stfdux(const StreamItem& si);
    uint32_t asm_lhbrx(const StreamItem& si);
    uint32_t asm_sraw(const StreamItem& si);
    uint32_t asm_srawi(const StreamItem& si);
    uint32_t asm_eieio(const StreamItem& si);
    uint32_t asm_sthbrx(const StreamItem& si);
    uint32_t asm_extsh(const StreamItem& si);
    uint32_t asm_extsb(const StreamItem& si);
    uint32_t asm_icbi(const StreamItem& si);
    uint32_t asm_stfiwx(const StreamItem& si);
    uint32_t asm_dcbz(const StreamItem& si);
    uint32_t asm_load_store_imm(const StreamItem& si, uint32_t base_opcode,
        bool is_store, bool is_float);
    uint32_t asm_lwz(const StreamItem& si);
    uint32_t asm_lwzu(const StreamItem& si);
    uint32_t asm_lbz(const StreamItem& si);
    uint32_t asm_lbzu(const StreamItem& si);
    uint32_t asm_stw(const StreamItem& si);
    uint32_t asm_stwu(const StreamItem& si);
    uint32_t asm_stb(const StreamItem& si);
    uint32_t asm_stbu(const StreamItem& si);
    uint32_t asm_lhz(const StreamItem& si);
    uint32_t asm_lhzu(const StreamItem& si);
    uint32_t asm_lha(const StreamItem& si);
    uint32_t asm_lhau(const StreamItem& si);
    uint32_t asm_sth(const StreamItem& si);
    uint32_t asm_sthu(const StreamItem& si);
    uint32_t asm_lmw(const StreamItem& si);
    uint32_t asm_stmw(const StreamItem& si);
    uint32_t asm_lfs(const StreamItem& si);
    uint32_t asm_lfsu(const StreamItem& si);
    uint32_t asm_lfd(const StreamItem& si);
    uint32_t asm_lfdu(const StreamItem& si);
    uint32_t asm_stfs(const StreamItem& si);
    uint32_t asm_stfsu(const StreamItem& si);
    uint32_t asm_stfd(const StreamItem& si);
    uint32_t asm_stfdu(const StreamItem& si);
    uint32_t asm_fdivs(const StreamItem& si);
    uint32_t asm_fsubs(const StreamItem& si);
    uint32_t asm_fadds(const StreamItem& si);
    uint32_t asm_fsqrts(const StreamItem& si);
    uint32_t asm_fres(const StreamItem& si);
    uint32_t asm_fmuls(const StreamItem& si);
    uint32_t asm_fmsubs(const StreamItem& si);
    uint32_t asm_fmadds(const StreamItem& si);
    uint32_t asm_fnmsubs(const StreamItem& si);
    uint32_t asm_fnmadds(const StreamItem& si);
    uint32_t asm_fdiv(const StreamItem& si);
    uint32_t asm_fsub(const StreamItem& si);
    uint32_t asm_fadd(const StreamItem& si);
    uint32_t asm_fsqrt(const StreamItem& si);
    uint32_t asm_fsel(const StreamItem& si);
    uint32_t asm_fmul(const StreamItem& si);
    uint32_t asm_frsqrte(const StreamItem& si);
    uint32_t asm_fmsub(const StreamItem& si);
    uint32_t asm_fmadd(const StreamItem& si);
    uint32_t asm_fnmsub(const StreamItem& si);
    uint32_t asm_fnmadd(const StreamItem& si);
    uint32_t asm_fcmpu(const StreamItem& si);
    uint32_t asm_frsp(const StreamItem& si);
    uint32_t asm_fctiw(const StreamItem& si);
    uint32_t asm_fctiwz(const StreamItem& si);
    uint32_t asm_fcmpo(const StreamItem& si);
    uint32_t asm_mtfsb1(const StreamItem& si);
    uint32_t asm_fneg(const StreamItem& si);
    uint32_t asm_mcrfs(const StreamItem& si);
    uint32_t asm_mtfsbb(const StreamItem& si);
    uint32_t asm_fmr(const StreamItem& si);
    uint32_t asm_mtfsfi(const StreamItem& si);
    uint32_t asm_fnabs(const StreamItem& si);
    uint32_t asm_fabs(const StreamItem& si);
    uint32_t asm_mffs(const StreamItem& si);
    uint32_t asm_mtfsf(const StreamItem& si);
    uint32_t asm_data(const StreamItem& si);
    uint32_t asm_offsetof(const StreamItem& si);
  };
};
