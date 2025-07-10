#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <string>
#include <variant>

#include "ImageSaver.hh"
#include "IndexFormats/Formats.hh"
#include "QuickDrawEngine.hh"
#include "ResourceFile.hh"
#include "SpriteDecoders/Decoders.hh"

using namespace std;
using namespace phosg;
using namespace std::placeholders;
using namespace ResourceDASM;

template <PixelFormat Format>
void write_output(const ImageSaver& image_saver, const string& output_prefix, const Image<Format>& img) {
  string filename = image_saver.save_image(img, output_prefix);
  fwrite_fmt(stderr, "... {}\n", filename);
}

template <PixelFormat Format>
void write_output(const ImageSaver& image_saver, const string& output_prefix, const vector<Image<Format>>& seq) {
  for (size_t x = 0; x < seq.size(); x++) {
    string filename = std::format("{}.{}", output_prefix, x);
    filename = image_saver.save_image(seq[x], filename);
    fwrite_fmt(stderr, "... {}\n", filename);
  }
};

template <PixelFormat Format>
void write_output(const ImageSaver& image_saver, const string& output_prefix, const unordered_map<string, Image<Format>>& dict) {
  for (const auto& it : dict) {
    string filename = std::format("{}.{}", output_prefix, it.first);
    filename = image_saver.save_image(it.second, filename);
    fwrite_fmt(stderr, "... {}\n", filename);
  }
};

void write_output(const string& output_prefix, const DecodedShap3D& shap) {
  string filename = output_prefix + "_model.stl";
  save_file(filename, shap.model_as_stl());
  fwrite_fmt(stderr, "... {}\n", filename);
  filename = output_prefix + "_model.obj";
  save_file(filename, shap.model_as_obj());
  fwrite_fmt(stderr, "... {}\n", filename);
  filename = output_prefix + "_top_view.svg";
  save_file(filename, shap.top_view_as_svg());
  fwrite_fmt(stderr, "... {}\n", filename);
}

struct Format {
  using DecoderG1 = function<ImageG1(const string&)>;
  using DecoderGA11 = function<ImageGA11(const string&)>;
  using DecoderRGB888WithCLUT = function<ImageRGB888(const string&, const vector<ColorTableEntry>&)>;
  using DecoderRGBA8888WithCLUT = function<ImageRGBA8888N(const string&, const vector<ColorTableEntry>&)>;
  using DecoderG1Multi = function<vector<ImageG1>(const string&)>;
  using DecoderRGB888MultiWithCLUT = function<vector<ImageRGB888>(const string&, const vector<ColorTableEntry>&)>;
  using DecoderRGBA8888 = function<ImageRGBA8888N(const string&)>;
  using DecoderRGBA8888Multi = function<vector<ImageRGBA8888N>(const string&)>;
  using DecoderRGBA8888MultiWithCLUT = function<vector<ImageRGBA8888N>(const string&, const vector<ColorTableEntry>&)>;
  using DecoderRGBA8888MapFromResCollWithCLUT = function<
      unordered_map<string, ImageRGBA8888N>(ResourceFile&, const string&, const vector<ColorTableEntry>&)>;
  using DecoderModelAndVectorImage = function<DecodedShap3D(const string&)>;

  using DecoderT = variant<
      DecoderG1,
      DecoderG1Multi,
      DecoderGA11,
      DecoderRGB888WithCLUT,
      DecoderRGB888MultiWithCLUT,
      DecoderRGBA8888,
      DecoderRGBA8888WithCLUT,
      DecoderRGBA8888Multi,
      DecoderRGBA8888MultiWithCLUT,
      DecoderModelAndVectorImage,
      DecoderRGBA8888MapFromResCollWithCLUT>;

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
        decode(decode) {}
};

// TODO: Figure out why std::bind doesn't work for these

static ImageG1 decode_PSCR_v1(const string& data) {
  return decode_PSCR(data, false);
}
static ImageG1 decode_PSCR_v2(const string& data) {
  return decode_PSCR(data, true);
}

const vector<Format> formats({
    Format(".256-m", "render a .256 image from Marathon 1", false, decode_marathon_256),
    Format(".256-pd", "render a .256 image from Pathways Into Darkness", false, decode_pathways_256),
    Format("1img", "render a 1img image from Factory", false, decode_1img),
    Format("4img", "render a 4img image from Factory", true, decode_4img),
    Format("8img", "render a 8img image from Factory", true, decode_8img),
    Format("BMap", "render a BMap image from DinoPark Tycoon", false, decode_BMap),
    Format("BTMP", "render a BTMP image from Blobbo", false, decode_BTMP),
    Format("btSP", "render a btSP image from Bubble Trouble", true, decode_btSP),
    Format("DC2", "render a DC2 image from Dark Castle", false, decode_DC2),
    Format("GSIF", "render a GSIF image from Greebles", true, decode_GSIF),
    Format("HrSp", "render a HrSp image from Harry the Handsome Executive", true, bind(decode_HrSp, _1, _2, 16)),
    Format("Imag", "render an Imag image from various MECC games", false, bind(decode_Imag, _1, _2, true)),
    Format("Imag-fm", "render an Imag image from MECC Munchers-series games", false, bind(decode_Imag, _1, _2, false)),
    Format("Pak", "render a Pak image set from Mario Teaches Typing", true, decode_Pak),
    Format("PBLK", "render a PBLK image from Beyond Dark Castle", false, decode_PBLK),
    Format("PMP8", "render a PMP8 image from Blobbo", true, decode_PMP8),
    Format("PPCT", "render a PPCT image from Dark Castle or Beyond Dark Castle", false, decode_PPCT),
    Format("PPic", "render a PPic image set from Swamp Gas", false, decode_PPic),
    Format("PPSS", "render a PPSS image set from Flashback", true, decode_PPSS),
    Format("PSCR-v1", "render a PSCR image from Dark Castle", false, decode_PSCR_v1),
    Format("PSCR-v2", "render a PSCR image from Beyond Dark Castle", false, decode_PSCR_v2),
    Format("SHAP", "render a SHAP image from Prince of Persia 2", true, decode_SHAP),
    Format("shap", "render a shap model from Spectre", false, decode_shap),
    Format("SHPD-p", "render a SHPD image set from Prince of Persia", false, bind(decode_SHPD_collection_images_only, _1, _2, _3, SHPDVersion::PRINCE_OF_PERSIA)),
    Format("SHPD-v1", "render a SHPD image set from Lemmings", false, bind(decode_SHPD_collection_images_only, _1, _2, _3, SHPDVersion::LEMMINGS_V1)),
    Format("SHPD-v2", "render a SHPD image set from Oh No! More Lemmings", false, bind(decode_SHPD_collection_images_only, _1, _2, _3, SHPDVersion::LEMMINGS_V2)),
    Format("SprD", "render an SprD image set from Slithereens", true, decode_SprD),
    Format("Spri", "render a Spri image from TheZone", true, decode_Spri),
    Format("Sprt", "render a Sprt image from Bonkheads", true, bind(decode_HrSp, _1, _2, 8)),
    Format("SPRT", "render a SPRT image set from SimCity 2000", true, decode_SPRT),
    Format("sssf", "render a sssf image set from Step On It!", true, decode_sssf),
    Format("XBig", "render an XBig image set from DinoPark Tycoon", false, decode_XBig),
    Format("XMap", "render an XMap image from DinoPark Tycoon", true, decode_XMap),
});

void print_usage() {
  fwrite_fmt(stderr, "\
Usage: render_sprite <input-option> [options] <input-file> [output-prefix]\n\
\n\
If output-prefix is not given, the input filename is used as the output prefix.\n\
The input file is not overwritten.\n\
\n\
Input format options (exactly one of these must be given):\n");
  for (const auto& format : formats) {
    fwrite_fmt(stderr, "  --{}: {}\n", format.cli_argument, format.cli_description);
  }
  fwrite_fmt(stderr, "\
\n\
Input parsing options:\n\
  --macbinary\n\
      For formats that expect both a data and resource fork (currently only the\n\
      SHPD formats), parse the input as a MacBinary file instead of as a normal\n\
      file with data and resource forks.\n\
\n\
Color table options:\n\
  --default-clut: use the default 256-color table\n\
  --clut=FILE: use a clut resource (.bin file) as the color table\n\
  --pltt=FILE: use a pltt resource (.bin file) as the color table\n\
  --CTBL=FILE: use a CTBL resource (.bin file) as the color table\n\
The = sign is required for these options, unlike the format options above.\n\
\n" IMAGE_SAVER_HELP);
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
  bool input_is_macbinary = false;
  ImageSaver image_saver;
  for (int x = 1; x < argc; x++) {
    if (argv[x][0] == '-' && argv[x][1] == '-') {
      if (!strcmp(argv[x], "--macbinary")) {
        input_is_macbinary = true;
      } else if (!strcmp(argv[x], "--default-clut")) {
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
      } else if (image_saver.process_cli_arg(argv[x])) {
        // Nothing
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
      fwrite_fmt(stderr, "invalid or excessive option: {}\n", argv[x]);
      print_usage();
      return 2;
    }
  }

  if (!input_filename || !format) {
    print_usage();
    return 2;
  }

  if ((color_table_type == ColorTableType::NONE) && format->color_table_required) {
    fwrite_fmt(stderr, "a color table is required for this sprite format; use --clut, --pltt, or --CTBL\n");
    print_usage();
    return 2;
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
  if (output_prefix.ends_with(".bmp")) {
    output_prefix.resize(output_prefix.size() - 4);
  }

  // TODO: This is dumb; use a template instead
  if (holds_alternative<Format::DecoderG1>(format->decode)) {
    write_output(image_saver, output_prefix, get<Format::DecoderG1>(format->decode)(sprite_data));
  } else if (holds_alternative<Format::DecoderG1Multi>(format->decode)) {
    write_output(image_saver, output_prefix, get<Format::DecoderG1Multi>(format->decode)(sprite_data));
  } else if (holds_alternative<Format::DecoderGA11>(format->decode)) {
    write_output(image_saver, output_prefix, get<Format::DecoderGA11>(format->decode)(sprite_data));
  } else if (holds_alternative<Format::DecoderRGB888WithCLUT>(format->decode)) {
    write_output(image_saver, output_prefix, get<Format::DecoderRGB888WithCLUT>(format->decode)(sprite_data, color_table));
  } else if (holds_alternative<Format::DecoderRGB888MultiWithCLUT>(format->decode)) {
    write_output(image_saver, output_prefix, get<Format::DecoderRGB888MultiWithCLUT>(format->decode)(sprite_data, color_table));
  } else if (holds_alternative<Format::DecoderRGBA8888>(format->decode)) {
    write_output(image_saver, output_prefix, get<Format::DecoderRGBA8888>(format->decode)(sprite_data));
  } else if (holds_alternative<Format::DecoderRGBA8888WithCLUT>(format->decode)) {
    write_output(image_saver, output_prefix, get<Format::DecoderRGBA8888WithCLUT>(format->decode)(sprite_data, color_table));
  } else if (holds_alternative<Format::DecoderRGBA8888Multi>(format->decode)) {
    write_output(image_saver, output_prefix, get<Format::DecoderRGBA8888Multi>(format->decode)(sprite_data));
  } else if (holds_alternative<Format::DecoderRGBA8888MultiWithCLUT>(format->decode)) {
    write_output(image_saver, output_prefix, get<Format::DecoderRGBA8888MultiWithCLUT>(format->decode)(sprite_data, color_table));
  } else if (holds_alternative<Format::DecoderModelAndVectorImage>(format->decode)) {
    write_output(output_prefix, get<Format::DecoderModelAndVectorImage>(format->decode)(sprite_data));
  } else if (holds_alternative<Format::DecoderRGBA8888MapFromResCollWithCLUT>(format->decode)) {
    if (input_is_macbinary) {
      auto decoded = parse_macbinary(sprite_data);
      // TODO: Using .all() here is an unnecessary string copy. Fix this.
      const auto& decoder = get<Format::DecoderRGBA8888MapFromResCollWithCLUT>(format->decode);
      write_output(image_saver, output_prefix, decoder(decoded.second, decoded.first.all(), color_table));
    } else {
      auto rf = parse_resource_fork(load_file(string(input_filename) + "/..namedfork/rsrc"));
      const auto& decoder = get<Format::DecoderRGBA8888MapFromResCollWithCLUT>(format->decode);
      write_output(image_saver, output_prefix, decoder(rf, sprite_data, color_table));
    }
  } else {
    throw logic_error("invalid decoder function type");
  }

  return 0;
}
