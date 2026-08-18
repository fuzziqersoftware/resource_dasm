#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <phosg/Arguments.hh>
#include <phosg/Filesystem.hh>

#include "IndexFormats/Formats.hh"
#include "TextCodecs.hh"

int main(int argc, char** argv) {
  phosg::Arguments args(argv + 1, argc - 1);

  std::string input_filename = args.get<std::string>(0, true);
  std::string output_filename = args.get<std::string>(1, false);
  if (output_filename.empty()) {
    if (input_filename.ends_with(".hqx")) {
      output_filename = input_filename.substr(0, input_filename.size() - 4);
    } else {
      output_filename = input_filename + ".dec";
    }
  }
  bool separate = args.get<bool>("separate");

  auto decoded = ResourceDASM::parse_binhex(phosg::load_file(input_filename));
  auto data_f = phosg::fopen_unique(output_filename + (separate ? ".data" : ""), "wb");
  auto rsrc_f = phosg::fopen_unique(output_filename + (separate ? ".rsrc" : "/..namedfork/rsrc"), "wb");
  phosg::fwritex(data_f.get(), decoded.data_fork);
  phosg::fwritex(rsrc_f.get(), decoded.resource_fork);
  phosg::log_info_f("Note: Decoded filename is \"{}\" with type {}, creator {}, Finder flags 0x{:04X}",
      decoded.file_name, ResourceDASM::string_for_resource_type(decoded.file_type),
      ResourceDASM::string_for_resource_type(decoded.creator_code), decoded.finder_flags);
  phosg::log_info_f("{} bytes in data fork, {} bytes in resource fork",
      decoded.data_fork.size(), decoded.resource_fork.size());
  return 0;
}
