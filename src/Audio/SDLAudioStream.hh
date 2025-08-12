#pragma once

#include <SDL3/SDL.h>

#include <inttypes.h>

#include <memory>
#include <vector>

namespace ResourceDASM {
namespace Audio {

class SDLAudioStream {
public:
  explicit SDLAudioStream(size_t num_channels, size_t sample_rate);
  ~SDLAudioStream();
  void pause();
  void resume();
  void clear();
  void add(const std::vector<float>& samples);
  void drain();
  double remaining_secs() const;
  void wait_until_remaining_secs(double pending_seconds);

private:
  SDL_AudioDeviceID device_id;
  SDL_AudioStream* stream;
  size_t num_channels;
  size_t sample_rate;
};

} // namespace Audio
} // namespace ResourceDASM
