#pragma once

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace ResourceDASM {
namespace Audio {

template <typename SampleT>
  requires(std::is_same_v<SampleT, uint8_t>)
float sample_to_float(SampleT sample) {
  return (static_cast<float>(sample) - 0x80) / 0x80;
}
template <typename SampleT>
  requires(std::is_same_v<SampleT, int8_t>)
float sample_to_float(SampleT sample) {
  return static_cast<float>(sample) / 0x80;
}
template <typename SampleT>
  requires(std::is_same_v<SampleT, int16_t>)
float sample_to_float(SampleT sample) {
  return static_cast<float>(sample) / 0x8000;
}
template <typename SampleT>
  requires(std::is_same_v<SampleT, float>)
float sample_to_float(SampleT sample) {
  return sample;
}

template <typename SampleT>
  requires(std::is_same_v<SampleT, uint8_t>)
SampleT sample_from_float(float sample) {
  return std::clamp<int64_t>((sample * 0x80) - 0x80, 0x00, 0xFF);
}
template <typename SampleT>
  requires(std::is_same_v<SampleT, int8_t>)
SampleT sample_from_float(float sample) {
  return std::clamp<int64_t>((sample * 0x80), -0x80, 0x7F);
}
template <typename SampleT>
  requires(std::is_same_v<SampleT, int16_t>)
SampleT sample_from_float(float sample) {
  return std::clamp<int64_t>(sample * 0x8000, -0x8000, 0x7FFF);
}
template <typename SampleT>
  requires(std::is_same_v<SampleT, float>)
SampleT sample_from_float(float sample) {
  return sample;
}

template <typename ToT, typename FromT>
std::vector<ToT> convert_samples(const std::vector<FromT>& samples) {
  std::vector<ToT> ret;
  ret.reserve(samples.size());
  for (const auto& sample : samples) {
    ret.emplace_back(sample_from_float<ToT>(sample_to_float<FromT>(sample)));
  }
  return ret;
}

enum class ResampleMethod {
  EXTEND = 0,
  LINEAR_INTERPOLATE,
};

template <typename SampleT, ResampleMethod Method>
std::vector<SampleT> resample_audio(const std::vector<SampleT>& input_samples, size_t num_channels, double ratio) {
  size_t num_frames = input_samples.size() / num_channels;

  std::vector<float> prev_samples(num_channels, 0.0);
  std::vector<float> current_samples(num_channels, 0.0);
  for (size_t z = 0; z < num_channels; z++) {
    prev_samples[z] = sample_to_float<SampleT>(input_samples[z]);
  }
  // in_frame_index starts at 1 because frame 0 is already in prev_samples
  size_t in_frame_index = 1;

  std::vector<SampleT> ret;
  ret.reserve(input_samples.size() * ratio);

  auto write_current_output_frames = [&]() -> void {
    size_t frames_to_write =
        static_cast<size_t>(ceil((in_frame_index + 1) * ratio)) -
        static_cast<size_t>(ceil(in_frame_index * ratio));
    for (size_t frame_index = 0; frame_index < frames_to_write; frame_index++) {
      if constexpr (Method == ResampleMethod::EXTEND) {
        // Just use the previous sample for the entire timestep
        for (size_t sample_index = 0; sample_index < num_channels; sample_index++) {
          ret.emplace_back(prev_samples[sample_index]);
        }
      } else if constexpr (Method == ResampleMethod::LINEAR_INTERPOLATE) {
        // Linearly interpolate this output sample between the previous and next input samples
        float progress_factor = static_cast<float>(frame_index) / frames_to_write;
        for (size_t sample_index = 0; sample_index < num_channels; sample_index++) {
          ret.emplace_back(prev_samples[sample_index] * (1.0 - progress_factor) + current_samples[sample_index] * progress_factor);
        }
      } else {
        static_assert(phosg::always_false<SampleT>::value, "Invalid resampling method");
      }
    }
  };

  for (; in_frame_index < num_frames; in_frame_index++) {
    for (size_t z = 0; z < num_channels; z++) {
      current_samples[z] = sample_to_float<SampleT>(input_samples[in_frame_index + z]);
    }
    write_current_output_frames();
    prev_samples = current_samples;
  }
  write_current_output_frames(); // Ensure the last sample is represented in the output
  return ret;
}

template <typename SampleT>
std::vector<SampleT> resample_audio(
    const std::vector<SampleT>& input_samples, size_t num_channels, double ratio, ResampleMethod method) {
  switch (method) {
    case ResampleMethod::EXTEND:
      return resample_audio<SampleT, ResampleMethod::EXTEND>(input_samples, num_channels, ratio);
    case ResampleMethod::LINEAR_INTERPOLATE:
      return resample_audio<SampleT, ResampleMethod::LINEAR_INTERPOLATE>(input_samples, num_channels, ratio);
    default:
      throw std::logic_error("Invalid resampling method");
  }
}

template <typename KeyT>
class SampleCache {
public:
  explicit SampleCache(ResampleMethod method) : method(method) {}
  ~SampleCache() = default;

  const std::vector<float>& at(const KeyT& k, float ratio) const {
    return this->cache.at(k).at(ratio);
  }

  const std::vector<float>& add(const KeyT& k, float ratio, std::vector<float>&& data) {
    return this->cache[k].emplace(ratio, std::move(data)).first->second;
  }

  const std::vector<float>& resample_add(
      const KeyT& k, const std::vector<float>& input_samples, size_t num_channels, float ratio) {
    try {
      return this->at(k, ratio);
    } catch (const std::out_of_range&) {
      auto data = resample_audio<float>(input_samples, num_channels, ratio, this->method);
      return this->add(k, ratio, std::move(data));
    }
  }

  std::vector<float> resample(const std::vector<float>& input_samples, size_t num_channels, double src_ratio) const {
    return resample_audio<float>(input_samples, num_channels, src_ratio, this->method);
  }

private:
  ResampleMethod method;
  std::unordered_map<KeyT, std::unordered_map<float, std::vector<float>>> cache;
};

} // namespace Audio
} // namespace ResourceDASM
