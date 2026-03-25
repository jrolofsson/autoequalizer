#include <cmath>

#include "core/Math.hpp"
#include "dsp/Processors.hpp"
#include "tests/TestFramework.hpp"

TEST_CASE("High-pass filter removes DC-heavy content") {
  autoequalizer::dsp::BiquadFilter filter;
  filter.setHighPass(48000, 120.0F);

  float accumulator = 0.0F;
  for (int index = 0; index < 4000; ++index) {
    accumulator += std::abs(filter.processSample(1.0F));
  }

  EXPECT_TRUE((accumulator / 4000.0F) < 0.15F);
}

TEST_CASE("Compressor reduces hot signal level") {
  autoequalizer::dsp::Compressor compressor(48000);
  compressor.setSettings({-18.0F, 2.0F, 5.0F, 50.0F, 0.0F});

  float outputPeak = 0.0F;
  for (int index = 0; index < 4000; ++index) {
    outputPeak = std::max(outputPeak, std::abs(compressor.processSample(0.85F)));
  }

  EXPECT_TRUE(outputPeak < 0.85F);
}

TEST_CASE("DeEsser attenuates intense high-frequency material") {
  autoequalizer::dsp::DeEsser deEsser(48000);
  autoequalizer::dsp::DeEsserSettings settings;
  settings.threshold = 0.02F;
  settings.maxReductionDb = 6.0F;
  deEsser.setSettings(settings);

  float inputRms = 0.0F;
  float outputRms = 0.0F;
  for (int index = 0; index < 4000; ++index) {
    const float input = (index % 2 == 0) ? 0.6F : -0.6F;
    inputRms += input * input;
    const float output = deEsser.processSample(input);
    outputRms += output * output;
  }

  inputRms = std::sqrt(inputRms / 4000.0F);
  outputRms = std::sqrt(outputRms / 4000.0F);
  EXPECT_TRUE(outputRms < inputRms);
}

