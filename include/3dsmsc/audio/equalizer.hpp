#pragma once

#include <cstddef>
#include <cstdint>

namespace threedsmsc {

constexpr int eq_band_count = 10;
constexpr int eq_min_db = -12;
constexpr int eq_max_db = 12;

// Centre frequency of a band in Hz: 31, 62, 125, 250, 500, 1k, 2k, 4k, 8k, 16k.
float eq_band_frequency(int band);

struct EqPreset {
  const char* name;
  std::int8_t gains[eq_band_count];
};

const EqPreset* eq_presets(std::size_t& count);
// Name of the preset whose gains match exactly, or "Custom".
const char* eq_preset_name(const std::int8_t* gains);
// Index of the preset after the one matching `gains` (the first when none match).
std::size_t eq_next_preset(const std::int8_t* gains);

// Ten peaking filters in series, applied to interleaved stereo 16-bit audio. Portable code:
// the 3DS audio thread calls it, and the host tests exercise the same implementation.
class Equalizer {
 public:
  // Recomputes the filters for these gains (dB per band) at the given sample rate. Filter
  // memory is kept, so changing a gain while playing does not click.
  void configure(const std::int8_t* gains, std::uint32_t sample_rate);
  // Clears the filter memory; call after a seek or when a new track starts.
  void reset();
  // False when every band is flat, so the caller can skip processing.
  bool active() const { return active_bands_ != 0; }
  void process(std::int16_t* stereo_frames, std::size_t frame_count);

 private:
  struct Band {
    bool enabled = false;
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1[2] = {0.0f, 0.0f};
    float z2[2] = {0.0f, 0.0f};
  };

  Band bands_[eq_band_count];
  int active_list_[eq_band_count] = {};  // indexes of the enabled bands, in order
  int active_bands_ = 0;
  float preamp_ = 1.0f;
};

}
