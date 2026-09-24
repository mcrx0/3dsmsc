#include <3ds.h>
#include <3ds/applets/swkbd.h>

#include <cstddef>
#include <cstdio>
#include <vector>

#include "3dsmsc/config/settings.hpp"
#include "3dsmsc/config/settings_file.hpp"
#include "3dsmsc/library/controller.hpp"
#include "3dsmsc/library/filesystem.hpp"
#include "3dsmsc/library/folder_browser.hpp"
#include "3dsmsc/library/library_index.hpp"
#include "3dsmsc/queue/playback_queue.hpp"
#include "3dsmsc/queue/queue_file.hpp"
#include "cassette_renderer.hpp"
#include "ndsp_player.hpp"

namespace {

void show_startup_error(const char* message) {
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
  consoleInit(GFX_TOP, nullptr);
  std::printf("3dsmsc 0.4.2b\nStarting renderer...\n");
  if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
    show_startup_error("3D renderer initialization failed.");
    gfxExit();
    return 1;
  }
  if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
    show_startup_error("2D renderer initialization failed.");
    C3D_Fini();
    gfxExit();
    return 1;
  }
  C2D_Prepare();
  threedsmsc::CassetteRenderer renderer;
  if (!renderer.init()) {
    show_startup_error("UI renderer initialization failed.");
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 1;
  }
  threedsmsc::CassetteView boot_view{};
  boot_view.artist = "Offline library player";
  boot_view.album = "New Nintendo 3DS";
  boot_view.status = "Starting audio...";
  renderer.render(boot_view);
  gfxFlushBuffers();
  gfxSwapBuffers();
  gspWaitForVBlank();
  threedsmsc::NdspAudioPlayer audio;
  const bool audio_available = audio.init();
  threedsmsc::Settings settings = threedsmsc::default_settings();
  std::string config_error;
  threedsmsc::load_settings_file("sdmc:/3dmms/config.toml", settings, &config_error);
  float volume = settings.volume;
  audio.set_volume(volume);
  audio.set_background_playback(settings.background_playback);
  threedsmsc::LocalFileSystem filesystem;
  threedsmsc::LibraryController library(filesystem, settings);
  threedsmsc::FolderBrowser folder_browser(filesystem, settings.music_root);
  std::string browser_error;
  folder_browser.refresh(browser_error);
  threedsmsc::PlaybackQueue queue;
  const std::string queue_path = "sdmc:/3dmms/queue.txt";
  std::string queue_error;
  load_queue_file(queue_path, queue, &queue_error);
  threedsmsc::CassetteView view;
  std::vector<std::size_t> search_results;
  std::vector<std::size_t> album_results;
  std::size_t search_selected = 0;
  std::size_t album_selected = 0;
  std::string loaded_path;
  view.artist = "Offline library player";
  view.album = "New Nintendo 3DS";
  view.playing = false;
  view.progress = 0.0f;
  view.reel_phase = 0.0f;
  view.has_track = false;
  view.selected_panel = 0;
  view.list_count = 0;
  view.list_selected = 0;
  view.list_visible = false;
  view.search_query.clear();
  view.status = audio_available ? library.status() : "Audio unavailable; UI active";
  const auto refresh_view = [&] {
    const threedsmsc::Track* track = queue.current();
    view.has_track = track != nullptr;
    if (track != nullptr) {
      if (loaded_path != track->path) {
        loaded_path = track->path;
        if (!audio.load(*track))
          loaded_path.clear();
      }
      view.title = track->title;
      view.artist = track->artist.empty() ? "Unknown artist" : track->artist;
      view.album = track->album.empty() ? "Unknown album" : track->album;
      view.artwork_path = track->artwork_path;
    } else {
      if (!loaded_path.empty()) {
        audio.stop();
        loaded_path.clear();
      }
      view.title.clear();
      view.artist = "Offline library player";
      view.album = "New Nintendo 3DS";
      view.artwork_path.clear();
    }
    view.status = library.status();
  };
  const auto change_queue_track = [&](bool forward) {
    const bool resume = audio.snapshot().state == threedsmsc::PlaybackState::Playing;
    const threedsmsc::Track* track = forward ? queue.next() : queue.previous();
    if (track == nullptr)
      return;
    refresh_view();
    if (resume)
      audio.play();
  };
  const auto refresh_list = [&] {
    view.list_visible =
        view.selected_panel == 1 || view.selected_panel == 2 || view.selected_panel == 3;
    view.list_count = 0;
    view.list_selected = 0;
    if (view.selected_panel == 1) {
      search_results =
          find_tracks(library.library(), view.search_query, settings.search_case_sensitive);
      if (search_selected >= search_results.size())
        search_selected = 0;
      view.list_selected = search_selected;
      view.list_count = search_results.size() < view.list_items.size() ? search_results.size()
                                                                       : view.list_items.size();
      for (std::size_t index = 0; index < view.list_count; ++index)
        view.list_items[index] = library.library().tracks[search_results[index]].title;
    } else if (view.selected_panel == 2) {
      album_results = find_albums(library.library());
      if (album_selected >= album_results.size())
        album_selected = 0;
      view.list_selected = album_selected;
      view.list_count = album_results.size() < view.list_items.size() ? album_results.size()
                                                                      : view.list_items.size();
      for (std::size_t index = 0; index < view.list_count; ++index) {
        const auto& track = library.library().tracks[album_results[index]];
        view.list_items[index] = track.album.empty() ? "Unknown album" : track.album;
      }
    } else if (view.selected_panel == 3) {
      const auto& tracks = queue.tracks();
      view.list_count =
          tracks.size() < view.list_items.size() ? tracks.size() : view.list_items.size();
      for (std::size_t index = 0; index < view.list_count; ++index) {
        view.list_items[index] = tracks[index].title;
        if (&tracks[index] == queue.current())
          view.list_selected = index;
      }
    }
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
      search_selected = 0;
      refresh_list();
    }
  };
  const auto scan_library = [&] {
    library.scan();
    queue.set_tracks(library.library().tracks);
    save_queue_file(queue_path, queue);
    refresh_view();
    refresh_list();
  };
  while (aptMainLoop()) {
    hidScanInput();
    const u32 keys_down = hidKeysDown();
    if (keys_down & KEY_START) {
      break;
    }
    if (keys_down & KEY_L) {
      volume = volume > 0.05f ? volume - 0.05f : 0.0f;
      audio.set_volume(volume);
      settings.volume = volume;
      save_settings_file("sdmc:/3dmms/config.toml", settings, &config_error);
      view.status = "Volume: " + std::to_string(static_cast<int>(volume * 100.0f)) + "%";
    }
    if (keys_down & KEY_R) {
      volume = volume < 0.95f ? volume + 0.05f : 1.0f;
      audio.set_volume(volume);
      settings.volume = volume;
      save_settings_file("sdmc:/3dmms/config.toml", settings, &config_error);
      view.status = "Volume: " + std::to_string(static_cast<int>(volume * 100.0f)) + "%";
    }
    if (keys_down & KEY_LEFT) {
      if (audio.seek(-5000))
        view.status = "Seek back 5 seconds";
    }
    if (keys_down & KEY_RIGHT) {
      if (audio.seek(5000))
        view.status = "Seek forward 5 seconds";
    }
    if (keys_down & KEY_TOUCH) {
      touchPosition touch;
      hidTouchRead(&touch);
      if (touch.py >= 42 && touch.py < 92) {
        view.selected_panel = 0;
        scan_library();
      } else if (touch.py >= 100 && touch.py < 130) {
        if (touch.px < 104) {
          change_queue_track(false);
        } else if (touch.px < 210) {
          const threedsmsc::Track* track = queue.current();
          if (track != nullptr) {
            if (audio.snapshot().state == threedsmsc::PlaybackState::Playing)
              audio.pause();
            else
              audio.play();
            view.status = audio.snapshot().state == threedsmsc::PlaybackState::Playing
                              ? "Playing"
                              : "Paused";
          }
        } else {
          change_queue_track(true);
        }
      } else if (touch.py >= 140 && touch.py < 185) {
        view.selected_panel = touch.px < 160 ? 1 : 2;
        view.status =
            view.selected_panel == 1 ? "Search: A edits, X selects" : "Albums: X selects an album";
      } else if (touch.py >= 190 && touch.py < 235) {
        view.selected_panel = touch.px < 160 ? 3 : 4;
        view.status = view.selected_panel == 3
                          ? (queue.empty() ? "Queue: no tracks selected"
                                           : "Queue: " + std::to_string(queue.size()) + " tracks")
                          : "Settings: Player and Library controls";
      }
      refresh_list();
    }
    if (view.selected_panel == 1) {
      if (keys_down & KEY_DUP && !search_results.empty())
        search_selected = search_selected == 0 ? search_results.size() - 1 : search_selected - 1;
      if (keys_down & KEY_DDOWN && !search_results.empty())
        search_selected = (search_selected + 1) % search_results.size();
      if (search_selected < search_results.size())
        view.list_selected = search_selected;
    }
    if (view.selected_panel == 2) {
      if (keys_down & KEY_DUP && !album_results.empty())
        album_selected = album_selected == 0 ? album_results.size() - 1 : album_selected - 1;
      if (keys_down & KEY_DDOWN && !album_results.empty())
        album_selected = (album_selected + 1) % album_results.size();
      if (album_selected < album_results.size())
        view.list_selected = album_selected;
    }
    if (view.selected_panel == 3) {
      if (keys_down & KEY_DUP)
        change_queue_track(false);
      if (keys_down & KEY_DDOWN)
        change_queue_track(true);
      refresh_list();
    }
    if (keys_down & KEY_Y) {
      if (view.selected_panel == 4) {
        scan_library();
      } else {
        change_queue_track(false);
      }
    }
    if (keys_down & KEY_X) {
      if (view.selected_panel == 1) {
        if (search_selected < search_results.size() &&
            queue.select(search_results[search_selected])) {
          refresh_view();
          refresh_list();
        }
      } else if (view.selected_panel == 2) {
        if (album_selected < album_results.size() && queue.select(album_results[album_selected])) {
          refresh_view();
          refresh_list();
        }
      } else if (view.selected_panel == 4) {
        settings.background_playback = !settings.background_playback;
        audio.set_background_playback(settings.background_playback);
        save_settings_file("sdmc:/3dmms/config.toml", settings, &config_error);
        view.status =
            settings.background_playback ? "Background playback: on" : "Background playback: off";
      } else {
        change_queue_track(true);
      }
    }
    if (view.selected_panel == 4) {
      if (keys_down & KEY_DUP && !folder_browser.entries().empty())
        folder_browser.select(folder_browser.selected() == 0 ? folder_browser.entries().size() - 1
                                                             : folder_browser.selected() - 1);
      if (keys_down & KEY_DDOWN && !folder_browser.entries().empty())
        folder_browser.select(folder_browser.selected() + 1);
      if (keys_down & KEY_B) {
        if (!folder_browser.leave(browser_error))
          view.status = browser_error;
      }
      if (keys_down & KEY_ZL) {
        settings.animation_enabled = !settings.animation_enabled;
        save_settings_file("sdmc:/3dmms/config.toml", settings, &config_error);
        view.status = settings.animation_enabled ? "Reel animation: on" : "Reel animation: off";
      }
      if (keys_down & KEY_A) {
        if (folder_browser.enter(folder_browser.selected(), browser_error)) {
          library.set_selected_root(folder_browser.current_path());
          scan_library();
        } else {
          view.status = browser_error;
        }
      }
    }
    if (keys_down & KEY_A && view.selected_panel == 1) {
      edit_search_query();
    } else if (keys_down & KEY_A && view.selected_panel != 4) {
      const threedsmsc::Track* track = queue.current();
      if (track != nullptr) {
        if (loaded_path != track->path)
          refresh_view();
        if (audio.snapshot().state == threedsmsc::PlaybackState::Playing) {
          audio.pause();
        } else {
          audio.play();
        }
      }
    }
    const bool was_playing = view.playing;
    audio.update();
    const threedsmsc::PlaybackSnapshot playback = audio.snapshot();
    if (was_playing && playback.state == threedsmsc::PlaybackState::Stopped)
      change_queue_track(true);
    view.playing = audio.snapshot().state == threedsmsc::PlaybackState::Playing;
    if (view.playing && settings.animation_enabled) {
      view.reel_phase += 0.08f * static_cast<float>(settings.animation_speed);
      if (view.reel_phase > 6.2831855f)
        view.reel_phase -= 6.2831855f;
    }
    renderer.render(view);
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
  }
  audio.shutdown();
  renderer.shutdown();
  C2D_Fini();
  C3D_Fini();
  gfxExit();
  return 0;
}
