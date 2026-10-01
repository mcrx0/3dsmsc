#pragma once

// Shared fakes and helpers for the unit tests.

#include <map>
#include <string>
#include <vector>

#include "3dsmsc/library/filesystem.hpp"
#include "3dsmsc/library/track.hpp"

#ifdef NDEBUG
#error "The unit tests rely on assert(); build them without NDEBUG"
#endif

namespace test {

class FakeFileSystem final : public threedsmsc::FileSystem {
 public:
  bool list_directory(const std::string& path, std::vector<threedsmsc::DirectoryEntry>& entries,
                      std::string& error) override {
    ++list_calls;
    const auto iterator = directories.find(path);
    if (iterator == directories.end()) {
      error = "missing directory";
      return false;
    }
    entries = iterator->second;
    error.clear();
    return true;
  }

  std::map<std::string, std::vector<threedsmsc::DirectoryEntry>> directories;
  int list_calls = 0;
};

inline threedsmsc::DirectoryEntry entry(const char* name, bool is_directory) {
  return {name, is_directory};
}

inline threedsmsc::Track make_track(const char* path, const char* title, const char* artist) {
  threedsmsc::Track track;
  track.path = path;
  track.title = title;
  track.artist = artist;
  track.format = threedsmsc::format_from_path(path);
  return track;
}

}
