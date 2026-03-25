#include "pipeline/AutoEqualizerPipeline.hpp"

#include <algorithm>
#include <vector>

#include "core/Math.hpp"
#include "dsp/Processors.hpp"

namespace autoequalizer::pipeline {

namespace {

[[nodiscard]] bool overlaps(const SegmentDecision& segment,
                            const core::RangeOverride& rangeOverride) {
  return (segment.endSeconds > rangeOverride.startSeconds) &&
         (segment.startSeconds < rangeOverride.endSeconds);
}

void appendOverrideRationale(SegmentDecision& segment,
                             core::RangeOverridePolicy policy) {
  const std::string sentence =
      " Timestamp-targeted override applied: " +
      core::toString(policy) +
      " policy took precedence for this window.";
  if (segment.rationale.find(sentence) == std::string::npos) {
    segment.rationale += sentence;
  }
}

}  // namespace

PipelineSnapshot AutoEqualizerPipeline::analyze(const audio::AudioBuffer& input,
                                         core::ProcessingMode mode,
                                         bool loudnessOnly,
                                         const std::vector<core::RangeOverride>& rangeOverrides,
                                         core::LoudnessTarget loudnessTarget) const {
  PipelineSnapshot snapshot;
  snapshot.analysisBefore = analysisEngine_.analyze(input);
  snapshot.plan =
      decisionEngine_.buildPlan(snapshot.analysisBefore, mode, loudnessTarget);
  applyRangeOverrides(snapshot.plan, rangeOverrides);
  if (loudnessOnly) {
    applyLoudnessOnly(snapshot.plan);
  }

  if (snapshot.plan.fileMarkedFragile) {
    snapshot.warnings.push_back(
        "The file was classified as fragile, so any future processing should "
        "remain conservative.");
  }
  if (snapshot.plan.fileMarkedSourceBaked) {
    snapshot.warnings.push_back(
        "The file showed source-baked characteristics; AutoEqualizer prefers "
        "preservation over aggressive restoration here.");
  }
  if (snapshot.plan.loudnessTarget.inferred) {
    snapshot.warnings.push_back(
        "The selected loudness profile uses an Apple Music compatibility preset "
        "rather than an Apple-published LUFS target.");
  }
  if (snapshot.plan.loudnessTarget.profile == core::LoudnessProfile::Stem) {
    snapshot.warnings.push_back(
        "Stem profile selected: AutoEqualizer will preserve relative loudness and "
        "headroom while constraining true peak, instead of forcing a streaming "
        "integrated LUFS target.");
  }
  if (snapshot.plan.mode == core::ProcessingMode::ArtifactSafe) {
    snapshot.warnings.push_back(
        "Artifact-safe mode selected: AutoEqualizer will prefer blend-limited, tone-"
        "preserving repair over stronger cleanup on metallic or source-baked "
        "segments.");
  }
  if (std::any_of(snapshot.plan.segments.begin(), snapshot.plan.segments.end(),
                  [](const SegmentDecision& segment) {
                    return segment.guardrails.limitReachedArtifact;
                  })) {
    snapshot.warnings.push_back(
        "Some metallic or synthetic reverb-like artifacts appeared partly "
        "source-baked, so AutoEqualizer used blend-limited repair in those regions "
        "instead of forcing stronger cleanup.");
  }
  if (!snapshot.plan.requestedOverrides.empty()) {
    snapshot.warnings.push_back(
        "Timestamp-targeted overrides were applied after analysis. Those manual "
        "windows take precedence over the default adaptive behavior inside the "
        "specified ranges.");
  }
  if (loudnessOnly) {
    snapshot.warnings.push_back(
        "Loudness-only mode selected: AutoEqualizer will skip corrective EQ, de-essing, "
        "and compression, and only apply loudness normalization and true-peak "
        "limiting.");
  }

  return snapshot;
}

PipelineSnapshot AutoEqualizerPipeline::process(audio::AudioBuffer input,
                                         core::ProcessingMode mode,
                                         bool loudnessOnly,
                                         const std::vector<core::RangeOverride>& rangeOverrides,
                                         core::LoudnessTarget loudnessTarget) const {
  PipelineSnapshot snapshot =
      analyze(input, mode, loudnessOnly, rangeOverrides, loudnessTarget);
  if (!loudnessOnly) {
    applyPlan(input, snapshot.plan);
  }
  MasteringSummary masteringSummary;
  masteringStage_.applyInPlace(input, core::limitsForMode(snapshot.plan.mode),
                               snapshot.plan.loudnessTarget,
                               snapshot.plan.fileMarkedFragile,
                               masteringSummary);
  snapshot.plan.finalNormalizationGainDbApplied =
      masteringSummary.loudnessGainDbApplied +
      masteringSummary.limiterTrimDbApplied;
  snapshot.analysisAfter = analysisEngine_.analyze(input);
  snapshot.processedBuffer = std::move(input);
  if (snapshot.plan.loudnessTarget.normalizeIntegratedLufs &&
      (snapshot.analysisAfter->profile.integratedLufs <
       (snapshot.plan.loudnessTarget.integratedLufs - 1.5F)) &&
      (snapshot.analysisAfter->profile.truePeakDbtp <=
       (snapshot.plan.loudnessTarget.truePeakDbtp + 0.2F))) {
    snapshot.warnings.push_back(
        "The requested streaming loudness target could not be reached without "
        "exceeding the true-peak safety ceiling. This often happens on isolated "
        "stems with high peak-to-loudness ratios; try --loudness-profile stem "
        "for mix-ready vocal outputs.");
  }
  return snapshot;
}

void AutoEqualizerPipeline::applyLoudnessOnly(ProcessingPlan& plan) const {
  if (std::find(plan.fileGuardrails.begin(), plan.fileGuardrails.end(),
                "loudness_only") == plan.fileGuardrails.end()) {
    plan.fileGuardrails.push_back("loudness_only");
  }

  for (auto& segment : plan.segments) {
    segment.harshCutDb = 0.0F;
    segment.deEssMaxReductionDb = 0.0F;
    segment.hfSmoothingDb = 0.0F;
    segment.compressionRatio = 1.0F;
    segment.processMix = 0.0F;
    segment.rationale =
        "Corrective EQ, de-essing, and compression were bypassed because "
        "loudness-only mode was selected.";
  }
}

void AutoEqualizerPipeline::applyRangeOverrides(
    ProcessingPlan& plan,
    const std::vector<core::RangeOverride>& rangeOverrides) const {
  if (rangeOverrides.empty()) {
    return;
  }

  plan.requestedOverrides = rangeOverrides;
  plan.fileGuardrails.push_back("timestamp_targeted_overrides");

  bool usedArtifactSafeOverride = false;
  bool usedPreserveOverride = false;
  bool usedBypassOverride = false;

  const core::ModeLimits limits = core::limitsForMode(plan.mode);

  for (const auto& rangeOverride : rangeOverrides) {
    for (auto& segment : plan.segments) {
      if (!overlaps(segment, rangeOverride)) {
        continue;
      }

      segment.overridePolicy = core::toString(rangeOverride.policy);
      switch (rangeOverride.policy) {
        case core::RangeOverridePolicy::ArtifactSafe:
          usedArtifactSafeOverride = true;
          segment.harshCutDb *= segment.guardrails.metallicCorrectionOverride
                                    ? 0.92F
                                    : 0.82F;
          segment.deEssMaxReductionDb *=
              (segment.guardrails.brightProtection ||
               segment.guardrails.highRegisterProtection)
                  ? 0.68F
                  : 0.78F;
          segment.hfSmoothingDb *=
              segment.guardrails.limitReachedArtifact ? 0.35F : 0.60F;
          segment.compressionRatio =
              1.0F + ((segment.compressionRatio - 1.0F) * 0.70F);
          segment.processMix = std::min(
              segment.processMix,
              segment.guardrails.limitReachedArtifact ? 0.55F : 0.72F);
          segment.guardrails.deEsserClamped = true;
          segment.guardrails.compressionRelaxed = true;
          break;
        case core::RangeOverridePolicy::Preserve:
          usedPreserveOverride = true;
          segment.harshCutDb *= 0.45F;
          segment.deEssMaxReductionDb *= 0.35F;
          segment.hfSmoothingDb *= 0.20F;
          segment.compressionRatio =
              1.0F + ((segment.compressionRatio - 1.0F) * 0.35F);
          segment.processMix = std::min(segment.processMix, 0.40F);
          segment.guardrails.deEsserClamped = true;
          segment.guardrails.compressionRelaxed = true;
          break;
        case core::RangeOverridePolicy::Bypass:
          usedBypassOverride = true;
          segment.harshCutDb = 0.0F;
          segment.deEssMaxReductionDb = 0.0F;
          segment.hfSmoothingDb = 0.0F;
          segment.compressionRatio = 1.0F;
          segment.processMix = 0.0F;
          segment.guardrails.deEsserClamped = true;
          segment.guardrails.compressionRelaxed = true;
          break;
      }

      segment.harshCutDb = core::clamp(segment.harshCutDb, 0.0F,
                                       limits.harshCutDbCap);
      segment.deEssMaxReductionDb =
          core::clamp(segment.deEssMaxReductionDb, 0.0F,
                      limits.deEssMaxReductionDbCap);
      segment.hfSmoothingDb =
          core::clamp(segment.hfSmoothingDb, 0.0F, limits.hfSmoothDbCap);
      segment.compressionRatio =
          core::clamp(segment.compressionRatio, 1.0F, limits.compressionRatioCap);
      segment.processMix = core::clamp(segment.processMix, 0.0F, 1.0F);
      appendOverrideRationale(segment, rangeOverride.policy);
    }
  }

  if (usedArtifactSafeOverride) {
    plan.fileGuardrails.push_back("targeted_artifact_safe_override");
  }
  if (usedPreserveOverride) {
    plan.fileGuardrails.push_back("targeted_preserve_override");
  }
  if (usedBypassOverride) {
    plan.fileGuardrails.push_back("targeted_bypass_override");
  }
}

void AutoEqualizerPipeline::applyPlan(audio::AudioBuffer& buffer,
                               ProcessingPlan& plan) const {
  if (plan.segments.empty() || (buffer.channelCount() == 0U)) {
    return;
  }

  std::vector<dsp::AdaptiveDspChain> chains;
  chains.reserve(buffer.channelCount());
  for (std::size_t channel = 0; channel < buffer.channelCount(); ++channel) {
    chains.emplace_back(buffer.sampleRate());
  }

  for (const auto& segment : plan.segments) {
    dsp::ChainSettings settings;
    settings.highPassCutoffHz = segment.highPassCutoffHz;
    settings.harshCenterHz = segment.harshCenterHz;
    settings.harshQ = 2.2F;
    settings.harshCutDb = segment.harshCutDb;
    settings.hfShelfCenterHz = 8500.0F;
    settings.hfSmoothingDb = segment.hfSmoothingDb;
    settings.deEsser.threshold = segment.deEssThreshold;
    settings.deEsser.splitFrequencyHz = 5500.0F;
    settings.deEsser.maxReductionDb = segment.deEssMaxReductionDb;
    settings.compressor.thresholdDb = segment.compressionThresholdDb;
    settings.compressor.ratio = segment.compressionRatio;
    settings.compressor.attackMs = 12.0F;
    settings.compressor.releaseMs = 90.0F;

    if (segment.guardrails.metallicCorrectionOverride) {
      // Favor a narrower upper-mid correction over broad top-end damping so
      // metallic cleanup does not collapse vocal air and clarity.
      settings.harshQ = 3.8F;
      settings.hfShelfCenterHz = 11000.0F;
      settings.hfSmoothingDb *= 0.40F;
      settings.deEsser.splitFrequencyHz = 6800.0F;
      settings.deEsser.maxReductionDb *= 0.55F;
      settings.compressor.ratio =
          1.0F + ((settings.compressor.ratio - 1.0F) * 0.75F);
    }
    if (segment.guardrails.limitReachedArtifact) {
      settings.harshQ = 4.4F;
      settings.hfShelfCenterHz = 12500.0F;
      settings.hfSmoothingDb *= 0.15F;
      settings.deEsser.splitFrequencyHz = 7600.0F;
      settings.deEsser.maxReductionDb *= 0.40F;
      settings.compressor.ratio =
          1.0F + ((settings.compressor.ratio - 1.0F) * 0.55F);
    }
    if (plan.mode == core::ProcessingMode::ArtifactSafe) {
      if (segment.guardrails.metallicCorrectionOverride) {
        settings.harshQ = std::max(settings.harshQ, 4.8F);
        settings.hfShelfCenterHz = std::max(settings.hfShelfCenterHz, 13000.0F);
        settings.hfSmoothingDb *= 0.60F;
        settings.deEsser.maxReductionDb *= 0.70F;
      }
      if (segment.guardrails.limitReachedArtifact) {
        settings.hfSmoothingDb *= 0.50F;
        settings.deEsser.maxReductionDb *= 0.65F;
      }
    }

    for (auto& chain : chains) {
      chain.configure(settings);
    }

    const std::size_t start = segment.sampleOffset;
    const std::size_t end =
        std::min(buffer.frameCount(), segment.sampleOffset + segment.sampleCount);

    for (std::size_t sample = start; sample < end; ++sample) {
      for (std::size_t channel = 0; channel < buffer.channelCount(); ++channel) {
        const float dry = buffer.channel(channel)[sample];
        const float wet = chains[channel].processSample(dry);
        buffer.channel(channel)[sample] =
            ((1.0F - segment.processMix) * dry) + (segment.processMix * wet);
      }
    }
  }

  const float peak = buffer.peakAbs();
  if (peak > core::kEpsilon) {
    if (core::linearToDb(peak) > 0.0F) {
      const float trimDb = -core::linearToDb(peak);
      buffer.applyGain(core::dbToLinear(trimDb));
    }
  }
}

}  // namespace autoequalizer::pipeline
