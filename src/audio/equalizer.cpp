#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "3dsmsc/audio/equalizer.hpp"

namespace threedsmsc {
namespace {

constexpr float band_frequencies[eq_band_count] = {31.0f,   62.0f,   125.0f,  250.0f,  500.0f,
                                                   1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};
// One octave between bands, so Q = sqrt(2) makes neighbouring bands overlap smoothly.
constexpr float band_q = 1.41421356f;
constexpr float pi = 3.14159265358979f;
// Added to the signal so filter memory never decays into denormal floats, which the ARM11's
// VFP handles in slow software. 0.001 of a 16-bit step is inaudible.
constexpr float anti_denormal = 0.001f;

constexpr EqPreset presets[] = {
    {"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"Bass boost", {6, 5, 4, 2, 0, 0, 0, 0, 0, 0}},
    {"Treble boost", {0, 0, 0, 0, 0, 1, 2, 4, 5, 6}},
    {"Vocal", {-2, -1, 0, 2, 4, 4, 3, 1, 0, -1}},
    {"Rock", {5, 4, 2, -1, -2, -1, 2, 4, 5, 5}},
    {"Pop", {-1, 2, 4, 5, 3, 0, -1, -1, -2, -2}},
    {"Loudness", {5, 3, 0, 0, -1, 1, 0, 0, 3, 5}},
};
constexpr std::size_t preset_count = sizeof(presets) / sizeof(presets[0]);

bool gains_match(const std::int8_t* a, const std::int8_t* b) {
  for (int band = 0; band < eq_band_count; ++band) {
    if (a[band] != b[band])
      return false;
  }
  return true;
}

}

float eq_band_frequency(int band) {
  return band >= 0 && band < eq_band_count ? band_frequencies[band] : 0.0f;
}

const EqPreset* eq_presets(std::size_t& count) {
  count = preset_count;
  return presets;
}

const char* eq_preset_name(const std::int8_t* gains) {
  for (const EqPreset& preset : presets) {
    if (gains_match(gains, preset.gains))
      return preset.name;
  }
  return "Custom";
}

std::size_t eq_next_preset(const std::int8_t* gains) {
  for (std::size_t index = 0; index < preset_count; ++index) {
    if (gains_match(gains, presets[index].gains))
      return (index + 1) % preset_count;
  }
  return 0;
}

void Equalizer::configure(const std::int8_t* gains, std::uint32_t sample_rate) {
  active_bands_ = 0;
  int loudest_boost = 0;
  for (int index = 0; index < eq_band_count; ++index) {
    Band& band = bands_[index];
    int gain = static_cast<int>(gains[index]);
    if (gain > eq_max_db)
      gain = eq_max_db;
    if (gain < eq_min_db)
      gain = eq_min_db;
    const float frequency = band_frequencies[index];
    // A band near or above the Nyquist limit (for example 16 kHz in 22 kHz audio) is skipped.
    if (gain == 0 || sample_rate == 0 || frequency >= 0.45f * static_cast<float>(sample_rate)) {
      band.enabled = false;
      continue;
    }
    // RBJ audio-EQ-cookbook peaking filter.
    const float amplitude = std::pow(10.0f, static_cast<float>(gain) / 40.0f);
    const float omega = 2.0f * pi * frequency / static_cast<float>(sample_rate);
    const float alpha = std::sin(omega) / (2.0f * band_q);
    const float cosine = std::cos(omega);
    const float a0 = 1.0f + alpha / amplitude;
    band.b0 = (1.0f + alpha * amplitude) / a0;
    band.b1 = (-2.0f * cosine) / a0;
    band.b2 = (1.0f - alpha * amplitude) / a0;
    band.a1 = (-2.0f * cosine) / a0;
    band.a2 = (1.0f - alpha / amplitude) / a0;
    band.enabled = true;
    active_list_[active_bands_++] = index;
    if (gain > loudest_boost)
      loudest_boost = gain;
  }
  // Boosted audio can exceed full scale, so leave half the loudest boost as headroom.
  preamp_ = std::pow(10.0f, -0.5f * static_cast<float>(loudest_boost) / 20.0f);
}

void Equalizer::reset() {
  for (Band& band : bands_) {
    band.z1[0] = band.z1[1] = 0.0f;
    band.z2[0] = band.z2[1] = 0.0f;
  }
}

void Equalizer::process(std::int16_t* stereo_frames, std::size_t frame_count) {
  if (active_bands_ == 0)
    return;
  for (std::size_t frame = 0; frame < frame_count; ++frame) {
    for (int channel = 0; channel < 2; ++channel) {
      float value =
          static_cast<float>(stereo_frames[frame * 2 + channel]) * preamp_ + anti_denormal;
      for (int slot = 0; slot < active_bands_; ++slot) {
        Band& band = bands_[active_list_[slot]];
        // Transposed direct form II.
        const float output = band.b0 * value + band.z1[channel];
        band.z1[channel] = band.b1 * value - band.a1 * output + band.z2[channel];
        band.z2[channel] = band.b2 * value - band.a2 * output;
        value = output;
      }
      int rounded = static_cast<int>(value + (value >= 0.0f ? 0.5f : -0.5f));
      if (rounded > 32767)
        rounded = 32767;
      if (rounded < -32768)
        rounded = -32768;
      stereo_frames[frame * 2 + channel] = static_cast<std::int16_t>(rounded);
    }
  }
}

}
