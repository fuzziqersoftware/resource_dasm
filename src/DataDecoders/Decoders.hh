#pragma once

#include <sys/types.h>

#include <string>

#include "../ResourceFile.hh"



// DinoParkTycoon-LZSS-RLE.cc
std::string decompress_dinopark_tycoon_lzss(const void* data, size_t size);
std::string decompress_dinopark_tycoon_lzss(const std::string& data);
std::string decompress_dinopark_tycoon_rle(const void* data, size_t size);
std::string decompress_dinopark_tycoon_rle(const std::string& data);
std::string decompress_dinopark_tycoon_data(const void* data, size_t size);
std::string decompress_dinopark_tycoon_data(const std::string& data);

// Flashback-LZSS.cc
std::string decompress_flashback_lzss(const void* data, size_t size, size_t max_output_bytes = 0);
std::string decompress_flashback_lzss(const std::string& data, size_t max_output_bytes = 0);

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
