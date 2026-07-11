#include "WAVFile.hh"

#include <string.h>

#include <format>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <vector>

namespace ResourceDASM {
namespace Audio {

SampledSound load_wav(FILE* f) {
  {
    phosg::be_uint32_t magic;
    phosg::freadx(f, &magic, sizeof(uint32_t));
    if (magic != 0x52494646) { // 'RIFF'
      throw std::runtime_error(std::format("unknown file format: {:08X}", magic.load()));
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
        throw std::runtime_error(std::format("sound has incorrect wave_magic ({:X})", wav.wave_magic.load()));
      }
      if (wav.fmt_magic != 0x20746D66) { // 'fmt '
        throw std::runtime_error(std::format("sound has incorrect fmt_magic ({:X})", wav.fmt_magic.load()));
      }
      // We only support mono and stereo files for now
      if (wav.num_channels > 2) {
        throw std::runtime_error(std::format("sound has too many channels ({})", wav.num_channels.load()));
      }

      contents.sample_rate = wav.sample_rate;
      contents.num_channels = wav.num_channels;

    } else if (chunk_header.magic == 0x6C706D73) { // 'smpl'
      if (wav.wave_magic == 0) {
        throw std::runtime_error("smpl chunk is before WAVE chunk");
      }

      const std::string data = phosg::freadx(f, chunk_header.size);
      const RIFFWAVESampleChunkHeader* sample_header = reinterpret_cast<const RIFFWAVESampleChunkHeader*>(data.data());
      const char* last_loop_ptr = data.data() + data.size() - sizeof(sample_header->loops[0]);

      contents.base_note = sample_header->base_note;
      contents.loops.resize(sample_header->num_loops);
      for (size_t x = 0; x < sample_header->num_loops; x++) {
        auto& contents_loop = contents.loops[x];
        auto* header_loop = &sample_header->loops[x];
        if (reinterpret_cast<const char*>(header_loop) > last_loop_ptr) {
          throw std::runtime_error("sound has malformed loop information");
        }
        // Convert the byte offsets to sample offsets
        contents_loop.start = header_loop->start / (wav.bits_per_sample >> 3);
        contents_loop.end = header_loop->end / (wav.bits_per_sample >> 3);
        contents_loop.type = header_loop->type;
      }
    } else if (chunk_header.magic == 0x61746164) { // 'data'
      if (wav.wave_magic == 0) {
        throw std::runtime_error("data chunk is before WAVE chunk");
      }

      contents.samples.resize((8 * chunk_header.size) / wav.bits_per_sample);

      if ((wav.format == 3) && (wav.bits_per_sample == 32)) { // 32-bit float
        phosg::freadx(f, contents.samples.data(), contents.samples.size() * sizeof(float));

      } else if ((wav.format == 1) && (wav.bits_per_sample == 16)) { // 16-bit signed int
        std::vector<int16_t> int_samples(contents.samples.size());
        phosg::freadx(f, int_samples.data(), int_samples.size() * sizeof(int16_t));
        for (size_t x = 0; x < int_samples.size(); x++) {
          if (int_samples[x] == -0x8000) {
            contents.samples[x] = -1.0f;
          } else {
            contents.samples[x] = static_cast<float>(int_samples[x]) / 32767.0f;
          }
        }

      } else if ((wav.format == 1) && (wav.bits_per_sample == 8)) { // 8-bit unsigned int
        std::vector<uint8_t> int_samples(contents.samples.size());
        phosg::freadx(f, int_samples.data(), int_samples.size() * sizeof(uint8_t));
        for (size_t x = 0; x < int_samples.size(); x++) {
          contents.samples[x] = (static_cast<float>(int_samples[x]) / 128.0f) - 1.0f;
        }

      } else {
        throw std::runtime_error(std::format(
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

std::string serialize_wav(
    const std::vector<float>& samples,
    size_t sample_rate,
    size_t num_channels,
    size_t loop_start,
    size_t loop_end,
    size_t base_note) {

  struct HeaderBase {
    phosg::be_uint32_t riff_magic = 0x52494646; // 'RIFF'
    phosg::le_uint32_t file_size = 0; // RIFF chunk data size (file size - 8)
    phosg::be_uint32_t wave_magic = 0x57415645; // 'WAVE'

    phosg::be_uint32_t fmt_magic = 0x666d7420; // 'fmt '
    phosg::le_uint32_t fmt_size = 16;
    phosg::le_uint16_t format = 3; // 1 = PCM, 3 = float
    phosg::le_uint16_t num_channels = 0;
    phosg::le_uint32_t sample_rate = 0;
    phosg::le_uint32_t byte_rate = 0; // num_channels * sample_rate * bits_per_sample / 8
    phosg::le_uint16_t block_align = 0; // num_channels * bits_per_sample / 8
    phosg::le_uint16_t bits_per_sample = 0;
  } __attribute__((packed));

  struct SampleMetadataBlock {
    phosg::be_uint32_t smpl_magic = 0x736D706C; // 'smpl'
    phosg::le_uint32_t smpl_size = 0x3C;
    phosg::le_uint32_t manufacturer = 0;
    phosg::le_uint32_t product = 0;
    phosg::le_uint32_t sample_period = 0; // 1000000000 / sample_rate
    phosg::le_uint32_t base_note = 0x3C;
    phosg::le_uint32_t pitch_fraction = 0;
    phosg::le_uint32_t smpte_format = 0;
    phosg::le_uint32_t smpte_offset = 0;
    phosg::le_uint32_t num_loops = 1; // = 1
    phosg::le_uint32_t sampler_data = 0x18; // Size of loop struct (6 following fields)

    phosg::le_uint32_t loop_cue_point_id = 0; // Can be zero? We'll only have at most one loop in this context
    phosg::le_uint32_t loop_type = 0; // 0 = normal, 1 = ping-pong, 2 = reverse
    phosg::le_uint32_t loop_start = 0; // Start and end are byte offsets into the wave data, not sample indexes
    phosg::le_uint32_t loop_end = 0;
    phosg::le_uint32_t loop_fraction = 0; // Fraction of a sample to loop (0)
    phosg::le_uint32_t loop_play_count = 0; // 0 = loop forever
  } __attribute__((packed));

  struct DataHeader {
    phosg::be_uint32_t data_magic = 0x64617461; // 'data'
    phosg::le_uint32_t data_size = 0; // num_samples * num_channels * bits_per_sample / 8
  } __attribute__((packed));

  bool has_sample_metadata = ((loop_start > 0) && (loop_end > 0)) || (base_note != 0x3C) || (base_note != 0);

  constexpr size_t bits_per_sample = sizeof(samples[0]) * 8;

  phosg::StringWriter w;
  {
    HeaderBase header;
    header.file_size = ((samples.size() * num_channels * bits_per_sample) / 8) +
        (sizeof(HeaderBase) - 8) +
        sizeof(DataHeader) +
        (has_sample_metadata ? sizeof(SampleMetadataBlock) : 0);
    header.num_channels = num_channels;
    header.sample_rate = sample_rate;
    header.byte_rate = num_channels * sample_rate * bits_per_sample / 8;
    header.block_align = num_channels * bits_per_sample / 8;
    header.bits_per_sample = bits_per_sample;
    w.put(header);
  }

  if (has_sample_metadata) {
    SampleMetadataBlock meta;
    meta.sample_period = 1000000000 / sample_rate;
    meta.base_note = base_note;

    // Note: loop_start and loop_end are given to this function as sample offsets, but in the wav file, they should
    // be byte offsets
    meta.loop_start = loop_start * (bits_per_sample >> 3);
    meta.loop_end = loop_end * (bits_per_sample >> 3);
    w.put(meta);
  }

  {
    DataHeader data;
    data.data_size = samples.size() * num_channels * bits_per_sample / 8;
    w.put(data);
  }

  for (float sample : samples) {
    w.put_f32l(sample);
  }
  return std::move(w.str());
}

void normalize_amplitude(std::vector<float>& data) {
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

void trim_ending_silence(std::vector<float>& data) {
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
