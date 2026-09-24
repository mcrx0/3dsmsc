#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "3dsmsc/library/library_index.hpp"

namespace threedsmsc {
namespace {

char normalized_char(char value, bool case_sensitive) {
  if (case_sensitive)
    return value;
  return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

bool contains(std::string_view value, std::string_view query, bool case_sensitive) {
  if (query.empty())
    return true;
  if (value.size() < query.size())
    return false;
  for (std::size_t offset = 0; offset <= value.size() - query.size(); ++offset) {
    bool matches = true;
    for (std::size_t index = 0; index < query.size(); ++index) {
      if (normalized_char(value[offset + index], case_sensitive) !=
          normalized_char(query[index], case_sensitive)) {
        matches = false;
        break;
      }
    }
    if (matches)
      return true;
  }
  return false;
}

bool matches(const Track& track, std::string_view query, bool case_sensitive) {
  return contains(track.title, query, case_sensitive) ||
         contains(track.artist, query, case_sensitive) ||
         contains(track.album, query, case_sensitive) ||
         contains(track.path, query, case_sensitive);
}

}

std::vector<std::size_t> find_tracks(const LibraryIndex& library, std::string_view query,
                                     bool case_sensitive) {
  std::vector<std::size_t> result;
  result.reserve(library.tracks.size());
  for (std::size_t index = 0; index < library.tracks.size(); ++index) {
    if (matches(library.tracks[index], query, case_sensitive)) {
      result.push_back(index);
    }
  }
  return result;
}

std::vector<std::size_t> find_albums(const LibraryIndex& library) {
  std::vector<std::size_t> result;
  for (std::size_t index = 0; index < library.tracks.size(); ++index) {
    bool seen = false;
    for (std::size_t existing : result) {
      if (library.tracks[existing].album == library.tracks[index].album) {
        seen = true;
        break;
      }
    }
    if (!seen)
      result.push_back(index);
  }
  return result;
}

std::string empty_library_message() {
  return "Go to Settings > Library > Full scan again";
}

}
