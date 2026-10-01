#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "3dsmsc/audio/player.hpp"
#include "3dsmsc/config/settings.hpp"
#include "3dsmsc/library/metadata.hpp"
#include "3dsmsc/queue/playback_queue.hpp"

namespace threedsmsc {

// The rules that connect the queue to the audio player: which file plays, what happens when a
// track ends, and how unplayable files and seeks are handled. It holds no UI state: results that
// the screen should show come back through take_status().
class PlaybackController {
 public:
  using LogSink = std::function<void(const std::string& line)>;

  PlaybackController(PlaybackQueue& queue, AudioPlayer& audio, MetadataReader& metadata,
                     const Settings& settings, LogSink log);

  // Loads the queue's current track, skipping files that cannot be opened (for example deleted
  // since the queue was saved). False when nothing in the queue can be played.
  bool load_current(bool resume);
  // Moves to the next or previous track, continuing to play if playback was running.
  void change_track(bool forward);
  // Replaces the queue and starts the track at `start`.
  void play_tracks(std::vector<Track> tracks, std::size_t start);
  // Plays the queue entry at `index` (an index into the queue, not the library).
  bool play_queue_entry(std::size_t index);
  void toggle_play_pause();
  // Seeks by the configured step; direction is -1 (back) or +1 (forward).
  void seek(int direction);
  // Call once per frame. Applies the end-of-track rules and returns the player's snapshot from
  // before they ran. `was_playing` is whether the previous frame was playing.
  PlaybackSnapshot tick(bool was_playing);
  // Forgets the loaded file, so the next load reopens it (the queue's contents changed).
  void invalidate() { loaded_path_.clear(); }
  bool has_loaded() const { return !loaded_path_.empty(); }

  // A message produced by the last operations (for example "Skipped 2 unplayable track(s)"). The
  // latest one wins; taking it clears it. Returns false when there is none.
  bool take_status(std::string& status);
  // True once if the queue's contents or position changed since the last call.
  bool take_queue_dirty();

 private:
  void set_status(std::string status);
  // Reads the title, artist, and length of a queue entry that only has a path (a queue restored
  // from disk has no tags until a rescan).
  void fill_missing_metadata();
  void log(const std::string& line) const;

  PlaybackQueue& queue_;
  AudioPlayer& audio_;
  MetadataReader& metadata_;
  const Settings& settings_;
  LogSink log_;
  std::string loaded_path_;
  std::string status_;
  bool has_status_ = false;
  bool queue_dirty_ = false;
  int consecutive_failures_ = 0;
};

}
