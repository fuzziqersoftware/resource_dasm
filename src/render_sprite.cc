#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <phosg/Encoding.hh>
#include <phosg/Image.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>
#include <variant>

#include "ResourceFile.hh"
#include "IndexFormats/Formats.hh"
#include "SpriteDecoders/Decoders.hh"

using namespace std;
using namespace std::placeholders;



vector<ColorTableEntry> create_default_clut() {
  return vector<ColorTableEntry>({
    {0x0000, Color(0xFFFF, 0xFFFF, 0xFFFF)},
    {0x0000, Color(0xFFFF, 0xFFFF, 0xCCCC)},
    {0x0000, Color(0xFFFF, 0xFFFF, 0x9999)},
    {0x0000, Color(0xFFFF, 0xFFFF, 0x6666)},
    {0x0000, Color(0xFFFF, 0xFFFF, 0x3333)},
    {0x0000, Color(0xFFFF, 0xFFFF, 0x0000)},
    {0x0000, Color(0xFFFF, 0xCCCC, 0xFFFF)},
    {0x0000, Color(0xFFFF, 0xCCCC, 0xCCCC)},
    {0x0000, Color(0xFFFF, 0xCCCC, 0x9999)},
    {0x0000, Color(0xFFFF, 0xCCCC, 0x6666)},
    {0x0000, Color(0xFFFF, 0xCCCC, 0x3333)},
    {0x0000, Color(0xFFFF, 0xCCCC, 0x0000)},
    {0x0000, Color(0xFFFF, 0x9999, 0xFFFF)},
    {0x0000, Color(0xFFFF, 0x9999, 0xCCCC)},
    {0x0000, Color(0xFFFF, 0x9999, 0x9999)},
    {0x0000, Color(0xFFFF, 0x9999, 0x6666)},
    {0x0000, Color(0xFFFF, 0x9999, 0x3333)},
    {0x0000, Color(0xFFFF, 0x9999, 0x0000)},
    {0x0000, Color(0xFFFF, 0x6666, 0xFFFF)},
    {0x0000, Color(0xFFFF, 0x6666, 0xCCCC)},
    {0x0000, Color(0xFFFF, 0x6666, 0x9999)},
    {0x0000, Color(0xFFFF, 0x6666, 0x6666)},
    {0x0000, Color(0xFFFF, 0x6666, 0x3333)},
    {0x0000, Color(0xFFFF, 0x6666, 0x0000)},
    {0x0000, Color(0xFFFF, 0x3333, 0xFFFF)},
    {0x0000, Color(0xFFFF, 0x3333, 0xCCCC)},
    {0x0000, Color(0xFFFF, 0x3333, 0x9999)},
    {0x0000, Color(0xFFFF, 0x3333, 0x6666)},
    {0x0000, Color(0xFFFF, 0x3333, 0x3333)},
    {0x0000, Color(0xFFFF, 0x3333, 0x0000)},
    {0x0000, Color(0xFFFF, 0x0000, 0xFFFF)},
    {0x0000, Color(0xFFFF, 0x0000, 0xCCCC)},
    {0x0000, Color(0xFFFF, 0x0000, 0x9999)},
    {0x0000, Color(0xFFFF, 0x0000, 0x6666)},
    {0x0000, Color(0xFFFF, 0x0000, 0x3333)},
    {0x0000, Color(0xFFFF, 0x0000, 0x0000)},
    {0x0000, Color(0xCCCC, 0xFFFF, 0xFFFF)},
    {0x0000, Color(0xCCCC, 0xFFFF, 0xCCCC)},
    {0x0000, Color(0xCCCC, 0xFFFF, 0x9999)},
    {0x0000, Color(0xCCCC, 0xFFFF, 0x6666)},
    {0x0000, Color(0xCCCC, 0xFFFF, 0x3333)},
    {0x0000, Color(0xCCCC, 0xFFFF, 0x0000)},
    {0x0000, Color(0xCCCC, 0xCCCC, 0xFFFF)},
    {0x0000, Color(0xCCCC, 0xCCCC, 0xCCCC)},
    {0x0000, Color(0xCCCC, 0xCCCC, 0x9999)},
    {0x0000, Color(0xCCCC, 0xCCCC, 0x6666)},
    {0x0000, Color(0xCCCC, 0xCCCC, 0x3333)},
    {0x0000, Color(0xCCCC, 0xCCCC, 0x0000)},
    {0x0000, Color(0xCCCC, 0x9999, 0xFFFF)},
    {0x0000, Color(0xCCCC, 0x9999, 0xCCCC)},
    {0x0000, Color(0xCCCC, 0x9999, 0x9999)},
    {0x0000, Color(0xCCCC, 0x9999, 0x6666)},
    {0x0000, Color(0xCCCC, 0x9999, 0x3333)},
    {0x0000, Color(0xCCCC, 0x9999, 0x0000)},
    {0x0000, Color(0xCCCC, 0x6666, 0xFFFF)},
    {0x0000, Color(0xCCCC, 0x6666, 0xCCCC)},
    {0x0000, Color(0xCCCC, 0x6666, 0x9999)},
    {0x0000, Color(0xCCCC, 0x6666, 0x6666)},
    {0x0000, Color(0xCCCC, 0x6666, 0x3333)},
    {0x0000, Color(0xCCCC, 0x6666, 0x0000)},
    {0x0000, Color(0xCCCC, 0x3333, 0xFFFF)},
    {0x0000, Color(0xCCCC, 0x3333, 0xCCCC)},
    {0x0000, Color(0xCCCC, 0x3333, 0x9999)},
    {0x0000, Color(0xCCCC, 0x3333, 0x6666)},
    {0x0000, Color(0xCCCC, 0x3333, 0x3333)},
    {0x0000, Color(0xCCCC, 0x3333, 0x0000)},
    {0x0000, Color(0xCCCC, 0x0000, 0xFFFF)},
    {0x0000, Color(0xCCCC, 0x0000, 0xCCCC)},
    {0x0000, Color(0xCCCC, 0x0000, 0x9999)},
    {0x0000, Color(0xCCCC, 0x0000, 0x6666)},
    {0x0000, Color(0xCCCC, 0x0000, 0x3333)},
    {0x0000, Color(0xCCCC, 0x0000, 0x0000)},
    {0x0000, Color(0x9999, 0xFFFF, 0xFFFF)},
    {0x0000, Color(0x9999, 0xFFFF, 0xCCCC)},
    {0x0000, Color(0x9999, 0xFFFF, 0x9999)},
    {0x0000, Color(0x9999, 0xFFFF, 0x6666)},
    {0x0000, Color(0x9999, 0xFFFF, 0x3333)},
    {0x0000, Color(0x9999, 0xFFFF, 0x0000)},
    {0x0000, Color(0x9999, 0xCCCC, 0xFFFF)},
    {0x0000, Color(0x9999, 0xCCCC, 0xCCCC)},
    {0x0000, Color(0x9999, 0xCCCC, 0x9999)},
    {0x0000, Color(0x9999, 0xCCCC, 0x6666)},
    {0x0000, Color(0x9999, 0xCCCC, 0x3333)},
    {0x0000, Color(0x9999, 0xCCCC, 0x0000)},
    {0x0000, Color(0x9999, 0x9999, 0xFFFF)},
    {0x0000, Color(0x9999, 0x9999, 0xCCCC)},
    {0x0000, Color(0x9999, 0x9999, 0x9999)},
    {0x0000, Color(0x9999, 0x9999, 0x6666)},
    {0x0000, Color(0x9999, 0x9999, 0x3333)},
    {0x0000, Color(0x9999, 0x9999, 0x0000)},
    {0x0000, Color(0x9999, 0x6666, 0xFFFF)},
    {0x0000, Color(0x9999, 0x6666, 0xCCCC)},
    {0x0000, Color(0x9999, 0x6666, 0x9999)},
    {0x0000, Color(0x9999, 0x6666, 0x6666)},
    {0x0000, Color(0x9999, 0x6666, 0x3333)},
    {0x0000, Color(0x9999, 0x6666, 0x0000)},
    {0x0000, Color(0x9999, 0x3333, 0xFFFF)},
    {0x0000, Color(0x9999, 0x3333, 0xCCCC)},
    {0x0000, Color(0x9999, 0x3333, 0x9999)},
    {0x0000, Color(0x9999, 0x3333, 0x6666)},
    {0x0000, Color(0x9999, 0x3333, 0x3333)},
    {0x0000, Color(0x9999, 0x3333, 0x0000)},
    {0x0000, Color(0x9999, 0x0000, 0xFFFF)},
    {0x0000, Color(0x9999, 0x0000, 0xCCCC)},
    {0x0000, Color(0x9999, 0x0000, 0x9999)},
    {0x0000, Color(0x9999, 0x0000, 0x6666)},
    {0x0000, Color(0x9999, 0x0000, 0x3333)},
    {0x0000, Color(0x9999, 0x0000, 0x0000)},
    {0x0000, Color(0x6666, 0xFFFF, 0xFFFF)},
    {0x0000, Color(0x6666, 0xFFFF, 0xCCCC)},
    {0x0000, Color(0x6666, 0xFFFF, 0x9999)},
    {0x0000, Color(0x6666, 0xFFFF, 0x6666)},
    {0x0000, Color(0x6666, 0xFFFF, 0x3333)},
    {0x0000, Color(0x6666, 0xFFFF, 0x0000)},
    {0x0000, Color(0x6666, 0xCCCC, 0xFFFF)},
    {0x0000, Color(0x6666, 0xCCCC, 0xCCCC)},
    {0x0000, Color(0x6666, 0xCCCC, 0x9999)},
    {0x0000, Color(0x6666, 0xCCCC, 0x6666)},
    {0x0000, Color(0x6666, 0xCCCC, 0x3333)},
    {0x0000, Color(0x6666, 0xCCCC, 0x0000)},
    {0x0000, Color(0x6666, 0x9999, 0xFFFF)},
    {0x0000, Color(0x6666, 0x9999, 0xCCCC)},
    {0x0000, Color(0x6666, 0x9999, 0x9999)},
    {0x0000, Color(0x6666, 0x9999, 0x6666)},
    {0x0000, Color(0x6666, 0x9999, 0x3333)},
    {0x0000, Color(0x6666, 0x9999, 0x0000)},
    {0x0000, Color(0x6666, 0x6666, 0xFFFF)},
    {0x0000, Color(0x6666, 0x6666, 0xCCCC)},
    {0x0000, Color(0x6666, 0x6666, 0x9999)},
    {0x0000, Color(0x6666, 0x6666, 0x6666)},
    {0x0000, Color(0x6666, 0x6666, 0x3333)},
    {0x0000, Color(0x6666, 0x6666, 0x0000)},
    {0x0000, Color(0x6666, 0x3333, 0xFFFF)},
    {0x0000, Color(0x6666, 0x3333, 0xCCCC)},
    {0x0000, Color(0x6666, 0x3333, 0x9999)},
    {0x0000, Color(0x6666, 0x3333, 0x6666)},
    {0x0000, Color(0x6666, 0x3333, 0x3333)},
    {0x0000, Color(0x6666, 0x3333, 0x0000)},
    {0x0000, Color(0x6666, 0x0000, 0xFFFF)},
    {0x0000, Color(0x6666, 0x0000, 0xCCCC)},
    {0x0000, Color(0x6666, 0x0000, 0x9999)},
    {0x0000, Color(0x6666, 0x0000, 0x6666)},
    {0x0000, Color(0x6666, 0x0000, 0x3333)},
    {0x0000, Color(0x6666, 0x0000, 0x0000)},
    {0x0000, Color(0x3333, 0xFFFF, 0xFFFF)},
    {0x0000, Color(0x3333, 0xFFFF, 0xCCCC)},
    {0x0000, Color(0x3333, 0xFFFF, 0x9999)},
    {0x0000, Color(0x3333, 0xFFFF, 0x6666)},
    {0x0000, Color(0x3333, 0xFFFF, 0x3333)},
    {0x0000, Color(0x3333, 0xFFFF, 0x0000)},
    {0x0000, Color(0x3333, 0xCCCC, 0xFFFF)},
    {0x0000, Color(0x3333, 0xCCCC, 0xCCCC)},
    {0x0000, Color(0x3333, 0xCCCC, 0x9999)},
    {0x0000, Color(0x3333, 0xCCCC, 0x6666)},
    {0x0000, Color(0x3333, 0xCCCC, 0x3333)},
    {0x0000, Color(0x3333, 0xCCCC, 0x0000)},
    {0x0000, Color(0x3333, 0x9999, 0xFFFF)},
    {0x0000, Color(0x3333, 0x9999, 0xCCCC)},
    {0x0000, Color(0x3333, 0x9999, 0x9999)},
    {0x0000, Color(0x3333, 0x9999, 0x6666)},
    {0x0000, Color(0x3333, 0x9999, 0x3333)},
    {0x0000, Color(0x3333, 0x9999, 0x0000)},
    {0x0000, Color(0x3333, 0x6666, 0xFFFF)},
    {0x0000, Color(0x3333, 0x6666, 0xCCCC)},
    {0x0000, Color(0x3333, 0x6666, 0x9999)},
    {0x0000, Color(0x3333, 0x6666, 0x6666)},
    {0x0000, Color(0x3333, 0x6666, 0x3333)},
    {0x0000, Color(0x3333, 0x6666, 0x0000)},
    {0x0000, Color(0x3333, 0x3333, 0xFFFF)},
    {0x0000, Color(0x3333, 0x3333, 0xCCCC)},
    {0x0000, Color(0x3333, 0x3333, 0x9999)},
    {0x0000, Color(0x3333, 0x3333, 0x6666)},
    {0x0000, Color(0x3333, 0x3333, 0x3333)},
    {0x0000, Color(0x3333, 0x3333, 0x0000)},
    {0x0000, Color(0x3333, 0x0000, 0xFFFF)},
    {0x0000, Color(0x3333, 0x0000, 0xCCCC)},
    {0x0000, Color(0x3333, 0x0000, 0x9999)},
    {0x0000, Color(0x3333, 0x0000, 0x6666)},
    {0x0000, Color(0x3333, 0x0000, 0x3333)},
    {0x0000, Color(0x3333, 0x0000, 0x0000)},
    {0x0000, Color(0x0000, 0xFFFF, 0xFFFF)},
    {0x0000, Color(0x0000, 0xFFFF, 0xCCCC)},
    {0x0000, Color(0x0000, 0xFFFF, 0x9999)},
    {0x0000, Color(0x0000, 0xFFFF, 0x6666)},
    {0x0000, Color(0x0000, 0xFFFF, 0x3333)},
    {0x0000, Color(0x0000, 0xFFFF, 0x0000)},
    {0x0000, Color(0x0000, 0xCCCC, 0xFFFF)},
    {0x0000, Color(0x0000, 0xCCCC, 0xCCCC)},
    {0x0000, Color(0x0000, 0xCCCC, 0x9999)},
    {0x0000, Color(0x0000, 0xCCCC, 0x6666)},
    {0x0000, Color(0x0000, 0xCCCC, 0x3333)},
    {0x0000, Color(0x0000, 0xCCCC, 0x0000)},
    {0x0000, Color(0x0000, 0x9999, 0xFFFF)},
    {0x0000, Color(0x0000, 0x9999, 0xCCCC)},
    {0x0000, Color(0x0000, 0x9999, 0x9999)},
    {0x0000, Color(0x0000, 0x9999, 0x6666)},
    {0x0000, Color(0x0000, 0x9999, 0x3333)},
    {0x0000, Color(0x0000, 0x9999, 0x0000)},
    {0x0000, Color(0x0000, 0x6666, 0xFFFF)},
    {0x0000, Color(0x0000, 0x6666, 0xCCCC)},
    {0x0000, Color(0x0000, 0x6666, 0x9999)},
    {0x0000, Color(0x0000, 0x6666, 0x6666)},
    {0x0000, Color(0x0000, 0x6666, 0x3333)},
    {0x0000, Color(0x0000, 0x6666, 0x0000)},
    {0x0000, Color(0x0000, 0x3333, 0xFFFF)},
    {0x0000, Color(0x0000, 0x3333, 0xCCCC)},
    {0x0000, Color(0x0000, 0x3333, 0x9999)},
    {0x0000, Color(0x0000, 0x3333, 0x6666)},
    {0x0000, Color(0x0000, 0x3333, 0x3333)},
    {0x0000, Color(0x0000, 0x3333, 0x0000)},
    {0x0000, Color(0x0000, 0x0000, 0xFFFF)},
    {0x0000, Color(0x0000, 0x0000, 0xCCCC)},
    {0x0000, Color(0x0000, 0x0000, 0x9999)},
    {0x0000, Color(0x0000, 0x0000, 0x6666)},
    {0x0000, Color(0x0000, 0x0000, 0x3333)},
    {0x0000, Color(0xEEEE, 0x0000, 0x0000)},
    {0x0000, Color(0xDDDD, 0x0000, 0x0000)},
    {0x0000, Color(0xBBBB, 0x0000, 0x0000)},
    {0x0000, Color(0xAAAA, 0x0000, 0x0000)},
    {0x0000, Color(0x8888, 0x0000, 0x0000)},
    {0x0000, Color(0x7777, 0x0000, 0x0000)},
    {0x0000, Color(0x5555, 0x0000, 0x0000)},
    {0x0000, Color(0x4444, 0x0000, 0x0000)},
    {0x0000, Color(0x2222, 0x0000, 0x0000)},
    {0x0000, Color(0x1111, 0x0000, 0x0000)},
    {0x0000, Color(0x0000, 0xEEEE, 0x0000)},
    {0x0000, Color(0x0000, 0xDDDD, 0x0000)},
    {0x0000, Color(0x0000, 0xBBBB, 0x0000)},
    {0x0000, Color(0x0000, 0xAAAA, 0x0000)},
    {0x0000, Color(0x0000, 0x8888, 0x0000)},
    {0x0000, Color(0x0000, 0x7777, 0x0000)},
    {0x0000, Color(0x0000, 0x5555, 0x0000)},
    {0x0000, Color(0x0000, 0x4444, 0x0000)},
    {0x0000, Color(0x0000, 0x2222, 0x0000)},
    {0x0000, Color(0x0000, 0x1111, 0x0000)},
    {0x0000, Color(0x0000, 0x0000, 0xEEEE)},
    {0x0000, Color(0x0000, 0x0000, 0xDDDD)},
    {0x0000, Color(0x0000, 0x0000, 0xBBBB)},
    {0x0000, Color(0x0000, 0x0000, 0xAAAA)},
    {0x0000, Color(0x0000, 0x0000, 0x8888)},
    {0x0000, Color(0x0000, 0x0000, 0x7777)},
    {0x0000, Color(0x0000, 0x0000, 0x5555)},
    {0x0000, Color(0x0000, 0x0000, 0x4444)},
    {0x0000, Color(0x0000, 0x0000, 0x2222)},
    {0x0000, Color(0x0000, 0x0000, 0x1111)},
    {0x0000, Color(0xEEEE, 0xEEEE, 0xEEEE)},
    {0x0000, Color(0xDDDD, 0xDDDD, 0xDDDD)},
    {0x0000, Color(0xBBBB, 0xBBBB, 0xBBBB)},
    {0x0000, Color(0xAAAA, 0xAAAA, 0xAAAA)},
    {0x0000, Color(0x8888, 0x8888, 0x8888)},
    {0x0000, Color(0x7777, 0x7777, 0x7777)},
    {0x0000, Color(0x5555, 0x5555, 0x5555)},
    {0x0000, Color(0x4444, 0x4444, 0x4444)},
    {0x0000, Color(0x2222, 0x2222, 0x2222)},
    {0x0000, Color(0x1111, 0x1111, 0x1111)},
    {0x0000, Color(0x0000, 0x0000, 0x0000)},
  });
}



template <typename T>
void write_output(const string&, const T&) {
  throw logic_error("unspecialized write_output should never be called");
}

template <>
void write_output<Image>(const string& output_prefix, const Image& img) {
  string filename = output_prefix + ".bmp";
  img.save(filename.c_str(), Image::Format::WINDOWS_BITMAP);
  fprintf(stderr, "... %s\n", filename.c_str());
}

template <>
void write_output<vector<Image>>(
    const string& output_prefix, const vector<Image>& seq) {
  for (size_t x = 0; x < seq.size(); x++) {
    string filename = string_printf("%s.%zu.bmp", output_prefix.c_str(), x);
    seq[x].save(filename.c_str(), Image::Format::WINDOWS_BITMAP);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
};

template <>
void write_output<unordered_map<string, Image>>(
    const string& output_prefix, const unordered_map<string, Image>& dict) {
  for (const auto& it : dict) {
    string filename = string_printf("%s.%s.bmp", output_prefix.c_str(), it.first.c_str());
    it.second.save(filename.c_str(), Image::Format::WINDOWS_BITMAP);
    fprintf(stderr, "... %s\n", filename.c_str());
  }
};

template <>
void write_output<DecodedShap3D>(
    const string& output_prefix, const DecodedShap3D& shap) {
  string filename = output_prefix + "_model.stl";
  save_file(filename, shap.model_as_stl());
  fprintf(stderr, "... %s\n", filename.c_str());
  filename = output_prefix + "_model.obj";
  save_file(filename, shap.model_as_obj());
  fprintf(stderr, "... %s\n", filename.c_str());
  filename = output_prefix + "_top_view.svg";
  save_file(filename, shap.top_view_as_svg());
  fprintf(stderr, "... %s\n", filename.c_str());
}



struct Format {
  using SingleImageMonoDecoderT = function<Image(const string&)>;
  using SingleImageColorDecoderT = function<
      Image(const string&, const vector<ColorTableEntry>&)>;
  using ImageSequenceMonoDecoderT = function<
      vector<Image>(const string&)>;
  using ImageSequenceColorDecoderT = function<
      vector<Image>(const string&, const vector<ColorTableEntry>&)>;
  using ImageDictFromResourceCollectionDecoderT = function<
      unordered_map<string, Image>(ResourceFile&, const string&, const vector<ColorTableEntry>&)>;
  using ModelAndVectorImageDecoderT = function<DecodedShap3D(const string&)>;
  using DecoderT = variant<
    SingleImageMonoDecoderT,
    SingleImageColorDecoderT,
    ImageSequenceMonoDecoderT,
    ImageSequenceColorDecoderT,
    ImageDictFromResourceCollectionDecoderT,
    ModelAndVectorImageDecoderT>;

  const char* cli_argument;
  const char* cli_description;
  bool color_table_required;
  DecoderT decode;

  Format(
      const char* cli_argument,
      const char* cli_description,
      bool color_table_required,
      DecoderT decode)
    : cli_argument(cli_argument),
      cli_description(cli_description),
      color_table_required(color_table_required),
      decode(decode) { }
};



// TODO: Figure out why std::bind doesn't work for these

static Image decode_PSCR_v1(const string& data) {
  return decode_PSCR(data, false);
}
static Image decode_PSCR_v2(const string& data) {
  return decode_PSCR(data, true);
}

const vector<Format> formats({
  Format("1img",    "render a 1img image from Factory", false, decode_1img),
  Format("4img",    "render a 4img image from Factory", true, decode_4img),
  Format("8img",    "render a 8img image from Factory", true, decode_8img),
  Format("BMap",    "render a BMap image from DinoPark Tycoon", false, decode_BMap),
  Format("BTMP",    "render a BTMP image from Blobbo", false, decode_BTMP),
  Format("btSP",    "render a btSP image from Bubble Trouble", true, decode_btSP),
  Format("DC2",     "render a DC2 image from Dark Castle", false, decode_DC2),
  Format("GSIF",    "render a GSIF image from Greebles", true, decode_GSIF),
  Format("HrSp",    "render a HrSp image from Harry the Handsome Executive", true, bind(decode_HrSp, _1, _2, 16)),
  Format("Imag",    "render an Imag image from various MECC games", false, bind(decode_Imag, _1, _2, true)),
  Format("Imag-fm", "render an Imag image from MECC Munchers-series games", false, bind(decode_Imag, _1, _2, false)),
  Format("Pak",     "render a Pak image set from Mario Teaches Typing", true, decode_Pak),
  Format("PBLK",    "render a PBLK image from Beyond Dark Castle", false, decode_PBLK),
  Format("PMP8",    "render a PMP8 image from Blobbo", true, decode_PMP8),
  Format("PPCT",    "render a PPCT image from Dark Castle or Beyond Dark Castle", false, decode_PPCT),
  Format("PPic",    "render a PPic image set from Swamp Gas", false, decode_PPic),
  Format("PPSS",    "render a PPSS image set from Flashback", true, decode_PPSS),
  Format("PSCR-v1", "render a PSCR image from Dark Castle", false, decode_PSCR_v1),
  Format("PSCR-v2", "render a PSCR image from Beyond Dark Castle", false, decode_PSCR_v2),
  Format("SHAP",    "render a SHAP image from Prince of Persia 2", true, decode_SHAP),
  Format("shap",    "render a shap model from Spectre", false, decode_shap),
  Format("SHPD-p",  "render a SHPD image set from Prince of Persia", false, bind(decode_SHPD_collection_images_only, _1, _2, _3, SHPDVersion::PRINCE_OF_PERSIA)),
  Format("SHPD-v1", "render a SHPD image set from Lemmings", false, bind(decode_SHPD_collection_images_only, _1, _2, _3, SHPDVersion::LEMMINGS_V1)),
  Format("SHPD-v2", "render a SHPD image set from Oh No! More Lemmings", false, bind(decode_SHPD_collection_images_only, _1, _2, _3, SHPDVersion::LEMMINGS_V2)),
  Format("SprD",    "render an SprD image set from Slithereens", true, decode_SprD),
  Format("Spri",    "render a Spri image from TheZone", true, decode_Spri),
  Format("Sprt",    "render a Sprt image from Bonkheads", true, bind(decode_HrSp, _1, _2, 8)),
  Format("SPRT",    "render a SPRT image set from SimCity 2000", true, decode_SPRT),
  Format("sssf",    "render a sssf image set from Step On It!", true, decode_sssf),
  Format("XBig",    "render an XBig image set from DinoPark Tycoon", false, decode_XBig),
  Format("XMap",    "render an XMap image from DinoPark Tycoon", true, decode_XMap),
});



void print_usage() {
  fprintf(stderr, "\
Usage: render_sprite <input-option> [options] <input-file> [output-prefix]\n\
\n\
If output-prefix is not given, the input filename is used as the output prefix.\n\
The input file is not overwritten.\n\
\n\
Input options (exactly one of these must be given):\n");
  for (const auto& format : formats) {
    fprintf(stderr, "  --%s: %s\n", format.cli_argument, format.cli_description);
  }
  fprintf(stderr, "\
\n\
Color table options:\n\
  --default-clut: use the default 256-color table\n\
  --clut=FILE: use a clut resource (.bin file) as the color table\n\
  --pltt=FILE: use a pltt resource (.bin file) as the color table\n\
  --CTBL=FILE: use a CTBL resource (.bin file) as the color table\n\
The = sign is required for these options, unlike the format options above.\n\
\n");
}

enum class ColorTableType {
  NONE,
  DEFAULT,
  CLUT,
  PLTT,
  CTBL,
};

int main(int argc, char* argv[]) {
  if (argc <= 1) {
    print_usage();
    return 1;
  }

  ColorTableType color_table_type = ColorTableType::NONE;
  const char* input_filename = nullptr;
  const char* color_table_filename = nullptr;
  const char* output_filename = nullptr;
  const Format* format = nullptr;
  for (int x = 1; x < argc; x++) {
    if (argv[x][0] == '-' && argv[x][1] == '-') {
      if (!strcmp(argv[x], "--default-clut")) {
        color_table_type = ColorTableType::DEFAULT;
      } else if (!strncmp(&argv[x][2], "clut=", 5)) {
        color_table_filename = &argv[x][7];
        color_table_type = ColorTableType::CLUT;
      } else if (!strncmp(&argv[x][2], "pltt=", 5)) {
        color_table_filename = &argv[x][7];
        color_table_type = ColorTableType::PLTT;
      } else if (!strncmp(&argv[x][2], "CTBL=", 5)) {
        color_table_filename = &argv[x][7];
        color_table_type = ColorTableType::CTBL;
      } else {
        for (const auto& candidate_format : formats) {
          if (!strcmp(&argv[x][2], candidate_format.cli_argument)) {
            if (format != nullptr) {
              throw invalid_argument("multiple format options given");
            }
            format = &candidate_format;
          }
        }
      }

    } else if (!input_filename) {
      input_filename = argv[x];
    } else if (!output_filename) {
      output_filename = argv[x];

    } else {
      throw invalid_argument(string_printf("invalid or excessive option: %s", argv[x]));
    }
  }

  if (!input_filename || !format) {
    print_usage();
    return 1;
  }
  if ((color_table_type == ColorTableType::NONE) && format->color_table_required) {
    throw invalid_argument("a color table is required for this image format; use --clut, --pltt, or --CTBL");
  }

  string sprite_data = load_file(input_filename);

  vector<ColorTableEntry> color_table;
  if (color_table_type != ColorTableType::NONE) {
    switch (color_table_type) {
      case ColorTableType::DEFAULT:
        color_table = create_default_clut();
        break;
      case ColorTableType::CLUT: {
        string data = load_file(color_table_filename);
        color_table = ResourceFile::decode_clut(data.data(), data.size());
        break;
      }
      case ColorTableType::PLTT: {
        string data = load_file(color_table_filename);
        auto pltt = ResourceFile::decode_pltt(data.data(), data.size());
        for (const auto& c : pltt) {
          auto& e = color_table.emplace_back();
          e.c = c;
          e.color_num = color_table.size() - 1;
        }
        break;
      }
      case ColorTableType::CTBL: {
        string data = load_file(color_table_filename);
        color_table = ResourceFile::decode_CTBL(data.data(), data.size());
        break;
      }
      default:
        throw logic_error("invalid color table type");
    }
  }

  string output_prefix = output_filename ? output_filename : input_filename;
  if (ends_with(output_prefix, ".bmp")) {
    output_prefix.resize(output_prefix.size() - 4);
  }

  if (holds_alternative<Format::SingleImageMonoDecoderT>(format->decode)) {
    write_output(output_prefix, get<Format::SingleImageMonoDecoderT>(format->decode)(sprite_data));

  } else if (holds_alternative<Format::SingleImageColorDecoderT>(format->decode)) {
    write_output(output_prefix, get<Format::SingleImageColorDecoderT>(format->decode)(sprite_data, color_table));

  } else if (holds_alternative<Format::ImageSequenceMonoDecoderT>(format->decode)) {
    write_output(output_prefix, get<Format::ImageSequenceMonoDecoderT>(format->decode)(sprite_data));

  } else if (holds_alternative<Format::ImageSequenceColorDecoderT>(format->decode)) {
    write_output(output_prefix, get<Format::ImageSequenceColorDecoderT>(format->decode)(sprite_data, color_table));

  } else if (holds_alternative<Format::ImageDictFromResourceCollectionDecoderT>(format->decode)) {
    auto rf = parse_resource_fork(load_file(string(input_filename) + "/..namedfork/rsrc"));
    write_output(output_prefix, get<Format::ImageDictFromResourceCollectionDecoderT>(format->decode)(
        rf, sprite_data, color_table));

  } else if (holds_alternative<Format::ModelAndVectorImageDecoderT>(format->decode)) {
    write_output(output_prefix, get<Format::ModelAndVectorImageDecoderT>(format->decode)(sprite_data));

  } else {
    throw logic_error("invalid decoder function type");
  }

  return 0;
}
