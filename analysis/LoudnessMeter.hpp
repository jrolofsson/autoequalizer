#pragma once

#include <vector>

#include "audio/AudioBuffer.hpp"

namespace autoequalizer::analysis {

struct LoudnessStats {
  float integratedLufs{-70.0F};
  float loudnessRangeLu{0.0F};
  float momentaryMaxLufs{-70.0F};
  float shortTermMaxLufs{-70.0F};
  float truePeakDbtp{-144.0F};
};

class LoudnessMeter {
 public:
  [[nodiscard]] LoudnessStats measure(const audio::AudioBuffer& buffer) const;

 private:
  [[nodiscard]] std::vector<std::vector<float>> resampleTo48k(
      const audio::AudioBuffer& buffer) const;
};

}  // namespace autoequalizer::analysis

