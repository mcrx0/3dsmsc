#include <algorithm>
#include <cstring>

#include "3dsmsc/audio/mp3_decoder.hpp"

namespace threedsmsc {

bool Mp3Decoder::open(const Track& track) {
  if (track.format != AudioFormat::Mp3)
    return false;
  path_ = track.path;
  file_ = open_file(path_, "rb");  // replacing the handle closes any previous file
  open_ = file_ != nullptr;
  if (!open_)
    return false;
  mp3dec_init(&decoder_);
  pending_input_.clear();
  pending_pos_ = 0;
  end_of_file_ = false;
  return true;
}

void Mp3Decoder::reset() {
  mp3dec_init(&decoder_);
  pending_input_.clear();
  pending_pos_ = 0;
  end_of_file_ = false;
  if (file_ != nullptr) {
    std::fseek(file_.get(), 0, SEEK_SET);
    std::clearerr(file_.get());
  }
}

bool Mp3Decoder::seek(std::uint64_t position_ms) {
  if (!open_ || file_ == nullptr)
    return false;
  reset();
  std::array<std::int16_t, max_frame_samples> discard{};
  // Skip most of the way without decoding (minimp3 only reads frame headers when given no
  // output buffer), then decode the last stretch so the bit reservoir is primed. Decoding
  // every frame from the start blocked the UI for seconds on long tracks.
  const std::uint64_t decode_from_ms = position_ms > priming_ms ? position_ms - priming_ms : 0;
  std::uint64_t elapsed_ms = 0;
  bool ok = true;
  while (elapsed_ms < position_ms) {
    skip_decode_ = elapsed_ms < decode_from_ms;
    std::size_t written = 0;
    PcmBlockInfo info;
    if (decode(discard.data(), discard.size(), written, info) != DecodeResult::FrameReady ||
        info.channels == 0 || info.sample_rate == 0) {
      ok = false;
      break;
    }
    const std::size_t frames = written / info.channels;
    elapsed_ms += static_cast<std::uint64_t>(frames) * 1000ULL / info.sample_rate;
  }
  skip_decode_ = false;
  return ok;
}

void Mp3Decoder::fill_input() {
  // minimp3 discards a trailing partial frame as junk, so keep at least min_input_bytes
  // buffered (its recommended window) until the file ends. Consumed bytes are skipped by offset
  // and only dropped when more input is read, rather than erased frame by frame.
  if (end_of_file_ || pending_input_.size() - pending_pos_ >= min_input_bytes)
    return;
  pending_input_.erase(pending_input_.begin(),
                       pending_input_.begin() + static_cast<std::ptrdiff_t>(pending_pos_));
  pending_pos_ = 0;
  while (!end_of_file_ && pending_input_.size() < min_input_bytes) {
    const std::size_t bytes_read =
        std::fread(input_chunk_.data(), 1, input_chunk_.size(), file_.get());
    end_of_file_ = bytes_read < input_chunk_.size();
    pending_input_.insert(pending_input_.end(), input_chunk_.begin(),
                          input_chunk_.begin() + static_cast<std::ptrdiff_t>(bytes_read));
  }
}

DecodeResult Mp3Decoder::publish_frame(int samples_per_channel,
                                       const mp3dec_frame_info_t& frame_info, std::int16_t* output,
                                       std::size_t output_capacity, std::size_t& written,
                                       PcmBlockInfo& info) {
  // minimp3 reports samples per channel; the output is interleaved across all channels.
  const auto total_samples =
      static_cast<std::size_t>(samples_per_channel) * static_cast<std::size_t>(frame_info.channels);
  if (total_samples > output_capacity || total_samples > frame_samples_.size()) {
    pending_input_.clear();
    pending_pos_ = 0;
    return DecodeResult::Error;
  }
  if (!skip_decode_)
    std::memcpy(output, frame_samples_.data(), total_samples * sizeof(std::int16_t));
  written = total_samples;
  info.sample_rate = static_cast<std::uint32_t>(frame_info.hz);
  info.channels = static_cast<std::uint16_t>(frame_info.channels);
  return DecodeResult::FrameReady;
}

DecodeResult Mp3Decoder::decode(std::int16_t* output, std::size_t output_capacity,
                                std::size_t& written, PcmBlockInfo& info) {
  written = 0;
  info = {};
  if (!open_ || file_ == nullptr || output == nullptr || output_capacity < max_frame_samples)
    return DecodeResult::Error;
  while (true) {
    fill_input();
    const std::size_t available = pending_input_.size() - pending_pos_;
    if (available == 0)
      return DecodeResult::EndOfStream;
    mp3dec_frame_info_t frame_info = {};
    const int samples = mp3dec_decode_frame(
        &decoder_, pending_input_.data() + pending_pos_, static_cast<int>(available),
        skip_decode_ ? nullptr : frame_samples_.data(), &frame_info);
    pending_pos_ += static_cast<std::size_t>(frame_info.frame_bytes);
    if (samples > 0)
      return publish_frame(samples, frame_info, output, output_capacity, written, info);
    if (frame_info.frame_bytes == 0) {
      // No frame in a full window means the data is not decodable audio.
      return end_of_file_ ? DecodeResult::EndOfStream : DecodeResult::Error;
    }
    // Skipped a tag or junk; keep looking.
  }
}

}
