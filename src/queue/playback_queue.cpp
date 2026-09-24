#include <cstddef>
#include <utility>
#include <vector>

#include "3dsmsc/queue/playback_queue.hpp"

namespace threedsmsc {

void PlaybackQueue::set_tracks(std::vector<Track> tracks) {
  tracks_ = std::move(tracks);
  current_index_ = tracks_.empty() ? 0 : current_index_ % tracks_.size();
}

void PlaybackQueue::enqueue(Track track) {
  tracks_.push_back(std::move(track));
  if (tracks_.size() == 1)
    current_index_ = 0;
}

void PlaybackQueue::clear() {
  tracks_.clear();
  current_index_ = 0;
}

bool PlaybackQueue::empty() const {
  return tracks_.empty();
}

std::size_t PlaybackQueue::size() const {
  return tracks_.size();
}

std::size_t PlaybackQueue::current_index() const {
  return current_index_;
}

const Track* PlaybackQueue::current() const {
  if (tracks_.empty())
    return nullptr;
  return &tracks_[current_index_];
}

bool PlaybackQueue::select(std::size_t index) {
  if (index >= tracks_.size())
    return false;
  current_index_ = index;
  return true;
}

const Track* PlaybackQueue::next() {
  if (tracks_.empty())
    return nullptr;
  current_index_ = (current_index_ + 1) % tracks_.size();
  return current();
}

const Track* PlaybackQueue::previous() {
  if (tracks_.empty())
    return nullptr;
  current_index_ = (current_index_ + tracks_.size() - 1) % tracks_.size();
  return current();
}

const std::vector<Track>& PlaybackQueue::tracks() const {
  return tracks_;
}

}
