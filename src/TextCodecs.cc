#include "TextCodecs.hh"

#include <phosg/Strings.hh>

// MacRoman to UTF-8
static constexpr const char mac_roman_table[0x100][4] = {
  // 00
  // Note: we intentionally incorrectly decode \r as \n here to convert CR line
  // breaks to LF line breaks which modern systems use
  "\x00", "\x01", "\x02", "\x03", "\x04", "\x05", "\x06", "\x07",
  "\x08", "\t", "\n", "\x0B", "\x0C", "\n", "\x0E",  "\x0F",
  // 10
  "\x10", "\xE2\x8C\x98", "\xE2\x87\xA7", "\xE2\x8C\xA5",
  "\xE2\x8C\x83", "\x15", "\x16", "\x17",
  "\x18", "\x19", "\x1A", "\x1B", "\x1C", "\x1D", "\x1E", "\x1F",
  // 20
  " ", "!", "\"", "#", "$", "%", "&", "\'",
  "(", ")", "*", "+", ",", "-", ".", "/",
  // 30
  "0", "1", "2", "3", "4", "5", "6", "7",
  "8", "9", ":", ";", "<", "=", ">", "?",
  // 40
  "@", "A", "B", "C", "D", "E", "F", "G",
  "H", "I", "J", "K", "L", "M", "N", "O",
  // 50
  "P", "Q", "R", "S", "T", "U", "V", "W",
  "X", "Y", "Z", "[", "\\", "]", "^", "_",
  // 60
  "`", "a", "b", "c", "d", "e", "f", "g",
  "h", "i", "j", "k", "l", "m", "n", "o",
  // 70
  "p", "q", "r", "s", "t", "u", "v", "w",
  "x", "y", "z", "{", "|", "}", "~", "\x7F",
  // 80
  "\xC3\x84", "\xC3\x85", "\xC3\x87", "\xC3\x89",
  "\xC3\x91", "\xC3\x96", "\xC3\x9C", "\xC3\xA1",
  "\xC3\xA0", "\xC3\xA2", "\xC3\xA4", "\xC3\xA3",
  "\xC3\xA5", "\xC3\xA7", "\xC3\xA9", "\xC3\xA8",
  // 90
  "\xC3\xAA", "\xC3\xAB", "\xC3\xAD", "\xC3\xAC",
  "\xC3\xAE", "\xC3\xAF", "\xC3\xB1", "\xC3\xB3",
  "\xC3\xB2", "\xC3\xB4", "\xC3\xB6", "\xC3\xB5",
  "\xC3\xBA", "\xC3\xB9", "\xC3\xBB", "\xC3\xBC",
  // A0
  "\xE2\x80\xA0", "\xC2\xB0", "\xC2\xA2", "\xC2\xA3",
  "\xC2\xA7", "\xE2\x80\xA2", "\xC2\xB6", "\xC3\x9F",
  "\xC2\xAE", "\xC2\xA9", "\xE2\x84\xA2", "\xC2\xB4",
  "\xC2\xA8", "\xE2\x89\xA0", "\xC3\x86", "\xC3\x98",
  // B0
  "\xE2\x88\x9E", "\xC2\xB1", "\xE2\x89\xA4", "\xE2\x89\xA5",
  "\xC2\xA5", "\xC2\xB5", "\xE2\x88\x82", "\xE2\x88\x91",
  "\xE2\x88\x8F", "\xCF\x80", "\xE2\x88\xAB", "\xC2\xAA",
  "\xC2\xBA", "\xCE\xA9", "\xC3\xA6", "\xC3\xB8",
  // C0
  "\xC2\xBF", "\xC2\xA1", "\xC2\xAC", "\xE2\x88\x9A",
  "\xC6\x92", "\xE2\x89\x88", "\xE2\x88\x86", "\xC2\xAB",
  "\xC2\xBB", "\xE2\x80\xA6", "\xC2\xA0", "\xC3\x80",
  "\xC3\x83", "\xC3\x95", "\xC5\x92", "\xC5\x93",
  // D0
  "\xE2\x80\x93", "\xE2\x80\x94", "\xE2\x80\x9C", "\xE2\x80\x9D",
  "\xE2\x80\x98", "\xE2\x80\x99", "\xC3\xB7", "\xE2\x97\x8A",
  "\xC3\xBF", "\xC5\xB8", "\xE2\x81\x84", "\xE2\x82\xAC",
  "\xE2\x80\xB9", "\xE2\x80\xBA", "\xEF\xAC\x81", "\xEF\xAC\x82",
  // E0
  "\xE2\x80\xA1", "\xC2\xB7", "\xE2\x80\x9A", "\xE2\x80\x9E",
  "\xE2\x80\xB0", "\xC3\x82", "\xC3\x8A", "\xC3\x81",
  "\xC3\x8B", "\xC3\x88", "\xC3\x8D", "\xC3\x8E",
  "\xC3\x8F", "\xC3\x8C", "\xC3\x93", "\xC3\x94",
  // F0
  "\xEF\xA3\xBF", "\xC3\x92", "\xC3\x9A", "\xC3\x9B",
  "\xC3\x99", "\xC4\xB1", "\xCB\x86", "\xCB\x9C",
  "\xC2\xAF", "\xCB\x98", "\xCB\x99", "\xCB\x9A",
  "\xC2\xB8", "\xCB\x9D", "\xCB\x9B", "\xCB\x87",
};


string decode_mac_roman(const char* data, size_t size, bool for_filename) {
  string ret;
  while (size--) {
    ret += decode_mac_roman(*(data++), for_filename);
  }
  return ret;
}

string decode_mac_roman(const string& data, bool for_filename) {
  return decode_mac_roman(data.data(), data.size(), for_filename);
}

string decode_mac_roman(char data, bool for_filename) {
  if (for_filename && should_escape_mac_roman_filename_char(data)) {
    return "_";
  } else {
    return mac_roman_table[static_cast<uint8_t>(data)];
  }
}


string string_for_resource_type(uint32_t type, bool for_filename) {
  string result;
  for (ssize_t s = 24; s >= 0; s -= 8) {
    uint8_t ch = (type >> s) & 0xFF;
    if (ch < 0x20 || (for_filename && should_escape_mac_roman_filename_char(ch))) {
      result += string_printf("\\x%02hhX", ch);
    } else if (ch == '\\') {
      result += "\\\\";
    } else  {
      result += decode_mac_roman(ch);
    }
  }
  return result;
}

string raw_string_for_resource_type(uint32_t type) {
  string result;
  for (ssize_t s = 24; s >= 0; s -= 8) {
    result += static_cast<char>((type >> s) & 0xFF);
  }
  return result;
}



string unpack_bits(const void* data, size_t size) {
  StringReader r(data, size);
  StringWriter w;

  // Commands:
  // 0CCCCCCC - write (1 + C) bytes directly from the input
  // 1CCCCCCC DDDDDDDD - write (1 - C) bytes of D (C treated as negative number)
  // 10000000 - no-op (for some reason)
  while (!r.eof()) {
    int8_t cmd = r.get_s8();
    if (cmd == -128) {
      continue;
    } else if (cmd < 0) {
      uint8_t v = r.get_u8();
      size_t count = 1 - cmd;
      for (size_t z = 0; z < count; z++) {
        w.put_u8(v);
      }
    } else {
      size_t count = 1 + cmd;
      for (size_t z = 0; z < count; z++) {
        w.put_u8(r.get_u8());
      }
    }
  }

  return move(w.str());
}

string unpack_bits(const string& data) {
  return unpack_bits(data.data(), data.size());
}

string pack_bits(const void* data, size_t size) {
  StringReader r(data, size);
  StringWriter w;

  size_t run_start_offset;
  while (!r.eof()) {
    uint8_t ch = r.get_u8();
    if (r.eof()) {
      // Only one byte left in the input; just write it verbatim
      w.put_u8(0x00);
      w.put_u8(ch);
      break;
    }

    run_start_offset = r.where() - 1;

    if (r.get_u8() == ch) { // Run of same byte
      while (((r.where() - run_start_offset) < 128) &&
             !r.eof() &&
             (r.get_u8(false) == ch)) {
        r.skip(1);
      }
      w.put_u8(1 - (r.where() - run_start_offset));
      w.put_u8(ch);

    } else { // Run of different bytes
      while (((r.where() - run_start_offset) < 128) &&
             !r.eof() &&
             (r.get_u8(false) != ch)) {
        ch = r.get_u8();
      }
      w.put_u8(r.where() - run_start_offset - 1);
      w.write(r.pread(run_start_offset, r.where() - run_start_offset));
    }
  }

  return move(w.str());
}

string pack_bits(const string& data) {
  return pack_bits(data.data(), data.size());
}
