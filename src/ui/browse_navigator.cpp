#include <string>

#include "3dsmsc/ui/browse_navigator.hpp"

namespace threedsmsc {

std::size_t list_window_start(std::size_t total, std::size_t selected, std::size_t visible) {
  if (visible == 0 || total <= visible)
    return 0;
  const std::size_t half = visible / 2;
  const std::size_t start = selected > half ? selected - half : 0;
  const std::size_t last_start = total - visible;
  return start > last_start ? last_start : start;
}

void BrowseNavigator::reset(const LibraryIndex* library, const BrowseIndex* index) {
  library_ = library;
  index_ = index;
  level_ = BrowseLevel::Artists;
  artist_ = 0;
  album_ = 0;
  selected_[0] = selected_[1] = selected_[2] = 0;
}

BrowseLevel BrowseNavigator::level() const {
  return level_;
}

std::size_t BrowseNavigator::count() const {
  if (index_ == nullptr)
    return 0;
  switch (level_) {
    case BrowseLevel::Artists:
      return index_->artists.size();
    case BrowseLevel::Albums:
      return index_->artists[artist_].album_count;
    case BrowseLevel::Tracks:
      return index_->albums[album_].tracks.size();
  }
  return 0;
}

std::size_t BrowseNavigator::selected() const {
  return selected_[static_cast<std::size_t>(level_)];
}

std::string BrowseNavigator::heading() const {
  if (index_ == nullptr)
    return "Artists";
  switch (level_) {
    case BrowseLevel::Artists:
      return "Artists";
    case BrowseLevel::Albums:
      return index_->artists[artist_].name;
    case BrowseLevel::Tracks:
      return index_->albums[album_].name;
  }
  return {};
}

std::string BrowseNavigator::label(std::size_t row) const {
  if (row >= count())
    return {};
  switch (level_) {
    case BrowseLevel::Artists:
      return index_->artists[row].name;
    case BrowseLevel::Albums:
      return index_->albums[index_->artists[artist_].first_album + row].name;
    case BrowseLevel::Tracks: {
      const Track& track = library_->tracks[index_->albums[album_].tracks[row]];
      const std::string title = track.title.empty() ? track.path : track.title;
      if (track.track_number == 0)
        return title;
      std::string number = std::to_string(track.track_number);
      if (number.size() < 2)
        number.insert(0, "0");
      return number + ". " + title;
    }
  }
  return {};
}

void BrowseNavigator::move(int delta) {
  const std::size_t total = count();
  if (total == 0)
    return;
  const long long position = static_cast<long long>(selected()) + delta;
  const long long size = static_cast<long long>(total);
  selected_[static_cast<std::size_t>(level_)] =
      static_cast<std::size_t>(((position % size) + size) % size);
}

void BrowseNavigator::select(std::size_t row) {
  if (row < count())
    selected_[static_cast<std::size_t>(level_)] = row;
}

bool BrowseNavigator::enter(BrowseSelection& playback) {
  if (count() == 0)
    return false;
  switch (level_) {
    case BrowseLevel::Artists:
      artist_ = selected();
      level_ = BrowseLevel::Albums;
      selected_[1] = 0;
      return false;
    case BrowseLevel::Albums:
      album_ = index_->artists[artist_].first_album + selected();
      level_ = BrowseLevel::Tracks;
      selected_[2] = 0;
      return false;
    case BrowseLevel::Tracks:
      playback.tracks = index_->albums[album_].tracks;
      playback.start = selected();
      return true;
  }
  return false;
}

bool BrowseNavigator::back() {
  if (level_ == BrowseLevel::Artists)
    return false;
  level_ = level_ == BrowseLevel::Tracks ? BrowseLevel::Albums : BrowseLevel::Artists;
  return true;
}

}
