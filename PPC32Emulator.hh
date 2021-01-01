#pragma once

#include <stdint.h>

#include <functional>
#include <memory>
#include <vector>
#include <set>

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

  void print(FILE* stream) const;
};

class PPC32Emulator {
public:
  PPC32Emulator(std::shared_ptr<MemoryContext> mem);
  ~PPC32Emulator() = default;

  std::shared_ptr<MemoryContext> memory();

  void set_syscall_handler(
      std::function<bool(PPC32Emulator&, PPC32Registers&)> handler);
  void set_debug_hook(
      std::function<bool(PPC32Emulator&, PPC32Registers&)> hook);
  void set_interrupt_manager(std::shared_ptr<InterruptManager> im);

  void execute(const PPC32Registers& regs);
  static std::string disassemble(const void* data, size_t size, uint32_t pc = 0);
  static std::string disassemble(uint32_t pc, uint32_t opcode, std::set<uint32_t>& labels);
  static std::string disassemble(uint32_t pc, uint32_t opcode);

private:
  bool should_exit;
  PPC32Registers regs;
  std::shared_ptr<MemoryContext> mem;
  std::function<bool(PPC32Emulator&, PPC32Registers&)> syscall_handler;
  std::function<bool(PPC32Emulator&, PPC32Registers&)> debug_hook;
  std::shared_ptr<InterruptManager> interrupt_manager;

  void (PPC32Emulator::*exec_fns[0x40])(uint32_t);
  static std::string (*dasm_fns[0x40])(uint32_t, uint32_t, std::set<uint32_t>&);

  bool should_branch(uint32_t op);
  void set_cr_bits_int(uint8_t crf, int32_t value);

  void exec_unimplemented(uint32_t op);
  static std::string dasm_unimplemented(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_invalid(uint32_t op);
  static std::string dasm_invalid(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_0C_twi(uint32_t op);
  static std::string dasm_0C_twi(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_1C_mulli(uint32_t op);
  static std::string dasm_1C_mulli(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_20_subfic(uint32_t op);
  static std::string dasm_20_subfic(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_28_cmpli(uint32_t op);
  static std::string dasm_28_cmpli(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_2C_cmpi(uint32_t op);
  static std::string dasm_2C_cmpi(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_30_34_addic(uint32_t op);
  static std::string dasm_30_34_addic(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_38_addi(uint32_t op);
  static std::string dasm_38_addi(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_3C_addis(uint32_t op);
  static std::string dasm_3C_addis(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_40_bc(uint32_t op);
  static std::string dasm_40_bc(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_44_sc(uint32_t op);
  static std::string dasm_44_sc(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_48_b(uint32_t op);
  static std::string dasm_48_b(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C(uint32_t op);
  static std::string dasm_4C(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C_000_mcrf(uint32_t op);
  static std::string dasm_4C_000_mcrf(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C_010_bclr(uint32_t op);
  static std::string dasm_4C_010_bclr(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C_021_crnor(uint32_t op);
  static std::string dasm_4C_021_crnor(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C_031_rfi(uint32_t op);
  static std::string dasm_4C_031_rfi(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C_081_crandc(uint32_t op);
  static std::string dasm_4C_081_crandc(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C_096_isync(uint32_t op);
  static std::string dasm_4C_096_isync(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C_0C1_crxor(uint32_t op);
  static std::string dasm_4C_0C1_crxor(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C_0E1_crnand(uint32_t op);
  static std::string dasm_4C_0E1_crnand(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C_101_crand(uint32_t op);
  static std::string dasm_4C_101_crand(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C_121_creqv(uint32_t op);
  static std::string dasm_4C_121_creqv(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C_1A1_crorc(uint32_t op);
  static std::string dasm_4C_1A1_crorc(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C_1C1_cror(uint32_t op);
  static std::string dasm_4C_1C1_cror(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_4C_210_bcctr(uint32_t op);
  static std::string dasm_4C_210_bcctr(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_50_rlwimi(uint32_t op);
  static std::string dasm_50_rlwimi(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_54_rlwinm(uint32_t op);
  static std::string dasm_54_rlwinm(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_5C_rlwnm(uint32_t op);
  static std::string dasm_5C_rlwnm(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_60_ori(uint32_t op);
  static std::string dasm_60_ori(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_64_oris(uint32_t op);
  static std::string dasm_64_oris(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_68_xori(uint32_t op);
  static std::string dasm_68_xori(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_6C_xoris(uint32_t op);
  static std::string dasm_6C_xoris(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_70_andi_rec(uint32_t op);
  static std::string dasm_70_andi_rec(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_74_andis_rec(uint32_t op);
  static std::string dasm_74_andis_rec(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C(uint32_t op);
  static std::string dasm_7C(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  static std::string dasm_7C_a_b(uint32_t op, const char* base_name);
  static std::string dasm_7C_d_a_b(uint32_t op, const char* base_name);
  static std::string dasm_7C_d_a_b_r(uint32_t op, const char* base_name);
  static std::string dasm_7C_s_a_b(uint32_t op, const char* base_name);
  static std::string dasm_7C_s_a_r(uint32_t op, const char* base_name);
  static std::string dasm_7C_s_a_b_r(uint32_t op, const char* base_name);
  static std::string dasm_7C_d_a_o_r(uint32_t op, const char* base_name);
  static std::string dasm_7C_d_a_b_o_r(uint32_t op, const char* base_name);
  void exec_7C_000_cmp(uint32_t op);
  static std::string dasm_7C_000_cmp(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_004_tw(uint32_t op);
  static std::string dasm_7C_004_tw(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_008_208_subfc(uint32_t op);
  static std::string dasm_7C_008_208_subfc(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_00A_20A_addc(uint32_t op);
  static std::string dasm_7C_00A_20A_addc(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_00B_mulhwu(uint32_t op);
  static std::string dasm_7C_00B_mulhwu(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_013_mfcr(uint32_t op);
  static std::string dasm_7C_013_mfcr(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_014_lwarx(uint32_t op);
  static std::string dasm_7C_014_lwarx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_017_lwzx(uint32_t op);
  static std::string dasm_7C_017_lwzx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_018_slw(uint32_t op);
  static std::string dasm_7C_018_slw(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_01A_cntlzw(uint32_t op);
  static std::string dasm_7C_01A_cntlzw(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_01C_and(uint32_t op);
  static std::string dasm_7C_01C_and(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_020_cmpl(uint32_t op);
  static std::string dasm_7C_020_cmpl(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_028_228_subf(uint32_t op);
  static std::string dasm_7C_028_228_subf(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_036_dcbst(uint32_t op);
  static std::string dasm_7C_036_dcbst(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_037_lwzux(uint32_t op);
  static std::string dasm_7C_037_lwzux(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_03C_andc(uint32_t op);
  static std::string dasm_7C_03C_andc(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_04B_mulhw(uint32_t op);
  static std::string dasm_7C_04B_mulhw(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_053_mfmsr(uint32_t op);
  static std::string dasm_7C_053_mfmsr(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_056_dcbf(uint32_t op);
  static std::string dasm_7C_056_dcbf(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_057_lbzx(uint32_t op);
  static std::string dasm_7C_057_lbzx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_058_258_neg(uint32_t op);
  static std::string dasm_7C_058_258_neg(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_077_lbzux(uint32_t op);
  static std::string dasm_7C_077_lbzux(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_07C_nor(uint32_t op);
  static std::string dasm_7C_07C_nor(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_088_288_subfe(uint32_t op);
  static std::string dasm_7C_088_288_subfe(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_08A_28A_adde(uint32_t op);
  static std::string dasm_7C_08A_28A_adde(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_090_mtcrf(uint32_t op);
  static std::string dasm_7C_090_mtcrf(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_092_mtmsr(uint32_t op);
  static std::string dasm_7C_092_mtmsr(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_096_stwcx_rec(uint32_t op);
  static std::string dasm_7C_096_stwcx_rec(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_097_stwx(uint32_t op);
  static std::string dasm_7C_097_stwx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_0B7_stwux(uint32_t op);
  static std::string dasm_7C_0B7_stwux(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_0C8_2C8_subfze(uint32_t op);
  static std::string dasm_7C_0C8_2C8_subfze(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_0CA_2CA_addze(uint32_t op);
  static std::string dasm_7C_0CA_2CA_addze(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_0D2_mtsr(uint32_t op);
  static std::string dasm_7C_0D2_mtsr(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_0D7_stbx(uint32_t op);
  static std::string dasm_7C_0D7_stbx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_0E8_2E8_subfme(uint32_t op);
  static std::string dasm_7C_0E8_2E8_subfme(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_0EA_2EA_addme(uint32_t op);
  static std::string dasm_7C_0EA_2EA_addme(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_0EB_2EB_mullw(uint32_t op);
  static std::string dasm_7C_0EB_2EB_mullw(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_0F2_mtsrin(uint32_t op);
  static std::string dasm_7C_0F2_mtsrin(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_0F6_dcbtst(uint32_t op);
  static std::string dasm_7C_0F6_dcbtst(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_0F7_stbux(uint32_t op);
  static std::string dasm_7C_0F7_stbux(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_10A_30A_add(uint32_t op);
  static std::string dasm_7C_10A_30A_add(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_116_dcbt(uint32_t op);
  static std::string dasm_7C_116_dcbt(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_117_lhzx(uint32_t op);
  static std::string dasm_7C_117_lhzx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_11C_eqv(uint32_t op);
  static std::string dasm_7C_11C_eqv(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_132_tlbie(uint32_t op);
  static std::string dasm_7C_132_tlbie(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_136_eciwx(uint32_t op);
  static std::string dasm_7C_136_eciwx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_137_lhzux(uint32_t op);
  static std::string dasm_7C_137_lhzux(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_13C_xor(uint32_t op);
  static std::string dasm_7C_13C_xor(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_153_mfspr(uint32_t op);
  static std::string dasm_7C_153_mfspr(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_157_lhax(uint32_t op);
  static std::string dasm_7C_157_lhax(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_172_tlbia(uint32_t op);
  static std::string dasm_7C_172_tlbia(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_173_mftb(uint32_t op);
  static std::string dasm_7C_173_mftb(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_177_lhaux(uint32_t op);
  static std::string dasm_7C_177_lhaux(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_197_sthx(uint32_t op);
  static std::string dasm_7C_197_sthx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_19C_orc(uint32_t op);
  static std::string dasm_7C_19C_orc(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_1B6_ecowx(uint32_t op);
  static std::string dasm_7C_1B6_ecowx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_1B7_sthux(uint32_t op);
  static std::string dasm_7C_1B7_sthux(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_1BC_or(uint32_t op);
  static std::string dasm_7C_1BC_or(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_1CB_3CB_divwu(uint32_t op);
  static std::string dasm_7C_1CB_3CB_divwu(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_1D3_mtspr(uint32_t op);
  static std::string dasm_7C_1D3_mtspr(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_1D6_dcbi(uint32_t op);
  static std::string dasm_7C_1D6_dcbi(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_1DC_nand(uint32_t op);
  static std::string dasm_7C_1DC_nand(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_1EB_3EB_divw(uint32_t op);
  static std::string dasm_7C_1EB_3EB_divw(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_200_mcrxr(uint32_t op);
  static std::string dasm_7C_200_mcrxr(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_215_lswx(uint32_t op);
  static std::string dasm_7C_215_lswx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_216_lwbrx(uint32_t op);
  static std::string dasm_7C_216_lwbrx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_217_lfsx(uint32_t op);
  static std::string dasm_7C_217_lfsx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_218_srw(uint32_t op);
  static std::string dasm_7C_218_srw(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_236_tlbsync(uint32_t op);
  static std::string dasm_7C_236_tlbsync(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_237_lfsux(uint32_t op);
  static std::string dasm_7C_237_lfsux(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_253_mfsr(uint32_t op);
  static std::string dasm_7C_253_mfsr(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_255_lswi(uint32_t op);
  static std::string dasm_7C_255_lswi(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_256_sync(uint32_t op);
  static std::string dasm_7C_256_sync(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_257_lfdx(uint32_t op);
  static std::string dasm_7C_257_lfdx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_277_lfdux(uint32_t op);
  static std::string dasm_7C_277_lfdux(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_293_mfsrin(uint32_t op);
  static std::string dasm_7C_293_mfsrin(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_295_stswx(uint32_t op);
  static std::string dasm_7C_295_stswx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_296_stwbrx(uint32_t op);
  static std::string dasm_7C_296_stwbrx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_297_stfsx(uint32_t op);
  static std::string dasm_7C_297_stfsx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_2B7_stfsux(uint32_t op);
  static std::string dasm_7C_2B7_stfsux(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_2E5_stswi(uint32_t op);
  static std::string dasm_7C_2E5_stswi(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_2E7_stfdx(uint32_t op);
  static std::string dasm_7C_2E7_stfdx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_2F6_dcba(uint32_t op);
  static std::string dasm_7C_2F6_dcba(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_2F7_stfdux(uint32_t op);
  static std::string dasm_7C_2F7_stfdux(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_316_lhbrx(uint32_t op);
  static std::string dasm_7C_316_lhbrx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_318_sraw(uint32_t op);
  static std::string dasm_7C_318_sraw(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_338_srawi(uint32_t op);
  static std::string dasm_7C_338_srawi(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_356_eieio(uint32_t op);
  static std::string dasm_7C_356_eieio(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_396_sthbrx(uint32_t op);
  static std::string dasm_7C_396_sthbrx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_39A_extsh(uint32_t op);
  static std::string dasm_7C_39A_extsh(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_3BA_extsb(uint32_t op);
  static std::string dasm_7C_3BA_extsb(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_3D6_icbi(uint32_t op);
  static std::string dasm_7C_3D6_icbi(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_3D7_stfiwx(uint32_t op);
  static std::string dasm_7C_3D7_stfiwx(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_7C_3F6_dcbz(uint32_t op);
  static std::string dasm_7C_3F6_dcbz(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  static std::string dasm_load_store_imm_u(uint32_t op, const char* base_name, bool is_store);
  static std::string dasm_load_store_imm(uint32_t op, const char* base_name, bool is_store);
  void exec_80_84_lwz_lwzu(uint32_t op);
  static std::string dasm_80_84_lwz_lwzu(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_88_8C_lbz_lbzu(uint32_t op);
  static std::string dasm_88_8C_lbz_lbzu(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_90_94_stw_stwu(uint32_t op);
  static std::string dasm_90_94_stw_stwu(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_98_9C_stb_stbu(uint32_t op);
  static std::string dasm_98_9C_stb_stbu(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_A0_A4_lhz_lhzu(uint32_t op);
  static std::string dasm_A0_A4_lhz_lhzu(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_A8_AC_lha_lhau(uint32_t op);
  static std::string dasm_A8_AC_lha_lhau(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_B0_B4_sth_sthu(uint32_t op);
  static std::string dasm_B0_B4_sth_sthu(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_B8_lmw(uint32_t op);
  static std::string dasm_B8_lmw(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_BC_stmw(uint32_t op);
  static std::string dasm_BC_stmw(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_C0_C4_lfs_lfsu(uint32_t op);
  static std::string dasm_C0_C4_lfs_lfsu(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_C8_CC_lfd_lfdu(uint32_t op);
  static std::string dasm_C8_CC_lfd_lfdu(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_D0_D4_stfs_stfsu(uint32_t op);
  static std::string dasm_D0_D4_stfs_stfsu(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_D8_DC_stfd_stfdu(uint32_t op);
  static std::string dasm_D8_DC_stfd_stfdu(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_EC(uint32_t op);
  static std::string dasm_EC(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  static std::string dasm_EC_FC_d_b_r(uint32_t op, const char* base_name);
  static std::string dasm_EC_FC_d_a_b_r(uint32_t op, const char* base_name);
  static std::string dasm_EC_FC_d_a_c_r(uint32_t op, const char* base_name);
  static std::string dasm_EC_FC_d_a_b_c_r(uint32_t op, const char* base_name);
  void exec_EC_12_fdivs(uint32_t op);
  static std::string dasm_EC_12_fdivs(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_EC_14_fsubs(uint32_t op);
  static std::string dasm_EC_14_fsubs(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_EC_15_fadds(uint32_t op);
  static std::string dasm_EC_15_fadds(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_EC_16_fsqrts(uint32_t op);
  static std::string dasm_EC_16_fsqrts(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_EC_18_fres(uint32_t op);
  static std::string dasm_EC_18_fres(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_EC_19_fmuls(uint32_t op);
  static std::string dasm_EC_19_fmuls(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_EC_1C_fmsubs(uint32_t op);
  static std::string dasm_EC_1C_fmsubs(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_EC_1D_fmadds(uint32_t op);
  static std::string dasm_EC_1D_fmadds(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_EC_1E_fnmsubs(uint32_t op);
  static std::string dasm_EC_1E_fnmsubs(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_EC_1F_fnmadds(uint32_t op);
  static std::string dasm_EC_1F_fnmadds(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC(uint32_t op);
  static std::string dasm_FC(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_12_fdiv(uint32_t op);
  static std::string dasm_FC_12_fdiv(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_14_fsub(uint32_t op);
  static std::string dasm_FC_14_fsub(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_15_fadd(uint32_t op);
  static std::string dasm_FC_15_fadd(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_16_fsqrt(uint32_t op);
  static std::string dasm_FC_16_fsqrt(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_17_fsel(uint32_t op);
  static std::string dasm_FC_17_fsel(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_19_fmul(uint32_t op);
  static std::string dasm_FC_19_fmul(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_1A_frsqrte(uint32_t op);
  static std::string dasm_FC_1A_frsqrte(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_1C_fmsub(uint32_t op);
  static std::string dasm_FC_1C_fmsub(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_1D_fmadd(uint32_t op);
  static std::string dasm_FC_1D_fmadd(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_1E_fnmsub(uint32_t op);
  static std::string dasm_FC_1E_fnmsub(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_1F_fnmadd(uint32_t op);
  static std::string dasm_FC_1F_fnmadd(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_000_fcmpu(uint32_t op);
  static std::string dasm_FC_000_fcmpu(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_00C_frsp(uint32_t op);
  static std::string dasm_FC_00C_frsp(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_00E_fctiw(uint32_t op);
  static std::string dasm_FC_00E_fctiw(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_00F_fctiwz(uint32_t op);
  static std::string dasm_FC_00F_fctiwz(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_020_fcmpo(uint32_t op);
  static std::string dasm_FC_020_fcmpo(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_026_mtfsb1(uint32_t op);
  static std::string dasm_FC_026_mtfsb1(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_028_fneg(uint32_t op);
  static std::string dasm_FC_028_fneg(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_040_mcrfs(uint32_t op);
  static std::string dasm_FC_040_mcrfs(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_046_mtfsb0(uint32_t op);
  static std::string dasm_FC_046_mtfsb0(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_048_fmr(uint32_t op);
  static std::string dasm_FC_048_fmr(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_086_mtfsfi(uint32_t op);
  static std::string dasm_FC_086_mtfsfi(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_088_fnabs(uint32_t op);
  static std::string dasm_FC_088_fnabs(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_108_fabs(uint32_t op);
  static std::string dasm_FC_108_fabs(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_247_mffs(uint32_t op);
  static std::string dasm_FC_247_mffs(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
  void exec_FC_2C7_mtfsf(uint32_t op);
  static std::string dasm_FC_2C7_mtfsf(uint32_t pc, uint32_t op, std::set<uint32_t>& labels);
};
