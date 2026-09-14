#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <array>
#include <deque>
#include <filesystem>
#include <forward_list>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <span>
#include <unordered_map>
#include <utility>

#include "MCS6502Emulator.hh"

namespace ResourceDASM {

template <typename VisitorT>
VisitorT::DecodeReturnT MCS6502Emulator::decode_instruction(VisitorT& visitor) {
  uint8_t opcode = visitor.read_ins_u8();
  uint8_t a = (opcode >> 5) & 7;
  uint8_t b = (opcode >> 2) & 7;
  uint8_t c = opcode & 3;

  auto decode_address_class0_class1 = [&](uint8_t b) -> DecodedAddress {
    switch (b) {
      case 0:
        return DecodedAddress{.mode = AM::INDIRECT_X, .disp = visitor.read_ins_u8()};
      case 1:
        return DecodedAddress{.mode = AM::ZERO_PAGE, .disp = visitor.read_ins_u8()};
      case 2:
        return DecodedAddress{.mode = AM::IMM, .disp = visitor.read_ins_u8()};
      case 3:
        return DecodedAddress{.mode = AM::ABSOLUTE, .disp = visitor.read_ins_u16()};
      case 4:
        return DecodedAddress{.mode = AM::INDIRECT_Y, .disp = visitor.read_ins_u8()};
      case 5:
        return DecodedAddress{.mode = AM::ZERO_PAGE_X, .disp = visitor.read_ins_u8()};
      case 6:
        return DecodedAddress{.mode = AM::ABSOLUTE_Y, .disp = visitor.read_ins_u16()};
      case 7:
        return DecodedAddress{.mode = AM::ABSOLUTE_X, .disp = visitor.read_ins_u16()};
      default:
        throw std::logic_error("Invalid addressing mode bits");
    }
  };

  switch (c) {
    case 0: {
      switch (opcode) {
        case 0b00010000: // BPL 00010000  $10   2   2++ -------
        case 0b00110000: // BMI 00110000  $30   2   2++ -------
        case 0b01010000: // BVC 01010000  $50   2   2++ -------
        case 0b01110000: // BVS 01110000  $70   2   2++ -------
        case 0b10010000: // BCC 10010000  $90   2   2++ -------
        case 0b10110000: // BCS 10110000  $B0   2   2++ -------
        case 0b11010000: // BNE 11010000  $D0   2   2++ -------
        case 0b11110000: // BEQ 11110000  $F0   2   2++ -------
          return visitor.on_b_mnemonics(a, visitor.read_ins_s8());
        case 0b00011000: // CLC 00011000  $18   1   2   --C----
          return visitor.on_clc();
        case 0b00111000: // SEC 00111000  $38   1   2   --C----
          return visitor.on_sec();
        case 0b01011000: // CLI 01011000  $58   1   2   ------I
          return visitor.on_cli();
        case 0b01111000: // SEI 01111000  $78   1   2   ------I
          return visitor.on_sei();
        case 0b10111000: // CLV 10111000  $B8   1   2   ---V---
          return visitor.on_clv();
        case 0b11011000: // CLD 11011000  $D8   1   2   -----D-
          return visitor.on_cld();
        case 0b11111000: // SED 11111000  $F8   1   2   -----D-
          return visitor.on_sed();
        case 0b00100100: // BIT 00100100  $24   2   3   NZ-V---  Zero Page
        case 0b00101100: // BIT 00101100  $2C   3   4   NZ-V---  Absolute
          return visitor.on_bit(decode_address_class0_class1(b));
        case 0b00000000: // BRK 00000000  $00   1   7   ----B--  Implied
          return visitor.on_brk();
        case 0b11100000: // CPX 11100000  $E0   2   2   NZC----  Immediate
          return visitor.on_cpx(DecodedAddress{.mode = AM::IMM, .disp = visitor.read_ins_u8()});
        case 0b11100100: // CPX 11100100  $E4   2   3   NZC----  Zero Page
        case 0b11101100: // CPX 11101100  $EC   3   4   NZC----  Absolute
          return visitor.on_cpx(decode_address_class0_class1(b));
        case 0b11000000: // CPY 11000000  $C0   2   2   NZC----  Immediate
          return visitor.on_cpy(DecodedAddress{.mode = AM::IMM, .disp = visitor.read_ins_u8()});
        case 0b11000100: // CPY 11000100  $C4   2   3   NZC----  Zero Page
        case 0b11001100: // CPY 11001100  $CC   3   4   NZC----  Absolute
          return visitor.on_cpy(decode_address_class0_class1(b));
        case 0b01001100: // JMP 01001100  $4C   3   3   -------  Absolute
          return visitor.on_jmp(DecodedAddress{.mode = AM::IMM, .disp = visitor.read_ins_u16()});
        case 0b01101100: // JMP 01101100  $6C   3   5   -------  Indirect
          return visitor.on_jmp(DecodedAddress{.mode = AM::ABSOLUTE, .disp = visitor.read_ins_u16()});
        case 0b00100000: // JSR 00100000  $20   3   6   -------  Absolute
          return visitor.on_jsr(DecodedAddress{.mode = AM::IMM, .disp = visitor.read_ins_u16()});
        case 0b01000000: // RTI 01000000  $40   1   6   NZCVBD-  Implied
          return visitor.on_rti();
        case 0b01100000: // RTS 01100000  $60   1   6   -------  Implied
          return visitor.on_rts();
        case 0b10001000: // DEY 10001000  $88   1   2   NZ-----
          return visitor.on_dey();
        case 0b11001000: // INY 11001000  $C8   1   2   NZ-----
          return visitor.on_iny();
        case 0b11101000: // INX 11101000  $E8   1   2   NZ-----
          return visitor.on_inx();
        case 0b10100000: // LDY 10100000  $A0   2   2   NZ-----  Immediate
          return visitor.on_ldy(DecodedAddress{.mode = AM::IMM, .disp = visitor.read_ins_u8()});
        case 0b10100100: // LDY 10100100  $A4   2   3   NZ-----  Zero Page
        case 0b10101100: // LDY 10101100  $AC   3   4   NZ-----  Absolute
        case 0b10110100: // LDY 10110100  $B4   2   4   NZ-----  Zero Page,X
        case 0b10111100: // LDY 10111100  $BC   3   4+  NZ-----  Absolute,X
          return visitor.on_ldy(decode_address_class0_class1(b));
        case 0b01001000: // PHA 01001000  $48   1   3   -------
          return visitor.on_pha();
        case 0b00001000: // PHP 00001000  $08   1   3   -------
          return visitor.on_php();
        case 0b01101000: // PLA 01101000  $68   1   4   -------
          return visitor.on_pla();
        case 0b00101000: // PLP 00101000  $28   1   4   -------
          return visitor.on_plp();
        case 0b10000100: // STY 10000100  $84   2   3   -------  Zero Page
        case 0b10001100: // STY 10001100  $8C   3   4   -------  Absolute
        case 0b10010100: // STY 10010100  $94   2   4   -------  Zero Page,X
          return visitor.on_sty(decode_address_class0_class1(b));
        case 0b10101000: // TAY 10101000  $A8   1   2   NZ-----
          return visitor.on_tay();
        case 0b10011000: // TYA 10011000  $98   1   2   NZ-----
          return visitor.on_tya();
        default:
          return visitor.on_invalid("Invalid class-0 opcode");
      }
      throw std::logic_error("Unhandled class-2 opcode");
    }

    case 1: {
      switch (a) {
        case 0:
          // ORA 00000001  $01   2   6   NZ-----  Indirect,X
          // ORA 00000101  $05   2   3   NZ-----  Zero Page
          // ORA 00001001  $09   2   2   NZ-----  Immediate
          // ORA 00001101  $0D   3   4   NZ-----  Absolute
          // ORA 00010001  $11   2   5+  NZ-----  Indirect,Y
          // ORA 00010101  $15   2   4   NZ-----  Zero Page,X
          // ORA 00011001  $19   3   4+  NZ-----  Absolute,Y
          // ORA 00011101  $1D   3   4+  NZ-----  Absolute,X
          return visitor.on_ora(decode_address_class0_class1(b));
        case 1:
          // AND 00100001  $21   2   6   NZ-----  Indirect,X
          // AND 00100101  $25   2   3   NZ-----  Zero Page
          // AND 00101001  $29   2   2   NZ-----  Immediate
          // AND 00101101  $2D   3   4   NZ-----  Absolute
          // AND 00110001  $31   2   5+  NZ-----  Indirect,Y
          // AND 00110101  $35   2   4   NZ-----  Zero Page,X
          // AND 00111001  $39   3   4+  NZ-----  Absolute,Y
          // AND 00111101  $3D   3   4+  NZ-----  Absolute,X
          return visitor.on_and(decode_address_class0_class1(b));
        case 2:
          // EOR 01000001  $41   2   6   NZ-----  Indirect,X
          // EOR 01000101  $45   2   3   NZ-----  Zero Page
          // EOR 01001001  $49   2   2   NZ-----  Immediate
          // EOR 01001101  $4D   3   4   NZ-----  Absolute
          // EOR 01010001  $51   2   5+  NZ ----  Indirect,Y
          // EOR 01010101  $55   2   4   NZ ----  Zero Page,X
          // EOR 01011001  $59   3   4+  NZ-----  Absolute,Y
          // EOR 01011101  $5D   3   4+  NZ-----  Absolute,X
          return visitor.on_xor(decode_address_class0_class1(b));
        case 3:
          // ADC 01100001  $61   2   6   NZCV---  Indirect,X
          // ADC 01100101  $65   2   3   NZCV---  Zero Page
          // ADC 01101001  $69   2   2   NZCV---  Immediate
          // ADC 01101101  $6D   3   4   NZCV---  Absolute
          // ADC 01110001  $71   2   5+  NZCV---  Indirect,Y
          // ADC 01110101  $75   2   4   NZCV---  Zero Page,X
          // ADC 01111001  $79   3   4+  NZCV---  Absolute,Y
          // ADC 01111101  $7D   3   4+  NZCV---  Absolute,X
          return visitor.on_adc(decode_address_class0_class1(b));
        case 4:
          // STA 10000001  $81   2   6   -------  Indirect,X
          // STA 10000101  $85   2   3   -------  Zero Page
          // STA 10001101  $8D   3   4   -------  Absolute
          // STA 10010001  $91   2   6   -------  Indirect,Y
          // STA 10010101  $95   2   4   -------  Zero Page,X
          // STA 10011001  $99   3   5   -------  Absolute,Y
          // STA 10011101  $9D   3   5   -------  Absolute,X
          return (b == 2)
              ? visitor.on_invalid("IMM not allowed for sta")
              : visitor.on_sta(decode_address_class0_class1(b));
        case 5:
          // LDA 10100001  $A1   2   6   NZ-----  Indirect,X
          // LDA 10100101  $A5   2   3   NZ-----  Zero Page
          // LDA 10101001  $A9   2   2   NZ-----  Immediate
          // LDA 10101101  $AD   3   4   NZ-----  Absolute
          // LDA 10110001  $B1   2   5+  NZ-----  Indirect,Y
          // LDA 10110101  $B5   2   4   NZ-----  Zero Page,X
          // LDA 10111001  $B9   3   4+  NZ-----  Absolute,Y
          // LDA 10111101  $BD   3   4+  NZ-----  Absolute,X
          return visitor.on_lda(decode_address_class0_class1(b));
        case 6:
          // CMP 11000001  $C1   2   6   NZC----  Indirect,X
          // CMP 11000101  $C5   2   3   NZC----  Zero Page
          // CMP 11001001  $C9   2   2   NZC----  Immediate
          // CMP 11001101  $CD   3   4   NZC----  Absolute
          // CMP 11010001  $D1   2   5+  NZC----  Indirect,Y
          // CMP 11010101  $D5   2   4   NZC----  Zero Page,X
          // CMP 11011001  $D9   3   4+  NZC----  Absolute,Y
          // CMP 11011101  $DD   3   4+  NZC----  Absolute,X
          return visitor.on_cmp(decode_address_class0_class1(b));
        case 7:
          // SBC 11100001  $E1   2   6   NZCV---  Indirect,X
          // SBC 11100101  $E5   2   3   NZCV---  Zero Page
          // SBC 11101001  $E9   2   2   NZCV---  Immediate
          // SBC 11101101  $ED   3   4   NZCV---  Absolute
          // SBC 11110001  $F1   2   5+  NZCV---  Indirect,Y
          // SBC 11110101  $F5   2   4   NZCV---  Zero Page,X
          // SBC 11111001  $F9   3   4+  NZCV---  Absolute,Y
          // SBC 11111101  $FD   3   4+  NZCV---  Absolute,X
          return visitor.on_sbc(decode_address_class0_class1(b));
      }
      throw std::logic_error("Unhandled class-1 opcode");
    }

    case 2: {
      auto decode_address_class2 = [&](uint8_t b, uint8_t allowed_modes) -> DecodedAddress {
        if (!(allowed_modes & (1 << b))) {
          return DecodedAddress{.mode = AM::INVALID, .invalid_reason = "Invalid addressing mode"};
        }
        switch (b) {
          case 0:
          case 4:
          case 6:
            return DecodedAddress{.mode = AM::INVALID, .invalid_reason = "Invalid class-2 addressing mode"};
          case 1:
            return DecodedAddress{.mode = AM::ZERO_PAGE, .disp = visitor.read_ins_u8()};
          case 2:
            return DecodedAddress{.mode = AM::A};
          case 3:
            return DecodedAddress{.mode = AM::ABSOLUTE, .disp = visitor.read_ins_u16()};
          case 5:
            return DecodedAddress{.mode = AM::ZERO_PAGE_X, .disp = visitor.read_ins_u8()};
          case 7:
            return DecodedAddress{.mode = AM::ABSOLUTE_X, .disp = visitor.read_ins_u16()};
          default:
            throw std::logic_error("Invalid addressing mode bits");
        }
      };
      switch (a) {
        case 0:
          // ASL 00000110  $06   2   5   NZC----  Zero Page
          // ASL 00001010  $0A   1   2   NZC----  Accumulator
          // ASL 00001110  $0E   3   6   NZC----  Absolute
          // ASL 00010110  $16   2   6   NZC----  Zero Page,X
          // ASL 00011110  $1E   3   7   NZC----  Absolute,X
          return visitor.on_asl(decode_address_class2(b, 0xAE));
        case 1:
          // ROL 00100110  $26   2   5   NZC----  Zero Page
          // ROL 00101010  $2A   1   2   NZC----  Accumulator
          // ROL 00101110  $2E   3   6   NZC----  Absolute
          // ROL 00110110  $36   2   6   NZC----  Zero Page,X
          // ROL 00111110  $3E   3   7   NZC----  Absolute,X
          return visitor.on_rol(decode_address_class2(b, 0xAE));
        case 2:
          // LSR 01000110  $46   2   5   NZC----  Zero Page
          // LSR 01001010  $4A   1   2   NZC----  Accumulator
          // LSR 01001110  $4E   3   6   NZC----  Absolute
          // LSR 01010110  $56   2   6   NZC----  Zero Page,X
          // LSR 01011110  $5E   3   7   NZC----  Absolute,X
          return visitor.on_lsr(decode_address_class2(b, 0xAE));
        case 3:
          // ROR 01100110  $66   2   5   NZC----  Zero Page
          // ROR 01101010  $6A   1   2   NZC----  Accumulator
          // ROR 01101110  $6E   3   6   NZC----  Absolute
          // ROR 01110110  $76   2   6   NZC----  Zero Page,X
          // ROR 01111110  $7E   3   7   NZC----  Absolute,X
          return visitor.on_ror(decode_address_class2(b, 0xAE));
        case 4:
          switch (b) {
            case 1: // STX 10000110  $86   2   3   -------  Zero Page
            case 3: // STX 10001110  $8E   3   4   -------  Absolute
              return visitor.on_stx(decode_address_class2(b, 0xFF));
            case 5: // STX 10010110  $96   2   4   -------  Zero Page,Y
              return visitor.on_stx(DecodedAddress{.mode = AM::ZERO_PAGE_Y, .disp = visitor.read_ins_u8()});
            case 2: // TXA 10001010  $8A   1   2   NZ-----
              return visitor.on_txa();
            case 6: // TXS 10011010  $9A   1   2   -------
              return visitor.on_txs();
            case 0:
            case 4:
            case 7:
              return visitor.on_invalid("Invalid opcode 2/4");
          }
          throw std::logic_error("Unhandled opcode 2/4");
        case 5:
          switch (b) {
            case 0: // LDX 10100010  $A2   2   2   NZ-----  Immediate
              return visitor.on_ldx(DecodedAddress{.mode = AM::IMM, .disp = visitor.read_ins_u8()});
            case 1: // LDX 10100110  $A6   2   3   NZ-----  Zero Page
            case 3: // LDX 10101110  $AE   3   4   NZ-----  Absolute
              return visitor.on_ldx(decode_address_class2(b, 0xFF));
            case 5: // LDX 10110110  $B6   2   4   NZ-----  Zero Page,Y
              return visitor.on_ldx(DecodedAddress{.mode = AM::ZERO_PAGE_Y, .disp = visitor.read_ins_u8()});
            case 7: // LDX 10111110  $BE   3   4+  NZ-----  Absolute,Y
              return visitor.on_ldx(DecodedAddress{.mode = AM::ABSOLUTE_Y, .disp = visitor.read_ins_u16()});
            case 2: // TAX 10101010  $AA   1   2   NZ-----
              return visitor.on_tax();
            case 6: // TSX 10111010  $BA   1   2   -------
              return visitor.on_tsx();
            case 4:
              return visitor.on_invalid("Invalid opcode 2/5");
          }
          throw std::logic_error("Unhandled opcode 2/5");
        case 6:
          // DEC 11000110  $C6   2   5   NZ-----  Zero Page
          // DEX 11001010  $CA   1   2   NZ-----
          // DEC 11001110  $CE   3   6   NZ-----  Absolute
          // DEC 11010110  $D6   2   6   NZ-----  Zero Page,X
          // DEC 11011110  $DE   3   7   NZ-----  Absolute,X
          return (b == 2) ? visitor.on_dex() : visitor.on_dec(decode_address_class2(b, 0xAA));
        case 7:
          switch (b) {
            case 1: // INC 11100110  $E6   2   5   NZ-----  Zero Page
            case 3: // INC 11101110  $EE   3   6   NZ-----  Absolute
            case 5: // INC 11110110  $F6   2   6   NZ-----  Zero Page,X
            case 7: // INC 11111110  $FE   3   7   NZ-----  Absolute,X
              return visitor.on_inc(decode_address_class2(b, 0xFF));
            case 2: // NOP 11101010  $EA   1   2   -------  Implied
              return visitor.on_nop();
            case 0:
            case 4:
            case 6:
              return visitor.on_invalid("Invalid opcode 2/7");
          }
          throw std::logic_error("Unhandled opcode 2/7");
        default:
          return visitor.on_invalid("Invalid class-2 opcode");
      }
      throw std::logic_error("Unhandled class-2 opcode");
    }

    case 3:
      return visitor.on_invalid("Invalid class-3 opcode");

    default:
      throw std::logic_error("Invalid opcode class");
  }
}

void MCS6502Emulator::Regs::import_state(FILE* stream) {
  uint8_t version = phosg::freadx<uint8_t>(stream);
  if (version > 0) {
    throw std::runtime_error("unknown format version");
  }
  this->a = phosg::freadx<uint8_t>(stream);
  this->x = phosg::freadx<uint8_t>(stream);
  this->y = phosg::freadx<uint8_t>(stream);
  this->s = phosg::freadx<uint8_t>(stream);
  this->pc = phosg::freadx<phosg::le_uint16_t>(stream);
  this->p.u = phosg::freadx<uint8_t>(stream);
}

void MCS6502Emulator::Regs::export_state(FILE* stream) const {
  phosg::fwritex(stream, 1); // version
  phosg::fwritex<uint8_t>(stream, this->a);
  phosg::fwritex<uint8_t>(stream, this->x);
  phosg::fwritex<uint8_t>(stream, this->y);
  phosg::fwritex<uint8_t>(stream, this->s);
  phosg::fwritex<phosg::le_uint16_t>(stream, this->pc);
  phosg::fwritex<uint8_t>(stream, this->p.u);
}

void MCS6502Emulator::Regs::set_by_name(const std::string& reg_name, uint32_t value) {
  auto lower_name = phosg::tolower(reg_name);
  if (lower_name == "a") {
    this->a = value;
  } else if (lower_name == "x") {
    this->x = value;
  } else if (lower_name == "y") {
    this->y = value;
  } else if (lower_name == "s") {
    this->s = value;
  } else if (lower_name == "pc") {
    this->pc = value;
  } else if (lower_name == "p") {
    this->p.u = value;
  } else {
    throw std::invalid_argument("invalid register name");
  }
}

void MCS6502Emulator::import_state(FILE* stream) {
  uint8_t version = phosg::freadx<uint8_t>(stream);
  if (version != 0) {
    throw std::runtime_error("unknown format version");
  }
  this->regs.import_state(stream);
  this->mem->import_state(stream);
}

void MCS6502Emulator::export_state(FILE* stream) const {
  phosg::fwritex<uint8_t>(stream, 0); // version
  this->regs.export_state(stream);
  this->mem->export_state(stream);
}

void MCS6502Emulator::print_state_header(FILE* stream) const {
  phosg::fwrite_fmt(stream, "A- X- Y- P-(NVBDIZC) PC-- = INSTRUCTION\n");
}

void MCS6502Emulator::print_state(FILE* stream) const {
  size_t pc_data_available = 4;
  while (pc_data_available && !this->mem->exists(this->regs.pc, pc_data_available)) {
    pc_data_available--;
  }

  std::string disassembly;
  if (pc_data_available) {
    const void* pc_data = this->mem->at<void>(this->regs.pc, pc_data_available);
    try {
      disassembly = this->disassemble_one(pc_data, pc_data_available, this->regs.pc);
    } catch (const std::exception& e) {
      disassembly = std::format(" (failed: {})", e.what());
    }
  } else {
    disassembly = " (address out of range)";
  }

  phosg::fwrite_fmt(stream, "{:02X} {:02X} {:02X} {:02X}({:c}{:c}{:c}{:c}{:c}{:c}{:c}) {:04X} ={}\n",
      this->regs.a, this->regs.x, this->regs.y, this->regs.p.u, (this->regs.p.get_n() ? 'n' : '-'),
      (this->regs.p.get_v() ? 'v' : '-'), (this->regs.p.get_b() ? 'b' : '-'), (this->regs.p.get_d() ? 'd' : '-'),
      (this->regs.p.get_i() ? 'i' : '-'), (this->regs.p.get_z() ? 'z' : '-'), (this->regs.p.get_c() ? 'c' : '-'),
      this->regs.pc, disassembly);
}

uint16_t MCS6502Emulator::read_u16_from_zero_page(uint8_t addr) const {
  return (addr == 0xFF) ? ((this->mem->read_u8(0x00) << 8) | this->mem->read_u8(0xFF)) : this->mem->read_u16l(addr);
}

std::string MCS6502Emulator::DecodedAddress::str() const {
  switch (this->mode) {
    case AM::IMM:
      return std::format("0x{:02X}", this->disp);
    case AM::ZERO_PAGE:
      return std::format("[0x{:02X}]", this->disp);
    case AM::ZERO_PAGE_X:
      return std::format("[(x + 0x{:02X}) & 0xFF]", this->disp);
    case AM::ZERO_PAGE_Y:
      return std::format("[(y + 0x{:02X}) & 0xFF]", this->disp);
    case AM::ABSOLUTE:
      return std::format("[0x{:04X}]", this->disp);
    case AM::ABSOLUTE_X:
      return std::format("[x + 0x{:04X}]", this->disp);
    case AM::ABSOLUTE_Y:
      return std::format("[y + 0x{:04X}]", this->disp);
    case AM::INDIRECT_X:
      return std::format("[[(x + 0x{:02X}) & 0xFF]]", this->disp);
    case AM::INDIRECT_Y:
      return std::format("[[0x{:02X}] + y]", this->disp);
    case AM::A:
      return std::format("a", this->disp);
    case AM::INVALID:
      return std::format("<< invalid address: {} >>", this->invalid_reason);
    default:
      throw std::logic_error("Invalid decoded address mode");
  }
}

uint8_t MCS6502Emulator::read_from_address(const DecodedAddress& addr) const {
  switch (addr.mode) {
    case AM::IMM:
      return addr.disp;
    case AM::ZERO_PAGE:
    case AM::ABSOLUTE:
      return this->mem->read_u8(addr.disp);
    case AM::ZERO_PAGE_X:
      return this->mem->read_u8((this->regs.x + addr.disp) & 0x00FF);
    case AM::ZERO_PAGE_Y:
      return this->mem->read_u8((this->regs.y + addr.disp) & 0x00FF);
    case AM::ABSOLUTE_X:
      return this->mem->read_u8(this->regs.x + addr.disp);
    case AM::ABSOLUTE_Y:
      return this->mem->read_u8(this->regs.y + addr.disp);
    case AM::INDIRECT_X:
      return this->mem->read_u8(this->read_u16_from_zero_page(addr.disp + this->regs.x));
    case AM::INDIRECT_Y:
      return this->mem->read_u8(this->read_u16_from_zero_page(addr.disp) + this->regs.y);
    case AM::A:
      return this->regs.a;
    case AM::INVALID:
      throw std::runtime_error(std::format("Invalid address: {}", addr.invalid_reason));
    default:
      throw std::logic_error("Invalid decoded address mode");
  }
}

void MCS6502Emulator::write_to_address(const DecodedAddress& addr, uint8_t v) {
  switch (addr.mode) {
    case AM::IMM:
      throw std::runtime_error("Cannot write to immediate value");
    case AM::ZERO_PAGE:
    case AM::ABSOLUTE:
      this->mem->write_u8(addr.disp, v);
      break;
    case AM::ZERO_PAGE_X:
      this->mem->write_u8((this->regs.x + addr.disp) & 0xFF, v);
      break;
    case AM::ZERO_PAGE_Y:
      this->mem->write_u8((this->regs.y + addr.disp) & 0xFF, v);
      break;
    case AM::ABSOLUTE_X:
      this->mem->write_u8(this->regs.x + addr.disp, v);
      break;
    case AM::ABSOLUTE_Y:
      this->mem->write_u8(this->regs.y + addr.disp, v);
      break;
    case AM::INDIRECT_X:
      this->mem->write_u8(this->read_u16_from_zero_page(this->regs.x + addr.disp), v);
      break;
    case AM::INDIRECT_Y:
      this->mem->write_u8(this->read_u16_from_zero_page(addr.disp) + this->regs.y, v);
      break;
    case AM::A:
      this->regs.a = v;
      break;
    case AM::INVALID:
      throw std::runtime_error(std::format("Invalid address: {}", addr.invalid_reason));
    default:
      throw std::logic_error("Invalid decoded address mode");
  }
}

MCS6502Emulator::DisassemblyState::DisassemblyState(const void* data, size_t size, uint32_t start_address)
    : r(data, size), start_address(start_address), opcode_start_address(this->start_address) {}

std::string MCS6502Emulator::DisassemblyState::on_invalid(const char* invalid_reason) {
  this->prev_was_valid = false;
  return std::format(".invalid  // {}", invalid_reason);
}
void MCS6502Emulator::on_invalid(const char* invalid_reason) {
  throw std::runtime_error(std::format("Invalid opcode: {}", invalid_reason));
}

std::string MCS6502Emulator::DisassemblyState::on_invalid(const DecodedAddress& addr) {
  this->prev_was_valid = false;
  return std::format(".invalid  {}", addr.str());
}
void MCS6502Emulator::on_invalid(const DecodedAddress& addr) {
  throw std::runtime_error(std::format("Invalid address: {}", addr.str()));
}

void MCS6502Emulator::on_adc_sbc(const DecodedAddress& addr, bool is_sbc) {
  uint8_t mem_val = this->read_from_address(addr) ^ (is_sbc ? 0xFF : 0x00);

  if (this->regs.p.get_d()) {
    // Based on https://github.com/eteran/pretendo/blob/master/doc/cpu/6502.txt
    bool c = this->regs.p.get_c();
    uint8_t result_high, result_low;
    if (is_sbc) {
      result_low = (this->regs.a & 0x0F) - (mem_val & 0x0F) - !c;
      if (result_low & 0x10) {
        result_low -= 6;
      }
      result_high = (this->regs.a >> 4) - (mem_val >> 4) - (result_low & 0x10);
      if (result_high & 0x10) {
        result_high -= 6;
      }
      uint16_t bin_result = (this->regs.a - mem_val - !c);
      this->regs.p.set_c(bin_result & 0x100);
      this->regs.p.set_z(bin_result & 0xFF);
      this->regs.p.set_v(((bin_result ^ mem_val) & 0x80) && ((this->regs.a ^ mem_val) & 0x80));
      this->regs.p.set_n(bin_result & 0x80);

    } else {
      result_low = (this->regs.a & 0x0F) + (mem_val & 0x0F) + c;
      result_high = (this->regs.a >> 4) + (mem_val >> 4) + (result_low > 0x0F);
      if (result_low > 9) {
        result_low += 6;
      }
      this->regs.p.set_z(((this->regs.a + mem_val + c) & 0xFF) != 0);
      this->regs.p.set_n(result_high & 0x08);
      this->regs.p.set_v((((result_high << 4) ^ this->regs.a) & 0x80) && !((this->regs.a ^ mem_val) & 0x80));
      if (result_high > 9) {
        result_high += 6;
      }
      this->regs.p.set_c(result_high > 0x0F);
    }
    this->regs.a = ((result_high << 4) | (result_low & 0x0F));

  } else {
    if (is_sbc) {
      mem_val = ~mem_val;
    }
    uint16_t new_val = this->regs.a + this->regs.p.get_c() + mem_val;
    this->regs.p.set_c(new_val > 0xFF);
    this->regs.p.set_z(new_val == 0);
    this->regs.p.set_v((new_val ^ this->regs.a) & (new_val ^ mem_val) & 0x80);
    this->regs.p.set_n(new_val & 0x80);
    this->regs.a = new_val;
  }
}

std::string MCS6502Emulator::DisassemblyState::on_adc(const DecodedAddress& addr) {
  return std::format("adc       {}", addr.str());
}
void MCS6502Emulator::on_adc(const DecodedAddress& addr) {
  this->on_adc_sbc(addr, false);
}

std::string MCS6502Emulator::DisassemblyState::on_and(const DecodedAddress& addr) {
  return std::format("and       {}", addr.str());
}
void MCS6502Emulator::on_and(const DecodedAddress& addr) {
  this->regs.a &= this->read_from_address(addr);
  this->regs.p.set_z(this->regs.a == 0);
  this->regs.p.set_n(this->regs.a & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_asl(const DecodedAddress& addr) {
  return std::format("asl       {}", addr.str());
}
void MCS6502Emulator::on_asl(const DecodedAddress& addr) {
  uint8_t v = this->read_from_address(addr);
  this->write_to_address(addr, v);
  this->regs.p.set_c(v & 0x80);
  v <<= 1;
  this->write_to_address(addr, v);
  this->regs.p.set_z(v == 0);
  this->regs.p.set_n(v & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_b_mnemonics(uint8_t condition, int8_t delta) {
  static constexpr std::array<const char*, 8> condition_names{"pl", "mi", "vc", "vs", "cc", "cs", "ne", "eq"};
  uint32_t target = this->r.where() + this->start_address + delta;
  this->add_branch_target_address(target, false);
  return std::format("b{}       {:c}0x{:02X} /* 0x{:04X} */",
      condition_names.at(condition),
      (delta < 0) ? '-' : '+',
      static_cast<uint8_t>((delta < 0) ? -delta : delta),
      target);
}
void MCS6502Emulator::on_b_mnemonics(uint8_t condition, int8_t delta) {
  // Conditions:
  //   0 (pl) = !n, 1 (mi) = n
  //   2 (vc) = !v, 3 (vs) = v
  //   4 (cc) = !c, 5 (cs) = c
  //   6 (ne) = !z, 7 (eq) = z
  static constexpr std::array<uint8_t, 4> condition_bits{Condition::N, Condition::V, Condition::C, Condition::Z};
  if (!!(this->regs.p.u & condition_bits.at(condition >> 1)) == (condition & 1)) {
    this->regs.pc += delta;
  }
}

std::string MCS6502Emulator::DisassemblyState::on_bit(const DecodedAddress& addr) {
  return std::format("bit       {}", addr.str());
}
void MCS6502Emulator::on_bit(const DecodedAddress& addr) {
  uint8_t mem_val = this->read_from_address(addr);
  this->regs.p.set_z(!(mem_val & this->regs.a));
  this->regs.p.set_v(mem_val & 0x40);
  this->regs.p.set_n(mem_val & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_brk() {
  return "brk";
}
void MCS6502Emulator::on_brk() {
  this->push16(this->regs.pc + 1); // Byte after brk instruction is unused, apparently
  this->push8(this->regs.p.u | 0x30);
  this->regs.pc = 0xFFFE;
}

std::string MCS6502Emulator::DisassemblyState::on_clc() {
  return "clc";
}
void MCS6502Emulator::on_clc() {
  this->regs.p.set_c(false);
}

std::string MCS6502Emulator::DisassemblyState::on_cld() {
  return "cld";
}
void MCS6502Emulator::on_cld() {
  this->regs.p.set_d(false);
}

std::string MCS6502Emulator::DisassemblyState::on_cli() {
  return "cli";
}
void MCS6502Emulator::on_cli() {
  this->regs.p.set_i(false);
}

std::string MCS6502Emulator::DisassemblyState::on_clv() {
  return "clv";
}
void MCS6502Emulator::on_clv() {
  this->regs.p.set_v(false);
}

void MCS6502Emulator::on_cmp_values(uint8_t reg_val, const DecodedAddress& addr) {
  uint8_t mem_val = this->read_from_address(addr);
  this->regs.p.set_c(reg_val >= mem_val);
  this->regs.p.set_z(reg_val == mem_val);
  this->regs.p.set_n((reg_val - mem_val) & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_cmp(const DecodedAddress& addr) {
  return std::format("cmp       {}", addr.str());
}
void MCS6502Emulator::on_cmp(const DecodedAddress& addr) {
  this->on_cmp_values(this->regs.a, addr);
}

std::string MCS6502Emulator::DisassemblyState::on_cpx(const DecodedAddress& addr) {
  return std::format("cpx       {}", addr.str());
}
void MCS6502Emulator::on_cpx(const DecodedAddress& addr) {
  this->on_cmp_values(this->regs.x, addr);
}

std::string MCS6502Emulator::DisassemblyState::on_cpy(const DecodedAddress& addr) {
  return std::format("cpy       {}", addr.str());
}
void MCS6502Emulator::on_cpy(const DecodedAddress& addr) {
  this->on_cmp_values(this->regs.y, addr);
}

std::string MCS6502Emulator::DisassemblyState::on_dec(const DecodedAddress& addr) {
  return std::format("dec       {}", addr.str());
}
void MCS6502Emulator::on_dec(const DecodedAddress& addr) {
  uint8_t v = this->read_from_address(addr);
  this->write_to_address(addr, v); // Original 6502 writes twice here
  v--;
  this->write_to_address(addr, v);
  this->regs.p.set_z(v == 0);
  this->regs.p.set_n(v & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_dex() {
  return "dex";
}
void MCS6502Emulator::on_dex() {
  this->regs.x--;
  this->regs.p.set_z(this->regs.x == 0);
  this->regs.p.set_n(this->regs.x & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_dey() {
  return "dey";
}
void MCS6502Emulator::on_dey() {
  this->regs.y--;
  this->regs.p.set_z(this->regs.y == 0);
  this->regs.p.set_n(this->regs.y & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_inc(const DecodedAddress& addr) {
  return std::format("inc       {}", addr.str());
}
void MCS6502Emulator::on_inc(const DecodedAddress& addr) {
  uint8_t v = this->read_from_address(addr);
  this->write_to_address(addr, v); // Original 6502 writes twice here
  v++;
  this->write_to_address(addr, v);
  this->regs.p.set_z(v == 0);
  this->regs.p.set_n(v & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_inx() {
  return "inx";
}
void MCS6502Emulator::on_inx() {
  this->regs.x++;
  this->regs.p.set_z(this->regs.x == 0);
  this->regs.p.set_n(this->regs.x & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_iny() {
  return "iny";
}
void MCS6502Emulator::on_iny() {
  this->regs.y++;
  this->regs.p.set_z(this->regs.y == 0);
  this->regs.p.set_n(this->regs.y & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_jmp(const DecodedAddress& addr) {
  this->add_jump_target_address(addr, false);
  return std::format("jmp       {}", addr.str());
}
void MCS6502Emulator::on_jmp(const DecodedAddress& addr) {
  if (addr.mode == AM::ABSOLUTE) {
    if ((addr.disp & 0xFF) == 0xFF) {
      this->regs.pc = (this->mem->read_u8(addr.disp & 0xFF00) << 8) | this->mem->read_u8(addr.disp);
    } else {
      this->regs.pc = this->mem->read_u16l(addr.disp);
    }
  } else if (addr.mode == AM::IMM) {
    this->regs.pc = addr.disp;
  } else {
    throw std::logic_error("Incorrect addressing mode for jmp instruction");
  }
}

std::string MCS6502Emulator::DisassemblyState::on_jsr(const DecodedAddress& addr) {
  this->add_jump_target_address(addr, true);
  return std::format("jsr       {}", addr.str());
}
void MCS6502Emulator::on_jsr(const DecodedAddress& addr) {
  if (addr.mode == AM::IMM) {
    this->push16(this->regs.pc - 1); // rts increments this value before jumping there
    this->regs.pc = addr.disp;
  } else {
    throw std::logic_error("Incorrect addressing mode for jmp instruction");
  }
}

std::string MCS6502Emulator::DisassemblyState::on_lda(const DecodedAddress& addr) {
  return std::format("lda       {}", addr.str());
}
void MCS6502Emulator::on_lda(const DecodedAddress& addr) {
  this->regs.a = this->read_from_address(addr);
  this->regs.p.set_z(this->regs.a == 0);
  this->regs.p.set_n(this->regs.a & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_ldx(const DecodedAddress& addr) {
  return std::format("ldx       {}", addr.str());
}
void MCS6502Emulator::on_ldx(const DecodedAddress& addr) {
  this->regs.x = this->read_from_address(addr);
  this->regs.p.set_z(this->regs.x == 0);
  this->regs.p.set_n(this->regs.x & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_ldy(const DecodedAddress& addr) {
  return std::format("ldy       {}", addr.str());
}
void MCS6502Emulator::on_ldy(const DecodedAddress& addr) {
  this->regs.y = this->read_from_address(addr);
  this->regs.p.set_z(this->regs.y == 0);
  this->regs.p.set_n(this->regs.y & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_lsr(const DecodedAddress& addr) {
  return std::format("lsr       {}", addr.str());
}
void MCS6502Emulator::on_lsr(const DecodedAddress& addr) {
  uint8_t v = this->read_from_address(addr);
  this->write_to_address(addr, v);
  this->regs.p.set_c(v & 1);
  v >>= 1;
  this->write_to_address(addr, v);
  this->regs.p.set_z(v == 0);
  this->regs.p.set_n(false);
}

std::string MCS6502Emulator::DisassemblyState::on_nop() {
  return "nop";
}
void MCS6502Emulator::on_nop() {}

std::string MCS6502Emulator::DisassemblyState::on_ora(const DecodedAddress& addr) {
  return std::format("ora       {}", addr.str());
}
void MCS6502Emulator::on_ora(const DecodedAddress& addr) {
  this->regs.a |= this->read_from_address(addr);
  this->regs.p.set_z(this->regs.a == 0);
  this->regs.p.set_n(this->regs.a & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_pha() {
  return "pha";
}
void MCS6502Emulator::on_pha() {
  this->push8(this->regs.a);
}

std::string MCS6502Emulator::DisassemblyState::on_php() {
  return "php";
}
void MCS6502Emulator::on_php() {
  this->push8(this->regs.p.u | 0x30);
}

std::string MCS6502Emulator::DisassemblyState::on_pla() {
  return "pla";
}
void MCS6502Emulator::on_pla() {
  this->regs.a = this->pop8();
}

std::string MCS6502Emulator::DisassemblyState::on_plp() {
  return "plp";
}
void MCS6502Emulator::on_plp() {
  this->regs.p.u = (this->regs.p.u & 0x30) | (this->pop8() & 0xCF);
}

std::string MCS6502Emulator::DisassemblyState::on_rol(const DecodedAddress& addr) {
  return std::format("rol       {}", addr.str());
}
void MCS6502Emulator::on_rol(const DecodedAddress& addr) {
  uint8_t v = this->read_from_address(addr);
  this->write_to_address(addr, v);
  bool old_c = this->regs.p.get_c();
  this->regs.p.set_c(v & 0x80);
  v = (v << 1) | old_c;
  this->write_to_address(addr, v);
  this->regs.p.set_z(v == 0);
  this->regs.p.set_n(v & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_ror(const DecodedAddress& addr) {
  return std::format("ror       {}", addr.str());
}
void MCS6502Emulator::on_ror(const DecodedAddress& addr) {
  uint8_t v = this->read_from_address(addr);
  this->write_to_address(addr, v);
  bool old_c = this->regs.p.get_c();
  this->regs.p.set_c(v & 1);
  v = (v >> 1) | (old_c << 7);
  this->write_to_address(addr, v);
  this->regs.p.set_z(v == 0);
  this->regs.p.set_n(v & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_rti() {
  return "rti";
}
void MCS6502Emulator::on_rti() {
  this->regs.p.u = (this->regs.p.u & 0x30) | (this->pop8() & 0xCF);
  this->regs.pc = this->pop16();
}

std::string MCS6502Emulator::DisassemblyState::on_rts() {
  return "rts";
}
void MCS6502Emulator::on_rts() {
  this->regs.pc = this->pop16() + 1;
}

std::string MCS6502Emulator::DisassemblyState::on_sbc(const DecodedAddress& addr) {
  return std::format("sbc       {}", addr.str());
}
void MCS6502Emulator::on_sbc(const DecodedAddress& addr) {
  this->on_adc_sbc(addr, true);
}

std::string MCS6502Emulator::DisassemblyState::on_sec() {
  return "sec";
}
void MCS6502Emulator::on_sec() {
  this->regs.p.set_c(true);
}

std::string MCS6502Emulator::DisassemblyState::on_sed() {
  return "sed";
}
void MCS6502Emulator::on_sed() {
  this->regs.p.set_d(true);
}

std::string MCS6502Emulator::DisassemblyState::on_sei() {
  return "sei";
}
void MCS6502Emulator::on_sei() {
  this->regs.p.set_i(true);
}

std::string MCS6502Emulator::DisassemblyState::on_sta(const DecodedAddress& addr) {
  return std::format("sta       {}", addr.str());
}
void MCS6502Emulator::on_sta(const DecodedAddress& addr) {
  this->write_to_address(addr, this->regs.a);
}

std::string MCS6502Emulator::DisassemblyState::on_stx(const DecodedAddress& addr) {
  return std::format("stx       {}", addr.str());
}
void MCS6502Emulator::on_stx(const DecodedAddress& addr) {
  this->write_to_address(addr, this->regs.x);
}

std::string MCS6502Emulator::DisassemblyState::on_sty(const DecodedAddress& addr) {
  return std::format("sty       {}", addr.str());
}
void MCS6502Emulator::on_sty(const DecodedAddress& addr) {
  this->write_to_address(addr, this->regs.y);
}

std::string MCS6502Emulator::DisassemblyState::on_tax() {
  return "tax";
}
void MCS6502Emulator::on_tax() {
  this->regs.x = this->regs.a;
  this->regs.p.set_z(this->regs.x == 0);
  this->regs.p.set_n(this->regs.x & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_tay() {
  return "tay";
}
void MCS6502Emulator::on_tay() {
  this->regs.y = this->regs.a;
  this->regs.p.set_z(this->regs.y == 0);
  this->regs.p.set_n(this->regs.y & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_tsx() {
  return "tsx";
}
void MCS6502Emulator::on_tsx() {
  this->regs.x = this->regs.s;
  this->regs.p.set_z(this->regs.x == 0);
  this->regs.p.set_n(this->regs.x & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_txa() {
  return "txa";
}
void MCS6502Emulator::on_txa() {
  this->regs.a = this->regs.x;
  this->regs.p.set_z(this->regs.a == 0);
  this->regs.p.set_n(this->regs.a & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_txs() {
  return "txs";
}
void MCS6502Emulator::on_txs() {
  this->regs.s = this->regs.x;
  this->regs.p.set_z(this->regs.s == 0);
  this->regs.p.set_n(this->regs.s & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_tya() {
  return "tya";
}
void MCS6502Emulator::on_tya() {
  this->regs.a = this->regs.y;
  this->regs.p.set_z(this->regs.a == 0);
  this->regs.p.set_n(this->regs.a & 0x80);
}

std::string MCS6502Emulator::DisassemblyState::on_xor(const DecodedAddress& addr) {
  return std::format("xor       {}", addr.str());
}
void MCS6502Emulator::on_xor(const DecodedAddress& addr) {
  this->regs.a ^= this->read_from_address(addr);
  this->regs.p.set_z(this->regs.a == 0);
  this->regs.p.set_n(this->regs.a & 0x80);
}

std::string MCS6502Emulator::disassemble_one(DisassemblyState& s) {
  size_t opcode_offset = s.r.where();
  std::string opcode_disassembly;

  s.opcode_start_address = s.start_address + s.r.where();
  try {
    opcode_disassembly = MCS6502Emulator::decode_instruction(s);
  } catch (const std::out_of_range&) {
    s.prev_was_valid = false;
    opcode_disassembly = ".incomplete";
  }

  size_t end_offset = s.r.where();
  if (end_offset <= opcode_offset) {
    throw std::logic_error(std::format("disassembly did not advance; used {:X}/{:X} bytes", s.r.where(), s.r.size()));
  }
  std::string hex_data = phosg::format_data_string(
      s.r.pread(opcode_offset, end_offset - opcode_offset), nullptr, phosg::FormatDataStringFlags::HEX_ONLY);
  if (hex_data.size() < 10) {
    hex_data.resize(10, ' ');
  }

  return std::format("{:<8s}  {}", hex_data, opcode_disassembly);
}

DisassembleResult MCS6502Emulator::disassemble_one_structured(DisassemblyState& s) {
  DisassembleResult ret;

  size_t opcode_offset = s.r.where();
  s.opcode_start_address = s.start_address + opcode_offset;

  std::string disassembly;
  try {
    disassembly = MCS6502Emulator::decode_instruction(s);
  } catch (const std::out_of_range&) {
    s.prev_was_valid = false;
    disassembly = ".incomplete";
  }
  ret.segments.emplace_back(DisassembleResult::Segment{
      .is_valid = s.prev_was_valid,
      .address = s.opcode_start_address,
      .size = s.r.where() - opcode_offset,
      .disassembly = std::move(disassembly)});

  return ret;
}

std::string MCS6502Emulator::disassemble_one(const void* vdata, size_t size, uint32_t start_address) {
  DisassemblyState s(vdata, size, start_address);
  return MCS6502Emulator::disassemble_one(s);
}

DisassembleResult MCS6502Emulator::disassemble_one_structured(
    const void* vdata, size_t size, uint32_t start_address) {
  DisassemblyState s(vdata, size, start_address);
  return MCS6502Emulator::disassemble_one_structured(s);
}

std::string MCS6502Emulator::disassemble(
    const void* vdata, size_t size, uint32_t start_address, const std::multimap<uint32_t, std::string>* labels) {
  static const std::multimap<uint32_t, std::string> empty_labels_map = {};
  if (!labels) {
    labels = &empty_labels_map;
  }

  std::map<uint32_t, std::pair<std::string, uint32_t>> lines; // {pc: (line, next_pc)}

  // Phase 1: Generate the disassembly for each opcode, and collect branch target addresses
  DisassemblyState s(vdata, size, start_address);
  while (!s.r.eof()) {
    s.opcode_start_address = s.r.where() + s.start_address;
    std::string line = std::format("{:08X} ", s.opcode_start_address);
    line += MCS6502Emulator::disassemble_one(s);
    line += '\n';
    uint32_t next_pc = s.r.where() + s.start_address;
    lines.emplace(s.opcode_start_address, std::make_pair(std::move(line), next_pc));
  }

  // TODO: Should we implement backups, like M68KEmulator does? (Or can we factor out disassemble() into EmulatorBase?)

  // Phase 2: generate output lines, including passed-in labels, branch target labels, and alternate branches
  size_t ret_bytes = 0;
  std::deque<std::string> ret_lines;
  auto branch_target_it = s.branch_target_addresses.lower_bound(s.start_address);
  auto label_it = labels->lower_bound(s.start_address);

  for (auto line_it = lines.begin(); line_it != lines.end(); line_it = lines.find(line_it->second.second)) {
    uint32_t pc = line_it->first;
    const std::string& line = line_it->second.first;
    for (; label_it != labels->end() && label_it->first <= pc; label_it++) {
      std::string label;
      if (label_it->first != pc) {
        label = std::format("{}: // at {:08X} (misaligned)\n", label_it->second, label_it->first);
      } else {
        label = std::format("{}:\n", label_it->second);
      }
      ret_bytes += label.size();
      ret_lines.emplace_back(std::move(label));
    }
    for (; (branch_target_it != s.branch_target_addresses.end()) && (branch_target_it->first <= pc);
        branch_target_it++) {
      std::string label;
      const char* label_type = branch_target_it->second ? "fn" : "label";
      if (branch_target_it->first != pc) {
        label = std::format("{}{:08X}: // (misaligned)\n", label_type, branch_target_it->first);
      } else {
        label = std::format("{}{:08X}:\n", label_type, branch_target_it->first);
      }
      ret_bytes += label.size();
      ret_lines.emplace_back(std::move(label));
    }

    ret_bytes += line.size();
    // TODO: we can eliminate this copy by making ret_lines instead keep references into the lines map. We can't just
    // move the line contents into ret_lines here because disassembly lines may appear multiple times in the output.
    // (Technically this should not be true, but I'm too lazy to verify as such right now.)
    ret_lines.emplace_back(line);
  }

  // Phase 3: assemble the output lines into a single string and return it
  std::string ret;
  ret.reserve(ret_bytes);
  for (const auto& line : ret_lines) {
    ret += line;
  }
  return ret;
}

AssembleResult MCS6502Emulator::assemble(
    const std::string& text, std::function<std::string(const std::string&)> get_include, uint32_t start_address) {
  (void)text;
  (void)get_include;
  (void)start_address;
  throw std::logic_error("6502 assembly is not implemented");
}

void MCS6502Emulator::execute_one() {
  throw std::logic_error("6502 execution is not implemented");
}

void MCS6502Emulator::execute() {
  throw std::logic_error("6502 execution is not implemented");
}

} // namespace ResourceDASM
