#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <array>
#include <deque>
#include <filesystem>
#include <map>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <phosg/Time.hh>
#include <string>

#include "MODSynthesizer.hh"
#include "WAVFile.hh"
#ifdef SDL3_AVAILABLE
#include "SDLAudioStream.hh"
#endif

using namespace std;
using namespace ResourceDASM::Audio;

class MODWriter : public MODSynthesizer {
protected:
  FILE* f;

public:
  MODWriter(shared_ptr<const Module> mod, shared_ptr<const Options> opts, FILE* f)
      : MODSynthesizer(mod, opts),
        f(f) {}

  virtual bool on_tick_samples_ready(vector<float>&& samples) {
    fwrite(samples.data(), sizeof(samples[0]), samples.size(), this->f);
    fflush(this->f);
    return true;
  }
};

#ifdef SDL3_AVAILABLE
class SDLMODPlayer : public MODSynthesizer {
protected:
  std::shared_ptr<SDLAudioStream> stream;

public:
  SDLMODPlayer(shared_ptr<const Module> mod, shared_ptr<const Options> opts)
      : MODSynthesizer(mod, opts), stream(make_shared<SDLAudioStream>(2, opts->sample_rate)) {}

  virtual bool on_tick_samples_ready(vector<float>&& samples) {
    this->stream->wait_until_remaining_secs(0.1);
    this->stream->add(samples);
    return true;
  }

  void drain() {
    this->stream->drain();
  }
};
#endif

void print_usage() {
  phosg::fwrite_fmt(stderr, "\
\n\
modsynth - a synthesizer for Protracker/Soundtracker modules\n\
\n\
Usage: modsynth <mode> [options] <input_filename>\n\
\n\
The --disassemble mode generates a human-readable representation of the\n\
instruments and sequence program from the module.\n\
\n\
The --disassemble-directory mode is like --disassemble, but operates on all\n\
files in the given directory. The options are the same as for --disassemble.\n\
\n\
The --export-instruments mode exports the instruments from the module. Each\n\
instrument has at most one sample. Each sample is saved as\n\
<input_filename>_<instrument_number>.wav. Samples are converted to 32-bit\n\
floating-point format during export. This mode has no other options.\n\
\n\
The --render mode generates a rasterized version of the sequence and saves the\n\
result as <input_filename>.wav.\n\
\n\
The --play mode plays the sequence through the default audio device. This is\n\
only available if modsynth is ubilt with SDL3.\n\
\n\
Options for --render and --play:\n\
  --sample-rate=N\n\
      Output audio at this sample rate (default 48000). The sample format is\n\
      always 32-bit float.\n\
  --resample-method=METHOD\n\
      Use this method for resampling instruments. Values are sinc-best,\n\
      sinc-medium, sinc-fast, hold, and linear. The default is hold, which most\n\
      closely approximates what happens on old systems when they play these\n\
      kinds of modules.\n\
  --volume=N\n\
      Set global volume to N (-1.0-1.0). With --render this doesn\'t really\n\
      matter unless --skip-normalize is also used, but with --play it overrides\n\
      the default behavior of using (2.0 / num_tracks), which corrects for\n\
      potentially very loud output for MODs with high track counts. Negative\n\
      volumes simply invert the output waveform; it will sound the same as a\n\
      positive volume but can be used for some advanced effects.\n\
  --default-panning-split=N\n\
      Set default panning split to N. Ranges from -64 (tracks 0 and 3 on the\n\
      right, 1 and 2 on the left) to +64 (the opposite). The default is +32.\n\
  --default-panning-split=surround\n\
      Use the inverse-wave surround effect instead of a panning split.\n\
  --time-limit=N\n\
      Stop generating audio after this many seconds have been generated\n\
      (unlimited by default).\n\
  --skip-partitions=N\n\
      Start at this offset in the partition table instead of at the beginning.\n\
  --allow-backward-position-jump\n\
      Allow position jump effects (Bxx) to jump to parts of the song that have\n\
      already been played. These generally result in infinite loops and are\n\
      disallowed by default.\n\
  --aggressive-tick-correction\n\
      Apply DC offsets on all volume changes, not just those that occur as a\n\
      result of a Cxx effect. This makes some songs sound better but others\n\
      sound worse.\n\
  --volume-exponent=EXP\n\
      Set the volume scaling exponent for the Cxx effect (default 0.65). The\n\
      effect of this inversely correlates with the value; that is, a smaller\n\
      value for EXP means that Cxx effects less than C40 will be louder.\n\
  --solo-track=N\n\
      Mute all the tracks except this one. The first track is numbered 0; most\n\
      MODs have tracks 0-3. May be given multiple times.\n\
  --mute-track=N\n\
      Mute this track. May be given multiple times.\n\
  --solo-instrument=N\n\
      Mute all the instruments except this one. The first instrument is\n\
      numbered 0. May be given multiple times.\n\
  --mute-instrument=N\n\
      Mute this instrument. May be given multiple times.\n\
  --tempo-bias=N\n\
      Speed up or slow down the song by this factor without changing pitch\n\
      (default 1.0). For example, 2.0 plays the song twice as fast; 0.5 plays\n\
      the song at half speed.\n\
  --pal-amiga\n\
      Use a slightly lower hardware frequency when computing note pitches,\n\
      which matches Amiga machines sold in Europe. The default is to use the\n\
      North American machines' frequency. (The difference is essentially\n\
      imperceptible.)\n\
  --arpeggio-frequency=N\n\
      Use a fixed arpeggio frequency instead of the default behavior, which is\n\
      to align arpeggio boundaries to ticks.\n\
  --vibrato-resolution=N\n\
      Evaluate vibrato effects this many times each tick (default 1).\n\
\n\
Options for --render only:\n\
  --skip-trim-silence\n\
      By default, modsynth will delete contiguous silence at the end of the\n\
      generated audio. This option skips that step.\n\
  --skip-normalize\n\
      By default, modsynth will normalize the output so the maximum sample\n\
      amplitude is 1.0 or -1.0. This option skips that step, so the output may\n\
      contain samples with higher amplitudes.\n\
  --write-stdout\n\
      Instead of saving to a file, write raw float32 data to stdout, which can\n\
      be piped to audiocat --play --format=stereo-f32. Generally only useful\n\
      for debugging problems with --render that don\'t occur when using --play.\n\
\n\
Options for all usage modes:\n\
  --color/--no-color\n\
      Enables or disables the generation of color escape codes for visualizing\n\
      pattern and instrument data. By default, color escapes are generated only\n\
      if the output is to a terminal.\n\
\n");
}

int main(int argc, char** argv) {
  enum class Behavior {
    DISASSEMBLE,
    DISASSEMBLE_DIRECTORY,
    EXPORT_INSTRUMENTS,
    RENDER,
    PLAY,
  };

  Behavior behavior = Behavior::DISASSEMBLE;
  const char* input_filename = nullptr;
  bool write_stdout = false;
  bool use_default_global_volume = true;
  bool trim_ending_silence_after_render = true;
  bool normalize_after_render = true;
  shared_ptr<MODSynthesizer::Options> opts(new MODSynthesizer::Options());
  opts->print_status_while_playing = true;
  for (int x = 1; x < argc; x++) {
    if (!strcmp(argv[x], "--disassemble")) {
      behavior = Behavior::DISASSEMBLE;
    } else if (!strcmp(argv[x], "--disassemble-directory")) {
      behavior = Behavior::DISASSEMBLE_DIRECTORY;
    } else if (!strcmp(argv[x], "--export-instruments")) {
      behavior = Behavior::EXPORT_INSTRUMENTS;
    } else if (!strcmp(argv[x], "--render")) {
      behavior = Behavior::RENDER;
    } else if (!strcmp(argv[x], "--play")) {
      behavior = Behavior::PLAY;

    } else if (!strcmp(argv[x], "--resample-method=hold")) {
      opts->resample_method = ResampleMethod::EXTEND;
    } else if (!strcmp(argv[x], "--resample-method=linear")) {
      opts->resample_method = ResampleMethod::LINEAR_INTERPOLATE;

    } else if (!strcmp(argv[x], "--write-stdout")) {
      write_stdout = true;
    } else if (!strcmp(argv[x], "--debug")) {
      opts->print_track_debug_while_playing = true;

    } else if (!strncmp(argv[x], "--solo-track=", 13)) {
      opts->solo_tracks.emplace(atoi(&argv[x][13]));
    } else if (!strncmp(argv[x], "--mute-track=", 13)) {
      opts->mute_tracks.emplace(atoi(&argv[x][13]));
    } else if (!strncmp(argv[x], "--solo-instrument=", 18)) {
      opts->solo_instruments.emplace(atoi(&argv[x][18]));
    } else if (!strncmp(argv[x], "--mute-instrument=", 18)) {
      opts->mute_instruments.emplace(atoi(&argv[x][18]));

    } else if (!strcmp(argv[x], "--pal-amiga")) {
      opts->amiga_hardware_frequency = 7093789.2;
    } else if (!strncmp(argv[x], "--tempo-bias=", 13)) {
      opts->tempo_bias = atof(&argv[x][13]);
    } else if (!strcmp(argv[x], "--default-panning-split=surround")) {
      opts->default_enable_surround = true;
    } else if (!strncmp(argv[x], "--default-panning-split=", 24)) {
      opts->default_panning_split = stoull(&argv[x][24], nullptr, 0);
      if (opts->default_panning_split < -0x40) {
        opts->default_panning_split = -0x40;
      } else if (opts->default_panning_split > 0x40) {
        opts->default_panning_split = 0x40;
      }
    } else if (!strncmp(argv[x], "--volume=", 9)) {
      use_default_global_volume = false;
      opts->global_volume = atof(&argv[x][9]);
      if (opts->global_volume > 1.0) {
        opts->global_volume = 1.0;
      } else if (opts->global_volume < -1.0) {
        opts->global_volume = -1.0;
      }
    } else if (!strncmp(argv[x], "--time-limit=", 13)) {
      opts->max_output_seconds = atof(&argv[x][13]);

    } else if (!strcmp(argv[x], "--skip-trim-silence")) {
      trim_ending_silence_after_render = false;
    } else if (!strcmp(argv[x], "--skip-normalize")) {
      normalize_after_render = false;

    } else if (!strncmp(argv[x], "--arpeggio-frequency=", 21)) {
      opts->arpeggio_frequency = atoi(&argv[x][21]);
    } else if (!strncmp(argv[x], "--vibrato-resolution=", 21)) {
      opts->vibrato_resolution = atoi(&argv[x][21]);

    } else if (!strncmp(argv[x], "--skip-partitions=", 18)) {
      opts->skip_partitions = atoi(&argv[x][18]);
    } else if (!strncmp(argv[x], "--skip-divisions=", 17)) {
      opts->skip_divisions = atoi(&argv[x][17]);
    } else if (!strcmp(argv[x], "--allow-backward-position-jump")) {
      opts->allow_backward_position_jump = true;
    } else if (!strcmp(argv[x], "--aggressive-tick-correction")) {
      opts->correct_ticks_on_all_volume_changes = true;
    } else if (!strncmp(argv[x], "--volume-exponent=", 18)) {
      opts->volume_exponent = strtof(&argv[x][18], nullptr);
    } else if (!strncmp(argv[x], "--sample-rate=", 14)) {
      opts->sample_rate = atoi(&argv[x][14]);

    } else if (!input_filename) {
      input_filename = argv[x];

    } else {
      phosg::fwrite_fmt(stderr, "error: multiple filenames given, or unknown option: {}\n",
          argv[x]);
      print_usage();
      return 1;
    }
  }
  if (!input_filename) {
    phosg::fwrite_fmt(stderr, "error: no input filename given\n");
    print_usage();
    return 1;
  }

  bool behavior_is_disassemble = ((behavior == Behavior::DISASSEMBLE) || (behavior == Behavior::DISASSEMBLE_DIRECTORY));
  opts->use_color = (isatty(fileno(behavior_is_disassemble ? stdout : stderr)));

  shared_ptr<Module> mod;
  if (behavior != Behavior::DISASSEMBLE_DIRECTORY) {
    mod = Module::parse(phosg::load_file(input_filename));
  }

  // Since we don't clip float32 samples and just play them directly, we could
  // end up generating very loud output. With --render this is fine, since we
  // normalize the output before saving it, but with --play we can't make a
  // second pass back over the data... so we set the global volume appropriately
  // based on the number of tracks, which essentially limits the output range to
  // [-1.0, 1.0].
  if (use_default_global_volume) {
    if (behavior == Behavior::PLAY) {
      opts->global_volume = 2.0 / mod->num_tracks;
      phosg::fwrite_fmt(stderr, "Setting global volume to {:g} to account for {} tracks\n", opts->global_volume, mod->num_tracks);
    } else {
      opts->global_volume = 1.0;
    }
  }

  switch (behavior) {
    case Behavior::DISASSEMBLE:
      // We don't call print_mod_text in this case because all the text is
      // contained in the disassembly
      mod->disassemble(stdout, opts->use_color);
      break;
    case Behavior::DISASSEMBLE_DIRECTORY: {
      for (const auto& entry : std::filesystem::directory_iterator(input_filename)) {
        string path = string(input_filename) + "/" + entry.path().filename().string();
        phosg::fwrite_fmt(stdout, "===== {}\n", path);

        try {
          Module::parse(phosg::load_file(path))->disassemble(stdout, opts->use_color);
          fputc('\n', stdout);
        } catch (const exception& e) {
          phosg::fwrite_fmt(stdout, "Failed: {}\n\n", e.what());
        }
        phosg::fwrite_fmt(stderr, "... {}\n", path);
      }
      break;
    }
    case Behavior::EXPORT_INSTRUMENTS:
      mod->export_instruments(input_filename);
      break;
    case Behavior::RENDER: {
      mod->print_text(stderr);
      if (write_stdout) {
        MODWriter writer(mod, opts, stdout);
        writer.run_all();
      } else {
        string output_filename = string(input_filename) + ".wav";
        MODRenderer renderer(mod, opts);
        phosg::fwrite_fmt(stderr, "Synthesis:\n");
        renderer.run_all();
        phosg::fwrite_fmt(stderr, "Assembling result\n");
        auto result = renderer.result();
        if (trim_ending_silence_after_render) {
          trim_ending_silence(result);
        }
        if (normalize_after_render) {
          normalize_amplitude(result);
        }
        phosg::fwrite_fmt(stderr, "... {}\n", output_filename);
        save_wav(output_filename, result, opts->sample_rate, 2);
      }
      break;
    }
    case Behavior::PLAY: {
#ifdef SDL3_AVAILABLE
      mod->print_text(stderr);
      SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
      SDL_Init(SDL_INIT_AUDIO);
      {
        SDLMODPlayer player(mod, opts);
        phosg::fwrite_fmt(stderr, "Synthesis:\n");
        player.run_all();
        player.drain();
      }
      SDL_Quit();
      break;
#else
      throw std::runtime_error("modsynth was not built with SDL support; cannot play audio directly");
#endif
    }
    default:
      throw logic_error("invalid behavior");
  }

  return 0;
}
