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
  uint32_t resource_id = 0;
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
  std::string disassemble() const;

  struct MIDIEvent {
    uint64_t when;
    std::vector<uint8_t> data;
  };

  struct Event {
    uint64_t when = 0;
    uint8_t channel = 0;
    size_t source_offset = 0;
    std::string source_data;

    virtual ~Event() = default;
    virtual void add_midi_events(std::vector<MIDIEvent>& events) const = 0;
    virtual std::string disassemble() const = 0;
    std::string disassembly_prefix() const;
  };

  struct NoteEvent : Event {
    uint64_t duration;
    uint8_t key;
    uint8_t vel;

    virtual void add_midi_events(std::vector<MIDIEvent>& events) const;
    virtual std::string disassemble() const;
  };

  struct NoteOffEvent : Event {
    uint8_t key;
    uint8_t vel;

    virtual void add_midi_events(std::vector<MIDIEvent>& events) const;
    virtual std::string disassemble() const;
  };

  struct PitchBendEvent : Event {
    int16_t value;

    virtual void add_midi_events(std::vector<MIDIEvent>& events) const;
    virtual std::string disassemble() const;
  };

  struct ControllerEvent : Event {
    uint8_t message;
    uint8_t value;

    virtual void add_midi_events(std::vector<MIDIEvent>& events) const;
    virtual std::string disassemble() const;
  };

  struct ChannelSetupEvent : Event {
    uint8_t volume; // 00-7F
    uint8_t panning; // 00-7F; 40 default
    uint16_t pitch_bend; // 0000-4000; 2000 default
    uint32_t instrument_number;
    std::string collection_name;
    std::string instrument_name;

    virtual void add_midi_events(std::vector<MIDIEvent>& events) const;
    virtual std::string disassemble() const;
  };

  std::vector<std::unique_ptr<Event>> events;
};

} // namespace Audio
} // namespace ResourceDASM
