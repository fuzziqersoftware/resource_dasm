#include "Instrument.hh"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <format>
#include <map>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <vector>

#include "QuickTimeInstrument.hh"

namespace ResourceDASM {
namespace Audio {

static constexpr uint32_t SSAI_TYPE = 0x73736169; // 'ssai'
static constexpr uint32_t SEAN_TYPE = 0x7365616E; // 'sean'
static constexpr uint32_t TONE_TYPE = 0x746F6E65; // 'tone'; kaiToneDescType
static constexpr uint32_t KNBL_TYPE = 0x6B6E626C; // 'knbl'; kaiKnobListType
static constexpr uint32_t SINF_TYPE = 0x73696E66; // 'sinf'; kaiKeyRangeInfoType
static constexpr uint32_t SDSC_TYPE = 0x73647363; // 'sdsc'; kaiSampleDescType
static constexpr uint32_t SMIN_TYPE = 0x736D696E; // 'smin'; kaiSampleInfoType
static constexpr uint32_t SNAM_TYPE = 0x736E616D; // 'snam'
static constexpr uint32_t SDAT_TYPE = 0x73646174; // 'sdat'; kaiSampleDataType
static constexpr uint32_t QUAL_TYPE = 0x7175616C; // 'qual'; kaiInstGMQualityType
static constexpr uint32_t QUID_TYPE = 0x71756964; // 'quid'; kaiSampleDataQUIDType
static constexpr uint32_t IINF_TYPE = 0x69696E66; // 'iinf'; kaiInstInfoType
static constexpr uint32_t IREF_TYPE = 0x69726566; // 'iref'; kaiInstrumentRefType
static constexpr uint32_t COPYRIGHT_WRT_TYPE = 0xA9777274; // '©wrt' (in MacRoman); kaiWriterType
static constexpr uint32_t COPYRIGHT_CPY_TYPE = 0xA9637079; // '©cpy' (in MacRoman); kaiCopyrightType
static constexpr uint32_t STR_TYPE = 0x73747220; // 'str '; kaiOtherStrType
static constexpr uint32_t MUSI_TYPE = 0x6D757369; // 'musi'
static constexpr uint32_t SS_TYPE = 0x73732020; // 'ss  '
// Block types in the QT headers which are not represented here (yet):
//   kaiNoteRequestInfoType        = FOUR_CHAR_CODE('ntrq')
//   kaiPictType                   = FOUR_CHAR_CODE('pict')
//   kaiLibraryInfoType            = FOUR_CHAR_CODE('linf')
//   kaiLibraryDescType            = FOUR_CHAR_CODE('ldsc')

struct FileHeader {
  /* 00 */ phosg::be_uint32_t size = 0; // Includes this header
  /* 04 */ phosg::be_uint32_t type = 0;
  /* 08 */ phosg::be_uint32_t block_number = 0;
  /* 0C */
} __attribute__((packed));

struct BlockHeader {
  /* 00 */ phosg::be_uint32_t size = 0; // Includes this header
  /* 04 */ phosg::be_uint32_t type = 0;
  /* 08 */ phosg::be_uint32_t block_number = 0;
  /* 0C */ phosg::be_uint32_t child_count = 0;
  /* 10 */ phosg::be_uint32_t unknown_a1 = 0;
  /* 14 */
} __attribute__((packed));

struct ToneBlock {
  /* 14 */ phosg::be_uint32_t unknown_a1[9] = {};
  /* 38 */ uint8_t name[0x20] = {}; // pstring; size is uncertain (may be shorter)
  /* 58 */ phosg::be_uint32_t unknown_a2 = 0xFFFFFFFF;
  /* 5C */ phosg::be_uint32_t resource_id = 0;
  /* 60 */
} __attribute__((packed));

struct KNBLBlock { // Knob list
  struct Entry {
    phosg::be_uint32_t number = 0;
    phosg::be_int32_t value = 0;
  } __attribute__((packed));

  /* 14 */ phosg::be_uint32_t entry_count = 0;
  /* 18 */ phosg::be_uint32_t flags = 0;
  /* 1C */ // Entries follow here
} __attribute__((packed));

struct SDSCBlock { // Sample description
  /* 14 */ phosg::be_uint32_t format = 0; // E.g. 'raw '
  /* 18 */ phosg::be_uint16_t num_channels = 0;
  /* 1A */ phosg::be_uint16_t bits_per_sample = 0;
  /* 1C */ phosg::be_uint16_t sample_rate_integer = 0; // Whole number part of a Fixed
  /* 1E */ phosg::be_uint16_t sample_rate_fractional = 0; // Fractional part of a Fixed
  /* 20 */ phosg::be_uint16_t sdat_block_number = 0;
  /* 22 */ phosg::be_uint32_t frame_offset = 0; // Possibly just for internal use? (See MPW headers)
  /* 26 */ phosg::be_uint32_t frame_count = 0; // TODO: Could also be sample_count or just num_sample_bytes
  /* 2A */ phosg::be_uint32_t loop_type = 0; // TODO: We don't use this; find out what the types are and implement them
  /* 2E */ phosg::be_uint32_t loop_start_offset = 0; // TODO: Could be in frames, samples, or bytes; we assume frames
  /* 32 */ phosg::be_uint32_t loop_end_offset = 0; // TODO: Could be in frames, samples, or bytes; we assume frames
  /* 36 */ phosg::be_uint32_t base_note = 0;
  /* 3A */ phosg::be_uint32_t key_low = 0;
  /* 3E */ phosg::be_uint32_t key_high = 0;
  /* 42 */
} __attribute__((packed));

struct QualBlock {
  /* 14 */ uint8_t unknown_a3[4] = {};
  /* 18 */
} __attribute__((packed));

struct QuidBlock {
  /* 14 */ uint8_t unknown_a3[0x10] = {};
  /* 24 */
} __attribute__((packed));

template <size_t BufSize>
std::string decode_pstring(const uint8_t* data) {
  if (*data > (BufSize - 1)) {
    throw std::runtime_error("Pascal string overflows buffer");
  }
  return std::string(reinterpret_cast<const char*>(data + 1), *data);
}

SSAIInstrument::SSAIInstrument(const void* data, size_t size) {
  phosg::StringReader r(data, size);

  const auto& root_header = r.get<FileHeader>();
  if (root_header.type != SSAI_TYPE) {
    throw std::runtime_error("Input is not an ssai file");
  }
  if (root_header.size != r.size()) {
    throw std::runtime_error("Header size field does not match file size");
  }

  this->parse_blocks(r, 1, nullptr, nullptr);
}

template <typename T>
static const T& get_fixed_block(phosg::StringReader& r, uint32_t block_type) {
  if (r.remaining() != sizeof(T)) {
    throw std::runtime_error(std::format("{:08X} block size is incorrect", block_type));
  }
  return r.get<T>();
}

void SSAIInstrument::parse_blocks(
    phosg::StringReader& r, size_t block_count, KeyRegion* current_key_region, SampleData* current_sample_data) {
  for (; block_count > 0; block_count--) {
    const auto& header = r.get<BlockHeader>();
    if (header.size < sizeof(BlockHeader)) {
      throw std::runtime_error("Invalid block header size");
    }
    this->parse_block(
        header.type,
        header.block_number,
        header.child_count,
        r.sub(r.where(), header.size - sizeof(header)),
        current_key_region,
        current_sample_data);
    r.skip(header.size - sizeof(header));
  }
}

void SSAIInstrument::parse_block(
    uint32_t block_type,
    uint32_t block_number,
    uint32_t child_count,
    phosg::StringReader r,
    KeyRegion* current_key_region,
    SampleData* current_sample_data) {
  switch (block_type) {
    case SEAN_TYPE:
    case IINF_TYPE:
    case IREF_TYPE:
      this->parse_blocks(r, child_count, current_key_region, current_sample_data);
      break;
    case SMIN_TYPE: {
      auto emplace_ret = this->sample_datas.emplace(block_number, SampleData{});
      if (!emplace_ret.second) {
        throw std::runtime_error(std::format("Duplicate global sample number {}", block_number));
      }
      auto& sample_data = emplace_ret.first->second;
      sample_data.smin_block_number = block_number;
      sample_data.sdat_block_number = 0;
      this->parse_blocks(r, child_count, current_key_region, &sample_data);
      break;
    }
    case SINF_TYPE:
      this->parse_blocks(r, child_count, &this->key_regions[block_number], current_sample_data);
      break;

    case TONE_TYPE: {
      const auto& tone_block = get_fixed_block<ToneBlock>(r, block_type);
      // Only update the name and resource ID if this block isn't a reference to another instrument. `tone` may appear
      // in the hierarchy ssai->sean->tone in which case it's the instrument metadata; it may also appear within an
      // sinf block in which case it's a reference to another instrument's samples
      if (!current_key_region && !current_sample_data) {
        // TODO: There might be other important stuff in ToneBlock too
        this->name = decode_pstring<0x20>(tone_block.name);
        if (this->resource_id == 0) {
          this->resource_id = tone_block.resource_id;
        }
      }
      break;
    }

    case KNBL_TYPE: {
      const auto& knbl_block = r.get<KNBLBlock>();
      auto& knobs = current_key_region ? current_key_region->knobs : this->knobs;
      if (!knobs.empty()) {
        throw std::runtime_error("Received multiple knob lists in same context");
      }
      for (size_t z = 0; z < knbl_block.entry_count; z++) {
        const auto& entry = r.get<KNBLBlock::Entry>();
        knobs.emplace(entry.number, entry.value);
      }
      break;
    }

    case SDSC_TYPE: {
      if (!current_key_region) {
        throw std::runtime_error("Received sdsc block outside of sinf block");
      }
      const auto& sdsc = r.get<SDSCBlock>();
      current_key_region->num_channels = sdsc.num_channels;
      current_key_region->bits_per_sample = sdsc.bits_per_sample;
      current_key_region->sample_rate = sdsc.sample_rate_integer + (static_cast<float>(sdsc.sample_rate_fractional) / 0x10000);
      current_key_region->sample_data_number = sdsc.sdat_block_number;
      current_key_region->frame_count = sdsc.frame_count;
      current_key_region->loop_start_offset = sdsc.loop_start_offset;
      current_key_region->loop_end_offset = sdsc.loop_end_offset;
      current_key_region->base_note = sdsc.base_note;
      current_key_region->key_low = sdsc.key_low;
      current_key_region->key_high = sdsc.key_high;
      break;
    }

    case SDAT_TYPE: {
      // Apparently sdat may appear within smin, or at the top level. If it appears within smin, it should be keyed by
      // the smin's block number; if it appears at the top level it should be keyed by its own block number
      if (current_sample_data) {
        current_sample_data->data = r.read(r.remaining());
      } else {
        auto& sample_data = this->sample_datas[block_number];
        sample_data.data = r.read(r.remaining());
        sample_data.smin_block_number = -1;
        sample_data.sdat_block_number = block_number;
      }
      break;
    }
    case QUAL_TYPE:
      get_fixed_block<QualBlock>(r, QUAL_TYPE);
      break;
    case QUID_TYPE:
      get_fixed_block<QuidBlock>(r, QUID_TYPE);
      break;
    case SNAM_TYPE:
      if (!current_sample_data) {
        throw std::runtime_error("Received snam block outside of smin block");
      }
      current_sample_data->name = r.all();
      break;
    case COPYRIGHT_WRT_TYPE:
      this->copyright_wrt = r.all();
      break;
    case COPYRIGHT_CPY_TYPE:
      this->copyright_cpy = r.all();
      break;
    case STR_TYPE:
      this->info_string = r.all();
      break;
    default:
      throw std::runtime_error(std::format("Unknown block type: {:08X}", block_type));
  }
}

const char* SSAIInstrument::name_for_knob(uint32_t knob_id) {
  static constexpr std::array<const char*, 0x40> names{
      /* 02000000 */ "kQTMSKnobStartID",
      /* 02000001 */ "kQTMSKnobVolumeAttackTimeID",
      /* 02000002 */ "kQTMSKnobVolumeDecayTimeID",
      /* 02000003 */ "kQTMSKnobVolumeSustainLevelID",
      /* 02000004 */ "kQTMSKnobVolumeRelease1RateID",
      /* 02000005 */ "kQTMSKnobVolumeDecayKeyScalingID",
      /* 02000006 */ "kQTMSKnobVolumeReleaseTimeID",
      /* 02000007 */ "kQTMSKnobVolumeLFODelayID",
      /* 02000008 */ "kQTMSKnobVolumeLFORampTimeID",
      /* 02000009 */ "kQTMSKnobVolumeLFOPeriodID",
      /* 0200000A */ "kQTMSKnobVolumeLFOShapeID",
      /* 0200000B */ "kQTMSKnobVolumeLFODepthID",
      /* 0200000C */ "kQTMSKnobVolumeOverallID",
      /* 0200000D */ "kQTMSKnobVolumeVelocity127ID",
      /* 0200000E */ "kQTMSKnobVolumeVelocity96ID",
      /* 0200000F */ "kQTMSKnobVolumeVelocity64ID",
      /* 02000010 */ "kQTMSKnobVolumeVelocity32ID",
      /* 02000011 */ "kQTMSKnobVolumeVelocity16ID",
      /* 02000012 */ "kQTMSKnobPitchTransposeID",
      /* 02000013 */ "kQTMSKnobPitchLFODelayID",
      /* 02000014 */ "kQTMSKnobPitchLFORampTimeID",
      /* 02000015 */ "kQTMSKnobPitchLFOPeriodID",
      /* 02000016 */ "kQTMSKnobPitchLFOShapeID",
      /* 02000017 */ "kQTMSKnobPitchLFODepthID",
      /* 02000018 */ "kQTMSKnobPitchLFOQuantizeID",
      /* 02000019 */ "kQTMSKnobStereoDefaultPanID",
      /* 0200001A */ "kQTMSKnobStereoPositionKeyScalingID",
      /* 0200001B */ "kQTMSKnobPitchLFOOffsetID",
      /* 0200001C */ "kQTMSKnobExclusionGroupID",
      /* 0200001D */ "kQTMSKnobSustainTimeID",
      /* 0200001E */ "kQTMSKnobSustainInfiniteID",
      /* 0200001F */ "kQTMSKnobVolumeLFOStereoID",
      /* 02000020 */ "kQTMSKnobVelocityLowID",
      /* 02000021 */ "kQTMSKnobVelocityHighID",
      /* 02000022 */ "kQTMSKnobVelocitySensitivityID",
      /* 02000023 */ "kQTMSKnobPitchSensitivityID",
      /* 02000024 */ "kQTMSKnobVolumeLFODepthFromWheelID",
      /* 02000025 */ "kQTMSKnobPitchLFODepthFromWheelID",
      /* 02000026 */ "kQTMSKnobVolumeExpOptionsID",
      /* 02000027 */ "kQTMSKnobEnv1AttackTimeID",
      /* 02000028 */ "kQTMSKnobEnv1DecayTimeID",
      /* 02000029 */ "kQTMSKnobEnv1SustainLevelID",
      /* 0200002A */ "kQTMSKnobEnv1SustainTimeID",
      /* 0200002B */ "kQTMSKnobEnv1SustainInfiniteID",
      /* 0200002C */ "kQTMSKnobEnv1ReleaseTimeID",
      /* 0200002D */ "kQTMSKnobEnv1ExpOptionsID",
      /* 0200002E */ "kQTMSKnobEnv2AttackTimeID",
      /* 0200002F */ "kQTMSKnobEnv2DecayTimeID",
      /* 02000030 */ "kQTMSKnobEnv2SustainLevelID",
      /* 02000031 */ "kQTMSKnobEnv2SustainTimeID",
      /* 02000032 */ "kQTMSKnobEnv2SustainInfiniteID",
      /* 02000033 */ "kQTMSKnobEnv2ReleaseTimeID",
      /* 02000034 */ "kQTMSKnobEnv2ExpOptionsID",
      /* 02000035 */ "kQTMSKnobPitchEnvelopeID",
      /* 02000036 */ "kQTMSKnobPitchEnvelopeDepthID",
      /* 02000037 */ "kQTMSKnobFilterKeyFollowID",
      /* 02000038 */ "kQTMSKnobFilterTransposeID",
      /* 02000039 */ "kQTMSKnobFilterQID",
      /* 0200003A */ "kQTMSKnobFilterFrequencyEnvelopeID",
      /* 0200003B */ "kQTMSKnobFilterFrequencyEnvelopeDepthID",
      /* 0200003C */ "kQTMSKnobFilterQEnvelopeID",
      /* 0200003D */ "kQTMSKnobFilterQEnvelopeDepthID",
      /* 0200003E */ "kQTMSKnobReverbThresholdID",
      /* 0200003F */ "kQTMSKnobVolumeAttackVelScalingID",
      /* 02000040 */ // "kQTMSKnobLastIDPlus1",
  };
  uint32_t effective_id = knob_id - 0x02000000;
  return (effective_id < names.size()) ? names[effective_id] : nullptr;
}

const char* SSAIInstrument::name_for_controller(uint32_t controller_id) {
  static const std::unordered_map<uint32_t, const char*> names{
      {kControllerModulationWheel, "kControllerModulationWheel"},
      {kControllerBreath, "kControllerBreath"},
      {kControllerFoot, "kControllerFoot"},
      {kControllerPortamentoTime, "kControllerPortamentoTime"},
      {kControllerVolume, "kControllerVolume"},
      {kControllerBalance, "kControllerBalance"},
      {kControllerPan, "kControllerPan"},
      {kControllerExpression, "kControllerExpression"},
      {kControllerLever1, "kControllerLever1"},
      {kControllerLever2, "kControllerLever2"},
      {kControllerLever3, "kControllerLever3"},
      {kControllerLever4, "kControllerLever4"},
      {kControllerLever5, "kControllerLever5"},
      {kControllerLever6, "kControllerLever6"},
      {kControllerLever7, "kControllerLever7"},
      {kControllerLever8, "kControllerLever8"},
      {kControllerPitchBend, "kControllerPitchBend"},
      {kControllerAfterTouch, "kControllerAfterTouch"},
      {kControllerPartTranspose, "kControllerPartTranspose"},
      {kControllerTuneTranspose, "kControllerTuneTranspose"},
      {kControllerPartVolume, "kControllerPartVolume"},
      {kControllerTuneVolume, "kControllerTuneVolume"},
      {kControllerSustain, "kControllerSustain"},
      {kControllerPortamento, "kControllerPortamento"},
      {kControllerSostenuto, "kControllerSostenuto"},
      {kControllerSoftPedal, "kControllerSoftPedal"},
      {kControllerReverb, "kControllerReverb"},
      {kControllerTremolo, "kControllerTremolo"},
      {kControllerChorus, "kControllerChorus"},
      {kControllerCeleste, "kControllerCeleste"},
      {kControllerPhaser, "kControllerPhaser"},
      {kControllerEditPart, "kControllerEditPart"},
      {kControllerMasterTune, "kControllerMasterTune"},
      {kControllerMasterTranspose, "kControllerMasterTranspose"},
      {kControllerMasterVolume, "kControllerMasterVolume"},
      {kControllerMasterCPULoad, "kControllerMasterCPULoad"},
      {kControllerMasterPolyphony, "kControllerMasterPolyphony"},
      {kControllerMasterFeatures, "kControllerMasterFeatures"},
  };
  auto it = names.find(controller_id);
  return (it == names.end()) ? nullptr : it->second;
}

struct TuneInstrumentDefinition {
  /* 00 */ uint8_t unknown_a1[0x0C];
  /* 0C */ uint8_t collection_name[0x20]; // Pascal string
  /* 2C */ uint8_t instrument_name[0x20]; // Pascal string
  /* 4C */ phosg::be_uint32_t instrument_number;
  /* 50 */ phosg::be_uint32_t unknown_a3;
  /* 54 */ phosg::be_uint16_t flags_and_type;
  /* 56 */ phosg::be_uint16_t message_size; // In 4-byte words
  /* 58 */
} __attribute__((packed));

struct TuneExtendedInstrumentDefinition {
  // NOTE: These can probably be arbitrarily complex; this just mirrors the format used in Harry the Handsome Executive
  /* 00 */ uint8_t unknown_a1[0x0C];
  /* 0C */ BlockHeader sean_block_header; // SEAN_TYPE, size 0x74, block index 1, child count 1
  /* 20 */ BlockHeader tone_block_header; // TONE_TYPE, size 0x60, block index 1, child count 0
  /* 34 */ phosg::be_uint32_t unknown_a2; // 'ss  ' (0x73730202)
  /* 38 */ uint8_t collection_name[0x20]; // Pascal string
  /* 58 */ uint8_t instrument_name[0x20]; // Pascal string
  /* 78 */ phosg::be_uint32_t instrument_number;
  /* 7C */ phosg::be_uint32_t unknown_a3;
  /* 80 */ phosg::be_uint16_t flags_and_type;
  /* 82 */ phosg::be_uint16_t message_size; // In 4-byte words
  /* 84 */
} __attribute__((packed));

std::string TuneResource::Event::disassembly_prefix() const {
  return std::format("{:08X}  {:<32}  @{:08X}",
      this->source_offset,
      phosg::format_data_string(this->source_data, nullptr, phosg::FormatDataStringFlags::HEX_ONLY),
      this->when);
}

void TuneResource::NoteEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
  auto& ev = events.emplace_back(MIDIEvent{this->when, {}});
  ev.data.emplace_back(0x90 | this->channel);
  ev.data.emplace_back(this->key);
  ev.data.emplace_back(this->vel);
}
std::string TuneResource::NoteEvent::disassemble() const {
  return std::format("{}  note           channel {}, key {}, velocity {}, duration {}",
      this->disassembly_prefix(), this->channel, this->key, this->vel, this->duration);
}

void TuneResource::NoteOffEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
  auto& ev = events.emplace_back(MIDIEvent{this->when, {}});
  ev.data.emplace_back(0x80 | this->channel);
  ev.data.emplace_back(this->key);
  ev.data.emplace_back(this->vel);
}
std::string TuneResource::NoteOffEvent::disassemble() const {
  return std::format("{}  note_off       channel {}, key {}, velocity {}",
      this->disassembly_prefix(), this->channel, this->key, this->vel);
}

void TuneResource::PitchBendEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
  auto& ev = events.emplace_back(MIDIEvent{this->when, {}});
  ev.data.emplace_back(0xE0 | this->channel);
  // Standard MIDI pitch bend range is +/- 2 semitones in either direction; clamp the result to that range
  int64_t value = std::clamp<int64_t>(this->semitones * 0x2000, -0x4000, 0x3FFF);
  ev.data.emplace_back(value & 0x7F);
  ev.data.emplace_back((value >> 7) & 0x7F);
}
std::string TuneResource::PitchBendEvent::disassemble() const {
  return std::format("{}  pitch_bend     channel {}, semitones {:g}",
      this->disassembly_prefix(), this->channel, this->semitones);
}

void TuneResource::ControllerEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
  auto& ev = events.emplace_back(MIDIEvent{this->when, {}});
  ev.data.emplace_back(0xB0 | this->channel);
  ev.data.emplace_back(this->message);
  ev.data.emplace_back(this->value);
}
std::string TuneResource::ControllerEvent::disassemble() const {
  auto name = SSAIInstrument::name_for_controller(this->message);
  if (name) {
    return std::format("{}  controller     channel {}, message {} ({}), value {}",
        this->disassembly_prefix(), this->channel, this->message, name, this->value);
  } else {
    return std::format("{}  controller     channel {}, message {}, value {}",
        this->disassembly_prefix(), this->channel, this->message, this->value);
  }
}

void TuneResource::ChannelSetupEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
  auto& ev1 = events.emplace_back(MIDIEvent{this->when, {}});
  ev1.data.emplace_back(0xC0 | this->channel);
  ev1.data.emplace_back(this->instrument_number);
  auto& ev2 = events.emplace_back(MIDIEvent{this->when, {}});
  ev2.data.emplace_back(0xB0 | this->channel);
  ev2.data.emplace_back(7); // Volume
  ev2.data.emplace_back(0x7F); // Default max volume
  auto& ev3 = events.emplace_back(MIDIEvent{this->when, {}});
  ev3.data.emplace_back(0xB0 | this->channel);
  ev3.data.emplace_back(10); // Panning
  ev3.data.emplace_back(0x40); // Center of unsigned 7-bit range
  auto& ev4 = events.emplace_back(MIDIEvent{this->when, {}});
  ev4.data.emplace_back(0xE0 | this->channel);
  ev4.data.emplace_back(0x00); // 0x2000 (center of unsigned 14-bit range)
  ev4.data.emplace_back(0x40);
}
std::string TuneResource::ChannelSetupEvent::disassemble() const {
  return std::format(
      "{}  channel_setup  channel {}, instrument number {}, collection name \"{}\", instrument name \"{}\"",
      this->disassembly_prefix(), this->channel, this->instrument_number, this->collection_name,
      this->instrument_name);
}

TuneResource::TuneResource(const void* data, size_t size) {
  phosg::StringReader r(data, size);

  const auto& header = r.get<BlockHeader>();
  if (header.type != MUSI_TYPE) {
    throw std::runtime_error("Tune identifier is incorrect");
  }

  std::unordered_map<uint16_t, uint8_t> partition_id_to_channel;
  uint64_t current_time = 0;

  auto add_event = [&](std::unique_ptr<Event> event, size_t start_offset) -> void {
    if (!event->when) {
      event->when = current_time;
    }
    event->source_offset = start_offset;
    event->source_data = r.pread(start_offset, r.where() - start_offset);
    this->events.emplace_back(std::move(event));
  };

  // The remainder of the data is playback commands
  while (!r.eof()) {
    size_t start_offset = r.where();
    uint32_t event = r.get_u32b();
    uint8_t type = (event >> 28) & 0x0F;

    switch (type) {
      case 0x00:
      case 0x01: // Pause
        current_time += (event & 0x00FFFFFF);
        break;

      case 0x02: // Simple note event
      case 0x03: // Simple note event
      case 0x09: { // Extended note event
        auto ev = std::make_unique<NoteEvent>();
        uint16_t partition_id;
        if (type == 0x09) {
          // Bits: TTTTPPPPPPPPPPPP---------KKKKKKK ---VVVVVVVDDDDDDDDDDDDDDDDDDDDDD
          uint32_t options = r.get_u32b();
          partition_id = (event >> 16) & 0xFFF;
          ev->key = event & 0x7F;
          ev->vel = (options >> 22) & 0x7F;
          ev->duration = options & 0x3FFFFF;
        } else {
          // Bits: TTTTPPPPKKKKKKVVVVVVVDDDDDDDDDDD
          partition_id = (event >> 24) & 0x1F;
          ev->key = ((event >> 18) & 0x3F) + 0x20;
          ev->vel = (event >> 11) & 0x7F;
          ev->duration = event & 0x7FF;
        }
        try {
          ev->channel = partition_id_to_channel.at(partition_id);
        } catch (const std::out_of_range&) {
          throw std::runtime_error("notes produced on uninitialized partition");
        }

        auto off_ev = std::make_unique<NoteOffEvent>();
        off_ev->when = current_time + ev->duration;
        off_ev->source_offset = ev->source_offset;
        // We intentionally do not set off_ev->source_data
        off_ev->channel = ev->channel;
        off_ev->key = ev->key;
        off_ev->vel = ev->vel;

        add_event(std::move(ev), start_offset);
        add_event(std::move(off_ev), start_offset);
        break;
      }

      case 0x04: // Simple controller event
      case 0x05: // Simple controller event
      case 0x0A: { // Extended controller event
        uint16_t message, partition_id, value;
        if (type == 0x0A) {
          uint32_t options = r.get_u32b();
          message = (options >> 16) & 0x3FFF;
          partition_id = (event >> 16) & 0xFFF;
          value = options & 0xFFFF;
        } else {
          message = (event >> 16) & 0xFF;
          partition_id = (event >> 24) & 0x0F;
          value = event & 0xFFFF;
        }

        // Controller messages can create channels
        uint8_t channel = partition_id_to_channel.emplace(partition_id, partition_id_to_channel.size()).first->second;
        if (channel >= 0x10) {
          throw std::runtime_error("Too many MIDI channels");
        }

        if (message == 0) {
          // Bank select (ignore for now)
          break;

        } else if (message == 7) { // Volume
          auto ev = std::make_unique<ControllerEvent>();
          ev->channel = channel;
          ev->message = message;
          ev->value = (value >> 8) & 0x7F;
          add_event(std::move(ev), start_offset);

        } else if (message == 10) { // Panning
          auto ev = std::make_unique<ControllerEvent>();
          ev->channel = channel;
          ev->message = message;
          ev->value = (value >> 1) & 0x7F;
          add_event(std::move(ev), start_offset);

        } else if (message == 32) { // Pitch bend
          auto ev = std::make_unique<PitchBendEvent>();
          ev->channel = channel;
          ev->semitones = static_cast<float>(static_cast<int16_t>(value)) / 0x100; // 8.8 fixed-point apparently
          add_event(std::move(ev), start_offset);

        } else { // Some other controller message
          auto ev = std::make_unique<ControllerEvent>();
          ev->channel = channel;
          ev->message = message;
          ev->value = value >> 8;
          add_event(std::move(ev), start_offset);
        }

        break;
      }

      case 0x0F: { // Metadata message
        uint16_t partition_id = (event >> 16) & 0xFFF;
        uint32_t message_size = (event & 0xFFFF) * 4;
        if (message_size < 8) {
          throw std::runtime_error("metadata message too short for type field");
        }

        auto msg_r = r.subx(r.where(), message_size - 4);
        r.skip(message_size - 4);

        // The second-to-last word contains the message type
        uint16_t message_type = msg_r.pget_u16b(msg_r.size() - 4) & 0x3FFF;

        // Meta messages can create channels
        uint8_t channel = partition_id_to_channel.emplace(partition_id, partition_id_to_channel.size()).first->second;
        if (channel >= 0x10) {
          throw std::runtime_error("not enough MIDI channels");
        }

        switch (message_type) {
          case 1: { // Instrument definition
            if (msg_r.remaining() != sizeof(TuneInstrumentDefinition)) {
              throw std::runtime_error(std::format(
                  "Instrument definition size is incorrect (expected 0x{:X}, received 0x{:X})",
                  sizeof(TuneInstrumentDefinition), msg_r.remaining()));
            }
            const auto& inst = msg_r.get<TuneInstrumentDefinition>();
            auto ev = std::make_unique<ChannelSetupEvent>();
            ev->channel = channel;
            ev->instrument_number = inst.instrument_number;
            ev->collection_name = decode_pstring<0x20>(inst.collection_name);
            ev->instrument_name = decode_pstring<0x20>(inst.instrument_name);
            add_event(std::move(ev), start_offset);
            break;
          }

          case 6: { // Extended (?) instrument definition
            if (msg_r.remaining() != sizeof(TuneExtendedInstrumentDefinition)) {
              throw std::runtime_error("Extended instrument definition size is incorrect");
            }
            const auto& inst = msg_r.get<TuneExtendedInstrumentDefinition>();
            if ((inst.sean_block_header.type != SEAN_TYPE) || (inst.tone_block_header.type != TONE_TYPE) ||
                (inst.unknown_a2 != SS_TYPE)) {
              throw std::runtime_error("Extended instrument definition format is unrecognized");
            }
            auto ev = std::make_unique<ChannelSetupEvent>();
            ev->channel = channel;
            ev->instrument_number = inst.instrument_number;
            ev->collection_name = decode_pstring<0x20>(inst.collection_name);
            ev->instrument_name = decode_pstring<0x20>(inst.instrument_name);
            add_event(std::move(ev), start_offset);
            break;
          }

          case 5: // Tune difference
          case 8: // MIDI channel (probably we should use this)
          case 10: // No operation
          case 11: // Notes used
            break;

          default:
            throw std::runtime_error(std::format("Unknown metadata event {:08X}/{:X} in Tune header",
                event, message_type));
        }

        break;
      }

      case 0x08: // Reserved (ignored; has 4-byte argument)
      case 0x0C: // Reserved (ignored; has 4-byte argument)
      case 0x0D: // Reserved (ignored; has 4-byte argument)
      case 0x0E: // Reserved (ignored; has 4-byte argument)
        r.go(r.where() + 4);
      case 0x06: // Marker (ignored)
      case 0x07: // Marker (ignored)
        break;

      default:
        throw std::runtime_error(std::format("Unsupported event {:08X} in Tune stream", event));
    }
  }
}

std::string TuneResource::midi() const {
  struct MIDIChunkHeader {
    phosg::be_uint32_t magic; // MThd or MTrk
    phosg::be_uint32_t size;
  } __attribute__((packed));
  struct MIDIHeader {
    MIDIChunkHeader header;
    phosg::be_uint16_t format;
    phosg::be_uint16_t track_count;
    phosg::be_uint16_t division;
  } __attribute__((packed));

  std::vector<MIDIEvent> midi_events;
  for (const auto& event : this->events) {
    event->add_midi_events(midi_events);
  }

  // Sort the events by time, since there can be out-of-order note off events
  std::stable_sort(midi_events.begin(), midi_events.end(), [](const MIDIEvent& a, const MIDIEvent& b) {
    return a.when < b.when;
  });

  std::string midi_track_data;
  auto encode_delay = [](uint64_t delta) -> std::string {
    std::string delta_str;
    while (delta > 0x7F) {
      delta_str.push_back(delta & 0x7F);
      delta >>= 7;
    }
    delta_str.push_back(delta);
    for (size_t x = 1; x < delta_str.size(); x++) {
      delta_str[x] |= 0x80;
    }
    reverse(delta_str.begin(), delta_str.end());
    return delta_str;
  };

  // Generate the MIDI track
  uint64_t current_time = 0;
  for (const auto& event : midi_events) {
    uint64_t delta = event.when - current_time;
    current_time = event.when;

    midi_track_data += encode_delay(delta);
    for (uint8_t v : event.data) {
      midi_track_data.push_back(v);
    }
  }
  // Add the track end event
  midi_track_data += encode_delay(0);
  midi_track_data.push_back(0xFF);
  midi_track_data.push_back(0x2F);
  midi_track_data.push_back(0x00);

  // Generate the MIDI headers
  MIDIHeader midi_header;
  midi_header.header.magic = 0x4D546864; // 'MThd'
  midi_header.header.size = 6;
  midi_header.format = 0;
  midi_header.track_count = 1;
  midi_header.division = 300; // Ticks per quarter note

  MIDIChunkHeader track_header;
  track_header.magic = 0x4D54726B; // 'MTrk'
  track_header.size = midi_track_data.size();

  // Generate the file and return it
  phosg::StringWriter w;
  w.put<MIDIHeader>(midi_header);
  w.put<MIDIChunkHeader>(track_header);
  w.write(midi_track_data);
  return std::move(w.str());
}

std::string TuneResource::disassemble() const {
  std::vector<std::pair<uint64_t, std::string>> lines;
  for (const auto& ev : events) {
    lines.emplace_back(make_pair(ev->when, ev->disassemble()));
  }
  std::stable_sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) -> bool {
    return a.first < b.first;
  });

  std::string ret;
  for (const auto& [_, line] : lines) {
    if (!ret.empty()) {
      ret += "\n";
    }
    ret += line;
  }
  return ret;
}

} // namespace Audio
} // namespace ResourceDASM
