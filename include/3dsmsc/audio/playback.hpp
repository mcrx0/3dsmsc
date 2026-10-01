#pragma once

#include <cstdint>
#include <string>

namespace threedsmsc {

enum class PlaybackState {
  Stopped,
  Playing,
  Paused,
  Error,
};

struct PlaybackSnapshot {
  PlaybackState state = PlaybackState::Stopped;
  std::uint64_t position_ms = 0;
  std::uint32_t sample_rate = 0;
  std::uint16_t channels = 0;
};

}
