#pragma once

#include <3ds.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "3dsmsc/config/settings.hpp"
#include "3dsmsc/library/artwork.hpp"
#include "3dsmsc/library/browse_index.hpp"
#include "3dsmsc/library/controller.hpp"
#include "3dsmsc/library/filesystem.hpp"
#include "3dsmsc/library/folder_browser.hpp"
#include "3dsmsc/library/metadata.hpp"
#include "3dsmsc/playback/playback_controller.hpp"
#include "3dsmsc/queue/playback_queue.hpp"
#include "3dsmsc/ui/browse_navigator.hpp"
#include "3dsmsc/ui/cassette_view.hpp"
#include "3dsmsc/ui/equalizer_editor.hpp"
#include "3dsmsc/ui/help_text.hpp"
#include "3dsmsc/ui/panel.hpp"
#include "cassette_renderer.hpp"
#include "cover_loader.hpp"
#include "ndsp_player.hpp"

namespace threedsmsc {

// Files the app keeps on the SD card.
inline constexpr const char* config_path = "sdmc:/3dsmsc/config.toml";
inline constexpr const char* folders_path = "sdmc:/3dsmsc/folders.txt";
inline constexpr const char* queue_path = "sdmc:/3dsmsc/queue.txt";
// The last scan result, so Browse and Search work after a restart without scanning again.
inline constexpr const char* library_cache_path = "sdmc:/3dsmsc/library.cache";

// Folder picker rows: scan, parent folder, then the sub-folders with a tick box.
inline constexpr std::size_t folder_row_scan = 0;
inline constexpr std::size_t folder_row_up = 1;
inline constexpr std::size_t folder_row_first = 2;

// The running application: owns the player, the library, the queue, and the screen state, and
// turns buttons and touches into actions. The rules live in the portable modules (the playback
// controller, the equalizer editor, the browse navigator); this class is the glue that reads the
// console's input, calls them, and fills the CassetteView the renderer draws.
//
// It is large for a reason that matters: the main thread's stack is 32 KB, so everything lives in
// members and the object itself is allocated on the heap.
//
// The implementation is split by concern:
//   app.cpp          start-up, the frame loop, and shutdown
//   app_input.cpp    buttons and touches
//   app_panels.cpp   what each list screen shows and what a row does
//   app_library.cpp  the library cache, scanning, and the folder picker
class App {
 public:
  explicit App(CassetteRenderer& renderer);
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // Restores the saved library, then runs until the user exits.
  void run();

 private:
  // --- app.cpp
  void refresh_view();
  // Shows the controller's latest message and the current track after a playback action.
  void sync_playback();
  void save_settings();
  void update_frame_state(const PlaybackSnapshot& playback, std::uint64_t frame_start);
  void poll_battery();
  void update_cover();
  void save_queue_if_due();
  void log_slow_frame(std::uint64_t frame_start, std::uint64_t render_start,
                      std::uint64_t frame_end);
  void change_volume(float delta);
  void shutdown();

  // --- app_input.cpp
  void handle_equalizer_input(std::uint32_t keys_down);
  void handle_equalizer_touch(std::uint32_t keys_down);
  void apply_equalizer(bool changed);
  void save_equalizer();
  void handle_list_input(std::uint32_t keys_down);
  void handle_home_input(std::uint32_t keys_down);
  void handle_list_touch(const touchPosition& touch);
  void handle_home_touch(const touchPosition& touch);
  void edit_search_query();

  // --- app_panels.cpp
  std::size_t& cursor_of(Panel panel) { return cursors_[static_cast<std::size_t>(panel)]; }
  Panel panel() const { return view_.selected_panel; }
  std::string describe_roots() const;
  const std::vector<HelpLine>& text_lines() const {
    return panel() == Panel::About ? about_lines_ : controls_lines_;
  }
  std::size_t row_count() const;
  std::size_t panel_cursor();
  bool row_is_header(std::size_t row) const;
  std::string row_label(std::size_t row) const;
  std::string settings_label(std::size_t row) const;
  std::string repeat_label() const;
  std::string folder_label(std::size_t row) const;
  std::string queue_label(std::size_t row) const;
  void refresh_list();
  void fill_list_heading();
  void fill_list_rows(std::size_t first);
  void open_panel(Panel next);
  void move_cursor(int delta);
  void activate_row();
  void activate_setting(std::size_t row);
  void activate_folder_row(std::size_t row);
  void activate_queue_row();
  void go_back();
  void sync_equalizer_view();
  void cycle_repeat();
  void toggle_shuffle();
  void play_selection(const std::vector<std::size_t>& indexes, std::size_t start);

  // --- app_library.cpp
  void restore_library_cache();
  void scan_library();
  void on_scan_track();
  void adopt_library();
  bool is_folder_selected(const std::string& path) const;
  void toggle_folder(std::size_t row);
  void select_first_folder_row();

  CassetteRenderer& renderer_;
  NdspAudioPlayer audio_;
  bool audio_available_ = false;
  Settings settings_;
  CoverLoader cover_loader_;
  std::string cover_path_;  // the file whose cover is shown or being decoded
  CoverPixels cover_buffer_ = {};
  std::string config_error_;
  float volume_ = 0.0F;
  LocalFileSystem filesystem_;
  // A queue restored from disk holds only paths, so the cover is looked up when a track plays.
  ArtworkLocator artwork_locator_;
  std::string cover_lookup_path_;
  LibraryController library_;
  FolderBrowser folder_browser_;
  std::vector<std::string> selected_folders_;
  PlaybackQueue queue_;
  EmbeddedMetadataReader embedded_reader_;
  FilenameMetadataReader filename_reader_;
  CompositeMetadataReader metadata_reader_;
  PlaybackController playback_;
  EqualizerEditor equalizer_;
  BrowseIndex browse_index_;
  BrowseNavigator navigator_;
  CassetteView view_;
  std::vector<std::size_t> search_results_;
  bool search_dirty_ = true;  // results are recomputed only when the query or library changes
  std::array<std::size_t, panel_count> cursors_ = {};
  int queue_toggle_ = 0;  // which button of the queue's pair the d-pad is on: 0 repeat, 1 shuffle
  const std::vector<HelpLine>& controls_lines_;
  std::vector<HelpLine> about_lines_;
  // Saving rewrites the whole queue file, so it is batched instead of done on every track change.
  bool queue_dirty_ = false;
  std::uint64_t last_queue_save_ = 0;
  std::uint64_t last_headphone_poll_ = 0;
  int slow_frame_logs_ = 0;
  std::size_t scan_tracks_ = 0;
  std::uint64_t scan_last_draw_ = 0;
};

}
