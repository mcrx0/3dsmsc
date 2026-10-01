#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "3dsmsc/config/settings.hpp"
#include "3dsmsc/config/settings_file.hpp"
#include "3dsmsc/library/controller.hpp"
#include "3dsmsc/queue/playback_queue.hpp"
#include "3dsmsc/ui/browse_navigator.hpp"
#include "test_suites.hpp"
#include "test_support.hpp"

namespace {

using test::entry;
using test::FakeFileSystem;
using test::make_track;

void test_defaults() {
  const threedsmsc::Settings settings = threedsmsc::default_settings();
  assert(settings.music_root == "sdmc:/3dsmsc/music/");
  assert(settings.volume == 0.8);
  assert(settings.search_case_sensitive == false);
  assert(settings.background_playback == true);
}

void test_settings_loading() {
  std::istringstream input(
      "[library]\n"
      "music_root = \"sdmc:/custom/music/\"\n"
      "scan_scope = \"selected\"\n"
      "[player]\n"
      "volume = 2.0\n"
      "search_case_sensitive = true\n");
  threedsmsc::Settings settings;
  std::string error;
  const bool loaded = threedsmsc::load_settings(input, settings, &error);
  assert(loaded);
  assert(settings.music_root == "sdmc:/custom/music/");
  assert(settings.scan_scope == threedsmsc::ScanScope::Selected);
  assert(settings.volume == 1.0);
  assert(settings.search_case_sensitive);
}

void test_invalid_settings_fall_back() {
  std::istringstream input("[player]\nvolume = high\n");
  threedsmsc::Settings settings;
  settings.volume = 0.25;
  std::string error;
  assert(!threedsmsc::load_settings(input, settings, &error));
  assert(!error.empty());
  assert(settings.music_root == "sdmc:/3dsmsc/music/");
  assert(settings.volume == 0.8);
}

void test_inline_comments() {
  // Trailing comments are valid TOML, and a '#' inside a quoted value is not a comment.
  std::istringstream input(
      "[library]\nmusic_root = \"sdmc:/Music #1/\"  # my folder\n"
      "[player]\ntheme = \"light\"  # \"dark\" or \"light\"\nseek_seconds = 15 # step\n"
      "[equalizer]\nbands = \"1,2,3,4,5,6,7,8,9,10\"  # 31 Hz .. 16 kHz\n");
  threedsmsc::Settings settings;
  std::string error;
  assert(threedsmsc::load_settings(input, settings, &error));
  assert(settings.music_root == "sdmc:/Music #1/");
  assert(settings.theme == threedsmsc::Theme::Light);
  assert(settings.seek_seconds == 15);
  assert(settings.eq_gains[0] == 1 && settings.eq_gains[9] == 10);

  // The shipped example config must load, comments and all.
  std::ifstream example("config/config.toml.example");
  threedsmsc::Settings loaded;
  assert(example.is_open());
  assert(threedsmsc::load_settings(example, loaded, &error));
}

void test_settings_file() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "3dsmsc-config.toml";
  std::ofstream output(path);
  output << "[library]\n"
         << "music_root = \"/custom/music/\"\n";
  output.close();
  threedsmsc::Settings settings;
  std::string error;
  assert(threedsmsc::load_settings_file(path.string(), settings, &error));
  assert(settings.music_root == "/custom/music/");
  settings.volume = 0.35;
  settings.animation_enabled = false;
  assert(settings.theme == threedsmsc::Theme::Dark);
  settings.theme = threedsmsc::Theme::Light;
  assert(settings.battery_display == threedsmsc::BatteryDisplay::Icon);
  settings.battery_display = threedsmsc::BatteryDisplay::Percent;
  assert(settings.seek_seconds == 10);
  settings.seek_seconds = 25;
  assert(settings.repeat == threedsmsc::RepeatMode::All);  // the queue has always looped
  settings.repeat = threedsmsc::RepeatMode::One;
  assert(!settings.shuffle);
  settings.shuffle = true;
  assert(!settings.eq_enabled);
  settings.eq_enabled = true;
  settings.eq_gains[0] = 6;
  settings.eq_gains[9] = -12;
  settings.search_case_sensitive = true;
  settings.next_button = "X2";
  assert(threedsmsc::save_settings_file(path.string(), settings, &error));
  assert(threedsmsc::load_settings_file(path.string(), settings, &error));
  assert(settings.volume == 0.35);
  assert(!settings.animation_enabled);
  assert(settings.theme == threedsmsc::Theme::Light);
  assert(settings.battery_display == threedsmsc::BatteryDisplay::Percent);
  assert(settings.seek_seconds == 25);
  assert(settings.repeat == threedsmsc::RepeatMode::One);
  assert(settings.shuffle);
  assert(settings.eq_enabled && settings.eq_gains[0] == 6 && settings.eq_gains[9] == -12);
  assert(settings.eq_gains[5] == 0);
  assert(settings.search_case_sensitive);
  assert(settings.next_button == "X2");
  std::error_code cleanup_error;
  std::filesystem::remove(path, cleanup_error);
}

void test_seek_step() {
  assert(threedsmsc::next_seek_seconds(5) == 10);
  assert(threedsmsc::next_seek_seconds(10) == 15);
  assert(threedsmsc::next_seek_seconds(15) == 25);
  assert(threedsmsc::next_seek_seconds(25) == 5);
  assert(threedsmsc::next_seek_seconds(7) == 5);  // an unknown value restarts the cycle
  int seconds = 10;
  assert(threedsmsc::parse_seek_seconds("15", seconds) && seconds == 15);
  assert(!threedsmsc::parse_seek_seconds("12", seconds) && seconds == 15);  // unchanged
  assert(!threedsmsc::parse_seek_seconds("fast", seconds));
}

void test_equalizer_settings() {
  std::int8_t gains[10] = {};
  assert(threedsmsc::parse_eq_bands("\"0,2,4,0,0,0,0,0,0,-3\"", gains));
  assert(gains[1] == 2 && gains[2] == 4 && gains[9] == -3);
  const std::string text = threedsmsc::format_eq_bands(gains);
  assert(text == "0,2,4,0,0,0,0,0,0,-3");
  std::int8_t unchanged[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  assert(!threedsmsc::parse_eq_bands("1,2,3", unchanged));                  // too few
  assert(!threedsmsc::parse_eq_bands("0,0,0,0,0,0,0,0,0,0,0", unchanged));  // too many
  assert(!threedsmsc::parse_eq_bands("0,0,0,0,0,0,0,0,0,13", unchanged));   // out of range
  assert(!threedsmsc::parse_eq_bands("0,0,0,0,0,0,0,0,0,x", unchanged));    // not a number
  assert(unchanged[0] == 1 && unchanged[9] == 1);                           // untouched
}

void test_repeat_mode() {
  using threedsmsc::RepeatMode;
  assert(threedsmsc::next_repeat_mode(RepeatMode::Off) == RepeatMode::All);
  assert(threedsmsc::next_repeat_mode(RepeatMode::All) == RepeatMode::One);
  assert(threedsmsc::next_repeat_mode(RepeatMode::One) == RepeatMode::Off);
  RepeatMode mode = RepeatMode::All;
  assert(threedsmsc::parse_repeat_mode("\"off\"", mode) && mode == RepeatMode::Off);
  assert(threedsmsc::parse_repeat_mode("one", mode) && mode == RepeatMode::One);
  assert(!threedsmsc::parse_repeat_mode("shuffle", mode) && mode == RepeatMode::One);  // unchanged
  assert(std::string(threedsmsc::repeat_mode_name(RepeatMode::Off)) == "off");
  assert(std::string(threedsmsc::repeat_mode_name(RepeatMode::All)) == "all");
  assert(std::string(threedsmsc::repeat_mode_name(RepeatMode::One)) == "one");
}

}

void run_settings_tests() {
  test_defaults();
  test_settings_loading();
  test_invalid_settings_fall_back();
  test_inline_comments();
  test_settings_file();
  test_seek_step();
  test_equalizer_settings();
  test_repeat_mode();
}
