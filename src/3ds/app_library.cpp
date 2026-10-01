#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

#include "3dsmsc/library/browse_index.hpp"
#include "3dsmsc/library/folder_list_file.hpp"
#include "3dsmsc/library/library_cache.hpp"
#include "app.hpp"
#include "boot_log.hpp"

namespace threedsmsc {
namespace {

// Redraw the scan screen by elapsed time: reading a track's tags can take long on a slow SD
// card, so a fixed track count made the screen look frozen.
constexpr std::uint64_t scan_redraw_ms = 150;

}

void App::adopt_library() {
  browse_index_ = build_browse_index(library_.library());
  navigator_.reset(&library_.library(), &browse_index_);
  search_dirty_ = true;
}

// Restores the library saved by the last scan. Scanning stays a manual action; this only
// remembers its result. A missing or damaged cache just leaves the library empty.
void App::restore_library_cache() {
  view_.status = "Loading saved library...";
  renderer_.render(view_);
  const std::uint64_t cache_start = osGetTime();
  LibraryIndex cached;
  if (!load_library_cache(library_cache_path, cached) || !library_.adopt_cached(std::move(cached)))
    return;
  adopt_library();
  char line[80];
  std::snprintf(line, sizeof(line), "library cache: %lu tracks in %lu ms",
                static_cast<unsigned long>(library_.library().tracks.size()),
                static_cast<unsigned long>(osGetTime() - cache_start));
  boot_log(line);
}

void App::on_scan_track() {
  ++scan_tracks_;
  const std::uint64_t now = osGetTime();
  if (now - scan_last_draw_ < scan_redraw_ms)
    return;
  scan_last_draw_ = now;
  view_.status = "Scanning... " + std::to_string(scan_tracks_) + " tracks (B cancels)";
  renderer_.render(view_);
}

void App::scan_library() {
  scan_tracks_ = 0;
  scan_last_draw_ = osGetTime();
  library_.set_selected_roots(selected_folders_);
  view_.list_visible = false;
  view_.selected_panel = Panel::Home;
  view_.status = "Scanning... B cancels";
  renderer_.render(view_);
  ScanCallbacks callbacks{};
  callbacks.context = this;
  callbacks.on_track = [](void* context, const Track&) {
    static_cast<App*>(context)->on_scan_track();
  };
  callbacks.is_cancelled = [](void*) {
    if (!aptMainLoop())
      return true;
    hidScanInput();
    return (hidKeysHeld() & KEY_B) != 0;
  };
  if (library_.scan(callbacks)) {
    adopt_library();
    search_results_.clear();
    queue_.set_tracks(library_.library().tracks);
    playback_.invalidate();
    playback_.load_current(false);
    sync_playback();
    queue_dirty_ = true;
    // Remember the result for the next launch (a large library takes a moment to write).
    view_.status = "Saving library...";
    renderer_.render(view_);
    if (!save_library_cache(library_cache_path, library_.library()))
      boot_log("could not save the library cache");
  }
  view_.library_path = describe_roots();
  view_.status = library_.status();
  refresh_list();
}

bool App::is_folder_selected(const std::string& path) const {
  return std::find(selected_folders_.begin(), selected_folders_.end(), path) !=
         selected_folders_.end();
}

void App::toggle_folder(std::size_t row) {
  if (row < folder_row_first || row - folder_row_first >= folder_browser_.entries().size())
    return;
  const std::string path = folder_browser_.path_of(row - folder_row_first);
  const auto found = std::find(selected_folders_.begin(), selected_folders_.end(), path);
  if (found == selected_folders_.end())
    selected_folders_.push_back(path);
  else
    selected_folders_.erase(found);
  if (!save_folder_list(folders_path, selected_folders_))
    view_.status = "Could not save the folder list";
  library_.set_selected_roots(selected_folders_);
  view_.library_path = describe_roots();
}

// After entering or leaving a folder, the cursor goes to its first sub-folder.
void App::select_first_folder_row() {
  cursor_of(Panel::Folders) = folder_browser_.entries().empty() ? 0 : folder_row_first;
}

}
