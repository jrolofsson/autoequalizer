#include <cmath>
#include <numbers>

#include "analysis/LoudnessMeter.hpp"
#include "audio/AudioBuffer.hpp"
#include "core/Math.hpp"
#include "tests/TestFramework.hpp"

TEST_CASE("LoudnessMeter tracks gain changes consistently") {
  constexpr int sampleRate = 48000;
  constexpr std::size_t frameCount = 48000 * 4;

  autoequalizer::audio::AudioBuffer buffer(sampleRate, 2U, frameCount);
  for (std::size_t index = 0; index < frameCount; ++index) {
    const float leftPhase = 2.0F * std::numbers::pi_v<float> * 220.0F *
                            static_cast<float>(index) /
                            static_cast<float>(sampleRate);
    const float rightPhase = 2.0F * std::numbers::pi_v<float> * 330.0F *
                             static_cast<float>(index) /
                             static_cast<float>(sampleRate);
    buffer.channel(0)[index] = 0.12F * std::sin(leftPhase);
    buffer.channel(1)[index] = 0.10F * std::sin(rightPhase);
  }

  autoequalizer::analysis::LoudnessMeter meter;
  const auto before = meter.measure(buffer);
  buffer.applyGain(autoequalizer::core::dbToLinear(6.0F));
  const auto after = meter.measure(buffer);

  EXPECT_NEAR(after.integratedLufs - before.integratedLufs, 6.0F, 0.4F);
  EXPECT_NEAR(after.truePeakDbtp - before.truePeakDbtp, 6.0F, 0.4F);
}
