#include <stdint.h>

#include <phosg/Image.hh>
#include <phosg/Vector.hh>
#include <string>
#include <unordered_map>
#include <vector>

#include "../QuickDrawFormats.hh"
#include "../ResourceFile.hh"

namespace ResourceDASM {

// Ambrosia-btSP-HrSp.cc
phosg::ImageRGBA8888N decode_btSP(const std::string& data, const std::vector<ColorTableEntry>& clut);
phosg::ImageRGBA8888N decode_HrSp(
    const std::string& data, const std::vector<ColorTableEntry>& clut, size_t header_size);
std::vector<phosg::ImageRGBA8888N> decode_SprD(const std::string& data, const std::vector<ColorTableEntry>& clut);

// Blobbo-BTMP-PMP8.cc
phosg::ImageG1 decode_BTMP(const std::string& data);
phosg::ImageRGB888 decode_PMP8(const std::string& data, const std::vector<ColorTableEntry>& clut);

// Bungie-256.cc
std::vector<phosg::ImageRGBA8888N> decode_pathways_256(const std::string& data);
std::vector<phosg::ImageRGBA8888N> decode_marathon_256(const std::string& data);

// DarkCastle-DC2.cc
phosg::ImageRGBA8888N decode_DC2(const std::string& data);

// DarkCastle-PPCT-PSCR.cc
std::string decompress_PSCR_v1(phosg::StringReader& r);
std::string decompress_PSCR_v2(phosg::StringReader& r);
std::string decompress_PPCT(phosg::StringReader& r, size_t expected_bits = 0);
phosg::ImageG1 decode_PBLK(const std::string& data);
phosg::ImageG1 decode_PSCR(const std::string& data, bool is_v2);
phosg::ImageGA11 decode_PPCT(const std::string& data);

// DinoParkTycoon-BMap.cc
phosg::ImageGA11 decode_BMap(const std::string& data);
std::vector<phosg::ImageG1> decode_XBig(const std::string& data);
phosg::ImageRGBA8888N decode_XMap(const std::string& data, const std::vector<ColorTableEntry>& clut);

// Factory-1img-4img-8img.cc
phosg::ImageG1 decode_1img(const std::string& data);
phosg::ImageRGB888 decode_4img(const std::string& data, const std::vector<ColorTableEntry>& pltt);
phosg::ImageRGB888 decode_8img(const std::string& data, const std::vector<ColorTableEntry>& pltt);

// Greebles-GSIF.cc
phosg::ImageRGB888 decode_GSIF(const std::string& data, const std::vector<ColorTableEntry>& pltt);

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
std::unordered_map<std::string, DecodedSHPDImage> decode_SHPD_collection(
    ResourceFile& rf,
    const std::string& data_fork_contents,
    const std::vector<ColorTableEntry>& clut,
    SHPDVersion version);
std::unordered_map<std::string, phosg::ImageRGBA8888N> decode_SHPD_collection_images_only(
    ResourceFile& rf,
    const std::string& data_fork_contents,
    const std::vector<ColorTableEntry>& clut,
    SHPDVersion version);

// MECC-Imag.cc
std::vector<phosg::ImageRGB888> decode_Imag(
    const std::string& data, const std::vector<ColorTableEntry>& clut, bool use_later_formats);

// Odyssey-NPIC-SHPS.cc
ResourceFile::DecodedPICTResource decode_NPIC(const std::string& data);
phosg::ImageRGBA8888N decode_NTEX(const std::string& data);
std::vector<phosg::ImageRGBA8888N> decode_SHPS(const std::string& data);

// Presage.cc
phosg::ImageGA11 decode_presage_mono_image(
    phosg::StringReader& r, size_t width, size_t height, bool use_and_compositing);
phosg::ImageRGBA8888N decode_presage_v1_commands(
    phosg::StringReader& r, size_t w, size_t h, const std::vector<ColorTableEntry>& clut);
phosg::ImageRGBA8888N decode_presage_v2_commands(
    phosg::StringReader& r, size_t w, size_t h, const std::vector<ColorTableEntry>& clut);
std::vector<phosg::ImageRGBA8888N> decode_PPSS(const std::string& data, const std::vector<ColorTableEntry>& clut);
std::vector<phosg::ImageRGBA8888N> decode_Pak(const std::string& data, const std::vector<ColorTableEntry>& clut);

// PrinceOfPersia2-SHAP.cc
std::string decompress_SHAP_lz(const std::string& data);
std::string decompress_SHAP_standard_rle(const std::string& data);
std::string decompress_SHAP_rows_rle(const std::string& data, size_t num_rows, size_t row_bytes);
phosg::ImageRGBA8888N decode_SHAP(const std::string& data, const std::vector<ColorTableEntry>& ctbl);

// SimCity2000-SPRT.cc
std::vector<phosg::ImageRGBA8888N> decode_SPRT(const std::string& data, const std::vector<ColorTableEntry>& pltt);

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
DecodedShap3D decode_shap(const std::string& data);

// StepOnIt-sssf.cc
std::vector<phosg::ImageRGBA8888N> decode_sssf(const std::string& data, const std::vector<ColorTableEntry>& clut);

// SwampGas-PPic.cc
std::string decompress_PPic_pixel_map_data(const std::string& data, size_t row_bytes, size_t height);
std::string decompress_PPic_bitmap_data(const std::string& data, size_t row_bytes, size_t height);
std::vector<phosg::ImageRGB888> decode_PPic(const std::string& data, const std::vector<ColorTableEntry>& clut);

// TheZone-Spri.cc
phosg::ImageRGBA8888N decode_Spri(const std::string& data, const std::vector<ColorTableEntry>& clut);

} // namespace ResourceDASM
