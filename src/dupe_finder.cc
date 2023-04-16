#include <stdio.h>

#include "Cli.hh"
#include "IndexFormats/Formats.hh"
#include "ResourceFile.hh"
#include "TextCodecs.hh"
#include <phosg/Filesystem.hh>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace std;

// see sys/paths.h
static constexpr char PATH_RSRCFORKSPEC[] = "/..namedfork/rsrc";

struct InputFile {
  const char* filename;
  ResourceFile resources;
  uint32_t num_deletions;
};

struct Resource {
  InputFile& file;
  shared_ptr<const ResourceFile::Resource> resource;
  bool is_duplicate;
};

static void print_duplicates(int16_t first_id, const string& second_filename, const set<int16_t>& second_ids) {
  fprintf(stderr, "    ID %d: ", first_id);
  bool first = true;
  for (int16_t id : second_ids) {
    if (!first) {
      fprintf(stderr, ", ");
    } else {
      first = false;
    }
    fprintf(stderr, "%d", id);
  }
  if (!second_filename.empty()) {
    fprintf(stderr, " in '%s'", second_filename.c_str());
  }
  fprintf(stderr, "\n");
}

static void print_usage() {
  fputs("\
Usage: dupe_finder [options] input-filename [input-filename...]\n\
\n\
Searches for identical resources of the same type in one or several input\n\
files, and logs and optionally deletes the duplicates. The original is\n\
the resource with the lowest ID in the earliest input file; the others\n\
are duplicates. This means it is possible to influence which resources\n\
are deleted by changing the order of the input files.\n\
\n\
Duplicate resources finder input options:\n\
  --data-fork\n\
      Process the file\'s data fork as if it were the resource fork.\n\
  --target=TYPE[:IDs]\n\
      Only check resources of this type and optionally IDs (can be given\n\
      multiple times). To specify characters with special meanings or\n\
      non-ASCII characters escape them as %<hex>. For example, to specify\n\
      the $ character in the type, escape it as %24. The % character\n\
      itself can be written as %25.\n\
      The optional IDs are a comma-separated list of single IDs or ID\n\
      ranges, where an ID range has the format <min id>..<max id>. Both\n\
      <min id> and <max_id> are optional and default to -32768 and\n\
      32767, respectively. Prefixing an ID [range] with '~' (the tilde)\n\
      excludes instead of includes.\n\
      For example, --target=PICT:128,1000..2000,~1234,..-12345 limits the check\n\
      to PICT resources with IDs -32768 to -12345, 128, and 1000 to 2000,\n\
      except for ID 1234.\n\
      Another example: --target=CODE:~0 exports only CODE resources with\n\
      an ID other than 0.\n\
  --delete\n\
      Delete duplicate resources WITHOUT PROMPTING FOR CONFIRMATION.\n\
  --backup\n\
      Rename the original input file to 'input-filename.bak' before\n\
      writing the new, modified file.\n\
\n",
      stderr);
}

int main(int argc, const char** argv) {
  try {
    if (argc < 2) {
      print_usage();
      return 2;
    }

    // Process command line args
    vector<const char*> input_filenames;
    map<uint32_t, ResourceIDs> input_res_types;
    bool use_data_fork = false;
    bool delete_duplicates = false;
    bool make_backup = false;

    for (int x = 1; x < argc; x++) {
      if (!strncmp(argv[x], "--", 2)) {
        if (!strcmp(argv[x], "--data-fork")) {
          use_data_fork = true;
        } else if (!strcmp(argv[x], "--delete")) {
          delete_duplicates = true;
        } else if (!strcmp(argv[x], "--backup")) {
          make_backup = true;
        } else if (!strncmp(argv[x], "--target=", 9)) {
          ResourceIDs ids(ResourceIDs::Init::NONE);
          uint32_t type = parse_cli_type_ids(&argv[x][9], &ids);
          input_res_types.emplace(type, ids);
        } else {
          fprintf(stderr, "unknown option: %s\n", argv[x]);
          print_usage();
          return 2;
        }
      } else {
        if (find_if(input_filenames.begin(), input_filenames.end(), [&](auto s) { return !strcmp(s, argv[x]); }) == input_filenames.end())
          input_filenames.push_back(argv[x]);
      }
    }

    if (input_filenames.empty()) {
      print_usage();
      return 2;
    }

    // Load resource files
    vector<InputFile> input_files;
    for (const char* basename : input_filenames) {
      string filename = basename;
      if (!use_data_fork) {
        filename += PATH_RSRCFORKSPEC;
      }
      auto file_info = stat(filename);
      if (isfile(file_info) && file_info.st_size > 0) {
        input_files.push_back({basename, parse_resource_fork(load_file(filename)), 0});
      } else {
        fprintf(stderr, "Input file '%s' does not exist, is empty or is not a file\n", filename.c_str());
      }
    }

    // Gather existing resource types, if none were specified on the command line
    if (input_res_types.empty()) {
      for (const InputFile& file : input_files) {
        for (uint32_t type : file.resources.all_resource_types()) {
          input_res_types.emplace(type, ResourceIDs(ResourceIDs::Init::ALL));
        }
      }
    }

    // Find duplicates, one resource type at a time. For this we need to compare
    // every resource with all other resources of the same type, which could be
    // slow with many large resources across several files.
    //
    // Instead, we group the resources by their hash. With a decent hash function,
    // this will result in very small groups. Then we only have to compare
    // resources in a single group to weed out hash collisions: what remains are
    // duplicates.

    uint32_t num_duplicates = 0;
    for (const auto& [res_type, res_ids] : input_res_types) {
      string res_type_str = string_for_resource_type(res_type);
      fprintf(stderr, "Searching for duplicate %s resources with IDs ", res_type_str.c_str()), res_ids.print(stderr, true);

      // 1. Group resources
      unordered_map<size_t, vector<Resource>> hashed_resources;
      for (InputFile& file : input_files) {
        for (int16_t res_id : file.resources.all_resources_of_type(res_type)) {
          if (res_ids[res_id]) {
            auto resource = file.resources.get_resource(res_type, res_id);
            size_t hash = std::hash<string>()(resource->data);
            hashed_resources[hash].push_back({file, resource, /*is_duplicate*/ false});
          }
        }
      }

      // 2. Look for duplicates in each group
      //  first filename -> first ID -> second filename -> second ID
      map<string, map<int16_t, map<string, set<int16_t>>>> duplicates;

      for (auto& [hash, resources] : hashed_resources) {
        // Compare the first resource with those after it, then the second resource
        // with those after it, and so on. Ignore resources that have already been
        // determined to be duplicates
        for (auto first = resources.begin(); first != resources.end(); ++first) {
          if (!first->is_duplicate) {
            for (auto second = next(first); second != resources.end(); ++second) {
              if (!second->is_duplicate) {
                if (first->resource->data == second->resource->data) {
                  duplicates[first->file.filename][first->resource->id][second->file.filename].insert(second->resource->id);

                  if (delete_duplicates) {
                    // Delete duplicate
                    second->file.resources.remove(second->resource->type, second->resource->id);
                    ++second->file.num_deletions;
                  }

                  ++num_duplicates;

                  // Mark resource as duplicate so we don't check it again, then
                  // continue to look for duplicates, as there might be more than
                  // one
                  second->is_duplicate = true;
                }
              }
            }
          }
        }
      }

      // 3. Print duplicates
      if (!duplicates.empty()) {
        for (const auto& [first_filename, first_ids] : duplicates) {
          fprintf(stderr, "  The following %s resources in file '%s' have duplicates:\n", res_type_str.c_str(), first_filename.c_str());
          for (const auto& [first_id, second_filenames] : first_ids) {
            // First output duplicates in same file as the original
            if (auto same_filename = second_filenames.find(first_filename); same_filename != second_filenames.end()) {
              print_duplicates(first_id, "", same_filename->second);
            }

            // Then output duplicates in other files
            for (const auto& [second_filename, second_ids] : second_filenames) {
              if (second_filename != first_filename) {
                print_duplicates(first_id, second_filename, second_ids);
              }
            }
          }
        }
      }
    }

    // If any resources were deleted, write the modified files to disk
    if (delete_duplicates) {
      for (const InputFile& file : input_files) {
        if (file.num_deletions > 0) {
          string filename = file.filename;
          if (make_backup) {
            rename(filename, filename + ".bak");
          }
          string output_data = serialize_resource_fork(file.resources);

          if (!use_data_fork) {
            if (make_backup) {
              // Attempting to open the resource fork of a nonexistent file will fail
              // without creating the file, so we touch the file first to make sure it
              // will exist when we write the output.
              (void)fopen_unique(filename, "a+");
            }
            filename += PATH_RSRCFORKSPEC;
          }
          save_file(filename, output_data);
          fprintf(stderr, "Saved file '%s' with %u deletions\n", file.filename, file.num_deletions);
        }
      }
    }

    fprintf(stderr, "Found%s %u duplicates\n", delete_duplicates ? " and deleted" : "", num_duplicates);

    return 0;
  } catch (const exception& e) {
    fprintf(stderr, "Error: %s\n", e.what());
    return 1;
  }
}
