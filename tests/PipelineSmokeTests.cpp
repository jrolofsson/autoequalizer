#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>

#include "audio/AudioBuffer.hpp"
#include "audio/AudioFileIO.hpp"
#include "pipeline/AutoEqualizerPipeline.hpp"
#include "report/Report.hpp"
#include "tests/TestFramework.hpp"

TEST_CASE("AudioFileIO round-trips a WAV file") {
  constexpr int sampleRate = 48000;
  constexpr std::size_t frameCount = 4096;

  autoequalizer::audio::AudioBuffer buffer(sampleRate, 1U, frameCount);
  for (std::size_t index = 0; index < frameCount; ++index) {
    const float phase = 2.0F * std::numbers::pi_v<float> * 330.0F *
                        static_cast<float>(index) /
                        static_cast<float>(sampleRate);
    buffer.channel(0)[index] = 0.4F * std::sin(phase);
  }

  const std::filesystem::path outputPath = "tmp/test_roundtrip.wav";
  std::filesystem::create_directories(outputPath.parent_path());

  auto writeResult = autoequalizer::audio::AudioFileIO::write(outputPath, buffer);
  EXPECT_TRUE(writeResult.ok());

  auto readResult = autoequalizer::audio::AudioFileIO::read(outputPath);
  EXPECT_TRUE(readResult.ok());
  EXPECT_TRUE(readResult.value().sampleRate() == sampleRate);
  EXPECT_TRUE(readResult.value().frameCount() == frameCount);

  std::filesystem::remove(outputPath);
}

TEST_CASE("AutoEqualizerPipeline processes a synthetic harsh signal") {
  constexpr int sampleRate = 48000;
  constexpr std::size_t frameCount = 8192;

  autoequalizer::audio::AudioBuffer buffer(sampleRate, 1U, frameCount);
  for (std::size_t index = 0; index < frameCount; ++index) {
    const float lowPhase = 2.0F * std::numbers::pi_v<float> * 180.0F *
                           static_cast<float>(index) /
                           static_cast<float>(sampleRate);
    const float highPhase = 2.0F * std::numbers::pi_v<float> * 6500.0F *
                            static_cast<float>(index) /
                            static_cast<float>(sampleRate);
    buffer.channel(0)[index] =
        (0.35F * std::sin(lowPhase)) + (0.18F * std::sin(highPhase));
  }

  autoequalizer::pipeline::AutoEqualizerPipeline pipeline;
  const auto snapshot =
      pipeline.process(buffer, autoequalizer::core::ProcessingMode::Normal);

  EXPECT_TRUE(snapshot.processedBuffer.has_value());
  EXPECT_TRUE(snapshot.analysisAfter.has_value());
  EXPECT_TRUE(!snapshot.plan.segments.empty());

  float difference = 0.0F;
  for (std::size_t index = 0; index < frameCount; ++index) {
    difference += std::abs(snapshot.processedBuffer->channel(0)[index] -
                           buffer.channel(0)[index]);
  }

  EXPECT_TRUE(difference > 0.1F);
}

TEST_CASE("AutoEqualizerPipeline loudness-only mode bypasses corrective DSP") {
  constexpr int sampleRate = 48000;
  constexpr std::size_t frameCount = 8192;

  autoequalizer::audio::AudioBuffer buffer(sampleRate, 1U, frameCount);
  for (std::size_t index = 0; index < frameCount; ++index) {
    const float lowPhase = 2.0F * std::numbers::pi_v<float> * 220.0F *
                           static_cast<float>(index) /
                           static_cast<float>(sampleRate);
    const float highPhase = 2.0F * std::numbers::pi_v<float> * 5100.0F *
                            static_cast<float>(index) /
                            static_cast<float>(sampleRate);
    buffer.channel(0)[index] =
        (0.025F * std::sin(lowPhase)) + (0.012F * std::sin(highPhase));
  }

  autoequalizer::pipeline::AutoEqualizerPipeline pipeline;
  const auto snapshot = pipeline.process(
      buffer, autoequalizer::core::ProcessingMode::Normal, true, {},
      autoequalizer::core::targetForLoudnessProfile(autoequalizer::core::LoudnessProfile::Stem));

  EXPECT_TRUE(snapshot.processedBuffer.has_value());
  EXPECT_TRUE(snapshot.analysisAfter.has_value());
  EXPECT_TRUE(!snapshot.plan.segments.empty());

  for (const auto& segment : snapshot.plan.segments) {
    EXPECT_NEAR(segment.processMix, 0.0F, 1.0e-6F);
    EXPECT_NEAR(segment.harshCutDb, 0.0F, 1.0e-6F);
    EXPECT_NEAR(segment.deEssMaxReductionDb, 0.0F, 1.0e-6F);
    EXPECT_NEAR(segment.hfSmoothingDb, 0.0F, 1.0e-6F);
    EXPECT_NEAR(segment.compressionRatio, 1.0F, 1.0e-6F);
  }

  EXPECT_NEAR(snapshot.analysisAfter->profile.meanSpectralCentroidHz,
              snapshot.analysisBefore.profile.meanSpectralCentroidHz, 30.0F);
  EXPECT_NEAR(snapshot.analysisAfter->profile.meanFlatness,
              snapshot.analysisBefore.profile.meanFlatness, 0.02F);
}

TEST_CASE("AutoEqualizerPipeline protects a falsetto-like sibilant vocal") {
  constexpr int sampleRate = 48000;
  constexpr std::size_t frameCount = 48000;
  constexpr float baseFrequency = 720.0F;

  autoequalizer::audio::AudioBuffer buffer(sampleRate, 1U, frameCount);
  float phase = 0.0F;
  for (std::size_t index = 0; index < frameCount; ++index) {
    const float timeSeconds =
        static_cast<float>(index) / static_cast<float>(sampleRate);
    const float instantaneousFrequency =
        baseFrequency +
        (12.0F * std::sin(2.0F * std::numbers::pi_v<float> * 5.8F *
                          timeSeconds));
    phase += (2.0F * std::numbers::pi_v<float> * instantaneousFrequency) /
             static_cast<float>(sampleRate);

    const float harmonicBody =
        (0.24F * std::sin(phase)) + (0.08F * std::sin(phase * 2.0F));
    const float sibilantGate =
        (std::fmod(timeSeconds, 0.14F) < 0.025F) ? 1.0F : 0.20F;
    const float sibilance =
        sibilantGate *
        ((0.026F * std::sin(2.0F * std::numbers::pi_v<float> * 6500.0F *
                            timeSeconds)) +
         (0.018F * std::sin(2.0F * std::numbers::pi_v<float> * 8200.0F *
                            timeSeconds)));
    buffer.channel(0)[index] = harmonicBody + sibilance;
  }

  autoequalizer::pipeline::AutoEqualizerPipeline pipeline;
  const auto snapshot = pipeline.analyze(
      buffer, autoequalizer::core::ProcessingMode::Normal, false, {},
      autoequalizer::core::targetForLoudnessProfile(autoequalizer::core::LoudnessProfile::Stem));

  EXPECT_TRUE(!snapshot.analysisBefore.frames.empty());
  EXPECT_TRUE(!snapshot.plan.segments.empty());

  float totalPitch = 0.0F;
  std::size_t voicedFrames = 0U;
  for (const auto& frame : snapshot.analysisBefore.frames) {
    if (frame.pitchEstimateHz > 0.0F) {
      totalPitch += frame.pitchEstimateHz;
      ++voicedFrames;
    }
  }

  EXPECT_TRUE(voicedFrames > (snapshot.analysisBefore.frames.size() / 2U));
  EXPECT_NEAR(totalPitch / static_cast<float>(voicedFrames), baseFrequency, 24.0F);

  const auto protectedSegment = std::find_if(
      snapshot.plan.segments.begin(), snapshot.plan.segments.end(),
      [](const autoequalizer::pipeline::SegmentDecision& segment) {
        return segment.guardrails.highRegisterProtection &&
               segment.guardrails.deEsserClamped;
      });

  EXPECT_TRUE(protectedSegment != snapshot.plan.segments.end());
  EXPECT_TRUE(protectedSegment->deEssMaxReductionDb < 1.8F);
  EXPECT_TRUE(protectedSegment->hfSmoothingDb < 0.9F);
}

TEST_CASE("ReportBuilder writes a spectrogram comparison for processed audio") {
  constexpr int sampleRate = 48000;
  constexpr std::size_t frameCount = 8192;

  autoequalizer::audio::AudioBuffer buffer(sampleRate, 1U, frameCount);
  for (std::size_t index = 0; index < frameCount; ++index) {
    const float lowPhase = 2.0F * std::numbers::pi_v<float> * 180.0F *
                           static_cast<float>(index) /
                           static_cast<float>(sampleRate);
    const float highPhase = 2.0F * std::numbers::pi_v<float> * 6200.0F *
                            static_cast<float>(index) /
                            static_cast<float>(sampleRate);
    buffer.channel(0)[index] =
        (0.28F * std::sin(lowPhase)) + (0.12F * std::sin(highPhase));
  }

  autoequalizer::pipeline::AutoEqualizerPipeline pipeline;
  auto snapshot =
      pipeline.process(buffer, autoequalizer::core::ProcessingMode::Normal);

  EXPECT_TRUE(snapshot.analysisAfter.has_value());

  autoequalizer::report::ReportBuilder reportBuilder;
  const std::filesystem::path svgPath = "tmp/test_report_spectrogram.svg";
  auto writeResult = reportBuilder.writeSpectrogramComparison(
      svgPath, snapshot.analysisBefore.spectrogram,
      snapshot.analysisAfter->spectrogram);
  EXPECT_TRUE(writeResult.ok());

  auto report = reportBuilder.makeReport(
      "input.wav", std::filesystem::path{"output.wav"},
      autoequalizer::core::ProcessingMode::Normal, std::move(snapshot), "processed",
      svgPath);
  const std::string json = reportBuilder.toJson(report);

  std::ifstream input(svgPath);
  EXPECT_TRUE(input.is_open());
  const std::string svg((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
  EXPECT_TRUE(svg.find("<svg") != std::string::npos);
  EXPECT_TRUE(svg.find("Input") != std::string::npos);
  EXPECT_TRUE(svg.find("Output") != std::string::npos);
  EXPECT_TRUE(svg.find("Delta") != std::string::npos);
  EXPECT_TRUE(json.find("\"spectrogramComparisonPath\":\"tmp/test_report_spectrogram.svg\"") !=
              std::string::npos);

  std::filesystem::remove(svgPath);
}
