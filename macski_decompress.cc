#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

using namespace std;



string decompress_RUN4(const void* vdata, size_t size) {
  fprintf(stderr, "decompressing %zu bytes of RUN4\n", size);

  if (size < 0x08) {
    throw invalid_argument("data is too small to be RUN4 compressed");
  }

  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  uint32_t type = bswap32(*reinterpret_cast<const uint32_t*>(data));
  if (type != 0x52554E34) {
    throw invalid_argument("data is not RUN4 compressed");
  }
  uint32_t decompressed_size = bswap32(*reinterpret_cast<const uint32_t*>(data + 4));
  const uint8_t* input = data + 8;

  uint8_t repeat_3_command = *(input++);
  uint8_t repeat_4_command = *(input++);
  uint8_t repeat_5_command = *(input++);
  uint8_t repeat_var_command = *(input++);

  string ret;
  while (ret.size() < decompressed_size) {
    uint8_t command = *(input++);
    size_t count;

    if (command == repeat_3_command) {
      count = 3;
      command = *(input++);
    } else if (command == repeat_4_command) {
      count = 4;
      command = *(input++);
    } else if (command == repeat_5_command) {
      count = 5;
      command = *(input++);
    } else if (command == repeat_var_command) {
      count = *(input++);
      command = *(input++);
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

string decompress_COOK_CO2K(const void* vdata, size_t size) {
  fprintf(stderr, "decompressing %zu bytes of COOK/CO2K\n", size);

  if (size < 0x0C) {
    throw invalid_argument("data is too small to be COOK or CO2K compressed");
  }

  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  uint32_t type = bswap32(*reinterpret_cast<const uint32_t*>(data));
  if ((type != 0x434F324B) && (type != 0x434F4F4B)) {
    throw invalid_argument("data is not COOK or CO2K compressed");
  }
  bool is_CO2K = (type == 0x434F324B);
  uint32_t decompressed_size = bswap32(*reinterpret_cast<const uint32_t*>(data + 4));
  const uint8_t* input = data + 8;

  uint8_t copy_3_command;
  uint8_t copy_4_command;
  uint8_t copy_5_command;
  uint8_t copy_var_command;
  uint8_t copy_4_command_far;
  uint8_t copy_5_command_far;
  uint8_t copy_command_far;

  if (is_CO2K) {
    uint8_t version = *(input++);
    if (version < 1) {
      throw invalid_argument("version 0 is not valid");
    }
    if (version > 2) {
      throw invalid_argument("versions beyond 2 not supported");
    }

    if (version <= 1) {
      is_CO2K = false;
    } else {
      copy_command_far = *(input++);
      copy_5_command_far = *(input++);
      copy_4_command_far = *(input++);
    }
  }

  copy_3_command = *(input++);
  copy_4_command = *(input++);
  copy_5_command = *(input++);
  copy_var_command = *(input++);

  if (!is_CO2K) {
    copy_command_far = copy_5_command_far = copy_4_command_far = copy_var_command;
  }

  string ret;
  while (ret.size() < decompressed_size) {
    uint8_t command = *(input++);
    uint32_t size;

    if (command == copy_3_command) {
      size = 3;

    } else if ((command == copy_var_command) || (command == copy_command_far)) {
      size = *(input++);

    } else if (command == copy_4_command) {
      size = 4;

    } else if (command == copy_5_command) {
      size = 5;

    } else if (command == copy_4_command_far) {
      if (*input == 0) {
        input++;
        size = 0;
      } else {
        size = 4;
      }

    } else if (command == copy_5_command_far) {
      if (*input == 0) {
        input++;
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
      offset = *(input++) << 8;
    }
    offset += *(input++);

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



typedef string (*decomp_fn_ptr_t)(const void*, size_t);

decomp_fn_ptr_t get_decompressor(const void* data, size_t size) {
  if (size < 4) {
    return NULL;
  }
  uint32_t type = bswap32(*reinterpret_cast<const uint32_t*>(data));
  if (type == 0x52554E34) {
    return decompress_RUN4;
  }
  if ((type == 0x434F4F4B) || (type == 0x434F324B)) {
    return decompress_COOK_CO2K;
  }
  return NULL;
}

string decompress_multi(const string& data) {
  string ret = data;
  while (auto decomp = get_decompressor(ret.data(), ret.size())) {
    ret = decomp(ret.data(), ret.size());
  }
  return ret;
}



int main(int argc, char* argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s filename\n", argv[0]);
    return 2;
  }

  string data = load_file(argv[1]);
  string out_filename = string(argv[1]) + ".dec";

  string data_dec = decompress_multi(data);
  save_file(out_filename, data_dec);

  return 0;
}
