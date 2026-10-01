#pragma once

#include <cstdint>
#include <istream>
#include <string>
#include <string_view>

namespace threedsmsc {

enum class ScanScope : std::uint8_t {
  Default,
  Selected,
};

enum class Theme : std::uint8_t {
  Dark,
  Light,
};

struct Settings {
  std::string music_root;
  std::string last_selected_root;
  ScanScope scan_scope;
  bool force_full_scan;
  bool exclude_hidden;
  double volume;
  bool animation_enabled;
  double animation_speed;
  bool search_case_sensitive;
  bool background_playback;
  Theme theme;
  int seek_seconds;  // step of the seek buttons; one of seek_step_options
  std::string play_pause_button;
  std::string back_button;
  std::string next_button;
  std::string previous_button;
  std::string seek_back_button;
  std::string seek_forward_button;
  std::string volume_down_button;
  std::string volume_up_button;
  std::string exit_button;
};

Settings default_settings();
bool load_settings(std::istream& input, Settings& settings, std::string* error = nullptr);
bool parse_scan_scope(std::string_view value, ScanScope& scope);
bool parse_theme(std::string_view value, Theme& theme);
bool parse_seek_seconds(std::string_view value, int& seconds);
// The next step in 5 -> 10 -> 15 -> 25 -> 5; any other value maps to the first option.
int next_seek_seconds(int current);
bool parse_bool(std::string_view value, bool& result);
bool parse_double(std::string_view value, double& result);
double clamp_unit_interval(double value);

}
