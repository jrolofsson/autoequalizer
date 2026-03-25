#include "analysis/AnalysisEngine.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <numbers>
#include <vector>

#include "core/Math.hpp"
#include "analysis/LoudnessMeter.hpp"

namespace autoequalizer::analysis {

namespace {

struct PitchEstimate {
  float frequencyHz{};
  float confidence{};
};

inline constexpr std::size_t kSpectrogramTimeBins = 160U;
inline constexpr std::size_t kSpectrogramFrequencyBins = 72U;
inline constexpr float kSpectrogramMinFrequencyHz = 40.0F;
inline constexpr float kSpectrogramMaxFrequencyHz = 16000.0F;

class FftPlan {
 public:
  explicit FftPlan(std::size_t size) : size_(size), bitReverse_(size) {
    std::size_t bits = 0;
    while ((1U << bits) < size_) {
      ++bits;
    }

    for (std::size_t index = 0; index < size_; ++index) {
      std::size_t reversed = 0;
      for (std::size_t bit = 0; bit < bits; ++bit) {
        reversed = (reversed << 1U) | ((index >> bit) & 1U);
      }
      bitReverse_[index] = reversed;
    }
  }

  void transform(const std::vector<float>& input,
                 std::vector<std::complex<float>>& output) const {
    output.assign(size_, std::complex<float>{});
    for (std::size_t index = 0; index < size_; ++index) {
      output[bitReverse_[index]] =
          std::complex<float>{index < input.size() ? input[index] : 0.0F, 0.0F};
    }

    for (std::size_t length = 2; length <= size_; length <<= 1U) {
      const float angle =
          (-2.0F * std::numbers::pi_v<float>) / static_cast<float>(length);
      const std::complex<float> wLength{std::cos(angle), std::sin(angle)};

      for (std::size_t block = 0; block < size_; block += length) {
        std::complex<float> w{1.0F, 0.0F};
        for (std::size_t index = 0; index < length / 2U; ++index) {
          const auto even = output[block + index];
          const auto odd = output[block + index + (length / 2U)] * w;
          output[block + index] = even + odd;
          output[block + index + (length / 2U)] = even - odd;
          w *= wLength;
        }
      }
    }
  }

 private:
  std::size_t size_{};
  std::vector<std::size_t> bitReverse_;
};

[[nodiscard]] std::vector<float> buildHannWindow(std::size_t size) {
  std::vector<float> window(size, 0.0F);
  if (size == 0U) {
    return window;
  }

  for (std::size_t index = 0; index < size; ++index) {
    window[index] =
        0.5F - (0.5F * std::cos((2.0F * std::numbers::pi_v<float> *
                                 static_cast<float>(index)) /
                                static_cast<float>(size - 1U)));
  }
  return window;
}

[[nodiscard]] float parabolicOffset(float left, float center, float right) {
  const float denominator = left - (2.0F * center) + right;
  if (std::abs(denominator) < core::kEpsilon) {
    return 0.0F;
  }

  return core::clamp(0.5F * (left - right) / denominator, -0.5F, 0.5F);
}

[[nodiscard]] PitchEstimate estimatePitchYin(
    const std::vector<float>& samples,
    std::size_t sampleCount,
    int sampleRate,
    std::vector<float>& yinBuffer) {
  PitchEstimate estimate;
  if (sampleCount < 96U) {
    return estimate;
  }

  const std::size_t minLag =
      std::max<std::size_t>(2U, static_cast<std::size_t>(sampleRate / 1000));
  const std::size_t maxLag = std::min(
      sampleCount / 2U, static_cast<std::size_t>(sampleRate / 80));
  if ((maxLag <= minLag) || (maxLag >= yinBuffer.size())) {
    return estimate;
  }

  const float mean = std::accumulate(samples.begin(), samples.begin() + sampleCount,
                                     0.0F) /
                     static_cast<float>(sampleCount);
  float energy = 0.0F;
  for (std::size_t index = 0; index < sampleCount; ++index) {
    const float centered = samples[index] - mean;
    energy += centered * centered;
  }
  if (energy <= core::kEpsilon) {
    return estimate;
  }

  std::fill(yinBuffer.begin(), yinBuffer.begin() + maxLag + 1U, 0.0F);
  for (std::size_t lag = 1U; lag <= maxLag; ++lag) {
    float difference = 0.0F;
    for (std::size_t index = 0; index < (sampleCount - lag); ++index) {
      const float current = samples[index] - mean;
      const float delayed = samples[index + lag] - mean;
      const float delta = current - delayed;
      difference += delta * delta;
    }
    yinBuffer[lag] = difference;
  }

  float runningSum = 0.0F;
  yinBuffer[0] = 1.0F;
  for (std::size_t lag = 1U; lag <= maxLag; ++lag) {
    runningSum += yinBuffer[lag];
    yinBuffer[lag] =
        (runningSum <= core::kEpsilon)
            ? 1.0F
            : (yinBuffer[lag] * static_cast<float>(lag) / runningSum);
  }

  constexpr float kThreshold = 0.12F;
  std::size_t selectedLag = 0U;
  for (std::size_t lag = minLag; lag < maxLag; ++lag) {
    if (yinBuffer[lag] < kThreshold) {
      selectedLag = lag;
      while ((selectedLag + 1U) <= maxLag &&
             (yinBuffer[selectedLag + 1U] <= yinBuffer[selectedLag])) {
        ++selectedLag;
      }
      break;
    }
  }

  if (selectedLag == 0U) {
    selectedLag = minLag;
    for (std::size_t lag = minLag + 1U; lag <= maxLag; ++lag) {
      if (yinBuffer[lag] < yinBuffer[selectedLag]) {
        selectedLag = lag;
      }
    }
    if (yinBuffer[selectedLag] > 0.35F) {
      return estimate;
    }
  }

  const float offset = (selectedLag > minLag && (selectedLag + 1U) <= maxLag)
                           ? parabolicOffset(yinBuffer[selectedLag - 1U],
                                             yinBuffer[selectedLag],
                                             yinBuffer[selectedLag + 1U])
                           : 0.0F;
  const float refinedLag = static_cast<float>(selectedLag) + offset;
  if (refinedLag <= 0.0F) {
    return estimate;
  }

  estimate.frequencyHz = static_cast<float>(sampleRate) / refinedLag;
  estimate.confidence = core::clamp(1.0F - yinBuffer[selectedLag], 0.0F, 1.0F);
  return estimate;
}

[[nodiscard]] PitchEstimate smoothPitchEstimate(const PitchEstimate& rawEstimate,
                                                float previousPitchHz,
                                                float previousConfidence,
                                                float spectralFlatness) {
  PitchEstimate smoothed = rawEstimate;

  if ((smoothed.frequencyHz <= 0.0F) || (smoothed.confidence < 0.35F)) {
    if ((previousPitchHz > 0.0F) && (previousConfidence > 0.60F) &&
        (spectralFlatness < 0.24F)) {
      smoothed.frequencyHz = previousPitchHz;
      smoothed.confidence = previousConfidence * 0.82F;
    }
    return smoothed;
  }

  if ((previousPitchHz <= 0.0F) || (previousConfidence <= 0.0F)) {
    return smoothed;
  }

  const float semitoneDelta =
      12.0F * std::abs(std::log2(smoothed.frequencyHz / previousPitchHz));
  const float continuity =
      1.0F - core::normalizeRange(semitoneDelta, 1.5F, 10.0F);
  const float smoothingAmount = core::clamp(
      (0.35F * previousConfidence) + (0.30F * (1.0F - smoothed.confidence)) +
          (0.25F * continuity),
      0.0F, 0.75F);

  if ((smoothed.confidence >= 0.45F) || (continuity >= 0.55F)) {
    smoothed.frequencyHz =
        core::lerp(smoothed.frequencyHz, previousPitchHz, smoothingAmount);
  }

  smoothed.confidence = core::clamp(
      (0.75F * smoothed.confidence) +
          (0.25F * previousConfidence * continuity),
      0.0F, 1.0F);
  return smoothed;
}

[[nodiscard]] float bandEnergy(const std::vector<float>& spectrum,
                               int sampleRate,
                               float lowHz,
                               float highHz) {
  if (spectrum.empty()) {
    return 0.0F;
  }

  const float nyquist = static_cast<float>(sampleRate) / 2.0F;
  const std::size_t maxBin = spectrum.size() - 1U;
  const auto toBin = [&](float hz) {
    const float clamped = core::clamp(hz, 0.0F, nyquist);
    return static_cast<std::size_t>(
        std::round((clamped / nyquist) * static_cast<float>(maxBin)));
  };

  const std::size_t lowBin = std::min(toBin(lowHz), maxBin);
  const std::size_t highBin = std::min(toBin(highHz), maxBin);
  float total = 0.0F;
  for (std::size_t bin = lowBin; bin <= highBin; ++bin) {
    total += spectrum[bin];
  }
  return total;
}

[[nodiscard]] float meanOf(const std::vector<float>& values) {
  if (values.empty()) {
    return 0.0F;
  }
  return std::accumulate(values.begin(), values.end(), 0.0F) /
         static_cast<float>(values.size());
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

[[nodiscard]] float calculateVariance(const std::vector<float>& values,
                                      float mean) {
  if (values.empty()) {
    return 0.0F;
  }

  float total = 0.0F;
  for (const float value : values) {
    const float difference = value - mean;
    total += difference * difference;
  }

  return total / static_cast<float>(values.size());
}

void initializeSpectrogram(Spectrogram& spectrogram,
                           int sampleRate,
                           std::size_t magnitudeSize,
                           std::size_t estimatedFrameCount,
                           std::vector<std::size_t>& columnCounts,
                           std::vector<std::pair<std::size_t, std::size_t>>&
                               frequencyRanges) {
  if ((magnitudeSize <= 1U) || (estimatedFrameCount == 0U)) {
    return;
  }

  spectrogram.timeBinCount =
      std::max<std::size_t>(1U,
                            std::min(kSpectrogramTimeBins, estimatedFrameCount));
  spectrogram.frequencyBinCount = std::max<std::size_t>(
      1U, std::min(kSpectrogramFrequencyBins, magnitudeSize - 1U));

  const float nyquist = static_cast<float>(sampleRate) / 2.0F;
  const float maxFrequency = std::max(
      1.0F, std::min(kSpectrogramMaxFrequencyHz, nyquist));
  const float minFrequency =
      (maxFrequency <= (kSpectrogramMinFrequencyHz + 1.0F))
          ? 0.0F
          : std::min(kSpectrogramMinFrequencyHz, maxFrequency * 0.5F);

  spectrogram.minFrequencyHz = minFrequency;
  spectrogram.maxFrequencyHz = maxFrequency;
  spectrogram.logPower.assign(spectrogram.timeBinCount *
                                  spectrogram.frequencyBinCount,
                              0.0F);
  columnCounts.assign(spectrogram.timeBinCount, 0U);
  frequencyRanges.resize(spectrogram.frequencyBinCount);

  const std::size_t maxBin = magnitudeSize - 1U;
  const auto toMagnitudeBin = [&](float frequencyHz) {
    if (nyquist <= core::kEpsilon) {
      return std::size_t{1U};
    }

    const float clamped = core::clamp(frequencyHz, 0.0F, nyquist);
    return std::min(
        maxBin,
        std::max<std::size_t>(
            1U, static_cast<std::size_t>(std::lround(
                    (clamped / nyquist) * static_cast<float>(maxBin)))));
  };

  const bool useLogScale = minFrequency > core::kEpsilon;
  const float frequencyRatio =
      useLogScale ? (maxFrequency / minFrequency) : 1.0F;
  auto edgeFrequencyAt = [&](std::size_t index) {
    const float fraction = static_cast<float>(index) /
                           static_cast<float>(spectrogram.frequencyBinCount);
    if (!useLogScale) {
      return fraction * maxFrequency;
    }
    return minFrequency * std::pow(frequencyRatio, fraction);
  };

  for (std::size_t bin = 0; bin < spectrogram.frequencyBinCount; ++bin) {
    const float lowFrequency = edgeFrequencyAt(bin);
    const float highFrequency = edgeFrequencyAt(bin + 1U);
    const std::size_t lowBin = toMagnitudeBin(lowFrequency);
    const std::size_t highBin =
        std::max(lowBin, toMagnitudeBin(highFrequency));
    frequencyRanges[bin] = {lowBin, highBin};
  }
}

void accumulateSpectrogramFrame(
    Spectrogram& spectrogram,
    const std::vector<float>& magnitude,
    std::size_t frameIndex,
    std::size_t estimatedFrameCount,
    const std::vector<std::pair<std::size_t, std::size_t>>& frequencyRanges,
    std::vector<std::size_t>& columnCounts) {
  if (spectrogram.logPower.empty() || (spectrogram.timeBinCount == 0U) ||
      (spectrogram.frequencyBinCount == 0U) || (estimatedFrameCount == 0U)) {
    return;
  }

  const std::size_t timeBin = std::min(
      spectrogram.timeBinCount - 1U,
      (frameIndex * spectrogram.timeBinCount) / estimatedFrameCount);
  ++columnCounts[timeBin];

  float* column =
      spectrogram.logPower.data() +
      (timeBin * spectrogram.frequencyBinCount);
  for (std::size_t frequencyBin = 0; frequencyBin < frequencyRanges.size();
       ++frequencyBin) {
    const auto [lowBin, highBin] = frequencyRanges[frequencyBin];
    float total = 0.0F;
    for (std::size_t bin = lowBin; bin <= highBin; ++bin) {
      total += magnitude[bin];
    }
    const float average =
        total / static_cast<float>((highBin - lowBin) + 1U);
    column[frequencyBin] +=
        10.0F * std::log10(average + core::kEpsilon);
  }
}

void finalizeSpectrogram(Spectrogram& spectrogram,
                         const std::vector<std::size_t>& columnCounts) {
  if (spectrogram.logPower.empty() ||
      (spectrogram.frequencyBinCount == 0U)) {
    return;
  }

  for (std::size_t timeBin = 0; timeBin < spectrogram.timeBinCount; ++timeBin) {
    const std::size_t count =
        std::max<std::size_t>(1U, columnCounts[timeBin]);
    float* column =
        spectrogram.logPower.data() +
        (timeBin * spectrogram.frequencyBinCount);
    for (std::size_t frequencyBin = 0; frequencyBin < spectrogram.frequencyBinCount;
         ++frequencyBin) {
      column[frequencyBin] /= static_cast<float>(count);
    }
  }
}

[[nodiscard]] std::vector<Hotspot> buildHotspots(
    const std::vector<FrameFeatures>& frames,
    const FileProfile& profile) {
  std::vector<Hotspot> hotspots;
  for (const auto& frame : frames) {
    const float harshness =
        (0.45F * core::normalizeRange(frame.upperMidRatio,
                                      profile.meanUpperMidRatio + 0.02F,
                                      profile.meanUpperMidRatio + 0.16F)) +
        (0.35F * core::normalizeRange(frame.sibilantActivity,
                                      profile.meanSibilantActivity + 0.02F,
                                      profile.meanSibilantActivity + 0.18F)) +
        (0.20F * core::normalizeRange(frame.spectralFlatness, 0.15F, 0.48F));

    const float bright =
        (0.35F * core::normalizeRange(frame.spectralCentroidHz,
                                      profile.meanSpectralCentroidHz * 0.9F,
                                      profile.brightnessBaseline * 1.1F)) +
        (0.25F * core::normalizeRange(frame.airRatio,
                                      profile.meanAirRatio + 0.01F, 0.24F)) +
        (0.20F * (1.0F - frame.spectralFlatness)) +
        (0.20F * core::normalizeRange(frame.pitchEstimateHz, 220.0F, 520.0F));

    const float fragile =
        (0.35F * core::normalizeRange(frame.spectralFlatness, 0.18F, 0.52F)) +
        (0.25F * core::normalizeRange(frame.changeScore, 0.05F, 0.28F)) +
        (0.20F * (1.0F - core::normalizeRange(frame.dynamicRangeDb, 7.0F, 18.0F))) +
        (0.20F * core::normalizeRange(frame.highFrequencyBalance, 0.55F, 1.45F));
    const bool metallicPipeResonance =
        (frame.upperMidRatio >= (profile.meanUpperMidRatio + 0.06F)) &&
        (frame.highFrequencyBalance >= 1.85F) &&
        (frame.spectralFlatness <= 0.04F) &&
        (frame.harmonicConfidence <= 0.10F);
    const bool whiteNoiseLikeTopEnd =
        (frame.spectralFlatness >= 0.22F) &&
        (frame.highFrequencyBalance >= 1.45F) &&
        (frame.sibilantActivity >= (profile.meanSibilantActivity + 0.06F));
    const bool voiceInstability =
        (frame.changeScore >= 0.16F) &&
        (frame.harmonicConfidence <= 0.10F) &&
        (frame.dynamicRangeDb <= 11.0F);
    const bool tinnyHighEnd =
        (frame.spectralCentroidHz >= (profile.brightnessBaseline * 1.10F)) &&
        (frame.lowBandRatio <= (profile.meanLowBandRatio + 0.01F)) &&
        (frame.upperMidRatio >= (profile.meanUpperMidRatio + 0.04F)) &&
        (frame.airRatio <= (profile.meanAirRatio + 0.04F));

    if (fragile >= 0.72F) {
      hotspots.push_back(
          Hotspot{frame.startSeconds, frame.endSeconds, "fragile_segment",
                  fragile,
                  "High flatness and instability suggested source-baked or "
                  "fragile material."});
      continue;
    }

    if (metallicPipeResonance) {
      hotspots.push_back(
          Hotspot{frame.startSeconds, frame.endSeconds, "metallic_pipe_resonance",
                  std::max(harshness, bright),
                  "A narrow, synthetic upper-mid edge was detected: bright but "
                  "weakly harmonic, with elevated upper-mid and HF emphasis."});
      continue;
    }

    if (whiteNoiseLikeTopEnd) {
      hotspots.push_back(
          Hotspot{frame.startSeconds, frame.endSeconds, "white_noise_like_top_end",
                  std::max(harshness, fragile),
                  "Flat, hiss-like high-frequency energy rose above the file "
                  "baseline and resembled broadband top-end noise."});
      continue;
    }

    if (voiceInstability) {
      hotspots.push_back(
          Hotspot{frame.startSeconds, frame.endSeconds, "voice_instability",
                  std::max(fragile, bright),
                  "Rapid spectral and pitch instability suggested a voice "
                  "identity shift or generator inconsistency."});
      continue;
    }

    if (harshness >= 0.68F) {
      hotspots.push_back(
          Hotspot{frame.startSeconds, frame.endSeconds, "harshness_hotspot",
                  harshness,
                  "Upper-mid energy, sibilance, and flatness spiked above the "
                  "file baseline."});
      continue;
    }

    if (tinnyHighEnd) {
      hotspots.push_back(
          Hotspot{frame.startSeconds, frame.endSeconds, "tinny_high_end", bright,
                  "The segment skewed bright and upper-mid forward while lacking "
                  "lower-body support, suggesting a tinny vocal balance."});
      continue;
    }

    if ((bright >= 0.72F) && (bright > harshness)) {
      hotspots.push_back(
          Hotspot{frame.startSeconds, frame.endSeconds,
                  "protected_bright_segment", bright,
                  "Persistent bright harmonic content looked intentional and "
                  "deserved guardrails."});
    }
  }

  return hotspots;
}

}  // namespace

AnalysisEngine::AnalysisEngine(std::size_t windowSize, std::size_t hopSize)
    : windowSize_(windowSize), hopSize_(hopSize) {}

AnalysisResult AnalysisEngine::analyze(const audio::AudioBuffer& buffer) const {
  AnalysisResult result;
  if ((buffer.channelCount() == 0U) || (buffer.frameCount() == 0U)) {
    return result;
  }

  const std::size_t estimatedFrameCount =
      ((buffer.frameCount() - 1U) / hopSize_) + 1U;
  const auto window = buildHannWindow(windowSize_);
  const FftPlan fft(windowSize_);
  std::vector<std::complex<float>> fftOutput;
  std::vector<float> analysisFrame(windowSize_, 0.0F);
  std::vector<float> pitchFrame(windowSize_, 0.0F);
  std::vector<float> magnitude(windowSize_ / 2U + 1U, 0.0F);
  std::vector<float> previousSpectrum(windowSize_ / 2U + 1U, 0.0F);
  std::vector<float> yinBuffer((windowSize_ / 2U) + 2U, 0.0F);
  float previousPitchEstimateHz = 0.0F;
  float previousPitchConfidence = 0.0F;
  std::vector<std::size_t> spectrogramColumnCounts;
  std::vector<std::pair<std::size_t, std::size_t>> spectrogramFrequencyRanges;
  initializeSpectrogram(result.spectrogram, buffer.sampleRate(),
                        magnitude.size(), estimatedFrameCount,
                        spectrogramColumnCounts, spectrogramFrequencyRanges);

  std::vector<float> rmsValues;
  std::vector<float> centroidValues;
  std::vector<float> lowBandRatios;
  std::vector<float> upperMidRatios;
  std::vector<float> airRatios;
  std::vector<float> flatnessValues;
  std::vector<float> transientnessValues;
  std::vector<float> sibilantValues;
  std::vector<float> highFrequencyBalances;
  std::vector<float> pitchValues;
  std::vector<float> changeScores;
  result.frames.reserve(estimatedFrameCount);
  rmsValues.reserve(estimatedFrameCount);
  centroidValues.reserve(estimatedFrameCount);
  lowBandRatios.reserve(estimatedFrameCount);
  upperMidRatios.reserve(estimatedFrameCount);
  airRatios.reserve(estimatedFrameCount);
  flatnessValues.reserve(estimatedFrameCount);
  transientnessValues.reserve(estimatedFrameCount);
  sibilantValues.reserve(estimatedFrameCount);
  highFrequencyBalances.reserve(estimatedFrameCount);
  pitchValues.reserve(estimatedFrameCount);
  changeScores.reserve(estimatedFrameCount);

  const float monoScale = 1.0F / static_cast<float>(buffer.channelCount());
  auto monoSampleAt = [&](std::size_t sampleIndex) {
    float sample = 0.0F;
    for (std::size_t channel = 0; channel < buffer.channelCount(); ++channel) {
      sample += buffer.channel(channel)[sampleIndex];
    }
    return sample * monoScale;
  };

  for (std::size_t frameIndex = 0, start = 0; start < buffer.frameCount();
       ++frameIndex, start += hopSize_) {
    std::fill(analysisFrame.begin(), analysisFrame.end(), 0.0F);
    std::fill(pitchFrame.begin(), pitchFrame.end(), 0.0F);
    const std::size_t availableSamples =
        std::min(windowSize_, buffer.frameCount() - start);

    float peakAbs = 0.0F;
    float rmsSum = 0.0F;
    for (std::size_t sample = 0; sample < availableSamples; ++sample) {
      const float value = monoSampleAt(start + sample);
      peakAbs = std::max(peakAbs, std::abs(value));
      rmsSum += value * value;
      pitchFrame[sample] = value;
      analysisFrame[sample] = value * window[sample];
    }

    const float rms =
        std::sqrt(rmsSum / static_cast<float>(std::max<std::size_t>(
                          availableSamples, static_cast<std::size_t>(1U))));

    fft.transform(analysisFrame, fftOutput);

    std::fill(magnitude.begin(), magnitude.end(), 0.0F);
    float totalEnergy = 0.0F;
    float centroidEnergy = 0.0F;
    float maxBinEnergy = 0.0F;

    for (std::size_t bin = 0; bin < magnitude.size(); ++bin) {
      const float energy = std::norm(fftOutput[bin]);
      magnitude[bin] = energy;
      totalEnergy += energy;
      const float frequency =
          static_cast<float>(bin) * static_cast<float>(buffer.sampleRate()) /
          static_cast<float>(windowSize_);
      centroidEnergy += frequency * energy;

      if ((frequency >= 80.0F) && (frequency <= 1000.0F) &&
          (energy > maxBinEnergy)) {
        maxBinEnergy = energy;
      }
    }

    float flatnessGeo = 0.0F;
    float flatnessArith = 0.0F;
    for (std::size_t bin = 1; bin < magnitude.size(); ++bin) {
      flatnessGeo += std::log(magnitude[bin] + core::kEpsilon);
      flatnessArith += magnitude[bin];
    }
    const float spectralFlatness =
        std::exp(flatnessGeo / static_cast<float>(magnitude.size() - 1U)) /
        ((flatnessArith / static_cast<float>(magnitude.size() - 1U)) +
         core::kEpsilon);

    const float voiceBandEnergy = bandEnergy(magnitude, buffer.sampleRate(),
                                             80.0F, 12000.0F) +
                                  core::kEpsilon;
    const float bodyBandEnergy =
        bandEnergy(magnitude, buffer.sampleRate(), 300.0F, 3000.0F) +
        core::kEpsilon;
    const float lowBandEnergy =
        bandEnergy(magnitude, buffer.sampleRate(), 20.0F, 120.0F);
    const float upperMidEnergy =
        bandEnergy(magnitude, buffer.sampleRate(), 2000.0F, 6000.0F);
    const float airEnergy =
        bandEnergy(magnitude, buffer.sampleRate(), 6000.0F, 10000.0F);
    const float sibilantEnergy =
        bandEnergy(magnitude, buffer.sampleRate(), 5000.0F, 9000.0F);

    accumulateSpectrogramFrame(result.spectrogram, magnitude, frameIndex,
                               estimatedFrameCount, spectrogramFrequencyRanges,
                               spectrogramColumnCounts);

    float flux = 0.0F;
    for (std::size_t bin = 0; bin < magnitude.size(); ++bin) {
      flux += std::max(0.0F, magnitude[bin] - previousSpectrum[bin]);
    }
    previousSpectrum = magnitude;

    const float crestFactorDb = core::linearToDb((peakAbs + core::kEpsilon) /
                                                 (rms + core::kEpsilon));
    const float transientness = core::clamp(
        (0.5F * core::normalizeRange(crestFactorDb, 6.0F, 18.0F)) +
            (0.5F * core::normalizeRange(flux / voiceBandEnergy, 0.01F, 0.24F)),
        0.0F, 1.0F);

    const PitchEstimate rawPitchEstimate =
        estimatePitchYin(pitchFrame, availableSamples, buffer.sampleRate(),
                         yinBuffer);
    const PitchEstimate smoothedPitchEstimate =
        smoothPitchEstimate(rawPitchEstimate, previousPitchEstimateHz,
                            previousPitchConfidence, spectralFlatness);
    const float pitchEstimateHz = smoothedPitchEstimate.frequencyHz;
    const float harmonicConfidence = core::clamp(
        (0.60F * (maxBinEnergy / voiceBandEnergy)) +
            (0.40F * smoothedPitchEstimate.confidence),
        0.0F, 1.0F);
    if (pitchEstimateHz > 0.0F) {
      previousPitchEstimateHz = pitchEstimateHz;
      previousPitchConfidence = smoothedPitchEstimate.confidence;
    } else {
      previousPitchConfidence *= 0.75F;
      if (previousPitchConfidence < 0.10F) {
        previousPitchEstimateHz = 0.0F;
      }
    }

    FrameFeatures features;
    features.frameIndex = frameIndex;
    features.sampleOffset = start;
    features.sampleCount = std::min(hopSize_, buffer.frameCount() - start);
    features.startSeconds =
        static_cast<double>(start) / static_cast<double>(buffer.sampleRate());
    features.endSeconds =
        static_cast<double>(start + availableSamples) /
        static_cast<double>(buffer.sampleRate());
    features.rmsDbfs = core::linearToDb(rms + core::kEpsilon);
    features.peakDbfs = core::linearToDb(peakAbs + core::kEpsilon);
    features.spectralCentroidHz = centroidEnergy / (totalEnergy + core::kEpsilon);
    features.lowBandRatio = lowBandEnergy / voiceBandEnergy;
    features.upperMidRatio = upperMidEnergy / voiceBandEnergy;
    features.airRatio = airEnergy / voiceBandEnergy;
    features.spectralFlatness = spectralFlatness;
    features.transientness = transientness;
    features.sibilantActivity = sibilantEnergy / voiceBandEnergy;
    features.highFrequencyBalance = (upperMidEnergy + airEnergy) / bodyBandEnergy;
    features.dynamicRangeDb = features.peakDbfs - features.rmsDbfs;
    features.pitchEstimateHz = pitchEstimateHz;
    features.harmonicConfidence = harmonicConfidence;

    if (!result.frames.empty()) {
      const auto& previous = result.frames.back();
      features.changeScore = core::clamp(
          (0.35F * core::normalizeRange(
                        std::abs(features.spectralCentroidHz -
                                 previous.spectralCentroidHz),
                        80.0F, 1200.0F)) +
              (0.30F * core::normalizeRange(
                           std::abs(features.upperMidRatio -
                                    previous.upperMidRatio),
                           0.01F, 0.15F)) +
              (0.20F * core::normalizeRange(
                           std::abs(features.airRatio - previous.airRatio), 0.01F,
                           0.12F)) +
              (0.15F * core::normalizeRange(
                           std::abs(features.pitchEstimateHz -
                                    previous.pitchEstimateHz),
                           12.0F, 180.0F)),
          0.0F, 1.0F);
    }

    result.frames.push_back(features);
    rmsValues.push_back(features.rmsDbfs);
    centroidValues.push_back(features.spectralCentroidHz);
    lowBandRatios.push_back(features.lowBandRatio);
    upperMidRatios.push_back(features.upperMidRatio);
    airRatios.push_back(features.airRatio);
    flatnessValues.push_back(features.spectralFlatness);
    transientnessValues.push_back(features.transientness);
    sibilantValues.push_back(features.sibilantActivity);
    highFrequencyBalances.push_back(features.highFrequencyBalance);
    changeScores.push_back(features.changeScore);
    if (features.pitchEstimateHz > 0.0F) {
      pitchValues.push_back(features.pitchEstimateHz);
    }
  }

  const float peakAbs = buffer.peakAbs();
  double integratedRmsSum = 0.0;
  for (std::size_t sample = 0; sample < buffer.frameCount(); ++sample) {
    const double monoSample = static_cast<double>(monoSampleAt(sample));
    integratedRmsSum += monoSample * monoSample;
  }
  const float integratedRms =
      std::sqrt(static_cast<float>(integratedRmsSum) /
                static_cast<float>(std::max<std::size_t>(
                    buffer.frameCount(), static_cast<std::size_t>(1U))));

  result.profile.integratedRmsDbfs = core::linearToDb(integratedRms);
  result.profile.peakDbfs = core::linearToDb(peakAbs + core::kEpsilon);
  result.profile.dynamicRangeDb =
      percentile(rmsValues, 0.95F) - percentile(rmsValues, 0.10F);
  result.profile.meanSpectralCentroidHz = meanOf(centroidValues);
  result.profile.meanLowBandRatio = meanOf(lowBandRatios);
  result.profile.meanUpperMidRatio = meanOf(upperMidRatios);
  result.profile.meanAirRatio = meanOf(airRatios);
  result.profile.meanFlatness = meanOf(flatnessValues);
  result.profile.meanSibilantActivity = meanOf(sibilantValues);
  result.profile.meanTransientness = meanOf(transientnessValues);
  result.profile.meanHighFrequencyBalance = meanOf(highFrequencyBalances);
  result.profile.voicedFrameRatio =
      static_cast<float>(pitchValues.size()) /
      static_cast<float>(std::max<std::size_t>(
          result.frames.size(), static_cast<std::size_t>(1U)));
  result.profile.brightnessBaseline =
      percentile(centroidValues, 0.75F) + (percentile(airRatios, 0.75F) * 800.0F);
  result.profile.harshnessBaseline =
      percentile(upperMidRatios, 0.75F) + percentile(sibilantValues, 0.75F);

  const float pitchVariance =
      calculateVariance(pitchValues, meanOf(pitchValues));
  result.profile.fragilityScore = core::clamp(
      (0.35F * core::normalizeRange(result.profile.meanFlatness, 0.12F, 0.48F)) +
          (0.25F *
           (1.0F -
            core::normalizeRange(result.profile.dynamicRangeDb, 8.0F, 20.0F))) +
          (0.20F * core::normalizeRange(percentile(changeScores, 0.75F), 0.03F,
                                        0.24F)) +
          (0.20F * core::normalizeRange(
                        result.profile.meanHighFrequencyBalance, 0.55F, 1.40F)),
      0.0F, 1.0F);

  result.profile.sourceBakedScore = core::clamp(
      (0.55F * result.profile.fragilityScore) +
          (0.25F * core::normalizeRange(result.profile.meanFlatness, 0.20F,
                                        0.60F)) +
          (0.20F * core::normalizeRange(std::sqrt(pitchVariance), 15.0F,
                                        120.0F)),
      0.0F, 1.0F);

  const LoudnessMeter loudnessMeter;
  const LoudnessStats loudness = loudnessMeter.measure(buffer);
  result.profile.integratedLufs = loudness.integratedLufs;
  result.profile.loudnessRangeLu = loudness.loudnessRangeLu;
  result.profile.momentaryMaxLufs = loudness.momentaryMaxLufs;
  result.profile.shortTermMaxLufs = loudness.shortTermMaxLufs;
  result.profile.truePeakDbtp = loudness.truePeakDbtp;
  result.spectrogram.durationSeconds =
      static_cast<double>(buffer.frameCount()) /
      static_cast<double>(buffer.sampleRate());
  finalizeSpectrogram(result.spectrogram, spectrogramColumnCounts);

  result.hotspots = buildHotspots(result.frames, result.profile);
  return result;
}

}  // namespace autoequalizer::analysis
