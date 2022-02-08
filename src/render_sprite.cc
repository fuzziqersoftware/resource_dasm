#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <phosg/Encoding.hh>
#include <phosg/Image.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>

#include "ResourceFile.hh"
#include "SpriteDecoders/Decoders.hh"

using namespace std;


void print_usage() {
  fprintf(stderr, "\
Usage: render_sprite <input-option> <color-table-option> [--output=file.bmp]\n\
\n\
Input options (exactly one of these must be given):\n\
  --btsp=btSP_file.bin\n\
  --hrsp=HrSp_file.bin\n\
  --dc2=DC2_file.bin\n\
  --gsif=GSIF_file.bin\n\
  --shap=SHAP_file.bin\n\
  --sprt=SPRT_file.bin\n\
  --sssf=sssf_file.bin\n\
  --spri=Spri_file.bin\n\
  \n\
Color table options (exactly one of these must be given, unless --dc2 is used):\n\
  --clut=clut_file.bin\n\
  --pltt=pltt_file.bin\n\
  --ctbl=CTBL_file.bin\n\
\n\
If --output is not given, the output is written to <input-filename>.bmp.\n\
");
}

enum class SpriteType {
  NONE,
  BTSP,
  HRSP,
  DC2,
  GSIF,
  SHAP,
  SPRT,
  SSSF,
  SPRI,
};

enum class ColorTableType {
  NONE,
  CLUT,
  PLTT,
  CTBL,
};

int main(int argc, char* argv[]) {
  if (argc <= 1) {
    print_usage();
    return 1;
  }

  SpriteType sprite_type = SpriteType::NONE;
  ColorTableType color_table_type = ColorTableType::NONE;
  const char* sprite_filename = nullptr;
  const char* color_table_filename = nullptr;
  const char* output_filename = nullptr;
  for (int x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "--btsp=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::BTSP;

    } else if (!strncmp(argv[x], "--hrsp=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::HRSP;

    } else if (!strncmp(argv[x], "--dc2=", 6)) {
      sprite_filename = &argv[x][6];
      sprite_type = SpriteType::DC2;

    } else if (!strncmp(argv[x], "--gsif=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::GSIF;

    } else if (!strncmp(argv[x], "--shap=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::SHAP;

    } else if (!strncmp(argv[x], "--sprt=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::SPRT;

    } else if (!strncmp(argv[x], "--sssf=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::SSSF;

    } else if (!strncmp(argv[x], "--spri=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::SPRI;

    } else if (!strncmp(argv[x], "--clut=", 7)) {
      color_table_filename = &argv[x][7];
      color_table_type = ColorTableType::CLUT;

    } else if (!strncmp(argv[x], "--pltt=", 7)) {
      color_table_filename = &argv[x][7];
      color_table_type = ColorTableType::PLTT;

    } else if (!strncmp(argv[x], "--ctbl=", 7)) {
      color_table_filename = &argv[x][7];
      color_table_type = ColorTableType::CTBL;

    } else if (!strncmp(argv[x], "--output=", 9)) {
      output_filename = &argv[x][9];

    } else {
      throw invalid_argument(string_printf("invalid or excessive option: %s", argv[x]));
    }
  }

  if (sprite_type == SpriteType::NONE) {
    print_usage();
    return 1;
  }
  if (color_table_type == ColorTableType::NONE && sprite_type != SpriteType::DC2) {
    print_usage();
    return 1;
  }

  vector<ColorTableEntry> color_table;
  string sprite_data = load_file(sprite_filename);

  if (color_table_type != ColorTableType::NONE) {
    string color_table_data = load_file(color_table_filename);
    switch (color_table_type) {
      case ColorTableType::CLUT:
        color_table = ResourceFile::decode_clut(color_table_data.data(), color_table_data.size());
        break;
      case ColorTableType::PLTT: {
        auto pltt = ResourceFile::decode_pltt(color_table_data.data(), color_table_data.size());
        for (const auto& c : pltt) {
          auto& e = color_table.emplace_back();
          e.c = c;
          e.color_num = color_table.size() - 1;
        }
        break;
      }
      case ColorTableType::CTBL:
        color_table = ResourceFile::decode_CTBL(color_table_data.data(), color_table_data.size());
        break;
      default:
        throw logic_error("invalid color table type");
    }
  }

  vector<Image> results;
  switch (sprite_type) {
    case SpriteType::BTSP:
      results.emplace_back(decode_btSP(sprite_data, color_table));
      break;
    case SpriteType::HRSP:
      results.emplace_back(decode_HrSp(sprite_data, color_table));
      break;
    case SpriteType::DC2:
      results.emplace_back(decode_DC2(sprite_data.data()));
      break;
    case SpriteType::GSIF:
      results.emplace_back(decode_GSIF(sprite_data, color_table));
      break;
    case SpriteType::SHAP:
      results.emplace_back(decode_SHAP(sprite_data, color_table));
      break;
    case SpriteType::SPRT:
      results = decode_SPRT(sprite_data, color_table);
      break;
    case SpriteType::SSSF:
      results = decode_sssf(sprite_data, color_table);
      break;
    case SpriteType::SPRI:
      results.emplace_back(decode_Spri(sprite_data, color_table));
      break;
    default:
      throw logic_error("invalid sprite type");
  }

  string output_prefix = output_filename ? output_filename : sprite_filename;
  if (ends_with(output_prefix, ".bmp")) {
    output_prefix.resize(output_prefix.size() - 4);
  }

  if (results.size() == 0) {
    fprintf(stderr, "*** No images were decoded\n");
  } else if (results.size() == 1) {
    string filename = output_prefix + ".bmp";
    results[0].save(filename.c_str(), Image::ImageFormat::WindowsBitmap);
    fprintf(stderr, "... %s\n", filename.c_str());
  } else {
    for (size_t x = 0; x < results.size(); x++) {
      string filename = string_printf("%s.%zu.bmp", output_prefix.c_str(), x);
      results[x].save(filename.c_str(), Image::ImageFormat::WindowsBitmap);
      fprintf(stderr, "... %s\n", filename.c_str());
    }
  }

  return 0;
}
