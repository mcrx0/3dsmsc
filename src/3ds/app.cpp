#include "app.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

#include "3dsmsc/config/settings_file.hpp"
#include "3dsmsc/library/folder_list_file.hpp"
#include "3dsmsc/queue/queue_file.hpp"
#include "boot_log.hpp"

namespace threedsmsc {
namespace {

constexpr float volume_step = 0.05F;
constexpr float two_pi = 6.2831855F;
constexpr std::uint64_t queue_save_delay_ms = 5000;
constexpr std::uint64_t headphone_poll_ms = 1000;
constexpr std::uint64_t slow_frame_ms = 100;
constexpr int max_slow_frame_logs = 40;

Settings load_initial_settings() {
  Settings settings = default_settings();
  std::string error;
  load_settings_file(config_path, settings, &error);
  return settings;
}

}

App::App(CassetteRenderer& renderer)
    : renderer_(renderer),
      settings_(load_initial_settings()),
      artwork_locator_(filesystem_),
      library_(filesystem_, settings_),
      // The folder picker can browse the whole SD card, so its root is the card itself.
      folder_browser_(filesystem_, "sdmc:/"),
      metadata_reader_(embedded_reader_, filename_reader_),
      playback_(queue_, audio_, metadata_reader_, settings_,
                [](const std::string& line) { boot_log(line.c_str()); }),
      equalizer_(settings_),
      controls_lines_(controls_help()),
      about_lines_(about_help(APP_VERSION)) {
  mcuHwcInit();
  ptmuInit();
  audio_available_ = audio_.init();
  cover_loader_.start();
  if (audio_available_) {
    boot_log("audio ready");
  } else {
    char line[96];
    std::snprintf(line, sizeof(line), "audio unavailable: ndspInit result 0x%08lX (%s)",
                  static_cast<unsigned long>(audio_.init_result()), audio_.error().c_str());
    boot_log(line);
  }
  volume_ = static_cast<float>(settings_.volume);
  audio_.set_volume(volume_);
  audio_.set_background_playback(settings_.background_playback);
  audio_.set_equalizer(settings_.eq_enabled, settings_.eq_gains);
  std::string browser_error;
  folder_browser_.refresh(browser_error);
  selected_folders_ = load_folder_list(folders_path);
  library_.set_selected_roots(selected_folders_);
  std::string queue_error;
  load_queue_file(queue_path, queue_, &queue_error);
  queue_.seed(static_cast<std::uint32_t>(osGetTime()));
  queue_.set_shuffle(settings_.shuffle);
  last_queue_save_ = osGetTime();

  view_.artist = "Offline library player";
  view_.album = "New Nintendo 3DS";
  view_.light_theme = settings_.theme == Theme::Light;
  view_.seek_seconds = settings_.seek_seconds;
  view_.repeat_mode = static_cast<int>(settings_.repeat);
  view_.battery_display = static_cast<int>(settings_.battery_display);
  view_.show_cover = settings_.show_cover;
  view_.shuffle = settings_.shuffle;
  view_.library_path = describe_roots();
  view_.status = audio_available_ ? library_.status() : audio_.error();
}

void App::refresh_view() {
  const Track* track = queue_.current();
  view_.has_track = track != nullptr;
  if (track == nullptr) {
    view_.title.clear();
    view_.artist = "Offline library player";
    view_.album = "New Nintendo 3DS";
    view_.artwork_path.clear();
    return;
  }
  view_.title = track->title;
  if (track->artist.empty())
    view_.artist = "Unknown artist";
  else
    view_.artist = track->artist;
  if (track->album.empty())
    view_.album = "Unknown album";
  else
    view_.album = track->album;
  view_.artwork_path = track->artwork_path;
}

void App::sync_playback() {
  std::string status;
  if (playback_.take_status(status))
    view_.status = std::move(status);
  if (playback_.take_queue_dirty())
    queue_dirty_ = true;
  refresh_view();
}

void App::save_settings() {
  save_settings_file(config_path, settings_, &config_error_);
}

void App::change_volume(float delta) {
  const float wanted = volume_ + delta;
  volume_ = wanted < 0.0F ? 0.0F : (wanted > 1.0F ? 1.0F : wanted);
  audio_.set_volume(volume_);
  settings_.volume = volume_;
  save_settings();
  view_.status = "Volume: " + std::to_string(std::lround(volume_ * 100.0F)) + "%";
}

void App::update_frame_state(const PlaybackSnapshot& playback, std::uint64_t frame_start) {
  // Headphones: one DSP query a second is plenty, and cheaper than asking every frame.
  if (frame_start - last_headphone_poll_ >= headphone_poll_ms) {
    last_headphone_poll_ = frame_start;
    audio_.poll_headphones();
    view_.headphones = audio_.headphones();
    poll_battery();
  }
  view_.playing = audio_.snapshot().state == PlaybackState::Playing;
  const Track* current = queue_.current();
  view_.position_ms = playback.position_ms;
  view_.duration_ms = current != nullptr ? current->duration_ms : 0;
  view_.volume = volume_;
  view_.library_tracks = library_.library().tracks.size();
  view_.queue_tracks = queue_.size();
  view_.progress =
      current != nullptr && current->duration_ms != 0
          ? static_cast<float>(playback.position_ms) / static_cast<float>(current->duration_ms)
          : 0.0F;
  if (view_.playing && settings_.animation_enabled) {
    view_.reel_phase += 0.08F * static_cast<float>(settings_.animation_speed);
    if (view_.reel_phase > two_pi)
      view_.reel_phase -= two_pi;
  }
}

// Both calls are service requests, so they share the once-a-second poll. On failure the last
// known value stays, instead of the icon vanishing for a frame.
void App::poll_battery() {
  std::uint8_t percent = 0;
  if (R_SUCCEEDED(MCUHWC_GetBatteryLevel(&percent)) && percent <= 100)
    view_.battery_percent = percent;
  std::uint8_t charging = 0;
  if (R_SUCCEEDED(PTMU_GetBatteryChargeState(&charging)))
    view_.battery_charging = charging != 0;
}

// Keeps the cover on the screen in step with the current track. The picture comes from the
// background decoder, so a track change first drops the old cover (a wrong picture is worse than
// the placeholder) and the new one appears a moment later.
void App::update_cover() {
  static const std::string no_cover;
  Track* track = queue_.mutable_current();
  if (settings_.show_cover && track != nullptr && track->artwork_path.empty() &&
      track->path != cover_lookup_path_) {
    cover_lookup_path_ = track->path;
    artwork_locator_.find(track->path, track->artwork_path);
  }
  const std::string& wanted =
      settings_.show_cover && track != nullptr ? track->artwork_path : no_cover;
  if (wanted != cover_path_) {
    cover_path_ = wanted;
    renderer_.set_cover(nullptr);
    cover_loader_.request(cover_path_);
  }
  std::string loaded;
  bool ok = false;
  if (cover_loader_.take(loaded, cover_buffer_, ok) && ok && loaded == cover_path_)
    renderer_.set_cover(&cover_buffer_);
}

void App::save_queue_if_due() {
  if (queue_dirty_ && osGetTime() - last_queue_save_ > queue_save_delay_ms) {
    save_queue_file(queue_path, queue_);
    queue_dirty_ = false;
    last_queue_save_ = osGetTime();
  }
}

// A normal frame is ~16 ms. Record the slow ones so a stall on real hardware can be traced to
// input/logic work or to drawing.
void App::log_slow_frame(std::uint64_t frame_start, std::uint64_t render_start,
                         std::uint64_t frame_end) {
  if (frame_end - frame_start <= slow_frame_ms || slow_frame_logs_ >= max_slow_frame_logs)
    return;
  ++slow_frame_logs_;
  char line[112];
  std::snprintf(line, sizeof(line), "slow frame %lums: logic=%lu draw=%lu panel=%d playing=%d",
                static_cast<unsigned long>(frame_end - frame_start),
                static_cast<unsigned long>(render_start - frame_start),
                static_cast<unsigned long>(frame_end - render_start), static_cast<int>(panel()),
                view_.playing ? 1 : 0);
  boot_log(line);
}

void App::run() {
  restore_library_cache();
  playback_.load_current(false);
  sync_playback();
  view_.status = audio_available_ ? library_.status() : audio_.error();
  while (aptMainLoop()) {
    const std::uint64_t frame_start = osGetTime();
    hidScanInput();
    const std::uint32_t keys_down = hidKeysDown();
    if ((keys_down & KEY_START) != 0)
      break;
    if ((keys_down & KEY_L) != 0)
      change_volume(-volume_step);
    if ((keys_down & KEY_R) != 0)
      change_volume(volume_step);
    // Decided before the keys run: a button that changes screen must not also send this frame's
    // touch to the screen it just left.
    const bool equalizer_open = panel() == Panel::Equalizer;
    const bool in_list = panel() != Panel::Home && !equalizer_open;
    if (equalizer_open) {
      handle_equalizer_input(keys_down);
    } else if (in_list) {
      handle_list_input(keys_down);
    } else {
      handle_home_input(keys_down);
    }
    if ((keys_down & KEY_TOUCH) != 0 && !equalizer_open) {
      touchPosition touch = {};
      hidTouchRead(&touch);
      if (in_list)
        handle_list_touch(touch);
      else
        handle_home_touch(touch);
    }
    const PlaybackSnapshot playback = playback_.tick(view_.playing);
    sync_playback();
    update_cover();
    update_frame_state(playback, frame_start);
    save_queue_if_due();
    const std::uint64_t render_start = osGetTime();
    renderer_.render(view_);  // C3D_FrameEnd presents and syncs to vblank
    log_slow_frame(frame_start, render_start, osGetTime());
  }
  shutdown();
}

void App::shutdown() {
  save_equalizer();
  sync_playback();
  if (queue_dirty_)
    save_queue_file(queue_path, queue_);
  cover_loader_.stop();
  audio_.shutdown();
  ptmuExit();
  mcuHwcExit();
}

}
