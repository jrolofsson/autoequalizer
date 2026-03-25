#include "cli/CommandLine.hpp"

#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace autoequalizer::cli {

namespace {

[[nodiscard]] core::Result<std::string_view> readValue(int argc,
                                                       char** argv,
                                                       int& index,
                                                       std::string_view flag) {
  if ((index + 1) >= argc) {
    return core::Error{
        "missing_value",
        std::string("Expected a value after ") + std::string(flag) + "."};
  }

  ++index;
  return std::string_view{argv[index]};
}

[[nodiscard]] core::Result<double> parseTimeToken(std::string_view token) {
  if (token.empty()) {
    return core::Error{"invalid_override_range",
                       "Timestamp overrides require non-empty time values."};
  }

  std::vector<std::string_view> parts;
  std::size_t start = 0U;
  for (std::size_t index = 0U; index <= token.size(); ++index) {
    if ((index == token.size()) || (token[index] == ':')) {
      parts.push_back(token.substr(start, index - start));
      start = index + 1U;
    }
  }

  if ((parts.size() < 1U) || (parts.size() > 3U)) {
    return core::Error{
        "invalid_override_range",
        "Timestamp overrides must use seconds, mm:ss, or hh:mm:ss format."};
  }

  auto parseIntegerPart = [](std::string_view value) -> core::Result<int> {
    int parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if ((result.ec != std::errc{}) || (result.ptr != end) || (parsed < 0)) {
      return core::Error{"invalid_override_range",
                         "Timestamp overrides contain an invalid time field."};
    }
    return parsed;
  };

  auto parseFloatingPart = [](std::string_view value) -> core::Result<double> {
    try {
      const double parsed = std::stod(std::string{value});
      if (parsed < 0.0) {
        return core::Error{
            "invalid_override_range",
            "Timestamp overrides cannot use negative time values."};
      }
      return parsed;
    } catch (...) {
      return core::Error{"invalid_override_range",
                         "Timestamp overrides contain an invalid seconds field."};
    }
  };

  double seconds = 0.0;
  if (parts.size() == 1U) {
    auto parsedSeconds = parseFloatingPart(parts[0]);
    if (!parsedSeconds.ok()) {
      return parsedSeconds.error();
    }
    seconds = parsedSeconds.value();
  } else if (parts.size() == 2U) {
    auto minutes = parseIntegerPart(parts[0]);
    auto parsedSeconds = parseFloatingPart(parts[1]);
    if (!minutes.ok()) {
      return minutes.error();
    }
    if (!parsedSeconds.ok()) {
      return parsedSeconds.error();
    }
    seconds = (static_cast<double>(minutes.value()) * 60.0) +
              parsedSeconds.value();
  } else {
    auto hours = parseIntegerPart(parts[0]);
    auto minutes = parseIntegerPart(parts[1]);
    auto parsedSeconds = parseFloatingPart(parts[2]);
    if (!hours.ok()) {
      return hours.error();
    }
    if (!minutes.ok()) {
      return minutes.error();
    }
    if (!parsedSeconds.ok()) {
      return parsedSeconds.error();
    }
    seconds = (static_cast<double>(hours.value()) * 3600.0) +
              (static_cast<double>(minutes.value()) * 60.0) +
              parsedSeconds.value();
  }

  return seconds;
}

[[nodiscard]] core::Result<core::RangeOverride> parseRangeOverride(
    std::string_view value) {
  const std::size_t at = value.find('@');
  const std::size_t dash = value.find('-');
  if ((at == std::string_view::npos) || (dash == std::string_view::npos) ||
      (dash >= at)) {
    return core::Error{
        "invalid_override_range",
        "Override ranges must use <start>-<end>@artifact-safe|preserve|bypass."};
  }

  auto parsedStart = parseTimeToken(value.substr(0U, dash));
  auto parsedEnd = parseTimeToken(value.substr(dash + 1U, at - dash - 1U));
  auto parsedPolicy = core::parseRangeOverridePolicy(value.substr(at + 1U));
  if (!parsedStart.ok()) {
    return parsedStart.error();
  }
  if (!parsedEnd.ok()) {
    return parsedEnd.error();
  }
  if (!parsedPolicy.ok()) {
    return parsedPolicy.error();
  }
  if (parsedEnd.value() <= parsedStart.value()) {
    return core::Error{
        "invalid_override_range",
        "Override ranges must end after they start."};
  }

  return core::RangeOverride{
      parsedStart.value(),
      parsedEnd.value(),
      parsedPolicy.value(),
  };
}

}  // namespace

core::Result<CommandLineOptions> CommandLine::parse(int argc, char** argv) {
  CommandLineOptions options;

  if ((argc <= 1) || (std::string_view{argv[1]} == "--help") ||
      (std::string_view{argv[1]} == "-h")) {
    options.command = Command::Help;
    return options;
  }

  const std::string_view command = argv[1];
  if (command == "analyze") {
    options.command = Command::Analyze;
  } else if (command == "process") {
    options.command = Command::Process;
  } else {
    return core::Error{"invalid_command",
                       "Expected 'analyze' or 'process'."};
  }

  if (argc < 3) {
    return core::Error{"missing_input", "Expected an input file or folder."};
  }

  options.inputPath = argv[2];

  for (int index = 3; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--output") {
      auto value = readValue(argc, argv, index, argument);
      if (!value.ok()) {
        return value.error();
      }
      options.outputPath = std::filesystem::path{std::string{value.value()}};
      continue;
    }

    if (argument == "--report") {
      auto value = readValue(argc, argv, index, argument);
      if (!value.ok()) {
        return value.error();
      }
      options.reportPath = std::filesystem::path{std::string{value.value()}};
      continue;
    }

    if (argument == "--suffix") {
      auto value = readValue(argc, argv, index, argument);
      if (!value.ok()) {
        return value.error();
      }
      options.suffix = std::string{value.value()};
      continue;
    }

    if (argument == "--mode") {
      auto value = readValue(argc, argv, index, argument);
      if (!value.ok()) {
        return value.error();
      }
      auto parsedMode = core::parseProcessingMode(value.value());
      if (!parsedMode.ok()) {
        return parsedMode.error();
      }
      options.mode = parsedMode.value();
      continue;
    }

    if (argument == "--loudness-only") {
      options.loudnessOnly = true;
      continue;
    }

    if (argument == "--loudness-profile") {
      auto value = readValue(argc, argv, index, argument);
      if (!value.ok()) {
        return value.error();
      }
      auto parsedProfile = core::parseLoudnessProfile(value.value());
      if (!parsedProfile.ok()) {
        return parsedProfile.error();
      }
      options.loudnessProfile = parsedProfile.value();
      continue;
    }

    if (argument == "--target-lufs") {
      auto value = readValue(argc, argv, index, argument);
      if (!value.ok()) {
        return value.error();
      }
      options.customTargetLufs = std::stof(std::string{value.value()});
      options.loudnessProfile = core::LoudnessProfile::Custom;
      continue;
    }

    if (argument == "--true-peak-limit") {
      auto value = readValue(argc, argv, index, argument);
      if (!value.ok()) {
        return value.error();
      }
      options.customTruePeakDbtp = std::stof(std::string{value.value()});
      options.loudnessProfile = core::LoudnessProfile::Custom;
      continue;
    }

    if (argument == "--override-range") {
      auto value = readValue(argc, argv, index, argument);
      if (!value.ok()) {
        return value.error();
      }
      auto parsedOverride = parseRangeOverride(value.value());
      if (!parsedOverride.ok()) {
        return parsedOverride.error();
      }
      options.rangeOverrides.push_back(parsedOverride.value());
      continue;
    }

    return core::Error{"unknown_argument",
                       std::string("Unknown argument: ") +
                           std::string(argument)};
  }

  if ((options.command == Command::Process) && !options.outputPath.has_value()) {
    return core::Error{"missing_output",
                       "The process command requires --output."};
  }

  return options;
}

std::string CommandLine::usage() {
  return "Usage:\n"
         "  autoequalizer analyze <input> [--report path] [--mode preserve|artifact-safe|normal|aggressive]\n"
         "                 [--loudness-only]\n"
         "                 [--loudness-profile stem|streaming|spotify|apple|custom]\n"
         "                 [--override-range <start>-<end>@artifact-safe|preserve|bypass]\n"
         "                 [--target-lufs value] [--true-peak-limit value]\n"
         "  autoequalizer process <input> --output path [--report path] [--suffix _autoequalizer]\n"
         "                 [--mode preserve|artifact-safe|normal|aggressive]\n"
         "                 [--loudness-only]\n"
         "                 [--loudness-profile stem|streaming|spotify|apple|custom]\n"
         "                 [--override-range <start>-<end>@artifact-safe|preserve|bypass]\n"
         "                 [--target-lufs value] [--true-peak-limit value]\n";
}

}  // namespace autoequalizer::cli
