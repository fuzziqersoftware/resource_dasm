#include <stdint.h>

#include <phosg/Image.hh>
#include <phosg/Vector.hh>
#include <string>
#include <unordered_map>
#include <vector>

#include "../QuickDrawFormats.hh"
#include "../ResourceFile.hh"

namespace ResourceDASM {

// TODO: This isn't really the right place for such a thing, but it doesn't seem to belong anywhere else currently.
// Find an appropriate place for this.
template <typename ImageT>
phosg::ImageRGBA8888N apply_clut(const ImageT& indexed_image, const std::vector<ColorTableEntry>& clut) {
  phosg::ImageRGBA8888N ret{indexed_image.get_width(), indexed_image.get_height()};
  for (size_t y = 0; y < indexed_image.get_height(); y++) {
    for (size_t x = 0; x < indexed_image.get_width(); x++) {
      uint32_t src = indexed_image.read(x, y);
      ret.write(x, y, clut.at(phosg::get_r(src)).c.rgba8888(phosg::get_a(src)));
    }
  }
  return ret;
}

// Ambrosia-btSP-HrSp.cc
phosg::ImageRGBA8888N decode_btSP(std::string_view data, const std::vector<ColorTableEntry>& clut);
phosg::ImageRGBA8888N decode_HrSp(std::string_view data, const std::vector<ColorTableEntry>& clut, size_t header_size);
std::vector<phosg::ImageRGBA8888N> decode_SprD(std::string_view data, const std::vector<ColorTableEntry>& clut);

// Blobbo-BTMP-PMP8.cc
phosg::ImageG1 decode_BTMP(std::string_view data);
phosg::ImageRGB888 decode_PMP8(std::string_view data, const std::vector<ColorTableEntry>& clut);

// Bungie-256.cc
std::vector<phosg::ImageRGBA8888N> decode_pathways_256(std::string_view data);
std::vector<phosg::ImageRGBA8888N> decode_marathon_256(std::string_view data);

// DarkCastle-DC2.cc
phosg::ImageRGBA8888N decode_DC2(std::string_view data);

// DarkCastle-PPCT-PSCR.cc
std::string decompress_PSCR_v1(phosg::StringReader& r);
std::string decompress_PSCR_v2(phosg::StringReader& r);
std::string decompress_PPCT(phosg::StringReader& r, size_t expected_bits = 0);
phosg::ImageG1 decode_PBLK(std::string_view data);
phosg::ImageG1 decode_PSCR(std::string_view data, bool is_v2);
phosg::ImageGA11 decode_PPCT(std::string_view data);

// DinoParkTycoon-BMap.cc
phosg::ImageGA11 decode_BMap(std::string_view data);
std::vector<phosg::ImageG1> decode_XBig(std::string_view data);
phosg::ImageRGBA8888N decode_XMap(std::string_view data, const std::vector<ColorTableEntry>& clut);

// Factory-1img-4img-8img.cc
phosg::ImageG1 decode_1img(std::string_view data);
phosg::ImageRGB888 decode_4img(std::string_view data, const std::vector<ColorTableEntry>& pltt);
phosg::ImageRGB888 decode_8img(std::string_view data, const std::vector<ColorTableEntry>& pltt);

// Greebles-GSIF.cc
phosg::ImageRGB888 decode_GSIF(std::string_view data, const std::vector<ColorTableEntry>& pltt);

// Lemmings-PrinceOfPersia-SHPD.cc
enum class SHPDVersion {
  LEMMINGS_V1 = 0,
  LEMMINGS_V2,
  PRINCE_OF_PERSIA,
};
struct DecodedSHPDImage {
  int16_t origin_x;
  int16_t origin_y;
  phosg::ImageRGBA8888N image;
};
std::string decompress_SHPD_data(phosg::StringReader& r);
std::unordered_map<size_t, DecodedSHPDImage> decode_SHPD(
    ResourceFile& rf,
    std::string_view data_fork_contents,
    int16_t res_id,
    const std::vector<ColorTableEntry>& clut,
    SHPDVersion version);

// MECC-Imag.cc
std::vector<phosg::ImageRGB888> decode_Imag(
    std::string_view data, const std::vector<ColorTableEntry>& clut, bool use_later_formats);

// Odyssey-NPIC-SHPS.cc
ResourceFile::DecodedPICTResource decode_NPIC(std::string_view data);
phosg::ImageRGBA8888N decode_NTEX(std::string_view data);
std::vector<phosg::ImageRGBA8888N> decode_SHPS(std::string_view data);

// Presage.cc
struct ColorPPSSEntry {
  int16_t origin_x = 0;
  int16_t origin_y = 0;
  phosg::ImageRGBA8888N image;
};
struct IndexedPPSSEntry {
  int16_t origin_x = 0;
  int16_t origin_y = 0;
  phosg::ImageGA88N image;
  inline ColorPPSSEntry apply_clut(const std::vector<ColorTableEntry>& clut) const {
    return ColorPPSSEntry{this->origin_x, this->origin_y, ResourceDASM::apply_clut(this->image, clut)};
  }
};
phosg::ImageGA11 decode_presage_mono_image(
    phosg::StringReader& r, size_t width, size_t height, bool use_and_compositing);
phosg::ImageGA88N decode_presage_v1_commands(phosg::StringReader& r, size_t w, size_t h);
phosg::ImageGA88N decode_presage_v2_commands(phosg::StringReader& r, size_t w, size_t h);
std::map<size_t, IndexedPPSSEntry> decode_PPSS_indexed(std::string_view data);
std::map<size_t, ColorPPSSEntry> decode_PPSS(std::string_view data, const std::vector<ColorTableEntry>& clut);
std::vector<phosg::ImageRGBA8888N> decode_Pak(std::string_view data, const std::vector<ColorTableEntry>& clut);

// PrinceOfPersia2-SHAP.cc
std::string decompress_SHAP_lz(std::string_view data);
std::string decompress_SHAP_standard_rle(std::string_view data);
std::string decompress_SHAP_rows_rle(std::string_view data, size_t num_rows, size_t row_bytes);
phosg::ImageRGBA8888N decode_SHAP(std::string_view data, const std::vector<ColorTableEntry>& ctbl);

// SimCity2000-SPRT.cc
std::vector<phosg::ImageRGBA8888N> decode_SPRT(std::string_view data, const std::vector<ColorTableEntry>& pltt);

// Spectre-shap.cc
struct DecodedShap3D {
  struct Plane {
    std::vector<size_t> vertex_nums;
    uint16_t color_index;
  };
  struct Line {
    size_t start;
    size_t end;
  };
  std::vector<phosg::Vector3<double>> vertices;
  std::vector<Plane> planes;
  std::vector<phosg::Vector3<double>> top_view_vertices;
  std::vector<Line> top_view_lines;

  std::string model_as_stl() const;
  std::string model_as_obj() const;
  std::string top_view_as_svg() const;
};
DecodedShap3D decode_shap(std::string_view data);

// StepOnIt-sssf.cc
std::vector<phosg::ImageRGBA8888N> decode_sssf(std::string_view data, const std::vector<ColorTableEntry>& clut);

// SwampGas-PPic.cc
std::string decompress_PPic_pixel_map_data(std::string_view data, size_t row_bytes, size_t height);
std::string decompress_PPic_bitmap_data(std::string_view data, size_t row_bytes, size_t height);
std::vector<phosg::ImageRGB888> decode_PPic(std::string_view data, const std::vector<ColorTableEntry>& clut);

// TheZone-Spri.cc
phosg::ImageRGBA8888N decode_Spri(std::string_view data, const std::vector<ColorTableEntry>& clut);

} // namespace ResourceDASM
