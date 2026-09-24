#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace threedsmsc {

struct CassetteView {
  std::string title;
  std::string artist;
  std::string album;
  float progress;
  float reel_phase;
  bool playing;
  bool has_track;
  int selected_panel;
  std::array<std::string, 5> list_items;
  std::size_t list_count;
  std::size_t list_selected;
  bool list_visible;
  std::string search_query;
  std::string artwork_path;
  std::string status;
};

}
