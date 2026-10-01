#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "3dsmsc/library/track.hpp"

namespace threedsmsc {

enum class ScanState : std::uint8_t {
  NotScanned,
  Ready,
  Failed,
  Cancelled,
};

struct LibraryIndex {
  std::string root;
  std::vector<Track> tracks;
  ScanState state = ScanState::NotScanned;
  std::string message;
};

std::vector<std::size_t> find_tracks(const LibraryIndex& library, std::string_view query,
                                     bool case_sensitive);
std::string empty_library_message();

}
