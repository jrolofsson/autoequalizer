#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/Result.hpp"
#include "core/Types.hpp"

namespace autoequalizer::cli {

enum class Command {
  Help,
  Analyze,
  Process
};

struct CommandLineOptions {
  Command command{Command::Help};
  std::filesystem::path inputPath;
  std::optional<std::filesystem::path> outputPath;
  std::optional<std::filesystem::path> reportPath;
  std::string suffix{"_autoequalizer"};
  core::ProcessingMode mode{core::ProcessingMode::Normal};
  bool loudnessOnly{};
  core::LoudnessProfile loudnessProfile{core::LoudnessProfile::Streaming};
  std::optional<float> customTargetLufs;
  std::optional<float> customTruePeakDbtp;
  std::vector<core::RangeOverride> rangeOverrides;
};

class CommandLine {
 public:
  [[nodiscard]] static core::Result<CommandLineOptions> parse(int argc,
                                                              char** argv);
  [[nodiscard]] static std::string usage();
};

}  // namespace autoequalizer::cli
