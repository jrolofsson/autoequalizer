#include "dsp/Processors.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace autoequalizer::dsp {

void BiquadFilter::reset() {
  z1_ = 0.0F;
  z2_ = 0.0F;
}

void BiquadFilter::setNormalizedCoefficients(float b0,
                                             float b1,
                                             float b2,
                                             float a0,
                                             float a1,
                                             float a2) {
  if (std::abs(a0) < core::kEpsilon) {
    b0_ = 1.0F;
    b1_ = 0.0F;
    b2_ = 0.0F;
    a1_ = 0.0F;
    a2_ = 0.0F;
    return;
  }

  b0_ = b0 / a0;
  b1_ = b1 / a0;
  b2_ = b2 / a0;
  a1_ = a1 / a0;
  a2_ = a2 / a0;
}

void BiquadFilter::setHighPass(int sampleRate, float cutoffHz, float q) {
  const float normalizedCutoff =
      core::clamp(cutoffHz, 10.0F, (static_cast<float>(sampleRate) * 0.45F));
  const float omega = (2.0F * std::numbers::pi_v<float> * normalizedCutoff) /
                      static_cast<float>(sampleRate);
  const float alpha = std::sin(omega) / (2.0F * q);
  const float cosine = std::cos(omega);

  setNormalizedCoefficients((1.0F + cosine) / 2.0F, -(1.0F + cosine),
                            (1.0F + cosine) / 2.0F, 1.0F + alpha,
                            -2.0F * cosine, 1.0F - alpha);
}

void BiquadFilter::setPeaking(int sampleRate,
                              float centerHz,
                              float q,
                              float gainDb) {
  if (std::abs(gainDb) < 0.001F) {
    setNormalizedCoefficients(1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F);
    return;
  }

  const float frequency =
      core::clamp(centerHz, 40.0F, static_cast<float>(sampleRate) * 0.45F);
  const float omega = (2.0F * std::numbers::pi_v<float> * frequency) /
                      static_cast<float>(sampleRate);
  const float alpha = std::sin(omega) / (2.0F * q);
  const float gain = std::pow(10.0F, gainDb / 40.0F);
  const float cosine = std::cos(omega);

  setNormalizedCoefficients(1.0F + (alpha * gain), -2.0F * cosine,
                            1.0F - (alpha * gain),
                            1.0F + (alpha / gain), -2.0F * cosine,
                            1.0F - (alpha / gain));
}

void BiquadFilter::setHighShelf(int sampleRate,
                                float centerHz,
                                float slope,
                                float gainDb) {
  if (std::abs(gainDb) < 0.001F) {
    setNormalizedCoefficients(1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F);
    return;
  }

  const float gain = std::pow(10.0F, gainDb / 40.0F);
  const float omega = (2.0F * std::numbers::pi_v<float> * centerHz) /
                      static_cast<float>(sampleRate);
  const float sine = std::sin(omega);
  const float cosine = std::cos(omega);
  const float beta = std::sqrt(gain) / core::clamp(slope, 0.2F, 2.0F);
  setNormalizedCoefficients(
      gain * ((gain + 1.0F) + ((gain - 1.0F) * cosine) + (beta * sine)),
      -2.0F * gain * ((gain - 1.0F) + ((gain + 1.0F) * cosine)),
      gain * ((gain + 1.0F) + ((gain - 1.0F) * cosine) - (beta * sine)),
      (gain + 1.0F) - ((gain - 1.0F) * cosine) + (beta * sine),
      2.0F * ((gain - 1.0F) - ((gain + 1.0F) * cosine)),
      (gain + 1.0F) - ((gain - 1.0F) * cosine) - (beta * sine));
}

float BiquadFilter::processSample(float input) {
  const float output = (b0_ * input) + z1_;
  z1_ = (b1_ * input) - (a1_ * output) + z2_;
  z2_ = (b2_ * input) - (a2_ * output);
  return output;
}

void Compressor::reset() { smoothedGainDb_ = 0.0F; }

float Compressor::processSample(float input) {
  const float inputDb = core::linearToDb(std::abs(input) + core::kEpsilon);
  float targetGainDb = 0.0F;

  if (inputDb > settings_.thresholdDb) {
    const float compressedDb =
        settings_.thresholdDb +
        ((inputDb - settings_.thresholdDb) /
         core::clamp(settings_.ratio, 1.0F, 20.0F));
    targetGainDb = compressedDb - inputDb;
  }

  const float attack = std::exp(-1.0F /
                                (0.001F * settings_.attackMs *
                                 static_cast<float>(sampleRate_) + 1.0F));
  const float release = std::exp(-1.0F /
                                 (0.001F * settings_.releaseMs *
                                  static_cast<float>(sampleRate_) + 1.0F));

  const float coefficient =
      (targetGainDb < smoothedGainDb_) ? attack : release;
  smoothedGainDb_ =
      ((1.0F - coefficient) * targetGainDb) + (coefficient * smoothedGainDb_);

  return input *
         core::dbToLinear(smoothedGainDb_ + settings_.makeupGainDb);
}

DeEsser::DeEsser(int sampleRate) : sampleRate_(sampleRate) {
  detectorHighPass_.setHighPass(sampleRate_, settings_.splitFrequencyHz);
}

void DeEsser::reset() {
  detectorHighPass_.reset();
  envelope_ = 0.0F;
}

void DeEsser::setSampleRate(int sampleRate) {
  sampleRate_ = sampleRate;
  detectorHighPass_.setHighPass(sampleRate_, settings_.splitFrequencyHz);
}

void DeEsser::setSettings(const DeEsserSettings& settings) {
  settings_ = settings;
  detectorHighPass_.setHighPass(sampleRate_, settings_.splitFrequencyHz);
}

float DeEsser::processSample(float input) {
  const float highBand = detectorHighPass_.processSample(input);
  const float detector = std::abs(highBand);

  const float attack = std::exp(-1.0F /
                                (0.001F * settings_.attackMs *
                                 static_cast<float>(sampleRate_) + 1.0F));
  const float release = std::exp(-1.0F /
                                 (0.001F * settings_.releaseMs *
                                  static_cast<float>(sampleRate_) + 1.0F));
  const float coefficient = (detector > envelope_) ? attack : release;
  envelope_ = ((1.0F - coefficient) * detector) + (coefficient * envelope_);

  const float activity = core::normalizeRange(
      envelope_, settings_.threshold, settings_.threshold + 0.20F);
  const float reductionDb = activity * settings_.maxReductionDb;
  const float gain = core::dbToLinear(-reductionDb);
  return input - (highBand * (1.0F - gain));
}

AdaptiveDspChain::AdaptiveDspChain(int sampleRate)
    : sampleRate_(sampleRate), deEsser_(sampleRate), compressor_(sampleRate) {}

void AdaptiveDspChain::reset() {
  highPass_.reset();
  harshEq_.reset();
  hfShelf_.reset();
  deEsser_.reset();
  compressor_.reset();
}

void AdaptiveDspChain::setSampleRate(int sampleRate) {
  sampleRate_ = sampleRate;
  deEsser_.setSampleRate(sampleRate);
  compressor_.setSampleRate(sampleRate);
}

void AdaptiveDspChain::configure(const ChainSettings& settings) {
  highPass_.setHighPass(sampleRate_, settings.highPassCutoffHz);
  harshEq_.setPeaking(sampleRate_, settings.harshCenterHz, settings.harshQ,
                      -std::abs(settings.harshCutDb));
  hfShelf_.setHighShelf(sampleRate_, settings.hfShelfCenterHz, 0.8F,
                        -std::abs(settings.hfSmoothingDb));
  deEsser_.setSettings(settings.deEsser);
  compressor_.setSettings(settings.compressor);
}

float AdaptiveDspChain::processSample(float input) {
  float sample = highPass_.processSample(input);
  sample = harshEq_.processSample(sample);
  sample = deEsser_.processSample(sample);
  sample = hfShelf_.processSample(sample);
  sample = compressor_.processSample(sample);
  return sample;
}

TruePeakLimiter::TruePeakLimiter(int sampleRate) : sampleRate_(sampleRate) {}

void TruePeakLimiter::setSampleRate(int sampleRate) { sampleRate_ = sampleRate; }

void TruePeakLimiter::setSettings(const TruePeakLimiterSettings& settings) {
  settings_ = settings;
}

void TruePeakLimiter::reset() {}

void TruePeakLimiter::processInPlace(audio::AudioBuffer& buffer) const {
  if ((buffer.channelCount() == 0U) || (buffer.frameCount() == 0U)) {
    return;
  }

  const float ceilingLinear = core::dbToLinear(settings_.ceilingDbtp);
  const std::size_t lookaheadSamples = std::max<std::size_t>(
      1U, static_cast<std::size_t>((settings_.lookaheadMs * 0.001F) *
                                   static_cast<float>(sampleRate_)));
  const float attack = std::exp(-1.0F /
                                ((settings_.attackMs * 0.001F *
                                  static_cast<float>(sampleRate_)) +
                                 1.0F));
  const float release = std::exp(-1.0F /
                                 ((settings_.releaseMs * 0.001F *
                                   static_cast<float>(sampleRate_)) +
                                  1.0F));

  for (std::size_t channel = 0; channel < buffer.channelCount(); ++channel) {
    auto& samples = buffer.channel(channel);
    const std::vector<float> source = samples;
    std::vector<float> delay(lookaheadSamples, 0.0F);
    std::size_t index = 0U;
    float gain = 1.0F;

    for (std::size_t frame = 0; frame < source.size(); ++frame) {
      const float currentSample = source[frame];
      const float delayed = delay[index];
      delay[index] = currentSample;
      index = (index + 1U) % delay.size();

      float localPeak = std::abs(currentSample);
      if ((frame + 1U) < source.size()) {
        localPeak = std::max(localPeak, std::abs(source[frame + 1U]));
      }

      const float targetGain =
          (localPeak > ceilingLinear) ? (ceilingLinear / localPeak) : 1.0F;
      const float coefficient = (targetGain < gain) ? attack : release;
      gain = ((1.0F - coefficient) * targetGain) + (coefficient * gain);
      samples[frame] = delayed * gain;
    }

    for (std::size_t frame = source.size();
         frame < (source.size() + delay.size()); ++frame) {
      const float delayed = delay[index];
      index = (index + 1U) % delay.size();
      const std::size_t targetIndex = frame - delay.size();
      if (targetIndex < samples.size()) {
        samples[targetIndex] = delayed * gain;
      }
    }
  }
}

audio::AudioBuffer TruePeakLimiter::process(const audio::AudioBuffer& input) const {
  audio::AudioBuffer output = input;
  processInPlace(output);
  return output;
}

}  // namespace autoequalizer::dsp
