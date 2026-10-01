#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "3dsmsc/playback/playback_controller.hpp"

namespace threedsmsc {
namespace {

// After this many tracks fail to play in a row, playback stops instead of skipping the queue.
constexpr int max_consecutive_failures = 3;
// Failures reset once a track has really played this long.
constexpr std::uint64_t healthy_playback_ms = 2000;
// A queue of unplayable files would otherwise fill the log with one line per file.
constexpr std::size_t max_logged_failures = 5;

}

PlaybackController::PlaybackController(PlaybackQueue& queue, AudioPlayer& audio,
                                       MetadataReader& metadata, const Settings& settings,
                                       LogSink log)
    : queue_(queue),
      audio_(audio),
      metadata_(metadata),
      settings_(settings),
      log_(std::move(log)) {}

void PlaybackController::set_status(std::string status) {
  status_ = std::move(status);
  has_status_ = true;
}

void PlaybackController::log(const std::string& line) const {
  if (log_)
    log_(line);
}

bool PlaybackController::take_status(std::string& status) {
  if (!has_status_)
    return false;
  status = std::move(status_);
  status_.clear();
  has_status_ = false;
  return true;
}

bool PlaybackController::take_queue_dirty() {
  const bool dirty = queue_dirty_;
  queue_dirty_ = false;
  return dirty;
}

void PlaybackController::fill_missing_metadata() {
  Track* track = queue_.mutable_current();
  if (track == nullptr || (track->duration_ms != 0 && track->title != track->path))
    return;
  TrackMetadata metadata;
  if (!metadata_.read(track->path, metadata))
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
}

bool PlaybackController::load_current(bool resume) {
  std::size_t skipped = 0;
  for (std::size_t attempt = 0; attempt < queue_.size(); ++attempt) {
    const Track* track = queue_.current();
    if (track == nullptr)
      break;
    if (loaded_path_ == track->path || audio_.load(*track)) {
      loaded_path_ = track->path;
      fill_missing_metadata();
      if (resume)
        audio_.play();
      if (skipped != 0)
        set_status("Skipped " + std::to_string(skipped) + " unplayable track(s)");
      return true;
    }
    loaded_path_.clear();
    if (skipped < max_logged_failures)
      log("cannot play " + track->path + ": " + audio_.error());
    // With no audio engine every track fails the same way; do not walk the whole queue.
    if (!audio_.available())
      break;
    ++skipped;
    queue_.next();
  }
  audio_.stop();
  loaded_path_.clear();
  if (!queue_.empty()) {
    // Say why: a missing DSP firmware looks the same as an unreadable file otherwise.
    set_status(audio_.error().empty() ? "No playable tracks in queue" : audio_.error());
  }
  return false;
}

void PlaybackController::change_track(bool forward) {
  const bool resume = audio_.snapshot().state == PlaybackState::Playing;
  if (queue_.empty())
    return;
  if (forward)
    queue_.next();
  else
    queue_.previous();
  load_current(resume);
  queue_dirty_ = true;
}

void PlaybackController::play_tracks(std::vector<Track> tracks, std::size_t start) {
  queue_.set_tracks(std::move(tracks));
  queue_.select(start);
  invalidate();
  if (load_current(true))
    set_status("Playing");
  queue_dirty_ = true;
}

bool PlaybackController::play_queue_entry(std::size_t index) {
  if (!queue_.select(index))
    return false;
  invalidate();
  load_current(true);
  queue_dirty_ = true;
  return true;
}

void PlaybackController::toggle_play_pause() {
  if (queue_.current() == nullptr)
    return;
  if (loaded_path_.empty() && !load_current(false))
    return;
  if (audio_.snapshot().state == PlaybackState::Playing) {
    audio_.pause();
    set_status("Paused");
  } else {
    audio_.play();
    set_status("Playing");
  }
}

void PlaybackController::seek(int direction) {
  if (queue_.current() == nullptr || loaded_path_.empty()) {
    set_status("Nothing to seek");
    return;
  }
  const std::int64_t step_ms = static_cast<std::int64_t>(settings_.seek_seconds) * 1000;
  if (audio_.seek(direction < 0 ? -step_ms : step_ms)) {
    set_status(std::string(direction < 0 ? "Back " : "Forward ") +
               std::to_string(settings_.seek_seconds) + " s");
  } else {
    set_status("This file cannot be seeked");
  }
}

PlaybackSnapshot PlaybackController::tick(bool was_playing) {
  audio_.update();
  const PlaybackSnapshot playback = audio_.snapshot();
  if (playback.state == PlaybackState::Playing && playback.position_ms > healthy_playback_ms)
    consecutive_failures_ = 0;
  const bool finished = playback.state == PlaybackState::Stopped;
  const bool failed = playback.state == PlaybackState::Error;
  if (!was_playing || (!finished && !failed))
    return playback;
  consecutive_failures_ = failed ? consecutive_failures_ + 1 : 0;
  if (consecutive_failures_ >= max_consecutive_failures) {
    audio_.stop();
    invalidate();
    set_status("Playback failed: unsupported or damaged file");
    consecutive_failures_ = 0;
  } else if (finished && settings_.repeat == RepeatMode::One) {
    invalidate();  // play the same track again
    load_current(true);
  } else if (finished && settings_.repeat == RepeatMode::Off && queue_.at_last()) {
    set_status("End of queue");  // stay on the last track, stopped
  } else {
    queue_.next();
    load_current(true);
    queue_dirty_ = true;
  }
  return playback;
}

}
