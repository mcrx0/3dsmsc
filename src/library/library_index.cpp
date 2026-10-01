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

// `query` must already be normalised (lower-cased when the search ignores case), so each
// character of `value` is converted once instead of both sides on every comparison.
bool contains(std::string_view value, std::string_view query, bool case_sensitive) {
  if (query.empty())
    return true;
  if (value.size() < query.size())
    return false;
  const char first = query[0];
  const std::size_t last_start = value.size() - query.size();
  for (std::size_t offset = 0; offset <= last_start; ++offset) {
    if (normalized_char(value[offset], case_sensitive) != first)
      continue;  // most positions fail on the first character
    std::size_t index = 1;
    while (index < query.size() &&
           normalized_char(value[offset + index], case_sensitive) == query[index])
      ++index;
    if (index == query.size())
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
  std::string normalized(query);
  if (!case_sensitive) {
    for (char& character : normalized)
      character = normalized_char(character, false);
  }
  std::vector<std::size_t> result;
  for (std::size_t index = 0; index < library.tracks.size(); ++index) {
    if (matches(library.tracks[index], normalized, case_sensitive)) {
      result.push_back(index);
    }
  }
  return result;
}

std::string empty_library_message() {
  return "Go to Settings > Full scan again";
}

}
