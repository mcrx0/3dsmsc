#include <cstdio>

#include "3dsmsc/audio/flac_decoder.hpp"

namespace threedsmsc {

FlacDecoder::~FlacDecoder() {
  if (decoder_ != nullptr)
    drflac_close(decoder_);
  if (file_ != nullptr)
    std::fclose(file_);
}

bool FlacDecoder::open(const Track& track) {
  if (track.format != AudioFormat::Flac)
    return false;
  if (decoder_ != nullptr) {
    drflac_close(decoder_);
    decoder_ = nullptr;
  }
  if (file_ != nullptr) {
    std::fclose(file_);
    file_ = nullptr;
  }
  path_ = track.path;
  file_ = std::fopen(path_.c_str(), "rb");
  if (file_ == nullptr) {
    open_ = false;
    return false;
  }
  decoder_ = drflac_open(read_callback, seek_callback, tell_callback, file_, nullptr);
  open_ = decoder_ != nullptr;
  return open_;
}

void FlacDecoder::reset() {
  if (file_ == nullptr)
    return;
  if (decoder_ != nullptr) {
    drflac_close(decoder_);
    decoder_ = nullptr;
  }
  std::fseek(file_, 0, SEEK_SET);
  std::clearerr(file_);
  decoder_ = drflac_open(read_callback, seek_callback, tell_callback, file_, nullptr);
  open_ = decoder_ != nullptr;
}

bool FlacDecoder::seek(std::uint64_t position_ms) {
  if (!open_ || decoder_ == nullptr)
    return false;
  const drflac_uint64 target_frame =
      static_cast<drflac_uint64>(position_ms) * decoder_->sampleRate / 1000ULL;
  return drflac_seek_to_pcm_frame(decoder_, target_frame) == DRFLAC_TRUE;
}

DecodeResult FlacDecoder::decode(std::int16_t* output, std::size_t output_capacity,
                                 std::size_t& written, PcmBlockInfo& info) {
  written = 0;
  info = {};
  if (!open_ || decoder_ == nullptr || output == nullptr)
    return DecodeResult::Error;
  if (decoder_->channels != 1 && decoder_->channels != 2)
    return DecodeResult::Error;
  const std::size_t frames = output_capacity / decoder_->channels;
  const drflac_uint64 decoded_frames = drflac_read_pcm_frames_s16(decoder_, frames, output);
  if (decoded_frames == 0)
    return DecodeResult::EndOfStream;
  written = static_cast<std::size_t>(decoded_frames * decoder_->channels);
  info.sample_rate = static_cast<std::uint32_t>(decoder_->sampleRate);
  info.channels = static_cast<std::uint16_t>(decoder_->channels);
  return DecodeResult::FrameReady;
}

std::size_t FlacDecoder::read_callback(void* data, void* buffer, std::size_t bytes) {
  return std::fread(buffer, 1, bytes, static_cast<std::FILE*>(data));
}

drflac_bool32 FlacDecoder::seek_callback(void* data, int offset, drflac_seek_origin origin) {
  int whence = SEEK_SET;
  if (origin == DRFLAC_SEEK_CUR)
    whence = SEEK_CUR;
  if (origin == DRFLAC_SEEK_END)
    whence = SEEK_END;
  return std::fseek(static_cast<std::FILE*>(data), offset, whence) == 0 ? DRFLAC_TRUE
                                                                        : DRFLAC_FALSE;
}

drflac_bool32 FlacDecoder::tell_callback(void* data, drflac_int64* cursor) {
  const long position = std::ftell(static_cast<std::FILE*>(data));
  if (position < 0)
    return DRFLAC_FALSE;
  *cursor = position;
  return DRFLAC_TRUE;
}

}
