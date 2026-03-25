#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "analysis/AnalysisEngine.hpp"
#include "audio/AudioBuffer.hpp"
#include "tests/TestFramework.hpp"

TEST_CASE("AnalysisEngine extracts stable features from a harmonic tone") {
  constexpr int sampleRate = 48000;
  constexpr std::size_t frameCount = 48000;
  constexpr float frequency = 440.0F;

  autoequalizer::audio::AudioBuffer buffer(sampleRate, 1U, frameCount);
  for (std::size_t index = 0; index < frameCount; ++index) {
    const float phase =
        2.0F * std::numbers::pi_v<float> * frequency *
        static_cast<float>(index) / static_cast<float>(sampleRate);
    buffer.channel(0)[index] = 0.55F * std::sin(phase);
  }

  autoequalizer::analysis::AnalysisEngine engine;
  const auto result = engine.analyze(buffer);

  EXPECT_TRUE(!result.frames.empty());
  EXPECT_TRUE(result.profile.meanSpectralCentroidHz > 300.0F);
  EXPECT_TRUE(result.profile.meanSpectralCentroidHz < 1000.0F);
  EXPECT_TRUE(result.profile.meanFlatness < 0.25F);
  EXPECT_TRUE(result.profile.voicedFrameRatio > 0.70F);

  float totalPitch = 0.0F;
  std::size_t voicedFrames = 0U;
  for (const auto& frame : result.frames) {
    if (frame.pitchEstimateHz > 0.0F) {
      totalPitch += frame.pitchEstimateHz;
      ++voicedFrames;
    }
  }

  EXPECT_TRUE(voicedFrames > (result.frames.size() / 2U));
  EXPECT_NEAR(totalPitch / static_cast<float>(voicedFrames), frequency, 8.0F);
}

TEST_CASE("AnalysisEngine tracks a vibrato-heavy harmonic tone") {
  constexpr int sampleRate = 48000;
  constexpr std::size_t frameCount = 48000;
  constexpr float baseFrequency = 440.0F;
  constexpr float vibratoDepthHz = 18.0F;
  constexpr float vibratoRateHz = 5.5F;

  autoequalizer::audio::AudioBuffer buffer(sampleRate, 1U, frameCount);
  float phase = 0.0F;
  for (std::size_t index = 0; index < frameCount; ++index) {
    const float timeSeconds =
        static_cast<float>(index) / static_cast<float>(sampleRate);
    const float instantaneousFrequency =
        baseFrequency +
        (vibratoDepthHz *
         std::sin(2.0F * std::numbers::pi_v<float> * vibratoRateHz *
                  timeSeconds));
    phase += (2.0F * std::numbers::pi_v<float> * instantaneousFrequency) /
             static_cast<float>(sampleRate);
    buffer.channel(0)[index] =
        (0.46F * std::sin(phase)) + (0.09F * std::sin(phase * 2.0F));
  }

  autoequalizer::analysis::AnalysisEngine engine;
  const auto result = engine.analyze(buffer);

  float totalPitch = 0.0F;
  float minPitch = 2000.0F;
  float maxPitch = 0.0F;
  std::size_t voicedFrames = 0U;
  for (const auto& frame : result.frames) {
    if (frame.pitchEstimateHz <= 0.0F) {
      continue;
    }
    totalPitch += frame.pitchEstimateHz;
    minPitch = std::min(minPitch, frame.pitchEstimateHz);
    maxPitch = std::max(maxPitch, frame.pitchEstimateHz);
    ++voicedFrames;
  }

  EXPECT_TRUE(voicedFrames > ((result.frames.size() * 3U) / 5U));
  EXPECT_NEAR(totalPitch / static_cast<float>(voicedFrames), baseFrequency, 15.0F);
  EXPECT_TRUE((maxPitch - minPitch) > 8.0F);
}

TEST_CASE("AnalysisEngine retains voiced pitch on a breathy vocal-like tone") {
  constexpr int sampleRate = 48000;
  constexpr std::size_t frameCount = 48000;
  constexpr float frequency = 262.0F;

  autoequalizer::audio::AudioBuffer buffer(sampleRate, 1U, frameCount);
  std::uint32_t state = 0x13579BDFu;
  auto nextNoise = [&]() {
    state = (1664525u * state) + 1013904223u;
    return (static_cast<float>((state >> 8U) & 0x00FFFFFFu) /
                static_cast<float>(0x00FFFFFFu)) *
               2.0F -
           1.0F;
  };

  for (std::size_t index = 0; index < frameCount; ++index) {
    const float timeSeconds =
        static_cast<float>(index) / static_cast<float>(sampleRate);
    const float amplitude =
        0.32F + (0.08F * std::sin(2.0F * std::numbers::pi_v<float> * 2.2F *
                                  timeSeconds));
    const float harmonic =
        amplitude * std::sin(2.0F * std::numbers::pi_v<float> * frequency *
                             timeSeconds);
    const float airyNoise = 0.035F * nextNoise();
    buffer.channel(0)[index] = harmonic + airyNoise;
  }

  autoequalizer::analysis::AnalysisEngine engine;
  const auto result = engine.analyze(buffer);

  std::vector<float> voicedPitches;
  float totalConfidence = 0.0F;
  for (const auto& frame : result.frames) {
    if (frame.pitchEstimateHz > 0.0F) {
      voicedPitches.push_back(frame.pitchEstimateHz);
      totalConfidence += frame.harmonicConfidence;
    }
  }

  EXPECT_TRUE(!voicedPitches.empty());
  EXPECT_TRUE(voicedPitches.size() > (result.frames.size() / 2U));

  float totalPitch = 0.0F;
  for (const float pitch : voicedPitches) {
    totalPitch += pitch;
  }

  EXPECT_NEAR(totalPitch / static_cast<float>(voicedPitches.size()), frequency,
              12.0F);
  EXPECT_TRUE((totalConfidence / static_cast<float>(voicedPitches.size())) > 0.18F);
}
