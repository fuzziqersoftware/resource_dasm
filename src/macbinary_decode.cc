#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <phosg/Arguments.hh>
#include <phosg/Filesystem.hh>

#include "IndexFormats/Formats.hh"

int main(int argc, char** argv) {
  phosg::Arguments args(argv + 1, argc - 1);

  std::string input_filename = args.get<std::string>(0, true);
  std::string output_filename = args.get<std::string>(1, false);
  if (output_filename.empty()) {
    if (input_filename.ends_with(".bin")) {
      output_filename = input_filename.substr(0, input_filename.size() - 4);
    } else {
      output_filename = input_filename + ".dec";
    }
  }
  bool separate = args.get<bool>("separate");

  auto decoded = ResourceDASM::parse_macbinary(phosg::load_file(input_filename));
  auto data_f = phosg::fopen_unique(output_filename + (separate ? ".data" : ""), "wb");
  auto rsrc_f = phosg::fopen_unique(output_filename + (separate ? ".rsrc" : "/..namedfork/rsrc"), "wb");
  phosg::fwritex(data_f.get(), decoded.first.getv(decoded.first.size()), decoded.first.size());
  phosg::fwritex(rsrc_f.get(), decoded.second.getv(decoded.second.size()), decoded.second.size());
  return 0;
}
