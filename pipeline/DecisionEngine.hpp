#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "analysis/AnalysisEngine.hpp"
#include "core/Types.hpp"

namespace autoequalizer::pipeline {

struct GuardrailFlags {
  bool brightProtection{};
  bool highRegisterProtection{};
  bool metallicCorrectionOverride{};
  bool limitReachedArtifact{};
  bool fragileMaterial{};
  bool sourceBakedFallback{};
  bool deEsserClamped{};
  bool compressionRelaxed{};
  bool lowConfidenceFallback{};
};

struct SegmentDecision {
  std::size_t frameIndex{};
  std::size_t sampleOffset{};
  std::size_t sampleCount{};
  double startSeconds{};
  double endSeconds{};
  float harshnessScore{};
  float brightnessScore{};
  float fragilityScore{};
  float confidence{};
  float highPassCutoffHz{};
  float harshCenterHz{};
  float harshCutDb{};
  float deEssThreshold{};
  float deEssMaxReductionDb{};
  float hfSmoothingDb{};
  float compressionThresholdDb{};
  float compressionRatio{};
  float processMix{1.0F};
  std::string overridePolicy;
  GuardrailFlags guardrails;
  std::string rationale;
};

struct ProcessingPlan {
  core::ProcessingMode mode{core::ProcessingMode::Normal};
  core::LoudnessTarget loudnessTarget{};
  analysis::FileProfile inputProfile;
  std::vector<SegmentDecision> segments;
  bool fileMarkedFragile{};
  bool fileMarkedSourceBaked{};
  float targetPeakDbfs{-1.0F};
  float finalNormalizationGainDbApplied{};
  std::vector<std::string> fileGuardrails;
  std::vector<core::RangeOverride> requestedOverrides;
};

class DecisionEngine {
 public:
  [[nodiscard]] ProcessingPlan buildPlan(
      const analysis::AnalysisResult& analysis,
      core::ProcessingMode mode,
      core::LoudnessTarget loudnessTarget) const;
};

}  // namespace autoequalizer::pipeline
