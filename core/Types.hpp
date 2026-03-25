#pragma once

#include <string>
#include <string_view>

#include "core/Result.hpp"

namespace autoequalizer::core {

enum class ProcessingMode {
  Preserve,
  ArtifactSafe,
  Normal,
  Aggressive
};

enum class LoudnessProfile {
  Stem,
  Streaming,
  Spotify,
  AppleMusic,
  Custom
};

enum class RangeOverridePolicy {
  ArtifactSafe,
  Preserve,
  Bypass
};

struct RangeOverride {
  double startSeconds{};
  double endSeconds{};
  RangeOverridePolicy policy{RangeOverridePolicy::ArtifactSafe};
};

struct ModeLimits {
  float harshCutDbCap{};
  float hfSmoothDbCap{};
  float deEssMaxReductionDbCap{};
  float compressionRatioCap{};
  float normalizationBoostCapDb{};
};

struct LoudnessTarget {
  LoudnessProfile profile{LoudnessProfile::Streaming};
  float integratedLufs{-14.0F};
  float truePeakDbtp{-1.0F};
  bool normalizeIntegratedLufs{true};
  bool inferred{false};
};

inline constexpr std::size_t kDefaultAnalysisWindow = 2048;
inline constexpr std::size_t kDefaultHopSize = 512;

[[nodiscard]] inline std::string toString(ProcessingMode mode) {
  switch (mode) {
    case ProcessingMode::Preserve:
      return "preserve";
    case ProcessingMode::ArtifactSafe:
      return "artifact-safe";
    case ProcessingMode::Normal:
      return "normal";
    case ProcessingMode::Aggressive:
      return "aggressive";
  }

  return "normal";
}

[[nodiscard]] inline Result<ProcessingMode> parseProcessingMode(
    std::string_view value) {
  if (value == "preserve") {
    return ProcessingMode::Preserve;
  }
  if ((value == "artifact-safe") || (value == "artifact_safe") ||
      (value == "artifactsafe")) {
    return ProcessingMode::ArtifactSafe;
  }
  if (value == "normal") {
    return ProcessingMode::Normal;
  }
  if (value == "aggressive") {
    return ProcessingMode::Aggressive;
  }

  return Error{"invalid_mode",
               "Mode must be one of: preserve, artifact-safe, normal, aggressive."};
}

[[nodiscard]] inline ModeLimits limitsForMode(ProcessingMode mode) {
  switch (mode) {
    case ProcessingMode::Preserve:
      return ModeLimits{1.5F, 1.0F, 2.0F, 1.25F, 6.0F};
    case ProcessingMode::ArtifactSafe:
      return ModeLimits{2.2F, 0.9F, 2.5F, 1.35F, 8.0F};
    case ProcessingMode::Normal:
      return ModeLimits{3.0F, 2.0F, 4.0F, 1.6F, 10.0F};
    case ProcessingMode::Aggressive:
      return ModeLimits{5.0F, 3.5F, 6.0F, 2.0F, 14.0F};
  }

  return ModeLimits{3.0F, 2.0F, 4.0F, 1.6F, 10.0F};
}

[[nodiscard]] inline std::string toString(LoudnessProfile profile) {
  switch (profile) {
    case LoudnessProfile::Stem:
      return "stem";
    case LoudnessProfile::Streaming:
      return "streaming";
    case LoudnessProfile::Spotify:
      return "spotify";
    case LoudnessProfile::AppleMusic:
      return "apple_music";
    case LoudnessProfile::Custom:
      return "custom";
  }

  return "streaming";
}

[[nodiscard]] inline Result<LoudnessProfile> parseLoudnessProfile(
    std::string_view value) {
  if ((value == "stem") || (value == "stem_safe") || (value == "vocal_stem")) {
    return LoudnessProfile::Stem;
  }
  if ((value == "streaming") || (value == "streaming-safe")) {
    return LoudnessProfile::Streaming;
  }
  if (value == "spotify") {
    return LoudnessProfile::Spotify;
  }
  if ((value == "apple") || (value == "apple_music")) {
    return LoudnessProfile::AppleMusic;
  }
  if (value == "custom") {
    return LoudnessProfile::Custom;
  }

  return Error{"invalid_loudness_profile",
               "Loudness profile must be one of: stem, streaming, spotify, apple, "
               "custom."};
}

[[nodiscard]] inline LoudnessTarget targetForLoudnessProfile(
    LoudnessProfile profile) {
  switch (profile) {
    case LoudnessProfile::Stem:
      return LoudnessTarget{profile, -18.0F, -3.0F, false, false};
    case LoudnessProfile::Streaming:
      return LoudnessTarget{profile, -14.0F, -1.0F, true, false};
    case LoudnessProfile::Spotify:
      return LoudnessTarget{profile, -14.0F, -1.0F, true, false};
    case LoudnessProfile::AppleMusic:
      // Apple documents Sound Check behavior and Apple Digital Masters true peak
      // guidance; this LUFS target is a compatibility-oriented engineering preset.
      return LoudnessTarget{profile, -16.0F, -1.0F, true, true};
    case LoudnessProfile::Custom:
      return LoudnessTarget{profile, -14.0F, -1.0F, true, false};
  }

  return LoudnessTarget{LoudnessProfile::Streaming, -14.0F, -1.0F, true, false};
}

[[nodiscard]] inline std::string toString(RangeOverridePolicy policy) {
  switch (policy) {
    case RangeOverridePolicy::ArtifactSafe:
      return "artifact-safe";
    case RangeOverridePolicy::Preserve:
      return "preserve";
    case RangeOverridePolicy::Bypass:
      return "bypass";
  }

  return "artifact-safe";
}

[[nodiscard]] inline Result<RangeOverridePolicy> parseRangeOverridePolicy(
    std::string_view value) {
  if ((value == "artifact-safe") || (value == "artifact_safe")) {
    return RangeOverridePolicy::ArtifactSafe;
  }
  if (value == "preserve") {
    return RangeOverridePolicy::Preserve;
  }
  if (value == "bypass") {
    return RangeOverridePolicy::Bypass;
  }

  return Error{"invalid_override_policy",
               "Override policy must be one of: artifact-safe, preserve, bypass."};
}

}  // namespace autoequalizer::core
