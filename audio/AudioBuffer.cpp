#include "audio/AudioBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace autoequalizer::audio {

AudioBuffer::AudioBuffer(int sampleRate,
                         std::size_t channels,
                         std::size_t frames)
    : sampleRate_(sampleRate), channels_(channels, std::vector<float>(frames)) {}

std::vector<float>& AudioBuffer::channel(std::size_t index) {
  if (index >= channels_.size()) {
    throw std::out_of_range("AudioBuffer channel index out of range");
  }

  return channels_[index];
}

const std::vector<float>& AudioBuffer::channel(std::size_t index) const {
  if (index >= channels_.size()) {
    throw std::out_of_range("AudioBuffer channel index out of range");
  }

  return channels_[index];
}

std::vector<float> AudioBuffer::mixdownMono() const {
  std::vector<float> mono(frameCount(), 0.0F);
  if (channels_.empty()) {
    return mono;
  }

  for (const auto& channelData : channels_) {
    for (std::size_t frame = 0; frame < channelData.size(); ++frame) {
      mono[frame] += channelData[frame];
    }
  }

  const float scale = 1.0F / static_cast<float>(channels_.size());
  for (float& sample : mono) {
    sample *= scale;
  }

  return mono;
}

float AudioBuffer::peakAbs() const {
  float peak = 0.0F;
  for (const auto& channelData : channels_) {
    for (const float sample : channelData) {
      peak = std::max(peak, std::abs(sample));
    }
  }

  return peak;
}

void AudioBuffer::applyGain(float linearGain) {
  for (auto& channelData : channels_) {
    for (float& sample : channelData) {
      sample *= linearGain;
    }
  }
}

}  // namespace autoequalizer::audio

