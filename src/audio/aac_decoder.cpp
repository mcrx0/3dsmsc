#include <algorithm>
#include <cstring>

#include "3dsmsc/audio/aac_decoder.hpp"

namespace threedsmsc {
namespace {

constexpr std::size_t raw_buffer_bytes = 65536;
constexpr unsigned aac_object_type_indication = 0x40;

std::uint64_t file_size(FILE* file) {
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  return size < 0 ? 0 : static_cast<std::uint64_t>(size);
}

}

AacDecoder::~AacDecoder() {
  if (faad_ != nullptr)
    NeAACDecClose(faad_);
  if (mp4_open_)
    MP4D_close(&mp4_);
  if (file_ != nullptr)
    std::fclose(file_);
}

bool AacDecoder::open(const Track& track) {
  if (track.format != AudioFormat::Aac && track.format != AudioFormat::M4a &&
      track.format != AudioFormat::Mp4) {
    return false;
  }
  if (faad_ != nullptr) {
    NeAACDecClose(faad_);
    faad_ = nullptr;
  }
  if (mp4_open_) {
    MP4D_close(&mp4_);
    mp4_open_ = false;
  }
  if (file_ != nullptr) {
    std::fclose(file_);
    file_ = nullptr;
  }
  path_ = track.path;
  file_ = std::fopen(path_.c_str(), "rb");
  if (file_ == nullptr)
    return false;
  faad_ = NeAACDecOpen();
  if (faad_ == nullptr) {
    std::fclose(file_);
    file_ = nullptr;
    return false;
  }
  NeAACDecConfiguration* configuration = NeAACDecGetCurrentConfiguration(faad_);
  configuration->outputFormat = FAAD_FMT_16BIT;
  configuration->downMatrix = 0;
  NeAACDecSetConfiguration(faad_, configuration);
  const bool opened = track.format == AudioFormat::Aac ? open_raw() : open_mp4(file_size(file_));
  if (!opened) {
    NeAACDecClose(faad_);
    faad_ = nullptr;
    if (mp4_open_)
      MP4D_close(&mp4_);
    if (file_ != nullptr)
      std::fclose(file_);
    file_ = nullptr;
  }
  return opened;
}

void AacDecoder::reset() {
  sample_index_ = 0;
  raw_pending_.clear();
  access_unit_.clear();
  if (faad_ == nullptr || file_ == nullptr)
    return;
  NeAACDecClose(faad_);
  faad_ = NeAACDecOpen();
  if (faad_ == nullptr)
    return;
  NeAACDecConfiguration* configuration = NeAACDecGetCurrentConfiguration(faad_);
  configuration->outputFormat = FAAD_FMT_16BIT;
  configuration->downMatrix = 0;
  NeAACDecSetConfiguration(faad_, configuration);
  if (mp4_open_) {
    MP4D_close(&mp4_);
    std::fseek(file_, 0, SEEK_SET);
    open_mp4(file_size(file_));
  } else {
    std::fseek(file_, 0, SEEK_SET);
    open_raw();
  }
}

bool AacDecoder::seek(std::uint64_t position_ms) {
  if (!open_ || !mp4_open_ || mp4_duration_ms_ == 0)
    return false;
  const unsigned sample_count = mp4_.track[track_].sample_count;
  if (position_ms >= mp4_duration_ms_) {
    sample_index_ = sample_count;
    return true;
  }
  sample_index_ = static_cast<unsigned>((position_ms * sample_count) / mp4_duration_ms_);
  return true;
}

DecodeResult AacDecoder::decode(std::int16_t* output, std::size_t output_capacity,
                                std::size_t& written, PcmBlockInfo& info) {
  written = 0;
  info = {};
  if (!open_ || output == nullptr || output_capacity == 0)
    return DecodeResult::Error;
  if (mp4_open_)
    return decode_mp4(output, output_capacity, written, info) ? DecodeResult::FrameReady
                                                              : DecodeResult::EndOfStream;
  return decode_raw(output, output_capacity, written, info) ? DecodeResult::FrameReady
                                                            : DecodeResult::EndOfStream;
}

bool AacDecoder::open_mp4(std::uint64_t size) {
  mp4_duration_ms_ = 0;
  sample_index_ = 0;
  if (MP4D_open(&mp4_, mp4_read_callback, file_, static_cast<std::int64_t>(size)) != 1)
    return false;
  mp4_open_ = true;
  if (mp4_.timescale != 0) {
    const std::uint64_t duration_units =
        (static_cast<std::uint64_t>(mp4_.duration_hi) << 32) | mp4_.duration_lo;
    mp4_duration_ms_ = duration_units * 1000ULL / mp4_.timescale;
  }
  for (unsigned candidate = 0; candidate < mp4_.track_count; ++candidate) {
    MP4D_track_t& track = mp4_.track[candidate];
    if (track.handler_type == MP4D_HANDLER_TYPE_SOUN &&
        track.object_type_indication == aac_object_type_indication && track.dsi != nullptr &&
        track.dsi_bytes > 0) {
      track_ = candidate;
      unsigned long sample_rate = 0;
      unsigned char channels = 0;
      if (NeAACDecInit2(faad_, track.dsi, track.dsi_bytes, &sample_rate, &channels) == 0) {
        sample_rate_ = static_cast<std::uint32_t>(sample_rate);
        channels_ = channels;
        open_ = true;
        return true;
      }
    }
  }
  MP4D_close(&mp4_);
  mp4_open_ = false;
  return false;
}

bool AacDecoder::open_raw() {
  raw_pending_.resize(raw_buffer_bytes);
  const std::size_t bytes_read = std::fread(raw_pending_.data(), 1, raw_pending_.size(), file_);
  raw_pending_.resize(bytes_read);
  unsigned long sample_rate = 0;
  unsigned char channels = 0;
  const long init_result = NeAACDecInit(
      faad_, raw_pending_.data(), static_cast<unsigned long>(bytes_read), &sample_rate, &channels);
  if (init_result < 0)
    return false;
  const std::size_t consumed =
      init_result == 0 ? 0 : static_cast<std::size_t>((init_result + 7) / 8);
  if (consumed > raw_pending_.size())
    return false;
  raw_pending_.erase(raw_pending_.begin(), raw_pending_.begin() + consumed);
  sample_rate_ = static_cast<std::uint32_t>(sample_rate);
  channels_ = channels;
  open_ = true;
  return true;
}

bool AacDecoder::decode_mp4(std::int16_t* output, std::size_t output_capacity, std::size_t& written,
                            PcmBlockInfo& info) {
  while (sample_index_ < mp4_.track[track_].sample_count) {
    unsigned frame_bytes = 0;
    unsigned timestamp = 0;
    unsigned duration = 0;
    const MP4D_file_offset_t offset =
        MP4D_frame_offset(&mp4_, track_, sample_index_, &frame_bytes, &timestamp, &duration);
    ++sample_index_;
    if (frame_bytes == 0 || std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0)
      return false;
    access_unit_.resize(frame_bytes);
    if (std::fread(access_unit_.data(), 1, frame_bytes, file_) != frame_bytes)
      return false;
    NeAACDecFrameInfo frame_info = {};
    const void* pcm = NeAACDecDecode(faad_, &frame_info, access_unit_.data(), frame_bytes);
    if (pcm == nullptr || frame_info.error != 0)
      return false;
    if (frame_info.samples == 0)
      continue;
    if (frame_info.channels != 1 && frame_info.channels != 2)
      return false;
    if (frame_info.samples > output_capacity)
      return false;
    std::memcpy(output, pcm, static_cast<std::size_t>(frame_info.samples) * sizeof(std::int16_t));
    written = static_cast<std::size_t>(frame_info.samples);
    info.sample_rate = static_cast<std::uint32_t>(frame_info.samplerate);
    info.channels = frame_info.channels;
    return true;
  }
  return false;
}

bool AacDecoder::decode_raw(std::int16_t* output, std::size_t output_capacity, std::size_t& written,
                            PcmBlockInfo& info) {
  while (true) {
    if (raw_pending_.size() < FAAD_MIN_STREAMSIZE) {
      std::vector<std::uint8_t> additional(raw_buffer_bytes);
      const std::size_t bytes_read = std::fread(additional.data(), 1, additional.size(), file_);
      additional.resize(bytes_read);
      raw_pending_.insert(raw_pending_.end(), additional.begin(), additional.end());
      if (bytes_read == 0 && raw_pending_.empty())
        return false;
    }
    NeAACDecFrameInfo frame_info = {};
    const void* pcm = NeAACDecDecode(faad_, &frame_info, raw_pending_.data(),
                                     static_cast<unsigned long>(raw_pending_.size()));
    if (frame_info.bytesconsumed > 0 && frame_info.bytesconsumed <= raw_pending_.size()) {
      raw_pending_.erase(raw_pending_.begin(), raw_pending_.begin() + frame_info.bytesconsumed);
    }
    if (pcm != nullptr && frame_info.error == 0 && frame_info.samples > 0) {
      if (frame_info.channels != 1 && frame_info.channels != 2)
        return false;
      if (frame_info.samples > output_capacity)
        return false;
      std::memcpy(output, pcm, static_cast<std::size_t>(frame_info.samples) * sizeof(std::int16_t));
      written = static_cast<std::size_t>(frame_info.samples);
      info.sample_rate = static_cast<std::uint32_t>(frame_info.samplerate);
      info.channels = frame_info.channels;
      return true;
    }
    if (raw_pending_.empty())
      return false;
    // Nothing decoded and nothing consumed: drop a byte to resynchronise, or a corrupt stream
    // would spin here forever.
    if (frame_info.bytesconsumed == 0)
      raw_pending_.erase(raw_pending_.begin());
  }
}

int AacDecoder::mp4_read_callback(std::int64_t offset, void* buffer, std::size_t size,
                                  void* token) {
  FILE* file = static_cast<FILE*>(token);
  if (offset < 0 || std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0)
    return 1;
  return std::fread(buffer, 1, size, file) == size ? 0 : 1;
}

}
