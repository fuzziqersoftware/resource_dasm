#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

#include "DataCodecs/Codecs.hh"

void print_usage() {
  phosg::fwrite_fmt(stderr, "\
Usage: data_decomp [options] [input-filename [output-filename]]\n\
\n\
If input-filename is omitted or is '-', read from stdin.\n\
\n\
If output-filename is omitted or is '-', write to <input-filename>.dec; if\n\
input-filename is also omitted or is '-', write to stdout.\n\
\n\
Format options (one of the following must be given):\n\
  --pack-bits\n\
      Compress data using the PackBits algorithm.\n\
  --unpack-bits\n\
      Decompress data using the PackBits algorithm.\n\
  --unpack-pathways\n\
      Decompress data using Bungie\'s variant of the PackBits algorithm.\n\
  --crypt-odyssey\n\
      Encrypt or decrypt (the operation is symmetric) data in the Odyssey: The\n\
      Legend of Nemesis format.\n\
  --dinopark\n\
      Decompress data using DinoPark Tycoon\'s LZSS encoding. If the input is\n\
      not compressed with this encoding, write the raw input data directly to\n\
      the output.\n\
  --presage\n\
      Decompress data using Presage LZSS encoding (used in Flashback, Lemmings,\n\
      and Prince of Persia).\n\
  --macski\n\
      Decompress data using MacSki\'s COOK, CO2K, or RUN4 encodings. If the\n\
      input is not compressed with one of these encodings, write the raw input\n\
      data directly to the output.\n\
  --sms\n\
      Decompress data using SoundMusicSys LZSS encoding.\n\
");
}

enum class Encoding {
  MISSING = 0,
  SOUNDMUSICSYS,
  MACSKI,
  PRESAGE_LZSS,
  DINOPARK_TYCOON,
  UNPACK_PATHWAYS,
  PACK_BITS,
  UNPACK_BITS,
  CRYPT_ODYSSEY,
};

int main(int argc, char** argv) {
  const char* input_filename = nullptr;
  const char* output_filename = nullptr;
  Encoding encoding = Encoding::MISSING;
  for (int z = 1; z < argc; z++) {
    if (!strcmp(argv[z], "--dinopark")) {
      encoding = Encoding::DINOPARK_TYCOON;
    } else if (!strcmp(argv[z], "--presage")) {
      encoding = Encoding::PRESAGE_LZSS;
    } else if (!strcmp(argv[z], "--macski")) {
      encoding = Encoding::MACSKI;
    } else if (!strcmp(argv[z], "--sms")) {
      encoding = Encoding::SOUNDMUSICSYS;
    } else if (!strcmp(argv[z], "--unpack-pathways")) {
      encoding = Encoding::UNPACK_PATHWAYS;
    } else if (!strcmp(argv[z], "--pack-bits")) {
      encoding = Encoding::PACK_BITS;
    } else if (!strcmp(argv[z], "--unpack-bits")) {
      encoding = Encoding::UNPACK_BITS;
    } else if (!strcmp(argv[z], "--crypt-odyssey")) {
      encoding = Encoding::CRYPT_ODYSSEY;
    } else if (!input_filename) {
      input_filename = argv[z];
    } else if (!output_filename) {
      output_filename = argv[z];
    } else {
      phosg::fwrite_fmt(stderr, "excess command-line argument: {}\n", argv[z]);
      print_usage();
      return 2;
    }
  }

  if (encoding == Encoding::MISSING) {
    print_usage();
    return 2;
  }

  std::string input_data;
  if (!input_filename || !strcmp(input_filename, "-")) {
    input_data = phosg::read_all(stdin);
  } else {
    input_data = phosg::load_file(input_filename);
  }

  std::string decoded;
  switch (encoding) {
    case Encoding::MISSING:
      throw std::logic_error("this case should have been handled earlier");
    case Encoding::SOUNDMUSICSYS:
      decoded = ResourceDASM::decompress_soundmusicsys_lzss(input_data);
      break;
    case Encoding::MACSKI:
      decoded = ResourceDASM::decompress_macski_multi(input_data);
      break;
    case Encoding::PRESAGE_LZSS:
      decoded = ResourceDASM::decompress_presage_lzss(input_data);
      break;
    case Encoding::DINOPARK_TYCOON:
      decoded = ResourceDASM::decompress_dinopark_tycoon_data(input_data);
      break;
    case Encoding::UNPACK_PATHWAYS:
      decoded = ResourceDASM::unpack_pathways(input_data);
      break;
    case Encoding::PACK_BITS:
      decoded = ResourceDASM::pack_bits(input_data);
      break;
    case Encoding::UNPACK_BITS:
      decoded = ResourceDASM::unpack_bits(input_data);
      break;
    case Encoding::CRYPT_ODYSSEY:
      decoded = ResourceDASM::decrypt_encrypt_odyssey(input_data);
      break;
  }

  if (!output_filename || !strcmp(output_filename, "-")) {
    if (!input_filename || !strcmp(input_filename, "-")) {
      phosg::fwritex(stdout, decoded);
    } else {
      std::string filename = input_filename;
      filename += ".dec";
      phosg::save_file(filename, decoded);
    }
  } else {
    phosg::save_file(output_filename, decoded);
  }

  return 0;
}
