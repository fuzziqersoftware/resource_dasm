#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <array>
#include <deque>
#include <filesystem>
#include <format>
#include <map>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <phosg/Time.hh>
#include <string>

#include "MODSynthesizer.hh"
#include "SampleCache.hh"
#include "WAVFile.hh"

using namespace std;

namespace ResourceDASM {
namespace Audio {

static inline int8_t sign_extend_nybble(int8_t x) {
  if (x & 0x08) {
    return x | 0xF0;
  } else {
    return x & 0x0F;
  }
}

shared_ptr<Module> Module::parse(const string& data) {
  phosg::StringReader r(data.data(), data.size());

  auto mod = make_shared<Module>();

  // First, look ahead to see if this file uses any extensions. Annoyingly, the
  // signature field is pretty late in the file format, and some preceding
  // fields' sizes depend on the enabled extensions.
  try {
    mod->extension_signature = r.pget_u32b(0x438);
  } catch (const out_of_range&) {
    mod->extension_signature = 0;
  }

  size_t num_instruments = 31; // This is only not 31 in the default case below
  switch (mod->extension_signature) {
    case 0x4D2E4B2E: // M.K.
    case 0x4D214B21: // M!K!
    case 0x464C5434: // FLT4
    case 0x464C5438: // FLT8
      // Note: the observational spec appears to be incorrect about the FLT8
      // case - MODs with that signature appear to have only 4 channels.
      mod->num_tracks = 4;
      break;
    default:
      if ((mod->extension_signature & 0xF0FFFFFF) == 0x3043484E) { // xCHN
        mod->num_tracks = (mod->extension_signature >> 24) & 0x0F;
      } else if ((mod->extension_signature & 0xF0F0FFFF) == 0x30304348) { // xxCH
        mod->num_tracks = (((mod->extension_signature >> 24) & 0x0F) * 10) +
            ((mod->extension_signature >> 16) & 0x0F);
      } else { // Unrecognized signature; probably a very old MOD
        num_instruments = 15;
        mod->num_tracks = 4;
      }
  }

  mod->name = r.read(0x14);
  phosg::strip_trailing_zeroes(mod->name);

  mod->instruments.resize(num_instruments);
  for (size_t x = 0; x < num_instruments; x++) {
    auto& i = mod->instruments[x];
    i.index = x;
    i.name = r.read(0x16);
    phosg::strip_trailing_zeroes(i.name);
    i.num_samples = static_cast<uint32_t>(r.get_u16b()) << 1;
    i.finetune = sign_extend_nybble(r.get_u8());
    i.volume = r.get_u8();
    i.loop_start_samples = static_cast<uint32_t>(r.get_u16b()) << 1;
    i.loop_length_samples = static_cast<uint32_t>(r.get_u16b()) << 1;
  }

  mod->partition_count = r.get_u8();
  r.get_u8(); // unused
  r.read(mod->partition_table.data(), mod->partition_table.size());

  // We should have gotten to exactly the same offset that we read ahead to at
  // the beginning, unless there were not 31 instruments.
  if (num_instruments == 31) {
    uint32_t inplace_extension_signature = r.get_u32b();
    if (mod->extension_signature && mod->extension_signature != inplace_extension_signature) {
      throw logic_error(format("read-ahead extension signature ({:08X}) does not match inplace extension signature ({:08X})",
          mod->extension_signature, inplace_extension_signature));
    }
  }

  // Compute the number of patterns based on the contents of the partition
  // table. The number of patterns is the maximum value in the table (+1, since
  // pattern 0 is valid), and even patterns that do not appear in this table but
  // are less than the maximum value will exist in the file. Some rare MODs have
  // unreferenced patterns in the unused space after the used partitions; we
  // have to iterate the entire table (not just up to mod->partition_count) to
  // account for those as well.
  size_t num_patterns = 0;
  for (size_t x = 0; x < 0x80; x++) {
    if (num_patterns <= mod->partition_table[x]) {
      num_patterns = mod->partition_table[x] + 1;
    }
  }

  // Load the patterns.
  mod->patterns.resize(num_patterns);
  for (size_t x = 0; x < num_patterns; x++) {
    auto& pat = mod->patterns[x];
    pat.divisions.resize(mod->num_tracks * 64);
    for (auto& div : pat.divisions) {
      div.wx = r.get_u16b();
      div.yz = r.get_u16b();
    }
  }

  // Load the sample data for each instrument.
  for (auto& i : mod->instruments) {
    i.original_sample_data.resize(i.num_samples);
    size_t samples_read = r.read(i.original_sample_data.data(), i.num_samples);
    if (samples_read != i.num_samples) {
      i.original_sample_data.resize(samples_read);
      // TODO: Should we signal this somehow? It'd be rude to write to stderr
      // from a library...
      // phosg::fwrite_fmt(stderr,
      //     "Some sound data is missing for instrument {} (expected 0x{:X} samples, received 0x{:X} samples)",
      //     i.index + 1, i.num_samples, samples_read);
    }
    i.sample_data = convert_samples<float, int8_t>(i.original_sample_data);
  }

  return mod;
}

const map<uint16_t, const char*> Module::note_name_for_period{
    {1712, "C 0"},
    {1616, "C#0"},
    {1525, "D 0"},
    {1440, "D#0"},
    {1357, "E 0"},
    {1281, "F 0"},
    {1209, "F#0"},
    {1141, "G 0"},
    {1077, "G#0"},
    {1017, "A 0"},
    {961, "A#0"},
    {907, "B 0"},
    {856, "C 1"},
    {808, "C#1"},
    {762, "D 1"},
    {720, "D#1"},
    {678, "E 1"},
    {640, "F 1"},
    {604, "F#1"},
    {570, "G 1"},
    {538, "G#1"},
    {508, "A 1"},
    {480, "A#1"},
    {453, "B 1"},
    {428, "C 2"},
    {404, "C#2"},
    {381, "D 2"},
    {360, "D#2"},
    {339, "E 2"},
    {320, "F 2"},
    {302, "F#2"},
    {285, "G 2"},
    {269, "G#2"},
    {254, "A 2"},
    {240, "A#2"},
    {226, "B 2"},
    {214, "C 3"},
    {202, "C#3"},
    {190, "D 3"},
    {180, "D#3"},
    {170, "E 3"},
    {160, "F 3"},
    {151, "F#3"},
    {143, "G 3"},
    {135, "G#3"},
    {127, "A 3"},
    {120, "A#3"},
    {113, "B 3"},
    {107, "C 4"},
    {101, "C#4"},
    {95, "D 4"},
    {90, "D#4"},
    {85, "E 4"},
    {80, "F 4"},
    {76, "F#4"},
    {71, "G 4"},
    {67, "G#4"},
    {64, "A 4"},
    {60, "A#4"},
    {57, "B 4"},
};

void Module::disassemble_pattern_row(FILE* stream, uint8_t pattern_num, uint8_t y, bool use_color) const {
  static const phosg::TerminalFormat track_colors[5] = {
      phosg::TerminalFormat::FG_RED,
      phosg::TerminalFormat::FG_CYAN,
      phosg::TerminalFormat::FG_YELLOW,
      phosg::TerminalFormat::FG_GREEN,
      phosg::TerminalFormat::FG_MAGENTA,
  };

  const auto& p = this->patterns.at(pattern_num);
  phosg::fwrite_fmt(stream, "  {:02} +{:2}", pattern_num, y);
  for (size_t z = 0; z < this->num_tracks; z++) {
    const auto& div = p.divisions[y * this->num_tracks + z];
    uint8_t instrument_num = div.instrument_num();
    uint16_t period = div.period();
    uint16_t effect = div.effect();

    if (!instrument_num && !period && !effect) {
      if (use_color) {
        print_color_escape(stream, phosg::TerminalFormat::NORMAL, phosg::TerminalFormat::END);
      }
      fputs("  |            ", stream);
    } else {
      if (use_color) {
        print_color_escape(stream, phosg::TerminalFormat::NORMAL, phosg::TerminalFormat::END);
      }
      fputs("  |", stream);
      if (use_color) {
        if (instrument_num || period) {
          print_color_escape(stream, track_colors[z % 5], phosg::TerminalFormat::BOLD, phosg::TerminalFormat::END);
        } else {
          print_color_escape(stream, phosg::TerminalFormat::NORMAL, phosg::TerminalFormat::END);
        }
      }

      if (instrument_num) {
        phosg::fwrite_fmt(stream, "  {:02}", instrument_num);
      } else {
        fputs("  --", stream);
      }
      if (period == 0) {
        fputs(" ---", stream);
      } else {
        try {
          phosg::fwrite_fmt(stream, " {}", note_name_for_period.at(period));
        } catch (const out_of_range&) {
          phosg::fwrite_fmt(stream, " {:03X}", period);
        }
      }
      if (effect) {
        phosg::fwrite_fmt(stream, " {:03X}", effect);
      } else {
        fputs(" ---", stream);
      }
    }
  }
  if (use_color) {
    print_color_escape(stream, phosg::TerminalFormat::NORMAL, phosg::TerminalFormat::END);
  }
}

void Module::print_text(FILE* stream) const {
  phosg::fwrite_fmt(stream, "Name: {}\n", this->name);
  fputs("Instruments/Notes:\n", stream);
  for (const auto& i : this->instruments) {
    if (i.name.empty() && i.sample_data.empty()) {
      continue;
    }
    string escaped_name = phosg::escape_quotes(i.name);
    phosg::fwrite_fmt(stream, "  [{:02}] {}\n", i.index + 1, escaped_name);
  }
}

void Module::disassemble(FILE* stream, bool use_color) const {
  phosg::fwrite_fmt(stream, "Name: {}\n", this->name);
  phosg::fwrite_fmt(stream, "Tracks: {}\n", this->num_tracks);
  phosg::fwrite_fmt(stream, "Instruments: {}\n", this->instruments.size());
  phosg::fwrite_fmt(stream, "Partitions: {}\n", this->partition_count);
  phosg::fwrite_fmt(stream, "Extension signature: {:08X}\n", this->extension_signature);

  for (const auto& i : this->instruments) {
    fputc('\n', stream);
    string escaped_name = phosg::escape_quotes(i.name);
    phosg::fwrite_fmt(stream, "Instrument {}: {}\n", i.index + 1, escaped_name);
    phosg::fwrite_fmt(stream, "  Fine-tune: {:c}{}/8 semitones\n",
        (i.finetune < 0) ? '-' : '+', (i.finetune < 0) ? -i.finetune : i.finetune);
    phosg::fwrite_fmt(stream, "  Volume: {}/64\n", i.volume);
    phosg::fwrite_fmt(stream, "  Loop: start at {} for {} samples\n", i.loop_start_samples, i.loop_length_samples);
    phosg::fwrite_fmt(stream, "  Data: ({} samples)\n", i.sample_data.size());
  }

  vector<bool> patterns_used(0x80, false);
  for (size_t x = 0; x < this->partition_count; x++) {
    patterns_used.at(this->partition_table.at(x)) = true;
  }

  for (size_t x = 0; x < this->patterns.size(); x++) {
    if (!patterns_used.at(x)) {
      continue;
    }
    fputc('\n', stream);
    phosg::fwrite_fmt(stream, "Pattern {}\n", x);
    for (size_t y = 0; y < 64; y++) {
      this->disassemble_pattern_row(stream, x, y, use_color);
      fputc('\n', stream);
    }
  }

  fputs("\nPartition table:\n", stream);
  for (size_t x = 0; x < this->partition_count; x++) {
    phosg::fwrite_fmt(stream, "  Partition {}: {}\n", x, this->partition_table[x]);
  }
}

void Module::export_instruments(const char* output_prefix) const {
  // Andrew's observational spec notes that about 8287 bytes of data are sent to
  // the channel per second when a normal sample is played at C2. Empirically,
  // it seems like this is 0.5x the sample rate we need to make music sound
  // normal. Maybe the spec should have said 8287 words were sent to the channel
  // per second instead?
  for (const auto& i : this->instruments) {
    if (i.sample_data.empty()) {
      phosg::fwrite_fmt(stderr, "... ({}) \"{}\" -> (no sound data)\n", i.index + 1, i.name);
    } else {
      string escaped_name = phosg::escape_quotes(i.name);
      phosg::fwrite_fmt(stderr, "... ({}) \"{}\" -> {} samples, +{}ft, {:02X} vol, loop [{}x{}]\n",
          i.index + 1,
          escaped_name,
          i.sample_data.size(),
          i.finetune,
          i.volume,
          i.loop_start_samples,
          i.loop_length_samples);

      string output_filename_u8 = std::format("{}_{}.u8.wav", output_prefix, i.index + 1);
      vector<uint8_t> u8_sample_data;
      u8_sample_data.reserve(i.num_samples);
      for (int8_t sample : i.original_sample_data) {
        u8_sample_data.emplace_back(static_cast<uint8_t>(sample) - 0x80);
      }
      save_wav(output_filename_u8.c_str(), u8_sample_data, 16574, 1);

      string output_filename_f32 = std::format("{}_{}.f32.wav", output_prefix, i.index + 1);
      save_wav(output_filename_f32.c_str(), i.sample_data, 16574, 1);
    }
  }
}

MODSynthesizer::Timing::Timing(size_t sample_rate, size_t beats_per_minute, size_t ticks_per_division)
    : sample_rate(sample_rate),
      beats_per_minute(beats_per_minute),
      ticks_per_division(ticks_per_division),
      divisions_per_minute(static_cast<double>(24 * this->beats_per_minute) / this->ticks_per_division),
      ticks_per_second(
          static_cast<double>(this->divisions_per_minute * this->ticks_per_division) / 60),
      samples_per_tick(static_cast<double>(this->sample_rate * 60) / (this->divisions_per_minute * this->ticks_per_division)) {}
string MODSynthesizer::Timing::str() const {
  return std::format("{}kHz {}bpm {}t/d => {:g}d/m {:g}t/sec {:g}smp/t",
      this->sample_rate,
      this->beats_per_minute,
      this->ticks_per_division,
      this->divisions_per_minute,
      this->ticks_per_second,
      this->samples_per_tick);
}

void MODSynthesizer::TrackState::reset_division_scoped_effects() {
  this->arpeggio_arg = 0;
  this->sample_retrigger_interval_ticks = 0;
  this->sample_start_delay_ticks = 0;
  this->cut_sample_after_ticks = -1;
  this->delayed_sample_instrument_num = 0;
  this->delayed_sample_period = 0;
  this->per_tick_period_increment = 0;
  this->per_tick_volume_increment = 0;
  this->slide_target_period = 0;
  this->vibrato_amplitude = 0;
  this->tremolo_amplitude = 0;
  this->vibrato_cycles = 0;
  this->tremolo_cycles = 0;
}

void MODSynthesizer::TrackState::start_note(int32_t instrument_num, int32_t period, int32_t volume) {
  this->instrument_num = instrument_num;
  this->period = period;
  this->volume = volume;
  this->finetune_override = -0x80;
  this->input_sample_offset = 0.0;
  if (!(this->vibrato_waveform & 4)) {
    this->vibrato_offset = 0.0;
  }
  if (!(this->tremolo_waveform & 4)) {
    this->tremolo_offset = 0.0;
  }
  this->set_discontinuous_flag();
}

void MODSynthesizer::TrackState::set_discontinuous_flag() {
  this->dc_offset = this->last_sample;
  this->next_sample_may_be_discontinuous = true;
}

void MODSynthesizer::TrackState::decay_dc_offset(float delta) {
  if (this->dc_offset > 0) {
    if (this->dc_offset <= delta) {
      this->dc_offset = 0;
    } else {
      this->dc_offset -= delta;
    }
  } else if (this->dc_offset < 0) {
    if (this->dc_offset >= -delta) {
      this->dc_offset = 0;
    } else {
      this->dc_offset += delta;
    }
  }
}

MODSynthesizer::SongPosition::SongPosition(size_t partition_count, size_t partition_index)
    : partition_count(partition_count),
      partition_index(partition_index) {
  this->partitions_executed.resize(0x80, false);
}

void MODSynthesizer::SongPosition::advance_division() {
  if (this->pattern_break_target >= 0 && this->partition_break_target >= 0) {
    this->partition_index = this->partition_break_target;
    this->division_index = this->pattern_break_target;
    this->partition_break_target = -1;
    this->pattern_break_target = -1;
    this->pattern_loop_start_index = 0;

  } else if (this->jump_to_pattern_loop_start) {
    this->division_index = this->pattern_loop_start_index;
    this->jump_to_pattern_loop_start = false;

  } else {
    this->division_index++;
    if (this->division_index >= 64) {
      this->division_index = 0;
      this->partition_index++;
      this->pattern_loop_start_index = 0;
    }
  }

  if (this->partition_index >= this->partition_count) {
    return;
  }
  if (this->division_index >= 64) {
    throw runtime_error("pattern break opcode jumps past end of next pattern");
  }
  this->partitions_executed.at(this->partition_index) = true;
}

MODSynthesizer::MODSynthesizer(shared_ptr<const Module> mod, shared_ptr<const Options> opts)
    : log("[MODSynthesizer] ", opts->log_level),
      mod(mod),
      opts(opts),
      timing(this->opts->sample_rate),
      pos(this->mod->partition_count, this->opts->skip_partitions),
      tracks(this->mod->num_tracks),
      sample_cache(this->opts->resample_method) {
  // Initialize track state which depends on track index
  for (size_t x = 0; x < this->tracks.size(); x++) {
    this->tracks[x].index = x;
    if (this->opts->default_enable_surround) {
      this->tracks[x].enable_surround_effect = true;
    } else {
      // Tracks 1 and 2 (mod 4) are on the right; the others are on the left.
      // These assignments can be overridden by a [14][8][x] (0xE8x) effect.
      this->tracks[x].panning = ((x & 3) == 1) || ((x & 3) == 2)
          ? (0x40 + this->opts->default_panning_split)
          : (0x40 - this->opts->default_panning_split);
    }
  }
}

void MODSynthesizer::show_current_division() const {
  uint8_t pattern_index = this->mod->partition_table.at(this->pos.partition_index);
  phosg::fwrite_fmt(stderr, "  {:3}  |", this->pos.partition_index);
  this->mod->disassemble_pattern_row(stderr, pattern_index, this->pos.division_index, this->opts->use_color);
  uint64_t time_usecs = (this->pos.total_output_samples * 1000000) / (2 * this->opts->sample_rate);
  phosg::fwrite_fmt(stderr, "  |  {:3}/{:-2} @ {}s\n",
      this->timing.beats_per_minute, this->timing.ticks_per_division, phosg::format_duration(time_usecs));
}

void MODSynthesizer::execute_current_division_commands() {
  this->pos.pattern_break_target = -1;
  this->pos.partition_break_target = -1;
  this->pos.divisions_to_delay = 0;
  const auto& pattern = this->current_pattern();
  for (auto& track : this->tracks) {
    const auto& div = pattern.divisions.at(this->pos.division_index * this->mod->num_tracks + track.index);

    uint16_t effect = div.effect();
    uint16_t div_period = div.period();
    uint8_t div_ins_num = div.instrument_num();

    if ((effect & 0xFF0) != 0xED0) {
      // If an instrument number is given, update the track's instrument and
      // reset the track's volume. It appears this should happen even if the
      // note is not played due to an effect 3xx or 5xx, but it probably should
      // NOT happen if there's an effect EDx.
      if (div_ins_num) {
        track.volume = 64;
      }

      // There are surprisingly many cases for when a note should start vs. not
      // start, and different behavior for each. It seems correct behavior is:
      // 1. Period given, ins_num given: start a new note
      // 2. Period given, ins_num missing: start a new note with old ins_num
      //    and old volume
      // 3. Period missing, ins_num given and matches old ins_num: reset volume
      //    only (this is already done above)
      // 4. Period missing, ins_num given and does not match old ins_num: start
      //    a new note, unless old ins_num is zero, in which case just set the
      //    track's ins_num for future notes
      // 5. Period and ins_num both missing: do nothing
      // Effects [3] and [5] are special cases and do not result in a new note
      // being played, since they use the period as an additional parameter.
      // Effect [14][13] is special in that it does not start the new note
      // immediately, and the existing note, if any, should continue playing for
      // at least another tick.
      if (((effect & 0xF00) != 0x300) && ((effect & 0xF00) != 0x500) &&
          (div_period || // Cases (1) and (2)
              (div_ins_num && (div_ins_num != track.instrument_num)))) { // Case (4)
        uint16_t note_period = div_period ? div_period : track.period;
        uint8_t note_ins_num = div_ins_num ? div_ins_num : track.instrument_num;
        // We already reset the track's volume above if ins_num is given. If
        // ins_num is not given, we should use the previous note volume anyway.
        track.start_note(note_ins_num, note_period, track.volume);
      }
    }

    switch (effect & 0xF00) {
      case 0x000: // Arpeggio (or no effect)
        track.arpeggio_arg = effect & 0x0FF;
        break;

      case 0x100: // Slide up
        track.slide_target_period = 113;
        track.per_tick_period_increment = -(effect & 0x0FF);
        break;
      case 0x200: // Slide down
        track.slide_target_period = 856;
        track.per_tick_period_increment = effect & 0x0FF;
        break;
      case 0x300: // Slide to note
        track.slide_target_period = div_period;
        if (track.slide_target_period == 0) {
          track.slide_target_period = track.last_slide_target_period;
        }

        track.per_tick_period_increment = effect & 0xFF;
        if (track.per_tick_period_increment == 0) {
          track.per_tick_period_increment = track.last_per_tick_period_increment;
        } else if (track.slide_target_period < track.period) {
          track.per_tick_period_increment = -track.per_tick_period_increment;
        }

        track.last_slide_target_period = track.slide_target_period;
        track.last_per_tick_period_increment = track.per_tick_period_increment;
        break;

      case 0x400: // Vibrato
        track.vibrato_amplitude = effect & 0x00F;
        if (!track.vibrato_amplitude) {
          track.vibrato_amplitude = track.last_vibrato_amplitude;
        } else {
          track.last_vibrato_amplitude = track.vibrato_amplitude;
        }
        track.vibrato_cycles = (effect & 0x0F0) >> 4;
        if (!track.vibrato_cycles) {
          track.vibrato_cycles = track.last_vibrato_cycles;
        } else {
          track.last_vibrato_cycles = track.vibrato_cycles;
        }
        break;

      case 0x500: // Volume slide during slide to note
        // If this division has a period, use it; otherwise use the last
        // target period.
        track.slide_target_period = div_period;
        if (!track.slide_target_period) {
          track.slide_target_period = track.last_slide_target_period;
        }
        track.per_tick_period_increment = track.last_per_tick_period_increment;
        goto VolumeSlideEffect;

      case 0x600: // Volume slide during vibrato
        track.vibrato_amplitude = track.last_vibrato_amplitude;
        track.vibrato_cycles = track.last_vibrato_cycles;
        goto VolumeSlideEffect;

      case 0x700: // Tremolo
        track.tremolo_amplitude = effect & 0x00F;
        if (!track.tremolo_amplitude) {
          track.tremolo_amplitude = track.last_tremolo_amplitude;
        } else {
          track.last_tremolo_amplitude = track.tremolo_amplitude;
        }
        track.tremolo_cycles = (effect & 0x0F0) >> 4;
        if (!track.tremolo_cycles) {
          track.tremolo_cycles = track.last_tremolo_cycles;
        } else {
          track.last_tremolo_cycles = track.tremolo_cycles;
        }
        break;

      case 0x800: // Panning
        track.panning = effect & 0x0FF;
        track.enable_surround_effect = (track.panning == 0xA4);
        if (track.panning > 0x80) {
          track.panning = 0x80;
        }
        break;

      case 0x900: { // Set sample offset
        // The spec says the parameter is essentially <<8 but is measured in
        // words. This appears to be false - PlayerPRO shifts by 8 here (not
        // 9), and the MODs I've tried sound wrong when using 9.
        track.input_sample_offset = static_cast<int32_t>(effect & 0x0FF) << 8;
        // If the instrument has a loop and the offset ie beyond the end of
        // the loop, jump to the start of the loop instead.
        const auto& i = this->mod->instruments.at(track.instrument_num - 1);
        if ((i.loop_length_samples > 2) &&
            (track.input_sample_offset >= i.loop_start_samples + i.loop_length_samples)) {
          track.input_sample_offset = i.loop_start_samples;
        }
        break;
      }

      VolumeSlideEffect:
      case 0xA00: // Volume slide
        if (effect & 0x0F0) {
          track.per_tick_volume_increment = (effect & 0x0F0) >> 4;
        } else {
          track.per_tick_volume_increment = -(effect & 0x00F);
        }
        break;

      case 0xB00: { // Position jump
        // Don't allow a jump into a partition that has already executed, to
        // prevent infinite loops.
        uint8_t target_partition = effect & 0x07F;
        if (this->opts->allow_backward_position_jump ||
            !this->pos.partitions_executed.at(target_partition)) {
          this->pos.partition_break_target = target_partition;
          this->pos.pattern_break_target = 0;
        }
        break;
      }

      case 0xC00: // Set volume
        track.volume = effect & 0x0FF;
        if (track.volume > 64) {
          track.volume = 64;
        }
        track.set_discontinuous_flag();
        break;

      case 0xD00: // Pattern break
        // This was probably just a typo in the original Protracker, but it's
        // now propagated everywhere... the high 4 bits are multiplied by 10,
        // not 16.
        this->pos.partition_break_target = this->pos.partition_index + 1;
        this->pos.pattern_break_target = (((effect & 0x0F0) >> 4) * 10) + (effect & 0x00F);
        break;

      case 0xE00: { // Sub-effects
        switch (effect & 0x0F0) {
          case 0x000: // Enable/disable hardware filter
            // This is a hardware command on some Amigas; it looks like
            // PlayerPRO doesn't implement it, so neither will we.
            break;

          case 0x010: // Fine slide up
            track.period -= effect & 0x00F;
            break;
          case 0x020: // Fine slide down
            track.period += effect & 0x00F;
            break;

          case 0x030: // Set glissando on/off
            track.enable_discrete_glissando = !!(effect & 0x00F);
            break;

          case 0x040: // Set vibrato waveform
            // Note: there are only 8 waveforms defined (at least in the MOD
            // spec) so we don't bother with bit 3
            track.vibrato_waveform = effect & 0x007;
            break;

          case 0x050: // Set finetune override
            track.finetune_override = sign_extend_nybble(effect & 0x00F);
            break;

          case 0x060: { // Loop pattern
            uint8_t times = effect & 0x00F;
            if (times == 0) {
              this->pos.pattern_loop_start_index = this->pos.division_index;
            } else if (this->pos.pattern_loop_times_remaining == -1) {
              this->pos.pattern_loop_times_remaining = times - 1;
              this->pos.jump_to_pattern_loop_start = true;
            } else if (this->pos.pattern_loop_times_remaining > 0) {
              this->pos.pattern_loop_times_remaining--;
              this->pos.jump_to_pattern_loop_start = true;
            } else {
              this->pos.pattern_loop_times_remaining = -1;
            }
            break;
          }

          case 0x070: // Set tremolo waveform
            track.tremolo_waveform = effect & 0x007;
            break;

          case 0x080: { // Set panning (PlayerPRO)
            uint16_t panning = effect & 0x00F;

            // To deal with the "halves" of the range not being equal sizes,
            // we stretch out the right half a bit so [14][8][15] hits the
            // right side exactly.
            if (panning <= 8) {
              panning *= 16;
            } else {
              panning *= 17;
            }
            track.panning = (panning * 0x80) / 0xFF;

            if (track.panning < 0) {
              track.panning = 0;
            } else if (track.panning > 0x80) {
              track.panning = 0x80;
            }
            break;
          }

          case 0x090: // Retrigger sample every x ticks
            track.sample_retrigger_interval_ticks = effect & 0x0F;
            break;
          case 0x0A0: // Fine volume slide up
            track.volume += effect & 0x00F;
            if (track.volume > 64) {
              track.volume = 64;
            }
            break;
          case 0x0B0: // Fine volume slide up
            track.volume -= effect & 0x00F;
            if (track.volume < 0) {
              track.volume = 0;
            }
            break;
          case 0x0C0: // Cut sample after ticks
            track.cut_sample_after_ticks = effect & 0x00F;
            break;
          case 0x0D0: // Delay sample
            track.sample_start_delay_ticks = effect & 0x00F;
            track.delayed_sample_instrument_num = div_ins_num;
            track.delayed_sample_period = div_period;
            break;
          case 0x0E0: // Delay pattern
            this->pos.divisions_to_delay = effect & 0x00F;
            break;

            // TODO: Implement this effect. See MODs:
            //   deepest space
            //   Gummisnoppis
            // [14][15]: Invert loop
            // Where [14][15][x] means "if x is greater than 0, then play the
            // current sample's loop upside down at speed x". Each byte in the
            // sample's loop will have its sign changed (negated). It will only
            // work if the sample's loop (defined previously) is not too big. The
            // speed is based on an internal table.

          default:
            goto unimplemented_effect;
        }
        break;
      }

      case 0xF00: { // Set speed
        uint8_t v = effect & 0xFF;
        if (v <= 32) {
          if (v == 0) {
            v = 1;
          }
          this->timing = Timing(
              this->timing.sample_rate, this->timing.beats_per_minute, v);
        } else {
          this->timing = Timing(
              this->timing.sample_rate, v, this->timing.ticks_per_division);
        }
        break;
      }
      unimplemented_effect:
      default:
        this->log.warning_f("Unimplemented effect {:03X}\n", effect);
    }
  }
}

float MODSynthesizer::get_vibrato_tremolo_wave_amplitude(float offset, uint8_t waveform) {
  float integer_part;
  float wave_progress = modff(offset, &integer_part);
  switch (waveform & 3) {
    case 0: // Sine wave
    case 3: // Supposedly random, but that would probably sound weird
      return sinf(wave_progress * 2 * M_PI);
    case 1: // Descending sawtooth wave
      return 1.0 - (2.0 * wave_progress);
    case 2: // Square wave
      return (wave_progress < 0.5) ? 1.0 : -1.0;
    default:
      throw logic_error("invalid vibrato/tremolo waveform");
  }
}

uint16_t MODSynthesizer::nearest_note_for_period(uint16_t period, bool snap_up) {
  auto it = Module::note_name_for_period.lower_bound(period);
  if (it == Module::note_name_for_period.end()) { // Period off the low end of the scale
    return Module::note_name_for_period.rbegin()->first; // Return lowest note
  }
  if (it == Module::note_name_for_period.begin()) { // Period off the high end of the scale
    return it->first; // Return highest note
  }
  if (it->first == period) { // Period exactly matches a note
    return period;
  }

  // Period is between notes; it.first is the note below it
  if (snap_up) {
    it--; // We want the note above instead
  }
  return it->first;
}

bool MODSynthesizer::render_current_division_audio() {
  bool should_continue = true;
  for (size_t tick_num = 0; tick_num < this->timing.ticks_per_division; tick_num++) {
    size_t num_tick_samples;
    if (opts->tempo_bias != 1.0) {
      num_tick_samples = this->timing.samples_per_tick / opts->tempo_bias;
    } else {
      num_tick_samples = this->timing.samples_per_tick;
    }
    // Note: we do this multiplication after the above computation because
    // num_tick_samples must not be an odd number, so we don't want to *2
    // during the floating-point computation.
    num_tick_samples *= 2;
    vector<float> tick_samples(num_tick_samples);
    for (auto& track : this->tracks) {

      // If track is muted or another track is solo'd, don't play its sound
      if (this->opts->mute_tracks.count(track.index) ||
          (!this->opts->solo_tracks.empty() && !this->opts->solo_tracks.count(track.index))) {
        track.last_sample = 0;
        continue;
      }

      if (track.sample_start_delay_ticks &&
          (track.sample_start_delay_ticks == tick_num)) {
        // Delay requested via effect EDx and we should start the sample now
        track.start_note(track.delayed_sample_instrument_num, track.delayed_sample_period, 64);
        track.sample_start_delay_ticks = 0;
        track.delayed_sample_instrument_num = 0;
        track.delayed_sample_period = 0;
      }

      if (track.instrument_num == 0 || track.period == 0) {
        track.last_sample = 0;
        continue; // Track has not played any sound yet
      }

      const auto& i = this->mod->instruments.at(track.instrument_num - 1);
      if (track.input_sample_offset >= i.sample_data.size()) {
        track.last_sample = 0;
        continue; // Previous sound is already done
      }

      if (track.sample_retrigger_interval_ticks &&
          ((tick_num % track.sample_retrigger_interval_ticks) == 0)) {
        track.input_sample_offset = 0;
      }
      if ((track.cut_sample_after_ticks >= 0) &&
          (tick_num == static_cast<size_t>(track.cut_sample_after_ticks))) {
        track.volume = 0;
      }

      float effective_period = track.enable_discrete_glissando
          ? this->nearest_note_for_period(track.period,
                track.per_tick_period_increment < 0)
          : track.period;
      int8_t finetune = (track.finetune_override == -0x80) ? i.finetune : track.finetune_override;
      if (finetune) {
        effective_period *= pow(2, -static_cast<float>(finetune) / (12.0 * 8.0));
      }

      // Handle arpeggio and vibrato effects, which can change a sample's
      // period within a tick. To handle this, we further divide each division
      // into "segments" where different periods can be used. Segments can
      // cross tick boundaries, which makes the sample generation loop below
      // unfortunately rather complicated.
      size_t division_output_offset = tick_num * tick_samples.size();
      // This is a list of (start_at_output_sample, instrument_period) for
      // the current tick
      vector<pair<size_t, float>> segments;
      if (track.vibrato_amplitude && track.vibrato_cycles) {
        if (track.arpeggio_arg) {
          throw logic_error("cannot have both arpeggio and vibrato effects in the same division");
        }
        for (size_t x = 0; x < this->opts->vibrato_resolution; x++) {
          float amplitude = this->get_vibrato_tremolo_wave_amplitude(
              track.vibrato_offset + static_cast<float>(track.vibrato_cycles) / (64 * this->opts->vibrato_resolution), track.vibrato_waveform);
          amplitude *= static_cast<float>(track.vibrato_amplitude) / 16.0;
          segments.emplace_back(make_pair(
              (num_tick_samples * x) / this->opts->vibrato_resolution,
              effective_period * pow(2, -amplitude / 12.0)));
        }

      } else if (track.arpeggio_arg) {
        float periods[3] = {
            effective_period,
            effective_period / powf(2, ((track.arpeggio_arg >> 4) & 0x0F) / 12.0),
            effective_period / powf(2, (track.arpeggio_arg & 0x0F) / 12.0),
        };

        // The spec describes arpeggio effects as being "evenly spaced" within
        // the division, but some trackers (e.g. PlayerPRO) do not implement
        // this - instead, they simply iterate through the arpeggio periods
        // for each tick, and if the number of ticks per division isn't
        // divisible by 3, then some periods are held for longer. This
        // actually sounds better for some MODs, so we implement both this
        // behavior and true evenly-spaced arpeggio.
        if (this->opts->arpeggio_frequency <= 0) {
          for (size_t x = 0; x < timing.ticks_per_division; x++) {
            segments.emplace_back(make_pair(x * num_tick_samples, periods[x % 3]));
          }

        } else {
          // We multiply by 2 here since this is relative to the number of
          // output samples generated, and the output is stereo.
          size_t interval_samples =
              2 * timing.samples_per_tick * timing.ticks_per_division;

          // An arpeggio effect causes three fluctuations in the order
          // (note, note+x, note+y), a total of arpeggio_frequency times. The
          // intervals are evenly spaced across the division, independent of
          // tick boundaries.
          size_t denom = this->opts->arpeggio_frequency * 3;
          for (size_t x = 0; x < this->opts->arpeggio_frequency; x++) {
            segments.emplace_back(make_pair((3 * x + 0) * interval_samples / denom, periods[0]));
            segments.emplace_back(make_pair((3 * x + 1) * interval_samples / denom, periods[1]));
            segments.emplace_back(make_pair((3 * x + 2) * interval_samples / denom, periods[2]));
          }
        }

      } else {
        // If neither arpeggio nor vibrato happens in this tick, then the
        // period is effectively constant.
        segments.emplace_back(make_pair(0, effective_period));
      }

      // Figure out the volume for this tick.
      int8_t effective_volume = track.volume;
      if (track.tremolo_amplitude && track.tremolo_cycles) {
        effective_volume += this->get_vibrato_tremolo_wave_amplitude(
                                track.tremolo_offset + static_cast<float>(track.tremolo_cycles) / 64, track.tremolo_waveform) *
            track.tremolo_amplitude;
        if (effective_volume < 0) {
          effective_volume = 0;
        } else if (effective_volume > 64) {
          effective_volume = 64;
        }
      }
      float track_volume_factor = static_cast<float>(effective_volume) / 64.0;
      float ins_volume_factor = static_cast<float>(i.volume) / 64.0;

      // If the volume changed, the waveform might become discontinuous, so
      // enable tick cleanup.
      if (this->opts->correct_ticks_on_all_volume_changes &&
          (track.last_effective_volume != effective_volume)) {
        track.set_discontinuous_flag();
      }
      track.last_effective_volume = effective_volume;

      // Apply the appropriate portion of the instrument's sample data to the
      // tick output data.
      const vector<float>* resampled_data = nullptr;
      ssize_t segment_index = -1;
      double src_ratio = -1.0;
      double resampled_offset = -1.0;
      double loop_start_offset = -1.0;
      double loop_end_offset = -1.0;
      for (size_t tick_output_offset = 0;
          tick_output_offset < tick_samples.size();
          tick_output_offset += 2, division_output_offset += 2) {

        // Advance to the appropriate segment if there is one
        bool changed_segment = false;
        while ((segment_index < static_cast<ssize_t>(segments.size() - 1)) &&
            division_output_offset >= segments.at(segment_index + 1).first) {
          segment_index++;
          changed_segment = true;
        }
        if (changed_segment) {
          const auto& segment = segments.at(segment_index);
          // Resample the instrument to the appropriate pitch
          // The input samples to be played per second is:
          // track_input_samples_per_second = hardware_freq / (2 * period)
          // To convert this to the number of output samples per input sample,
          // all we have to do is divide the output sample rate by it:
          // out_samples_per_in_sample = sample_rate / (hardware_freq / (2 * period))
          // out_samples_per_in_sample = (sample_rate * 2 * period) / hardware_freq
          // This gives how many samples to generate for each input sample.
          src_ratio =
              static_cast<double>(2 * this->timing.sample_rate * segment.second) / this->opts->amiga_hardware_frequency;
          resampled_data = &this->sample_cache.resample_add(
              track.instrument_num, i.sample_data, 1, src_ratio);
          resampled_offset = track.input_sample_offset * src_ratio;

          // The sample has a loop if the length in words is > 1. We convert words
          // to samples long before this point, so we have to check for >2 here.
          loop_start_offset = static_cast<double>(i.loop_start_samples) * src_ratio;
          loop_end_offset = (i.loop_length_samples > 2)
              ? static_cast<double>(i.loop_start_samples + i.loop_length_samples) * src_ratio
              : 0.0;
        }

        if (!resampled_data) {
          throw logic_error("resampled data not present at sound generation time");
        }

        // The sample could "end" here (and not below) because of
        // floating-point imprecision
        if (resampled_offset >= resampled_data->size()) {
          if (loop_end_offset != 0.0) {
            // This should only happen if the loop ends right at the end of
            // the sample, so we can just blindly reset to the loop start
            // offset.
            track.input_sample_offset = loop_start_offset / src_ratio;
          } else {
            track.input_sample_offset = i.sample_data.size();
          }
          break;
        }

        float overall_volume_factor = (this->opts->volume_exponent == 1.0)
            ? (track_volume_factor * ins_volume_factor)
            : pow(track_volume_factor * ins_volume_factor, this->opts->volume_exponent);

        // When a new sample is played on a track and it interrupts another
        // already-playing sample, the waveform can become discontinuous,
        // which causes an audible ticking sound. To avoid this, we store a
        // DC offset in each track and adjust it so that the new sample begins
        // at the same amplitude. The DC offset then decays after each
        // subsequent sample and fairly quickly reaches zero. This eliminates
        // the tick and doesn't leave any other audible effects.
        float sample_from_ins = resampled_data->at(static_cast<size_t>(resampled_offset)) *
            overall_volume_factor;
        if (track.next_sample_may_be_discontinuous) {
          track.last_sample = track.dc_offset;
          track.dc_offset -= sample_from_ins;
          track.next_sample_may_be_discontinuous = false;
        } else {
          track.last_sample = sample_from_ins + track.dc_offset;
        }
        track.decay_dc_offset(this->dc_offset_decay);

        // Apply panning and produce the final sample. The surround effect
        // (enabled with effect 8A4) plays the same sample in both ears, but
        // with one inverted.
        float l_factor, r_factor;
        if (track.enable_surround_effect) {
          l_factor = (track.index & 1) ? -0.5 : 0.5;
          r_factor = (track.index & 1) ? 0.5 : -0.5;
        } else {
          l_factor = (1.0 - static_cast<float>(track.panning) / 128.0);
          r_factor = (static_cast<float>(track.panning) / 128.0);
        }
        tick_samples[tick_output_offset + 0] +=
            track.last_sample * l_factor * this->opts->global_volume;
        tick_samples[tick_output_offset + 1] +=
            track.last_sample * r_factor * this->opts->global_volume;

        // The observational spec claims that the loop only begins after the
        // the sample has been played to the end once, but this seems false.
        // It seems like we should instead always jump back when we reach the
        // end of the loop region, even the first time we reach it (which is
        // what's implemented here).
        resampled_offset++;
        // Since we use floats to represent the loop points, we actually could
        // miss it and think the sample ended when there's really a loop to be
        // played! To handle this, we assume that if we reach the end and a
        // loop is defined, we should just always use it.
        if ((loop_end_offset != 0.0) &&
            ((resampled_offset >= loop_end_offset) || (resampled_offset >= resampled_data->size() - 1))) {
          resampled_offset = loop_start_offset;
        } else if (resampled_offset >= resampled_data->size()) {
          track.input_sample_offset = i.sample_data.size();
          break;
        }

        // Advance the input offset by a proportional amount to the sound we
        // just generated, so the next tick or segment will start at the right
        // place
        track.input_sample_offset = resampled_offset / src_ratio;
      }

      // Apparently per-tick slides don't happen after the last tick in the
      // division. (Why? Protracker bug?)
      if (tick_num != timing.ticks_per_division - 1) {
        if (track.per_tick_period_increment) {
          track.period += track.per_tick_period_increment;
          // If a slide to note effect (3) is underway, enforce the limit
          // given by the effect command
          if (track.slide_target_period &&
              (((track.per_tick_period_increment > 0) &&
                   (track.period > track.slide_target_period)) ||
                  ((track.per_tick_period_increment < 0) &&
                      (track.period < track.slide_target_period)))) {
            track.period = track.slide_target_period;
            track.per_tick_period_increment = 0;
            track.slide_target_period = 0;
          }
          if (track.period <= 0) {
            track.period = 1;
          }
        }
        if (track.per_tick_volume_increment) {
          track.volume += track.per_tick_volume_increment;
          if (track.volume < 0) {
            track.volume = 0;
          } else if (track.volume > 64) {
            track.volume = 64;
          }
        }
      }
      track.vibrato_offset += static_cast<float>(track.vibrato_cycles) / 64;
      if (track.vibrato_offset >= 1) {
        track.vibrato_offset -= 1;
      }
      track.tremolo_offset += static_cast<float>(track.tremolo_cycles) / 64;
      if (track.tremolo_offset >= 1) {
        track.tremolo_offset -= 1;
      }
    }
    this->pos.total_output_samples += tick_samples.size();
    if (!on_tick_samples_ready(std::move(tick_samples)) || this->exceeded_time_limit()) {
      should_continue = false;
      break;
    }
  }

  // Clear division-scoped effects on all tracks
  for (auto& track : tracks) {
    track.reset_division_scoped_effects();
  }
  return should_continue;
}

void MODSynthesizer::run() {
  bool changed_partition = false;
  this->max_output_samples = this->opts->sample_rate * this->opts->max_output_seconds * 2;
  while (this->pos.partition_index < this->mod->partition_count && !this->exceeded_time_limit()) {
    this->execute_current_division_commands();
    // Note: We print the partition after executing its commands so that the
    // timing information will be consistent if any Fxx commands were run.
    if (this->opts->print_status_while_playing) {
      if (changed_partition) {
        fputc('\n', stderr);
      }
      this->show_current_division();
    }
    for (this->pos.divisions_to_delay++; this->pos.divisions_to_delay > 0; this->pos.divisions_to_delay--) {
      if (!this->render_current_division_audio()) {
        break;
      }
    }
    uint8_t old_partition_index = this->pos.partition_index;
    this->pos.advance_division();
    changed_partition = (this->pos.partition_index != old_partition_index);
  }
}

MODRenderer::MODRenderer(shared_ptr<const Module> mod, shared_ptr<const Options> opts)
    : MODSynthesizer(mod, opts) {}

bool MODRenderer::on_tick_samples_ready(vector<float>&& samples) {
  this->tick_samples.emplace_back(std::move(samples));
  return true;
}

const vector<float>& MODRenderer::result() {
  if (this->all_tick_samples.empty()) {
    this->all_tick_samples.reserve(this->pos.total_output_samples);
    for (const auto& s : this->tick_samples) {
      this->all_tick_samples.insert(this->all_tick_samples.end(), s.begin(), s.end());
    }
  }
  return this->all_tick_samples;
}

} // namespace Audio
} // namespace ResourceDASM
