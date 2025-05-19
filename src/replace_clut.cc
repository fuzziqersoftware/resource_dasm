#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <string>

#include "ResourceFile.hh"

using namespace std;
using namespace phosg;
using namespace ResourceDASM;

size_t diff(size_t a, size_t b) {
  return (a > b) ? (a - b) : (b - a);
}

void print_usage() {
  fwrite_fmt(stderr, "\
Usage: replace_clut [options] in_clut.bin out_clut.bin [in.bmp [out.bmp]]\n\
\n\
If no BMP filenames are given, read from stdin and write to stdout. You should\n\
redirect stdout to a file because it will contain binary data which will\n\
probably goof up your terminal if it happens to contain escape codes.\n\
\n\
If an input filename is given but no output filename is given, render_bits will\n\
write to a file named <input_filename>.bmp. (The extension is always appended,\n\
so this will not replace the input file - the output file will have a .bmp.bmp\n\
suffix.)\n\
\n\
Options:\n\
  --input-pltt\n\
      Decode the input clut as a pltt resource instead of a clut resource.\n\
  --output-pltt\n\
      Decode the output clut as a pltt resource instead of a clut resource.\n\
\n");
}

int main(int argc, char* argv[]) {
  const char* input_clut_filename = nullptr;
  const char* output_clut_filename = nullptr;
  const char* input_filename = nullptr;
  const char* output_filename = nullptr;
  bool input_pltt = false;
  bool output_pltt = false;
  for (int x = 1; x < argc; x++) {
    if (!strcmp(argv[x], "--input-pltt")) {
      input_pltt = true;
    } else if (!strcmp(argv[x], "--output-pltt")) {
      output_pltt = true;
    } else if (!input_clut_filename) {
      input_clut_filename = argv[x];
    } else if (!output_clut_filename) {
      output_clut_filename = argv[x];
    } else if (!input_filename) {
      input_filename = argv[x];
    } else if (!output_filename) {
      output_filename = argv[x];
    } else {
      print_usage();
      throw invalid_argument("too many command-line arguments");
    }
  }

  if (!input_clut_filename || !output_clut_filename) {
    print_usage();
    throw invalid_argument("one or both clut filenames are missing");
  }

  string data;
  if (input_filename) {
    data = load_file(input_filename);
  } else {
    data = read_all(stdin);
  }

  string input_clut_data = load_file(input_clut_filename);
  auto input_clut = input_pltt
      ? to_color8(ResourceFile::decode_pltt(input_clut_data.data(), input_clut_data.size()))
      : to_color8(ResourceFile::decode_clut(input_clut_data.data(), input_clut_data.size()));
  if (input_clut.empty()) {
    throw invalid_argument("input clut is empty");
  }

  string output_clut_data = load_file(output_clut_filename);
  auto output_clut = output_pltt
      ? to_color8(ResourceFile::decode_pltt(output_clut_data.data(), output_clut_data.size()))
      : to_color8(ResourceFile::decode_clut(output_clut_data.data(), output_clut_data.size()));
  if (output_clut.empty()) {
    throw invalid_argument("output clut is empty");
  }
  if (output_clut.size() < input_clut.size()) {
    throw invalid_argument("output clut is smaller than input clut");
  }
  if (output_clut.size() > input_clut.size()) {
    fwrite_fmt(stderr, "Warning: output clut is larger than input clut; some colors will be unused\n");
  }

  Image img(input_filename);
  for (size_t y = 0; y < img.get_height(); y++) {
    for (size_t x = 0; x < img.get_width(); x++) {
      uint64_t r, g, b, a;
      img.read_pixel(x, y, &r, &g, &b, &a);
      size_t z;
      size_t min_diff = 0xFFFFFFFF;
      size_t min_diff_index = 0;
      for (z = 0; (z < input_clut.size()) && (min_diff != 0); z++) {
        const auto& ic = input_clut[z];
        size_t this_diff = diff(ic.r, r) + diff(ic.g, g) + diff(ic.b, b);
        if (this_diff < min_diff) {
          min_diff = this_diff;
          min_diff_index = z;
        }
      }
      auto& oc = output_clut[min_diff_index];
      img.write_pixel(x, y, oc.r, oc.g, oc.b, a);
    }
  }

  if (output_filename) {
    img.save(output_filename, Image::Format::WINDOWS_BITMAP);
    fwrite_fmt(stderr, "... {}\n", output_filename);
  } else if (input_filename) {
    string output_filename = std::format("{}.bmp", input_filename);
    img.save(output_filename, Image::Format::WINDOWS_BITMAP);
    fwrite_fmt(stderr, "... {}\n", output_filename);
  } else {
    img.save(stdout, Image::Format::WINDOWS_BITMAP);
  }

  return 0;
}
