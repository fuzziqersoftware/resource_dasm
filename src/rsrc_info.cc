#include <filesystem>

#include "IndexFormats/Formats.hh"
#include "ResourceFile.hh"
#include "TextCodecs.hh"

static constexpr char PATH_RSRCFORKSPEC[] = "/..namedfork/rsrc";

static void print_usage() {
  fputs("\
Usage: rsrc_info input-filename [input-filename...]\n\
\n\
Prints information about the resources in one or several input files.\n\
\n\
Input options:\n\
  --data-fork\n\
      Process the file\'s data fork as if it were the resource fork.\n\
\n",
      stderr);
}

static uint32_t calc_resource_size_of_type(const ResourceDASM::ResourceFile& file, uint32_t type) {
  uint32_t result = 0;
  for (int16_t id : file.all_resources_of_type(type)) {
    result += file.get_resource(type, id)->data.size();
  }

  return result;
}

static uint32_t calc_resource_size_of_all_types(const ResourceDASM::ResourceFile& file) {
  uint32_t result = 0;
  for (auto [type, id] : file.all_resources()) {
    result += file.get_resource(type, id)->data.size();
  }

  return result;
}

int main(int argc, const char** argv) {
  try {
    if (argc < 2) {
      print_usage();
      return 2;
    }

    // Process command line arguments
    std::vector<const char*> input_filenames;
    bool use_data_fork = false;
    for (int x = 1; x < argc; x++) {
      if (!strncmp(argv[x], "--", 2)) {
        if (!strcmp(argv[x], "--data-fork")) {
          use_data_fork = true;
        } else {
          phosg::fwrite_fmt(stderr, "unknown option: {}\n", argv[x]);
          print_usage();
          return 2;
        }
      } else {
        if (find_if(input_filenames.begin(), input_filenames.end(), [&](auto s) { return !strcmp(s, argv[x]); }) == input_filenames.end())
          input_filenames.push_back(argv[x]);
      }
    }

    // Process resource files
    bool has_too_many = false;
    for (const char* i : input_filenames) {
      std::string input_filename = i;
      if (!use_data_fork) {
        input_filename += PATH_RSRCFORKSPEC;
      }

      // Load resource file
      if (std::filesystem::is_directory(input_filename) || (std::filesystem::file_size(input_filename) == 0)) {
        phosg::fwrite_fmt(stderr, "Input file '{}' does not exist, is empty or is not a file\n", input_filename);
        return 1;
      }

      auto input_file = ResourceDASM::parse_resource_fork(phosg::load_file(input_filename));

      // Print information
      phosg::fwrite_fmt(stdout, "File '{}':\n", input_filename);
      {
        auto all_types = input_file.all_resource_types();
        uint32_t totalRsrcCount = input_file.count_resources();
        uint32_t totalRsrcSize = calc_resource_size_of_all_types(input_file);
        if (totalRsrcCount > 2727)
          has_too_many = true;

        for (uint32_t res_type : all_types) {
          uint32_t count = input_file.count_resources_of_type(res_type);
          uint32_t size = calc_resource_size_of_type(input_file, res_type);

          phosg::fwrite_fmt(stdout, "  {}: {:4} ({:7} bytes)\n",
              ResourceDASM::string_for_resource_type(res_type), count, size);
        }
        phosg::fwrite_fmt(stdout, "  ----------\n");
        phosg::fwrite_fmt(stdout, "        {:4} ({:7} bytes){}\n",
            totalRsrcCount, totalRsrcSize, totalRsrcCount > 2727 ? " ! >2727" : "");
      }
      phosg::fwrite_fmt(stdout, "\n");
    }

    if (has_too_many)
      fputs("\
At least one input file has more than 2727, the maximum amount supported\n\
by MacOS's Resource Manager.\n\
\n",
          stdout);

    return 0;
  } catch (const std::exception& e) {
    phosg::fwrite_fmt(stderr, "Error: {}\n", e.what());
    return 1;
  }
}
