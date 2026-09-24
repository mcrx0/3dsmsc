#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "3dsmsc/audio/decoder.hpp"
#include "minimp3.h"

namespace threedsmsc {

class Mp3Decoder final : public AudioDecoder {
 public:
  ~Mp3Decoder() override;
  bool open(const Track& track) override;
  DecodeResult decode(std::int16_t* output, std::size_t output_capacity, std::size_t& written,
                      PcmBlockInfo& info) override;
  void reset() override;
  bool seek(std::uint64_t position_ms) override;

 private:
  static constexpr std::size_t max_input_bytes = 32768;
  static constexpr std::size_t max_frame_samples = 2304;
  static constexpr std::size_t input_chunk_bytes = 4096;

  mp3dec_t decoder_ = {};
  std::vector<std::uint8_t> pending_input_;
  std::array<std::uint8_t, input_chunk_bytes> input_chunk_ = {};
  std::array<std::int16_t, max_frame_samples> frame_samples_ = {};
  std::FILE* file_ = nullptr;
  std::string path_;
  bool open_ = false;
};

}
