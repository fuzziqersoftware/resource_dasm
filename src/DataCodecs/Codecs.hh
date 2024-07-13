#pragma once

#include <sys/types.h>

#include <string>

#include "../ResourceFile.hh"

namespace ResourceDASM {

// PackBits.cc
std::string unpack_bits(const void* data, size_t size);
std::string unpack_bits(const std::string& data);
// unpacks until `uncompressed_size` have been written to `uncompressed_data`
void unpack_bits(StringReader& in, void* uncompressed_data, uint32_t uncompressed_size);

std::string pack_bits(const void* data, size_t size);
std::string pack_bits(const std::string& data);

std::string decompress_packed_icns_data(const void* data, size_t size);
std::string decompress_packed_icns_data(const std::string& data);

// Returns the number of bytes written to `out`
uint32_t compress_strided_icns_data(StringWriter& out, const void* uncompressed_data, uint32_t uncompressed_size, uint32_t uncompressed_stride);

// Bungie.cc
std::string unpack_pathways(const void* data, size_t size);
std::string unpack_pathways(const std::string& data);

// DinoParkTycoon-LZSS-RLE.cc
std::string decompress_dinopark_tycoon_lzss(const void* data, size_t size);
std::string decompress_dinopark_tycoon_lzss(const std::string& data);
std::string decompress_dinopark_tycoon_rle(const void* data, size_t size);
std::string decompress_dinopark_tycoon_rle(const std::string& data);
std::string decompress_dinopark_tycoon_data(const void* data, size_t size);
std::string decompress_dinopark_tycoon_data(const std::string& data);

// Presage-LZSS.cc
std::string decompress_presage_lzss(StringReader& r, size_t max_output_bytes = 0);
std::string decompress_presage_lzss(const void* data, size_t size, size_t max_output_bytes = 0);
std::string decompress_presage_lzss(const std::string& data, size_t max_output_bytes = 0);

// MacSki-RUN4-COOK-CO2K.cc
std::string decompress_macski_RUN4(const void* data, size_t size);
std::string decompress_macski_RUN4(const std::string& data);
std::string decompress_macski_COOK_CO2K(const void* data, size_t size);
std::string decompress_macski_COOK_CO2K(const std::string& data);
std::string decompress_macski_multi(const void* data, size_t size);
std::string decompress_macski_multi(const std::string& data);

// SoundMusicSys-LZSS.cc
std::string decompress_soundmusicsys_lzss(const void* vsrc, size_t size);
std::string decompress_soundmusicsys_lzss(const std::string& data);

} // namespace ResourceDASM
