#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>

#include "DataDecoders/Decoders.hh"

using namespace std;



void print_usage() {
  fprintf(stderr, "\
Usage: data_decomp [options] [input_filename [output_filename]]\n\
\n\
If input_filename is omitted or is '-', read from stdin.\n\
If output_filename is omitted or is '-', write to stdout.\n\
\n\
Format options (one of the following must be given):\n\
  --dinopark\n\
      Decompress data using DinoPark Tycoon\'s LZSS encoding. If the input is\n\
      not compressed with this encoding, write the raw input data directly to\n\
      the output.\n\
  --flashback\n\
      Decompress data using Flashback\'s LZSS encoding.\n\
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
  FLASHBACK,
  DINOPARK_TYCOON,
};

int main(int argc, char** argv) {
  const char* input_filename = nullptr;
  const char* output_filename = nullptr;
  Encoding encoding = Encoding::MISSING;
  for (int z = 1; z < argc; z++) {
    if (!strcmp(argv[z], "--dinopark")) {
      encoding = Encoding::DINOPARK_TYCOON;
    } else if (!strcmp(argv[z], "--flashback")) {
      encoding = Encoding::FLASHBACK;
    } else if (!strcmp(argv[z], "--macski")) {
      encoding = Encoding::MACSKI;
    } else if (!strcmp(argv[z], "--sms")) {
      encoding = Encoding::SOUNDMUSICSYS;
    } else if (!input_filename) {
      input_filename = argv[z];
    } else if (!output_filename) {
      output_filename = argv[z];
    } else {
      throw invalid_argument("excess command-line argument: " + string(argv[z]));
    }
  }

  if (encoding == Encoding::MISSING) {
    print_usage();
    return 1;
  }

  string input_data;
  if (!input_filename || !strcmp(input_filename, "-")) {
    input_data = read_all(stdin);
  } else {
    input_data = load_file(input_filename);
  }

  string decoded;
  switch (encoding) {
    case Encoding::MISSING:
      throw logic_error("this case should have been handled earlier");
    case Encoding::SOUNDMUSICSYS:
      decoded = decompress_soundmusicsys_lzss(input_data);
      break;
    case Encoding::MACSKI:
      decoded = decompress_macski_multi(input_data);
      break;
    case Encoding::FLASHBACK:
      decoded = decompress_flashback_lzss(input_data);
      break;
    case Encoding::DINOPARK_TYCOON:
      decoded = decompress_dinopark_tycoon_lzss(input_data);
      break;
  }

  if (!output_filename || !strcmp(output_filename, "-")) {
    fwritex(stdout, decoded);
  } else {
    save_file(output_filename, decoded);
  }

  return 0;
}
