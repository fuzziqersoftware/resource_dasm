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
  bool afc_large_frames;
  mutable std::vector<float> decoded_samples;
  size_t num_channels;
  size_t sample_rate;

  uint8_t base_note;
  // if both of the following are zero, there's no loop
  size_t loop_start;
  size_t loop_end;

  int64_t sound_id;

  std::string source_filename;
  uint32_t source_offset;
  uint32_t source_size;

  uint32_t aw_file_index;
  uint32_t wave_table_index;

  const std::vector<float>& samples() const;
};

struct VelocityRegion {
  uint8_t vel_low;
  uint8_t vel_high;
  uint16_t sample_bank_id;
  uint16_t sound_id;
  float freq_mult;
  float volume_mult;
  bool constant_pitch;

  int8_t base_note;
  const Sound* sound;

  VelocityRegion(uint8_t vel_low, uint8_t vel_high, uint16_t sample_bank_id,
      uint16_t sound_id, float freq_mult, float volume_mult,
      int8_t base_note = -1, bool constant_pitch = false);
};

struct KeyRegion {
  uint8_t key_low;
  uint8_t key_high;
  std::vector<VelocityRegion> vel_regions;

  KeyRegion(uint8_t key_low, uint8_t key_high);

  const VelocityRegion& region_for_velocity(uint8_t velocity) const;
};

struct Instrument {

  uint32_t id;
  std::vector<KeyRegion> key_regions;

  explicit Instrument(uint32_t id);

  const KeyRegion& region_for_key(uint8_t key) const;
};

struct InstrumentBank {
  uint32_t id;
  uint32_t chunk_id;
  std::unordered_map<uint32_t, Instrument> id_to_instrument;

  explicit InstrumentBank(uint32_t id);
};

InstrumentBank ibnk_decode(const void* vdata);

} // namespace Audio
} // namespace ResourceDASM
