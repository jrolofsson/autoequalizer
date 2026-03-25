#include "cli/CommandLine.hpp"
#include "tests/TestFramework.hpp"

TEST_CASE("CLI parser accepts process command with mode and suffix") {
  const char* argv[] = {"autoequalizer", "process", "input.wav", "--output", "out",
                        "--suffix", "_clean", "--mode", "preserve",
                        "--loudness-profile", "spotify"};

  auto parsed = autoequalizer::cli::CommandLine::parse(11, const_cast<char**>(argv));
  EXPECT_TRUE(parsed.ok());
  EXPECT_TRUE(parsed.value().command == autoequalizer::cli::Command::Process);
  EXPECT_TRUE(parsed.value().suffix == "_clean");
  EXPECT_TRUE(parsed.value().mode == autoequalizer::core::ProcessingMode::Preserve);
  EXPECT_TRUE(parsed.value().loudnessProfile ==
              autoequalizer::core::LoudnessProfile::Spotify);
}

TEST_CASE("CLI parser accepts artifact-safe mode") {
  const char* argv[] = {"autoequalizer", "process", "input.wav", "--output", "out",
                        "--mode", "artifact-safe"};

  auto parsed = autoequalizer::cli::CommandLine::parse(7, const_cast<char**>(argv));
  EXPECT_TRUE(parsed.ok());
  EXPECT_TRUE(parsed.value().mode ==
              autoequalizer::core::ProcessingMode::ArtifactSafe);
}

TEST_CASE("CLI parser accepts timestamp-targeted override ranges") {
  const char* argv[] = {"autoequalizer",        "process",
                        "input.wav",     "--output",
                        "out",           "--override-range",
                        "2:15-2:30@preserve"};

  auto parsed = autoequalizer::cli::CommandLine::parse(7, const_cast<char**>(argv));
  EXPECT_TRUE(parsed.ok());
  EXPECT_TRUE(parsed.value().rangeOverrides.size() == 1U);
  EXPECT_NEAR(parsed.value().rangeOverrides.front().startSeconds, 135.0, 0.001);
  EXPECT_NEAR(parsed.value().rangeOverrides.front().endSeconds, 150.0, 0.001);
  EXPECT_TRUE(parsed.value().rangeOverrides.front().policy ==
              autoequalizer::core::RangeOverridePolicy::Preserve);
}

TEST_CASE("CLI parser accepts stem loudness profile") {
  const char* argv[] = {"autoequalizer", "process", "input.wav", "--output", "out",
                        "--loudness-profile", "stem"};

  auto parsed = autoequalizer::cli::CommandLine::parse(7, const_cast<char**>(argv));
  EXPECT_TRUE(parsed.ok());
  EXPECT_TRUE(parsed.value().loudnessProfile ==
              autoequalizer::core::LoudnessProfile::Stem);
}

TEST_CASE("CLI parser accepts loudness-only mode") {
  const char* argv[] = {"autoequalizer", "process", "input.wav", "--output", "out",
                        "--loudness-only", "--loudness-profile", "stem"};

  auto parsed = autoequalizer::cli::CommandLine::parse(8, const_cast<char**>(argv));
  EXPECT_TRUE(parsed.ok());
  EXPECT_TRUE(parsed.value().loudnessOnly);
  EXPECT_TRUE(parsed.value().loudnessProfile ==
              autoequalizer::core::LoudnessProfile::Stem);
}

TEST_CASE("CLI parser rejects process command without output") {
  const char* argv[] = {"autoequalizer", "process", "input.wav"};

  auto parsed = autoequalizer::cli::CommandLine::parse(3, const_cast<char**>(argv));
  EXPECT_TRUE(!parsed.ok());
}

TEST_CASE("CLI parser accepts custom LUFS target settings") {
  const char* argv[] = {"autoequalizer",     "analyze", "input.wav", "--target-lufs",
                        "-15.5",      "--true-peak-limit", "-1.5"};

  auto parsed = autoequalizer::cli::CommandLine::parse(7, const_cast<char**>(argv));
  EXPECT_TRUE(parsed.ok());
  EXPECT_TRUE(parsed.value().loudnessProfile ==
              autoequalizer::core::LoudnessProfile::Custom);
  EXPECT_NEAR(*parsed.value().customTargetLufs, -15.5F, 0.001F);
  EXPECT_NEAR(*parsed.value().customTruePeakDbtp, -1.5F, 0.001F);
}
