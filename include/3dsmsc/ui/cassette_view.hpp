#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "3dsmsc/ui/panel.hpp"

namespace threedsmsc {

// What a list row is, stored in CassetteView::list_check. Values 0 and 1 are an unchecked and a
// checked box (the folder picker); the rest are not selectable choices.
constexpr std::int8_t row_plain = -1;
constexpr std::int8_t row_header = -2;       // a section title
constexpr std::int8_t row_button_pair = -3;  // repeat and shuffle side by side
constexpr std::int8_t row_text = -4;         // a line of plain text
constexpr std::int8_t row_mapping = -5;      // "button|action"

// Every field has a default: the renderer reads all of them every frame, and a default-built
// view must never expose leftover memory (an uninitialised eq_visible once drew the equalizer
// editor over the home screen).
struct CassetteView {
  std::string title;
  std::string artist;
  std::string album;
  float progress = 0.0f;
  std::uint64_t position_ms = 0;
  std::uint64_t duration_ms = 0;
  float volume = 0.0f;
  bool show_cover = true;    // reserve a slot beside the title for the cover (or a placeholder)
  int battery_percent = -1;  // 0..100, or -1 when unknown (nothing is drawn)
  bool battery_charging = false;
  int battery_display = 1;  // 0 off, 1 icon, 2 percentage
  bool headphones = false;  // shows a small icon in the header while headphones are plugged in
  bool light_theme = false;
  int seek_seconds = 10;
  int repeat_mode = 0;  // 0 off, 1 all, 2 one
  bool shuffle = false;
  // Equalizer editor (the bottom screen shows it instead of the home screen or a list).
  bool eq_visible = false;
  bool eq_enabled = false;
  std::array<std::int8_t, 10> eq_gains = {};
  int eq_selected = 0;
  std::string eq_preset;
  std::size_t library_tracks = 0;
  std::size_t queue_tracks = 0;
  float reel_phase = 0.0f;
  bool playing = false;
  bool has_track = false;
  Panel selected_panel = Panel::Home;
  // One screenful of rows; list_selected is a row within this window.
  std::array<std::string, 5> list_items;
  // Per row: a row_* value, or 0 / 1 for an unchecked / checked box (the row then also shows a
  // chevron).
  std::array<std::int8_t, 5> list_check = {{row_plain, row_plain, row_plain, row_plain, row_plain}};
  int list_sub_selected = 0;  // which half of a button-pair row is selected: 0 left, 1 right
  std::size_t list_count = 0;
  std::size_t list_selected = 0;
  std::size_t list_total = 0;
  std::size_t list_position = 0;
  bool list_visible = false;
  std::string list_heading;
  std::string list_empty;
  std::string search_query;
  std::string library_path;
  std::string artwork_path;
  std::string status;
};

}
