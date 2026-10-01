#include <algorithm>
#include <cstddef>
#include <string>

#include "3dsmsc/audio/equalizer.hpp"
#include "3dsmsc/library/library_index.hpp"
#include "app.hpp"

namespace threedsmsc {
namespace {

// Settings rows, grouped by topic. The Header rows are section titles, not choices.
enum class SettingsRow : std::size_t {
  HeaderLibrary,
  Scan,
  Folders,
  HeaderSound,
  Equalizer,
  Repeat,
  Shuffle,
  Seek,
  Background,
  HeaderUi,
  Animation,
  Theme,
  Battery,
  HeaderMisc,
  Controls,
  About,
  Count
};

constexpr std::size_t as_row(SettingsRow row) {
  return static_cast<std::size_t>(row);
}

bool is_settings_header(std::size_t row) {
  return row == as_row(SettingsRow::HeaderLibrary) || row == as_row(SettingsRow::HeaderSound) ||
         row == as_row(SettingsRow::HeaderUi) || row == as_row(SettingsRow::HeaderMisc);
}

// Queue rows: 0 is the repeat/shuffle button pair, 1 a "TRACKS" divider, then the tracks.
constexpr std::size_t queue_row_divider = 1;
constexpr std::size_t queue_rows_before_tracks = 2;

}

std::string App::describe_roots() const {
  if (selected_folders_.empty())
    return settings_.music_root + "  (default)";
  if (selected_folders_.size() == 1)
    return selected_folders_.front();
  return std::to_string(selected_folders_.size()) + " folders selected";
}

std::size_t App::row_count() const {
  switch (panel()) {
    case Panel::Search:
      return search_results_.size();
    case Panel::Browse:
      return navigator_.count();
    case Panel::Queue:
      return queue_.size() + queue_rows_before_tracks;
    case Panel::Settings:
      return as_row(SettingsRow::Count);
    case Panel::Folders:
      return folder_row_first + folder_browser_.entries().size();
    case Panel::About:
    case Panel::Controls:
      return text_lines().size();
    case Panel::Home:
    case Panel::Equalizer:
      break;
  }
  return 0;
}

std::size_t App::panel_cursor() {
  return panel() == Panel::Browse ? navigator_.selected() : cursor_of(panel());
}

bool App::row_is_header(std::size_t row) const {
  if (panel() == Panel::Settings)
    return is_settings_header(row);
  return panel() == Panel::Queue && row == queue_row_divider;
}

std::string App::repeat_label() const {
  switch (settings_.repeat) {
    case RepeatMode::Off:
      return "Repeat: off";
    case RepeatMode::One:
      return "Repeat: one track";
    case RepeatMode::All:
      break;
  }
  return "Repeat: all tracks";
}

std::string App::settings_label(std::size_t row) const {
  switch (static_cast<SettingsRow>(row)) {
    case SettingsRow::HeaderLibrary:
      return "LIBRARY";
    case SettingsRow::HeaderSound:
      return "SOUND";
    case SettingsRow::HeaderUi:
      return "UI";
    case SettingsRow::HeaderMisc:
      return "MISC";
    case SettingsRow::Controls:
      return "Button mapping";
    case SettingsRow::About:
      return "About this";
    case SettingsRow::Scan:
      return "Full scan again";
    case SettingsRow::Folders:
      return "Music folders: " + describe_roots();
    case SettingsRow::Equalizer:
      return settings_.eq_enabled ? std::string("Equalizer: ") + eq_preset_name(settings_.eq_gains)
                                  : std::string("Equalizer: off");
    case SettingsRow::Repeat:
      return repeat_label();
    case SettingsRow::Shuffle:
      return settings_.shuffle ? "Shuffle: on" : "Shuffle: off";
    case SettingsRow::Seek:
      return "Seek step: " + std::to_string(settings_.seek_seconds) + " s";
    case SettingsRow::Background:
      return settings_.background_playback ? "Background playback: on" : "Background playback: off";
    case SettingsRow::Animation:
      return settings_.animation_enabled ? "Reel animation: on" : "Reel animation: off";
    case SettingsRow::Battery:
      return std::string("Battery: ") + battery_display_name(settings_.battery_display);
    case SettingsRow::Theme:
      return settings_.theme == Theme::Light ? "Theme: light" : "Theme: dark";
    case SettingsRow::Count:
      break;
  }
  return {};
}

std::string App::folder_label(std::size_t row) const {
  if (row == folder_row_scan) {
    return selected_folders_.empty()
               ? "Scan the default folder"
               : "Scan " + std::to_string(selected_folders_.size()) + " selected folder(s)";
  }
  if (row == folder_row_up)
    return "..  Up one folder";
  const std::size_t index = row - folder_row_first;
  if (index >= folder_browser_.entries().size())
    return {};
  return folder_browser_.entries()[index].name + "/";
}

std::string App::queue_label(std::size_t row) const {
  if (row == 0)
    return {};  // drawn as two buttons from view_.repeat_mode / view_.shuffle
  if (row == queue_row_divider)
    return "TRACKS";
  // Tracks restored from disk have the path as a placeholder title; show the file name.
  const Track& track = queue_.tracks()[row - queue_rows_before_tracks];
  if (track.title != track.path)
    return track.title;
  const std::size_t slash = track.path.find_last_of("/\\");
  return slash == std::string::npos ? track.path : track.path.substr(slash + 1);
}

std::string App::row_label(std::size_t row) const {
  switch (panel()) {
    case Panel::Search: {
      const Track& track = library_.library().tracks[search_results_[row]];
      return track.artist.empty() ? track.title : track.artist + " - " + track.title;
    }
    case Panel::Browse:
      return navigator_.label(row);
    case Panel::Queue:
      return queue_label(row);
    case Panel::Settings:
      return settings_label(row);
    case Panel::Folders:
      return folder_label(row);
    case Panel::About:
    case Panel::Controls:
      return text_lines()[row].text;
    case Panel::Home:
    case Panel::Equalizer:
      break;
  }
  return {};
}

void App::sync_equalizer_view() {
  view_.eq_enabled = settings_.eq_enabled;
  view_.eq_selected = equalizer_.selected();
  for (int band = 0; band < eq_band_count; ++band)
    view_.eq_gains[band] = settings_.eq_gains[band];
  view_.eq_preset = eq_preset_name(settings_.eq_gains);
}

void App::fill_list_heading() {
  const bool library_empty = library_.library().tracks.empty();
  view_.list_empty = "";
  switch (panel()) {
    case Panel::Search:
      if (search_dirty_) {
        search_results_ =
            find_tracks(library_.library(), view_.search_query, settings_.search_case_sensitive);
        search_dirty_ = false;
      }
      view_.list_heading =
          view_.search_query.empty() ? "Search: A to type" : "Search: " + view_.search_query;
      view_.list_empty = library_empty ? "Scan your library first" : "No matching tracks";
      break;
    case Panel::Browse:
      view_.list_heading = navigator_.heading();
      view_.list_empty = library_empty ? "Scan your library first" : "Nothing here";
      break;
    case Panel::Queue:
      view_.list_heading = "Queue";
      view_.list_empty = "Queue is empty";
      break;
    case Panel::Settings:
      view_.list_heading = "Settings";
      break;
    case Panel::Folders:
      view_.list_heading = "Folders: " + folder_browser_.current_path();
      break;
    case Panel::About:
      view_.list_heading = "About this";
      break;
    case Panel::Controls:
      view_.list_heading = "Button mapping";
      break;
    case Panel::Home:
    case Panel::Equalizer:
      break;
  }
}

void App::fill_list_rows(std::size_t first) {
  for (std::size_t row = 0; row < view_.list_count; ++row) {
    const std::size_t index = first + row;
    view_.list_items[row] = row_label(index);
    std::int8_t mark = row_plain;
    if (row_is_header(index))
      mark = row_header;
    if (panel() == Panel::About || panel() == Panel::Controls) {
      const bool header = text_lines()[index].header;
      mark = header ? row_header : (panel() == Panel::About ? row_text : row_mapping);
    }
    if (panel() == Panel::Queue && index == 0)
      mark = row_button_pair;
    if (panel() == Panel::Folders && index >= folder_row_first)
      mark = is_folder_selected(folder_browser_.path_of(index - folder_row_first)) ? 1 : 0;
    view_.list_check[row] = mark;
  }
}

void App::refresh_list() {
  view_.eq_visible = panel() == Panel::Equalizer;
  if (view_.eq_visible)
    sync_equalizer_view();
  view_.list_visible = panel() != Panel::Home && panel() != Panel::Equalizer;
  view_.list_count = 0;
  view_.list_selected = 0;
  view_.list_total = 0;
  view_.list_position = 0;
  if (!view_.list_visible)
    return;
  fill_list_heading();
  const std::size_t total = row_count();
  if (panel() != Panel::Browse && cursor_of(panel()) >= total)
    cursor_of(panel()) = 0;
  if (panel() != Panel::Browse && row_is_header(cursor_of(panel())))
    cursor_of(panel()) += 1;  // the first row of a section follows its header
  view_.list_sub_selected = queue_toggle_;
  const std::size_t cursor = panel_cursor();
  const std::size_t first = list_window_start(total, cursor, view_.list_items.size());
  view_.list_total = total;
  view_.list_position = cursor;
  view_.list_count = std::min(total - first, view_.list_items.size());
  view_.list_selected = cursor - first;
  fill_list_rows(first);
}

void App::open_panel(Panel next) {
  view_.selected_panel = next;
  if (next == Panel::Search)
    search_dirty_ = true;
  if (next == Panel::Browse && navigator_.count() == 0 && !browse_index_.artists.empty())
    navigator_.reset(&library_.library(), &browse_index_);
  if (next == Panel::Queue)
    cursor_of(Panel::Queue) = queue_.current_index() + queue_rows_before_tracks;
  if (next == Panel::Folders) {
    std::string ignored;
    folder_browser_.refresh(ignored);
  }
  switch (next) {
    case Panel::Search:
      view_.status = "A types a search, X plays the result";
      break;
    case Panel::Browse:
      view_.status = "A opens, B goes back";
      break;
    case Panel::Queue:
      view_.status =
          queue_.empty() ? "Queue is empty" : std::to_string(queue_.size()) + " tracks queued";
      break;
    case Panel::Settings:
      view_.status = "A selects, B goes back";
      break;
    case Panel::Folders:
      view_.status = "A opens, X ticks, B goes up";
      break;
    case Panel::Equalizer:
      view_.status = "Drag the bars, or use the d-pad";
      break;
    case Panel::About:
    case Panel::Controls:
      view_.status = "D-pad scrolls, B goes back";
      break;
    case Panel::Home:
      view_.status = library_.status();
      break;
  }
  refresh_list();
}

void App::move_cursor(int delta) {
  const std::size_t total = row_count();
  if (total == 0)
    return;
  if (panel() == Panel::Browse) {
    navigator_.move(delta);
    return;
  }
  std::size_t& cursor = cursor_of(panel());
  const long long size = static_cast<long long>(total);
  cursor =
      static_cast<std::size_t>(((static_cast<long long>(cursor) + delta) % size + size) % size);
  // Section headers cannot be selected: keep going the same way until a real row.
  if (panel() == Panel::Settings || panel() == Panel::Queue) {
    const long long step = delta < 0 ? -1 : 1;
    for (std::size_t guard = 0; guard < total && row_is_header(cursor); ++guard)
      cursor = static_cast<std::size_t>((static_cast<long long>(cursor) + step + size) % size);
  }
}

void App::cycle_repeat() {
  settings_.repeat = next_repeat_mode(settings_.repeat);
  view_.repeat_mode = static_cast<int>(settings_.repeat);
  save_settings();
}

void App::toggle_shuffle() {
  settings_.shuffle = !settings_.shuffle;
  queue_.set_shuffle(settings_.shuffle);
  view_.shuffle = settings_.shuffle;
  save_settings();
}

// Replaces the queue with library tracks and starts playing the chosen one.
void App::play_selection(const std::vector<std::size_t>& indexes, std::size_t start) {
  std::vector<Track> tracks;
  tracks.reserve(indexes.size());
  for (const std::size_t index : indexes)
    tracks.push_back(library_.library().tracks[index]);
  playback_.play_tracks(std::move(tracks), start);
  sync_playback();
  view_.selected_panel = Panel::Home;
}

void App::activate_queue_row() {
  std::size_t& cursor = cursor_of(Panel::Queue);
  if (cursor == 0) {
    if (queue_toggle_ == 0)
      cycle_repeat();
    else
      toggle_shuffle();
  } else if (cursor >= queue_rows_before_tracks &&
             playback_.play_queue_entry(cursor - queue_rows_before_tracks)) {
    sync_playback();
    view_.selected_panel = Panel::Home;
  }
}

void App::activate_setting(std::size_t row) {
  switch (static_cast<SettingsRow>(row)) {
    case SettingsRow::Scan:
      scan_library();
      break;
    case SettingsRow::Folders:
      open_panel(Panel::Folders);
      break;
    case SettingsRow::Equalizer:
      open_panel(Panel::Equalizer);
      break;
    case SettingsRow::Controls:
      open_panel(Panel::Controls);
      break;
    case SettingsRow::About:
      open_panel(Panel::About);
      break;
    case SettingsRow::Repeat:
      cycle_repeat();
      break;
    case SettingsRow::Shuffle:
      toggle_shuffle();
      break;
    case SettingsRow::Seek:
      settings_.seek_seconds = next_seek_seconds(settings_.seek_seconds);
      view_.seek_seconds = settings_.seek_seconds;
      save_settings();
      break;
    case SettingsRow::Background:
      settings_.background_playback = !settings_.background_playback;
      audio_.set_background_playback(settings_.background_playback);
      save_settings();
      break;
    case SettingsRow::Animation:
      settings_.animation_enabled = !settings_.animation_enabled;
      save_settings();
      break;
    case SettingsRow::Battery:
      settings_.battery_display = next_battery_display(settings_.battery_display);
      view_.battery_display = static_cast<int>(settings_.battery_display);
      save_settings();
      break;
    case SettingsRow::Theme:
      settings_.theme = settings_.theme == Theme::Light ? Theme::Dark : Theme::Light;
      view_.light_theme = settings_.theme == Theme::Light;
      save_settings();
      break;
    case SettingsRow::HeaderLibrary:
    case SettingsRow::HeaderSound:
    case SettingsRow::HeaderUi:
    case SettingsRow::HeaderMisc:
    case SettingsRow::Count:
      break;
  }
}

void App::activate_folder_row(std::size_t row) {
  if (row == folder_row_scan) {
    scan_library();
    return;
  }
  std::string error;
  const bool moved = row == folder_row_up ? folder_browser_.leave(error)
                                          : folder_browser_.enter(row - folder_row_first, error);
  if (moved)
    select_first_folder_row();
  else
    view_.status = error;
}

void App::activate_row() {
  switch (panel()) {
    case Panel::Search:
      if (!search_results_.empty())
        play_selection(search_results_, cursor_of(Panel::Search));
      break;
    case Panel::Browse: {
      BrowseSelection selection;
      if (navigator_.enter(selection))
        play_selection(selection.tracks, selection.start);
      break;
    }
    case Panel::Queue:
      activate_queue_row();
      break;
    case Panel::Settings:
      activate_setting(cursor_of(Panel::Settings));
      break;
    case Panel::Folders:
      activate_folder_row(cursor_of(Panel::Folders));
      break;
    case Panel::Home:
    case Panel::Equalizer:
    case Panel::About:
    case Panel::Controls:
      break;
  }
  refresh_list();
}

void App::go_back() {
  if (panel() == Panel::Browse && navigator_.back()) {
    refresh_list();
    return;
  }
  if (panel() == Panel::Equalizer) {
    save_equalizer();
    open_panel(Panel::Settings);
    return;
  }
  if (panel() == Panel::About || panel() == Panel::Controls) {
    open_panel(Panel::Settings);
    return;
  }
  if (panel() == Panel::Folders) {
    if (folder_browser_.at_root()) {
      open_panel(Panel::Settings);
      return;
    }
    std::string error;
    if (folder_browser_.leave(error))
      select_first_folder_row();
    refresh_list();
    return;
  }
  open_panel(Panel::Home);
}

}
