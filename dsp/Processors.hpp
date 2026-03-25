#pragma once

#include <cstddef>
#include <vector>

#include "audio/AudioBuffer.hpp"
#include "core/Math.hpp"

namespace autoequalizer::dsp {

class BiquadFilter {
 public:
  void reset();

  void setHighPass(int sampleRate, float cutoffHz, float q = 0.7071F);
  void setPeaking(int sampleRate, float centerHz, float q, float gainDb);
  void setHighShelf(int sampleRate, float centerHz, float slope, float gainDb);

  [[nodiscard]] float processSample(float input);

 private:
  void setNormalizedCoefficients(float b0,
                                 float b1,
                                 float b2,
                                 float a0,
                                 float a1,
                                 float a2);

  float b0_{1.0F};
  float b1_{0.0F};
  float b2_{0.0F};
  float a1_{0.0F};
  float a2_{0.0F};
  float z1_{0.0F};
  float z2_{0.0F};
};

struct CompressorSettings {
  float thresholdDb{-14.0F};
  float ratio{1.2F};
  float attackMs{12.0F};
  float releaseMs{90.0F};
  float makeupGainDb{0.0F};
};

class Compressor {
 public:
  explicit Compressor(int sampleRate = 48000) : sampleRate_(sampleRate) {}

  void reset();
  void setSampleRate(int sampleRate) { sampleRate_ = sampleRate; }
  void setSettings(const CompressorSettings& settings) { settings_ = settings; }
  [[nodiscard]] float processSample(float input);

 private:
  int sampleRate_{48000};
  CompressorSettings settings_{};
  float smoothedGainDb_{0.0F};
};

struct DeEsserSettings {
  float splitFrequencyHz{5500.0F};
  float threshold{0.08F};
  float maxReductionDb{0.0F};
  float attackMs{3.0F};
  float releaseMs{65.0F};
};

class DeEsser {
 public:
  explicit DeEsser(int sampleRate = 48000);

  void reset();
  void setSampleRate(int sampleRate);
  void setSettings(const DeEsserSettings& settings);
  [[nodiscard]] float processSample(float input);

 private:
  int sampleRate_{48000};
  DeEsserSettings settings_{};
  BiquadFilter detectorHighPass_{};
  float envelope_{0.0F};
};

struct ChainSettings {
  float highPassCutoffHz{35.0F};
  float harshCenterHz{3400.0F};
  float harshQ{2.2F};
  float harshCutDb{0.0F};
  float hfShelfCenterHz{8500.0F};
  float hfSmoothingDb{0.0F};
  DeEsserSettings deEsser{};
  CompressorSettings compressor{};
};

class AdaptiveDspChain {
 public:
  explicit AdaptiveDspChain(int sampleRate = 48000);

  void reset();
  void setSampleRate(int sampleRate);
  void configure(const ChainSettings& settings);
  [[nodiscard]] float processSample(float input);

 private:
  int sampleRate_{48000};
  BiquadFilter highPass_{};
  BiquadFilter harshEq_{};
  BiquadFilter hfShelf_{};
  DeEsser deEsser_{};
  Compressor compressor_{};
};

struct TruePeakLimiterSettings {
  float ceilingDbtp{-1.0F};
  float attackMs{0.5F};
  float releaseMs{80.0F};
  float lookaheadMs{3.0F};
};

class TruePeakLimiter {
 public:
  explicit TruePeakLimiter(int sampleRate = 48000);

  void setSampleRate(int sampleRate);
  void setSettings(const TruePeakLimiterSettings& settings);
  void reset();
  void processInPlace(audio::AudioBuffer& buffer) const;
  [[nodiscard]] audio::AudioBuffer process(const audio::AudioBuffer& input) const;

 private:
  int sampleRate_{48000};
  TruePeakLimiterSettings settings_{};
};

}  // namespace autoequalizer::dsp
