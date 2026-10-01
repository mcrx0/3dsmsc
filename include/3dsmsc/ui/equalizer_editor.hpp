#pragma once

#include <string>

#include "3dsmsc/audio/equalizer.hpp"
#include "3dsmsc/config/settings.hpp"

namespace threedsmsc {

// The editing rules of the equalizer screen, apart from drawing and input: which band is
// selected, how a touch maps to a band and a gain, and what each action does to the settings.
// The mutating functions return true when something audible changed, so the caller knows to
// pass the new gains to the audio player.
class EqualizerEditor {
 public:
  explicit EqualizerEditor(Settings& settings) : settings_(settings) {}

  int selected() const { return selected_; }
  void select(int band);
  void select_previous();
  void select_next();

  // The bar under a touch at x, clamped to the first and last bar.
  static int band_at(int x);
  // The gain in dB a touch at y asks for, rounded and clamped to the allowed range.
  static int gain_at(int y);

  // Changing a gain switches the equalizer on, so a change is always audible.
  bool set_gain(int band, int gain);
  bool raise() { return set_gain(selected_, settings_.eq_gains[selected_] + 1); }
  bool lower() { return set_gain(selected_, settings_.eq_gains[selected_] - 1); }
  bool toggle();
  bool next_preset();
  bool reset();

  // Dragging a bar with the stylus: the band being dragged, or -1.
  int dragging() const { return dragging_; }
  void begin_drag(int band);
  void end_drag() { dragging_ = -1; }

  // True once after any change, until the settings have been saved.
  bool take_unsaved();
  // A message about the last change, for example "Preset: Rock". Returns false when there is none.
  bool take_status(std::string& status);

 private:
  void changed();

  Settings& settings_;
  int selected_ = eq_band_count / 2;
  int dragging_ = -1;
  bool unsaved_ = false;
  std::string status_;
  bool has_status_ = false;
};

}
