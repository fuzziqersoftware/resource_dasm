#include <stdint.h>

#include <phosg/Image.hh>
#include <string>
#include <vector>
#include <unordered_map>

#include "../QuickDrawFormats.hh"

// Ambrosia-btSP-HrSp.cc
Image decode_btSP(const std::string& data, const std::vector<ColorTableEntry>& clut);
Image decode_HrSp(const std::string& data, const std::vector<ColorTableEntry>& clut);

// DarkCastle-DC2.cc
Image decode_DC2(const void* input_data);

// Greebles-GSIF.cc
Image decode_GSIF(const std::string& data, const std::vector<ColorTableEntry>& pltt);

// PrinceOfPersia2-SHAP.cc
Image decode_SHAP(const std::string& data, const std::vector<ColorTableEntry>& ctbl);

// SimCity2000-SPRT.cc
std::vector<Image> decode_SPRT(const std::string& data, const std::vector<ColorTableEntry>& pltt);

// StepOnIt-sssf.cc
std::vector<Image> decode_sssf(const std::string& data, const std::vector<ColorTableEntry>& clut);

// TheZone-Spri.cc
Image decode_Spri(const std::string& data, const std::vector<ColorTableEntry>& clut);
