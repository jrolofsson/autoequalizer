#include <algorithm>
#include <array>

#include "analysis/AnalysisEngine.hpp"
#include "core/Types.hpp"
#include "pipeline/AutoEqualizerPipeline.hpp"
#include "pipeline/DecisionEngine.hpp"
#include "tests/TestFramework.hpp"

namespace {

autoequalizer::analysis::AnalysisResult makeBrightAnalysis() {
  autoequalizer::analysis::AnalysisResult analysis;
  analysis.profile.meanSpectralCentroidHz = 2400.0F;
  analysis.profile.meanLowBandRatio = 0.03F;
  analysis.profile.meanUpperMidRatio = 0.16F;
  analysis.profile.meanAirRatio = 0.11F;
  analysis.profile.meanFlatness = 0.18F;
  analysis.profile.meanSibilantActivity = 0.10F;
  analysis.profile.meanHighFrequencyBalance = 0.95F;
  analysis.profile.brightnessBaseline = 3200.0F;
  analysis.profile.fragilityScore = 0.25F;
  analysis.profile.sourceBakedScore = 0.18F;
  analysis.profile.voicedFrameRatio = 0.85F;

  for (std::size_t index = 0; index < 5U; ++index) {
    autoequalizer::analysis::FrameFeatures frame;
    frame.frameIndex = index;
    frame.sampleOffset = index * 512U;
    frame.sampleCount = 512U;
    frame.startSeconds = static_cast<double>(index) * 0.01;
    frame.endSeconds = frame.startSeconds + 0.02;
    frame.rmsDbfs = -18.0F;
    frame.peakDbfs = -6.0F;
    frame.spectralCentroidHz = 3600.0F;
    frame.lowBandRatio = 0.02F;
    frame.upperMidRatio = 0.20F;
    frame.airRatio = 0.18F;
    frame.spectralFlatness = 0.14F;
    frame.transientness = 0.20F;
    frame.sibilantActivity = 0.11F;
    frame.highFrequencyBalance = 1.10F;
    frame.dynamicRangeDb = 12.0F;
    frame.pitchEstimateHz = 420.0F;
    frame.harmonicConfidence = 0.42F;
    frame.changeScore = 0.02F;
    analysis.frames.push_back(frame);
  }

  return analysis;
}

autoequalizer::analysis::AnalysisResult makeHarshAnalysis() {
  autoequalizer::analysis::AnalysisResult analysis;
  analysis.profile.meanSpectralCentroidHz = 1800.0F;
  analysis.profile.meanLowBandRatio = 0.05F;
  analysis.profile.meanUpperMidRatio = 0.11F;
  analysis.profile.meanAirRatio = 0.05F;
  analysis.profile.meanFlatness = 0.28F;
  analysis.profile.meanSibilantActivity = 0.08F;
  analysis.profile.meanHighFrequencyBalance = 0.62F;
  analysis.profile.brightnessBaseline = 2500.0F;
  analysis.profile.fragilityScore = 0.22F;
  analysis.profile.sourceBakedScore = 0.18F;

  for (std::size_t index = 0; index < 5U; ++index) {
    autoequalizer::analysis::FrameFeatures frame;
    frame.frameIndex = index;
    frame.sampleOffset = index * 512U;
    frame.sampleCount = 512U;
    frame.startSeconds = static_cast<double>(index) * 0.01;
    frame.endSeconds = frame.startSeconds + 0.02;
    frame.rmsDbfs = -14.0F;
    frame.peakDbfs = -3.0F;
    frame.spectralCentroidHz = 2600.0F;
    frame.lowBandRatio = 0.04F;
    frame.upperMidRatio = 0.30F;
    frame.airRatio = 0.07F;
    frame.spectralFlatness = 0.42F;
    frame.transientness = 0.45F;
    frame.sibilantActivity = 0.28F;
    frame.highFrequencyBalance = 1.45F;
    frame.dynamicRangeDb = 9.0F;
    frame.pitchEstimateHz = 0.0F;
    frame.harmonicConfidence = 0.10F;
    frame.changeScore = 0.08F;
    analysis.frames.push_back(frame);
  }

  return analysis;
}

autoequalizer::analysis::AnalysisResult makeMetallicBrightAnalysis() {
  autoequalizer::analysis::AnalysisResult analysis;
  analysis.profile.meanSpectralCentroidHz = 2000.0F;
  analysis.profile.meanLowBandRatio = 0.04F;
  analysis.profile.meanUpperMidRatio = 0.147559F;
  analysis.profile.meanAirRatio = 0.0689456F;
  analysis.profile.meanFlatness = 0.12F;
  analysis.profile.meanSibilantActivity = 0.09F;
  analysis.profile.meanHighFrequencyBalance = 0.90F;
  analysis.profile.brightnessBaseline = 2231.2F;
  analysis.profile.fragilityScore = 0.22F;
  analysis.profile.sourceBakedScore = 0.16F;
  analysis.profile.voicedFrameRatio = 0.80F;

  const std::array<float, 5U> centroids{4800.0F, 2500.0F, 5100.0F, 2200.0F,
                                        4700.0F};
  const std::array<float, 5U> upperMidRatios{0.335524F, 0.353769F, 0.291265F,
                                             0.393635F, 0.667767F};
  const std::array<float, 5U> airRatios{0.392419F, 0.109254F, 0.428257F,
                                        0.088574F, 0.360416F};
  const std::array<float, 5U> flatnessValues{0.00766552F, 0.00704919F,
                                             0.00671581F, 0.0066909F,
                                             0.00687019F};
  const std::array<float, 5U> sibilantValues{0.345061F, 0.290877F, 0.387025F,
                                             0.208911F, 0.204325F};
  const std::array<float, 5U> highFrequencyBalances{4.61409F, 4.00779F, 3.33007F,
                                                    2.12697F, 2.95623F};
  const std::array<float, 5U> pitches{562.5F, 632.812F, 562.5F, 726.562F, 750.0F};
  const std::array<float, 5U> harmonicConfidences{0.0236733F, 0.0197276F,
                                                  0.0295195F, 0.0832213F,
                                                  0.0163861F};

  for (std::size_t index = 0; index < 5U; ++index) {
    autoequalizer::analysis::FrameFeatures frame;
    frame.frameIndex = index;
    frame.sampleOffset = index * 512U;
    frame.sampleCount = 512U;
    frame.startSeconds = static_cast<double>(index) * 0.01;
    frame.endSeconds = frame.startSeconds + 0.02;
    frame.rmsDbfs = -18.0F;
    frame.peakDbfs = -5.0F;
    frame.spectralCentroidHz = centroids[index];
    frame.lowBandRatio = 0.02F;
    frame.upperMidRatio = upperMidRatios[index];
    frame.airRatio = airRatios[index];
    frame.spectralFlatness = flatnessValues[index];
    frame.transientness = 0.22F;
    frame.sibilantActivity = sibilantValues[index];
    frame.highFrequencyBalance = highFrequencyBalances[index];
    frame.dynamicRangeDb = 11.0F;
    frame.pitchEstimateHz = pitches[index];
    frame.harmonicConfidence = harmonicConfidences[index];
    frame.changeScore = 0.05F;
    analysis.frames.push_back(frame);
  }

  return analysis;
}

}  // namespace

TEST_CASE("DecisionEngine protects bright high-register segments") {
  autoequalizer::pipeline::DecisionEngine engine;
  const auto plan = engine.buildPlan(
      makeBrightAnalysis(), autoequalizer::core::ProcessingMode::Normal,
      autoequalizer::core::targetForLoudnessProfile(
          autoequalizer::core::LoudnessProfile::Streaming));

  EXPECT_TRUE(!plan.segments.empty());
  EXPECT_TRUE(plan.segments.front().guardrails.brightProtection);
  EXPECT_TRUE(plan.segments.front().deEssMaxReductionDb < 1.6F);
}

TEST_CASE("DecisionEngine applies stronger correction to harsh segments") {
  autoequalizer::pipeline::DecisionEngine engine;
  const auto plan = engine.buildPlan(
      makeHarshAnalysis(), autoequalizer::core::ProcessingMode::Normal,
      autoequalizer::core::targetForLoudnessProfile(
          autoequalizer::core::LoudnessProfile::Streaming));

  EXPECT_TRUE(!plan.segments.empty());
  EXPECT_TRUE(!plan.segments.front().guardrails.brightProtection);
  EXPECT_TRUE(plan.segments.front().harshCutDb > 1.5F);
  EXPECT_TRUE(plan.segments.front().deEssMaxReductionDb > 2.0F);
}

TEST_CASE("DecisionEngine targets metallic bright segments without mistaking them for safe air") {
  autoequalizer::pipeline::DecisionEngine engine;
  const auto plan = engine.buildPlan(
      makeMetallicBrightAnalysis(), autoequalizer::core::ProcessingMode::Normal,
      autoequalizer::core::targetForLoudnessProfile(
          autoequalizer::core::LoudnessProfile::Stem));

  EXPECT_TRUE(!plan.segments.empty());
  const auto metallicSegment = std::find_if(
      plan.segments.begin(), plan.segments.end(),
      [](const autoequalizer::pipeline::SegmentDecision& segment) {
        return segment.guardrails.metallicCorrectionOverride;
      });
  EXPECT_TRUE(metallicSegment != plan.segments.end());
  EXPECT_TRUE(metallicSegment->harshCutDb >= 0.75F);
  EXPECT_TRUE(metallicSegment->deEssMaxReductionDb >= 0.20F);
  EXPECT_TRUE(metallicSegment->processMix < 0.70F);
  EXPECT_TRUE(metallicSegment->guardrails.limitReachedArtifact);
  EXPECT_TRUE(metallicSegment->rationale.find("blend-limited repair") !=
              std::string::npos);
}

TEST_CASE("Artifact-safe mode lowers metallic repair mix further") {
  autoequalizer::pipeline::DecisionEngine engine;
  const auto normalPlan = engine.buildPlan(
      makeMetallicBrightAnalysis(), autoequalizer::core::ProcessingMode::Normal,
      autoequalizer::core::targetForLoudnessProfile(
          autoequalizer::core::LoudnessProfile::Stem));
  const auto artifactSafePlan = engine.buildPlan(
      makeMetallicBrightAnalysis(),
      autoequalizer::core::ProcessingMode::ArtifactSafe,
      autoequalizer::core::targetForLoudnessProfile(
          autoequalizer::core::LoudnessProfile::Stem));

  const auto normalSegment = std::find_if(
      normalPlan.segments.begin(), normalPlan.segments.end(),
      [](const autoequalizer::pipeline::SegmentDecision& segment) {
        return segment.guardrails.limitReachedArtifact;
      });
  const auto artifactSafeSegment = std::find_if(
      artifactSafePlan.segments.begin(), artifactSafePlan.segments.end(),
      [](const autoequalizer::pipeline::SegmentDecision& segment) {
        return segment.guardrails.limitReachedArtifact;
      });

  EXPECT_TRUE(normalSegment != normalPlan.segments.end());
  EXPECT_TRUE(artifactSafeSegment != artifactSafePlan.segments.end());
  EXPECT_TRUE(artifactSafeSegment->processMix < normalSegment->processMix);
  EXPECT_TRUE(artifactSafeSegment->deEssMaxReductionDb <=
              normalSegment->deEssMaxReductionDb);
}

TEST_CASE("Timestamp-targeted preserve override backs off a local metallic window") {
  autoequalizer::pipeline::AutoEqualizerPipeline pipeline;
  autoequalizer::audio::AudioBuffer buffer(48000, 1U, 4096U);
  for (std::size_t index = 0; index < buffer.frameCount(); ++index) {
    buffer.channel(0)[index] = (index % 32U) < 16U ? 0.12F : -0.12F;
  }

  const auto baseline = pipeline.analyze(
      buffer, autoequalizer::core::ProcessingMode::Normal, false, {},
      autoequalizer::core::targetForLoudnessProfile(
          autoequalizer::core::LoudnessProfile::Stem));
  const auto overridden = pipeline.analyze(
      buffer, autoequalizer::core::ProcessingMode::Normal, false,
      {autoequalizer::core::RangeOverride{0.0, 0.05,
                                   autoequalizer::core::RangeOverridePolicy::Preserve}},
      autoequalizer::core::targetForLoudnessProfile(
          autoequalizer::core::LoudnessProfile::Stem));

  EXPECT_TRUE(!baseline.plan.segments.empty());
  EXPECT_TRUE(!overridden.plan.segments.empty());
  EXPECT_TRUE(overridden.plan.requestedOverrides.size() == 1U);
  EXPECT_TRUE(overridden.plan.segments.front().overridePolicy == "preserve");
  EXPECT_TRUE(overridden.plan.segments.front().processMix <=
              baseline.plan.segments.front().processMix);
  EXPECT_TRUE(overridden.plan.segments.front().deEssMaxReductionDb <=
              baseline.plan.segments.front().deEssMaxReductionDb);
  EXPECT_TRUE(overridden.plan.segments.front().rationale.find(
                  "Timestamp-targeted override applied") != std::string::npos);
}
