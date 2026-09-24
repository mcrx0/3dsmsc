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
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }
  std::size_t last = value.size();
  while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
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

struct ConfigEntry {
  std::string_view section;
  std::string_view key;
  std::string_view value;
};

bool set_key(Settings& settings, const ConfigEntry& entry) {
  const std::string_view section = entry.section;
  const std::string_view key = entry.key;
  const std::string_view value = entry.value;
  if (section == "library") {
    if (key == "music_root") {
      settings.music_root = unquote(value);
    } else if (key == "last_selected_root") {
      settings.last_selected_root = unquote(value);
    } else if (key == "scan_scope") {
      return parse_scan_scope(value, settings.scan_scope);
    } else if (key == "force_full_scan") {
      return parse_bool(value, settings.force_full_scan);
    } else if (key == "exclude_hidden") {
      return parse_bool(value, settings.exclude_hidden);
    }
  } else if (section == "player") {
    if (key == "volume") {
      return parse_double(value, settings.volume);
    } else if (key == "animation_enabled") {
      return parse_bool(value, settings.animation_enabled);
    } else if (key == "animation_speed") {
      return parse_double(value, settings.animation_speed);
    } else if (key == "search_case_sensitive") {
      return parse_bool(value, settings.search_case_sensitive);
    } else if (key == "background_playback") {
      return parse_bool(value, settings.background_playback);
    }
  } else if (section == "controls") {
    std::string* target = nullptr;
    if (key == "play_pause") {
      target = &settings.play_pause_button;
    }
    if (key == "back") {
      target = &settings.back_button;
    }
    if (key == "next") {
      target = &settings.next_button;
    }
    if (key == "previous") {
      target = &settings.previous_button;
    }
    if (key == "seek_back") {
      target = &settings.seek_back_button;
    }
    if (key == "seek_forward") {
      target = &settings.seek_forward_button;
    }
    if (key == "volume_down") {
      target = &settings.volume_down_button;
    }
    if (key == "volume_up") {
      target = &settings.volume_up_button;
    }
    if (key == "exit") {
      target = &settings.exit_button;
    }
    if (target != nullptr) {
      *target = unquote(value);
    }
  }
  return true;
}

}

Settings default_settings() {
  Settings settings;
  settings.music_root = "sdmc:/3dmms/music/";
  settings.scan_scope = ScanScope::Default;
  settings.force_full_scan = false;
  settings.exclude_hidden = true;
  settings.volume = 0.8;
  settings.animation_enabled = true;
  settings.animation_speed = 1.0;
  settings.search_case_sensitive = false;
  settings.background_playback = true;
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
    const std::string value = trim(std::string_view(content).substr(separator + 1));
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
