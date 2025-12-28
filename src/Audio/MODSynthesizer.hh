#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <memory>
#include <phosg/Strings.hh>
#include <string>
#include <unordered_set>
#include <vector>

#include "SampleCache.hh"

namespace ResourceDASM {
namespace Audio {

struct Module {
  struct Instrument {
    size_t index;
    std::string name;
    uint32_t num_samples;
    int8_t finetune; // Pitch shift (positive or negative) in increments of 1/8 semitone
    uint8_t volume; // 0-64
    uint16_t loop_start_samples;
    uint16_t loop_length_samples;
    std::vector<int8_t> original_sample_data;
    std::vector<float> sample_data;

    inline bool loop_valid() const {
      return (this->loop_start_samples < this->sample_data.size()) &&
          ((this->loop_start_samples + this->loop_length_samples) < this->sample_data.size());
    }
  };

  struct Pattern {
    struct Division {
      uint16_t wx;
      uint16_t yz;

      inline uint8_t instrument_num() const {
        return ((this->wx >> 8) & 0xF0) | ((this->yz >> 12) & 0x0F);
      }
      inline uint16_t period() const {
        return this->wx & 0x0FFF;
      }
      inline uint16_t effect() const {
        return this->yz & 0x0FFF;
      }
    };

    std::vector<Division> divisions;
  };

  std::string name;
  size_t num_tracks;
  // Note: instrument references are 1-based pretty much everywhere and this
  // array is of course 0-based, so remember to +1/-1 the index as needed
  std::vector<Instrument> instruments;
  uint8_t partition_count;
  std::array<uint8_t, 0x80> partition_table;
  uint32_t extension_signature;
  std::vector<Pattern> patterns;

  static const std::map<uint16_t, const char*> note_name_for_period;

  static std::shared_ptr<Module> parse(const std::string& data);

  void disassemble_pattern_row(FILE* stream, uint8_t pattern_num, uint8_t y, bool use_color) const;
  void disassemble_pattern_cell(FILE* stream, uint8_t pattern_num, uint8_t y, size_t track_num, bool use_color) const;
  void disassemble(FILE* stream, bool use_color) const;
  void export_instruments(const char* output_prefix) const;
  void print_text(FILE* stream) const;
};

class MODSynthesizer {
public:
  struct Options {
    // Base frequency for all playback. Generally you shouldn't change this.
    double amiga_hardware_frequency = 7159090.5;
    // Sample rate of the resulting audio.
    size_t sample_rate = 48000;
    // Method to use when resampling instruments for different notes. EXTEND
    // produces crisper-sounding audio and is generally appropriate for
    // Protracker/Soundtracker modules.
    ResampleMethod resample_method = ResampleMethod::EXTEND;
    // How far each track's output is from center on the left-right spectrum
    int8_t default_panning_split = 0x20; // -0x40-0x40
    // Whether to enable the panning surround effect by default
    bool default_enable_surround = false;
    // Overall volume factor; note that no clipping is performed, so this could
    // produce very loud output if set incorrectly!
    float global_volume = 1.0;
    // If not zero, the synthesizer will stop after this many seconds of audio
    // have been generated
    float max_output_seconds = 0.0;
    // Number of partitions to skip at the beginning of synthesis
    size_t skip_partitions = 0;
    // Number of divisions to skip at the beginning of synthesis (after
    // skipping partitions)
    size_t skip_divisions = 0;
    // Whether to allow backward position jump. If this is true, some songs
    // will play forever; if this is false, synthesis will always stop in a
    // finite amount of time
    bool allow_backward_position_jump = false;
    // Whether to enable tick correction on Cxx effects
    bool correct_ticks_on_all_volume_changes = false;
    // Exponent by which Cxx volume effects are scaled. A value of 1 here means
    // C20 will be exactly half of the amplitude of C40; a value less than 1
    // means that C20 will be more than half of the amplitude of C40. It seems
    // that synthesizers of the Classic Mac OS era used a value somewhere in
    // the range [0.6, 0.8] here, so we choose a default that provides similar-
    // sounding results.
    float volume_exponent = 0.65;
    // Which tracks to mute during synthesis. Muted tracks still can execute
    // commands, they just produce no audio.
    std::unordered_set<size_t> mute_tracks;
    // If not empty, audio is muted for all tracks except those specified in
    // this set
    std::unordered_set<size_t> solo_tracks;
    // Which instruments to mute during synthesis.
    std::unordered_set<size_t> mute_instruments;
    // If not empty, audio is muted for all instruments except those specified
    // in this set
    std::unordered_set<size_t> solo_instruments;
    // Factor by which to speed up or slow down the entire song
    float tempo_bias = 1.0;
    // Number of full arpeggio cycles per division. If set to zero, arpeggios
    // use tick boundaries instead of being evenly spaced across the division
    size_t arpeggio_frequency = 0;
    // TODO: Document how this option works
    size_t vibrato_resolution = 1;
    // If true, the synthesizer prints a line for each division showing what
    // happened on each track
    bool print_status_while_playing = false;
    // If true, the synthesizer also shows track status while playing. No
    // effect if print_status_while_playing is false.
    bool print_track_debug_while_playing = false;
    // If true, the printed output uses color when a track starts a new note.
    // No effect if print_status_while_playing is false
    bool use_color = false;
    // Log level for the synthesizer. This is generally used for warnings (e.g.
    // unimplemented effect types), so set this to L_ERROR if you don't want to
    // see those.
    phosg::LogLevel log_level = phosg::LogLevel::L_INFO;
  };

  MODSynthesizer(std::shared_ptr<const Module> mod, std::shared_ptr<const Options> opts);
  void run_one();
  void run_all();

  bool done() const;

  inline std::shared_ptr<const Module> get_module() const {
    return this->mod;
  }
  inline std::shared_ptr<const Options> get_options() const {
    return this->opts;
  }

protected:
  struct Timing {
    size_t sample_rate;
    size_t beats_per_minute;
    size_t ticks_per_division;
    double divisions_per_minute;
    double ticks_per_second;
    double samples_per_tick;

    Timing(size_t sample_rate, size_t beats_per_minute = 125, size_t ticks_per_division = 6);
    std::string str() const;
  };

  struct TrackState {
    size_t index = 0;

    int32_t instrument_num = 0; // 1-based! 0 = no instrument
    int32_t period = 0;
    int32_t volume = 64; // 0 - 64
    int32_t panning = 64; // 0 (left) - 128 (right)
    bool enable_surround_effect = false;
    int8_t finetune_override = -0x80; // -0x80 = use instrument finetune (default)
    double input_sample_offset = 0.0; // relative to input samples, not any resampling thereof
    uint8_t vibrato_waveform = 0;
    uint8_t tremolo_waveform = 0;
    float vibrato_offset = 0.0;
    float tremolo_offset = 0.0;
    bool enable_discrete_glissando = false;

    uint8_t arpeggio_arg = 0;
    uint8_t sample_retrigger_interval_ticks = 0;
    uint8_t sample_start_delay_ticks = 0;
    int8_t cut_sample_after_ticks = -1;
    int32_t delayed_sample_instrument_num = 0;
    int32_t delayed_sample_period = 0;
    int16_t per_tick_period_increment = 0;
    int16_t per_tick_volume_increment = 0;
    int16_t slide_target_period = 0;
    int16_t vibrato_amplitude = 0;
    int16_t tremolo_amplitude = 0;
    int16_t vibrato_cycles = 0;
    int16_t tremolo_cycles = 0;

    // These are not reset each division, and are used for effects that continue a
    // previous effect
    int16_t last_slide_target_period = 0;
    int16_t last_per_tick_period_increment = 0;
    int16_t last_vibrato_amplitude = 0;
    int16_t last_tremolo_amplitude = 0;
    int16_t last_vibrato_cycles = 0;
    int16_t last_tremolo_cycles = 0;
    float last_sample = 0.0;
    int8_t last_effective_volume = 0;
    float dc_offset = 0.0;
    bool next_sample_may_be_discontinuous = false;

    void reset_division_scoped_effects();
    void start_note(int32_t instrument_num, int32_t period, int32_t volume);
    void set_discontinuous_flag();
    void decay_dc_offset(float delta);
  };

  struct SongPosition {
    size_t partition_count;
    size_t partition_index;
    ssize_t division_index = 0;
    ssize_t pattern_loop_start_index = 0;
    ssize_t pattern_loop_times_remaining = -1;
    bool jump_to_pattern_loop_start = false;
    size_t total_output_samples = 0;
    int32_t pattern_break_target = -1;
    int32_t partition_break_target = -1;
    std::vector<bool> partitions_executed;
    ssize_t divisions_to_delay = 0;

    SongPosition(size_t partition_count, size_t partition_index, size_t division_index);

    void advance_division();
  };

  phosg::PrefixedLogger log;
  std::shared_ptr<const Module> mod;
  std::shared_ptr<const Options> opts;
  size_t max_output_samples = 0;
  Timing timing;
  SongPosition pos;
  std::vector<TrackState> tracks;
  SampleCache<uint8_t> sample_cache;
  float dc_offset_decay = 0.001;

  [[nodiscard]] virtual bool on_tick_samples_ready(std::vector<float>&&) = 0;

  void show_current_division() const;

  inline const Module::Pattern& current_pattern() const {
    uint8_t pattern_index = this->mod->partition_table.at(this->pos.partition_index);
    return this->mod->patterns.at(pattern_index);
  }

  void execute_current_division_commands();
  static float get_vibrato_tremolo_wave_amplitude(float offset, uint8_t waveform);
  static uint16_t nearest_note_for_period(uint16_t period, bool snap_up);
  bool render_current_division_audio();

  inline bool exceeded_time_limit() const {
    return this->max_output_samples && (this->pos.total_output_samples > this->max_output_samples);
  }
};

class MODRenderer : public MODSynthesizer {
protected:
  std::deque<std::vector<float>> tick_samples;
  std::vector<float> all_tick_samples;

public:
  MODRenderer(std::shared_ptr<const Module> mod, std::shared_ptr<const Options> opts);
  virtual bool on_tick_samples_ready(std::vector<float>&& samples);
  const std::vector<float>& result();
};

} // namespace Audio
} // namespace ResourceDASM
