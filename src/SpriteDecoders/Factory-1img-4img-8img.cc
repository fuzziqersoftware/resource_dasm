#include "Decoders.hh"

#include <string.h>

#include <map>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

using namespace std;

// These are all just fixed-size, fixed-depth images. Oddly, their dimensions
// don't really make sense - the game uses 20x20-pixel icons, but the height of
// all formats is 21 pixels (and the last row is blank). The monochrome width
// being 32 internally makes sense, as on 68K systems row_bytes would have to be
// at least 4; the other formats wouldn't have this restriction, but still have
// widths 32 and 40. All the examples I've seen have nothing relevant in that
// extra unused space, so it's not clear why the images are so large.

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
