#pragma once

#include <optional>
#include <vector>

#include "analysis/AnalysisEngine.hpp"
#include "audio/AudioBuffer.hpp"
#include "core/Types.hpp"
#include "pipeline/DecisionEngine.hpp"
#include "pipeline/MasteringStage.hpp"

namespace autoequalizer::pipeline {

struct PipelineSnapshot {
  analysis::AnalysisResult analysisBefore;
  std::optional<analysis::AnalysisResult> analysisAfter;
  ProcessingPlan plan;
  std::optional<audio::AudioBuffer> processedBuffer;
  std::vector<std::string> warnings;
};

class AutoEqualizerPipeline {
 public:
  PipelineSnapshot analyze(const audio::AudioBuffer& input,
                           core::ProcessingMode mode,
                           bool loudnessOnly = false,
                           const std::vector<core::RangeOverride>& rangeOverrides = {},
                           core::LoudnessTarget loudnessTarget =
                               core::targetForLoudnessProfile(
                                   core::LoudnessProfile::Streaming)) const;
  PipelineSnapshot process(audio::AudioBuffer input,
                           core::ProcessingMode mode,
                           bool loudnessOnly = false,
                           const std::vector<core::RangeOverride>& rangeOverrides = {},
                           core::LoudnessTarget loudnessTarget =
                               core::targetForLoudnessProfile(
                                   core::LoudnessProfile::Streaming)) const;

 private:
  void applyLoudnessOnly(ProcessingPlan& plan) const;
  void applyRangeOverrides(
      ProcessingPlan& plan,
      const std::vector<core::RangeOverride>& rangeOverrides) const;
  void applyPlan(audio::AudioBuffer& buffer, ProcessingPlan& plan) const;

  analysis::AnalysisEngine analysisEngine_{};
  DecisionEngine decisionEngine_{};
  MasteringStage masteringStage_{};
};

}  // namespace autoequalizer::pipeline
