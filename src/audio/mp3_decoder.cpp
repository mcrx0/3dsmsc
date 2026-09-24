#include <algorithm>
#include <cstring>

#include "3dsmsc/audio/mp3_decoder.hpp"

namespace threedsmsc {

Mp3Decoder::~Mp3Decoder() {
  if (file_ != nullptr) {
    std::fclose(file_);
  }
}

bool Mp3Decoder::open(const Track& track) {
  if (track.format != AudioFormat::Mp3)
    return false;
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
  mp3dec_init(&decoder_);
  pending_input_.clear();
  open_ = true;
  return true;
}

void Mp3Decoder::reset() {
  mp3dec_init(&decoder_);
  pending_input_.clear();
  if (file_ != nullptr) {
    std::fseek(file_, 0, SEEK_SET);
    std::clearerr(file_);
  }
}

bool Mp3Decoder::seek(std::uint64_t position_ms) {
  if (!open_ || file_ == nullptr)
    return false;
  reset();
  std::array<std::int16_t, max_frame_samples> discard{};
  std::uint64_t elapsed_ms = 0;
  while (elapsed_ms < position_ms) {
    std::size_t written = 0;
    PcmBlockInfo info;
    if (decode(discard.data(), discard.size(), written, info) != DecodeResult::FrameReady ||
        info.channels == 0 || info.sample_rate == 0) {
      return false;
    }
    const std::size_t frames = written / info.channels;
    elapsed_ms += static_cast<std::uint64_t>(frames) * 1000ULL / info.sample_rate;
  }
  return true;
}

DecodeResult Mp3Decoder::decode(std::int16_t* output, std::size_t output_capacity,
                                std::size_t& written, PcmBlockInfo& info) {
  written = 0;
  info = {};
  if (!open_ || file_ == nullptr || output == nullptr || output_capacity < max_frame_samples) {
    return DecodeResult::Error;
  }
  while (true) {
    const std::size_t bytes_read = std::fread(input_chunk_.data(), 1, input_chunk_.size(), file_);
    const bool end_of_stream = std::feof(file_) != 0;
    if (bytes_read > 0) {
      if (pending_input_.size() + bytes_read > max_input_bytes) {
        pending_input_.clear();
        return DecodeResult::Error;
      }
      pending_input_.insert(pending_input_.end(), input_chunk_.begin(),
                            input_chunk_.begin() + bytes_read);
    }
    bool made_progress = false;
    while (!pending_input_.empty()) {
      mp3dec_frame_info_t frame_info = {};
      const int samples = mp3dec_decode_frame(&decoder_, pending_input_.data(),
                                              static_cast<int>(pending_input_.size()),
                                              frame_samples_.data(), &frame_info);
      if (frame_info.frame_bytes > 0) {
        pending_input_.erase(pending_input_.begin(),
                             pending_input_.begin() + frame_info.frame_bytes);
        made_progress = true;
      }
      if (samples <= 0) {
        if (frame_info.frame_bytes == 0) {
          break;
        }
        continue;
      }
      if (static_cast<std::size_t>(samples) > output_capacity) {
        pending_input_.clear();
        return DecodeResult::Error;
      }
      std::memcpy(output, frame_samples_.data(),
                  static_cast<std::size_t>(samples) * sizeof(std::int16_t));
      written = static_cast<std::size_t>(samples);
      info.sample_rate = static_cast<std::uint32_t>(frame_info.hz);
      info.channels = static_cast<std::uint16_t>(frame_info.channels);
      return DecodeResult::FrameReady;
    }
    if (end_of_stream && pending_input_.empty()) {
      return DecodeResult::EndOfStream;
    }
    if (!made_progress && bytes_read == 0) {
      return end_of_stream ? DecodeResult::EndOfStream : DecodeResult::NeedMoreInput;
    }
  }
}

}
