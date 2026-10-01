#include <cstddef>
#include <utility>
#include <vector>

#include "3dsmsc/queue/playback_queue.hpp"

namespace threedsmsc {

void PlaybackQueue::set_tracks(std::vector<Track> tracks) {
  tracks_ = std::move(tracks);
  current_index_ = tracks_.empty() ? 0 : current_index_ % tracks_.size();
  if (shuffle_)
    rebuild_order();
}

void PlaybackQueue::clear() {
  tracks_.clear();
  current_index_ = 0;
  order_.clear();
  position_ = 0;
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

Track* PlaybackQueue::mutable_current() {
  if (tracks_.empty())
    return nullptr;
  return &tracks_[current_index_];
}

bool PlaybackQueue::select(std::size_t index) {
  if (index >= tracks_.size())
    return false;
  current_index_ = index;
  if (shuffle_)
    rebuild_order();
  return true;
}

const Track* PlaybackQueue::next() {
  if (tracks_.empty())
    return nullptr;
  if (!shuffle_) {
    current_index_ = (current_index_ + 1) % tracks_.size();
    return current();
  }
  if (position_ + 1 >= order_.size()) {
    // The pass is over: shuffle again, but never start it with the track that just played.
    // rebuild_order() puts the current track first, so move it to a random later slot.
    rebuild_order();
    if (order_.size() > 1)
      std::swap(order_[0], order_[1 + next_random() % (order_.size() - 1)]);
    position_ = 0;
  } else {
    ++position_;
  }
  current_index_ = order_[position_];
  return current();
}

const Track* PlaybackQueue::previous() {
  if (tracks_.empty())
    return nullptr;
  if (!shuffle_) {
    current_index_ = (current_index_ + tracks_.size() - 1) % tracks_.size();
    return current();
  }
  position_ = position_ > 0 ? position_ - 1 : order_.size() - 1;
  current_index_ = order_[position_];
  return current();
}

void PlaybackQueue::set_shuffle(bool enabled) {
  shuffle_ = enabled;
  if (enabled)
    rebuild_order();
  else
    order_.clear();
}

bool PlaybackQueue::at_last() const {
  if (tracks_.empty())
    return true;
  return shuffle_ ? position_ + 1 >= order_.size() : current_index_ + 1 >= tracks_.size();
}

std::uint32_t PlaybackQueue::next_random() {
  // xorshift32: small, fast, and deterministic once seeded (the tests rely on that).
  random_state_ ^= random_state_ << 13;
  random_state_ ^= random_state_ >> 17;
  random_state_ ^= random_state_ << 5;
  return random_state_;
}

void PlaybackQueue::rebuild_order() {
  order_.clear();
  position_ = 0;
  if (tracks_.empty())
    return;
  std::vector<std::size_t> rest;
  rest.reserve(tracks_.size());
  for (std::size_t index = 0; index < tracks_.size(); ++index) {
    if (index != current_index_)
      rest.push_back(index);
  }
  for (std::size_t index = rest.size(); index > 1; --index)  // Fisher-Yates
    std::swap(rest[index - 1], rest[next_random() % index]);
  order_.push_back(current_index_);
  order_.insert(order_.end(), rest.begin(), rest.end());
}

const std::vector<Track>& PlaybackQueue::tracks() const {
  return tracks_;
}

}
