#include <fstream>
#include <iomanip>
#include <string>

#include "3dsmsc/config/settings.hpp"
#include "3dsmsc/config/settings_file.hpp"

namespace threedsmsc {
namespace {

const char* scan_scope_name(ScanScope scope) {
  return scope == ScanScope::Selected ? "selected" : "default";
}

bool write_string(std::ofstream& output, const std::string& value) {
  output << '"' << value << '"';
  return static_cast<bool>(output);
}

}

bool load_settings_file(const std::string& path, Settings& settings, std::string* error) {
  std::ifstream input(path.c_str());
  if (!input) {
    settings = default_settings();
    if (error != nullptr)
      *error = "configuration file not found";
    return false;
  }
  return load_settings(input, settings, error);
}

bool save_settings_file(const std::string& path, const Settings& settings, std::string* error) {
  std::ofstream output(path.c_str(), std::ios::trunc);
  if (!output) {
    if (error != nullptr)
      *error = "configuration file could not be opened";
    return false;
  }
  output << "[library]\n";
  output << "music_root = ";
  write_string(output, settings.music_root);
  output << '\n';
  output << "last_selected_root = ";
  write_string(output, settings.last_selected_root);
  output << '\n';
  output << "scan_scope = \"" << scan_scope_name(settings.scan_scope) << "\"\n";
  output << "force_full_scan = " << (settings.force_full_scan ? "true" : "false") << '\n';
  output << "exclude_hidden = " << (settings.exclude_hidden ? "true" : "false") << '\n';
  output << "[player]\n";
  output << std::fixed << std::setprecision(3) << "volume = " << settings.volume << '\n';
  output << "animation_enabled = " << (settings.animation_enabled ? "true" : "false") << '\n';
  output << "animation_speed = " << settings.animation_speed << '\n';
  output << "search_case_sensitive = " << (settings.search_case_sensitive ? "true" : "false")
         << '\n';
  output << "background_playback = " << (settings.background_playback ? "true" : "false") << '\n';
  output << "theme = \"" << (settings.theme == Theme::Light ? "light" : "dark") << "\"\n";
  output << "seek_seconds = " << settings.seek_seconds << '\n';
  output << "[controls]\n";
  output << "play_pause = ";
  write_string(output, settings.play_pause_button);
  output << '\n';
  output << "back = ";
  write_string(output, settings.back_button);
  output << '\n';
  output << "next = ";
  write_string(output, settings.next_button);
  output << '\n';
  output << "previous = ";
  write_string(output, settings.previous_button);
  output << '\n';
  output << "seek_back = ";
  write_string(output, settings.seek_back_button);
  output << '\n';
  output << "seek_forward = ";
  write_string(output, settings.seek_forward_button);
  output << '\n';
  output << "volume_down = ";
  write_string(output, settings.volume_down_button);
  output << '\n';
  output << "volume_up = ";
  write_string(output, settings.volume_up_button);
  output << '\n';
  output << "exit = ";
  write_string(output, settings.exit_button);
  output << '\n';
  if (!output.good()) {
    if (error != nullptr)
      *error = "configuration file could not be written";
    return false;
  }
  if (error != nullptr)
    error->clear();
  return true;
}

}
