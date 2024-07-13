#include "System.hh"

#include <stdint.h>

#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace ResourceDASM {

static uint32_t decode_int_1_63(BitReader& r) {
  // Decodes an integer in the range 1-63. Input => output map:
  // 0 => 1
  // 100 => 2
  // 101 => 3
  // 110xx => 4 + x (4-7)
  // 1110xxx => 8 + x (8-15)
  // 11110xxyy => 16 + x.y (16-31)
  // 11111xxyyy => 32 + x.y (32-63)

  if (r.read(1) == 0) {
    return 1;
  }

  switch (r.read(2)) {
    case 0:
      return 2;
    case 1:
      return 3;
    case 2:
      return r.read(2) + 4;
    case 3: {
      uint16_t which = r.read(4);
      if (which < 8) {
        return which + 8;
      } else if (which < 12) {
        return r.read(2) + ((which - 0x08) << 2) + 0x10;
      } else {
        return r.read(3) + ((which - 0x0C) << 3) + 0x20;
      }
    }
    default:
      throw logic_error("impossible case");
  }
}

static uint32_t decode_int_0_2042(BitReader& r) {
  // 0x => x (0 or 1)
  // 100 => 2
  // 101x => 3 + x (3 or 4)
  // 1100x => 5 + x
  // 1101xx => 7 + x
  // 1110xxx => 11 + x
  // 11110xxx => 19 + x
  // 111110xxxxx => 27 + x
  // 1111110xxxxxx => 59 + x
  // 11111110xxxxxxx => 123 + x
  // 111111110xxxxxxxx => 251 + x
  // 1111111110xxxxxxxxx => 507 + x
  // 11111111110xxxxxxxxxx => 1019 + x

  size_t which;
  for (which = 0; (which < 10) && r.read(1); which++)
    ;
  switch (which) {
    case 0:
      return r.read(1);
    case 1:
      if (r.read(1) == 0) {
        return 2;
      } else {
        return r.read(1) + 3;
      }
    case 2:
      if (r.read(1) == 0) {
        return r.read(1) + 5;
      } else {
        return r.read(2) + 7;
      }
    case 3:
      return r.read(3) + 11;
    case 4:
      return r.read(3) + 19;
    case 5:
      return r.read(5) + 27;
    case 6:
      return r.read(6) + 59;
    case 7:
      return r.read(7) + 123;
    case 8:
      return r.read(8) + 251;
    case 9:
      return r.read(9) + 507;
    case 10:
      return r.read(10) + 1019;
    default:
      throw logic_error("impossible case");
  }
}

static uint32_t read_int_max_15(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return 1;
  }
  if (r.read(1) == 0) {
    return r.read(2) + 2;
  }
  if (max_value <= 7) {
    return r.read(1) + 6;
  } else if (max_value <= 9) {
    return r.read(2) + 6;
  } else if (max_value <= 13) {
    return r.read(3) + 6;
  } else if (max_value <= 21) {
    return r.read(4) + 6;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_2A(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(1) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(3) + 3;
  }
  if (max_value <= 12) {
    return r.read(1) + 11;
  } else if (max_value <= 14) {
    return r.read(2) + 11;
  } else if (max_value <= 18) {
    return r.read(3) + 11;
  } else if (max_value <= 26) {
    return r.read(4) + 11;
  } else if (max_value <= 42) {
    return r.read(5) + 11;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_54(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(2) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(4) + 5;
  }
  if (max_value <= 0x16) {
    return r.read(1) + 0x15;
  } else if (max_value <= 0x18) {
    return r.read(2) + 0x15;
  } else if (max_value <= 0x1C) {
    return r.read(3) + 0x15;
  } else if (max_value <= 0x24) {
    return r.read(4) + 0x15;
  } else if (max_value <= 0x34) {
    return r.read(5) + 0x15;
  } else if (max_value <= 0x54) {
    return r.read(6) + 0x15;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_A8(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(3) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(5) + 9;
  }
  if (max_value <= 0x2A) {
    return r.read(1) + 0x29;
  } else if (max_value <= 0x2C) {
    return r.read(2) + 0x29;
  } else if (max_value <= 0x30) {
    return r.read(3) + 0x29;
  } else if (max_value <= 0x38) {
    return r.read(4) + 0x29;
  } else if (max_value <= 0x48) {
    return r.read(5) + 0x29;
  } else if (max_value <= 0x68) {
    return r.read(6) + 0x29;
  } else if (max_value <= 0xA8) {
    return r.read(7) + 0x29;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_150(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(4) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(6) + 0x11;
  }
  if (max_value <= 0x52) {
    return r.read(1) + 0x51;
  } else if (max_value <= 0x54) {
    return r.read(2) + 0x51;
  } else if (max_value <= 0x58) {
    return r.read(3) + 0x51;
  } else if (max_value <= 0x60) {
    return r.read(4) + 0x51;
  } else if (max_value <= 0x70) {
    return r.read(5) + 0x51;
  } else if (max_value <= 0x90) {
    return r.read(6) + 0x51;
  } else if (max_value <= 0xD0) {
    return r.read(7) + 0x51;
  } else if (max_value <= 0x150) {
    return r.read(8) + 0x51;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_2A0(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(5) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(7) + 0x21;
  }
  if (max_value <= 0xA2) {
    return r.read(1) + 0xA1;
  } else if (max_value <= 0xA4) {
    return r.read(2) + 0xA1;
  } else if (max_value <= 0xA8) {
    return r.read(3) + 0xA1;
  } else if (max_value <= 0xB0) {
    return r.read(4) + 0xA1;
  } else if (max_value <= 0xC0) {
    return r.read(5) + 0xA1;
  } else if (max_value <= 0xE0) {
    return r.read(6) + 0xA1;
  } else if (max_value <= 0x120) {
    return r.read(7) + 0xA1;
  } else if (max_value <= 0x1A0) {
    return r.read(8) + 0xA1;
  } else if (max_value <= 0x2A0) {
    return r.read(9) + 0xA1;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_540(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(6) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(8) + 0x41;
  }
  if (max_value <= 0x142) {
    return r.read(1) + 0x141;
  } else if (max_value <= 0x144) {
    return r.read(2) + 0x141;
  } else if (max_value <= 0x148) {
    return r.read(3) + 0x141;
  } else if (max_value <= 0x150) {
    return r.read(4) + 0x141;
  } else if (max_value <= 0x160) {
    return r.read(5) + 0x141;
  } else if (max_value <= 0x180) {
    return r.read(6) + 0x141;
  } else if (max_value <= 0x1C0) {
    return r.read(7) + 0x141;
  } else if (max_value <= 0x240) {
    return r.read(8) + 0x141;
  } else if (max_value <= 0x340) {
    return r.read(9) + 0x141;
  } else if (max_value <= 0x540) {
    return r.read(10) + 0x141;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_A80(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(7) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(9) + 0x81;
  }
  if (max_value <= 0x282) {
    return r.read(1) + 0x281;
  } else if (max_value <= 0x284) {
    return r.read(2) + 0x281;
  } else if (max_value <= 0x288) {
    return r.read(4) + 0x281;
  } else if (max_value <= 0x290) {
    return r.read(4) + 0x281;
  } else if (max_value <= 0x2A0) {
    return r.read(5) + 0x281;
  } else if (max_value <= 0x2C0) {
    return r.read(6) + 0x281;
  } else if (max_value <= 0x300) {
    return r.read(7) + 0x281;
  } else if (max_value <= 0x380) {
    return r.read(8) + 0x281;
  } else if (max_value <= 0x480) {
    return r.read(9) + 0x281;
  } else if (max_value <= 0x66C) { // Bug in original code - should be 0x680
    return r.read(10) + 0x281;
  } else if (max_value <= 0xA80) {
    return r.read(11) + 0x281;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_1500(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(8) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(10) + 0x101;
  }
  if (max_value <= 0x502) {
    return r.read(1) + 0x501;
  } else if (max_value <= 0x504) {
    return r.read(2) + 0x501;
  } else if (max_value <= 0x508) {
    return r.read(3) + 0x501;
  } else if (max_value <= 0x510) {
    return r.read(4) + 0x501;
  } else if (max_value <= 0x520) {
    return r.read(5) + 0x501;
  } else if (max_value <= 0x540) {
    return r.read(6) + 0x501;
  } else if (max_value <= 0x580) {
    return r.read(7) + 0x501;
  } else if (max_value <= 0x600) {
    return r.read(8) + 0x501;
  } else if (max_value <= 0x700) {
    return r.read(9) + 0x501;
  } else if (max_value <= 0x900) {
    return r.read(10) + 0x501;
  } else if (max_value <= 0xD00) {
    return r.read(11) + 0x501;
  } else if (max_value <= 0x1500) {
    return r.read(12) + 0x501;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_2A00(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(9) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(11) + 0x201;
  }
  if (max_value <= 0xA02) {
    return r.read(1) + 0xA01;
  } else if (max_value <= 0xA04) {
    return r.read(2) + 0xA01;
  } else if (max_value <= 0xA08) {
    return r.read(3) + 0xA01;
  } else if (max_value <= 0xA10) {
    return r.read(4) + 0xA01;
  } else if (max_value <= 0xA20) {
    return r.read(5) + 0xA01;
  } else if (max_value <= 0xA40) {
    return r.read(6) + 0xA01;
  } else if (max_value <= 0xA80) {
    return r.read(7) + 0xA01;
  } else if (max_value <= 0xB00) {
    return r.read(8) + 0xA01;
  } else if (max_value <= 0xC00) {
    return r.read(9) + 0xA01;
  } else if (max_value <= 0xE00) {
    return r.read(10) + 0xA01;
  } else if (max_value <= 0x1200) {
    return r.read(11) + 0xA01;
  } else if (max_value <= 0x1A00) {
    return r.read(12) + 0xA01;
  } else if (max_value <= 0x2A00) {
    return r.read(13) + 0xA01;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_5400(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(10) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(12) + 0x401;
  }
  if (max_value <= 0x1402) {
    return r.read(1) + 0x1401;
  } else if (max_value <= 0x1404) {
    return r.read(2) + 0x1401;
  } else if (max_value <= 0x1408) {
    return r.read(3) + 0x1401;
  } else if (max_value <= 0x1410) {
    return r.read(4) + 0x1401;
  } else if (max_value <= 0x1420) {
    return r.read(5) + 0x1401;
  } else if (max_value <= 0x1440) {
    return r.read(6) + 0x1401;
  } else if (max_value <= 0x1480) {
    return r.read(7) + 0x1401;
  } else if (max_value <= 0x1500) {
    return r.read(8) + 0x1401;
  } else if (max_value <= 0x1600) {
    return r.read(9) + 0x1401;
  } else if (max_value <= 0x1800) {
    return r.read(10) + 0x1401;
  } else if (max_value <= 0x1C00) {
    return r.read(11) + 0x1401;
  } else if (max_value <= 0x2400) {
    return r.read(12) + 0x1401;
  } else if (max_value <= 0x3400) {
    return r.read(13) + 0x1401;
  } else if (max_value <= 0x5400) {
    return r.read(14) + 0x1401;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_A800(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(11) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(13) + 0x801;
  }
  if (max_value <= 0x2802) {
    return r.read(1) + 0x2801;
  } else if (max_value <= 0x2804) {
    return r.read(2) + 0x2801;
  } else if (max_value <= 0x2808) {
    return r.read(3) + 0x2801;
  } else if (max_value <= 0x2810) {
    return r.read(4) + 0x2801;
  } else if (max_value <= 0x2820) {
    return r.read(5) + 0x2801;
  } else if (max_value <= 0x2840) {
    return r.read(6) + 0x2801;
  } else if (max_value <= 0x2880) {
    return r.read(7) + 0x2801;
  } else if (max_value <= 0x2900) {
    return r.read(8) + 0x2801;
  } else if (max_value <= 0x2A00) {
    return r.read(9) + 0x2801;
  } else if (max_value <= 0x2C00) {
    return r.read(10) + 0x2801;
  } else if (max_value <= 0x3000) {
    return r.read(11) + 0x2801;
  } else if (max_value <= 0x3800) {
    return r.read(12) + 0x2801;
  } else if (max_value <= 0x4800) {
    return r.read(13) + 0x2801;
  } else if (max_value <= 0x6800) {
    return r.read(14) + 0x2801;
  } else if (max_value <= 0xA800) {
    return r.read(15) + 0x2801;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_15000(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(12) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(14) + 0x1001;
  }
  if (max_value <= 0x5002) {
    return r.read(1) + 0x5001;
  } else if (max_value <= 0x5004) {
    return r.read(2) + 0x5001;
  } else if (max_value <= 0x5008) {
    return r.read(3) + 0x5001;
  } else if (max_value <= 0x5010) {
    return r.read(4) + 0x5001;
  } else if (max_value <= 0x5020) {
    return r.read(5) + 0x5001;
  } else if (max_value <= 0x5040) {
    return r.read(6) + 0x5001;
  } else if (max_value <= 0x5080) {
    return r.read(7) + 0x5001;
  } else if (max_value <= 0x5100) {
    return r.read(8) + 0x5001;
  } else if (max_value <= 0x5200) {
    return r.read(9) + 0x5001;
  } else if (max_value <= 0x5400) {
    return r.read(10) + 0x5001;
  } else if (max_value <= 0x5800) {
    return r.read(11) + 0x5001;
  } else if (max_value <= 0x6000) {
    return r.read(12) + 0x5001;
  } else if (max_value <= 0x7000) {
    return r.read(13) + 0x5001;
  } else if (max_value <= 0x9000) {
    return r.read(14) + 0x5001;
  } else if (max_value <= 0xD000) {
    return r.read(15) + 0x5001;
  } else if (max_value <= 0x15000) {
    return r.read(16) + 0x5001;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_2A000(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(13) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(15) + 0x2001;
  }
  if (max_value <= 0xA002) {
    return r.read(1) + 0xA001;
  } else if (max_value <= 0xA004) {
    return r.read(2) + 0xA001;
  } else if (max_value <= 0xA008) {
    return r.read(3) + 0xA001;
  } else if (max_value <= 0xA010) {
    return r.read(4) + 0xA001;
  } else if (max_value <= 0xA020) {
    return r.read(5) + 0xA001;
  } else if (max_value <= 0xA040) {
    return r.read(6) + 0xA001;
  } else if (max_value <= 0xA080) {
    return r.read(7) + 0xA001;
  } else if (max_value <= 0xA100) {
    return r.read(8) + 0xA001;
  } else if (max_value <= 0xA200) {
    return r.read(9) + 0xA001;
  } else if (max_value <= 0xA400) {
    return r.read(10) + 0xA001;
  } else if (max_value <= 0xA800) {
    return r.read(11) + 0xA001;
  } else if (max_value <= 0xB000) {
    return r.read(12) + 0xA001;
  } else if (max_value <= 0xC000) {
    return r.read(13) + 0xA001;
  } else if (max_value <= 0xE000) {
    return r.read(14) + 0xA001;
  } else if (max_value <= 0x12000) {
    return r.read(15) + 0xA001;
  } else if (max_value <= 0x1A000) {
    return r.read(16) + 0xA001;
  } else if (max_value <= 0x2A000) {
    return r.read(17) + 0xA001;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max_54000(uint32_t max_value, BitReader& r) {
  if (r.read(1) == 0) {
    return r.read(14) + 1;
  }
  if (r.read(1) == 0) {
    return r.read(16) + 0x4001;
  }
  if (max_value <= 0x14002) {
    return r.read(1) + 0x14001;
  } else if (max_value <= 0x14004) {
    return r.read(2) + 0x14001;
  } else if (max_value <= 0x14008) {
    return r.read(3) + 0x14001;
  } else if (max_value <= 0x14010) {
    return r.read(4) + 0x14001;
  } else if (max_value <= 0x14020) {
    return r.read(5) + 0x14001;
  } else if (max_value <= 0x14040) {
    return r.read(6) + 0x14001;
  } else if (max_value <= 0x200C) { // Bug in original code - should be 0x14080
    return r.read(7) + 0x14001;
  } else if (max_value <= 0x14100) {
    return r.read(8) + 0x14001;
  } else if (max_value <= 0x14200) {
    return r.read(9) + 0x14001;
  } else if (max_value <= 0x14400) {
    return r.read(10) + 0x14001;
  } else if (max_value <= 0x14800) {
    return r.read(11) + 0x14001;
  } else if (max_value <= 0x15000) {
    return r.read(12) + 0x14001;
  } else if (max_value <= 0x16000) {
    return r.read(13) + 0x14001;
  } else if (max_value <= 0x18000) {
    return r.read(14) + 0x14001;
  } else if (max_value <= 0x1C000) {
    return r.read(15) + 0x14001;
  } else if (max_value <= 0x24000) {
    return r.read(16) + 0x14001;
  } else if (max_value <= 0x34000) {
    return r.read(17) + 0x14001;
  } else if (max_value <= 0x54000) {
    return r.read(18) + 0x14001;
  } else {
    throw logic_error("invalid max value");
  }
}

static uint32_t read_int_max(uint32_t max_value, BitReader& r) {
  if (max_value <= 0x0A) {
    return read_int_max_15(max_value, r);
  } else if (max_value <= 0x14) {
    return read_int_max_2A(max_value, r);
  } else if (max_value <= 0x28) {
    return read_int_max_54(max_value, r);
  } else if (max_value <= 0x50) {
    return read_int_max_A8(max_value, r);
  } else if (max_value <= 0xA0) {
    return read_int_max_150(max_value, r);
  } else if (max_value <= 0x2A0) {
    return read_int_max_2A0(max_value, r);
  } else if (max_value <= 0x3E8) {
    return read_int_max_540(max_value, r);
  } else if (max_value <= 0xA80) {
    return read_int_max_A80(max_value, r);
  } else if (max_value <= 0x1500) {
    return read_int_max_1500(max_value, r);
  } else if (max_value <= 0x2A00) {
    return read_int_max_2A00(max_value, r);
  } else if (max_value <= 0x5400) {
    return read_int_max_5400(max_value, r);
  } else if (max_value <= 0xA800) {
    return read_int_max_A800(max_value, r);
  } else if (max_value <= 0x11170) {
    return read_int_max_15000(max_value, r);
  } else if (max_value <= 0x2A000) {
    return read_int_max_2A000(max_value, r);
  } else {
    return read_int_max_54000(max_value, r);
  }
}

string decompress_system3(
    const CompressedResourceHeader& header,
    const void* source,
    size_t size) {
  BitReader r(source, size * 8);
  StringWriter w;
  w.str().reserve(header.decompressed_size);

  bool stream_block_allowed = true;
  while (w.str().size() < header.decompressed_size) {
    size_t bytes_written_before_command = w.str().size();

    // Decode the next command

    uint32_t backreference_bytes = decode_int_0_2042(r);
    uint32_t backreference_offset = 0;
    uint32_t stream_bytes = 0;

    if ((backreference_bytes <= 0) && stream_block_allowed) {
      stream_bytes = decode_int_1_63(r);
      stream_block_allowed = (stream_bytes >= 0x3F);

    } else {
      backreference_bytes += 2;
      if (!stream_block_allowed) {
        backreference_bytes++;
      }
      stream_block_allowed = true;
      backreference_offset = read_int_max(
          bytes_written_before_command, r);
    }

    // Execute the decoded command

    if (backreference_bytes <= 0) {
      for (; stream_bytes > 0; stream_bytes--) {
        w.put_u8(r.read(8));
      }

    } else {
      if (backreference_offset > w.str().size()) {
        throw runtime_error("backreference beyond beginning of string");
      }
      // It'd be nice to use memcpy or operator+ or something faster here, but:
      // 1. The string can be reallocated and moved during this loop (even
      //    though we called .reserve above), and
      // 2. Backreferences technically can overlap the current end to form a
      //    repeating pattern.
      for (; backreference_bytes > 0; backreference_bytes--) {
        w.put_s8(w.str()[w.str().size() - backreference_offset]);
      }
    }

    if (w.str().size() <= bytes_written_before_command) {
      throw logic_error("decompression did not advance");
    }
  }

  return w.str();
}

} // namespace ResourceDASM
