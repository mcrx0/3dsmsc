#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace threedsmsc {

struct CassetteView {
  std::string title;
  std::string artist;
  std::string album;
  float progress;
  std::uint64_t position_ms;
  std::uint64_t duration_ms;
  float volume;
  bool light_theme;
  int seek_seconds;
  std::size_t library_tracks;
  std::size_t queue_tracks;
  float reel_phase;
  bool playing;
  bool has_track;
  int selected_panel;
  static constexpr std::size_t list_rows = 5;
  // One screenful of rows; list_selected is a row within this window.
  std::array<std::string, 5> list_items;
  // Per row: -1 no checkbox, 0 unchecked, 1 checked. Checkbox rows also show a chevron.
  std::array<std::int8_t, 5> list_check;
  std::size_t list_count;
  std::size_t list_selected;
  std::size_t list_total;
  std::size_t list_position;
  bool list_visible;
  std::string list_heading;
  std::string list_empty;
  std::string search_query;
  std::string library_path;
  std::string artwork_path;
  std::string status;
};

}
