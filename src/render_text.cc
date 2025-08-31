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
  using Align = BitmapFontRenderer::HorizontalAlignment;

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
  --text-color=RRGGBBAA: Render the text in this color. Default is black.\n\
  --background-color=RRGGBBAA: Render the background in this color. Default is\n\
      transparent if --alpha is given, or white if not.\n\
  --wrap-width=W: Wrap text to fit within this pixel width.\n\
  --width=W: Produce an output image W pixels wide, even if the text is\n\
      smaller or larger. The text will be clipped if it\'s larger.\n\
  --height=H: Produce an output image H pixels tall, even if the text is\n\
      smaller or larger. The text will be clipped if it\'s larger.\n\
  --align=left: Left-align the text (default).\n\
  --align=center: Center-align the text.\n\
  --align=right: Right-align the text.\n\
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
  uint64_t wrap_width = args.get<uint64_t>("wrap-width", 0);
  uint64_t width = args.get<uint64_t>("width", 0);
  uint64_t height = args.get<uint64_t>("height", 0);
  bool cr = args.get<bool>("cr");
  bool inline_text = args.get<bool>("inline");
  Align align = Align::LEFT;
  const auto& align_str = args.get<string>("align", false);
  if (align_str == "center") {
    align = Align::CENTER;
  } else if (align_str == "right") {
    align = Align::RIGHT;
  } else if (align_str != "left" && align_str != "") {
    throw std::runtime_error("Invalid horizontal alignment mode");
  }
  string font_filename = args.get<string>(0, true);
  string input_filename = args.get<string>(1, true);
  string output_filename = args.get<string>(2, true);

  string font_data = load_file(font_filename);
  auto font = make_shared<ResourceFile::DecodedFontResource>(ResourceFile::decode_FONT_only(font_data.data(), font_data.size()));
  BitmapFontRenderer renderer(font);

  string text = inline_text ? input_filename : ((input_filename == "-") ? read_all(stdin) : load_file(input_filename));
  strip_trailing_whitespace(text);

  if (cr) {
    text = replace_cr_with_lf(text);
  }

  if (wrap_width) {
    text = renderer.wrap_text_to_pixel_width(text, wrap_width);
  }

  if (text.empty()) {
    throw runtime_error("No text to render");
  }

  auto [text_width, text_height] = renderer.pixel_dimensions_for_text(text);
  width = width ? width : text_width;
  height = height ? height : text_height;
  fwrite_fmt(stderr, "Text dimensions computed as {}x{} (image dimensions {}x{}, wrap width {})\n",
      text_width, text_height, width, height, wrap_width);

  ImageRGBA8888N ret(width, height);
  ret.clear(bg_color);
  renderer.render_text(ret, text, 0, 0, ret.get_width(), ret.get_height(), text_color, align);

  if (output_filename == "-") {
    fwritex(stdout, ret.serialize(phosg::ImageFormat::WINDOWS_BITMAP));
    fwrite_fmt(stderr, "Image written to stdout\n");
  } else {
    save_file(output_filename, ret.serialize(phosg::ImageFormat::WINDOWS_BITMAP));
    fwrite_fmt(stderr, "Image written to {}\n", output_filename);
  }

  return 0;
}
