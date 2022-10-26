#include <stdint.h>

#include <phosg/Image.hh>
#include <phosg/Vector.hh>
#include <string>
#include <vector>
#include <unordered_map>

#include "../QuickDrawFormats.hh"
#include "../ResourceFile.hh"

// Ambrosia-btSP-HrSp.cc
Image decode_btSP(
    const std::string& data, const std::vector<ColorTableEntry>& clut);
Image decode_HrSp(
    const std::string& data,
    const std::vector<ColorTableEntry>& clut,
    size_t header_size);
std::vector<Image> decode_SprD(
    const std::string& data,
    const std::vector<ColorTableEntry>& clut);

// Blobbo-BTMP-PMP8.cc
Image decode_BTMP(const std::string& data);
Image decode_PMP8(const std::string& data, const std::vector<ColorTableEntry>& clut);

// DarkCastle-DC2.cc
Image decode_DC2(const std::string& data);

// DarkCastle-PPCT-PSCR.cc
std::string decompress_PSCR_v1(StringReader& r);
std::string decompress_PSCR_v2(StringReader& r);
std::string decompress_PPCT(StringReader& r, size_t expected_bits = 0);
Image decode_PBLK(const std::string& data);
Image decode_PPCT(const std::string& data);
Image decode_PSCR(const std::string& data, bool is_v2);

// DinoParkTycoon-BMap.cc
Image decode_BMap(const std::string& data);
Image decode_XMap(const std::string& data, const std::vector<ColorTableEntry>& clut);
std::vector<Image> decode_XBig(const std::string& data);

// Presage.cc
Image decode_presage_mono_image(StringReader& r, size_t width, size_t height, bool use_and_compositing);
Image decode_presage_v1_commands(StringReader& r, size_t w, size_t h, const std::vector<ColorTableEntry>& clut);
Image decode_presage_v2_commands(StringReader& r, size_t w, size_t h, const std::vector<ColorTableEntry>& clut);
std::vector<Image> decode_PPSS(const std::string& data, const std::vector<ColorTableEntry>& clut);
std::vector<Image> decode_Pak(const std::string& data, const std::vector<ColorTableEntry>& clut);

// MECC-Imag.cc
std::vector<Image> decode_Imag(
    const std::string& data,
    const std::vector<ColorTableEntry>& clut,
    bool use_later_formats);

// Factory-1img-4img-8img.cc
Image decode_1img(const std::string& data);
Image decode_4img(const std::string& data, const std::vector<ColorTableEntry>& pltt);
Image decode_8img(const std::string& data, const std::vector<ColorTableEntry>& pltt);

// Greebles-GSIF.cc
Image decode_GSIF(const std::string& data, const std::vector<ColorTableEntry>& pltt);

// Lemmings-PrinceOfPersia-SHPD.cc
enum class SHPDVersion {
  LEMMINGS_V1 = 0,
  LEMMINGS_V2,
  PRINCE_OF_PERSIA,
};
struct DecodedSHPDImage {
  int16_t origin_x;
  int16_t origin_y;
  Image image;
};
std::string decompress_SHPD_data(StringReader& r);
std::unordered_map<std::string, DecodedSHPDImage> decode_SHPD_collection(
    ResourceFile& rf,
    const std::string& data_fork_contents,
    const std::vector<ColorTableEntry>& clut,
    SHPDVersion version);
std::unordered_map<std::string, Image> decode_SHPD_collection_images_only(
    ResourceFile& rf,
    const std::string& data_fork_contents,
    const std::vector<ColorTableEntry>& clut,
    SHPDVersion version);

// PrinceOfPersia2-SHAP.cc
std::string decompress_SHAP_lz(const std::string& data);
std::string decompress_SHAP_standard_rle(const std::string& data);
std::string decompress_SHAP_rows_rle(const std::string& data, size_t num_rows, size_t row_bytes);
Image decode_SHAP(const std::string& data, const std::vector<ColorTableEntry>& ctbl);

// SimCity2000-SPRT.cc
std::vector<Image> decode_SPRT(const std::string& data, const std::vector<ColorTableEntry>& pltt);

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
  std::vector<Vector3<double>> vertices;
  std::vector<Plane> planes;
  std::vector<Vector3<double>> top_view_vertices;
  std::vector<Line> top_view_lines;

  std::string model_as_stl() const;
  std::string model_as_obj() const;
  std::string top_view_as_svg() const;
};
DecodedShap3D decode_shap(const std::string& data);

// StepOnIt-sssf.cc
std::vector<Image> decode_sssf(const std::string& data, const std::vector<ColorTableEntry>& clut);

// SwampGas-PPic.cc
std::string decompress_PPic_pixel_map_data(const std::string& data, size_t row_bytes, size_t height);
std::string decompress_PPic_bitmap_data(const std::string& data, size_t row_bytes, size_t height);
std::vector<Image> decode_PPic(const std::string& data, const std::vector<ColorTableEntry>& clut);

// TheZone-Spri.cc
Image decode_Spri(const std::string& data, const std::vector<ColorTableEntry>& clut);
