#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "3dsmsc/library/browse_index.hpp"
#include "3dsmsc/library/library_index.hpp"

namespace threedsmsc {

enum class BrowseLevel {
  Artists,
  Albums,
  Tracks,
};

// Tracks chosen for playback: indexes into LibraryIndex::tracks and the one to start with.
struct BrowseSelection {
  std::vector<std::size_t> tracks;
  std::size_t start = 0;
};

// First visible row of a scrolling list that keeps `selected` roughly centred.
std::size_t list_window_start(std::size_t total, std::size_t selected, std::size_t visible);

// Drill-down cursor over a BrowseIndex: Artists -> Albums -> Tracks. The navigator holds
// no audio or queue state; entering a track returns the selection to play.
class BrowseNavigator {
 public:
  void reset(const LibraryIndex* library, const BrowseIndex* index);

  BrowseLevel level() const;
  std::size_t count() const;
  std::size_t selected() const;
  std::string heading() const;
  std::string label(std::size_t row) const;

  // Moves the cursor, wrapping around the ends of the list.
  void move(int delta);
  void select(std::size_t row);
  // Descends one level, or returns true with the tracks to play at the Tracks level.
  bool enter(BrowseSelection& playback);
  // Ascends one level; false when already at the top.
  bool back();

 private:
  const LibraryIndex* library_ = nullptr;
  const BrowseIndex* index_ = nullptr;
  BrowseLevel level_ = BrowseLevel::Artists;
  std::size_t artist_ = 0;
  std::size_t album_ = 0;
  std::size_t selected_[3] = {0, 0, 0};
};

}
