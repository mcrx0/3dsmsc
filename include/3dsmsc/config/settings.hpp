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

// What happens when a track ends: stop after the last track of the queue, start the queue over,
// or play the same track again.
enum class RepeatMode : std::uint8_t {
  Off,
  All,
  One,
};

// How the header shows the battery: not at all, as an icon, or as a percentage.
enum class BatteryDisplay : std::uint8_t {
  Off,
  Icon,
  Percent,
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
  BatteryDisplay battery_display;
  bool show_cover;  // a small cover image beside the title on the top screen
  RepeatMode repeat;
  bool shuffle;
  int seek_seconds;  // step of the seek buttons; one of seek_step_options
  bool eq_enabled;
  std::int8_t eq_gains[10];  // dB per equalizer band, -12..12
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
bool parse_battery_display(std::string_view value, BatteryDisplay& display);
const char* battery_display_name(BatteryDisplay display);  // "off", "icon" or "percent"
// Icon -> Percent -> Off -> Icon.
BatteryDisplay next_battery_display(BatteryDisplay display);
bool parse_repeat_mode(std::string_view value, RepeatMode& mode);
const char* repeat_mode_name(RepeatMode mode);  // "off", "all" or "one"
// Off -> All -> One -> Off.
RepeatMode next_repeat_mode(RepeatMode mode);
bool parse_seek_seconds(std::string_view value, int& seconds);
// Ten comma-separated whole dB values, each within -12..12, e.g. "0,2,4,0,0,0,0,0,0,-3".
bool parse_eq_bands(std::string_view value, std::int8_t* gains);
std::string format_eq_bands(const std::int8_t* gains);
// The next step in 5 -> 10 -> 15 -> 25 -> 5; any other value maps to the first option.
int next_seek_seconds(int current);
bool parse_bool(std::string_view value, bool& result);
bool parse_double(std::string_view value, double& result);
double clamp_unit_interval(double value);

}
