#include "Instrument.hh"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <format>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <vector>

#include "QuickTimeInstrument.hh"

namespace ResourceDASM {
namespace Audio {

static constexpr uint32_t SSAI_TYPE = 0x73736169; // 'ssai'
static constexpr uint32_t SEAN_TYPE = 0x7365616E; // 'sean'
static constexpr uint32_t TONE_TYPE = 0x746F6E65; // 'tone'
static constexpr uint32_t KNBL_TYPE = 0x6B6E626C; // 'knbl'
static constexpr uint32_t SINF_TYPE = 0x73696E66; // 'sinf'
static constexpr uint32_t SDSC_TYPE = 0x73647363; // 'sdsc'
static constexpr uint32_t SMIN_TYPE = 0x736D696E; // 'smin'
static constexpr uint32_t SNAM_TYPE = 0x736E616D; // 'snam'
static constexpr uint32_t SDAT_TYPE = 0x73646174; // 'sdat'
static constexpr uint32_t QUAL_TYPE = 0x7175616C; // 'qual'
static constexpr uint32_t QUID_TYPE = 0x71756964; // 'quid'
static constexpr uint32_t IINF_TYPE = 0x69696E66; // 'iinf'
static constexpr uint32_t IREF_TYPE = 0x69726566; // 'iref'
static constexpr uint32_t COPYRIGHT_WRT_TYPE = 0xA9777274; // '©wrt' (in MacRoman)
static constexpr uint32_t COPYRIGHT_CPY_TYPE = 0xA9637079; // '©cpy' (in MacRoman)
static constexpr uint32_t STR_TYPE = 0x73747220; // 'str '
static constexpr uint32_t MUSI_TYPE = 0x6D757369; // 'musi'
static constexpr uint32_t SS_TYPE = 0x73732020; // 'ss  '

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

struct KNBLBlock { // Knob list?
  struct Entry {
    uint8_t unknown_a1 = 0;
    uint8_t unknown_a2 = 0;
    phosg::be_uint16_t unknown_a3 = 0;
    phosg::be_uint32_t unknown_a4 = 0;
  } __attribute__((packed));

  /* 14 */ phosg::be_uint32_t entry_count = 0;
  /* 18 */ phosg::be_uint32_t unknown_a3 = 0;
  /* 1C */ // Entries follow here
} __attribute__((packed));

struct SDSCBlock { // Sample description
  /* 14 */ phosg::be_uint32_t format = 0; // E.g. 'raw '
  /* 18 */ phosg::be_uint16_t num_channels = 0;
  /* 1A */ phosg::be_uint16_t bits_per_sample = 0;
  /* 1C */ phosg::be_uint16_t sample_rate_integer = 0; // Whole number part of a Fixed
  /* 1E */ phosg::be_uint16_t sample_rate_fractional = 0; // Fractional part of a Fixed
  /* 20 */ phosg::be_uint16_t sdat_block_number = 0; // Block number of relevant sdat block
  /* 22 */ phosg::be_uint32_t unknown_a4 = 0;
  /* 26 */ phosg::be_uint32_t frame_count = 0; // TODO: Could also be sample_count or just num_sample_bytes
  /* 2A */ phosg::be_uint32_t unknown_a5 = 0;
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
      // TODO: There might be other important stuff in ToneBlock too
      this->name = decode_pstring<0x20>(tone_block.name);
      break;
    }

    case KNBL_TYPE: {
      const auto& knbl_block = r.get<KNBLBlock>();
      auto* knob_list = current_key_region ? &current_key_region->knobs : &this->knobs;
      if (!knob_list->empty()) {
        throw std::runtime_error("Received multiple knob lists in same context");
      }
      for (size_t z = 0; z < knbl_block.entry_count; z++) {
        const auto& entry = r.get<KNBLBlock::Entry>();
        knob_list->emplace_back(KnobEntry{entry.unknown_a1, entry.unknown_a2, entry.unknown_a3, entry.unknown_a4});
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

void TuneResource::NoteEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
  auto& ev1 = events.emplace_back(MIDIEvent{this->when, {}});
  ev1.data.emplace_back(0x90 | this->channel);
  ev1.data.emplace_back(this->key);
  ev1.data.emplace_back(this->vel);
  auto& ev2 = events.emplace_back(MIDIEvent{this->when + this->duration, {}});
  ev2.data.emplace_back(0x80 | this->channel);
  ev2.data.emplace_back(this->key);
  ev2.data.emplace_back(this->vel);
}
void TuneResource::PitchBendEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
  auto& ev = events.emplace_back(MIDIEvent{this->when, {}});
  ev.data.emplace_back(0xE0 | this->channel);
  ev.data.emplace_back(this->value & 0x7F);
  ev.data.emplace_back((this->value >> 7) & 0x7F);
}
void TuneResource::ControllerEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
  auto& ev = events.emplace_back(MIDIEvent{this->when, {}});
  ev.data.emplace_back(0xB0 | this->channel);
  ev.data.emplace_back(this->message);
  ev.data.emplace_back(this->value);
}
void TuneResource::ChannelSetupEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
  auto& ev1 = events.emplace_back(MIDIEvent{this->when, {}});
  ev1.data.emplace_back(0xC0 | this->channel);
  ev1.data.emplace_back(this->instrument_number);
  auto& ev2 = events.emplace_back(MIDIEvent{this->when, {}});
  ev2.data.emplace_back(0xB0 | this->channel);
  ev2.data.emplace_back(7); // Volume
  ev2.data.emplace_back(this->volume);
  auto& ev3 = events.emplace_back(MIDIEvent{this->when, {}});
  ev3.data.emplace_back(0xB0 | this->channel);
  ev3.data.emplace_back(10); // Panning
  ev3.data.emplace_back(this->panning);
  auto& ev4 = events.emplace_back(MIDIEvent{this->when, {}});
  ev4.data.emplace_back(0xE0 | this->channel);
  ev4.data.emplace_back(0); // Pitch bend
  ev4.data.emplace_back(this->pitch_bend);
}
void TuneResource::TrackEndEvent::add_midi_events(std::vector<MIDIEvent>& events) const {
  auto& ev = events.emplace_back(MIDIEvent{this->when, {}});
  ev.data.emplace_back(0xFF);
  ev.data.emplace_back(0x2F);
  ev.data.emplace_back(0x00);
}

TuneResource::TuneResource(const void* data, size_t size) {
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

  phosg::StringReader r(data, size);

  const auto& header = r.get<BlockHeader>();
  if (header.type != MUSI_TYPE) {
    throw std::runtime_error("Tune identifier is incorrect");
  }

  struct Event {
    uint64_t when;
    uint8_t status;
    std::string data;

    Event(uint64_t when, uint8_t status, uint8_t param) : when(when), status(status) {
      this->data.push_back(param);
    }

    Event(uint64_t when, uint8_t status, uint8_t param1, uint8_t param2) : when(when), status(status) {
      this->data.push_back(param1);
      this->data.push_back(param2);
    }
  };
  std::vector<Event> events;
  std::unordered_map<uint16_t, uint8_t> partition_id_to_channel;
  uint64_t current_time = 0;

  // The remainder of the data is playback commands
  while (!r.eof()) {
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
        ev->when = current_time;
        uint16_t partition_id;
        if (type == 0x09) {
          uint32_t options = r.get_u32b();
          partition_id = (event >> 16) & 0xFFF;
          ev->key = (event >> 8) & 0xFF;
          ev->vel = (options >> 22) & 0x7F;
          ev->duration = options & 0x3FFFFF;
        } else {
          partition_id = (event >> 24) & 0x1F;
          ev->key = ((event >> 18) & 0x3F) + 32;
          ev->vel = (event >> 11) & 0x7F;
          ev->duration = event & 0x7FF;
        }
        try {
          ev->channel = partition_id_to_channel.at(partition_id);
        } catch (const std::out_of_range&) {
          throw std::runtime_error("notes produced on uninitialized partition");
        }
        this->events.emplace_back(std::move(ev));
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
          partition_id = (event >> 24) & 0x1F;
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

        } else if (message == 32) { // Pitch bend
          auto ev = std::make_unique<PitchBendEvent>();
          ev->when = current_time;
          ev->channel = channel;

          // Clamp the value and convert to MIDI range (14-bit)
          ev->value = static_cast<int16_t>(value);
          if (ev->value < -0x0200) {
            ev->value = -0x0200;
          }
          if (ev->value > 0x01FF) {
            ev->value = 0x01FF;
          }
          ev->value = (ev->value + 0x200) * 0x10;

          this->events.emplace_back(std::move(ev));

        } else { // Some other controller message
          auto ev = std::make_unique<ControllerEvent>();
          ev->when = current_time;
          ev->channel = channel;
          ev->message = message;
          ev->value = value >> 8;
          this->events.emplace_back(std::move(ev));
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
            ev->when = current_time;
            ev->channel = channel;
            ev->instrument_number = inst.instrument_number;
            ev->volume = 0x7F;
            ev->panning = 0x40;
            ev->pitch_bend = 0x40;
            ev->collection_name = decode_pstring<0x20>(inst.collection_name);
            ev->instrument_name = decode_pstring<0x20>(inst.instrument_name);
            this->events.emplace_back(std::move(ev));
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
            ev->when = current_time;
            ev->channel = channel;
            ev->instrument_number = inst.instrument_number;
            ev->volume = 0x7F;
            ev->panning = 0x40;
            ev->pitch_bend = 0x40;
            ev->collection_name = decode_pstring<0x20>(inst.collection_name);
            ev->instrument_name = decode_pstring<0x20>(inst.instrument_name);
            this->events.emplace_back(std::move(ev));
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

  // Append the MIDI track end event
  auto end_ev = std::make_unique<TrackEndEvent>();
  end_ev->when = current_time;
  this->events.emplace_back(std::move(end_ev));
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

  // Generate the MIDI track
  std::string midi_track_data;
  uint64_t current_time = 0;
  for (const auto& event : midi_events) {
    uint64_t delta = event.when - current_time;
    current_time = event.when;

    // Write the delay field (encoded as variable-length int)
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
    midi_track_data += delta_str;

    for (uint8_t v : event.data) {
      midi_track_data.push_back(v);
    }
  }

  // Generate the MIDI headers
  MIDIHeader midi_header;
  midi_header.header.magic = 0x4D546864; // 'MThd'
  midi_header.header.size = 6;
  midi_header.format = 0;
  midi_header.track_count = 1;
  midi_header.division = 600; // Ticks per quarter note

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

} // namespace Audio
} // namespace ResourceDASM
