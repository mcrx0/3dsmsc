#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <tuple>

#include "3dsmsc/library/browse_index.hpp"

namespace threedsmsc {
namespace {

std::string lowercase(const std::string& value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

struct SortKey {
  std::string artist;
  std::string album;
  std::uint16_t disc;
  std::uint16_t track;
  std::string title;
  std::size_t index;
};

// Untagged numbers (0) sort after numbered tracks.
std::uint32_t number_key(std::uint16_t value) {
  return value == 0 ? 0x10000u : value;
}

bool key_less(const SortKey& a, const SortKey& b) {
  return std::make_tuple(std::cref(a.artist), std::cref(a.album), number_key(a.disc),
                         number_key(a.track), std::cref(a.title), a.index) <
         std::make_tuple(std::cref(b.artist), std::cref(b.album), number_key(b.disc),
                         number_key(b.track), std::cref(b.title), b.index);
}

}

BrowseIndex build_browse_index(const LibraryIndex& library) {
  std::vector<SortKey> keys;
  keys.reserve(library.tracks.size());
  for (std::size_t index = 0; index < library.tracks.size(); ++index) {
    const Track& track = library.tracks[index];
    keys.push_back({track.artist.empty() ? "Unknown artist" : track.artist,
                    track.album.empty() ? "Unknown album" : track.album, track.disc_number,
                    track.track_number, track.title, index});
  }
  // Group by lowercase names so "abba" and "ABBA" share one Artist, keeping display names.
  std::vector<SortKey> display = keys;
  for (SortKey& key : keys) {
    key.artist = lowercase(key.artist);
    key.album = lowercase(key.album);
    key.title = lowercase(key.title);
  }
  std::vector<std::size_t> order(keys.size());
  for (std::size_t i = 0; i < order.size(); ++i)
    order[i] = i;
  std::sort(order.begin(), order.end(),
            [&](std::size_t a, std::size_t b) { return key_less(keys[a], keys[b]); });

  BrowseIndex result;
  for (const std::size_t position : order) {
    const SortKey& key = keys[position];
    if (result.artists.empty() || lowercase(result.artists.back().name) != key.artist) {
      result.artists.push_back({display[position].artist, result.albums.size(), 0});
    }
    BrowseArtist& artist = result.artists.back();
    if (artist.album_count == 0 ||
        lowercase(result.albums.back().name) != key.album) {
      result.albums.push_back({display[position].album, result.artists.size() - 1, {}});
      ++artist.album_count;
    }
    result.albums.back().tracks.push_back(key.index);
  }
  return result;
}

}
