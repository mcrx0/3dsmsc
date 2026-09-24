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
  PlaybackState state;
  std::uint64_t position_ms;
  std::uint32_t sample_rate;
  std::uint16_t channels;
};

}
