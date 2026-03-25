#include "report/Report.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <ostream>
#include <sstream>

#include "core/Math.hpp"

namespace autoequalizer::report {

namespace {

[[nodiscard]] std::string escapeJson(const std::string& value) {
  std::ostringstream output;
  for (const char character : value) {
    switch (character) {
      case '\\':
        output << "\\\\";
        break;
      case '"':
        output << "\\\"";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        output << character;
        break;
    }
  }
  return output.str();
}

[[nodiscard]] std::string quote(const std::string& value) {
  return "\"" + escapeJson(value) + "\"";
}

[[nodiscard]] std::string escapeXml(const std::string& value) {
  std::ostringstream output;
  for (const char character : value) {
    switch (character) {
      case '&':
        output << "&amp;";
        break;
      case '<':
        output << "&lt;";
        break;
      case '>':
        output << "&gt;";
        break;
      case '"':
        output << "&quot;";
        break;
      case '\'':
        output << "&apos;";
        break;
      default:
        output << character;
        break;
    }
  }
  return output.str();
}

struct Rgb {
  int red{};
  int green{};
  int blue{};
};

[[nodiscard]] Rgb mixRgb(const Rgb& left, const Rgb& right, float amount) {
  const float clamped = std::clamp(amount, 0.0F, 1.0F);
  return Rgb{
      static_cast<int>(std::lround(
          static_cast<float>(left.red) +
          ((static_cast<float>(right.red - left.red)) * clamped))),
      static_cast<int>(std::lround(
          static_cast<float>(left.green) +
          ((static_cast<float>(right.green - left.green)) * clamped))),
      static_cast<int>(std::lround(
          static_cast<float>(left.blue) +
          ((static_cast<float>(right.blue - left.blue)) * clamped))),
  };
}

[[nodiscard]] std::string rgbString(const Rgb& color) {
  return "rgb(" + std::to_string(color.red) + "," +
         std::to_string(color.green) + "," + std::to_string(color.blue) + ")";
}

[[nodiscard]] float normalizeValue(float value, float minimum, float maximum) {
  if ((maximum - minimum) <= core::kEpsilon) {
    return 0.5F;
  }
  return std::clamp((value - minimum) / (maximum - minimum), 0.0F, 1.0F);
}

[[nodiscard]] Rgb spectrogramColor(float value) {
  const Rgb low{7, 11, 24};
  const Rgb midLow{27, 83, 163};
  const Rgb mid{25, 156, 152};
  const Rgb high{246, 195, 85};
  const float clamped = std::clamp(value, 0.0F, 1.0F);
  if (clamped < 0.34F) {
    return mixRgb(low, midLow, clamped / 0.34F);
  }
  if (clamped < 0.68F) {
    return mixRgb(midLow, mid, (clamped - 0.34F) / 0.34F);
  }
  return mixRgb(mid, high, (clamped - 0.68F) / 0.32F);
}

[[nodiscard]] Rgb deltaColor(float value) {
  const Rgb reduced{232, 123, 58};
  const Rgb neutral{27, 32, 44};
  const Rgb boosted{72, 164, 255};
  const float clamped = std::clamp(value, -1.0F, 1.0F);
  if (clamped < 0.0F) {
    return mixRgb(neutral, reduced, -clamped);
  }
  return mixRgb(neutral, boosted, clamped);
}

[[nodiscard]] std::string formatSeconds(double seconds) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(seconds >= 100.0 ? 0 : 1)
         << seconds << "s";
  return output.str();
}

[[nodiscard]] std::string formatFrequency(float frequencyHz) {
  std::ostringstream output;
  if (frequencyHz >= 1000.0F) {
    output << std::fixed << std::setprecision(frequencyHz >= 10000.0F ? 0 : 1)
           << (frequencyHz / 1000.0F) << " kHz";
  } else {
    output << std::fixed << std::setprecision(0) << frequencyHz << " Hz";
  }
  return output.str();
}

[[nodiscard]] float spectrogramYForFrequency(
    const analysis::Spectrogram& spectrogram,
    float frequencyHz,
    float originY,
    float cellHeight) {
  if ((spectrogram.frequencyBinCount <= 1U) ||
      (spectrogram.maxFrequencyHz <= core::kEpsilon)) {
    return originY;
  }

  const float clamped = std::clamp(frequencyHz, spectrogram.minFrequencyHz,
                                   spectrogram.maxFrequencyHz);
  float fraction = 0.0F;
  if (spectrogram.minFrequencyHz > core::kEpsilon) {
    fraction =
        std::log(clamped / spectrogram.minFrequencyHz) /
        std::log(spectrogram.maxFrequencyHz / spectrogram.minFrequencyHz);
  } else {
    fraction = clamped / spectrogram.maxFrequencyHz;
  }

  const float panelHeight =
      static_cast<float>(spectrogram.frequencyBinCount) * cellHeight;
  return originY + ((1.0F - fraction) * panelHeight);
}

void appendSpectrogramPanel(std::ostream& svg,
                            const analysis::Spectrogram& spectrogram,
                            float valueMinimum,
                            float valueMaximum,
                            float originX,
                            float originY,
                            float cellWidth,
                            float cellHeight) {
  for (std::size_t timeBin = 0; timeBin < spectrogram.timeBinCount; ++timeBin) {
    for (std::size_t frequencyBin = 0; frequencyBin < spectrogram.frequencyBinCount;
         ++frequencyBin) {
      const float value =
          spectrogram.logPower[(timeBin * spectrogram.frequencyBinCount) +
                               frequencyBin];
      const std::string fill = rgbString(
          spectrogramColor(normalizeValue(value, valueMinimum, valueMaximum)));
      const float x = originX + (static_cast<float>(timeBin) * cellWidth);
      const float y =
          originY +
          (static_cast<float>(spectrogram.frequencyBinCount - 1U - frequencyBin) *
           cellHeight);
      svg << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\""
          << cellWidth << "\" height=\"" << cellHeight << "\" fill=\"" << fill
          << "\" />\n";
    }
  }
}

void appendDeltaPanel(std::ostream& svg,
                      const analysis::Spectrogram& before,
                      const analysis::Spectrogram& after,
                      float deltaExtent,
                      float originX,
                      float originY,
                      float cellWidth,
                      float cellHeight) {
  const std::size_t timeBins =
      std::min(before.timeBinCount, after.timeBinCount);
  const std::size_t frequencyBins =
      std::min(before.frequencyBinCount, after.frequencyBinCount);
  const float extent = std::max(deltaExtent, 1.0F);

  for (std::size_t timeBin = 0; timeBin < timeBins; ++timeBin) {
    for (std::size_t frequencyBin = 0; frequencyBin < frequencyBins;
         ++frequencyBin) {
      const std::size_t beforeIndex =
          (timeBin * before.frequencyBinCount) + frequencyBin;
      const std::size_t afterIndex =
          (timeBin * after.frequencyBinCount) + frequencyBin;
      const float delta =
          after.logPower[afterIndex] - before.logPower[beforeIndex];
      const std::string fill = rgbString(deltaColor(delta / extent));
      const float x = originX + (static_cast<float>(timeBin) * cellWidth);
      const float y =
          originY +
          (static_cast<float>(frequencyBins - 1U - frequencyBin) * cellHeight);
      svg << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\""
          << cellWidth << "\" height=\"" << cellHeight << "\" fill=\"" << fill
          << "\" />\n";
    }
  }
}

void appendFileProfile(std::ostream& json,
                       const analysis::FileProfile& profile) {
  json << "{"
       << "\"integratedRmsDbfs\":" << profile.integratedRmsDbfs << ","
       << "\"peakDbfs\":" << profile.peakDbfs << ","
       << "\"dynamicRangeDb\":" << profile.dynamicRangeDb << ","
       << "\"meanSpectralCentroidHz\":" << profile.meanSpectralCentroidHz << ","
       << "\"meanLowBandRatio\":" << profile.meanLowBandRatio << ","
       << "\"meanUpperMidRatio\":" << profile.meanUpperMidRatio << ","
       << "\"meanAirRatio\":" << profile.meanAirRatio << ","
       << "\"meanFlatness\":" << profile.meanFlatness << ","
       << "\"meanSibilantActivity\":" << profile.meanSibilantActivity << ","
       << "\"meanTransientness\":" << profile.meanTransientness << ","
       << "\"meanHighFrequencyBalance\":" << profile.meanHighFrequencyBalance
       << ","
       << "\"voicedFrameRatio\":" << profile.voicedFrameRatio << ","
       << "\"brightnessBaseline\":" << profile.brightnessBaseline << ","
       << "\"harshnessBaseline\":" << profile.harshnessBaseline << ","
       << "\"fragilityScore\":" << profile.fragilityScore << ","
       << "\"sourceBakedScore\":" << profile.sourceBakedScore << ","
       << "\"integratedLufs\":" << profile.integratedLufs << ","
       << "\"loudnessRangeLu\":" << profile.loudnessRangeLu << ","
       << "\"momentaryMaxLufs\":" << profile.momentaryMaxLufs << ","
       << "\"shortTermMaxLufs\":" << profile.shortTermMaxLufs << ","
       << "\"truePeakDbtp\":" << profile.truePeakDbtp << "}";
}

void appendStringArray(std::ostream& json,
                       const std::vector<std::string>& values) {
  json << "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0U) {
      json << ",";
    }
    json << quote(values[index]);
  }
  json << "]";
}

void appendHotspots(std::ostream& json,
                    const std::vector<analysis::Hotspot>& hotspots) {
  json << "[";
  for (std::size_t index = 0; index < hotspots.size(); ++index) {
    const auto& hotspot = hotspots[index];
    if (index > 0U) {
      json << ",";
    }

    json << "{"
         << "\"startSeconds\":" << hotspot.startSeconds << ","
         << "\"endSeconds\":" << hotspot.endSeconds << ","
         << "\"label\":" << quote(hotspot.label) << ","
         << "\"score\":" << hotspot.score << ","
         << "\"rationale\":" << quote(hotspot.rationale) << "}";
  }
  json << "]";
}

void appendRequestedOverrides(std::ostream& json,
                              const std::vector<core::RangeOverride>& overrides) {
  json << "[";
  for (std::size_t index = 0; index < overrides.size(); ++index) {
    const auto& rangeOverride = overrides[index];
    if (index > 0U) {
      json << ",";
    }

    json << "{"
         << "\"startSeconds\":" << rangeOverride.startSeconds << ","
         << "\"endSeconds\":" << rangeOverride.endSeconds << ","
         << "\"policy\":" << quote(core::toString(rangeOverride.policy)) << "}";
  }
  json << "]";
}

void appendSegments(std::ostream& json,
                    const std::vector<pipeline::SegmentDecision>& segments) {
  json << "[";
  for (std::size_t index = 0; index < segments.size(); ++index) {
    const auto& segment = segments[index];
    if (index > 0U) {
      json << ",";
    }

    json << "{"
         << "\"frameIndex\":" << segment.frameIndex << ","
         << "\"startSeconds\":" << segment.startSeconds << ","
         << "\"endSeconds\":" << segment.endSeconds << ","
         << "\"harshnessScore\":" << segment.harshnessScore << ","
         << "\"brightnessScore\":" << segment.brightnessScore << ","
         << "\"fragilityScore\":" << segment.fragilityScore << ","
         << "\"confidence\":" << segment.confidence << ","
         << "\"highPassCutoffHz\":" << segment.highPassCutoffHz << ","
         << "\"harshCenterHz\":" << segment.harshCenterHz << ","
         << "\"harshCutDb\":" << segment.harshCutDb << ","
         << "\"deEssThreshold\":" << segment.deEssThreshold << ","
         << "\"deEssMaxReductionDb\":" << segment.deEssMaxReductionDb << ","
         << "\"hfSmoothingDb\":" << segment.hfSmoothingDb << ","
         << "\"compressionThresholdDb\":" << segment.compressionThresholdDb
         << ","
         << "\"compressionRatio\":" << segment.compressionRatio << ","
         << "\"processMix\":" << segment.processMix << ","
         << "\"overridePolicy\":";
    if (!segment.overridePolicy.empty()) {
      json << quote(segment.overridePolicy);
    } else {
      json << "null";
    }
    json << ","
         << "\"rationale\":" << quote(segment.rationale) << ","
         << "\"guardrails\":{"
         << "\"brightProtection\":"
         << (segment.guardrails.brightProtection ? "true" : "false") << ","
         << "\"highRegisterProtection\":"
         << (segment.guardrails.highRegisterProtection ? "true" : "false")
         << ","
         << "\"metallicCorrectionOverride\":"
         << (segment.guardrails.metallicCorrectionOverride ? "true" : "false")
         << ","
         << "\"limitReachedArtifact\":"
         << (segment.guardrails.limitReachedArtifact ? "true" : "false") << ","
         << "\"fragileMaterial\":"
         << (segment.guardrails.fragileMaterial ? "true" : "false") << ","
         << "\"sourceBakedFallback\":"
         << (segment.guardrails.sourceBakedFallback ? "true" : "false") << ","
         << "\"deEsserClamped\":"
         << (segment.guardrails.deEsserClamped ? "true" : "false") << ","
         << "\"compressionRelaxed\":"
         << (segment.guardrails.compressionRelaxed ? "true" : "false") << ","
         << "\"lowConfidenceFallback\":"
         << (segment.guardrails.lowConfidenceFallback ? "true" : "false")
         << "}}";
  }
  json << "]";
}

void appendReportJson(std::ostream& json, const FileReport& report) {
  json << "{";
  json << "\"schemaVersion\":" << quote(report.schemaVersion) << ",";
  json << "\"status\":" << quote(report.status) << ",";
  json << "\"mode\":" << quote(report.mode) << ",";
  json << "\"inputPath\":" << quote(report.inputPath) << ",";
  json << "\"outputPath\":";
  if (report.outputPath.has_value()) {
    json << quote(*report.outputPath);
  } else {
    json << "null";
  }
  json << ",";
  json << "\"spectrogramComparisonPath\":";
  if (report.spectrogramComparisonPath.has_value()) {
    json << quote(*report.spectrogramComparisonPath);
  } else {
    json << "null";
  }
  json << ",";
  json << "\"summary\":" << quote(report.summary) << ",";
  json << "\"warnings\":";
  appendStringArray(json, report.warnings);
  json << ",";
  json << "\"errors\":";
  appendStringArray(json, report.errors);
  json << ",";
  json << "\"guardrails\":";
  appendStringArray(json, report.guardrails);
  json << ",";
  json << "\"requestedOverrides\":";
  appendRequestedOverrides(json, report.requestedOverrides);
  json << ",";
  json << "\"loudnessProfile\":" << quote(report.loudnessProfile) << ",";
  json << "\"targetIntegratedLufs\":" << report.targetIntegratedLufs << ",";
  json << "\"targetTruePeakDbtp\":" << report.targetTruePeakDbtp << ",";
  json << "\"loudnessTargetInferred\":"
       << (report.loudnessTargetInferred ? "true" : "false") << ",";
  json << "\"treatedAsFragile\":"
       << (report.treatedAsFragile ? "true" : "false") << ",";
  json << "\"treatedAsSourceBaked\":"
       << (report.treatedAsSourceBaked ? "true" : "false") << ",";
  json << "\"normalizationGainDbApplied\":"
       << report.normalizationGainDbApplied << ",";
  json << "\"profileBefore\":";
  appendFileProfile(json, report.profileBefore);
  json << ",";
  json << "\"profileAfter\":";
  if (report.profileAfter.has_value()) {
    appendFileProfile(json, *report.profileAfter);
  } else {
    json << "null";
  }
  json << ",";
  json << "\"hotspots\":";
  appendHotspots(json, report.hotspots);
  json << ",";
  json << "\"segments\":";
  appendSegments(json, report.segments);
  json << "}";
}

}  // namespace

FileReport ReportBuilder::makeReport(
    const std::filesystem::path& inputPath,
    const std::optional<std::filesystem::path>& outputPath,
    core::ProcessingMode mode,
    pipeline::PipelineSnapshot snapshot,
    std::string status,
    std::optional<std::filesystem::path> spectrogramComparisonPath,
    std::vector<std::string> warnings,
    std::vector<std::string> errors) const {
  FileReport report;
  report.status = std::move(status);
  report.mode = core::toString(mode);
  report.inputPath = inputPath.string();
  if (outputPath.has_value()) {
    report.outputPath = outputPath->string();
  }
  if (spectrogramComparisonPath.has_value()) {
    report.spectrogramComparisonPath = spectrogramComparisonPath->string();
  }
  report.warnings = std::move(warnings);
  report.warnings.insert(report.warnings.end(),
                         std::make_move_iterator(snapshot.warnings.begin()),
                         std::make_move_iterator(snapshot.warnings.end()));
  report.errors = std::move(errors);
  report.guardrails = std::move(snapshot.plan.fileGuardrails);
  report.loudnessProfile = core::toString(snapshot.plan.loudnessTarget.profile);
  report.targetIntegratedLufs = snapshot.plan.loudnessTarget.integratedLufs;
  report.targetTruePeakDbtp = snapshot.plan.loudnessTarget.truePeakDbtp;
  report.loudnessTargetInferred = snapshot.plan.loudnessTarget.inferred;
  report.profileBefore = snapshot.analysisBefore.profile;
  std::vector<analysis::FrameFeatures>{}.swap(snapshot.analysisBefore.frames);
  std::vector<float>{}.swap(snapshot.analysisBefore.spectrogram.logPower);
  if (snapshot.analysisAfter.has_value()) {
    report.profileAfter = snapshot.analysisAfter->profile;
    std::vector<analysis::FrameFeatures>{}.swap(snapshot.analysisAfter->frames);
    std::vector<analysis::Hotspot>{}.swap(snapshot.analysisAfter->hotspots);
    std::vector<float>{}.swap(snapshot.analysisAfter->spectrogram.logPower);
  }
  report.hotspots = std::move(snapshot.analysisBefore.hotspots);
  report.segments = std::move(snapshot.plan.segments);
  report.requestedOverrides = std::move(snapshot.plan.requestedOverrides);
  report.treatedAsFragile = snapshot.plan.fileMarkedFragile;
  report.treatedAsSourceBaked = snapshot.plan.fileMarkedSourceBaked;
  report.normalizationGainDbApplied =
      snapshot.plan.finalNormalizationGainDbApplied;

  std::size_t brightProtected = 0U;
  std::size_t metallicOverrides = 0U;
  std::size_t limitReachedArtifacts = 0U;
  std::size_t sourceBakedFallbacks = 0U;
  const bool loudnessOnly =
      std::find(report.guardrails.begin(), report.guardrails.end(),
                "loudness_only") != report.guardrails.end();
  for (const auto& segment : report.segments) {
    if (segment.guardrails.brightProtection ||
        segment.guardrails.highRegisterProtection) {
      ++brightProtected;
    }
    if (segment.guardrails.metallicCorrectionOverride) {
      ++metallicOverrides;
    }
    if (segment.guardrails.limitReachedArtifact) {
      ++limitReachedArtifacts;
    }
    if (segment.guardrails.sourceBakedFallback) {
      ++sourceBakedFallbacks;
    }
  }

  std::ostringstream summary;
  summary << "Detected " << report.hotspots.size() << " notable segments. ";
  if (loudnessOnly) {
    summary << "Loudness-only mode skipped corrective EQ, de-essing, and "
            << "compression after analysis. ";
  } else {
    summary << brightProtected
            << " segments triggered bright/high-register protection, "
            << metallicOverrides
            << " segments used metallic-edge correction overrides, "
            << limitReachedArtifacts
            << " segments were marked limit-reached/source-baked, and "
            << sourceBakedFallbacks
            << " segments fell back to source-baked preservation behavior. ";
  }
  summary << report.requestedOverrides.size()
          << " timestamp-targeted overrides were requested. "
          << "Target loudness was " << report.targetIntegratedLufs
          << " LUFS with a true-peak ceiling of " << report.targetTruePeakDbtp
          << " dBTP.";
  report.summary = summary.str();
  return report;
}

std::string ReportBuilder::toJson(const FileReport& report) const {
  std::ostringstream json;
  appendReportJson(json, report);
  return json.str();
}

core::Result<void> ReportBuilder::writeSpectrogramComparison(
    const std::filesystem::path& path,
    const analysis::Spectrogram& before,
    const analysis::Spectrogram& after) const {
  if (before.logPower.empty() || after.logPower.empty() ||
      (before.timeBinCount == 0U) || (before.frequencyBinCount == 0U) ||
      (after.timeBinCount == 0U) || (after.frequencyBinCount == 0U)) {
    return core::Error{"spectrogram_write_failed",
                       "Spectrogram comparison data was not available."};
  }

  const std::size_t timeBins =
      std::min(before.timeBinCount, after.timeBinCount);
  const std::size_t frequencyBins =
      std::min(before.frequencyBinCount, after.frequencyBinCount);
  if ((timeBins == 0U) || (frequencyBins == 0U)) {
    return core::Error{"spectrogram_write_failed",
                       "Spectrogram comparison dimensions were invalid."};
  }

  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  if (!output.is_open()) {
    return core::Error{"spectrogram_write_failed",
                       "Failed to open the spectrogram comparison path."};
  }

  float valueMinimum = std::numeric_limits<float>::max();
  float valueMaximum = std::numeric_limits<float>::lowest();
  float deltaExtent = 0.0F;
  for (std::size_t timeBin = 0; timeBin < timeBins; ++timeBin) {
    for (std::size_t frequencyBin = 0; frequencyBin < frequencyBins;
         ++frequencyBin) {
      const std::size_t beforeIndex =
          (timeBin * before.frequencyBinCount) + frequencyBin;
      const std::size_t afterIndex =
          (timeBin * after.frequencyBinCount) + frequencyBin;
      valueMinimum = std::min(valueMinimum, before.logPower[beforeIndex]);
      valueMinimum = std::min(valueMinimum, after.logPower[afterIndex]);
      valueMaximum = std::max(valueMaximum, before.logPower[beforeIndex]);
      valueMaximum = std::max(valueMaximum, after.logPower[afterIndex]);
      deltaExtent = std::max(
          deltaExtent,
          std::abs(after.logPower[afterIndex] - before.logPower[beforeIndex]));
    }
  }

  const float cellWidth = 3.0F;
  const float cellHeight = 3.0F;
  const float panelWidth = static_cast<float>(timeBins) * cellWidth;
  const float panelHeight = static_cast<float>(frequencyBins) * cellHeight;
  const float marginLeft = 64.0F;
  const float marginTop = 40.0F;
  const float marginRight = 28.0F;
  const float marginBottom = 42.0F;
  const float panelGap = 30.0F;
  const float totalWidth = marginLeft + panelWidth + marginRight;
  const float totalHeight =
      marginTop + (panelHeight * 3.0F) + (panelGap * 2.0F) + marginBottom;

  const float inputPanelY = marginTop;
  const float outputPanelY = inputPanelY + panelHeight + panelGap;
  const float deltaPanelY = outputPanelY + panelHeight + panelGap;

  output << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "
         << totalWidth << " " << totalHeight << "\" width=\"" << totalWidth
         << "\" height=\"" << totalHeight << "\">\n";
  output << "<rect width=\"100%\" height=\"100%\" fill=\"#060914\" />\n";
  output << "<text x=\"" << marginLeft << "\" y=\"22\" fill=\"#f3f5f8\" "
         << "font-size=\"15\" font-family=\"monospace\">"
         << "AutoEqualizer Spectrogram Comparison</text>\n";
  output << "<text x=\"" << marginLeft << "\" y=\"34\" fill=\"#94a3b8\" "
         << "font-size=\"10\" font-family=\"monospace\">"
         << "Delta uses output minus input dB. Blue indicates more energy; "
         << "amber indicates less.</text>\n";

  const auto appendPanelDecor = [&](const std::string& label, float panelY) {
    output << "<text x=\"12\" y=\"" << (panelY + 14.0F)
           << "\" fill=\"#d6deeb\" font-size=\"12\" font-family=\"monospace\">"
           << escapeXml(label) << "</text>\n";
    output << "<rect x=\"" << marginLeft << "\" y=\"" << panelY
           << "\" width=\"" << panelWidth << "\" height=\"" << panelHeight
           << "\" fill=\"#0b1020\" stroke=\"#1f2937\" stroke-width=\"1\" />\n";
  };

  appendPanelDecor("Input", inputPanelY);
  appendPanelDecor("Output", outputPanelY);
  appendPanelDecor("Delta", deltaPanelY);
  appendSpectrogramPanel(output, before, valueMinimum, valueMaximum, marginLeft,
                         inputPanelY, cellWidth, cellHeight);
  appendSpectrogramPanel(output, after, valueMinimum, valueMaximum, marginLeft,
                         outputPanelY, cellWidth, cellHeight);
  appendDeltaPanel(output, before, after, deltaExtent, marginLeft, deltaPanelY,
                   cellWidth, cellHeight);

  const std::array<float, 3U> guideFrequencies{
      std::max(before.minFrequencyHz, 100.0F),
      std::min(before.maxFrequencyHz, 1000.0F),
      std::min(before.maxFrequencyHz, 8000.0F),
  };
  for (const float frequencyHz : guideFrequencies) {
    const float inputGuideY = spectrogramYForFrequency(before, frequencyHz,
                                                       inputPanelY, cellHeight);
    const float outputGuideY = spectrogramYForFrequency(after, frequencyHz,
                                                        outputPanelY, cellHeight);
    const float deltaGuideY = spectrogramYForFrequency(after, frequencyHz,
                                                       deltaPanelY, cellHeight);
    output << "<line x1=\"" << marginLeft << "\" y1=\"" << inputGuideY
           << "\" x2=\"" << (marginLeft + panelWidth) << "\" y2=\""
           << inputGuideY
           << "\" stroke=\"#162033\" stroke-width=\"0.5\" />\n";
    output << "<line x1=\"" << marginLeft << "\" y1=\"" << outputGuideY
           << "\" x2=\"" << (marginLeft + panelWidth) << "\" y2=\""
           << outputGuideY
           << "\" stroke=\"#162033\" stroke-width=\"0.5\" />\n";
    output << "<line x1=\"" << marginLeft << "\" y1=\"" << deltaGuideY
           << "\" x2=\"" << (marginLeft + panelWidth) << "\" y2=\""
           << deltaGuideY
           << "\" stroke=\"#162033\" stroke-width=\"0.5\" />\n";
    output << "<text x=\"8\" y=\"" << (inputGuideY + 3.0F)
           << "\" fill=\"#7f8ea3\" font-size=\"9\" font-family=\"monospace\">"
           << escapeXml(formatFrequency(frequencyHz)) << "</text>\n";
    output << "<text x=\"8\" y=\"" << (outputGuideY + 3.0F)
           << "\" fill=\"#7f8ea3\" font-size=\"9\" font-family=\"monospace\">"
           << escapeXml(formatFrequency(frequencyHz)) << "</text>\n";
    output << "<text x=\"8\" y=\"" << (deltaGuideY + 3.0F)
           << "\" fill=\"#7f8ea3\" font-size=\"9\" font-family=\"monospace\">"
           << escapeXml(formatFrequency(frequencyHz)) << "</text>\n";
  }

  const std::array<double, 3U> guideTimes{
      0.0,
      before.durationSeconds * 0.5,
      before.durationSeconds,
  };
  for (const double timeSeconds : guideTimes) {
    const float x = marginLeft +
                    (panelWidth * static_cast<float>(
                                      timeSeconds /
                                      std::max(before.durationSeconds, 0.001)));
    output << "<text x=\"" << x << "\" y=\"" << (totalHeight - 14.0F)
           << "\" fill=\"#7f8ea3\" font-size=\"9\" font-family=\"monospace\" "
           << "text-anchor=\"middle\">"
           << escapeXml(formatSeconds(timeSeconds)) << "</text>\n";
  }

  output << "</svg>\n";
  if (!output.good()) {
    return core::Error{"spectrogram_write_failed",
                       "Failed to write the spectrogram comparison."};
  }

  return core::Result<void>{};
}

core::Result<void> ReportBuilder::writeJson(
    const std::filesystem::path& path,
    const FileReport& report) const {
  std::ofstream output(path);
  if (!output.is_open()) {
    return core::Error{"report_write_failed",
                       "Failed to open report output path."};
  }

  appendReportJson(output, report);
  output << "\n";
  if (!output.good()) {
    return core::Error{"report_write_failed",
                       "Failed to write report contents."};
  }

  return core::Result<void>{};
}

}  // namespace autoequalizer::report
