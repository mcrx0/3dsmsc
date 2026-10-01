#include <3ds.h>
#include <3ds/applets/swkbd.h>

#include <sys/stat.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "3dsmsc/config/settings.hpp"
#include "3dsmsc/config/settings_file.hpp"
#include "3dsmsc/library/browse_index.hpp"
#include "3dsmsc/library/controller.hpp"
#include "3dsmsc/library/filesystem.hpp"
#include "3dsmsc/library/folder_browser.hpp"
#include "3dsmsc/library/folder_list_file.hpp"
#include "3dsmsc/library/library_index.hpp"
#include "3dsmsc/library/metadata.hpp"
#include "3dsmsc/queue/playback_queue.hpp"
#include "3dsmsc/queue/queue_file.hpp"
#include "3dsmsc/ui/browse_navigator.hpp"
#include "cassette_renderer.hpp"
#include "ndsp_player.hpp"

namespace {

enum Panel { kPanelHome, kPanelSearch, kPanelBrowse, kPanelQueue, kPanelSettings, kPanelFolders };

// Appends a line to sdmc:/3dsmsc/boot.log so a hang during start-up can be located.
void boot_log(const char* message) {
  std::FILE* file = std::fopen("sdmc:/3dsmsc/boot.log", "a");
  if (file == nullptr)
    return;
  std::fprintf(file, "%s\n", message);
  std::fclose(file);
}

// Shows a message on the text console. Call only after the renderers are shut down,
// because citro3d and the console cannot share a screen.
void show_startup_error(const char* message) {
  boot_log(message);
  consoleInit(GFX_TOP, nullptr);
  std::printf("%s\nPress START to exit.\n", message);
  while (aptMainLoop()) {
    hidScanInput();
    if (hidKeysDown() & KEY_START)
      break;
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
  }
}

}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  gfxInitDefault();
  // First run: make sure the data folder and default music folder exist.
  mkdir("sdmc:/3dsmsc", 0777);
  mkdir("sdmc:/3dsmsc/music", 0777);
  std::remove("sdmc:/3dsmsc/boot.log");
  boot_log("3dsmsc 0.4.2b: gfx ready");
  if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
    show_startup_error("3D renderer initialization failed.");
    gfxExit();
    return 1;
  }
  boot_log("citro3d ready");
  if (!C2D_Init(8192)) {
    C3D_Fini();
    show_startup_error("2D renderer initialization failed.");
    gfxExit();
    return 1;
  }
  boot_log("citro2d ready");
  C2D_Prepare();
  // romfs holds the optional icon sheet; the UI still works without it.
  const bool romfs_mounted = R_SUCCEEDED(romfsInit());
  boot_log(romfs_mounted ? "romfs mounted" : "romfs unavailable (text labels)");
  threedsmsc::CassetteRenderer renderer;
  if (!renderer.init()) {
    C2D_Fini();
    C3D_Fini();
    show_startup_error("UI renderer initialization failed.");
    gfxExit();
    return 1;
  }
  boot_log("renderer ready");
  threedsmsc::CassetteView boot_view{};
  boot_view.artist = "Offline library player";
  boot_view.album = "New Nintendo 3DS";
  boot_view.status = "Starting audio...";
  renderer.render(boot_view);
  boot_log("first frame drawn");
  threedsmsc::NdspAudioPlayer audio;
  const bool audio_available = audio.init();
  if (audio_available) {
    boot_log("audio ready");
  } else {
    char line[96];
    std::snprintf(line, sizeof(line), "audio unavailable: ndspInit result 0x%08lX (%s)",
                  static_cast<unsigned long>(audio.init_result()), audio.error().c_str());
    boot_log(line);
  }
  threedsmsc::Settings settings = threedsmsc::default_settings();
  std::string config_error;
  threedsmsc::load_settings_file("sdmc:/3dsmsc/config.toml", settings, &config_error);
  float volume = settings.volume;
  audio.set_volume(volume);
  audio.set_background_playback(settings.background_playback);
  threedsmsc::LocalFileSystem filesystem;
  threedsmsc::LibraryController library(filesystem, settings);
  // The folder picker can browse the whole SD card, so its root is the card itself.
  threedsmsc::FolderBrowser folder_browser(filesystem, "sdmc:/");
  std::string browser_error;
  folder_browser.refresh(browser_error);
  const std::string folders_path = "sdmc:/3dsmsc/folders.txt";
  std::vector<std::string> selected_folders = threedsmsc::load_folder_list(folders_path);
  library.set_selected_roots(selected_folders);
  threedsmsc::PlaybackQueue queue;
  const std::string queue_path = "sdmc:/3dsmsc/queue.txt";
  std::string queue_error;
  load_queue_file(queue_path, queue, &queue_error);
  // Saving rewrites the whole queue file, so it is batched instead of done on every track change.
  bool queue_dirty = false;
  u64 last_queue_save = osGetTime();
  threedsmsc::CassetteView view;
  threedsmsc::BrowseIndex browse_index;
  threedsmsc::BrowseNavigator navigator;
  std::vector<std::size_t> search_results;
  bool search_dirty = true;  // results are recomputed only when the query or library changes
  std::size_t cursors[6] = {};  // per-panel cursor for search, queue, settings and folders
  std::string loaded_path;
  view.artist = "Offline library player";
  view.album = "New Nintendo 3DS";
  view.playing = false;
  view.progress = 0.0f;
  view.reel_phase = 0.0f;
  view.has_track = false;
  view.selected_panel = kPanelHome;
  view.list_count = 0;
  view.list_selected = 0;
  view.list_total = 0;
  view.list_position = 0;
  view.list_visible = false;
  view.search_query.clear();
  view.light_theme = settings.theme == threedsmsc::Theme::Light;
  view.seek_seconds = settings.seek_seconds;
  // Describes what a scan will cover: the default folder or the ticked folders.
  const auto describe_roots = [&]() -> std::string {
    if (selected_folders.empty())
      return settings.music_root + "  (default)";
    if (selected_folders.size() == 1)
      return selected_folders.front();
    return std::to_string(selected_folders.size()) + " folders selected";
  };
  view.library_path = describe_roots();
  view.status = audio_available ? library.status() : audio.error();

  const auto refresh_view = [&] {
    const threedsmsc::Track* track = queue.current();
    view.has_track = track != nullptr;
    if (track != nullptr) {
      view.title = track->title;
      view.artist = track->artist.empty() ? "Unknown artist" : track->artist;
      view.album = track->album.empty() ? "Unknown album" : track->album;
      view.artwork_path = track->artwork_path;
    } else {
      view.title.clear();
      view.artist = "Offline library player";
      view.album = "New Nintendo 3DS";
      view.artwork_path.clear();
    }
  };
  // A queue restored from disk holds only paths, so its tracks have no title or length until a
  // rescan. Read them when a track is about to play.
  threedsmsc::EmbeddedMetadataReader embedded_reader;
  threedsmsc::FilenameMetadataReader filename_reader;
  threedsmsc::CompositeMetadataReader metadata_reader(embedded_reader, filename_reader);
  const auto fill_missing_metadata = [&] {
    threedsmsc::Track* track = queue.mutable_current();
    if (track == nullptr || (track->duration_ms != 0 && track->title != track->path))
      return;
    threedsmsc::TrackMetadata metadata;
    if (!metadata_reader.read(track->path, metadata))
      return;
    if (track->title.empty() || track->title == track->path)
      track->title = metadata.title;
    if (track->artist.empty())
      track->artist = metadata.artist;
    if (track->album.empty())
      track->album = metadata.album;
    if (track->duration_ms == 0)
      track->duration_ms = metadata.duration_ms;
    if (track->track_number == 0)
      track->track_number = metadata.track_number;
  };
  // Loads the queue's current track, skipping tracks that cannot be opened (for example
  // files deleted since the queue was saved). Returns false when nothing in the queue plays.
  const auto load_current = [&](bool resume) {
    std::size_t skipped = 0;
    for (std::size_t attempt = 0; attempt < queue.size(); ++attempt) {
      const threedsmsc::Track* track = queue.current();
      if (track == nullptr)
        break;
      if (loaded_path == track->path || audio.load(*track)) {
        loaded_path = track->path;
        fill_missing_metadata();
        if (resume)
          audio.play();
        refresh_view();
        if (skipped != 0)
          view.status = "Skipped " + std::to_string(skipped) + " unplayable track(s)";
        return true;
      }
      loaded_path.clear();
      if (skipped < 5)  // keep the log short when a whole queue fails
        boot_log((std::string("cannot play ") + track->path + ": " + audio.error()).c_str());
      // With no audio engine every track fails the same way; do not walk the whole queue.
      if (!audio_available)
        break;
      ++skipped;
      queue.next();
    }
    audio.stop();
    loaded_path.clear();
    refresh_view();
    if (!queue.empty()) {
      // Say why: a missing DSP firmware looks the same as an unreadable file otherwise.
      view.status = audio.error().empty() ? "No playable tracks in queue" : audio.error();
    }
    return false;
  };
  const auto change_queue_track = [&](bool forward) {
    const bool resume = audio.snapshot().state == threedsmsc::PlaybackState::Playing;
    if (queue.empty())
      return;
    if (forward)
      queue.next();
    else
      queue.previous();
    load_current(resume);
    queue_dirty = true;
  };
  // Replaces the queue with library tracks and starts playing the chosen one.
  const auto play_selection = [&](const std::vector<std::size_t>& indexes, std::size_t start) {
    std::vector<threedsmsc::Track> tracks;
    tracks.reserve(indexes.size());
    for (const std::size_t index : indexes)
      tracks.push_back(library.library().tracks[index]);
    queue.set_tracks(std::move(tracks));
    queue.select(start);
    loaded_path.clear();
    if (load_current(true))
      view.status = "Playing";
    queue_dirty = true;
    view.selected_panel = kPanelHome;
  };

  // Settings rows.
  enum : std::size_t {
    kRowScan,
    kRowFolders,
    kRowSeek,
    kRowBackground,
    kRowAnimation,
    kRowTheme,
    kRowCount
  };
  const auto settings_label = [&](std::size_t row) -> std::string {
    switch (row) {
      case kRowScan:
        return "Full scan again";
      case kRowFolders:
        return "Music folders: " + describe_roots();
      case kRowSeek:
        return "Seek step: " + std::to_string(settings.seek_seconds) + " s";
      case kRowBackground:
        return settings.background_playback ? "Background playback: on"
                                            : "Background playback: off";
      case kRowAnimation:
        return settings.animation_enabled ? "Reel animation: on" : "Reel animation: off";
      case kRowTheme:
        return settings.theme == threedsmsc::Theme::Light ? "Theme: light" : "Theme: dark";
    }
    return {};
  };
  // Folder picker rows: scan, parent folder, then sub-folders with a tick box.
  enum : std::size_t { kFolderScan, kFolderUp, kFolderFirst };
  const auto is_folder_selected = [&](const std::string& path) {
    return std::find(selected_folders.begin(), selected_folders.end(), path) !=
           selected_folders.end();
  };
  const auto folder_label = [&](std::size_t row) -> std::string {
    if (row == kFolderScan)
      return selected_folders.empty()
                 ? "Scan the default folder"
                 : "Scan " + std::to_string(selected_folders.size()) + " selected folder(s)";
    if (row == kFolderUp)
      return "..  Up one folder";
    const std::size_t index = row - kFolderFirst;
    if (index >= folder_browser.entries().size())
      return {};
    return folder_browser.entries()[index].name + "/";
  };
  const auto toggle_folder = [&](std::size_t row) {
    if (row < kFolderFirst || row - kFolderFirst >= folder_browser.entries().size())
      return;
    const std::string path = folder_browser.path_of(row - kFolderFirst);
    const auto found = std::find(selected_folders.begin(), selected_folders.end(), path);
    if (found == selected_folders.end())
      selected_folders.push_back(path);
    else
      selected_folders.erase(found);
    if (!threedsmsc::save_folder_list(folders_path, selected_folders))
      view.status = "Could not save the folder list";
    library.set_selected_roots(selected_folders);
    view.library_path = describe_roots();
  };
  const auto panel_count = [&]() -> std::size_t {
    switch (view.selected_panel) {
      case kPanelSearch:
        return search_results.size();
      case kPanelBrowse:
        return navigator.count();
      case kPanelQueue:
        return queue.size();
      case kPanelSettings:
        return kRowCount;
      case kPanelFolders:
        return kFolderFirst + folder_browser.entries().size();
    }
    return 0;
  };
  const auto panel_cursor = [&]() -> std::size_t {
    return view.selected_panel == kPanelBrowse ? navigator.selected()
                                               : cursors[view.selected_panel];
  };
  const auto panel_label = [&](std::size_t row) -> std::string {
    switch (view.selected_panel) {
      case kPanelSearch: {
        const threedsmsc::Track& track = library.library().tracks[search_results[row]];
        return track.artist.empty() ? track.title : track.artist + " - " + track.title;
      }
      case kPanelBrowse:
        return navigator.label(row);
      case kPanelQueue: {
        // Tracks restored from disk have the path as a placeholder title; show the file name.
        const threedsmsc::Track& track = queue.tracks()[row];
        if (track.title != track.path)
          return track.title;
        const std::size_t slash = track.path.find_last_of("/\\");
        return slash == std::string::npos ? track.path : track.path.substr(slash + 1);
      }
      case kPanelSettings:
        return settings_label(row);
      case kPanelFolders:
        return folder_label(row);
    }
    return {};
  };
  const auto refresh_list = [&] {
    view.list_visible = view.selected_panel != kPanelHome;
    view.list_count = 0;
    view.list_selected = 0;
    view.list_total = 0;
    view.list_position = 0;
    if (!view.list_visible)
      return;
    switch (view.selected_panel) {
      case kPanelSearch:
        if (search_dirty) {
          search_results =
              find_tracks(library.library(), view.search_query, settings.search_case_sensitive);
          search_dirty = false;
        }
        view.list_heading =
            view.search_query.empty() ? "Search: A to type" : "Search: " + view.search_query;
        view.list_empty = library.library().tracks.empty() ? "Scan your library first"
                                                           : "No matching tracks";
        break;
      case kPanelBrowse:
        view.list_heading = navigator.heading();
        view.list_empty = library.library().tracks.empty() ? "Scan your library first"
                                                           : "Nothing here";
        break;
      case kPanelQueue:
        view.list_heading = "Queue";
        view.list_empty = "Queue is empty";
        break;
      case kPanelSettings:
        view.list_heading = "Settings > Library";
        view.list_empty = "";
        break;
      case kPanelFolders:
        view.list_heading = "Folders: " + folder_browser.current_path();
        view.list_empty = "";
        break;
    }
    const std::size_t total = panel_count();
    if (view.selected_panel != kPanelBrowse && cursors[view.selected_panel] >= total)
      cursors[view.selected_panel] = 0;
    const std::size_t cursor = panel_cursor();
    const std::size_t first = threedsmsc::list_window_start(total, cursor, view.list_items.size());
    view.list_total = total;
    view.list_position = cursor;
    view.list_count = total - first < view.list_items.size() ? total - first : view.list_items.size();
    view.list_selected = cursor - first;
    for (std::size_t row = 0; row < view.list_count; ++row) {
      view.list_items[row] = panel_label(first + row);
      view.list_check[row] = -1;
      if (view.selected_panel == kPanelFolders && first + row >= kFolderFirst) {
        view.list_check[row] =
            is_folder_selected(folder_browser.path_of(first + row - kFolderFirst)) ? 1 : 0;
      }
    }
  };
  const auto open_panel = [&](int panel) {
    view.selected_panel = panel;
    if (panel == kPanelSearch)
      search_dirty = true;
    if (panel == kPanelBrowse && navigator.count() == 0 && !browse_index.artists.empty())
      navigator.reset(&library.library(), &browse_index);
    if (panel == kPanelQueue)
      cursors[kPanelQueue] = queue.current_index();
    if (panel == kPanelFolders) {
      std::string ignored;
      folder_browser.refresh(ignored);
    }
    switch (panel) {
      case kPanelSearch:
        view.status = "A types a search, X plays the result";
        break;
      case kPanelBrowse:
        view.status = "A opens, B goes back";
        break;
      case kPanelQueue:
        view.status = queue.empty() ? "Queue is empty"
                                    : std::to_string(queue.size()) + " tracks queued";
        break;
      case kPanelSettings:
        view.status = "A selects, B goes back";
        break;
      case kPanelFolders:
        view.status = "A opens, X ticks, B goes up";
        break;
      default:
        view.status = library.status();
        break;
    }
    refresh_list();
  };
  const auto move_cursor = [&](int delta) {
    const std::size_t total = panel_count();
    if (total == 0)
      return;
    if (view.selected_panel == kPanelBrowse) {
      navigator.move(delta);
      return;
    }
    std::size_t& cursor = cursors[view.selected_panel];
    const long long size = static_cast<long long>(total);
    cursor = static_cast<std::size_t>(((static_cast<long long>(cursor) + delta) % size + size) % size);
  };
  const auto edit_search_query = [&] {
    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_QWERTY, 2, 63);
    swkbdSetFeatures(&keyboard, SWKBD_DEFAULT_QWERTY);
    swkbdSetHintText(&keyboard, "Search music");
    swkbdSetInitialText(&keyboard, view.search_query.c_str());
    char query[64] = {};
    if (swkbdInputText(&keyboard, query, sizeof(query)) == SWKBD_BUTTON_CONFIRM) {
      view.search_query = query;
      search_dirty = true;
      cursors[kPanelSearch] = 0;
    }
    refresh_list();
  };

  struct ScanProgress {
    threedsmsc::CassetteRenderer* renderer;
    threedsmsc::CassetteView* view;
    std::size_t tracks;
    u64 last_draw;
  } scan_progress{&renderer, &view, 0, 0};
  const auto scan_library = [&] {
    scan_progress.tracks = 0;
    scan_progress.last_draw = osGetTime();
    library.set_selected_roots(selected_folders);
    view.list_visible = false;
    view.selected_panel = kPanelHome;
    view.status = "Scanning... B cancels";
    renderer.render(view);
    threedsmsc::ScanCallbacks callbacks{};
    callbacks.context = &scan_progress;
    callbacks.on_track = [](void* context, const threedsmsc::Track&) {
      auto* progress = static_cast<ScanProgress*>(context);
      ++progress->tracks;
      // Redraw by elapsed time: reading a track's tags can take long on a slow SD card, so a
      // fixed track count made the screen look frozen.
      const u64 now = osGetTime();
      if (now - progress->last_draw < 150)
        return;
      progress->last_draw = now;
      progress->view->status =
          "Scanning... " + std::to_string(progress->tracks) + " tracks (B cancels)";
      progress->renderer->render(*progress->view);
    };
    callbacks.is_cancelled = [](void*) {
      if (!aptMainLoop())
        return true;
      hidScanInput();
      return (hidKeysHeld() & KEY_B) != 0;
    };
    const bool scanned = library.scan(callbacks);
    if (scanned) {
      browse_index = threedsmsc::build_browse_index(library.library());
      navigator.reset(&library.library(), &browse_index);
      search_results.clear();
      search_dirty = true;
      queue.set_tracks(library.library().tracks);
      loaded_path.clear();
      load_current(false);
      queue_dirty = true;
    }
    view.library_path = describe_roots();
    view.status = library.status();
    refresh_list();
  };
  const auto activate_row = [&] {
    switch (view.selected_panel) {
      case kPanelSearch:
        if (!search_results.empty())
          play_selection(search_results, cursors[kPanelSearch]);
        break;
      case kPanelBrowse: {
        threedsmsc::BrowseSelection selection;
        if (navigator.enter(selection))
          play_selection(selection.tracks, selection.start);
        break;
      }
      case kPanelQueue:
        if (queue.select(cursors[kPanelQueue])) {
          loaded_path.clear();
          load_current(true);
          queue_dirty = true;
          view.selected_panel = kPanelHome;
        }
        break;
      case kPanelSettings: {
        const std::size_t row = cursors[kPanelSettings];
        if (row == kRowScan) {
          scan_library();
        } else if (row == kRowFolders) {
          open_panel(kPanelFolders);
        } else if (row == kRowSeek) {
          settings.seek_seconds = threedsmsc::next_seek_seconds(settings.seek_seconds);
          view.seek_seconds = settings.seek_seconds;
          save_settings_file("sdmc:/3dsmsc/config.toml", settings, &config_error);
        } else if (row == kRowBackground) {
          settings.background_playback = !settings.background_playback;
          audio.set_background_playback(settings.background_playback);
          save_settings_file("sdmc:/3dsmsc/config.toml", settings, &config_error);
        } else if (row == kRowAnimation) {
          settings.animation_enabled = !settings.animation_enabled;
          save_settings_file("sdmc:/3dsmsc/config.toml", settings, &config_error);
        } else if (row == kRowTheme) {
          settings.theme = settings.theme == threedsmsc::Theme::Light ? threedsmsc::Theme::Dark
                                                                      : threedsmsc::Theme::Light;
          view.light_theme = settings.theme == threedsmsc::Theme::Light;
          save_settings_file("sdmc:/3dsmsc/config.toml", settings, &config_error);
        }
        break;
      }
      case kPanelFolders: {
        const std::size_t row = cursors[kPanelFolders];
        std::string error;
        if (row == kFolderScan) {
          scan_library();
        } else if (row == kFolderUp) {
          if (folder_browser.leave(error))
            cursors[kPanelFolders] = folder_browser.entries().empty() ? std::size_t{0} : std::size_t{kFolderFirst};
          else
            view.status = error;
        } else if (folder_browser.enter(row - kFolderFirst, error)) {
          cursors[kPanelFolders] = folder_browser.entries().empty() ? std::size_t{0} : std::size_t{kFolderFirst};
        } else {
          view.status = error;
        }
        break;
      }
    }
    refresh_list();
  };
  const auto go_back = [&] {
    if (view.selected_panel == kPanelBrowse && navigator.back()) {
      refresh_list();
      return;
    }
    if (view.selected_panel == kPanelFolders) {
      if (!folder_browser.at_root()) {
        std::string error;
        if (folder_browser.leave(error))
          cursors[kPanelFolders] = folder_browser.entries().empty() ? std::size_t{0} : std::size_t{kFolderFirst};
        refresh_list();
      } else {
        open_panel(kPanelSettings);
      }
      return;
    }
    open_panel(kPanelHome);
  };
  const auto toggle_playback = [&] {
    if (queue.current() == nullptr)
      return;
    if (loaded_path.empty() && !load_current(false))
      return;
    if (audio.snapshot().state == threedsmsc::PlaybackState::Playing) {
      audio.pause();
      view.status = "Paused";
    } else {
      audio.play();
      view.status = "Playing";
    }
  };
  // Seeks by the configured step; direction is -1 (back) or +1 (forward). The audio thread
  // does the work, so this only records the request and returns at once.
  const auto seek_by = [&](int direction) {
    if (queue.current() == nullptr || loaded_path.empty()) {
      view.status = "Nothing to seek";
      return;
    }
    const std::int64_t step_ms = static_cast<std::int64_t>(settings.seek_seconds) * 1000;
    if (audio.seek(direction < 0 ? -step_ms : step_ms)) {
      view.status = std::string(direction < 0 ? "Back " : "Forward ") +
                    std::to_string(settings.seek_seconds) + " s";
    } else {
      view.status = "This file cannot be seeked";
    }
  };
  const auto change_volume = [&](float delta) {
    volume = volume + delta < 0.0f ? 0.0f : (volume + delta > 1.0f ? 1.0f : volume + delta);
    audio.set_volume(volume);
    settings.volume = volume;
    save_settings_file("sdmc:/3dsmsc/config.toml", settings, &config_error);
    view.status = "Volume: " + std::to_string(static_cast<int>(volume * 100.0f + 0.5f)) + "%";
  };

  int consecutive_failures = 0;
  load_current(false);
  view.status = audio_available ? library.status() : audio.error();
  int slow_frame_logs = 0;
  while (aptMainLoop()) {
    const u64 frame_start = osGetTime();
    hidScanInput();
    const u32 keys_down = hidKeysDown();
    if (keys_down & KEY_START)
      break;
    if (keys_down & KEY_L)
      change_volume(-0.05f);
    if (keys_down & KEY_R)
      change_volume(0.05f);
    const bool in_list = view.selected_panel != kPanelHome;
    if (in_list) {
      if (keys_down & KEY_DUP)
        move_cursor(-1);
      if (keys_down & KEY_DDOWN)
        move_cursor(1);
      if (keys_down & KEY_DLEFT)
        move_cursor(-static_cast<int>(view.list_items.size()));
      if (keys_down & KEY_DRIGHT)
        move_cursor(static_cast<int>(view.list_items.size()));
      if (keys_down & KEY_B)
        go_back();
      else if (keys_down & KEY_A) {
        if (view.selected_panel == kPanelSearch && view.search_query.empty())
          edit_search_query();
        else
          activate_row();
      }
      if (keys_down & KEY_X) {
        if (view.selected_panel == kPanelSearch)
          edit_search_query();
        else if (view.selected_panel == kPanelFolders)
          toggle_folder(cursors[kPanelFolders]);
      }
      if (keys_down != 0)  // rebuilding the visible rows every frame is wasted work
        refresh_list();
    } else {
      if (keys_down & KEY_LEFT)
        seek_by(-1);
      if (keys_down & KEY_RIGHT)
        seek_by(1);
      if (keys_down & KEY_A)
        toggle_playback();
      if (keys_down & KEY_X)
        change_queue_track(true);
      if (keys_down & KEY_Y)
        change_queue_track(false);
    }
    if (keys_down & KEY_TOUCH) {
      touchPosition touch;
      hidTouchRead(&touch);
      if (in_list) {
        if (touch.py < 66) {
          go_back();
        } else if (touch.py >= 68 && touch.py < 68 + 28 * static_cast<int>(view.list_items.size())) {
          const std::size_t row = static_cast<std::size_t>(touch.py - 68) / 28;
          if (row < view.list_count) {
            const std::size_t first = panel_cursor() - view.list_selected;
            if (view.selected_panel == kPanelFolders && touch.px < 56 &&
                first + row >= kFolderFirst) {
              cursors[kPanelFolders] = first + row;  // the tick box toggles without opening
              toggle_folder(first + row);
            } else if (first + row == panel_cursor()) {
              activate_row();
            } else if (view.selected_panel == kPanelBrowse) {
              navigator.select(first + row);
            } else {
              cursors[view.selected_panel] = first + row;
            }
          }
        }
        refresh_list();
      } else if (touch.py >= 36 && touch.py < 72) {
        open_panel(kPanelFolders);  // the status card shows the scan folders
      } else if (touch.py >= 72 && touch.py < 134) {
        // Five buttons: seek back, previous, play/pause, next, seek forward.
        if (touch.px < 70)
          seek_by(-1);
        else if (touch.px < 130)
          change_queue_track(false);
        else if (touch.px < 190)
          toggle_playback();
        else if (touch.px < 250)
          change_queue_track(true);
        else
          seek_by(1);
      } else if (touch.py >= 142 && touch.py < 186) {
        open_panel(touch.px < 160 ? kPanelSearch : kPanelBrowse);
      } else if (touch.py >= 190 && touch.py < 234) {
        open_panel(touch.px < 160 ? kPanelQueue : kPanelSettings);
      }
    }
    const bool was_playing = view.playing;
    audio.update();
    const threedsmsc::PlaybackSnapshot playback = audio.snapshot();
    if (playback.state == threedsmsc::PlaybackState::Playing && playback.position_ms > 2000)
      consecutive_failures = 0;
    const bool finished = playback.state == threedsmsc::PlaybackState::Stopped;
    const bool failed = playback.state == threedsmsc::PlaybackState::Error;
    if (was_playing && (finished || failed)) {
      consecutive_failures = failed ? consecutive_failures + 1 : 0;
      // Stop after a few failures in a row rather than skipping through the whole queue.
      if (consecutive_failures >= 3) {
        audio.stop();
        loaded_path.clear();
        view.status = "Playback failed: unsupported or damaged file";
        consecutive_failures = 0;
      } else {
        queue.next();
        load_current(true);
        queue_dirty = true;
      }
    }
    view.playing = audio.snapshot().state == threedsmsc::PlaybackState::Playing;
    const threedsmsc::Track* current = queue.current();
    view.position_ms = playback.position_ms;
    view.duration_ms = current != nullptr ? current->duration_ms : 0;
    view.volume = volume;
    view.library_tracks = library.library().tracks.size();
    view.queue_tracks = queue.size();
    view.progress = current != nullptr && current->duration_ms != 0
                        ? static_cast<float>(playback.position_ms) /
                              static_cast<float>(current->duration_ms)
                        : 0.0f;
    if (view.playing && settings.animation_enabled) {
      view.reel_phase += 0.08f * static_cast<float>(settings.animation_speed);
      if (view.reel_phase > 6.2831855f)
        view.reel_phase -= 6.2831855f;
    }
    if (queue_dirty && osGetTime() - last_queue_save > 5000) {
      save_queue_file(queue_path, queue);
      queue_dirty = false;
      last_queue_save = osGetTime();
    }
    const u64 render_start = osGetTime();
    renderer.render(view);  // C3D_FrameEnd presents and syncs to vblank
    // A normal frame is ~16 ms. Record the slow ones so a stall on real hardware can be traced
    // to input/logic work or to drawing.
    const u64 frame_end = osGetTime();
    if (frame_end - frame_start > 100 && slow_frame_logs < 40) {
      ++slow_frame_logs;
      char line[112];
      std::snprintf(line, sizeof(line), "slow frame %lums: logic=%lu draw=%lu panel=%d playing=%d",
                    static_cast<unsigned long>(frame_end - frame_start),
                    static_cast<unsigned long>(render_start - frame_start),
                    static_cast<unsigned long>(frame_end - render_start), view.selected_panel,
                    view.playing ? 1 : 0);
      boot_log(line);
    }
  }
  if (queue_dirty)
    save_queue_file(queue_path, queue);
  audio.shutdown();
  renderer.shutdown();
  if (romfs_mounted)
    romfsExit();
  C2D_Fini();
  C3D_Fini();
  gfxExit();
  return 0;
}
