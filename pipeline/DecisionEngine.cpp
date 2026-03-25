#include "pipeline/DecisionEngine.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

#include "core/Math.hpp"

namespace autoequalizer::pipeline {

namespace {

[[nodiscard]] float localMean(const std::vector<analysis::FrameFeatures>& frames,
                              std::size_t center,
                              float analysis::FrameFeatures::*member) {
  const std::size_t start = (center > 2U) ? (center - 2U) : 0U;
  const std::size_t end =
      std::min(frames.size(), center + static_cast<std::size_t>(3U));

  float total = 0.0F;
  std::size_t count = 0U;
  for (std::size_t index = start; index < end; ++index) {
    total += frames[index].*member;
    ++count;
  }

  return count == 0U ? 0.0F : total / static_cast<float>(count);
}

[[nodiscard]] float localVariance(
    const std::vector<analysis::FrameFeatures>& frames,
    std::size_t center,
    float analysis::FrameFeatures::*member,
    float mean) {
  const std::size_t start = (center > 2U) ? (center - 2U) : 0U;
  const std::size_t end =
      std::min(frames.size(), center + static_cast<std::size_t>(3U));

  float total = 0.0F;
  std::size_t count = 0U;
  for (std::size_t index = start; index < end; ++index) {
    const float delta = (frames[index].*member) - mean;
    total += delta * delta;
    ++count;
  }

  return count == 0U ? 0.0F : total / static_cast<float>(count);
}

[[nodiscard]] std::string buildRationale(bool brightProtection,
                                         bool highRegisterProtection,
                                         bool sourceBakedFallback,
                                         bool fragileMaterial,
                                         bool metallicOverride,
                                         bool limitReachedArtifact,
                                         float harshnessScore,
                                         float brightnessScore,
                                         float confidence) {
  if (sourceBakedFallback || fragileMaterial) {
    return "The segment looked fragile or source-baked, so AutoEqualizer backed off to "
           "minimal corrective settings.";
  }

  if (limitReachedArtifact) {
    return "A metallic or synthetic reverb-like artifact was detected, but it "
           "looked partly source-baked, so AutoEqualizer used a blend-limited repair "
           "instead of pushing the vocal into dullness.";
  }

  if (metallicOverride) {
    return "Bright harmonic content was preserved, but a metallic upper-mid edge "
           "still triggered a targeted corrective override.";
  }

  if (brightProtection || highRegisterProtection) {
    return "Persistent bright or high-register harmonic content was protected, "
           "so de-essing, top-end smoothing, and compression were clamped.";
  }

  if (harshnessScore >= 0.60F) {
    return "Harsh upper-mid energy and sibilant behavior exceeded the file "
           "baseline, so targeted correction was enabled.";
  }

  if (confidence < 0.45F) {
    return "Confidence was limited, so AutoEqualizer preferred gentle cleanup over "
           "assertive correction.";
  }

  if (brightnessScore > harshnessScore) {
    return "The segment leaned bright but stable, so only minimal cleanup was "
           "applied.";
  }

  return "Only light corrective processing was required based on the local "
         "analysis profile.";
}

}  // namespace

ProcessingPlan DecisionEngine::buildPlan(const analysis::AnalysisResult& analysis,
                                         core::ProcessingMode mode,
                                         core::LoudnessTarget loudnessTarget) const {
  ProcessingPlan plan;
  plan.mode = mode;
  plan.loudnessTarget = loudnessTarget;
  plan.inputProfile = analysis.profile;

  const core::ModeLimits limits = core::limitsForMode(mode);
  plan.fileMarkedFragile = analysis.profile.fragilityScore >= 0.58F;
  plan.fileMarkedSourceBaked = analysis.profile.sourceBakedScore >= 0.62F;

  if (plan.fileMarkedFragile) {
    plan.fileGuardrails.push_back("fragile_material_preservation");
  }
  if (plan.fileMarkedSourceBaked) {
    plan.fileGuardrails.push_back("source_baked_fallback");
  }
  if (analysis.profile.voicedFrameRatio >= 0.55F) {
    plan.fileGuardrails.push_back("vocal_harmonic_protection");
  }
  if (mode == core::ProcessingMode::ArtifactSafe) {
    plan.fileGuardrails.push_back("artifact_safe_mode");
  }

  bool sawLimitReachedArtifact = false;
  plan.segments.reserve(analysis.frames.size());

  for (std::size_t index = 0; index < analysis.frames.size(); ++index) {
    const auto& frame = analysis.frames[index];
    SegmentDecision segment;
    segment.frameIndex = frame.frameIndex;
    segment.sampleOffset = frame.sampleOffset;
    segment.sampleCount = frame.sampleCount;
    segment.startSeconds = frame.startSeconds;
    segment.endSeconds = frame.endSeconds;

    const float centroidMean =
        localMean(analysis.frames, index,
                  &analysis::FrameFeatures::spectralCentroidHz);
    const float upperMidMean =
        localMean(analysis.frames, index, &analysis::FrameFeatures::upperMidRatio);
    const float airMean =
        localMean(analysis.frames, index, &analysis::FrameFeatures::airRatio);
    const float airVariance = localVariance(
        analysis.frames, index, &analysis::FrameFeatures::airRatio, airMean);
    const float centroidVariance =
        localVariance(analysis.frames, index,
                      &analysis::FrameFeatures::spectralCentroidHz,
                      centroidMean);

    const float persistence = 1.0F - core::clamp(
                                           (0.5F * core::normalizeRange(
                                                        airVariance, 0.0005F,
                                                        0.02F)) +
                                               (0.5F * core::normalizeRange(
                                                            centroidVariance,
                                                            10000.0F, 450000.0F)),
                                           0.0F, 1.0F);

    const float pitchHighRegisterScore =
        (frame.harmonicConfidence >= 0.12F)
            ? core::normalizeRange(frame.pitchEstimateHz, 280.0F, 560.0F)
            : 0.0F;
    const float brightHarmonicScore = core::clamp(
        (0.55F * core::normalizeRange(
                     frame.spectralCentroidHz,
                     analysis.profile.brightnessBaseline * 1.08F,
                     analysis.profile.brightnessBaseline * 1.60F)) +
            (0.25F * core::normalizeRange(
                         frame.airRatio, analysis.profile.meanAirRatio + 0.02F,
                         0.28F)) +
            (0.20F * persistence),
        0.0F, 1.0F);
    const float highRegisterScore =
        std::max(pitchHighRegisterScore, brightHarmonicScore);

    segment.brightnessScore = core::clamp(
        (0.30F * core::normalizeRange(
                     frame.spectralCentroidHz,
                     analysis.profile.meanSpectralCentroidHz * 0.9F,
                     analysis.profile.brightnessBaseline * 1.25F)) +
            (0.20F * core::normalizeRange(
                         upperMidMean + airMean,
                         analysis.profile.meanUpperMidRatio +
                             analysis.profile.meanAirRatio + 0.02F,
                         0.48F)) +
            (0.20F * (1.0F - frame.spectralFlatness)) +
            (0.15F * persistence) +
            (0.15F * highRegisterScore),
        0.0F, 1.0F);

    segment.harshnessScore = core::clamp(
        (0.35F * core::normalizeRange(frame.upperMidRatio,
                                      analysis.profile.meanUpperMidRatio + 0.01F,
                                      analysis.profile.meanUpperMidRatio + 0.18F)) +
            (0.30F * core::normalizeRange(
                         frame.sibilantActivity,
                         analysis.profile.meanSibilantActivity + 0.02F,
                         analysis.profile.meanSibilantActivity + 0.22F)) +
            (0.20F * core::normalizeRange(frame.highFrequencyBalance, 0.55F,
                                          1.60F)) +
            (0.15F * core::normalizeRange(frame.spectralFlatness, 0.12F, 0.48F)),
        0.0F, 1.0F);

    const bool legitimateBright =
        (segment.brightnessScore >= 0.60F) &&
        (segment.brightnessScore >= (segment.harshnessScore + 0.08F)) &&
        (segment.brightnessScore >= segment.harshnessScore * 1.15F) &&
        (persistence >= 0.55F) &&
        ((frame.airRatio >= analysis.profile.meanAirRatio) ||
         (frame.pitchEstimateHz >= 260.0F)) &&
        (frame.spectralFlatness <= 0.24F);
    const bool airyHighRegister =
        (brightHarmonicScore >= 0.76F) &&
        (frame.airRatio >= (analysis.profile.meanAirRatio + 0.015F)) &&
        (frame.spectralFlatness <= 0.22F) &&
        (frame.harmonicConfidence >= 0.08F) &&
        (frame.highFrequencyBalance <= 2.20F) &&
        (frame.sibilantActivity <=
         (analysis.profile.meanSibilantActivity + 0.20F));
    const bool highRegisterProtection =
        (((pitchHighRegisterScore >= 0.58F) && (persistence >= 0.45F)) ||
         airyHighRegister) &&
        (segment.harshnessScore <= (segment.brightnessScore + 0.10F));
    if (legitimateBright) {
      segment.harshnessScore *= 0.68F;
    }

    segment.fragilityScore = core::clamp(
        (0.35F * core::normalizeRange(frame.spectralFlatness, 0.18F, 0.54F)) +
            (0.25F * core::normalizeRange(frame.changeScore, 0.03F, 0.26F)) +
            (0.20F *
             (1.0F - core::normalizeRange(frame.dynamicRangeDb, 7.0F, 18.0F))) +
            (0.20F * analysis.profile.fragilityScore),
        0.0F, 1.0F);

    segment.confidence = core::clamp(
        0.55F + (0.20F * std::abs(segment.harshnessScore - segment.brightnessScore)) +
            (0.10F * frame.harmonicConfidence) -
            (0.25F * segment.fragilityScore),
        0.15F, 0.98F);

    const bool upperMidEdge =
        (frame.upperMidRatio >=
         (analysis.profile.meanUpperMidRatio + 0.045F)) &&
        ((frame.highFrequencyBalance >= 1.75F) ||
         (frame.sibilantActivity >=
          (analysis.profile.meanSibilantActivity + 0.08F)));
    const bool weakHarmonicEvidence =
        (frame.harmonicConfidence <= 0.10F) || (pitchHighRegisterScore <= 0.20F);
    const bool metallicOverride =
        (segment.harshnessScore >= 0.58F) &&
        (segment.brightnessScore >= 0.72F) &&
        (segment.fragilityScore <= 0.50F) &&
        upperMidEdge &&
        weakHarmonicEvidence &&
        (frame.spectralFlatness <= 0.04F);
    const bool limitReachedArtifact =
        metallicOverride &&
        (frame.highFrequencyBalance >= 2.40F) &&
        (frame.harmonicConfidence <= 0.06F) &&
        ((frame.airRatio >= (analysis.profile.meanAirRatio + 0.10F)) ||
         (persistence >= 0.50F));

    const bool sourceBakedFallback =
        plan.fileMarkedSourceBaked ||
        ((segment.fragilityScore >= 0.62F) && (frame.spectralFlatness >= 0.34F) &&
         (frame.changeScore >= 0.10F));

    segment.guardrails.brightProtection = legitimateBright;
    segment.guardrails.highRegisterProtection = highRegisterProtection;
    segment.guardrails.metallicCorrectionOverride = metallicOverride;
    segment.guardrails.limitReachedArtifact = limitReachedArtifact;
    sawLimitReachedArtifact = sawLimitReachedArtifact || limitReachedArtifact;
    segment.guardrails.fragileMaterial =
        plan.fileMarkedFragile || (segment.fragilityScore >= 0.58F);
    segment.guardrails.sourceBakedFallback = sourceBakedFallback;
    segment.guardrails.lowConfidenceFallback = segment.confidence < 0.45F;
    segment.processMix = 1.0F;

    segment.highPassCutoffHz = core::clamp(
        28.0F +
            (core::normalizeRange(frame.lowBandRatio,
                                  analysis.profile.meanLowBandRatio + 0.005F,
                                  0.16F) *
             48.0F),
        25.0F, plan.fileMarkedFragile ? 52.0F : 82.0F);
    segment.harshCenterHz =
        3200.0F + (core::normalizeRange(frame.spectralCentroidHz, 1200.0F, 4200.0F) *
                   1200.0F);
    segment.harshCutDb = segment.harshnessScore * limits.harshCutDbCap;
    segment.deEssThreshold = core::clamp(
        0.14F -
            (core::normalizeRange(frame.sibilantActivity,
                                  analysis.profile.meanSibilantActivity + 0.02F,
                                  analysis.profile.meanSibilantActivity + 0.18F) *
             0.08F),
        0.04F, 0.16F);
    segment.deEssMaxReductionDb =
        core::normalizeRange(frame.sibilantActivity,
                             analysis.profile.meanSibilantActivity + 0.02F, 0.36F) *
        limits.deEssMaxReductionDbCap;
    segment.hfSmoothingDb =
        std::max(0.0F, segment.harshnessScore - (segment.brightnessScore * 0.55F)) *
        limits.hfSmoothDbCap;
    segment.compressionThresholdDb =
        core::clamp(frame.rmsDbfs + 8.0F, -28.0F, -8.0F);
    segment.compressionRatio =
        1.0F +
        (core::normalizeRange(frame.dynamicRangeDb, 9.0F, 20.0F) *
         (limits.compressionRatioCap - 1.0F) * 0.55F);

    if (segment.guardrails.brightProtection ||
        segment.guardrails.highRegisterProtection) {
      const float harshClamp = metallicOverride ? 0.72F : 0.40F;
      const float deEssClamp = metallicOverride ? 0.60F : 0.35F;
      const float hfClamp = metallicOverride ? 0.80F : 0.25F;
      const float compressionClamp = metallicOverride ? 0.70F : 0.40F;
      segment.harshCutDb *= harshClamp;
      segment.deEssMaxReductionDb *= deEssClamp;
      segment.hfSmoothingDb *= hfClamp;
      segment.compressionRatio =
          1.0F + ((segment.compressionRatio - 1.0F) * compressionClamp);
      segment.guardrails.deEsserClamped = true;
      segment.guardrails.compressionRelaxed = true;
    }

    if (metallicOverride) {
      segment.harshCutDb = std::max(segment.harshCutDb, 0.95F);
      segment.deEssMaxReductionDb = std::max(segment.deEssMaxReductionDb, 0.35F);
      segment.hfSmoothingDb = std::max(segment.hfSmoothingDb, 0.01F);
      segment.processMix = limitReachedArtifact ? 0.38F : 0.60F;
    }

    if (limitReachedArtifact) {
      segment.harshCutDb *= 0.88F;
      segment.deEssMaxReductionDb *= 0.75F;
      segment.hfSmoothingDb *= 0.35F;
      segment.compressionRatio =
          1.0F + ((segment.compressionRatio - 1.0F) * 0.55F);
      segment.guardrails.deEsserClamped = true;
      segment.guardrails.compressionRelaxed = true;
    }

    if (mode == core::ProcessingMode::ArtifactSafe) {
      if (segment.guardrails.metallicCorrectionOverride) {
        segment.harshCutDb *= 0.92F;
        segment.deEssMaxReductionDb *= 0.70F;
        segment.hfSmoothingDb *= 0.50F;
        segment.compressionRatio =
            1.0F + ((segment.compressionRatio - 1.0F) * 0.72F);
        segment.processMix *= segment.guardrails.limitReachedArtifact ? 0.82F : 0.90F;
      }

      if (segment.guardrails.brightProtection ||
          segment.guardrails.highRegisterProtection) {
        segment.deEssMaxReductionDb *= 0.88F;
        segment.hfSmoothingDb *= 0.80F;
      }
    }

    if (segment.guardrails.fragileMaterial ||
        segment.guardrails.sourceBakedFallback) {
      const float reduction =
          segment.guardrails.sourceBakedFallback ? 0.25F : 0.50F;
      segment.harshCutDb *= reduction;
      segment.deEssMaxReductionDb *= reduction;
      segment.hfSmoothingDb *= reduction;
      segment.compressionRatio = 1.0F + ((segment.compressionRatio - 1.0F) * reduction);
      segment.guardrails.deEsserClamped = true;
      segment.guardrails.compressionRelaxed = true;
    }

    if (segment.guardrails.lowConfidenceFallback) {
      segment.harshCutDb *= 0.65F;
      segment.deEssMaxReductionDb *= 0.65F;
      segment.hfSmoothingDb *= 0.65F;
      segment.compressionRatio = 1.0F + ((segment.compressionRatio - 1.0F) * 0.70F);
    }

    segment.harshCutDb = core::clamp(segment.harshCutDb, 0.0F,
                                     limits.harshCutDbCap);
    segment.deEssMaxReductionDb =
        core::clamp(segment.deEssMaxReductionDb, 0.0F,
                    limits.deEssMaxReductionDbCap);
    segment.hfSmoothingDb = core::clamp(segment.hfSmoothingDb, 0.0F,
                                        limits.hfSmoothDbCap);
    segment.compressionRatio =
        core::clamp(segment.compressionRatio, 1.0F, limits.compressionRatioCap);
    segment.processMix = core::clamp(segment.processMix, 0.20F, 1.0F);

    segment.rationale = buildRationale(
        segment.guardrails.brightProtection,
        segment.guardrails.highRegisterProtection,
        segment.guardrails.sourceBakedFallback,
        segment.guardrails.fragileMaterial, metallicOverride,
        segment.guardrails.limitReachedArtifact,
        segment.harshnessScore,
        segment.brightnessScore, segment.confidence);

    plan.segments.push_back(segment);
  }

  if (sawLimitReachedArtifact) {
    plan.fileGuardrails.push_back("limit_reached_artifact_preservation");
  }

  if (!plan.segments.empty()) {
    struct SmoothedValues {
      float highPassCutoffHz{};
      float harshCenterHz{};
      float harshCutDb{};
      float deEssThreshold{};
      float deEssMaxReductionDb{};
      float hfSmoothingDb{};
      float compressionThresholdDb{};
      float compressionRatio{};
      float processMix{};
    };

    std::vector<SmoothedValues> smoothed(plan.segments.size());
    for (std::size_t index = 0; index < plan.segments.size(); ++index) {
      const std::size_t start = (index > 2U) ? (index - 2U) : 0U;
      const std::size_t end =
          std::min(plan.segments.size(), index + static_cast<std::size_t>(3U));
      const float count = static_cast<float>(end - start);

      auto meanValue = [&](auto accessor) {
        float total = 0.0F;
        for (std::size_t offset = start; offset < end; ++offset) {
          total += accessor(plan.segments[offset]);
        }
        return total / count;
      };

      smoothed[index].highPassCutoffHz = meanValue(
          [](const SegmentDecision& segment) { return segment.highPassCutoffHz; });
      smoothed[index].harshCenterHz = meanValue(
          [](const SegmentDecision& segment) { return segment.harshCenterHz; });
      smoothed[index].harshCutDb = meanValue(
          [](const SegmentDecision& segment) { return segment.harshCutDb; });
      smoothed[index].deEssThreshold = meanValue(
          [](const SegmentDecision& segment) { return segment.deEssThreshold; });
      smoothed[index].deEssMaxReductionDb = meanValue([](const SegmentDecision& segment) {
        return segment.deEssMaxReductionDb;
      });
      smoothed[index].hfSmoothingDb = meanValue(
          [](const SegmentDecision& segment) { return segment.hfSmoothingDb; });
      smoothed[index].compressionThresholdDb = meanValue([](const SegmentDecision& segment) {
        return segment.compressionThresholdDb;
      });
      smoothed[index].compressionRatio = meanValue(
          [](const SegmentDecision& segment) { return segment.compressionRatio; });
      smoothed[index].processMix = meanValue(
          [](const SegmentDecision& segment) { return segment.processMix; });
    }

    for (std::size_t index = 0; index < plan.segments.size(); ++index) {
      auto& segment = plan.segments[index];
      const auto& values = smoothed[index];
      segment.highPassCutoffHz = values.highPassCutoffHz;
      segment.harshCenterHz = values.harshCenterHz;
      segment.harshCutDb = values.harshCutDb;
      segment.deEssThreshold = values.deEssThreshold;
      segment.deEssMaxReductionDb = values.deEssMaxReductionDb;
      segment.hfSmoothingDb = values.hfSmoothingDb;
      segment.compressionThresholdDb = values.compressionThresholdDb;
      segment.compressionRatio = values.compressionRatio;
      segment.processMix = values.processMix;
    }
  }

  return plan;
}

}  // namespace autoequalizer::pipeline
