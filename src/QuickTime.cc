#include "QuickTime.hh"

#include <algorithm>
#include <phosg/Strings.hh>

#include "QuickTimeParser.hh"
#include "TextCodecs.hh"

namespace ResourceDASM {
namespace QuickTime {

// QTMA atom types
static constexpr uint32_t SSAI_TYPE = resource_type("ssai");
static constexpr uint32_t SEAN_TYPE = resource_type("sean");
static constexpr uint32_t TONE_TYPE = resource_type("tone"); // kaiToneDescType
static constexpr uint32_t KNBL_TYPE = resource_type("knbl"); // kaiKnobListType
static constexpr uint32_t SINF_TYPE = resource_type("sinf"); // kaiKeyRangeInfoType
static constexpr uint32_t SDSC_TYPE = resource_type("sdsc"); // kaiSampleDescType
static constexpr uint32_t SMIN_TYPE = resource_type("smin"); // kaiSampleInfoType
static constexpr uint32_t SNAM_TYPE = resource_type("snam"); //
static constexpr uint32_t SDAT_TYPE = resource_type("sdat"); // kaiSampleDataType
static constexpr uint32_t QUAL_TYPE = resource_type("qual"); // kaiInstGMQualityType
static constexpr uint32_t QUID_TYPE = resource_type("quid"); // kaiSampleDataQUIDType
static constexpr uint32_t IINF_TYPE = resource_type("iinf"); // kaiInstInfoType
static constexpr uint32_t IREF_TYPE = resource_type("iref"); // kaiInstrumentRefType
static constexpr uint32_t COPYRIGHT_WRT_TYPE = 0xA9777274; // '©wrt' (in MacRoman); kaiWriterType
static constexpr uint32_t COPYRIGHT_CPY_TYPE = 0xA9637079; // '©cpy' (in MacRoman); kaiCopyrightType
static constexpr uint32_t STR_TYPE = resource_type("str "); // kaiOtherStrType
static constexpr uint32_t MUSI_TYPE = resource_type("musi");
static constexpr uint32_t SS_TYPE = resource_type("ss  ");

// QT movie types
constexpr uint32_t MOVIE_ATOM_TYPE = resource_type("moov");
constexpr uint32_t MOVIE_HEADER_ATOM_TYPE = resource_type("mvhd");
constexpr uint32_t TRACK_ATOM_TYPE = resource_type("trak");
constexpr uint32_t TRACK_HEADER_ATOM_TYPE = resource_type("tkhd");
constexpr uint32_t EDITS_ATOM_TYPE = resource_type("edts");
constexpr uint32_t EDIT_LIST_ATOM_TYPE = resource_type("elst");
constexpr uint32_t HANDLER_ATOM_TYPE = resource_type("hdlr");
constexpr uint32_t MEDIA_ATOM_TYPE = resource_type("mdia");
constexpr uint32_t MEDIA_HEADER_ATOM_TYPE = resource_type("mdhd");
constexpr uint32_t MEDIA_INFO_ATOM_TYPE = resource_type("minf");
constexpr uint32_t BASE_MEDIA_INFO_HEADER_ATOM_TYPE = resource_type("gmhd");
constexpr uint32_t BASE_MEDIA_INFO_ATOM_TYPE = resource_type("gmin");
constexpr uint32_t DATA_INFO_ATOM_TYPE = resource_type("dinf");
constexpr uint32_t DATA_REFERENCE_ATOM_TYPE = resource_type("dref");
constexpr uint32_t DATA_REFERENCE_ALIAS_ATOM_TYPE = resource_type("alis");
constexpr uint32_t DATA_REFERENCE_HANDLE_ATOM_TYPE = resource_type("hndl");
constexpr uint32_t DATA_REFERENCE_HANDLE_DATA_ATOM_TYPE = resource_type("data");
// constexpr uint32_t DATA_REFERENCE_RESOURCE_ATOM_TYPE = resource_type("rsrc");
// constexpr uint32_t DATA_REFERENCE_URL_ATOM_TYPE = resource_type("url ");
constexpr uint32_t SAMPLE_TABLE_ATOM_TYPE = resource_type("stbl");
constexpr uint32_t SAMPLE_DESCRIPTION_ATOM_TYPE = resource_type("stsd");
constexpr uint32_t TIME_TO_SAMPLE_ATOM_TYPE = resource_type("stts");
constexpr uint32_t SAMPLE_TO_CHUNK_ATOM_TYPE = resource_type("stsc");
constexpr uint32_t SAMPLE_SIZES_ATOM_TYPE = resource_type("stsz");
constexpr uint32_t CHUNK_OFFSETS_ATOM_TYPE = resource_type("stco");
constexpr uint32_t USER_DATA_ATOM_TYPE = resource_type("udta");
constexpr uint32_t CLIP_ATOM_TYPE = resource_type("clip");
// constexpr uint32_t CLIP_REGION_ATOM_TYPE = resource_type("crgn");

// Handler types
constexpr uint32_t MUSI_COMPONENT_TYPE = resource_type("mhlr");
constexpr uint32_t MUSI_COMPONENT_SUBTYPE = resource_type("musi");

struct SSAIAtom {
  /* 08 */ phosg::be_uint32_t atom_number = 0;
  /* 0C */
} __attribute__((packed));

struct AtomBase { // All atoms below begin with this structure (only 'ssai' does not include it)
  /* 08 */ phosg::be_uint32_t atom_number = 0;
  /* 0C */ phosg::be_uint32_t child_count = 0;
  /* 10 */ phosg::be_uint32_t unknown_a1 = 0;
  /* 14 */
} __attribute__((packed));

struct KNBLAtom { // Knob list
  struct Entry {
    phosg::be_uint32_t number = 0;
    phosg::be_int32_t value = 0;
  } __attribute__((packed));

  /* 14 */ phosg::be_uint32_t entry_count = 0;
  /* 18 */ phosg::be_uint32_t flags = 0;
  /* 1C */ // Entries follow here
} __attribute__((packed));

struct SDSCAtom { // Sample description
  /* 14 */ phosg::be_uint32_t format = 0; // E.g. 'raw '
  /* 18 */ phosg::be_uint16_t num_channels = 0;
  /* 1A */ phosg::be_uint16_t bits_per_sample = 0;
  /* 1C */ phosg::be_uint16_t sample_rate_integer = 0; // Whole number part of a Fixed
  /* 1E */ phosg::be_uint16_t sample_rate_fractional = 0; // Fractional part of a Fixed
  /* 20 */ phosg::be_uint16_t sdat_atom_number = 0;
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

struct QualAtom {
  /* 14 */ uint8_t unknown_a3[4] = {};
  /* 18 */
} __attribute__((packed));

struct QuidAtom {
  /* 14 */ uint8_t unknown_a3[0x10] = {};
  /* 24 */
} __attribute__((packed));

struct ToneDescription {
  /* 00 */ phosg::be_uint32_t collection_type; // 'ss  ' (0x73730202)
  /* 04 */ uint8_t collection_name[0x20]; // Pascal string
  /* 24 */ uint8_t instrument_name[0x20]; // Pascal string
  /* 44 */ phosg::be_uint32_t instrument_number;
  /* 48 */ phosg::be_uint32_t midi_instrument_number;
  /* 4C */
} __attribute__((packed));

template <size_t BufSize>
std::string decode_pstring(const uint8_t* data) {
  if (*data > (BufSize - 1)) {
    throw std::runtime_error("Pascal string overflows buffer");
  }
  return std::string(reinterpret_cast<const char*>(data + 1), *data);
}

class QuickTimeSSAIParser : public QuickTime::Parser {
public:
  QuickTimeSSAIParser(SSAIInstrument* ssai) : ssai(ssai) {}

protected:
  SSAIInstrument* ssai;
  SSAIInstrument::KeyRegion* current_key_region = nullptr;
  SSAIInstrument::SampleData* current_sample_data = nullptr;

  virtual void handle_atom(uint32_t type, phosg::StringReader& r) {
    if (type == SSAI_TYPE) {
      r.skip(sizeof(SSAIAtom)); // We don't care about the ssai block number
      this->parse_atom_list(r.extract());
      return;
    }

    const auto& base = r.get<AtomBase>();
    switch (type) {
      case SEAN_TYPE:
      case IINF_TYPE:
      case IREF_TYPE:
        this->parse_atom_list(r.extract(), base.child_count);
        break;
      case SMIN_TYPE: {
        auto emplace_ret = this->ssai->sample_datas.emplace(base.atom_number, SSAIInstrument::SampleData{});
        if (!emplace_ret.second) {
          throw std::runtime_error(std::format("Duplicate global sample number {}", base.atom_number));
        }
        auto& sample_data = emplace_ret.first->second;
        sample_data.smin_atom_number = base.atom_number;
        sample_data.sdat_atom_number = 0;
        if (this->current_sample_data) {
          this->throw_parse_error("Received smin atom inside another smin atom");
        }
        this->current_sample_data = &sample_data;
        this->parse_atom_list(r.extract(), base.child_count);
        this->current_sample_data = nullptr;
        break;
      }
      case SINF_TYPE:
        if (this->current_key_region) {
          this->throw_parse_error("Received sinf atom inside another sinf atom");
        }
        this->current_key_region = &this->ssai->key_regions[base.atom_number];
        this->parse_atom_list(r.extract(), base.child_count);
        this->current_key_region = nullptr;
        break;

      case TONE_TYPE: {
        const auto& tone_atom = this->get_fixed_atom<ToneDescription>(r);
        // Only update the name and resource ID if this atom isn't a reference to another instrument. `tone` may appear
        // in the hierarchy ssai->sean->tone in which case it's the instrument metadata; it may also appear within an
        // sinf atom in which case it's a reference to another instrument's samples
        if (!this->current_key_region && !this->current_sample_data) {
          // TODO: There might be other important stuff in ToneAtom too
          this->ssai->name = decode_pstring<0x20>(tone_atom.instrument_name);
          if (this->ssai->midi_instrument_number == 0) {
            this->ssai->midi_instrument_number = tone_atom.midi_instrument_number;
          }
        }
        break;
      }

      case KNBL_TYPE: {
        const auto& knbl_atom = r.get<KNBLAtom>();
        auto& knobs = this->current_key_region ? this->current_key_region->knobs : this->ssai->knobs;
        if (!knobs.empty()) {
          this->throw_parse_error("Received multiple knob lists in same context");
        }
        for (size_t z = 0; z < knbl_atom.entry_count; z++) {
          const auto& entry = r.get<KNBLAtom::Entry>();
          knobs.emplace(entry.number, entry.value);
        }
        r.skip(r.remaining()); // Sometimes knbl atoms end with extra data; just ignore it
        break;
      }

      case SDSC_TYPE: {
        if (!this->current_key_region) {
          this->throw_parse_error("Received sdsc atom outside of sinf atom");
        }
        const auto& sdsc = r.get<SDSCAtom>();
        this->current_key_region->num_channels = sdsc.num_channels;
        this->current_key_region->bits_per_sample = sdsc.bits_per_sample;
        this->current_key_region->sample_rate = sdsc.sample_rate_integer +
            (static_cast<float>(sdsc.sample_rate_fractional) / 0x10000);
        this->current_key_region->sample_data_number = sdsc.sdat_atom_number;
        this->current_key_region->frame_count = sdsc.frame_count;
        this->current_key_region->loop_start_offset = sdsc.loop_start_offset;
        this->current_key_region->loop_end_offset = sdsc.loop_end_offset;
        this->current_key_region->base_note = sdsc.base_note;
        this->current_key_region->key_low = sdsc.key_low;
        this->current_key_region->key_high = sdsc.key_high;
        break;
      }

      case SDAT_TYPE: {
        // Apparently sdat may appear within smin, or at the top level. If it appears within smin, it should be keyed
        // by the smin's atom number; if it appears at the top level it should be keyed by its own atom number
        if (this->current_sample_data) {
          this->current_sample_data->data = r.read(r.remaining());
        } else {
          auto& sample_data = this->ssai->sample_datas[base.atom_number];
          sample_data.data = r.read(r.remaining());
          sample_data.smin_atom_number = -1;
          sample_data.sdat_atom_number = base.atom_number;
        }
        break;
      }
      case QUAL_TYPE:
        this->get_fixed_atom<QualAtom>(r);
        break;
      case QUID_TYPE:
        this->get_fixed_atom<QuidAtom>(r);
        break;
      case SNAM_TYPE:
        if (!this->current_sample_data) {
          throw std::runtime_error("Received snam atom outside of smin atom");
        }
        this->current_sample_data->name = r.read(r.remaining());
        break;
      case COPYRIGHT_WRT_TYPE:
        this->ssai->copyright_wrt = r.read(r.remaining());
        break;
      case COPYRIGHT_CPY_TYPE:
        this->ssai->copyright_cpy = r.read(r.remaining());
        break;
      case STR_TYPE:
        this->ssai->info_string = r.read(r.remaining());
        break;
      default:
        this->throw_parse_error("Unknown atom type");
    }
  }
};

SSAIInstrument::SSAIInstrument(const void* data, size_t size) {
  QuickTimeSSAIParser parser(this);
  parser.parse(data, size);
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
  // Flag bits (from MPW headers):
  //   01 = kNoteRequestNoGM: don't degrade to a GM synth
  //   02 = kNoteRequestNoSynthType: don't degrade to another synth of same type but different name
  //   04 = kNoteRequestSynthMustMatch: synthType must be a match, including kGMSynthComponentSubType
  //   80 = kNoteRequestSpecifyMIDIChannel (not described in MPW headers)
  /* 00 */ uint8_t flags;
  /* 01 */ uint8_t midi_channel_number;
  /* 02 */ phosg::be_uint16_t max_polyphony; // Maximum number of concurrent voices
  /* 04 */ Fixed typical_polyphony;
  /* 08 */ ToneDescription desc;
  /* 54 */ phosg::be_uint16_t flags_and_type;
  /* 56 */ phosg::be_uint16_t message_size; // In 4-byte words
  /* 58 */
} __attribute__((packed));

class TuneExtendedInstrumentDefinitionParser : public QuickTime::Parser {
public:
  TuneExtendedInstrumentDefinitionParser(ToneDescription* tone) : tone(tone) {}

  bool received_tone_atom = false;

protected:
  ToneDescription* tone;

  virtual void handle_atom(uint32_t type, phosg::StringReader& r) {
    const auto& base = r.get<AtomBase>();
    switch (type) {
      case SEAN_TYPE:
        this->parse_atom_list(r.extract(), base.child_count);
        break;
      case TONE_TYPE:
        if (this->received_tone_atom) {
          this->throw_parse_error("Received multiple tone atoms");
        }
        this->received_tone_atom = true;
        *this->tone = this->get_fixed_atom<ToneDescription>(r);
        break;
      default:
        this->throw_parse_error("Unknown atom type");
    }
  }
};

std::string QTMASequence::Event::disassembly_prefix() const {
  return std::format("{:08X}  {:<32}  @{:08X}",
      this->source_offset,
      phosg::format_data_string(this->source_data, nullptr, phosg::FormatDataStringFlags::HEX_ONLY),
      this->when);
}

void QTMASequence::NoteEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
  auto& ev = events.emplace_back(MIDIEvent{this->when, {}});
  ev.data.emplace_back(0x90 | this->channel);
  ev.data.emplace_back(this->key);
  ev.data.emplace_back(this->vel);
}
std::string QTMASequence::NoteEvent::disassemble() const {
  return std::format("{}  note           channel {}, key {}, velocity {}, duration {}",
      this->disassembly_prefix(), this->channel, this->key, this->vel, this->duration);
}

void QTMASequence::NoteOffEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
  auto& ev = events.emplace_back(MIDIEvent{this->when, {}});
  ev.data.emplace_back(0x80 | this->channel);
  ev.data.emplace_back(this->key);
  ev.data.emplace_back(this->vel);
}
std::string QTMASequence::NoteOffEvent::disassemble() const {
  return std::format("{}  note_off       channel {}, key {}, velocity {}",
      this->disassembly_prefix(), this->channel, this->key, this->vel);
}

void QTMASequence::ControllerEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
  auto& ev = events.emplace_back(MIDIEvent{this->when, {}});
  if (this->message == 0x20) { // Pitch bend
    ev.data.emplace_back(0xE0 | this->channel);
    // Standard MIDI pitch bend range is +/- 2 semitones in either direction; clamp the result to that range
    int64_t value = std::clamp<int64_t>(static_cast<int64_t>(this->value) * 0x20, -0x4000, 0x3FFF);
    ev.data.emplace_back(value & 0x7F);
    ev.data.emplace_back((value >> 7) & 0x7F);
  } else {
    ev.data.emplace_back(0xB0 | this->channel);
    ev.data.emplace_back(this->message);
    ev.data.emplace_back(this->value >> 8);
  }
}
std::string QTMASequence::ControllerEvent::disassemble() const {
  auto name = SSAIInstrument::name_for_controller(this->message);
  if (name) {
    return std::format("{}  controller     channel {}, message {} ({}), value {}",
        this->disassembly_prefix(), this->channel, this->message, name, this->value);
  } else {
    return std::format("{}  controller     channel {}, message {}, value {}",
        this->disassembly_prefix(), this->channel, this->message, this->value);
  }
}

void QTMASequence::ChannelSetupEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
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
std::string QTMASequence::ChannelSetupEvent::disassemble() const {
  return std::format(
      "{}  channel_setup  channel {}, instrument number {} (MIDI {}), collection name \"{}\", instrument name \"{}\"",
      this->disassembly_prefix(), this->channel, this->instrument_number, this->midi_instrument_number,
      this->collection_name, this->instrument_name);
}

QTMASequence::QTMASequence(const void* data, size_t size, bool expect_header) {
  phosg::StringReader r(data, size);

  if (expect_header) {
    const auto& header = r.get<QuickTime::AtomHeader>();
    if (header.type != MUSI_TYPE) {
      throw std::runtime_error("Tune identifier is incorrect");
    }
    r.skip(sizeof(AtomBase));
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
        auto ev = std::make_unique<ControllerEvent>();
        ev->channel = partition_id_to_channel.emplace(partition_id, partition_id_to_channel.size()).first->second;
        ev->message = message;
        ev->value = value;
        add_event(std::move(ev), start_offset);
        break;
      }

      case 0x0F: { // Metadata message
        uint16_t partition_id = (event >> 16) & 0xFFF;
        uint32_t message_size = (event & 0xFFFF) * 4;
        if (message_size < 8) {
          throw std::runtime_error("metadata message too short for type field");
        }

        auto msg_r = r.extract(message_size - 4);

        // The second-to-last word contains the message type
        uint16_t message_type = msg_r.pget_u16b(msg_r.size() - 4) & 0x3FFF;

        // Meta messages can create channels
        uint8_t channel = partition_id_to_channel.emplace(partition_id, partition_id_to_channel.size()).first->second;

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
            ev->instrument_number = inst.desc.instrument_number;
            ev->midi_instrument_number = inst.desc.midi_instrument_number;
            ev->collection_name = decode_pstring<0x20>(inst.desc.collection_name);
            ev->instrument_name = decode_pstring<0x20>(inst.desc.instrument_name);
            add_event(std::move(ev), start_offset);
            break;
          }

          case 6: { // Extended (?) instrument definition
            ToneDescription inst;
            TuneExtendedInstrumentDefinitionParser parser(&inst);
            parser.parse(msg_r.pgetv(msg_r.where() + 0x0C, msg_r.size() - 0x10), msg_r.size() - 0x10);
            if (!parser.received_tone_atom) {
              throw std::runtime_error("Extended instrument definition did not include a tone atom");
            }
            if (inst.collection_type != SS_TYPE) {
              throw std::runtime_error("Extended instrument definition format is unrecognized");
            }
            auto ev = std::make_unique<ChannelSetupEvent>();
            ev->channel = channel;
            ev->instrument_number = inst.instrument_number;
            ev->midi_instrument_number = inst.midi_instrument_number;
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

std::string QTMASequence::midi() const {
  for (const auto& event : this->events) {
    if (event->channel >= 0x10) {
      throw std::runtime_error("not enough MIDI channels");
    }
  }

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

std::string QTMASequence::disassemble() const {
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

struct MatrixField {
  // a, b, c, d, tx, ty are 16.16 fixed-point; u, v, w are 2.30 fixed-point apparently
  FixedBase<phosg::be_int32_t, 16> a;
  FixedBase<phosg::be_int32_t, 16> b;
  FixedBase<phosg::be_int32_t, 30> u;
  FixedBase<phosg::be_int32_t, 16> c;
  FixedBase<phosg::be_int32_t, 16> d;
  FixedBase<phosg::be_int32_t, 30> v;
  FixedBase<phosg::be_int32_t, 16> tx;
  FixedBase<phosg::be_int32_t, 16> ty;
  FixedBase<phosg::be_int32_t, 30> w;

  operator Matrix() const {
    Matrix ret;
    ret.a = this->a.as_float();
    ret.b = this->b.as_float();
    ret.u = this->u.as_float();
    ret.c = this->c.as_float();
    ret.d = this->d.as_float();
    ret.v = this->v.as_float();
    ret.tx = this->tx.as_float();
    ret.ty = this->ty.as_float();
    ret.w = this->w.as_float();
    return ret;
  }
};

struct MovieHeaderAtom { // mvhd
  /* 08 */ phosg::be_uint32_t version_and_flags; // High byte = version; low 3 bytes = flags
  /* 0C */ phosg::be_uint32_t creation_time; // Seconds since midnight 1 Jan 1904
  /* 10 */ phosg::be_uint32_t modification_time; // Seconds since midnight 1 Jan 1904
  /* 14 */ phosg::be_uint32_t time_scale; // Number of "time units" per second
  /* 18 */ phosg::be_uint32_t duration; // Measured in time units
  /* 1C */ Fixed preferred_rate; // 16.16 fixed-point frame rate multiplier (1.0 = normal rate)
  /* 1E */ FixedBase<phosg::be_int16_t, 8> preferred_volume; // 8.8 fixed-point volume multiplier (1.0 = full volume)
  /* 22 */ uint8_t reserved[10];
  /* 2C */ MatrixField matrix;
  /* 50 */ phosg::be_uint32_t preview_time;
  /* 54 */ phosg::be_uint32_t preview_duration;
  /* 58 */ phosg::be_uint32_t poster_time;
  /* 5C */ phosg::be_uint32_t selection_time;
  /* 60 */ phosg::be_uint32_t selection_duration;
  /* 64 */ phosg::be_uint32_t current_time;
  /* 68 */ phosg::be_uint32_t next_track_id;
  /* 6C */
} __attribute__((packed));

struct TrackHeaderAtom { // tkhd
  /* 08 */ phosg::be_uint32_t version_and_flags; // High byte = version; low 3 bytes = flags
  /* 0C */ phosg::be_uint32_t creation_time; // Seconds since midnight 1 Jan 1904
  /* 10 */ phosg::be_uint32_t modification_time; // Seconds since midnight 1 Jan 1904
  /* 14 */ phosg::be_uint32_t track_id;
  /* 18 */ phosg::be_uint32_t reserved1;
  /* 1C */ phosg::be_uint32_t duration;
  /* 20 */ phosg::be_uint32_t reserved2;
  /* 24 */ phosg::be_uint32_t reserved3;
  /* 28 */ phosg::be_uint16_t layer;
  /* 2A */ phosg::be_uint16_t alternate_group;
  /* 2C */ phosg::be_uint16_t volume;
  /* 2E */ phosg::be_uint16_t reserved4;
  /* 30 */ MatrixField matrix;
  /* 54 */ phosg::be_uint32_t width;
  /* 58 */ phosg::be_uint32_t height;
  /* 5C */
} __attribute__((packed));

struct CountedListAtom {
  /* 08 */ phosg::be_uint32_t version_and_flags; // High byte = version; low 3 bytes = flags
  /* 0C */ phosg::be_uint32_t entry_count;
  /* 10 */ // Entry entries[entry_count]; // Type depends on atom type
} __attribute__((packed));

struct EditListAtomEntry { // elst (CountedListAtom)
  phosg::be_uint32_t duration;
  phosg::be_uint32_t time;
  Fixed rate;
} __attribute__((packed));

struct MediaHeaderAtom { // mdhd
  /* 08 */ phosg::be_uint32_t version_and_flags; // High byte = version; low 3 bytes = flags
  /* 0C */ phosg::be_uint32_t creation_time; // Seconds since midnight 1 Jan 1904
  /* 10 */ phosg::be_uint32_t modification_time; // Seconds since midnight 1 Jan 1904
  /* 14 */ phosg::be_uint32_t time_scale; // Number of "time units" per second
  /* 18 */ phosg::be_uint32_t duration;
  /* 1C */ phosg::be_uint16_t language;
  /* 1E */ phosg::be_uint16_t quality;
  /* 20 */
} __attribute__((packed));

struct HandlerReferenceAtom { // hdlr
  /* 08 */ phosg::be_uint32_t version_and_flags; // High byte = version; low 3 bytes = flags
  /* 0C */ phosg::be_uint32_t component_type;
  /* 10 */ phosg::be_uint32_t component_subtype;
  /* 14 */ phosg::be_uint32_t component_manufacturer;
  /* 18 */ phosg::be_uint32_t component_flags;
  /* 1C */ phosg::be_uint32_t component_flags_mask;
  /* 20 */ uint8_t component_name_bytes;
  /* 21 */ char component_name[0]; // Actually [component_name_bytes] (p-string)
} __attribute__((packed));

struct BaseMediaInfoAtom { // gmin
  /* 08 */ phosg::be_uint32_t version_and_flags; // High byte = version; low 3 bytes = flags
  /* 0C */ phosg::be_uint16_t graphics_mode; // https://developer.apple.com/documentation/quicktime-file-format/graphics_modes
  /* 0E */ Color op_color;
  /* 14 */ phosg::be_int16_t sound_balance;
  /* 16 */ phosg::be_uint16_t reserved;
  /* 18 */
} __attribute__((packed));

struct DataReferenceAliasAtom { // alis
  /* 08 */ phosg::be_uint32_t version_and_flags; // High byte = version; low 3 bytes = flags
  /* 0C */ // If !(flags & 1), then an AliasRecord follows here (we don't support this)
} __attribute__((packed));

struct TimeToSampleTableEntry { // stts (CountedListAtom)
  /* 00 */ phosg::be_uint32_t sample_count;
  /* 04 */ phosg::be_uint32_t duration_per_sample;
  /* 08 */
} __attribute__((packed));

struct SampleToChunkTableEntry { // stsc (CountedListAtom)
  /* 00 */ phosg::be_uint32_t first_chunk;
  /* 04 */ phosg::be_uint32_t samples_per_chunk;
  /* 08 */ phosg::be_uint32_t sample_description_id;
  /* 0C */
} __attribute__((packed));

struct SampleSizesAtom { // stsz
  /* 08 */ phosg::be_uint32_t version_and_flags; // High byte = version; low 3 bytes = flags
  /* 0C */ phosg::be_uint32_t base_sample_size;
  /* 10 */ phosg::be_uint32_t entry_count;
  /* 14 */ // Entry entries[entry_count]; // Type depends on atom type
} __attribute__((packed));

class MovieParser : public Parser {
public:
  MovieParser(Movie* moov) : moov(moov) {}

protected:
  Movie* moov;
  Movie::Track* current_track = nullptr;
  Movie::Media* current_media = nullptr;
  Movie::DataReference* current_data_ref = nullptr;

  Movie::Track& require_track() {
    if (!this->current_track) {
      this->throw_parse_error("Atom must be within a track atom");
    }
    return *this->current_track;
  }
  Movie::Media& require_media() {
    if (!this->current_media) {
      this->throw_parse_error("Atom must be within a media atom");
    }
    return *this->current_media;
  }
  Movie::DataReference& require_data_ref() {
    if (!this->current_data_ref) {
      this->throw_parse_error("Atom must be within a data reference atom");
    }
    return *this->current_data_ref;
  }

  void ensure_sample_count(size_t count) {
    auto& media = this->require_media();
    if (media.samples.empty()) {
      media.samples.resize(count);
    } else if (media.samples.size() != count) {
      this->throw_parse_error("Incorrect sample count");
    }
  }

  virtual void handle_atom(uint32_t type, phosg::StringReader& r) {
    switch (type) {
      case MOVIE_ATOM_TYPE:
        this->parse_atom_list(r.extract(), -1, {MOVIE_HEADER_ATOM_TYPE});
        break;
      case MOVIE_HEADER_ATOM_TYPE: {
        const auto& atom = r.get<MovieHeaderAtom>();
        this->moov->creation_time = atom.creation_time;
        this->moov->modification_time = atom.modification_time;
        this->moov->time_scale = atom.time_scale;
        this->moov->duration = atom.duration;
        this->moov->preferred_rate = atom.preferred_rate.as_float();
        this->moov->preferred_volume = atom.preferred_volume.as_float();
        this->moov->matrix = atom.matrix;
        this->moov->preview_time = atom.preview_time;
        this->moov->preview_duration = atom.preview_duration;
        this->moov->poster_time = atom.poster_time;
        this->moov->selection_time = atom.selection_time;
        this->moov->selection_duration = atom.selection_duration;
        this->moov->current_time = atom.current_time;
        this->moov->next_track_id = atom.next_track_id;
        break;
      }
      case TRACK_ATOM_TYPE: {
        if (this->current_track) {
          this->throw_parse_error("Received track atom within another track");
        }
        Movie::Track track;
        this->current_track = &track;
        this->parse_atom_list(r.extract(), -1, {TRACK_HEADER_ATOM_TYPE, MEDIA_ATOM_TYPE});
        this->current_track = nullptr;
        if (!moov->tracks.emplace(track.track_id, std::move(track)).second) {
          this->throw_parse_error("Duplicate track ID: {}", track.track_id);
        }
        break;
      }
      case TRACK_HEADER_ATOM_TYPE: {
        auto& track = this->require_track();
        const auto& atom = r.get<TrackHeaderAtom>();
        track.creation_time = atom.creation_time;
        track.modification_time = atom.modification_time;
        track.track_id = atom.track_id;
        track.duration = atom.duration;
        track.layer = atom.layer;
        track.alternate_group = atom.alternate_group;
        track.volume = atom.volume;
        track.matrix = atom.matrix;
        track.width = atom.width;
        track.height = atom.height;
        break;
      }
      case EDITS_ATOM_TYPE:
        this->parse_atom_list(r.extract(), -1, {EDIT_LIST_ATOM_TYPE});
        break;
      case EDIT_LIST_ATOM_TYPE: {
        auto& track = this->require_track();
        const auto& atom = r.get<CountedListAtom>();
        for (size_t z = 0; z < atom.entry_count; z++) {
          const auto& entry = r.get<EditListAtomEntry>();
          track.edits.emplace_back(Movie::Edit{entry.duration.load(), entry.time.load(), entry.rate.as_float()});
        }
        break;
      }
      case MEDIA_ATOM_TYPE: {
        if (this->current_media) {
          this->throw_parse_error("Received media atom within another media");
        }
        this->current_media = &this->moov->media.emplace_back();
        this->parse_atom_list(r.extract(), -1, {MEDIA_HEADER_ATOM_TYPE});
        this->current_media = nullptr;
        break;
      }
      case MEDIA_HEADER_ATOM_TYPE: {
        auto& media = this->require_media();
        const auto& atom = r.get<MediaHeaderAtom>();
        media.creation_time = atom.creation_time;
        media.modification_time = atom.modification_time;
        media.time_scale = atom.time_scale;
        media.duration = atom.duration;
        media.language = atom.language;
        media.quality = atom.quality;
        break;
      }
      case HANDLER_ATOM_TYPE: {
        auto& media = this->require_media();
        bool in_minf = this->is_within_atom(MEDIA_INFO_ATOM_TYPE);
        std::unique_ptr<Movie::HandlerReference>& ref = in_minf ? media.data_handler : media.media_handler;
        if (ref) {
          this->throw_parse_error("Received multiple handler atoms for the same media{}", in_minf ? " info" : "");
        }
        ref = std::make_unique<Movie::HandlerReference>();
        const auto& atom = r.get<HandlerReferenceAtom>();
        ref->component_type = atom.component_type;
        ref->component_subtype = atom.component_subtype;
        ref->component_manufacturer = atom.component_manufacturer;
        ref->component_flags = atom.component_flags;
        ref->component_flags_mask = atom.component_flags_mask;
        ref->component_name = decode_mac_roman(r.read(atom.component_name_bytes));
        break;
      }
      case MEDIA_INFO_ATOM_TYPE: {
        if (!this->current_media) {
          this->throw_parse_error("Received media info atom outside of any media");
        }
        // TODO: For video, you need vmhd and hdlr; for sound, you need smhd and hdlr
        this->parse_atom_list(r.extract(), -1, {BASE_MEDIA_INFO_HEADER_ATOM_TYPE});
        break;
      }
      case BASE_MEDIA_INFO_HEADER_ATOM_TYPE: {
        if (!this->current_media) {
          this->throw_parse_error("Received base media info header atom outside of any media");
        }
        this->parse_atom_list(r.extract(), -1, {BASE_MEDIA_INFO_ATOM_TYPE});
        break;
      }
      case BASE_MEDIA_INFO_ATOM_TYPE: {
        auto& media = this->require_media();
        const auto& atom = r.get<BaseMediaInfoAtom>();
        media.graphics_mode = atom.graphics_mode;
        media.op_color = atom.op_color;
        media.sound_balance = atom.sound_balance;
        break;
      }
      case DATA_INFO_ATOM_TYPE:
        this->parse_atom_list(r.extract(), -1, {DATA_REFERENCE_ATOM_TYPE});
        break;
      case DATA_REFERENCE_ATOM_TYPE: {
        const auto& atom = r.get<CountedListAtom>();
        this->parse_atom_list(r.extract(), atom.entry_count);
        break;
      }
      case DATA_REFERENCE_ALIAS_ATOM_TYPE: {
        auto& media = this->require_media();
        const auto& atom = r.get<DataReferenceAliasAtom>();
        if (atom.version_and_flags & 1) {
          media.data_refs.emplace_back(Movie::DataReference{.is_self = true});
        } else {
          this->throw_parse_error("Data reference refers to non-self location");
        }
        break;
      }
      case DATA_REFERENCE_HANDLE_ATOM_TYPE: {
        auto& media = this->require_media();
        // Somewhere in these bytes is a Pascal string, but all examples I've seen contain only zeroes, so I don't know
        // which of these 9 bytes is the length byte of the Pascal string.
        for (size_t z = 0; z < 9; z++) {
          if (r.get_u8() != 0) {
            throw std::runtime_error("Handle data atom header contains nonzero bytes in unknown section");
          }
        }
        if (this->current_data_ref) {
          this->throw_parse_error("Received data reference atom inside another data reference");
        }
        this->current_data_ref = &media.data_refs.emplace_back();
        this->parse_atom_list(r.extract(), -1, {DATA_REFERENCE_HANDLE_DATA_ATOM_TYPE});
        this->current_data_ref = nullptr;
        break;
      }
      case DATA_REFERENCE_HANDLE_DATA_ATOM_TYPE:
        this->require_data_ref().handle_data = r.read(r.remaining());
        break;
      case SAMPLE_TABLE_ATOM_TYPE:
        this->parse_atom_list(r.extract());
        break;
      case SAMPLE_DESCRIPTION_ATOM_TYPE: {
        const auto& atom = r.get<CountedListAtom>();
        this->parse_atom_list(r.extract(), atom.entry_count);
        break;
      }
      case MUSI_TYPE:
        r.skip(sizeof(AtomBase));
        this->require_media().setup_sequence_data = r.read(r.remaining());
        break;
      case TIME_TO_SAMPLE_ATOM_TYPE: {
        auto& media = this->require_media();
        const auto& header = r.get<CountedListAtom>();
        const auto* entries = r.get_array<TimeToSampleTableEntry>(header.entry_count);
        size_t sample_count = 0;
        for (size_t z = 0; z < header.entry_count; z++) {
          sample_count += entries[z].sample_count;
        }
        if (media.samples.empty()) {
          media.samples.resize(sample_count);
        } else if (media.samples.size() != sample_count) {
          this->throw_parse_error("Incorrect sample count");
        }
        size_t sample_index = 0;
        for (size_t z = 0; z < header.entry_count; z++) {
          for (size_t w = 0; w < entries[z].sample_count; w++) {
            media.samples[sample_index].duration = entries[z].duration_per_sample;
            sample_index++;
          }
        }
        break;
      }
      case SAMPLE_TO_CHUNK_ATOM_TYPE: {
        auto& media = this->require_media();
        const auto& header = r.get<CountedListAtom>();
        for (size_t z = 0; z < header.entry_count; z++) {
          const auto& entry = r.get<SampleToChunkTableEntry>();
          media.sample_to_chunk_entries.emplace_back(Movie::SampleToChunkEntry{
              entry.first_chunk, entry.samples_per_chunk, entry.sample_description_id});
        }
        break;
      }
      case SAMPLE_SIZES_ATOM_TYPE: {
        auto& media = this->require_media();
        const auto& header = r.get<SampleSizesAtom>();
        this->ensure_sample_count(header.entry_count);
        for (size_t z = 0; z < header.entry_count; z++) {
          // TODO: Is this right...? It seems some moovs have an incorrect count here
          media.samples[z].size = r.eof() ? header.base_sample_size.load() : r.get_u32b();
        }
        break;
      }
      case CHUNK_OFFSETS_ATOM_TYPE: {
        auto& media = this->require_media();
        const auto& header = r.get<CountedListAtom>();
        this->ensure_sample_count(header.entry_count);
        for (size_t z = 0; z < header.entry_count; z++) {
          media.samples[z].chunk_offset = r.get_u32b();
        }
        break;
      }
      case CLIP_ATOM_TYPE:
      case USER_DATA_ATOM_TYPE:
        // TODO: We probably shouldn't entirely ignore these
        r.skip(r.remaining());
        break;
      default:
        this->throw_parse_error("Unknown atom type");
    }
  }
};

Movie::Movie(std::string_view moov, std::string_view mdat) {
  MovieParser parser(this);

  // Parse only the first atom; the mdat atom (unsized) may be appended after the moov atom
  parser.parse(moov.substr(0, phosg::StringReader(moov).get_u32b()));

  for (auto& media : this->media) {
    // TODO: How should we handle this? Is this what SampleToChunkEntry is for?
    if (media.data_refs.size() != 1) {
      throw std::runtime_error("Media does not have exactly one data reference");
    }
    const auto& data_ref = media.data_refs[0];
    std::string_view data_view;
    if (data_ref.is_self) {
      data_view = mdat.empty() ? moov : mdat;
    } else if (!data_ref.handle_data.empty()) {
      data_view = data_ref.handle_data;
    } else {
      throw std::runtime_error("Unknown data reference type");
    }
    for (auto& sample : media.samples) {
      sample.data = data_view.substr(sample.chunk_offset, sample.size);
    }
  }
}

QTMASequence Movie::as_qtma_sequence() const {
  // TODO: We probably can support this in the future; just return a vector/map
  if (this->media.size() != 1) {
    throw std::runtime_error("Movie has multiple media");
  }
  const auto& media = this->media[0];
  if (!media.media_handler) {
    throw std::runtime_error("Media handler definition is missing");
  }
  if (media.media_handler->component_type != MUSI_COMPONENT_TYPE) {
    throw std::runtime_error(std::format("Incorrect media handler component type ({})",
        string_for_resource_type(media.media_handler->component_type)));
  }
  if (media.media_handler->component_subtype != MUSI_COMPONENT_SUBTYPE) {
    throw std::runtime_error(std::format("Incorrect media handler component subtype ({})",
        string_for_resource_type(media.media_handler->component_subtype)));
  }
  if (media.setup_sequence_data.empty()) {
    throw std::runtime_error("Media setup sequence is missing");
  }

  std::string data = media.setup_sequence_data;
  for (const auto& sample : media.samples) {
    data += sample.data;
  }
  return QTMASequence(data.data(), data.size(), false);
}

} // namespace QuickTime
} // namespace ResourceDASM
