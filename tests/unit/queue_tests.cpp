#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "3dsmsc/library/track.hpp"
#include "3dsmsc/queue/playback_queue.hpp"
#include "3dsmsc/queue/queue_file.hpp"
#include "3dsmsc/ui/browse_navigator.hpp"
#include "test_suites.hpp"
#include "test_support.hpp"

namespace {

using test::entry;
using test::FakeFileSystem;
using test::make_track;

void test_queue() {
  threedsmsc::PlaybackQueue queue;
  assert(queue.empty());
  queue.set_tracks({make_track("a.mp3", "A", "Artist"), make_track("b.flac", "B", "Artist")});
  assert(queue.size() == 2);
  assert(queue.current() != nullptr && queue.current()->title == "A");
  assert(queue.select(1));
  assert(queue.current() != nullptr && queue.current()->title == "B");
  assert(!queue.select(2));
  assert(queue.select(0));
  const threedsmsc::Track* second = queue.next();
  assert(second != nullptr && second->title == "B");
  const threedsmsc::Track* wrapped = queue.next();
  assert(wrapped != nullptr && wrapped->title == "A");
  const threedsmsc::Track* first = queue.previous();
  assert(first != nullptr && first->title == "B");
  queue.clear();
  assert(queue.empty());
}

void test_queue_shuffle() {
  threedsmsc::PlaybackQueue queue;
  queue.seed(12345);
  std::vector<threedsmsc::Track> tracks;
  for (int index = 0; index < 6; ++index)
    tracks.push_back(make_track(("t" + std::to_string(index) + ".mp3").c_str(), "T", "A"));
  queue.set_tracks(tracks);
  assert(queue.select(2));
  assert(!queue.shuffle());
  assert(!queue.at_last());

  // Turning shuffle on keeps the current track, then every other track plays exactly once.
  queue.set_shuffle(true);
  assert(queue.shuffle() && queue.current_index() == 2);
  const auto play_pass = [&](std::size_t first) {
    std::set<std::size_t> seen = {first};
    std::size_t last = first;
    for (int step = 1; step < 6; ++step) {
      assert(!queue.at_last());
      queue.next();
      seen.insert(queue.current_index());
      last = queue.current_index();
    }
    assert(seen.size() == 6);  // all six, none twice
    assert(queue.at_last());
    return last;
  };
  const std::size_t last_of_first_pass = play_pass(2);

  // The next track starts a new pass, and it is never the one that just finished.
  queue.next();
  assert(queue.current_index() != last_of_first_pass);
  assert(!queue.at_last());
  const std::size_t first_of_second_pass = queue.current_index();
  play_pass(first_of_second_pass);

  // Previous steps back along the shuffled order.
  queue.next();
  const std::size_t a = queue.current_index();
  queue.next();
  const std::size_t b = queue.current_index();
  assert(queue.previous() != nullptr && queue.current_index() == a);
  queue.next();
  assert(queue.current_index() == b);

  // Selecting a track starts a new pass from it.
  assert(queue.select(4));
  assert(queue.current_index() == 4);
  play_pass(4);

  // Shuffle off resumes the normal order from the current track.
  assert(queue.select(1));
  queue.set_shuffle(false);
  queue.next();
  assert(queue.current_index() == 2);
  assert(!queue.at_last());
  assert(queue.select(5));
  assert(queue.at_last());  // the last track of a plain queue

  // A shuffled pass keeps working with a single track and an empty queue.
  threedsmsc::PlaybackQueue single;
  single.set_shuffle(true);
  assert(single.at_last() && single.next() == nullptr);
  single.set_tracks({make_track("only.mp3", "Only", "A")});
  assert(single.next() != nullptr && single.current_index() == 0);
}

void test_queue_file() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "3dsmsc-queue.txt";
  threedsmsc::PlaybackQueue queue;
  queue.set_tracks({make_track("a.mp3", "A", "Artist"), make_track("b.flac", "B", "Artist")});
  assert(queue.next() != nullptr);
  assert(threedsmsc::save_queue_file(path.string(), queue));

  threedsmsc::PlaybackQueue loaded;
  std::string error;
  assert(threedsmsc::load_queue_file(path.string(), loaded, &error));
  assert(error.empty());
  assert(loaded.size() == 2);
  assert(loaded.current_index() == 1);
  assert(loaded.current() != nullptr && loaded.current()->path == "b.flac");
  assert(loaded.tracks()[0].path == "a.mp3");
  assert(loaded.tracks()[1].format == threedsmsc::AudioFormat::Flac);

  std::error_code cleanup_error;
  std::filesystem::remove(path, cleanup_error);
}

}

void run_queue_tests() {
  test_queue();
  test_queue_shuffle();
  test_queue_file();
}
