#pragma once

#include <cstddef>
#include <vector>

#include "3dsmsc/library/track.hpp"

namespace threedsmsc {

class PlaybackQueue {
 public:
  void set_tracks(std::vector<Track> tracks);
  void enqueue(Track track);
  void clear();
  bool empty() const;
  std::size_t size() const;
  std::size_t current_index() const;
  const Track* current() const;
  bool select(std::size_t index);
  const Track* next();
  const Track* previous();
  const std::vector<Track>& tracks() const;

 private:
  std::vector<Track> tracks_;
  std::size_t current_index_ = 0;
};

}
