#include <string.h>

#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

#include "ResourceFile.hh"
#include "AmbrosiaSprites.hh"

using namespace std;



int main(int argc, char* argv[]) {
  if (argc != 4) {
    fprintf(stderr, "Usage: bt_render <--btsp|--hrsp> <filename> <clut_filename>\n");
    return 2;
  }

  Image (*decode_fn)(const string&, const vector<ColorTableEntry>&) = nullptr;
  if (!strcmp(argv[1], "--btsp")) {
    decode_fn = decode_btSP_sprite;
  } else if (!strcmp(argv[1], "--hrsp")) {
    decode_fn = decode_HrSp_sprite;
  } else {
    fprintf(stderr, "incorrect decoder specified\n");
    return 1;
  }

  string clut_data = load_file(argv[3]);
  auto clut = ResourceFile::decode_clut(clut_data.data(), clut_data.size());

  string data = load_file(argv[2]);
  string out_filename = string(argv[2]) + ".bmp";

  Image img = decode_fn(data, clut);
  img.save(out_filename.c_str(), Image::ImageFormat::WindowsBitmap);

  return 0;
}
