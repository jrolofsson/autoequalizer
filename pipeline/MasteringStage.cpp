#include "pipeline/MasteringStage.hpp"

#include <cmath>

#include "core/Math.hpp"
#include "dsp/Processors.hpp"

namespace autoequalizer::pipeline {

audio::AudioBuffer MasteringStage::apply(const audio::AudioBuffer& input,
                                         const core::ModeLimits& modeLimits,
                                         const core::LoudnessTarget& target,
                                         bool fragileMaterial,
                                         MasteringSummary& summary) const {
  audio::AudioBuffer output = input;
  applyInPlace(output, modeLimits, target, fragileMaterial, summary);
  return output;
}

void MasteringStage::applyInPlace(audio::AudioBuffer& buffer,
                                  const core::ModeLimits& modeLimits,
                                  const core::LoudnessTarget& target,
                                  bool fragileMaterial,
                                  MasteringSummary& summary) const {
  summary.before = loudnessMeter_.measure(buffer);

  if (target.normalizeIntegratedLufs) {
    float gainDb = target.integratedLufs - summary.before.integratedLufs;
    const float positiveCap =
        fragileMaterial ? std::min(modeLimits.normalizationBoostCapDb, 6.0F)
                        : modeLimits.normalizationBoostCapDb;
    gainDb = core::clamp(gainDb, -18.0F, positiveCap);
    buffer.applyGain(core::dbToLinear(gainDb));
    summary.loudnessGainDbApplied = gainDb;
  }

  dsp::TruePeakLimiter limiter(buffer.sampleRate());
  limiter.setSettings({target.truePeakDbtp, 0.35F, 120.0F, 3.0F});

  for (int pass = 0; pass < 3; ++pass) {
    limiter.processInPlace(buffer);
    summary.limiterPasses = pass + 1;
    summary.limiterEngaged = true;

    const auto stats = loudnessMeter_.measure(buffer);
    if (stats.truePeakDbtp <= (target.truePeakDbtp + 0.05F)) {
      summary.after = stats;
      break;
    }

    const float trimDb = target.truePeakDbtp - stats.truePeakDbtp - 0.10F;
    buffer.applyGain(core::dbToLinear(trimDb));
    summary.limiterTrimDbApplied += trimDb;
    summary.after = loudnessMeter_.measure(buffer);
  }

  if (!summary.limiterEngaged) {
    summary.after = loudnessMeter_.measure(buffer);
  }

  if (target.normalizeIntegratedLufs) {
    const float correctionDb = core::clamp(
        target.integratedLufs - summary.after.integratedLufs, -1.5F, 1.5F);
    if ((std::abs(correctionDb) >= 0.35F) &&
        ((correctionDb <= 0.0F) ||
         (summary.after.truePeakDbtp <= (target.truePeakDbtp - 0.75F)))) {
      buffer.applyGain(core::dbToLinear(correctionDb));
      summary.loudnessGainDbApplied += correctionDb;

      limiter.processInPlace(buffer);
      const auto correctedStats = loudnessMeter_.measure(buffer);
      if (correctedStats.truePeakDbtp > (target.truePeakDbtp + 0.05F)) {
        const float trimDb =
            target.truePeakDbtp - correctedStats.truePeakDbtp - 0.10F;
        buffer.applyGain(core::dbToLinear(trimDb));
        summary.limiterTrimDbApplied += trimDb;
      }
      summary.after = loudnessMeter_.measure(buffer);
    }
  }
}

}  // namespace autoequalizer::pipeline
