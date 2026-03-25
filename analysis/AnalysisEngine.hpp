#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "audio/AudioBuffer.hpp"
#include "core/Types.hpp"

namespace autoequalizer::analysis {

struct FrameFeatures {
  std::size_t frameIndex{};
  std::size_t sampleOffset{};
  std::size_t sampleCount{};
  double startSeconds{};
  double endSeconds{};
  float rmsDbfs{};
  float peakDbfs{};
  float spectralCentroidHz{};
  float lowBandRatio{};
  float upperMidRatio{};
  float airRatio{};
  float spectralFlatness{};
  float transientness{};
  float sibilantActivity{};
  float highFrequencyBalance{};
  float dynamicRangeDb{};
  float pitchEstimateHz{};
  float harmonicConfidence{};
  float changeScore{};
};

struct FileProfile {
  float integratedRmsDbfs{};
  float peakDbfs{};
  float dynamicRangeDb{};
  float meanSpectralCentroidHz{};
  float meanLowBandRatio{};
  float meanUpperMidRatio{};
  float meanAirRatio{};
  float meanFlatness{};
  float meanSibilantActivity{};
  float meanTransientness{};
  float meanHighFrequencyBalance{};
  float voicedFrameRatio{};
  float brightnessBaseline{};
  float harshnessBaseline{};
  float fragilityScore{};
  float sourceBakedScore{};
  float integratedLufs{};
  float loudnessRangeLu{};
  float momentaryMaxLufs{};
  float shortTermMaxLufs{};
  float truePeakDbtp{};
};

struct Hotspot {
  double startSeconds{};
  double endSeconds{};
  std::string label;
  float score{};
  std::string rationale;
};

struct Spectrogram {
  std::size_t timeBinCount{};
  std::size_t frequencyBinCount{};
  float minFrequencyHz{};
  float maxFrequencyHz{};
  double durationSeconds{};
  std::vector<float> logPower;
};

struct AnalysisResult {
  FileProfile profile;
  std::vector<FrameFeatures> frames;
  std::vector<Hotspot> hotspots;
  Spectrogram spectrogram;
};

class AnalysisEngine {
 public:
  explicit AnalysisEngine(std::size_t windowSize = core::kDefaultAnalysisWindow,
                          std::size_t hopSize = core::kDefaultHopSize);

  [[nodiscard]] AnalysisResult analyze(const audio::AudioBuffer& buffer) const;
  [[nodiscard]] std::size_t windowSize() const noexcept { return windowSize_; }
  [[nodiscard]] std::size_t hopSize() const noexcept { return hopSize_; }

 private:
  std::size_t windowSize_{};
  std::size_t hopSize_{};
};

}  // namespace autoequalizer::analysis
