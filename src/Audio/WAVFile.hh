#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <vector>

namespace ResourceDASM {
namespace Audio {

struct RIFFHeader {
  phosg::le_uint32_t riff_magic; // 0x52494646 ('RIFF')
  phosg::le_uint32_t file_size; // size of file - 8
} __attribute__((packed));

struct WAVEHeader {
  phosg::le_uint32_t wave_magic; // 0x57415645 ('WAVE')
  phosg::le_uint32_t fmt_magic; // 0x666d7420 ('fmt ')
  phosg::le_uint32_t fmt_size; // 16
  phosg::le_uint16_t format; // 1 = PCM, 3 = float
  phosg::le_uint16_t num_channels;
  phosg::le_uint32_t sample_rate;
  phosg::le_uint32_t byte_rate; // num_channels * sample_rate * bits_per_sample / 8
  phosg::le_uint16_t block_align; // num_channels * bits_per_sample / 8
  phosg::le_uint16_t bits_per_sample;
} __attribute__((packed));

struct RIFFChunkHeader {
  phosg::le_uint32_t magic;
  phosg::le_uint32_t size;
} __attribute__((packed));

struct RIFFWAVESampleChunkHeader {
  phosg::le_uint32_t manufacturer;
  phosg::le_uint32_t product;
  phosg::le_uint32_t sample_period;
  phosg::le_uint32_t base_note;
  phosg::le_uint32_t pitch_fraction;
  phosg::le_uint32_t smpte_format;
  phosg::le_uint32_t smpte_offset;
  phosg::le_uint32_t num_loops;
  phosg::le_uint32_t sampler_data;

  struct Loop {
    phosg::le_uint32_t cue_point_id;
    phosg::le_uint32_t type; // 0 = normal, 1 = ping-pong, 2 = reverse
    phosg::le_uint32_t start; // byte offset into the wave data
    phosg::le_uint32_t end; // byte offset into the wave data
    phosg::le_uint32_t fraction; // fraction of a sample to loop
    phosg::le_uint32_t play_count; // 0 = loop forever
  } __attribute__((packed));
  Loop loops[0];
} __attribute__((packed));

struct SampledSound {
  struct Loop {
    size_t start;
    size_t end;
    uint8_t type;
  };

  std::vector<float> samples;
  size_t num_channels = 0;
  size_t sample_rate = 0;
  int64_t base_note = -1; // -1 if not specified
  std::vector<Loop> loops;

  inline float seconds() const {
    return static_cast<float>(this->samples.size()) / this->sample_rate;
  }
};

SampledSound load_wav(FILE* f);

struct SaveWAVHeader {
  uint32_t riff_magic = phosg::bswap32(0x52494646); // 'RIFF'
  uint32_t file_size = 0; // RIFF chunk data size (file size - 8)
  uint32_t wave_magic = phosg::bswap32(0x57415645); // 'WAVE'

  uint32_t fmt_magic = phosg::bswap32(0x666d7420); // 'fmt '
  uint32_t fmt_size = 16;
  uint16_t format = 0; // 1 = PCM, 3 = float
  uint16_t num_channels = 0;
  uint32_t sample_rate = 0;
  uint32_t byte_rate = 0; // num_channels * sample_rate * bits_per_sample / 8
  uint16_t block_align = 0; // num_channels * bits_per_sample / 8
  uint16_t bits_per_sample = 0;

  uint32_t data_magic = phosg::bswap32(0x64617461); // 'data'
  uint32_t data_size; // num_samples * num_channels * bits_per_sample / 8
};

template <typename SampleT>
void save_wav(const std::string& filename, const std::vector<SampleT>& samples, size_t sample_rate, size_t num_channels) {
  SaveWAVHeader header;
  header.file_size = ((samples.size() * num_channels * sizeof(SampleT))) + sizeof(SaveWAVHeader) - 8;
  header.format = std::is_floating_point_v<SampleT> ? 3 : 1;
  header.num_channels = num_channels;
  header.sample_rate = sample_rate;
  header.byte_rate = num_channels * sample_rate * sizeof(SampleT);
  header.block_align = num_channels * sizeof(SampleT);
  header.bits_per_sample = sizeof(SampleT) << 3;
  header.data_size = samples.size() * num_channels * sizeof(SampleT);

  auto f = phosg::fopen_unique(filename, "wb");
  phosg::fwritex(f.get(), &header, sizeof(SaveWAVHeader));
  phosg::fwritex(f.get(), samples.data(), sizeof(SampleT) * samples.size());
}

void normalize_amplitude(std::vector<float>& data);
void trim_ending_silence(std::vector<float>& data);

} // namespace Audio
} // namespace ResourceDASM
