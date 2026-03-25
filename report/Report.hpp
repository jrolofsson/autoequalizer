#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "analysis/AnalysisEngine.hpp"
#include "core/Result.hpp"
#include "core/Types.hpp"
#include "pipeline/AutoEqualizerPipeline.hpp"

namespace autoequalizer::report {

struct FileReport {
  std::string schemaVersion{"1.2.0"};
  std::string status;
  std::string mode;
  std::string inputPath;
  std::optional<std::string> outputPath;
  std::optional<std::string> spectrogramComparisonPath;
  std::string summary;
  std::vector<std::string> warnings;
  std::vector<std::string> errors;
  std::vector<std::string> guardrails;
  std::string loudnessProfile;
  float targetIntegratedLufs{};
  float targetTruePeakDbtp{};
  bool loudnessTargetInferred{};
  analysis::FileProfile profileBefore;
  std::optional<analysis::FileProfile> profileAfter;
  std::vector<analysis::Hotspot> hotspots;
  std::vector<pipeline::SegmentDecision> segments;
  std::vector<core::RangeOverride> requestedOverrides;
  bool treatedAsFragile{};
  bool treatedAsSourceBaked{};
  float normalizationGainDbApplied{};
};

class ReportBuilder {
 public:
  [[nodiscard]] FileReport makeReport(
      const std::filesystem::path& inputPath,
      const std::optional<std::filesystem::path>& outputPath,
      core::ProcessingMode mode,
      pipeline::PipelineSnapshot snapshot,
      std::string status,
      std::optional<std::filesystem::path> spectrogramComparisonPath = std::nullopt,
      std::vector<std::string> warnings = {},
      std::vector<std::string> errors = {}) const;

  [[nodiscard]] std::string toJson(const FileReport& report) const;
  [[nodiscard]] core::Result<void> writeSpectrogramComparison(
      const std::filesystem::path& path,
      const analysis::Spectrogram& before,
      const analysis::Spectrogram& after) const;
  [[nodiscard]] core::Result<void> writeJson(
      const std::filesystem::path& path,
      const FileReport& report) const;
};

}  // namespace autoequalizer::report
