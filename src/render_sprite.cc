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
Usage: render_sprite <input-option> <color-table-option> [--output=filename]\n\
\n\
Input options (exactly one of these must be given):\n\
  --bmap=FILE - render a BMap image from DinoPark Tycoon\n\
  --btmp=FILE - render a BTMP image from Blobbo\n\
  --btsp=FILE - render a btSP image from Bubble Trouble\n\
  --dc2=FILE - render a DC2 image from Dark Castle\n\
  --gsif=FILE - render a GSIF image from Greebles\n\
  --hrsp=FILE - render a HrSp image from Harry the Handsome Executive\n\
  --imag=FILE - render an Imag image from various MECC games\n\
  --imag-fm=FILE - render an Imag image from MECC Munchers-series games\n\
  --pblk=FILE - render a PBLK image from Beyond Dark Castle\n\
  --pmp8=FILE - render a PMP8 image from Blobbo\n\
  --ppct=FILE - render a PPCT image from Dark Castle or Beyond Dark Castle\n\
  --ppic=FILE - render a PPic image set from Swamp Gas\n\
  --ppss=FILE - render a PPSS image set from Flashback\n\
  --pscr-v1=FILE - render a PSCR image from Dark Castle\n\
  --pscr-v2=FILE - render a PSCR image from Beyond Dark Castle\n\
  --shap=FILE - render a SHAP image from Prince of Persia 2\n\
  --shap-3d=FILE - render a shap model from Spectre\n\
  --shpd-coll-p=FILE - render a SHPD image set from Prince of Persia\n\
  --shpd-coll-v1=FILE - render a SHPD image set from Lemmings\n\
  --shpd-coll-v2=FILE - render a SHPD image set from Oh No! More Lemmings\n\
  --spri=FILE - render a Spri image from TheZone\n\
  --sprt=FILE - render a SPRT image set from SimCity 2000\n\
  --sprt-bh=FILE - render a Sprt image from Bonkheads\n\
  --sssf=FILE - render a sssf image set from Step On It!\n\
  --xbig=FILE - render an XBig image set from DinoPark Tycoon\n\
  --xmap=FILE - render an XMap image from DinoPark Tycoon\n\
\n\
Color table options (usually exactly one of these must be given):\n\
  --clut=FILE - use a clut resource (.bin file) as the color table\n\
  --pltt=FILE - use a pltt resource (.bin file) as the color table\n\
  --ctbl=FILE - use a CTBL resource (.bin file) as the color table\n\
\n\
If --output is not given, the output is written to <input-filename>.bmp. If\n\
multiple images are generated, the output is written to a sequence of files\n\
named <input-filename>.#.bmp.\n");
}

enum class SpriteType {
  NONE,
  F_1IMG,
  F_4IMG,
  F_8IMG,
  BMAP,
  BTMP,
  BTSP,
  DC2,
  GSIF,
  HRSP,
  IMAG,
  IMAG_FM,
  PBLK,
  PMP8,
  PPCT,
  PPIC,
  PPSS,
  PSCR_V1,
  PSCR_V2,
  SHAP_3D, // Spectre shap
  SHAP, // Prince of Persia 2 SHAP
  SPRI,
  SPRT,
  SPRT_BH,
  SPRT_S, // Slithereen SprT
  SSSF,
  SHPD_COLL_V1,
  SHPD_COLL_V2,
  SHPD_COLL_P,
  XBIG,
  XMAP,
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

  SHPDVersion shpd_version = SHPDVersion::LEMMINGS_V1;
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

    } else if (!strncmp(argv[x], "--bmap=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::BMAP;

    } else if (!strncmp(argv[x], "--xmap=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::XMAP;

    } else if (!strncmp(argv[x], "--xbig=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::XBIG;

    } else if (!strncmp(argv[x], "--ppss=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::PPSS;

    } else if (!strncmp(argv[x], "--SprT-S=", 7)) {
      sprite_filename = &argv[x][9];
      sprite_type = SpriteType::SPRT_S;

    } else if (!strncmp(argv[x], "--btmp=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::BTMP;

    } else if (!strncmp(argv[x], "--pmp8=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::PMP8;

    } else if (!strncmp(argv[x], "--imag=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::IMAG;
    } else if (!strncmp(argv[x], "--imag-fm=", 10)) {
      sprite_filename = &argv[x][10];
      sprite_type = SpriteType::IMAG_FM;

    } else if (!strncmp(argv[x], "--dc2=", 6)) {
      sprite_filename = &argv[x][6];
      sprite_type = SpriteType::DC2;

    } else if (!strncmp(argv[x], "--gsif=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::GSIF;

    } else if (!strncmp(argv[x], "--ppct=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::PPCT;

    } else if (!strncmp(argv[x], "--ppic=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::PPIC;

    } else if (!strncmp(argv[x], "--pblk=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::PBLK;

    } else if (!strncmp(argv[x], "--pscr-v1=", 10)) {
      sprite_filename = &argv[x][10];
      sprite_type = SpriteType::PSCR_V1;

    } else if (!strncmp(argv[x], "--pscr-v2=", 10)) {
      sprite_filename = &argv[x][10];
      sprite_type = SpriteType::PSCR_V2;

    } else if (!strncmp(argv[x], "--shap=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::SHAP;

    } else if (!strncmp(argv[x], "--shap-3d=", 10)) {
      sprite_filename = &argv[x][10];
      sprite_type = SpriteType::SHAP_3D;

    } else if (!strncmp(argv[x], "--sprt=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::SPRT;

    } else if (!strncmp(argv[x], "--sprt-bh=", 10)) {
      sprite_filename = &argv[x][10];
      sprite_type = SpriteType::SPRT_BH;

    } else if (!strncmp(argv[x], "--sssf=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::SSSF;

    } else if (!strncmp(argv[x], "--spri=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::SPRI;

    } else if (!strncmp(argv[x], "--shpd-coll-v1=", 15)) {
      sprite_filename = &argv[x][15];
      sprite_type = SpriteType::SHPD_COLL_V1;
      shpd_version = SHPDVersion::LEMMINGS_V1;

    } else if (!strncmp(argv[x], "--shpd-coll-v2=", 15)) {
      sprite_filename = &argv[x][15];
      sprite_type = SpriteType::SHPD_COLL_V2;
      shpd_version = SHPDVersion::LEMMINGS_V2;

    } else if (!strncmp(argv[x], "--shpd-coll-p=", 14)) {
      sprite_filename = &argv[x][14];
      sprite_type = SpriteType::SHPD_COLL_P;
      shpd_version = SHPDVersion::PRINCE_OF_PERSIA;

    } else if (!strncmp(argv[x], "--1img=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::F_1IMG;
    } else if (!strncmp(argv[x], "--4img=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::F_4IMG;
    } else if (!strncmp(argv[x], "--8img=", 7)) {
      sprite_filename = &argv[x][7];
      sprite_type = SpriteType::F_8IMG;

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
  // Color tables must be given for all formats except these
  if (color_table_type == ColorTableType::NONE &&
      sprite_type != SpriteType::F_1IMG &&
      sprite_type != SpriteType::BMAP &&
      sprite_type != SpriteType::BTMP &&
      sprite_type != SpriteType::DC2 &&
      sprite_type != SpriteType::IMAG &&
      sprite_type != SpriteType::IMAG_FM &&
      sprite_type != SpriteType::PBLK &&
      sprite_type != SpriteType::PPCT &&
      sprite_type != SpriteType::PPIC &&
      sprite_type != SpriteType::PSCR_V1 &&
      sprite_type != SpriteType::PSCR_V2 &&
      sprite_type != SpriteType::SHAP_3D &&
      sprite_type != SpriteType::SHPD_COLL_V1 &&
      sprite_type != SpriteType::SHPD_COLL_V2 &&
      sprite_type != SpriteType::SHPD_COLL_P &&
      sprite_type != SpriteType::XBIG) {
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

  if (sprite_type == SpriteType::SHAP_3D) {
    auto result = decode_shap(sprite_data);

    // TODO: Use the color table to color faces in OBJ output
    string stl_contents = result.model_as_stl();
    string obj_contents = result.model_as_obj();
    string svg_contents = result.top_view_as_svg();

    string output_prefix = output_filename ? output_filename : sprite_filename;

    string filename = output_prefix + "_model.stl";
    save_file(filename, stl_contents);
    fprintf(stderr, "... %s\n", filename.c_str());
    filename = output_prefix + "_model.obj";
    save_file(filename, obj_contents);
    fprintf(stderr, "... %s\n", filename.c_str());
    filename = output_prefix + "_top_view.svg";
    save_file(filename, svg_contents);
    fprintf(stderr, "... %s\n", filename.c_str());

  } else {
    vector<Image> results;
    unordered_map<string, Image> dict_results;
    switch (sprite_type) {
      case SpriteType::BTSP:
        results.emplace_back(decode_btSP(sprite_data, color_table));
        break;
      case SpriteType::HRSP:
        results.emplace_back(decode_HrSp(sprite_data, color_table, 16));
        break;
      case SpriteType::BMAP:
        results.emplace_back(decode_BMap(sprite_data));
        break;
      case SpriteType::XMAP:
        results.emplace_back(decode_XMap(sprite_data, color_table));
        break;
      case SpriteType::XBIG:
        results = decode_XBig(sprite_data);
        break;
      case SpriteType::PPSS:
        results = decode_PPSS(sprite_data, color_table);
      case SpriteType::SPRT_S:
        results = decode_SprD(sprite_data, color_table, 20);
        break;
      case SpriteType::BTMP:
        results.emplace_back(decode_BTMP(sprite_data));
        break;
      case SpriteType::PMP8:
        results.emplace_back(decode_PMP8(sprite_data, color_table));
        break;
      case SpriteType::IMAG:
        results = decode_Imag(sprite_data, color_table, true);
        break;
      case SpriteType::IMAG_FM:
        results = decode_Imag(sprite_data, color_table, false);
        break;
      case SpriteType::DC2:
        results.emplace_back(decode_DC2(sprite_data));
        break;
      case SpriteType::GSIF:
        results.emplace_back(decode_GSIF(sprite_data, color_table));
        break;
      case SpriteType::PPCT:
        results.emplace_back(decode_PPCT(sprite_data));
        break;
      case SpriteType::PPIC:
        results = decode_PPic(sprite_data, color_table);
        break;
      case SpriteType::PBLK:
        results.emplace_back(decode_PBLK(sprite_data));
        break;
      case SpriteType::PSCR_V1:
        results.emplace_back(decode_PSCR(sprite_data, false));
        break;
      case SpriteType::PSCR_V2:
        results.emplace_back(decode_PSCR(sprite_data, true));
        break;
      case SpriteType::SHAP:
        results.emplace_back(decode_SHAP(sprite_data, color_table));
        break;
      case SpriteType::SPRT:
        results = decode_SPRT(sprite_data, color_table);
        break;
      case SpriteType::SPRT_BH:
        results.emplace_back(decode_HrSp(sprite_data, color_table, 8));
        break;
      case SpriteType::SSSF:
        results = decode_sssf(sprite_data, color_table);
        break;
      case SpriteType::SPRI:
        results.emplace_back(decode_Spri(sprite_data, color_table));
        break;
      case SpriteType::SHPD_COLL_V1:
      case SpriteType::SHPD_COLL_V2:
      case SpriteType::SHPD_COLL_P: {
        string resource_fork_contents = load_file(string(sprite_filename) + "/..namedfork/rsrc");
        auto results = decode_SHPD_collection(resource_fork_contents, sprite_data,
            color_table, shpd_version);
        for (const auto& result : results) {
          dict_results.emplace(result.first, move(result.second.image));
        }
        break;
      }
      case SpriteType::F_1IMG:
        results.emplace_back(decode_1img(sprite_data));
        break;
      case SpriteType::F_4IMG:
        results.emplace_back(decode_4img(sprite_data, color_table));
        break;
      case SpriteType::F_8IMG:
        results.emplace_back(decode_8img(sprite_data, color_table));
        break;
      default:
        throw logic_error("invalid sprite type");
    }

    string output_prefix = output_filename ? output_filename : sprite_filename;
    if (ends_with(output_prefix, ".bmp")) {
      output_prefix.resize(output_prefix.size() - 4);
    }

    if (results.size() == 0 && dict_results.size() == 0) {
      fprintf(stderr, "*** No images were decoded\n");
    } else if (results.size() == 1) {
      string filename = output_prefix + ".bmp";
      results[0].save(filename.c_str(), Image::Format::WINDOWS_BITMAP);
      fprintf(stderr, "... %s\n", filename.c_str());
    } else if (results.size() > 1) {
      for (size_t x = 0; x < results.size(); x++) {
        string filename = string_printf("%s.%zu.bmp", output_prefix.c_str(), x);
        results[x].save(filename.c_str(), Image::Format::WINDOWS_BITMAP);
        fprintf(stderr, "... %s\n", filename.c_str());
      }
    } else { // dict_results.size() > 0
      for (const auto& it : dict_results) {
        string filename = string_printf("%s.%s.bmp", output_prefix.c_str(), it.first.c_str());
        it.second.save(filename.c_str(), Image::Format::WINDOWS_BITMAP);
        fprintf(stderr, "... %s\n", filename.c_str());
      }
    }
  }

  return 0;
}
