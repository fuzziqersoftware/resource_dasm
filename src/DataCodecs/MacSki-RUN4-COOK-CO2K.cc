#include "Codecs.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

using namespace std;



string decompress_macski_RUN4(const void* vdata, size_t size) {
  fprintf(stderr, "decompressing %zu bytes of RUN4\n", size);

  StringReader r(vdata, size);

  if (r.get_u32b() != 0x52554E34) { // 'RUN4'
    throw invalid_argument("data is not RUN4 compressed");
  }
  uint32_t decompressed_size = r.get_u32b();

  uint8_t repeat_3_command = r.get_u8();
  uint8_t repeat_4_command = r.get_u8();
  uint8_t repeat_5_command = r.get_u8();
  uint8_t repeat_var_command = r.get_u8();

  string ret;
  while (ret.size() < decompressed_size) {
    uint8_t command = r.get_u8();
    size_t count;

    if (command == repeat_3_command) {
      count = 3;
      command = r.get_u8();
    } else if (command == repeat_4_command) {
      count = 4;
      command = r.get_u8();
    } else if (command == repeat_5_command) {
      count = 5;
      command = r.get_u8();
    } else if (command == repeat_var_command) {
      count = r.get_u8();
      command = r.get_u8();
    } else {
      count = 1;
    }

    for (; count != 0; count--) {
      ret += static_cast<char>(command);
    }

    if (ret.size() > decompressed_size) {
      throw runtime_error("decompression produced too much data");
    }
  }

  return ret;
}

string decompress_macski_RUN4(const string& data) {
  return decompress_macski_RUN4(data.data(), data.size());
}

string decompress_macski_COOK_CO2K(const void* vdata, size_t size) {
  fprintf(stderr, "decompressing %zu bytes of COOK/CO2K\n", size);

  StringReader r(vdata, size);
  uint32_t type = r.get_u32b();
  if ((type != 0x434F324B) && (type != 0x434F4F4B)) { // 'CO2K' or 'COOK'
    throw invalid_argument("data is not COOK or CO2K compressed");
  }
  bool is_CO2K = (type == 0x434F324B);

  uint32_t decompressed_size = r.get_u32b();

  uint8_t copy_3_command;
  uint8_t copy_4_command;
  uint8_t copy_5_command;
  uint8_t copy_var_command;
  uint8_t copy_4_command_far;
  uint8_t copy_5_command_far;
  uint8_t copy_command_far;

  if (is_CO2K) {
    uint8_t version = r.get_u8();
    if (version < 1) {
      throw invalid_argument("version 0 is not valid");
    }
    if (version > 2) {
      throw invalid_argument("versions beyond 2 not supported");
    }

    if (version <= 1) {
      is_CO2K = false;
    } else {
      copy_command_far = r.get_u8();
      copy_5_command_far = r.get_u8();
      copy_4_command_far = r.get_u8();
    }
  }

  copy_3_command = r.get_u8();
  copy_4_command = r.get_u8();
  copy_5_command = r.get_u8();
  copy_var_command = r.get_u8();

  if (!is_CO2K) {
    copy_command_far = copy_5_command_far = copy_4_command_far = copy_var_command;
  }

  string ret;
  while (ret.size() < decompressed_size) {
    uint8_t command = r.get_u8();
    uint32_t size;

    if (command == copy_3_command) {
      size = 3;

    } else if ((command == copy_var_command) || (command == copy_command_far)) {
      size = r.get_u8();

    } else if (command == copy_4_command) {
      size = 4;

    } else if (command == copy_5_command) {
      size = 5;

    } else if (command == copy_4_command_far) {
      if (r.get_u8(false) == 0) {
        r.skip(1);
        size = 0;
      } else {
        size = 4;
      }

    } else if (command == copy_5_command_far) {
      if (r.get_u8(false) == 0) {
        r.skip(1);
        size = 0;
      } else {
        size = 5;
      }

    } else {
      size = 0;
    }

    if (size == 0) {
      ret += static_cast<char>(command);
      continue;
    }

    uint32_t offset = 0;
    if (is_CO2K && ((command == copy_4_command_far) || (command == copy_5_command_far) || (command == copy_command_far))) {
      offset = r.get_u8() << 8;
    }
    offset += r.get_u8();

    if (offset != 0) {
      if (offset > ret.size()) {
        throw runtime_error("backreference out of bounds");
      }
      const uint8_t* src = reinterpret_cast<const uint8_t*>(ret.data()) + ret.size() - offset;
      for (; size > 0; size--) {
        ret += static_cast<char>(*(src++));
      }
    } else {
      ret += static_cast<char>(command);
    }
  }

  if (ret.size() > decompressed_size) {
    throw runtime_error("decompression produced too much data");
  }

  return ret;
}

string decompress_macski_COOK_CO2K(const string& data) {
  return decompress_macski_COOK_CO2K(data.data(), data.size());
}



typedef string (*decomp_fn_ptr_t)(const void*, size_t);

static decomp_fn_ptr_t get_decompressor(const void* data, size_t size) {
  if (size < 4) {
    return nullptr;
  }
  uint32_t type = *reinterpret_cast<const be_uint32_t*>(data);
  if (type == 0x52554E34) {
    return decompress_macski_RUN4;
  }
  if ((type == 0x434F4F4B) || (type == 0x434F324B)) {
    return decompress_macski_COOK_CO2K;
  }
  return nullptr;
}

string decompress_macski_multi(const void* data, size_t size) {
  string ret;
  bool decompressed = false;
  while (auto decomp = get_decompressor(ret.data(), ret.size())) {
    ret = decomp(data, size);
    data = ret.data();
    size = ret.size();
    decompressed = true;
  }
  if (decompressed) {
    return ret;
  } else {
    return string(reinterpret_cast<const char*>(data), size);
  }
}

string decompress_macski_multi(const string& data) {
  string ret = data;
  while (auto decomp = get_decompressor(ret.data(), ret.size())) {
    ret = decomp(ret.data(), ret.size());
  }
  return ret;
}
