#include "analysis/LoudnessMeter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <numbers>
#include <vector>

#include "core/Math.hpp"

namespace autoequalizer::analysis {

namespace {

constexpr int kMeasurementSampleRate = 48000;
constexpr float kBlockOffset = -0.691F;

class FixedBiquad {
 public:
  void set(float b0, float b1, float b2, float a1, float a2) {
    b0_ = b0;
    b1_ = b1;
    b2_ = b2;
    a1_ = a1;
    a2_ = a2;
  }

  [[nodiscard]] float process(float input) {
    const float output = (b0_ * input) + z1_;
    z1_ = (b1_ * input) - (a1_ * output) + z2_;
    z2_ = (b2_ * input) - (a2_ * output);
    return output;
  }

 private:
  float b0_{1.0F};
  float b1_{0.0F};
  float b2_{0.0F};
  float a1_{0.0F};
  float a2_{0.0F};
  float z1_{0.0F};
  float z2_{0.0F};
};

void applyKWeighting(std::vector<float>& samples) {
  FixedBiquad shelf;
  FixedBiquad highPass;

  // ITU-R BS.1770 coefficients at 48 kHz.
  shelf.set(1.53512485958697F, -2.69169618940638F, 1.19839281085285F,
            -1.69065929318241F, 0.73248077421585F);
  highPass.set(1.0F, -2.0F, 1.0F, -1.99004745483398F, 0.99007225036621F);

  for (float& sample : samples) {
    sample = highPass.process(shelf.process(sample));
  }
}

[[nodiscard]] float blockEnergy(const std::vector<std::vector<float>>& channels,
                                std::size_t start,
                                std::size_t length) {
  if (channels.empty() || length == 0U) {
    return 0.0F;
  }

  double total = 0.0;
  for (const auto& channel : channels) {
    const std::size_t end = std::min(channel.size(), start + length);
    if (end <= start) {
      continue;
    }

    double energy = 0.0;
    for (std::size_t index = start; index < end; ++index) {
      const double sample = channel[index];
      energy += sample * sample;
    }
    energy /= static_cast<double>(end - start);
    total += energy;
  }

  return static_cast<float>(total);
}

[[nodiscard]] float energyToLufs(float energy) {
  return kBlockOffset + (10.0F * std::log10(std::max(energy, core::kEpsilon)));
}

[[nodiscard]] float percentile(std::vector<float> values, float fraction) {
  if (values.empty()) {
    return 0.0F;
  }

  std::sort(values.begin(), values.end());
  const std::size_t index = static_cast<std::size_t>(
      core::clamp(fraction, 0.0F, 1.0F) *
      static_cast<float>(values.size() - 1U));
  return values[index];
}

[[nodiscard]] float cubicHermite(float y0, float y1, float y2, float y3,
                                 float t) {
  const float c0 = y1;
  const float c1 = 0.5F * (y2 - y0);
  const float c2 = y0 - (2.5F * y1) + (2.0F * y2) - (0.5F * y3);
  const float c3 = (0.5F * (y3 - y0)) + (1.5F * (y1 - y2));
  return ((c3 * t + c2) * t + c1) * t + c0;
}

[[nodiscard]] float estimateTruePeakDbtp(
    const std::vector<std::vector<float>>& channels) {
  float maxPeak = 0.0F;
  for (const auto& channel : channels) {
    if (channel.empty()) {
      continue;
    }

    for (std::size_t index = 0; index < channel.size(); ++index) {
      maxPeak = std::max(maxPeak, std::abs(channel[index]));
      const float y0 = channel[index == 0U ? 0U : index - 1U];
      const float y1 = channel[index];
      const float y2 = channel[std::min(index + 1U, channel.size() - 1U)];
      const float y3 = channel[std::min(index + 2U, channel.size() - 1U)];

      for (int step = 1; step < 4; ++step) {
        const float sample =
            cubicHermite(y0, y1, y2, y3, static_cast<float>(step) / 4.0F);
        maxPeak = std::max(maxPeak, std::abs(sample));
      }
    }
  }

  return core::linearToDb(maxPeak + core::kEpsilon);
}

}  // namespace

std::vector<std::vector<float>> LoudnessMeter::resampleTo48k(
    const audio::AudioBuffer& buffer) const {
  std::vector<std::vector<float>> channels(buffer.channelCount());
  if (buffer.channelCount() == 0U) {
    return channels;
  }

  const double ratio = static_cast<double>(kMeasurementSampleRate) /
                       static_cast<double>(buffer.sampleRate());
  const std::size_t outputFrames = static_cast<std::size_t>(
      std::max(1.0, std::round(static_cast<double>(buffer.frameCount()) * ratio)));

  for (std::size_t channelIndex = 0; channelIndex < buffer.channelCount();
       ++channelIndex) {
    const auto& source = buffer.channel(channelIndex);
    auto& target = channels[channelIndex];
    if (source.empty()) {
      continue;
    }
    target.resize(outputFrames, 0.0F);

    for (std::size_t frame = 0; frame < outputFrames; ++frame) {
      const double sourcePosition = static_cast<double>(frame) / ratio;
      const std::size_t left = static_cast<std::size_t>(sourcePosition);
      const std::size_t right = std::min(left + 1U, source.size() - 1U);
      const double blend = sourcePosition - static_cast<double>(left);
      target[frame] = static_cast<float>(
          ((1.0 - blend) * source[left]) + (blend * source[right]));
    }
  }

  return channels;
}

LoudnessStats LoudnessMeter::measure(const audio::AudioBuffer& buffer) const {
  LoudnessStats stats;
  auto channels = resampleTo48k(buffer);
  if (channels.empty() || channels.front().empty()) {
    return stats;
  }

  for (auto& channel : channels) {
    applyKWeighting(channel);
  }

  const std::size_t momentaryWindow = 19200U;
  const std::size_t momentaryStep = 4800U;
  const std::size_t shortTermWindow = 144000U;
  const std::size_t shortTermStep = 48000U;

  std::vector<float> momentaryLoudness;
  std::vector<float> momentaryEnergies;
  for (std::size_t start = 0; start < channels.front().size();
       start += momentaryStep) {
    const std::size_t length =
        std::min(momentaryWindow, channels.front().size() - start);
    const float energy = blockEnergy(channels, start, length);
    momentaryEnergies.push_back(energy);
    momentaryLoudness.push_back(energyToLufs(energy));
    if ((start + length) >= channels.front().size()) {
      break;
    }
  }

  std::vector<float> gatedEnergies;
  for (std::size_t index = 0; index < momentaryLoudness.size(); ++index) {
    if (momentaryLoudness[index] > -70.0F) {
      gatedEnergies.push_back(momentaryEnergies[index]);
    }
  }

  float preliminaryLufs = -70.0F;
  if (!gatedEnergies.empty()) {
    const float avgEnergy =
        std::accumulate(gatedEnergies.begin(), gatedEnergies.end(), 0.0F) /
        static_cast<float>(gatedEnergies.size());
    preliminaryLufs = energyToLufs(avgEnergy);
  }

  const float relativeGate = preliminaryLufs - 10.0F;
  gatedEnergies.clear();
  for (std::size_t index = 0; index < momentaryLoudness.size(); ++index) {
    if (momentaryLoudness[index] > std::max(-70.0F, relativeGate)) {
      gatedEnergies.push_back(momentaryEnergies[index]);
    }
  }

  if (!gatedEnergies.empty()) {
    const float avgEnergy =
        std::accumulate(gatedEnergies.begin(), gatedEnergies.end(), 0.0F) /
        static_cast<float>(gatedEnergies.size());
    stats.integratedLufs = energyToLufs(avgEnergy);
  } else {
    stats.integratedLufs = preliminaryLufs;
  }

  stats.momentaryMaxLufs = momentaryLoudness.empty()
                               ? -70.0F
                               : *std::max_element(momentaryLoudness.begin(),
                                                   momentaryLoudness.end());

  std::vector<float> shortTermLoudness;
  for (std::size_t start = 0; start < channels.front().size();
       start += shortTermStep) {
    const std::size_t length =
        std::min(shortTermWindow, channels.front().size() - start);
    const float energy = blockEnergy(channels, start, length);
    shortTermLoudness.push_back(energyToLufs(energy));
    if ((start + length) >= channels.front().size()) {
      break;
    }
  }

  stats.shortTermMaxLufs = shortTermLoudness.empty()
                               ? stats.integratedLufs
                               : *std::max_element(shortTermLoudness.begin(),
                                                   shortTermLoudness.end());

  std::vector<float> gatedShortTerm;
  for (const float lufs : shortTermLoudness) {
    if (lufs > std::max(-70.0F, stats.integratedLufs - 20.0F)) {
      gatedShortTerm.push_back(lufs);
    }
  }
  if (!gatedShortTerm.empty()) {
    stats.loudnessRangeLu =
        percentile(gatedShortTerm, 0.95F) - percentile(gatedShortTerm, 0.10F);
  }

  stats.truePeakDbtp = estimateTruePeakDbtp(channels);
  return stats;
}

}  // namespace autoequalizer::analysis
