#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <phosg/Arguments.hh>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <string>

#include "BitmapFontRenderer.hh"
#include "ResourceFile.hh"

using namespace std;
using namespace phosg;
using namespace ResourceDASM;

int main(int argc, char* argv[]) {
  phosg::Arguments args(&argv[1], argc - 1);

  if (args.get<bool>("help") || argc <= 1) {
    fwrite_fmt(stderr, "\
Usage: render_text [options] FONT-FILE INPUT-FILE OUTPUT-FILE\n\
\n\
FONT-FILE should refer to an exported FONT resource from a Classic Mac OS\n\
resource fork. Such a file can be generated with resource_dasm using the\n\
--save-raw option.\n\
\n\
INPUT-FILE should refer to a text file, containing the text to be rendered.\n\
\n\
OUTPUT-FILE specifies where to write the output (a BMP image file).\n\
\n\
Options:\n\
  --alpha: Generate a 32-bit image with an alpha channel instead of 24-bit.\n\
  --text-color=RRGGBBAA: Render the text in this color. Default is black.\n\
  --background-color=RRGGBBAA: Render the background in this color. Default is\n\
      transparent if --alpha is given, or white if not.\n\
  --max-width=N: Limit the output width to N pixels. Wrap text automatically\n\
      to fit within this width.\n\
  --cr: Replace carriage return (\\r; 0D) characters with newlines (\\n; 0A)\n\
      before rendering. This is needed to render text directly from Classic\n\
      Mac OS applications.\n\
  --inline: Don\'t load data from INPUT-FILE; instead render the filename as\n\
      if it were the file contents.\n\
\n");
    return 0;
  }

  uint32_t text_color = args.get<uint32_t>("text-color", 0x000000FF, phosg::Arguments::IntFormat::HEX);
  uint32_t bg_color = args.get<uint32_t>("background-color", 0xFFFFFFFF, phosg::Arguments::IntFormat::HEX);
  uint64_t max_width = args.get<uint64_t>("max-width", 0);
  bool has_alpha = args.get<bool>("alpha");
  bool cr = args.get<bool>("cr");
  bool inline_text = args.get<bool>("inline");
  string font_filename = args.get<string>(0, true);
  string input_filename = args.get<string>(1, true);
  string output_filename = args.get<string>(2, true);

  string font_data = load_file(font_filename);
  auto font = make_shared<ResourceFile::DecodedFontResource>(
      ResourceFile::decode_FONT_only(font_data.data(), font_data.size()));
  BitmapFontRenderer renderer(font);

  string text = inline_text ? input_filename : ((input_filename == "-") ? read_all(stdin) : load_file(input_filename));
  strip_trailing_whitespace(text);

  if (cr) {
    text = replace_cr_with_lf(text);
  }

  if (max_width) {
    text = renderer.wrap_text_to_pixel_width(text, max_width);
  }

  if (text.empty()) {
    throw runtime_error("No text to render");
  }

  auto dimensions = renderer.pixel_dimensions_for_text(text);
  fwrite_fmt(stderr, "Text dimensions computed as {}x{} (max width {})\n", dimensions.first, dimensions.second, max_width);

  ImageRGBA8888 ret(dimensions.first, dimensions.second, has_alpha);
  ret.clear(bg_color);
  renderer.render_text(ret, text, 0, 0, ret.get_width(), ret.get_height(), text_color);

  if (output_filename == "-") {
    fwritex(stdout, ret.serialize(phosg::ImageFormat::WINDOWS_BITMAP));
    fwrite_fmt(stderr, "Image written to stdout\n");
  } else {
    save_file(output_filename, ret.serialize(phosg::ImageFormat::WINDOWS_BITMAP));
    fwrite_fmt(stderr, "Image written to {}\n", output_filename);
  }

  return 0;
}
