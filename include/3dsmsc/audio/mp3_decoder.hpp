#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "3dsmsc/audio/decoder.hpp"
#include "3dsmsc/util/unique_file.hpp"
#include "minimp3.h"

namespace threedsmsc {

class Mp3Decoder final : public AudioDecoder {
 public:
  bool open(const Track& track) override;
  DecodeResult decode(std::int16_t* output, std::size_t output_capacity, std::size_t& written,
                      PcmBlockInfo& info) override;
  void reset() override;
  bool seek(std::uint64_t position_ms) override;

 private:
  // Keeps at least min_input_bytes buffered until the file ends (see decode()).
  void fill_input();
  // Copies one decoded frame to `output`, or reports why it cannot.
  DecodeResult publish_frame(int samples_per_channel, const mp3dec_frame_info_t& frame_info,
                             std::int16_t* output, std::size_t output_capacity,
                             std::size_t& written, PcmBlockInfo& info);

  static constexpr std::size_t min_input_bytes = 16384;
  static constexpr std::uint64_t priming_ms = 300;
  static constexpr std::size_t max_frame_samples = 2304;
  static constexpr std::size_t input_chunk_bytes = 4096;

  mp3dec_t decoder_ = {};
  std::vector<std::uint8_t> pending_input_;
  std::size_t pending_pos_ = 0;  // bytes of pending_input_ already consumed
  std::array<std::uint8_t, input_chunk_bytes> input_chunk_ = {};
  std::array<std::int16_t, max_frame_samples> frame_samples_ = {};
  UniqueFile file_;
  std::string path_;
  bool open_ = false;
  bool end_of_file_ = false;
  bool skip_decode_ = false;
};

}
