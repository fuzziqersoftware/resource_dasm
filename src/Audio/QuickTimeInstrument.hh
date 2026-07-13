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

  uint32_t id;
  std::string name;
  std::string copyright_wrt;
  std::string copyright_cpy;
  std::string info_string;
  std::vector<KnobEntry> knobs;
  struct KeyRegion {
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
  std::unordered_map<uint32_t, KeyRegion> key_regions; // Keyed by block_number
  struct SampleData {
    int64_t smin_block_number = -1; // -1 = not part of an smin
    uint32_t sdat_block_number = 0;
    std::string data;
    std::string name;
  };
  std::unordered_map<uint32_t, SampleData> sample_datas;

protected:
  void parse_blocks(
      phosg::StringReader& r, size_t block_count, KeyRegion* current_key_region, SampleData* current_sample_data);
  void parse_block(
      uint32_t block_type,
      uint32_t block_number,
      uint32_t child_count,
      phosg::StringReader r,
      KeyRegion* current_key_region,
      SampleData* current_sample_data);
};

class TuneResource {
public:
  TuneResource(const void* data, size_t size);
  inline TuneResource(const std::string& data) : TuneResource(data.data(), data.size()) {}

  std::string midi() const;

  struct MIDIEvent {
    uint64_t when;
    std::vector<uint8_t> data;
  };

  struct Event {
    virtual ~Event() = default;
    virtual void add_midi_events(std::vector<MIDIEvent>& events) const = 0;
    uint64_t when;
  };
  struct NoteEvent : Event {
    virtual void add_midi_events(std::vector<MIDIEvent>& events) const;
    uint64_t duration;
    uint8_t channel;
    uint8_t key;
    uint8_t vel;
  };
  struct PitchBendEvent : Event {
    virtual void add_midi_events(std::vector<MIDIEvent>& events) const;
    uint8_t channel;
    int16_t value;
  };
  struct ControllerEvent : Event {
    virtual void add_midi_events(std::vector<MIDIEvent>& events) const;
    uint8_t channel;
    uint8_t message;
    uint8_t value;
  };
  struct ChannelSetupEvent : Event {
    virtual void add_midi_events(std::vector<MIDIEvent>& events) const;
    uint8_t channel;
    uint8_t instrument_number;
    uint8_t volume; // 00-7F
    uint8_t panning; // 00-7F; 40 default
    uint8_t pitch_bend; // 00-7F; 40 default
    std::string collection_name;
    std::string instrument_name;
  };
  struct TrackEndEvent : Event {
    virtual void add_midi_events(std::vector<MIDIEvent>& events) const;
  };

  std::vector<std::unique_ptr<Event>> events;
};

} // namespace Audio
} // namespace ResourceDASM
