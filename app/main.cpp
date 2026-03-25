#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "audio/AudioFileIO.hpp"
#include "cli/CommandLine.hpp"
#include "pipeline/AutoEqualizerPipeline.hpp"
#include "report/Report.hpp"

namespace {

[[nodiscard]] bool looksLikeReportFile(const std::filesystem::path& path) {
  return path.extension() == ".json";
}

[[nodiscard]] bool looksLikeAudioFile(const std::filesystem::path& path) {
  return autoequalizer::audio::AudioFileIO::isSupportedExtension(path);
}

[[nodiscard]] std::filesystem::path buildOutputPath(
    const autoequalizer::cli::CommandLineOptions& options,
    const std::filesystem::path& inputRoot,
    const std::filesystem::path& inputFile) {
  if (!options.outputPath.has_value()) {
    return {};
  }

  if (looksLikeAudioFile(*options.outputPath) &&
      std::filesystem::is_regular_file(inputFile) &&
      !std::filesystem::is_directory(*options.outputPath)) {
    return *options.outputPath;
  }

  const std::filesystem::path relativeParent =
      std::filesystem::is_directory(inputRoot)
          ? std::filesystem::relative(inputFile.parent_path(), inputRoot)
          : std::filesystem::path{};
  return *options.outputPath / relativeParent /
         (inputFile.stem().string() + options.suffix + inputFile.extension().string());
}

[[nodiscard]] std::filesystem::path buildReportPath(
    const autoequalizer::cli::CommandLineOptions& options,
    const std::filesystem::path& inputRoot,
    const std::filesystem::path& inputFile,
    bool processing) {
  const std::string suffix = processing ? options.suffix : "_analysis";

  if (options.reportPath.has_value() && looksLikeReportFile(*options.reportPath) &&
      std::filesystem::is_regular_file(inputFile) &&
      !std::filesystem::is_directory(*options.reportPath)) {
    return *options.reportPath;
  }

  std::filesystem::path reportRoot;
  if (options.reportPath.has_value()) {
    reportRoot = *options.reportPath;
  } else if (options.outputPath.has_value()) {
    reportRoot = *options.outputPath / "reports";
  } else {
    reportRoot = inputFile.parent_path() / "reports";
  }

  const std::filesystem::path relativeParent =
      std::filesystem::is_directory(inputRoot)
          ? std::filesystem::relative(inputFile.parent_path(), inputRoot)
          : std::filesystem::path{};
  return reportRoot / relativeParent /
         (inputFile.stem().string() + suffix + ".json");
}

[[nodiscard]] std::filesystem::path buildSpectrogramPath(
    const std::filesystem::path& reportPath) {
  return reportPath.parent_path() /
         (reportPath.stem().string() + "_spectrogram.svg");
}

}  // namespace

int main(int argc, char** argv) {
  auto parsed = autoequalizer::cli::CommandLine::parse(argc, argv);
  if (!parsed.ok()) {
    std::cerr << parsed.error().message << "\n\n"
              << autoequalizer::cli::CommandLine::usage();
    return 1;
  }

  const auto options = parsed.value();
  if (options.command == autoequalizer::cli::Command::Help) {
    std::cout << autoequalizer::cli::CommandLine::usage();
    return 0;
  }

  if (!std::filesystem::exists(options.inputPath)) {
    std::cerr << "Input path does not exist: " << options.inputPath << "\n";
    return 1;
  }

  const std::vector<std::filesystem::path> files =
      autoequalizer::audio::AudioFileIO::collectSupportedFiles(options.inputPath);
  if (files.empty()) {
    std::cerr << "No supported audio files were found.\n";
    return 1;
  }

  autoequalizer::pipeline::AutoEqualizerPipeline pipeline;
  autoequalizer::report::ReportBuilder reportBuilder;
  auto loudnessTarget =
      autoequalizer::core::targetForLoudnessProfile(options.loudnessProfile);
  if (options.customTargetLufs.has_value()) {
    loudnessTarget.integratedLufs = *options.customTargetLufs;
  }
  if (options.customTruePeakDbtp.has_value()) {
    loudnessTarget.truePeakDbtp = *options.customTruePeakDbtp;
  }

  int exitCode = 0;
  for (const auto& file : files) {
    auto decoded = autoequalizer::audio::AudioFileIO::read(file);
    if (!decoded.ok()) {
      std::cerr << "Failed to read " << file << ": " << decoded.error().message
                << "\n";
      exitCode = 1;
      continue;
    }

    const bool processing = options.command == autoequalizer::cli::Command::Process;
    autoequalizer::pipeline::PipelineSnapshot snapshot;
    if (processing) {
      snapshot = pipeline.process(std::move(decoded).value(), options.mode,
                                  options.loudnessOnly,
                                  options.rangeOverrides, loudnessTarget);
    } else {
      snapshot = pipeline.analyze(decoded.value(), options.mode,
                                  options.loudnessOnly,
                                  options.rangeOverrides, loudnessTarget);
    }

    std::optional<std::filesystem::path> outputPath;
    std::vector<std::string> errors;
    if (processing && snapshot.processedBuffer.has_value()) {
      outputPath = buildOutputPath(options, options.inputPath, file);
      std::filesystem::create_directories(outputPath->parent_path());
      auto writeResult =
          autoequalizer::audio::AudioFileIO::write(*outputPath, *snapshot.processedBuffer);
      if (!writeResult.ok()) {
        errors.push_back(writeResult.error().message);
        exitCode = 1;
      }
      snapshot.processedBuffer.reset();
    }

    const auto reportPath = buildReportPath(options, options.inputPath, file, processing);
    std::filesystem::create_directories(reportPath.parent_path());
    std::optional<std::filesystem::path> spectrogramPath;
    if (processing && snapshot.analysisAfter.has_value()) {
      spectrogramPath = buildSpectrogramPath(reportPath);
      auto spectrogramWrite = reportBuilder.writeSpectrogramComparison(
          *spectrogramPath, snapshot.analysisBefore.spectrogram,
          snapshot.analysisAfter->spectrogram);
      if (!spectrogramWrite.ok()) {
        errors.push_back(spectrogramWrite.error().message);
        spectrogramPath.reset();
        exitCode = 1;
      }
    }
    auto report = reportBuilder.makeReport(
        file, outputPath, options.mode, std::move(snapshot),
        errors.empty() ? (processing ? "processed" : "analyzed")
                       : "partial_failure",
        spectrogramPath,
        {}, errors);

    auto reportWrite = reportBuilder.writeJson(reportPath, report);
    if (!reportWrite.ok()) {
      std::cerr << "Failed to write report for " << file
                << ": " << reportWrite.error().message << "\n";
      exitCode = 1;
      continue;
    }

    std::cout << (processing ? "Processed " : "Analyzed ") << file << "\n";
    if (outputPath.has_value()) {
      std::cout << "  Output: " << *outputPath << "\n";
    }
    std::cout << "  Report: " << reportPath << "\n";
    if (spectrogramPath.has_value()) {
      std::cout << "  Spectrogram: " << *spectrogramPath << "\n";
    }
  }

  return exitCode;
}
