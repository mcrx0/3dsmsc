#pragma once

#include <cstddef>
#include <cstdint>

#include "3dsmsc/library/track.hpp"

namespace threedsmsc {

enum class DecodeResult {
  NeedMoreInput,
  FrameReady,
  EndOfStream,
  Error,
};

struct PcmBlockInfo {
  std::uint32_t sample_rate;
  std::uint16_t channels;
};

class AudioDecoder {
 public:
  virtual ~AudioDecoder() = default;
  virtual bool open(const Track& track) = 0;
  virtual DecodeResult decode(std::int16_t* output, std::size_t output_capacity,
                              std::size_t& written, PcmBlockInfo& info) = 0;
  virtual void reset() = 0;
  virtual bool seek(std::uint64_t position_ms) {
    (void)position_ms;
    return false;
  }
};

}
