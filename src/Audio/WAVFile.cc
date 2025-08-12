#include "WAVFile.hh"

#include <string.h>

#include <format>
#include <phosg/Filesystem.hh>
#include <vector>

using namespace std;

namespace ResourceDASM {
namespace Audio {

SampledSound load_wav(FILE* f) {
  {
    phosg::be_uint32_t magic;
    phosg::freadx(f, &magic, sizeof(uint32_t));
    if (magic != 0x52494646) { // 'RIFF'
      throw runtime_error(format("unknown file format: {:08X}", magic.load()));
    }
  }

  phosg::le_uint32_t file_size;
  phosg::freadx(f, &file_size, sizeof(uint32_t));

  SampledSound contents;
  WAVEHeader wav;
  wav.wave_magic = 0;
  for (;;) {
    RIFFChunkHeader chunk_header;
    phosg::freadx(f, &chunk_header, sizeof(RIFFChunkHeader));

    if (chunk_header.magic == 0x45564157) { // 'WAVE'
      memcpy(&wav, &chunk_header, sizeof(RIFFChunkHeader));
      phosg::freadx(f, reinterpret_cast<uint8_t*>(&wav) + sizeof(RIFFChunkHeader), sizeof(WAVEHeader) - sizeof(RIFFChunkHeader));

      if (wav.wave_magic != 0x45564157) { // 'WAVE'
        throw runtime_error(format("sound has incorrect wave_magic ({:X})", wav.wave_magic.load()));
      }
      if (wav.fmt_magic != 0x20746D66) { // 'fmt '
        throw runtime_error(format("sound has incorrect fmt_magic ({:X})", wav.fmt_magic.load()));
      }
      // We only support mono and stereo files for now
      if (wav.num_channels > 2) {
        throw runtime_error(format("sound has too many channels ({})", wav.num_channels.load()));
      }

      contents.sample_rate = wav.sample_rate;
      contents.num_channels = wav.num_channels;

    } else if (chunk_header.magic == 0x6C706D73) { // 'smpl'
      if (wav.wave_magic == 0) {
        throw runtime_error("smpl chunk is before WAVE chunk");
      }

      const string data = phosg::freadx(f, chunk_header.size);
      const RIFFWAVESampleChunkHeader* sample_header = reinterpret_cast<const RIFFWAVESampleChunkHeader*>(data.data());
      const char* last_loop_ptr = data.data() + data.size() - sizeof(sample_header->loops[0]);

      contents.base_note = sample_header->base_note;
      contents.loops.resize(sample_header->num_loops);
      for (size_t x = 0; x < sample_header->num_loops; x++) {
        auto& contents_loop = contents.loops[x];
        auto* header_loop = &sample_header->loops[x];
        if (reinterpret_cast<const char*>(header_loop) > last_loop_ptr) {
          throw runtime_error("sound has malformed loop information");
        }
        // Convert the byte offsets to sample offsets
        contents_loop.start = header_loop->start / (wav.bits_per_sample >> 3);
        contents_loop.end = header_loop->end / (wav.bits_per_sample >> 3);
        contents_loop.type = header_loop->type;
      }
    } else if (chunk_header.magic == 0x61746164) { // 'data'
      if (wav.wave_magic == 0) {
        throw runtime_error("data chunk is before WAVE chunk");
      }

      contents.samples.resize((8 * chunk_header.size) / wav.bits_per_sample);

      // 32-bit float
      if ((wav.format == 3) && (wav.bits_per_sample == 32)) {
        phosg::freadx(f, contents.samples.data(), contents.samples.size() * sizeof(float));

        // 16-bit signed int
      } else if ((wav.format == 1) && (wav.bits_per_sample == 16)) {
        vector<int16_t> int_samples(contents.samples.size());
        phosg::freadx(f, int_samples.data(), int_samples.size() * sizeof(int16_t));
        for (size_t x = 0; x < int_samples.size(); x++) {
          if (int_samples[x] == -0x8000) {
            contents.samples[x] = -1.0f;
          } else {
            contents.samples[x] = static_cast<float>(int_samples[x]) / 32767.0f;
          }
        }

        // 8-bit unsigned int
      } else if ((wav.format == 1) && (wav.bits_per_sample == 8)) {
        vector<uint8_t> int_samples(contents.samples.size());
        phosg::freadx(f, int_samples.data(), int_samples.size() * sizeof(uint8_t));
        for (size_t x = 0; x < int_samples.size(); x++) {
          contents.samples[x] = (static_cast<float>(int_samples[x]) / 128.0f) - 1.0f;
        }
      } else {
        throw runtime_error(format(
            "sample width is not supported (format={}, bits_per_sample={})",
            wav.format.load(), wav.bits_per_sample.load()));
      }

      break;
    } else {
      fseek(f, chunk_header.size, SEEK_CUR);
    }
  }

  return contents;
}

void normalize_amplitude(vector<float>& data) {
  float max_amplitude = 0.0f;
  for (float sample : data) {
    if (sample > max_amplitude) {
      max_amplitude = sample;
    }
    if (sample < -max_amplitude) {
      max_amplitude = -sample;
    }
  }

  if (max_amplitude == 0.0f) {
    return;
  }
  for (float& sample : data) {
    sample /= max_amplitude;
  }
}

void trim_ending_silence(vector<float>& data) {
  size_t end_offset = data.size();
  for (; end_offset > 1; end_offset -= 2) {
    if (data[end_offset - 2] != 0.0 || data[end_offset - 1] != 0.0) {
      break;
    }
  }
  if (end_offset != data.size()) {
    data.resize(end_offset);
  }
}

} // namespace Audio
} // namespace ResourceDASM
