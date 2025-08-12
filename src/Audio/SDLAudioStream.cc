#include "SDLAudioStream.hh"

#include <SDL3/SDL.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <format>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <vector>

#include "../AudioCodecs.hh"

using namespace std;

namespace ResourceDASM {
namespace Audio {

SDLAudioStream::SDLAudioStream(size_t num_channels, size_t sample_rate)
    : device_id(0), stream(nullptr), num_channels(num_channels), sample_rate(sample_rate) {
  // We expect SDL_Init(SDL_INIT_AUDIO) to already be called

  this->device_id = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
  if (this->device_id == 0) {
    throw std::logic_error(std::format("Failed to open audio: {}", SDL_GetError()));
  }

  SDL_AudioSpec spec;
  spec.format = SDL_AUDIO_F32;
  spec.channels = this->num_channels;
  spec.freq = this->sample_rate;
  this->stream = SDL_CreateAudioStream(&spec, &spec);
  if (!this->stream) {
    throw std::runtime_error(std::format("Cannot create output audio stream: {}", SDL_GetError()));
  }

  SDL_BindAudioStream(this->device_id, this->stream);
  if (!SDL_SetAudioStreamFormat(this->stream, &spec, NULL)) {
    throw std::runtime_error(std::format("Cannot set audio stream format: {}", SDL_GetError()));
  }
}

SDLAudioStream::~SDLAudioStream() {
  if (this->stream) {
    SDL_DestroyAudioStream(this->stream);
  }
  if (this->device_id > 0) {
    SDL_CloseAudioDevice(this->device_id);
  }
}

void SDLAudioStream::clear() {
  if (!SDL_ClearAudioStream(this->stream)) {
    throw std::runtime_error(std::format("Cannot clear audio stream: {}", SDL_GetError()));
  }
}

void SDLAudioStream::add(const std::vector<float>& samples) {
  if (!SDL_PutAudioStreamData(this->stream, samples.data(), samples.size() * sizeof(float))) {
    throw std::runtime_error(std::format("Cannot put audio stream data: {}", SDL_GetError()));
  }
}

void SDLAudioStream::drain() {
  SDL_FlushAudioStream(this->stream);
  this->wait_until_remaining_secs(0);
}

double SDLAudioStream::remaining_secs() const {
  size_t frame_size = this->num_channels * sizeof(float);
  int64_t bytes = SDL_GetAudioStreamQueued(this->stream);
  if (bytes < 0) {
    throw std::runtime_error(std::format("Cannot get audio stream size: {}", SDL_GetError()));
  }
  return (static_cast<double>(bytes) / (frame_size * this->sample_rate));
}

void SDLAudioStream::wait_until_remaining_secs(double pending_seconds) {
  for (;;) {
    double seconds = this->remaining_secs();
    if (seconds <= pending_seconds) {
      break;
    }

    double extra_time = seconds - pending_seconds;
    size_t ms = extra_time * 500; // 1/2x to prevent missing the deadline
    SDL_Delay(ms ? ms : 1);
  }
}

} // namespace Audio
} // namespace ResourceDASM
