#include <stdio.h>

#include "IndexFormats/Formats.hh"
#include "ResourceFile.hh"
#include "TextCodecs.hh"
#include <phosg/Filesystem.hh>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace std;


struct InputFile {
  const char*   filename;
  ResourceFile  resources;
  uint32_t      num_deletions;
};


struct Resource {
  InputFile&                                file;
  shared_ptr<const ResourceFile::Resource>  resource;
  bool                                      is_duplicate;
};


static void print_duplicates(int16_t first_id, const string& second_filename, const set<int16_t>& second_ids) {
  fprintf(stderr, "  ID %d: ", first_id);
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
Duplicate resources finder input options:\n\
  --data-fork\n\
      Process the file\'s data fork as if it were the resource fork.\n\
  --target-type=TYPE\n\
      Only check resources of this type (can be given multiple times). To\n\
      specify characters with special meanings or non-ASCII characters in\n\
      either type, escape them as %<hex>. For example, to specify the $\n\
      character in the type, escape it as %24. The % character itself can be\n\
      written as %25.\n\
  --delete\n\
      Delete duplicate resources WITHOUT PROMPTING FOR CONFIRMATION.\n\
  --backup\n\
      Rename the original input file to 'input-filename.bak' before\n\
      writing the new, modified file.\n\
\n", stderr);
}

static uint32_t parse_cli_type(const char* str) {
  char  result[4] = { ' ', ' ', ' ', ' ' };
  for (uint8_t dest_offset = 0; dest_offset < 4 && *str; ++dest_offset) {
    uint8_t ch = *str++;
    if (ch == '%') {
      ch = value_for_hex_char(*str++) << 4;
      ch |= value_for_hex_char(*str++);
    }
    result[dest_offset] = ch;
  }
  
  be_uint32_t  r;
  memcpy(&r, result, 4);
  return r;
}

int main(int argc, const char** argv) {
  try {
    if (argc < 2) {
      print_usage();
      return 2;
    }
    
    // Process command line args
    vector<const char*>     input_filenames;
    set<uint32_t>           input_res_types;
    bool                    use_data_fork = false;
    bool                    delete_duplicates = false;
    bool                    make_backup = false;
    
    for (int x = 1; x < argc; x++) {
      if (!strncmp(argv[x], "--", 2)) {
        if (!strcmp(argv[x], "--data-fork")) {
          use_data_fork = true;
        } else if (!strcmp(argv[x], "--delete")) {
          delete_duplicates = true;
        } else if (!strcmp(argv[x], "--backup")) {
          make_backup = true;
        } else if (!strncmp(argv[x], "--target-type=", 14)) {
          input_res_types.insert(parse_cli_type(&argv[x][14]));
        } else {
          throw invalid_argument(string_printf("unknown option: %s", argv[x]));
        }
      } else {
        input_filenames.push_back(argv[x]);
      }
    }

    if (input_filenames.empty()) {
      print_usage();
      return 2;
    }
    
    // Load resource files
    vector<InputFile>  input_files;
    for (const char* basename : input_filenames) {
      string filename = basename;
      if (!use_data_fork) {
        filename += "/..namedfork/rsrc";
      }
      auto    file_info = stat(filename);
      if (isfile(file_info) && file_info.st_size > 0) {
        input_files.push_back({ basename, parse_resource_fork(load_file(filename)), 0 });
      } else {
        fprintf(stderr, "Input file %s does not exist, is empty or is not a file\n", filename.c_str());
      }
    }
    
    // Gather existing resource types
    if (input_res_types.empty()) {
      for (const InputFile& file : input_files) {
        for (uint32_t type : file.resources.all_resource_types()) {
          input_res_types.insert(type);
        }
      }
    }
    
    // Find duplicates, one resource type at a time
    uint32_t  num_duplicates = 0;
    for (uint32_t res_type : input_res_types) {
      // Hash all resources of type `res_type` and group them by hash
      unordered_map<size_t, vector<Resource>> hashed_resources;
      for (InputFile& file : input_files) {
        for (int16_t res_id : file.resources.all_resources_of_type(res_type)) {
          auto    resource = file.resources.get_resource(res_type, res_id);
          size_t  hash = std::hash<string>()(resource->data);
          hashed_resources[hash].push_back(Resource{ file, resource, false });
        }
      }
    
      // Iterate over the resources and collect duplicates
      // First filename -> first ID -> second filename -> second ID
      map<string, map<int16_t, map<string, set<int16_t>>>> duplicates;
      
      for (auto& [hash, resources] : hashed_resources) {
        // Iterate over all resources with the same hash: they are either
        // duplicates, or their hashes collide. Compare the first resource with
        // those after it, then the second resource with those after it, and so
        // on. Ignore resources that have already been determined to be duplicates
        for (auto first = resources.begin(); first != resources.end(); ++first) {
          if (!first->is_duplicate) {
            for (auto second = next(first); second != resources.end(); ++second) {
              if (!second->is_duplicate) {
                if (first->resource->data == second->resource->data) {
                  duplicates[first->file.filename][first->resource->id][second->file.filename].insert(second->resource->id);
                  
                  if (delete_duplicates) {
                    second->file.resources.remove(second->resource->type, second->resource->id);
                    ++second->file.num_deletions;
                  }
                  
                  ++num_duplicates;
                  
                  // Mark both as duplicates, but still continue to look for duplicates
                  // of the first resource
                  first->is_duplicate = true;
                  second->is_duplicate = true;
                }
              }
            }
          }
        }
      }
      
      // Output duplicates
      if (!duplicates.empty()) {
        string  res_type_str = string_for_resource_type(res_type);
        
        for (const auto& [first_filename, first_ids] : duplicates) {
          fprintf(stderr, "File '%s' has duplicate '%s' resources:\n", first_filename.c_str(), res_type_str.c_str());
          for (const auto& [first_id, second_filenames] : first_ids) {
            // Output duplicates in same file first
            if (auto same_filename = second_filenames.find(first_filename); same_filename != second_filenames.end()) {
              print_duplicates(first_id, "", same_filename->second);
            }
          
            // Output duplicates in other files
            for (const auto& [second_filename, second_ids] : second_filenames) {
              if (second_filename != first_filename) {
                print_duplicates(first_id, second_filename, second_ids);
              }
            }
          }
        }
      }
    }
    
    if (delete_duplicates) {
      for (const InputFile& file : input_files) {
        if (file.num_deletions > 0) {
          string filename = file.filename;
          if (make_backup) {
            rename(filename, filename + ".bak");
          }
          string output_data = serialize_resource_fork(file.resources);

          // Attempting to open the resource fork of a nonexistent file will fail
          // without creating the file, so we touch the file first to make sure it
          // will exist when we write the output.
          (void) fopen_unique(filename, "a+");
          
          if (!use_data_fork) {
            filename += "/..namedfork/rsrc";
          }
          save_file(filename, output_data);
          fprintf(stderr, "Saved file '%s' with %u deletions\n", file.filename, file.num_deletions);
        }
      }
    }
    
    fprintf(stderr, "Found%s %u duplicates\n", delete_duplicates ? " and deleted" : "", num_duplicates);
  } catch (const exception& e) {
    fprintf(stderr, "Error: %s\n", e.what());
    return 1;
  }
}
