#include <cmath>
#include <numbers>

#include "audio/AudioBuffer.hpp"
#include "core/Types.hpp"
#include "pipeline/MasteringStage.hpp"
#include "tests/TestFramework.hpp"

TEST_CASE("MasteringStage targets streaming loudness and true peak safely") {
  constexpr int sampleRate = 48000;
  constexpr std::size_t frameCount = 48000 * 6;

  autoequalizer::audio::AudioBuffer buffer(sampleRate, 2U, frameCount);
  for (std::size_t index = 0; index < frameCount; ++index) {
    const float lowPhase = 2.0F * std::numbers::pi_v<float> * 110.0F *
                           static_cast<float>(index) /
                           static_cast<float>(sampleRate);
    const float highPhase = 2.0F * std::numbers::pi_v<float> * 6200.0F *
                            static_cast<float>(index) /
                            static_cast<float>(sampleRate);
    buffer.channel(0)[index] =
        (0.09F * std::sin(lowPhase)) + (0.04F * std::sin(highPhase));
    buffer.channel(1)[index] =
        (0.08F * std::sin(lowPhase * 1.1F)) + (0.03F * std::sin(highPhase));
  }

  autoequalizer::pipeline::MasteringStage mastering;
  autoequalizer::pipeline::MasteringSummary summary;
  const auto output = mastering.apply(
      buffer,
      autoequalizer::core::limitsForMode(autoequalizer::core::ProcessingMode::Aggressive),
      autoequalizer::core::targetForLoudnessProfile(
          autoequalizer::core::LoudnessProfile::Streaming),
      false, summary);

  EXPECT_TRUE(output.frameCount() == buffer.frameCount());
  EXPECT_TRUE(summary.after.truePeakDbtp <= -0.90F);
  EXPECT_TRUE(summary.after.integratedLufs <= -13.4F);
  EXPECT_TRUE(summary.after.integratedLufs >= -15.2F);
}
