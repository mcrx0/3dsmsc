#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "3dsmsc/library/library_index.hpp"

namespace threedsmsc {

struct BrowseAlbum {
  std::string name;
  std::size_t artist;
  std::vector<std::size_t> tracks;
};

struct BrowseArtist {
  std::string name;
  std::size_t first_album;
  std::size_t album_count;
};

// Artist -> Album -> Track view over a scanned library. Track entries are indexes into
// LibraryIndex::tracks, ordered by disc number, track number, then title.
struct BrowseIndex {
  std::vector<BrowseArtist> artists;
  std::vector<BrowseAlbum> albums;
};

BrowseIndex build_browse_index(const LibraryIndex& library);

}
