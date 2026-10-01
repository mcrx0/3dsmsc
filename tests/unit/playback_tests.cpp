#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "3dsmsc/audio/player.hpp"
#include "3dsmsc/config/settings.hpp"
#include "3dsmsc/library/metadata.hpp"
#include "3dsmsc/playback/playback_controller.hpp"
#include "3dsmsc/queue/playback_queue.hpp"
#include "test_suites.hpp"
#include "test_support.hpp"

namespace {

using threedsmsc::PlaybackState;
using threedsmsc::RepeatMode;

// An audio backend that records what it is asked to do and fails on demand.
class FakeAudio final : public threedsmsc::AudioPlayer {
 public:
  bool available() const override { return engine_running; }
  bool load(const threedsmsc::Track& track) override {
    calls.push_back("load:" + track.path);
    if (!engine_running || unplayable.count(track.path) != 0) {
      error_text = engine_running ? "Cannot open or decode this file" : "No DSP firmware";
      return false;
    }
    error_text.clear();
    loaded = track.path;
    state = PlaybackState::Stopped;
    return true;
  }
  void play() override {
    calls.push_back("play");
    state = PlaybackState::Playing;
  }
  void pause() override {
    calls.push_back("pause");
    state = PlaybackState::Paused;
  }
  void stop() override {
    calls.push_back("stop");
    state = PlaybackState::Stopped;
  }
  void update() override {}
  bool seek(std::int64_t delta_ms) override {
    calls.push_back("seek:" + std::to_string(delta_ms));
    return seekable;
  }
  threedsmsc::PlaybackSnapshot snapshot() const override {
    threedsmsc::PlaybackSnapshot result;
    result.state = state;
    result.position_ms = position_ms;
    return result;
  }
  const std::string& error() const override { return error_text; }

  int count(const std::string& call) const {
    int total = 0;
    for (const std::string& entry : calls) {
      if (entry == call)
        ++total;
    }
    return total;
  }

  bool engine_running = true;
  bool seekable = true;
  std::set<std::string> unplayable;
  PlaybackState state = PlaybackState::Stopped;
  std::uint64_t position_ms = 0;
  std::string loaded;
  std::string error_text;
  std::vector<std::string> calls;
};

// Tags for files that are "read" by the controller when a restored queue lacks them.
class FakeMetadata final : public threedsmsc::MetadataReader {
 public:
  bool read(const std::string& path, threedsmsc::TrackMetadata& metadata) override {
    const auto found = tags.find(path);
    if (found == tags.end())
      return false;
    metadata = found->second;
    return true;
  }
  std::map<std::string, threedsmsc::TrackMetadata> tags;
};

struct Rig {
  explicit Rig(std::vector<std::string> paths = {"a.mp3", "b.mp3", "c.mp3"})
      : settings(threedsmsc::default_settings()),
        controller(queue, audio, metadata, settings,
                   [this](const std::string& line) { log.push_back(line); }) {
    std::vector<threedsmsc::Track> tracks;
    for (const std::string& path : paths) {
      threedsmsc::Track track = test::make_track(path.c_str(), path.c_str(), "Artist");
      track.duration_ms = 1000;  // complete tags: nothing to read when loading
      tracks.push_back(track);
    }
    queue.set_tracks(tracks);
  }

  std::string status() {
    std::string text;
    return controller.take_status(text) ? text : std::string("<none>");
  }
  const threedsmsc::Track& current() const { return *queue.current(); }

  threedsmsc::Settings settings;
  threedsmsc::PlaybackQueue queue;
  FakeAudio audio;
  FakeMetadata metadata;
  std::vector<std::string> log;
  threedsmsc::PlaybackController controller;
};

void test_load_current() {
  Rig rig;
  assert(rig.controller.load_current(false));
  assert(rig.audio.loaded == "a.mp3");
  assert(rig.audio.count("play") == 0);  // loaded, but not started
  assert(rig.controller.has_loaded());
  assert(rig.status() == "<none>");

  // Asking again for the same file does not reopen it.
  assert(rig.controller.load_current(true));
  assert(rig.audio.count("load:a.mp3") == 1);
  assert(rig.audio.count("play") == 1);
}

void test_unplayable_tracks_are_skipped() {
  Rig rig({"bad1.mp3", "bad2.mp3", "good.mp3"});
  rig.audio.unplayable = {"bad1.mp3", "bad2.mp3"};
  assert(rig.controller.load_current(true));
  assert(rig.audio.loaded == "good.mp3");
  assert(rig.current().path == "good.mp3");  // the queue moved past the broken files
  assert(rig.status() == "Skipped 2 unplayable track(s)");
  assert(rig.log.size() == 2);
  assert(rig.log[0] == "cannot play bad1.mp3: Cannot open or decode this file");
  assert(rig.status() == "<none>");  // a status is delivered once
}

void test_nothing_playable() {
  Rig rig({"bad1.mp3", "bad2.mp3"});
  rig.audio.unplayable = {"bad1.mp3", "bad2.mp3"};
  assert(!rig.controller.load_current(true));
  assert(rig.audio.count("stop") == 1);
  assert(!rig.controller.has_loaded());
  assert(rig.status() == "Cannot open or decode this file");

  // Without an audio engine every track fails the same way, so only one is tried.
  Rig silent;
  silent.audio.engine_running = false;
  assert(!silent.controller.load_current(true));
  assert(silent.audio.count("load:a.mp3") == 1);
  assert(silent.audio.count("load:b.mp3") == 0);
  assert(silent.status() == "No DSP firmware");  // the reason, not a generic message

  // An empty queue is not an error worth reporting.
  Rig empty({});
  assert(!empty.controller.load_current(true));
  assert(empty.status() == "<none>");
}

void test_the_log_is_bounded() {
  std::vector<std::string> paths;
  for (int index = 0; index < 20; ++index)
    paths.push_back("bad" + std::to_string(index) + ".mp3");
  Rig rig(paths);
  for (const std::string& path : paths)
    rig.audio.unplayable.insert(path);
  assert(!rig.controller.load_current(true));
  assert(rig.log.size() == 5);  // a whole broken queue must not flood the log
}

void test_change_track_keeps_playing_state() {
  Rig rig;
  assert(rig.controller.load_current(true));  // playing a.mp3
  rig.controller.change_track(true);
  assert(rig.current().path == "b.mp3");
  assert(rig.audio.state == PlaybackState::Playing);  // it kept playing

  rig.audio.state = PlaybackState::Paused;
  rig.controller.change_track(false);
  assert(rig.current().path == "a.mp3");
  // Two plays so far: the initial one, and the one that kept it going to b.mp3. Changing
  // track while paused must not add a third.
  assert(rig.audio.count("play") == 2);
  assert(rig.controller.take_queue_dirty());
  assert(!rig.controller.take_queue_dirty());  // reported once
}

void test_play_tracks_and_queue_entries() {
  Rig rig;
  std::vector<threedsmsc::Track> chosen = {test::make_track("x.mp3", "X", "A"),
                                           test::make_track("y.mp3", "Y", "A")};
  rig.controller.play_tracks(chosen, 1);
  assert(rig.current().path == "y.mp3");
  assert(rig.audio.loaded == "y.mp3" && rig.audio.state == PlaybackState::Playing);
  assert(rig.status() == "Playing");

  assert(rig.controller.play_queue_entry(0));
  assert(rig.audio.loaded == "x.mp3");
  assert(!rig.controller.play_queue_entry(5));  // past the end: refused, nothing changes
  assert(rig.current().path == "x.mp3");
}

void test_toggle_play_pause() {
  Rig rig;
  rig.controller.toggle_play_pause();  // nothing is loaded yet: it loads, then plays
  assert(rig.audio.loaded == "a.mp3");
  assert(rig.audio.state == PlaybackState::Playing);
  assert(rig.status() == "Playing");

  rig.controller.toggle_play_pause();
  assert(rig.audio.state == PlaybackState::Paused);
  assert(rig.status() == "Paused");
  rig.controller.toggle_play_pause();
  assert(rig.audio.state == PlaybackState::Playing);

  Rig empty({});
  empty.controller.toggle_play_pause();  // no track: nothing happens
  assert(empty.audio.calls.empty());

  Rig broken({"bad.mp3"});
  broken.audio.unplayable = {"bad.mp3"};
  broken.controller.toggle_play_pause();  // cannot load: it must not try to play
  assert(broken.audio.count("play") == 0);
}

void test_seek() {
  Rig rig;
  rig.controller.seek(1);  // no file loaded
  assert(rig.status() == "Nothing to seek");
  assert(rig.audio.count("seek:10000") == 0);

  assert(rig.controller.load_current(true));
  rig.controller.seek(1);
  assert(rig.status() == "Forward 10 s");
  rig.controller.seek(-1);
  assert(rig.status() == "Back 10 s");
  assert(rig.audio.count("seek:10000") == 1 && rig.audio.count("seek:-10000") == 1);

  rig.settings.seek_seconds = 25;  // the step comes from the live settings
  rig.controller.seek(1);
  assert(rig.audio.count("seek:25000") == 1);
  assert(rig.status() == "Forward 25 s");

  rig.audio.seekable = false;
  rig.controller.seek(1);
  assert(rig.status() == "This file cannot be seeked");
}

// Simulates "the track ended" or "the track failed" for the end-of-track rules.
void finish_track(Rig& rig, PlaybackState how) {
  rig.audio.state = how;
  rig.controller.tick(true);
}

void test_repeat_all_advances_and_wraps() {
  Rig rig;
  rig.settings.repeat = RepeatMode::All;
  assert(rig.controller.load_current(true));
  finish_track(rig, PlaybackState::Stopped);
  assert(rig.current().path == "b.mp3" && rig.audio.state == PlaybackState::Playing);
  finish_track(rig, PlaybackState::Stopped);
  finish_track(rig, PlaybackState::Stopped);  // after the last track the queue starts over
  assert(rig.current().path == "a.mp3" && rig.audio.state == PlaybackState::Playing);
}

void test_repeat_one_replays_the_same_track() {
  Rig rig;
  rig.settings.repeat = RepeatMode::One;
  assert(rig.controller.load_current(true));
  finish_track(rig, PlaybackState::Stopped);
  assert(rig.current().path == "a.mp3");
  assert(rig.audio.count("load:a.mp3") == 2);  // reopened, not just resumed
  assert(rig.audio.state == PlaybackState::Playing);
}

void test_repeat_off_stops_after_the_last_track() {
  Rig rig;
  rig.settings.repeat = RepeatMode::Off;
  assert(rig.controller.load_current(true));
  finish_track(rig, PlaybackState::Stopped);  // not the last track yet: moves on
  assert(rig.current().path == "b.mp3");
  finish_track(rig, PlaybackState::Stopped);
  assert(rig.current().path == "c.mp3");
  rig.status();
  finish_track(rig, PlaybackState::Stopped);  // the last one ended
  assert(rig.status() == "End of queue");
  assert(rig.current().path == "c.mp3");  // stays on the last track
  assert(rig.audio.state == PlaybackState::Stopped);
}

void test_nothing_happens_unless_playback_was_running() {
  Rig rig;
  assert(rig.controller.load_current(false));
  rig.audio.state = PlaybackState::Stopped;
  rig.controller.tick(
      false);  // it was not playing last frame, so a Stopped state is not "finished"
  assert(rig.current().path == "a.mp3");
  assert(rig.audio.count("play") == 0);
}

void test_repeated_failures_stop_playback() {
  Rig rig({"a.mp3", "b.mp3", "c.mp3", "d.mp3"});
  assert(rig.controller.load_current(true));
  finish_track(rig, PlaybackState::Error);  // failure 1: skip ahead
  assert(rig.current().path == "b.mp3");
  rig.audio.state = PlaybackState::Playing;
  finish_track(rig, PlaybackState::Error);  // failure 2
  assert(rig.current().path == "c.mp3");
  rig.audio.state = PlaybackState::Playing;
  rig.status();
  finish_track(rig, PlaybackState::Error);  // failure 3 in a row: give up
  assert(rig.status() == "Playback failed: unsupported or damaged file");
  assert(rig.audio.count("stop") >= 1);
  assert(!rig.controller.has_loaded());
  assert(rig.current().path == "c.mp3");  // it did not keep walking the queue
}

void test_healthy_playback_resets_the_failure_count() {
  Rig rig({"a.mp3", "b.mp3", "c.mp3", "d.mp3"});
  assert(rig.controller.load_current(true));
  finish_track(rig, PlaybackState::Error);
  rig.audio.state = PlaybackState::Playing;
  rig.audio.position_ms = 5000;  // a track that really played resets the count
  rig.controller.tick(true);
  rig.audio.position_ms = 0;
  finish_track(rig, PlaybackState::Error);
  rig.audio.state = PlaybackState::Playing;
  finish_track(rig, PlaybackState::Error);
  assert(rig.current().path == "d.mp3");  // three failures since the reset would stop it
  assert(rig.controller.has_loaded());
}

void test_missing_tags_are_read_when_a_track_is_loaded() {
  threedsmsc::Track bare = test::make_track("sdmc:/music/song.mp3", "sdmc:/music/song.mp3", "");
  Rig rig({});
  rig.queue.set_tracks({bare});  // a queue restored from disk: the title is the path
  threedsmsc::TrackMetadata tags;
  tags.title = "Real Title";
  tags.artist = "Real Artist";
  tags.album = "Real Album";
  tags.duration_ms = 187000;
  tags.track_number = 4;
  rig.metadata.tags["sdmc:/music/song.mp3"] = tags;
  assert(rig.controller.load_current(false));
  assert(rig.current().title == "Real Title" && rig.current().artist == "Real Artist");
  assert(rig.current().album == "Real Album" && rig.current().duration_ms == 187000);
  assert(rig.current().track_number == 4);

  // A track that already has its tags is left alone.
  Rig complete;
  complete.queue.mutable_current()->title = "Kept";
  complete.metadata.tags["a.mp3"] = tags;
  assert(complete.controller.load_current(false));
  assert(complete.current().title == "Kept");
}

void test_invalidate_forces_a_reload() {
  Rig rig;
  assert(rig.controller.load_current(false));
  rig.controller.invalidate();
  assert(!rig.controller.has_loaded());
  assert(rig.controller.load_current(false));
  assert(rig.audio.count("load:a.mp3") == 2);
}

}

void run_playback_tests() {
  test_load_current();
  test_unplayable_tracks_are_skipped();
  test_nothing_playable();
  test_the_log_is_bounded();
  test_change_track_keeps_playing_state();
  test_play_tracks_and_queue_entries();
  test_toggle_play_pause();
  test_seek();
  test_repeat_all_advances_and_wraps();
  test_repeat_one_replays_the_same_track();
  test_repeat_off_stops_after_the_last_track();
  test_nothing_happens_unless_playback_was_running();
  test_repeated_failures_stop_playback();
  test_healthy_playback_resets_the_failure_count();
  test_missing_tags_are_read_when_a_track_is_loaded();
  test_invalidate_forces_a_reload();
}
