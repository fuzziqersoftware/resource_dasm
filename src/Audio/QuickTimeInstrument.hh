#pragma once

#include <inttypes.h>

#include <phosg/Strings.hh>
#include <string>
#include <unordered_map>
#include <vector>

namespace ResourceDASM {
namespace Audio {

class SSAIInstrument {
public:
  SSAIInstrument(const void* data, size_t size);
  inline SSAIInstrument(const std::string& data) : SSAIInstrument(data.data(), data.size()) {}

  struct KnobEntry {
    uint8_t unknown_a1 = 0;
    uint8_t unknown_a2 = 0;
    uint16_t unknown_a3 = 0;
    uint32_t unknown_a4 = 0;
  };

  std::string name;
  std::vector<KnobEntry> knobs;
  struct KeyRegion {
    uint32_t format = 0;
    uint16_t num_channels = 0;
    uint16_t bits_per_sample = 0;
    float sample_rate = 0.0;
    uint16_t sample_data_number = 0;
    uint32_t frame_count = 0; // TODO: Could also be sample_count or just num_sample_bytes
    uint32_t loop_start_offset = 0; // TODO: Could be in frames, samples, or bytes; we assume frames
    uint32_t loop_end_offset = 0; // TODO: Could be in frames, samples, or bytes; we assume frames
    uint32_t base_note = 0;
    uint32_t key_low = 0;
    uint32_t key_high = 0;
    std::vector<KnobEntry> knobs;
  };
  std::unordered_map<uint32_t, KeyRegion> key_regions;
  std::unordered_map<uint32_t, std::string> sample_datas;

  void parse_blocks(phosg::StringReader& r, size_t block_count, KeyRegion* current_key_region);
  void parse_block(
      uint32_t block_type,
      uint32_t block_number,
      uint32_t child_count,
      phosg::StringReader r,
      KeyRegion* current_key_region);
};

} // namespace Audio
} // namespace ResourceDASM
