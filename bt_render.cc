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
    fprintf(stderr, "usage: %s <--btsp|--hrsp> filename clut_filename\n", argv[0]);
    return 2;
  }

  Image (*decode_fn)(const string&, const vector<Color>&) = NULL;
  if (!strcmp(argv[1], "--btsp")) {
    decode_fn = decode_btSP_sprite;
  } else if (!strcmp(argv[1], "--hrsp")) {
    decode_fn = decode_HrSp_sprite;
  } else {
    fprintf(stderr, "incorrect decoder specified\n");
    return 1;
  }

  ResourceFile clut_res_file(ResourceFile::Resource(RESOURCE_TYPE_clut, 0, load_file(argv[3])));
  auto clut = clut_res_file.decode_clut(0);

  string data = load_file(argv[2]);
  string out_filename = string(argv[2]) + ".bmp";

  Image img = decode_fn(data, clut);
  img.save(out_filename.c_str(), Image::ImageFormat::WindowsBitmap);

  return 0;
}
