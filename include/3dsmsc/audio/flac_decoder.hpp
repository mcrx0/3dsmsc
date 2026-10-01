#pragma once

#define DR_FLAC_NO_STDIO
#include "dr_flac.h"

#include <cstdio>
#include <string>

#include "3dsmsc/audio/decoder.hpp"
#include "3dsmsc/util/unique_file.hpp"

namespace threedsmsc {

class FlacDecoder final : public AudioDecoder {
 public:
  ~FlacDecoder() override;
  bool open(const Track& track) override;
  DecodeResult decode(std::int16_t* output, std::size_t output_capacity, std::size_t& written,
                      PcmBlockInfo& info) override;
  void reset() override;
  bool seek(std::uint64_t position_ms) override;

 private:
  static std::size_t read_callback(void* data, void* buffer, std::size_t bytes);
  static drflac_bool32 seek_callback(void* data, int offset, drflac_seek_origin origin);
  static drflac_bool32 tell_callback(void* data, drflac_int64* cursor);

  UniqueFile file_;  // declared before decoder_: the file must outlive the decoder reading it
  std::string path_;
  drflac* decoder_ = nullptr;
  bool open_ = false;
};

}
