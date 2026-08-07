#pragma once

#include <inttypes.h>

#include <phosg/Strings.hh>
#include <string>
#include <unordered_map>
#include <vector>

namespace ResourceDASM {
namespace Audio {

enum QTMAKnobID {
  kQTMSKnobStartID = 0x02000000,
  kQTMSKnobVolumeAttackTimeID = 0x02000001,
  kQTMSKnobVolumeDecayTimeID = 0x02000002,
  kQTMSKnobVolumeSustainLevelID = 0x02000003,
  kQTMSKnobVolumeRelease1RateID = 0x02000004,
  kQTMSKnobVolumeDecayKeyScalingID = 0x02000005,
  kQTMSKnobVolumeReleaseTimeID = 0x02000006,
  kQTMSKnobVolumeLFODelayID = 0x02000007,
  kQTMSKnobVolumeLFORampTimeID = 0x02000008,
  kQTMSKnobVolumeLFOPeriodID = 0x02000009,
  kQTMSKnobVolumeLFOShapeID = 0x0200000A,
  kQTMSKnobVolumeLFODepthID = 0x0200000B,
  kQTMSKnobVolumeOverallID = 0x0200000C,
  kQTMSKnobVolumeVelocity127ID = 0x0200000D,
  kQTMSKnobVolumeVelocity96ID = 0x0200000E,
  kQTMSKnobVolumeVelocity64ID = 0x0200000F,
  kQTMSKnobVolumeVelocity32ID = 0x02000010,
  kQTMSKnobVolumeVelocity16ID = 0x02000011,
  kQTMSKnobPitchTransposeID = 0x02000012,
  kQTMSKnobPitchLFODelayID = 0x02000013,
  kQTMSKnobPitchLFORampTimeID = 0x02000014,
  kQTMSKnobPitchLFOPeriodID = 0x02000015,
  kQTMSKnobPitchLFOShapeID = 0x02000016,
  kQTMSKnobPitchLFODepthID = 0x02000017,
  kQTMSKnobPitchLFOQuantizeID = 0x02000018,
  kQTMSKnobStereoDefaultPanID = 0x02000019,
  kQTMSKnobStereoPositionKeyScalingID = 0x0200001A,
  kQTMSKnobPitchLFOOffsetID = 0x0200001B,
  kQTMSKnobExclusionGroupID = 0x0200001C,
  kQTMSKnobSustainTimeID = 0x0200001D,
  kQTMSKnobSustainInfiniteID = 0x0200001E,
  kQTMSKnobVolumeLFOStereoID = 0x0200001F,
  kQTMSKnobVelocityLowID = 0x02000020,
  kQTMSKnobVelocityHighID = 0x02000021,
  kQTMSKnobVelocitySensitivityID = 0x02000022,
  kQTMSKnobPitchSensitivityID = 0x02000023,
  kQTMSKnobVolumeLFODepthFromWheelID = 0x02000024,
  kQTMSKnobPitchLFODepthFromWheelID = 0x02000025,
  kQTMSKnobVolumeExpOptionsID = 0x02000026,
  kQTMSKnobEnv1AttackTimeID = 0x02000027,
  kQTMSKnobEnv1DecayTimeID = 0x02000028,
  kQTMSKnobEnv1SustainLevelID = 0x02000029,
  kQTMSKnobEnv1SustainTimeID = 0x0200002A,
  kQTMSKnobEnv1SustainInfiniteID = 0x0200002B,
  kQTMSKnobEnv1ReleaseTimeID = 0x0200002C,
  kQTMSKnobEnv1ExpOptionsID = 0x0200002D,
  kQTMSKnobEnv2AttackTimeID = 0x0200002E,
  kQTMSKnobEnv2DecayTimeID = 0x0200002F,
  kQTMSKnobEnv2SustainLevelID = 0x02000030,
  kQTMSKnobEnv2SustainTimeID = 0x02000031,
  kQTMSKnobEnv2SustainInfiniteID = 0x02000032,
  kQTMSKnobEnv2ReleaseTimeID = 0x02000033,
  kQTMSKnobEnv2ExpOptionsID = 0x02000034,
  kQTMSKnobPitchEnvelopeID = 0x02000035,
  kQTMSKnobPitchEnvelopeDepthID = 0x02000036,
  kQTMSKnobFilterKeyFollowID = 0x02000037,
  kQTMSKnobFilterTransposeID = 0x02000038,
  kQTMSKnobFilterQID = 0x02000039,
  kQTMSKnobFilterFrequencyEnvelopeID = 0x0200003A,
  kQTMSKnobFilterFrequencyEnvelopeDepthID = 0x0200003B,
  kQTMSKnobFilterQEnvelopeID = 0x0200003C,
  kQTMSKnobFilterQEnvelopeDepthID = 0x0200003D,
  kQTMSKnobReverbThresholdID = 0x0200003E,
  kQTMSKnobVolumeAttackVelScalingID = 0x0200003F,
};

class SSAIInstrument {
public:
  SSAIInstrument(const void* data, size_t size);
  inline SSAIInstrument(const std::string& data) : SSAIInstrument(data.data(), data.size()) {}

  static const char* name_for_knob(uint32_t knob_id);

  uint32_t id;
  uint32_t resource_id = 0;
  std::string name;
  std::string copyright_wrt;
  std::string copyright_cpy;
  std::string info_string;
  std::unordered_map<uint32_t, int32_t> knobs;
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
    std::unordered_map<uint32_t, int32_t> knobs;
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
