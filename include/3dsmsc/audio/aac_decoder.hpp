#pragma once

#include <minimp4.h>
#include <neaacdec.h>

#include <cstdio>
#include <string>
#include <vector>

#include "3dsmsc/audio/decoder.hpp"
#include "3dsmsc/util/unique_file.hpp"

namespace threedsmsc {

class AacDecoder final : public AudioDecoder {
 public:
  ~AacDecoder() override;
  bool open(const Track& track) override;
  DecodeResult decode(std::int16_t* output, std::size_t output_capacity, std::size_t& written,
                      PcmBlockInfo& info) override;
  void reset() override;
  bool can_seek() const override { return mp4_open_ && mp4_duration_ms_ != 0; }
  bool seek(std::uint64_t position_ms) override;

 private:
  static int mp4_read_callback(std::int64_t offset, void* buffer, std::size_t size, void* token);
  // Opens a FAAD decoder configured for 16-bit output; false if FAAD cannot start.
  bool open_faad();
  bool open_mp4(std::uint64_t file_size);
  bool open_raw();
  bool decode_mp4(std::int16_t* output, std::size_t output_capacity, std::size_t& written,
                  PcmBlockInfo& info);
  bool decode_raw(std::int16_t* output, std::size_t output_capacity, std::size_t& written,
                  PcmBlockInfo& info);

  UniqueFile file_;
  std::string path_;
  NeAACDecHandle faad_ = nullptr;
  MP4D_demux_t mp4_ = {};
  bool mp4_open_ = false;
  unsigned track_ = 0;
  unsigned sample_index_ = 0;
  std::vector<std::uint8_t> raw_pending_;
  std::vector<std::uint8_t> access_unit_;
  std::uint64_t mp4_duration_ms_ = 0;
  std::uint32_t sample_rate_ = 0;
  std::uint16_t channels_ = 0;
  bool open_ = false;
};

}
