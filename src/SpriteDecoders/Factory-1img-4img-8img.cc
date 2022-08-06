#include "Decoders.hh"

#include <string.h>

#include <map>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

using namespace std;



Image decode_1img(const string& data) {
  return decode_monochrome_image(data.data(), data.size(), 32, 21);
}

Image decode_4img(const string& data, const vector<ColorTableEntry>& clut) {
  auto clut8 = to_color8(clut);
  return decode_4bit_image(data.data(), data.size(), 32, 21, &clut8);
}

Image decode_8img(const string& data, const vector<ColorTableEntry>& clut) {
  auto clut8 = to_color8(clut);
  return decode_8bit_image(data.data(), data.size(), 40, 21, &clut8);
}
