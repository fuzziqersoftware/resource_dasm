#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <vector>

#include "AAFArchive.hh"
#include "Constants.hh"
#include "WAVFile.hh"

using namespace std;

using namespace ResourceDASM::Audio;

string base_filename_for_sound(const Sound& s) {
  return std::format("sample-{}-{:X}-{:08X}-{:08X}-{:08X}",
      s.source_filename, s.source_offset, s.sound_id, s.aw_file_index, s.wave_table_index);
}

int main(int argc, char** argv) {
  if (argc != 3) {
    phosg::fwrite_fmt(stderr, "usage: smsdumpbanks bank_directory output_directory\n");
    return 1;
  }

  auto env = load_sound_environment(argv[1]);

  // generate text file
  for (const auto& ibank_it : env.instrument_banks) {
    const auto& ibank = ibank_it.second;

    string filename = std::format("{}/bank-{}.txt", argv[2], ibank_it.first);
    auto f = phosg::fopen_unique(filename, "wt");

    for (const auto& inst_it : ibank.id_to_instrument) {
      phosg::fwrite_fmt(f.get(), "instrument {} (0x{:X}):\n", inst_it.first, inst_it.first);
      for (const auto& key_region : inst_it.second.key_regions) {
        string key_low_str = name_for_note(key_region.key_low);
        string key_high_str = name_for_note(key_region.key_high);
        phosg::fwrite_fmt(f.get(), "  key region [{},{}] / [0x{:02X},0x{:02X}] / [{},{}]:\n",
            key_region.key_low, key_region.key_high, key_region.key_low,
            key_region.key_high, key_low_str, key_high_str);
        for (const auto& vel_region : key_region.vel_regions) {
          if (vel_region.sound) {
            phosg::fwrite_fmt(f.get(), "    velocity region [{},{}] / [0x{:02X},0x{:02X}]: sound id 0x{:X}, frequency multiplier {:g}, base note {:02X}, sound base note {:02X}\n",
                vel_region.vel_low, vel_region.vel_high, vel_region.vel_low,
                vel_region.vel_high, vel_region.sound_id, vel_region.freq_mult,
                vel_region.base_note, vel_region.sound->base_note);
          } else {
            phosg::fwrite_fmt(f.get(), "    velocity region [{},{}] / [0x{:02X},0x{:02X}]: sound id 0x{:X}, frequency multiplier {:g}, base note {:02X}, sound base note missing\n",
                vel_region.vel_low, vel_region.vel_high, vel_region.vel_low,
                vel_region.vel_high, vel_region.sound_id, vel_region.freq_mult,
                vel_region.base_note);
          }
        }
      }
    }
  }

  // generate soundfont text file
  {
    string filename = std::format("{}/metadata-sf.txt", argv[2]);
    auto f = phosg::fopen_unique(filename, "wt");

    map<string, bool> filenames;

    phosg::fwrite_fmt(f.get(), "[Samples]\n\n");
    for (const auto& bank_it : env.sample_banks) {
      for (const Sound& s : bank_it.second) {
        string sound_basename = base_filename_for_sound(s);
        phosg::fwrite_fmt(f.get(), "\
    SampleName={}.wav\n\
        SampleRate={}\n\
        Key={}\n\
        FineTune=0\n\
        Type=1\n\n",
            sound_basename, s.sample_rate, s.base_note);
        filenames.emplace(sound_basename, false);
      }
    }

    phosg::fwrite_fmt(f.get(), "[Instruments]\n\n");
    for (const auto& ibank_it : env.instrument_banks) {
      const auto& ibank = ibank_it.second;
      for (const auto& inst_it : ibank.id_to_instrument) {
        string instrument_name = std::format("inst_{:08X}_{:08X}", ibank.id, inst_it.first);
        phosg::fwrite_fmt(f.get(), "    InstrumentName={}\n\n", instrument_name);
        for (const auto& key_region : inst_it.second.key_regions) {
          for (const auto& vel_region : key_region.vel_regions) {
            if (!vel_region.sound) {
              phosg::fwrite_fmt(stderr, "warning: sound missing for instrument={:08X}:{:08X} key=[{},{}] vel=[{},{}]: sound id 0x{:X}, frequency multiplier {:g}, base note {:02X}\n",
                  ibank.id, inst_it.first, key_region.key_low, key_region.key_high,
                  vel_region.vel_low, vel_region.vel_high, vel_region.sound_id,
                  vel_region.freq_mult, vel_region.base_note);
            } else {
              string basename = base_filename_for_sound(*vel_region.sound);
              uint8_t base_note = vel_region.base_note ? vel_region.base_note : vel_region.sound->base_note;
              phosg::fwrite_fmt(f.get(), "\
        Sample={}\n\
            Z_LowKey={}\n\
            Z_HighKey={}\n\
            Z_LowVelocity={}\n\
            Z_HighVelocity={}\n\
            Z_sampleModes=1\n\
            Z_overridingRootKey={}\n\
            Z_Modulator=(NoteOnVelocity,ReverseDirection,Unipolar,Linear), initialFilterFc, 0, (NoteOnVelocity,ReverseDirection,Unipolar,Switch), 0\n\n",
                  basename, key_region.key_low, key_region.key_high,
                  vel_region.vel_low, vel_region.vel_high, base_note);
              filenames[basename] = true;
            }
          }
        }
      }
    }

    phosg::fwrite_fmt(f.get(), "[Presets]\n\n");
    for (const auto& ibank_it : env.instrument_banks) {
      const auto& ibank = ibank_it.second;
      for (const auto& inst_it : ibank.id_to_instrument) {
        string instrument_name = std::format("inst_{:08X}_{:08X}", ibank.id, inst_it.first);
        phosg::fwrite_fmt(f.get(), "\
    PresetName=preset_{}\n\
        Bank={}\n\
        Program={}\n\
\n\
        Instrument={}\n\
            L_LowKey=0\n\
            L_HighKey=127\n\
            L_LowVelocity=0\n\
            L_HighVelocity=127\n\n",
            instrument_name, ibank.id, inst_it.first, instrument_name);
      }
    }

    phosg::fwrite_fmt(f.get(), "\
[Info]\n\
Version=2.1\n\
Engine=\n\
Name=\n\
ROMName=\n\
ROMVersion=\n\
Date=\n\
Designer=\n\
Product=\n\
Copyright=\n\
Editor=\n\
Comments=\n");

    size_t num_unused = 0;
    for (const auto& it : filenames) {
      phosg::fwrite_fmt(stderr, "[check] {} {}.wav\n", it.second ? "used" : "UNUSED", it.first);
      if (!it.second) {
        num_unused++;
      }
    }
    phosg::fwrite_fmt(stderr, "[check] {}/{} unused\n", num_unused, filenames.size());
  }

  // export samples
  for (const auto& wsys_it : env.sample_banks) {
    for (const auto& s : wsys_it.second) {
      auto samples = s.samples();
      if (samples.empty()) {
        phosg::fwrite_fmt(stderr, "warning: can\'t decode {}:{:X}:{:X}\n", s.source_filename, s.source_offset, s.source_size);
        continue;
      }
      string basename = base_filename_for_sound(s);
      string filename = std::format("{}/{}.wav", argv[2], basename);
      save_wav(filename, samples, s.sample_rate, s.num_channels);
    }
  }

  // export sequences
  for (const auto& s : env.sequence_programs) {
    string fn = std::format("{}/sequence-{}-{}.bms", argv[2], s.second.index, s.first);
    phosg::save_file(fn, s.second.data);
  }

  return 0;
}
