#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <istream>
#include <sstream>
#include <string>
#include <string_view>

#include "3dsmsc/config/settings.hpp"

namespace threedsmsc {
namespace {

std::string trim(std::string_view value) {
  std::size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }
  std::size_t last = value.size();
  while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
    --last;
  }
  return std::string(value.substr(first, last - first));
}

std::string unquote(std::string_view value) {
  std::string trimmed = trim(value);
  if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
    return trimmed.substr(1, trimmed.size() - 2);
  }
  return trimmed;
}

// Removes a trailing `# comment`, ignoring any `#` inside a quoted string (a path may contain one).
std::string strip_inline_comment(std::string_view value) {
  bool in_quotes = false;
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '"')
      in_quotes = !in_quotes;
    else if (value[index] == '#' && !in_quotes)
      return trim(value.substr(0, index));
  }
  return trim(value);
}

struct ConfigEntry {
  std::string_view section;
  std::string_view key;
  std::string_view value;
};

// One row per setting: where it lives in the file, and how its value is read into Settings.
using ValueParser = bool (*)(Settings& settings, std::string_view value);

struct SettingRule {
  std::string_view section;
  std::string_view key;
  ValueParser parse;
};

const SettingRule setting_rules[] = {
    {"library", "music_root",
     [](Settings& s, std::string_view v) {
       s.music_root = unquote(v);
       return true;
     }},
    {"library", "last_selected_root",
     [](Settings& s, std::string_view v) {
       s.last_selected_root = unquote(v);
       return true;
     }},
    {"library", "scan_scope",
     [](Settings& s, std::string_view v) { return parse_scan_scope(v, s.scan_scope); }},
    {"library", "force_full_scan",
     [](Settings& s, std::string_view v) { return parse_bool(v, s.force_full_scan); }},
    {"library", "exclude_hidden",
     [](Settings& s, std::string_view v) { return parse_bool(v, s.exclude_hidden); }},
    {"player", "volume", [](Settings& s, std::string_view v) { return parse_double(v, s.volume); }},
    {"player", "animation_enabled",
     [](Settings& s, std::string_view v) { return parse_bool(v, s.animation_enabled); }},
    {"player", "animation_speed",
     [](Settings& s, std::string_view v) { return parse_double(v, s.animation_speed); }},
    {"player", "search_case_sensitive",
     [](Settings& s, std::string_view v) { return parse_bool(v, s.search_case_sensitive); }},
    {"player", "background_playback",
     [](Settings& s, std::string_view v) { return parse_bool(v, s.background_playback); }},
    {"player", "theme", [](Settings& s, std::string_view v) { return parse_theme(v, s.theme); }},
    {"player", "battery_display",
     [](Settings& s, std::string_view v) { return parse_battery_display(v, s.battery_display); }},
    {"player", "repeat",
     [](Settings& s, std::string_view v) { return parse_repeat_mode(v, s.repeat); }},
    {"player", "shuffle", [](Settings& s, std::string_view v) { return parse_bool(v, s.shuffle); }},
    {"player", "seek_seconds",
     [](Settings& s, std::string_view v) { return parse_seek_seconds(v, s.seek_seconds); }},
    {"equalizer", "enabled",
     [](Settings& s, std::string_view v) { return parse_bool(v, s.eq_enabled); }},
    {"equalizer", "bands",
     [](Settings& s, std::string_view v) { return parse_eq_bands(v, s.eq_gains); }},
};

// The [controls] section: a button name per action, all plain strings.
struct ButtonRule {
  std::string_view key;
  std::string Settings::*member;
};

const ButtonRule button_rules[] = {
    {"play_pause", &Settings::play_pause_button},
    {"back", &Settings::back_button},
    {"next", &Settings::next_button},
    {"previous", &Settings::previous_button},
    {"seek_back", &Settings::seek_back_button},
    {"seek_forward", &Settings::seek_forward_button},
    {"volume_down", &Settings::volume_down_button},
    {"volume_up", &Settings::volume_up_button},
    {"exit", &Settings::exit_button},
};

// Returns false only for a known key whose value is invalid; unknown keys are ignored.
bool set_key(Settings& settings, const ConfigEntry& entry) {
  for (const SettingRule& rule : setting_rules) {
    if (rule.section == entry.section && rule.key == entry.key)
      return rule.parse(settings, entry.value);
  }
  if (entry.section == "controls") {
    for (const ButtonRule& rule : button_rules) {
      if (rule.key == entry.key)
        settings.*rule.member = unquote(entry.value);
    }
  }
  return true;
}

}

Settings default_settings() {
  Settings settings;
  settings.music_root = "sdmc:/3dsmsc/music/";
  settings.scan_scope = ScanScope::Default;
  settings.force_full_scan = false;
  settings.exclude_hidden = true;
  settings.volume = 0.8;
  settings.animation_enabled = true;
  settings.animation_speed = 1.0;
  settings.search_case_sensitive = false;
  settings.background_playback = true;
  settings.theme = Theme::Dark;
  settings.battery_display = BatteryDisplay::Icon;
  settings.repeat = RepeatMode::All;  // the queue has always looped
  settings.shuffle = false;
  settings.seek_seconds = 10;
  settings.eq_enabled = false;
  for (std::int8_t& gain : settings.eq_gains)
    gain = 0;
  settings.play_pause_button = "A";
  settings.back_button = "B";
  settings.next_button = "X";
  settings.previous_button = "Y";
  settings.seek_back_button = "Left";
  settings.seek_forward_button = "Right";
  settings.volume_down_button = "L";
  settings.volume_up_button = "R";
  settings.exit_button = "START";
  return settings;
}

namespace {
constexpr int seek_step_options[] = {5, 10, 15, 25};
}

bool parse_repeat_mode(std::string_view value, RepeatMode& mode) {
  const std::string normalized = unquote(value);
  if (normalized == "off")
    mode = RepeatMode::Off;
  else if (normalized == "all")
    mode = RepeatMode::All;
  else if (normalized == "one")
    mode = RepeatMode::One;
  else
    return false;
  return true;
}

bool parse_battery_display(std::string_view value, BatteryDisplay& display) {
  const std::string normalized = unquote(value);
  for (const BatteryDisplay candidate :
       {BatteryDisplay::Off, BatteryDisplay::Icon, BatteryDisplay::Percent}) {
    if (normalized == battery_display_name(candidate)) {
      display = candidate;
      return true;
    }
  }
  return false;
}

const char* battery_display_name(BatteryDisplay display) {
  switch (display) {
    case BatteryDisplay::Off:
      return "off";
    case BatteryDisplay::Percent:
      return "percent";
    case BatteryDisplay::Icon:
      break;
  }
  return "icon";
}

BatteryDisplay next_battery_display(BatteryDisplay display) {
  switch (display) {
    case BatteryDisplay::Icon:
      return BatteryDisplay::Percent;
    case BatteryDisplay::Percent:
      return BatteryDisplay::Off;
    case BatteryDisplay::Off:
      break;
  }
  return BatteryDisplay::Icon;
}

const char* repeat_mode_name(RepeatMode mode) {
  switch (mode) {
    case RepeatMode::Off:
      return "off";
    case RepeatMode::One:
      return "one";
    case RepeatMode::All:
      break;
  }
  return "all";
}

RepeatMode next_repeat_mode(RepeatMode mode) {
  switch (mode) {
    case RepeatMode::Off:
      return RepeatMode::All;
    case RepeatMode::All:
      return RepeatMode::One;
    case RepeatMode::One:
      break;
  }
  return RepeatMode::Off;
}

bool parse_seek_seconds(std::string_view value, int& seconds) {
  double parsed = 0.0;
  if (!parse_double(value, parsed))
    return false;
  for (const int option : seek_step_options) {
    if (parsed == static_cast<double>(option)) {
      seconds = option;
      return true;
    }
  }
  return false;  // only the offered steps are valid
}

int next_seek_seconds(int current) {
  const int count = static_cast<int>(sizeof(seek_step_options) / sizeof(seek_step_options[0]));
  for (int index = 0; index < count; ++index) {
    if (seek_step_options[index] == current)
      return seek_step_options[(index + 1) % count];
  }
  return seek_step_options[0];
}

bool parse_eq_bands(std::string_view value, std::int8_t* gains) {
  const std::string text = unquote(value);
  std::int8_t parsed[10] = {};
  std::size_t position = 0;
  for (int band = 0; band < 10; ++band) {
    if (position >= text.size())
      return false;  // fewer than ten values
    char* end = nullptr;
    const long number = std::strtol(text.c_str() + position, &end, 10);
    if (end == text.c_str() + position || number < -12 || number > 12)
      return false;
    parsed[band] = static_cast<std::int8_t>(number);
    position = static_cast<std::size_t>(end - text.c_str());
    if (band < 9) {
      if (position >= text.size() || text[position] != ',')
        return false;
      ++position;
    }
  }
  if (position != text.size())
    return false;  // trailing characters or an eleventh value
  for (int band = 0; band < 10; ++band)
    gains[band] = parsed[band];
  return true;
}

std::string format_eq_bands(const std::int8_t* gains) {
  std::string text;
  for (int band = 0; band < 10; ++band) {
    if (band != 0)
      text += ',';
    text += std::to_string(static_cast<int>(gains[band]));
  }
  return text;
}

bool parse_theme(std::string_view value, Theme& theme) {
  const std::string normalized = unquote(value);
  if (normalized == "dark") {
    theme = Theme::Dark;
    return true;
  }
  if (normalized == "light") {
    theme = Theme::Light;
    return true;
  }
  return false;
}

bool parse_scan_scope(std::string_view value, ScanScope& scope) {
  const std::string normalized = unquote(value);
  if (normalized == "default") {
    scope = ScanScope::Default;
    return true;
  }
  if (normalized == "selected") {
    scope = ScanScope::Selected;
    return true;
  }
  return false;
}

bool parse_bool(std::string_view value, bool& result) {
  const std::string normalized = trim(value);
  if (normalized == "true") {
    result = true;
    return true;
  }
  if (normalized == "false") {
    result = false;
    return true;
  }
  return false;
}

bool parse_double(std::string_view value, double& result) {
  const std::string normalized = trim(value);
  if (normalized.empty()) {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const double parsed = std::strtod(normalized.c_str(), &end);
  if (errno == ERANGE || end != normalized.c_str() + normalized.size()) {
    return false;
  }
  result = parsed;
  return true;
}

double clamp_unit_interval(double value) {
  if (value < 0.0)
    return 0.0;
  if (value > 1.0)
    return 1.0;
  return value;
}

bool load_settings(std::istream& input, Settings& settings, std::string* error) {
  Settings parsed = default_settings();
  std::string section;
  std::string line;
  std::size_t line_number = 0;

  while (std::getline(input, line)) {
    ++line_number;
    const std::string content = trim(line);
    if (content.empty() || content.front() == '#') {
      continue;
    }
    if (content.front() == '[' && content.back() == ']') {
      section = trim(std::string_view(content).substr(1, content.size() - 2));
      continue;
    }
    const std::size_t separator = content.find('=');
    if (separator == std::string::npos) {
      if (error != nullptr)
        *error = "invalid line " + std::to_string(line_number);
      settings = default_settings();
      return false;
    }
    const std::string key = trim(std::string_view(content).substr(0, separator));
    const std::string value = strip_inline_comment(std::string_view(content).substr(separator + 1));
    if (!set_key(parsed, ConfigEntry{section, key, value})) {
      if (error != nullptr) {
        *error = "invalid value for ";
        *error += section;
        *error += '.';
        *error += key;
      }
      settings = default_settings();
      return false;
    }
  }

  if (parsed.music_root.empty()) {
    if (error != nullptr)
      *error = "library.music_root must not be empty";
    settings = default_settings();
    return false;
  }
  parsed.volume = clamp_unit_interval(parsed.volume);
  if (parsed.animation_speed < 0.1)
    parsed.animation_speed = 0.1;
  if (parsed.animation_speed > 4.0)
    parsed.animation_speed = 4.0;
  settings = parsed;
  return true;
}

}
