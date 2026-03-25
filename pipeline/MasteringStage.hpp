#pragma once

#include "analysis/LoudnessMeter.hpp"
#include "audio/AudioBuffer.hpp"
#include "core/Types.hpp"

namespace autoequalizer::pipeline {

struct MasteringSummary {
  float loudnessGainDbApplied{};
  float limiterTrimDbApplied{};
  bool limiterEngaged{};
  int limiterPasses{};
  analysis::LoudnessStats before{};
  analysis::LoudnessStats after{};
};

class MasteringStage {
 public:
  [[nodiscard]] audio::AudioBuffer apply(const audio::AudioBuffer& input,
                                         const core::ModeLimits& modeLimits,
                                         const core::LoudnessTarget& target,
                                         bool fragileMaterial,
                                         MasteringSummary& summary) const;
  void applyInPlace(audio::AudioBuffer& buffer,
                    const core::ModeLimits& modeLimits,
                    const core::LoudnessTarget& target,
                    bool fragileMaterial,
                    MasteringSummary& summary) const;

 private:
  analysis::LoudnessMeter loudnessMeter_{};
};

}  // namespace autoequalizer::pipeline
