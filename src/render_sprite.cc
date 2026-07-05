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

template <phosg::PixelFormat Format>
void write_output(
    const ResourceDASM::ImageSaver& image_saver, const std::string& output_prefix, const phosg::Image<Format>& img) {
  std::string filename = image_saver.save_image(img, output_prefix);
  phosg::fwrite_fmt(stderr, "... {}\n", filename);
}

template <phosg::PixelFormat Format>
void write_output(
    const ResourceDASM::ImageSaver& image_saver, const std::string& output_prefix, const std::vector<phosg::Image<Format>>& seq) {
  for (size_t x = 0; x < seq.size(); x++) {
    std::string filename = std::format("{}.{}", output_prefix, x);
    filename = image_saver.save_image(seq[x], filename);
    phosg::fwrite_fmt(stderr, "... {}\n", filename);
  }
};

template <phosg::PixelFormat Format>
void write_output(
    const ResourceDASM::ImageSaver& image_saver, const std::string& output_prefix, const std::unordered_map<std::string, phosg::Image<Format>>& dict) {
  for (const auto& it : dict) {
    std::string filename = std::format("{}.{}", output_prefix, it.first);
    filename = image_saver.save_image(it.second, filename);
    phosg::fwrite_fmt(stderr, "... {}\n", filename);
  }
};

void write_output(const std::string& output_prefix, const ResourceDASM::DecodedShap3D& shap) {
  std::string filename = output_prefix + "_model.stl";
  phosg::save_file(filename, shap.model_as_stl());
  phosg::fwrite_fmt(stderr, "... {}\n", filename);
  filename = output_prefix + "_model.obj";
  phosg::save_file(filename, shap.model_as_obj());
  phosg::fwrite_fmt(stderr, "... {}\n", filename);
  filename = output_prefix + "_top_view.svg";
  phosg::save_file(filename, shap.top_view_as_svg());
  phosg::fwrite_fmt(stderr, "... {}\n", filename);
}

struct Format {
  using DecoderG1 = std::function<phosg::ImageG1(const std::string&)>;
  using DecoderG1Multi = std::function<std::vector<phosg::ImageG1>(const std::string&)>;
  using DecoderGA11 = std::function<phosg::ImageGA11(const std::string&)>;
  using DecoderRGB888WithCLUT = std::function<phosg::ImageRGB888(const std::string&, const std::vector<ResourceDASM::ColorTableEntry>&)>;
  using DecoderRGBA8888WithCLUT = std::function<phosg::ImageRGBA8888N(const std::string&, const std::vector<ResourceDASM::ColorTableEntry>&)>;
  using DecoderRGB888MultiWithCLUT = std::function<std::vector<phosg::ImageRGB888>(const std::string&, const std::vector<ResourceDASM::ColorTableEntry>&)>;
  using DecoderRGBA8888 = std::function<phosg::ImageRGBA8888N(const std::string&)>;
  using DecoderRGBA8888Multi = std::function<std::vector<phosg::ImageRGBA8888N>(const std::string&)>;
  using DecoderRGBA8888MultiWithCLUT = std::function<std::vector<phosg::ImageRGBA8888N>(const std::string&, const std::vector<ResourceDASM::ColorTableEntry>&)>;
  using DecoderRGBA8888MapFromResCollWithCLUT = std::function<
      std::unordered_map<std::string, phosg::ImageRGBA8888N>(ResourceDASM::ResourceFile&, const std::string&, const std::vector<ResourceDASM::ColorTableEntry>&)>;
  using DecoderPICT = std::function<ResourceDASM::ResourceFile::DecodedPICTResource(const std::string&)>;
  using DecoderModelAndVectorImage = std::function<ResourceDASM::DecodedShap3D(const std::string&)>;

  using DecoderT = std::variant<
      DecoderG1,
      DecoderG1Multi,
      DecoderGA11,
      DecoderRGB888WithCLUT,
      DecoderRGBA8888WithCLUT,
      DecoderRGB888MultiWithCLUT,
      DecoderRGBA8888,
      DecoderRGBA8888Multi,
      DecoderRGBA8888MultiWithCLUT,
      DecoderRGBA8888MapFromResCollWithCLUT,
      DecoderPICT,
      DecoderModelAndVectorImage>;

  const char* cli_argument;
  const char* cli_description;
  bool color_table_required;
  DecoderT decode;

  Format(const char* cli_argument, const char* cli_description, bool color_table_required, DecoderT decode)
      : cli_argument(cli_argument),
        cli_description(cli_description),
        color_table_required(color_table_required),
        decode(decode) {}
};

namespace ph = std::placeholders;

const std::vector<Format> formats{
    Format(".256-m", "render a .256 image from Marathon 1", false, ResourceDASM::decode_marathon_256),
    Format(".256-pd", "render a .256 image from Pathways Into Darkness", false, ResourceDASM::decode_pathways_256),
    Format("1img", "render a 1img image from Factory", false, ResourceDASM::decode_1img),
    Format("4img", "render a 4img image from Factory", true, ResourceDASM::decode_4img),
    Format("8img", "render a 8img image from Factory", true, ResourceDASM::decode_8img),
    Format("BMap", "render a BMap image from DinoPark Tycoon", false, ResourceDASM::decode_BMap),
    Format("BTMP", "render a BTMP image from Blobbo", false, ResourceDASM::decode_BTMP),
    Format("btSP", "render a btSP image from Bubble Trouble", true, ResourceDASM::decode_btSP),
    Format("DC2", "render a DC2 image from Dark Castle", false, ResourceDASM::decode_DC2),
    Format("GSIF", "render a GSIF image from Greebles", true, ResourceDASM::decode_GSIF),
    Format("HrSp", "render a HrSp image from Harry the Handsome Executive", true, bind(ResourceDASM::decode_HrSp, ph::_1, ph::_2, 16)),
    Format("Imag", "render an Imag image from various MECC games", false, bind(ResourceDASM::decode_Imag, ph::_1, ph::_2, true)),
    Format("Imag-fm", "render an Imag image from MECC Munchers-series games", false, bind(ResourceDASM::decode_Imag, ph::_1, ph::_2, false)),
    Format("NPIC", "render an NPIC picture from Odyssey: The Legend of Nemesis", false, ResourceDASM::decode_NPIC),
    Format("Pak", "render a Pak image set from Mario Teaches Typing", true, ResourceDASM::decode_Pak),
    Format("PBLK", "render a PBLK image from Beyond Dark Castle", false, ResourceDASM::decode_PBLK),
    Format("PMP8", "render a PMP8 image from Blobbo", true, ResourceDASM::decode_PMP8),
    Format("PPCT", "render a PPCT image from Dark Castle or Beyond Dark Castle", false, ResourceDASM::decode_PPCT),
    Format("PPic", "render a PPic image set from Swamp Gas", false, ResourceDASM::decode_PPic),
    Format("PPSS", "render a PPSS image set from Flashback", true, ResourceDASM::decode_PPSS),
    Format("PSCR-v1", "render a PSCR image from Dark Castle", false, std::bind(ResourceDASM::decode_PSCR, ph::_1, false)),
    Format("PSCR-v2", "render a PSCR image from Beyond Dark Castle", false, std::bind(ResourceDASM::decode_PSCR, ph::_1, true)),
    Format("SHAP", "render a SHAP image from Prince of Persia 2", true, ResourceDASM::decode_SHAP),
    Format("shap", "render a shap model from Spectre", false, ResourceDASM::decode_shap),
    Format("SHPD-p", "render a SHPD image set from Prince of Persia", false, bind(ResourceDASM::decode_SHPD_collection_images_only, ph::_1, ph::_2, ph::_3, ResourceDASM::SHPDVersion::PRINCE_OF_PERSIA)),
    Format("SHPD-v1", "render a SHPD image set from Lemmings", false, bind(ResourceDASM::decode_SHPD_collection_images_only, ph::_1, ph::_2, ph::_3, ResourceDASM::SHPDVersion::LEMMINGS_V1)),
    Format("SHPD-v2", "render a SHPD image set from Oh No! More Lemmings", false, bind(ResourceDASM::decode_SHPD_collection_images_only, ph::_1, ph::_2, ph::_3, ResourceDASM::SHPDVersion::LEMMINGS_V2)),
    Format("SHPS", "render a SHPS image set from Odyssey: The Legend of Nemesis", false, ResourceDASM::decode_SHPS),
    Format("SprD", "render an SprD image set from Slithereens", true, ResourceDASM::decode_SprD),
    Format("Spri", "render a Spri image from TheZone", true, ResourceDASM::decode_Spri),
    Format("Sprt", "render a Sprt image from Bonkheads", true, bind(ResourceDASM::decode_HrSp, ph::_1, ph::_2, 8)),
    Format("SPRT", "render a SPRT image set from SimCity 2000", true, ResourceDASM::decode_SPRT),
    Format("sssf", "render a sssf image set from Step On It!", true, ResourceDASM::decode_sssf),
    Format("XBig", "render an XBig image set from DinoPark Tycoon", false, ResourceDASM::decode_XBig),
    Format("XMap", "render an XMap image from DinoPark Tycoon", true, ResourceDASM::decode_XMap),
};

void print_usage() {
  phosg::fwrite_fmt(stderr, "\
Usage: render_sprite <input-option> [options] <input-file> [output-prefix]\n\
\n\
If output-prefix is not given, the input filename is used as the output prefix.\n\
The input file is not overwritten.\n\
\n\
Input format options (exactly one of these must be given):\n");
  for (const auto& format : formats) {
    phosg::fwrite_fmt(stderr, "  --{}: {}\n", format.cli_argument, format.cli_description);
  }
  phosg::fwrite_fmt(stderr, "\
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

int main(int argc, char** argv) {
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
  ResourceDASM::ImageSaver image_saver;
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
              throw std::invalid_argument("multiple format options given");
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
      phosg::fwrite_fmt(stderr, "invalid or excessive option: {}\n", argv[x]);
      print_usage();
      return 2;
    }
  }

  if (!input_filename || !format) {
    print_usage();
    return 2;
  }

  if ((color_table_type == ColorTableType::NONE) && format->color_table_required) {
    phosg::fwrite_fmt(stderr, "a color table is required for this sprite format; use --clut, --pltt, or --CTBL\n");
    print_usage();
    return 2;
  }

  auto sprite_data = phosg::load_file(input_filename);

  std::vector<ResourceDASM::ColorTableEntry> color_table;
  if (color_table_type != ColorTableType::NONE) {
    switch (color_table_type) {
      case ColorTableType::DEFAULT:
        color_table = ResourceDASM::create_default_clut();
        break;
      case ColorTableType::CLUT: {
        auto data = phosg::load_file(color_table_filename);
        color_table = ResourceDASM::ResourceFile::decode_clut(data.data(), data.size());
        break;
      }
      case ColorTableType::PLTT: {
        auto data = phosg::load_file(color_table_filename);
        auto pltt = ResourceDASM::ResourceFile::decode_pltt(data.data(), data.size());
        for (const auto& c : pltt) {
          auto& e = color_table.emplace_back();
          e.c = c;
          e.color_num = color_table.size() - 1;
        }
        break;
      }
      case ColorTableType::CTBL: {
        auto data = phosg::load_file(color_table_filename);
        color_table = ResourceDASM::ResourceFile::decode_CTBL(data.data(), data.size());
        break;
      }
      default:
        throw std::logic_error("invalid color table type");
    }
  }

  std::string output_prefix = output_filename ? output_filename : input_filename;
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
  } else if (holds_alternative<Format::DecoderPICT>(format->decode)) {
    write_output(image_saver, output_prefix, get<Format::DecoderPICT>(format->decode)(sprite_data).image);
  } else if (holds_alternative<Format::DecoderRGBA8888MapFromResCollWithCLUT>(format->decode)) {
    if (input_is_macbinary) {
      auto decoded = ResourceDASM::parse_macbinary(sprite_data);
      auto rf = ResourceDASM::parse_resource_fork(decoded.second);
      // TODO: Using .all() here is an unnecessary string copy. Fix this.
      const auto& decoder = get<Format::DecoderRGBA8888MapFromResCollWithCLUT>(format->decode);
      write_output(image_saver, output_prefix, decoder(rf, decoded.first.all(), color_table));
    } else {
      auto rf = ResourceDASM::parse_resource_fork(phosg::load_file(std::string(input_filename) + "/..namedfork/rsrc"));
      const auto& decoder = get<Format::DecoderRGBA8888MapFromResCollWithCLUT>(format->decode);
      write_output(image_saver, output_prefix, decoder(rf, sprite_data, color_table));
    }
  } else if (holds_alternative<Format::DecoderModelAndVectorImage>(format->decode)) {
    write_output(output_prefix, get<Format::DecoderModelAndVectorImage>(format->decode)(sprite_data));
  } else {
    throw std::logic_error("invalid decoder function type");
  }

  return 0;
}
