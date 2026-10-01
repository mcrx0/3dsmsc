#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "3dsmsc/library/track.hpp"

namespace threedsmsc {

class PlaybackQueue {
 public:
  void set_tracks(std::vector<Track> tracks);
  void clear();
  bool empty() const;
  std::size_t size() const;
  std::size_t current_index() const;
  const Track* current() const;
  Track* mutable_current();
  bool select(std::size_t index);
  const Track* next();
  const Track* previous();
  const std::vector<Track>& tracks() const;

  // Shuffle plays every track once in random order (a "pass"), then reshuffles. Turning it on
  // or off keeps the current track playing; selecting a track starts a new pass from it.
  void set_shuffle(bool enabled);
  bool shuffle() const { return shuffle_; }
  void seed(std::uint32_t value) { random_state_ = value == 0 ? 1u : value; }
  // True on the last track of the queue (or of the current shuffled pass).
  bool at_last() const;

 private:
  void rebuild_order();  // current track first, the others in random order
  std::uint32_t next_random();

  std::vector<Track> tracks_;
  std::size_t current_index_ = 0;
  bool shuffle_ = false;
  std::vector<std::size_t> order_;  // play order while shuffling; indexes into tracks_
  std::size_t position_ = 0;        // where current_index_ sits in order_
  std::uint32_t random_state_ = 0x2545F491u;
};

}
