#pragma once

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>
#include <unordered_map>
#include <vector>

namespace ResourceDASM {
namespace Audio {

struct Sound {
  mutable std::string afc_data;
  bool afc_large_frames = false;
  mutable std::vector<float> decoded_samples;
  size_t num_channels = 1;
  size_t sample_rate = 0;

  uint8_t base_note = 0;
  // If both of the following are zero, there's no loop
  size_t loop_start = 0;
  size_t loop_end = 0;

  int64_t sound_id = -1;

  std::string source_filename;
  uint32_t source_offset = 0;
  uint32_t source_size = 0;

  uint32_t aw_file_index = 0;
  uint32_t wave_table_index = 0;

  const std::vector<float>& samples() const;
};

struct VelocityRegion {
  uint8_t vel_low = 0;
  uint8_t vel_high = 0;
  uint16_t sample_bank_id = 0;
  uint16_t sound_id = 0;
  float freq_mult = 1.0;
  float volume_mult = 1.0;
  bool constant_pitch = false;

  int8_t base_note = -1;
  const Sound* sound = nullptr;
};

struct KeyRegion {
  uint8_t key_low = 0;
  uint8_t key_high = 0;
  std::vector<VelocityRegion> vel_regions;

  const VelocityRegion& region_for_velocity(uint8_t velocity) const;
};

struct Instrument {
  uint32_t id = 0;
  std::vector<KeyRegion> key_regions;

  const KeyRegion& region_for_key(uint8_t key) const;
};

struct InstrumentBank {
  uint32_t id = 0;
  uint32_t chunk_id = 0;
  std::unordered_map<uint32_t, Instrument> id_to_instrument;
};

InstrumentBank ibnk_decode(const void* vdata);

} // namespace Audio
} // namespace ResourceDASM
