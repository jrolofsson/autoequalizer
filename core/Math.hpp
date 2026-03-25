#pragma once

#include <algorithm>
#include <cmath>

namespace autoequalizer::core {

inline constexpr float kEpsilon = 1.0e-6F;

template <typename T>
[[nodiscard]] constexpr T clamp(T value, T low, T high) {
  return std::max(low, std::min(value, high));
}

[[nodiscard]] inline float linearToDb(float value) {
  return 20.0F * std::log10(std::max(value, kEpsilon));
}

[[nodiscard]] inline float dbToLinear(float valueDb) {
  return std::pow(10.0F, valueDb / 20.0F);
}

[[nodiscard]] inline float normalizeRange(float value, float low, float high) {
  if (high <= low) {
    return 0.0F;
  }

  return clamp((value - low) / (high - low), 0.0F, 1.0F);
}

[[nodiscard]] inline float lerp(float start, float end, float amount) {
  return start + ((end - start) * amount);
}

}  // namespace autoequalizer::core

