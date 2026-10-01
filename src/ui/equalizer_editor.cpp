#include <cstddef>
#include <string>

#include "3dsmsc/ui/equalizer_editor.hpp"
#include "3dsmsc/ui/layout.hpp"

namespace threedsmsc {

void EqualizerEditor::changed() {
  unsaved_ = true;
}

void EqualizerEditor::select(int band) {
  if (band >= 0 && band < eq_band_count)
    selected_ = band;
}

void EqualizerEditor::select_previous() {
  selected_ = (selected_ + eq_band_count - 1) % eq_band_count;
}

void EqualizerEditor::select_next() {
  selected_ = (selected_ + 1) % eq_band_count;
}

int EqualizerEditor::band_at(int x) {
  // Float division on purpose: the bars are 29.2 px apart, and truncating the pitch to 29 would
  // drift a whole bar by the right-hand end.
  const auto band = static_cast<int>(static_cast<float>(x - layout::eq_bars_left) /
                                     (static_cast<float>(layout::eq_bars_width) / eq_band_count));
  if (band < 0)
    return 0;
  return band >= eq_band_count ? eq_band_count - 1 : band;
}

int EqualizerEditor::gain_at(int y) {
  const float gain = static_cast<float>(layout::eq_zero_y - y) * static_cast<float>(eq_max_db) /
                     static_cast<float>(layout::eq_half_range_px);
  const int rounded = static_cast<int>(gain + (gain >= 0.0F ? 0.5F : -0.5F));
  if (rounded > eq_max_db)
    return eq_max_db;
  return rounded < eq_min_db ? eq_min_db : rounded;
}

bool EqualizerEditor::set_gain(int band, int gain) {
  if (band < 0 || band >= eq_band_count)
    return false;
  if (gain > eq_max_db)
    gain = eq_max_db;
  if (gain < eq_min_db)
    gain = eq_min_db;
  if (settings_.eq_gains[band] == gain && settings_.eq_enabled)
    return false;
  settings_.eq_gains[band] = static_cast<std::int8_t>(gain);
  settings_.eq_enabled = true;
  changed();
  return true;
}

bool EqualizerEditor::toggle() {
  settings_.eq_enabled = !settings_.eq_enabled;
  changed();
  return true;
}

bool EqualizerEditor::next_preset() {
  std::size_t count = 0;
  const EqPreset* presets = eq_presets(count);
  const std::size_t next = eq_next_preset(settings_.eq_gains);
  for (int band = 0; band < eq_band_count; ++band)
    settings_.eq_gains[band] = presets[next].gains[band];
  settings_.eq_enabled = true;
  changed();
  status_ = std::string("Preset: ") + presets[next].name;
  has_status_ = true;
  return true;
}

bool EqualizerEditor::reset() {
  for (std::int8_t& gain : settings_.eq_gains)
    gain = 0;
  changed();
  status_ = "Equalizer reset to flat";
  has_status_ = true;
  return true;
}

void EqualizerEditor::begin_drag(int band) {
  dragging_ = band;
  selected_ = band;
}

bool EqualizerEditor::take_unsaved() {
  const bool unsaved = unsaved_;
  unsaved_ = false;
  return unsaved;
}

bool EqualizerEditor::take_status(std::string& status) {
  if (!has_status_)
    return false;
  status = std::move(status_);
  status_.clear();
  has_status_ = false;
  return true;
}

}
