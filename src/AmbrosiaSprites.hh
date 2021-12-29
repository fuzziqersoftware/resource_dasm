#include <phosg/Image.hh>
#include <string>
#include <vector>

#include "QuickDrawFormats.hh"

Image decode_btSP_sprite(
    const std::string& data,
    const std::vector<ColorTableEntry>& clut);
Image decode_HrSp_sprite(
    const std::string& data,
    const std::vector<ColorTableEntry>& clut);
