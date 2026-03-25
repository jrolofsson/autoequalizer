#pragma once

#include <cstddef>
#include <vector>

namespace autoequalizer::audio {

class AudioBuffer {
 public:
  AudioBuffer() = default;
  AudioBuffer(int sampleRate, std::size_t channels, std::size_t frames);

  [[nodiscard]] int sampleRate() const noexcept { return sampleRate_; }
  [[nodiscard]] std::size_t channelCount() const noexcept {
    return channels_.size();
  }
  [[nodiscard]] std::size_t frameCount() const noexcept {
    return channels_.empty() ? 0U : channels_.front().size();
  }

  [[nodiscard]] std::vector<float>& channel(std::size_t index);
  [[nodiscard]] const std::vector<float>& channel(std::size_t index) const;
  [[nodiscard]] std::vector<float> mixdownMono() const;
  [[nodiscard]] float peakAbs() const;

  void applyGain(float linearGain);

 private:
  int sampleRate_{48000};
  std::vector<std::vector<float>> channels_;
};

}  // namespace autoequalizer::audio

