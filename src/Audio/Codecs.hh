#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <phosg/Encoding.hh>
#include <vector>

namespace ResourceDASM {
namespace Audio {

template <typename SampleT>
float sample_to_float(SampleT) {
  static_assert(phosg::always_false<SampleT>::v, "Unspecialized sample_to_float cannot be used");
  return 0.0;
}
template <>
inline float sample_to_float<uint8_t>(uint8_t sample) {
  return (static_cast<float>(sample) - 0x80) / 0x80;
}
template <>
inline float sample_to_float<int8_t>(int8_t sample) {
  return static_cast<float>(sample) / 0x80;
}
template <>
inline float sample_to_float<int16_t>(int16_t sample) {
  return static_cast<float>(sample) / 0x8000;
}
template <>
inline float sample_to_float<phosg::le_int16_t>(phosg::le_int16_t sample) {
  return static_cast<float>(sample) / 0x8000;
}
template <>
inline float sample_to_float<phosg::be_int16_t>(phosg::be_int16_t sample) {
  return static_cast<float>(sample) / 0x8000;
}
template <>
inline float sample_to_float<float>(float sample) {
  return sample;
}

template <typename SampleT>
SampleT sample_from_float(float) {
  static_assert(phosg::always_false<SampleT>::v, "Unspecialized sample_from_float cannot be used");
}
template <>
inline uint8_t sample_from_float<uint8_t>(float sample) {
  return std::clamp<int64_t>((sample * 0x80) + 0x80, 0x00, 0xFF);
}
template <>
inline int8_t sample_from_float<int8_t>(float sample) {
  return std::clamp<int64_t>((sample * 0x80), -0x80, 0x7F);
}
template <>
inline int16_t sample_from_float<int16_t>(float sample) {
  return std::clamp<int64_t>(sample * 0x8000, -0x8000, 0x7FFF);
}
template <>
inline phosg::le_int16_t sample_from_float<phosg::le_int16_t>(float sample) {
  return std::clamp<int64_t>(sample * 0x8000, -0x8000, 0x7FFF);
}
template <>
inline phosg::be_int16_t sample_from_float<phosg::be_int16_t>(float sample) {
  return std::clamp<int64_t>(sample * 0x8000, -0x8000, 0x7FFF);
}
template <>
inline float sample_from_float<float>(float sample) {
  return sample;
}

template <typename ToT, typename FromT>
std::vector<ToT> convert_samples(const FromT* samples, size_t count) {
  std::vector<ToT> ret;
  ret.reserve(count);
  for (size_t z = 0; z < count; z++) {
    ret.emplace_back(sample_from_float<ToT>(sample_to_float<FromT>(samples[z])));
  }
  return ret;
}
template <typename ToT, typename FromT>
std::vector<ToT> convert_samples(const std::vector<FromT>& samples) {
  return convert_samples<ToT, FromT>(samples.data(), samples.size());
}
template <typename ToT, typename FromT>
std::vector<ToT> convert_samples(const std::string& data) {
  return convert_samples<ToT, FromT>(reinterpret_cast<const FromT*>(data.data()), data.size() / sizeof(FromT));
}

std::vector<float> convert_samples_dynamic(const void* data, size_t size, size_t bits_per_sample);

inline std::vector<float> convert_samples_dynamic(const std::string& data, size_t bits_per_sample) {
  return convert_samples_dynamic(data.data(), data.size(), bits_per_sample);
}

std::vector<float> decode_mace(const void* data, size_t size, bool stereo, bool is_mace3);
std::vector<float> decode_ima4(const void* data, size_t size, bool stereo);
std::vector<float> decode_alaw(const void* data, size_t size);
std::vector<float> decode_ulaw(const void* data, size_t size);
std::vector<float> decode_afc(const void* data, size_t size, bool small_frames);

} // namespace Audio
} // namespace ResourceDASM
