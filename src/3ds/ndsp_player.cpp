#include <algorithm>
#include <cstring>
#include <utility>

#include "ndsp_player.hpp"

namespace threedsmsc {
namespace {

constexpr u32 audio_channel = 0;
constexpr u32 audio_format = NDSP_FORMAT_STEREO_PCM16;

std::unique_ptr<AudioDecoder> make_decoder(AudioFormat format) {
  if (format == AudioFormat::Mp3)
    return std::unique_ptr<AudioDecoder>(new Mp3Decoder());
  if (format == AudioFormat::Flac)
    return std::unique_ptr<AudioDecoder>(new FlacDecoder());
  if (format == AudioFormat::Aac || format == AudioFormat::M4a || format == AudioFormat::Mp4)
    return std::unique_ptr<AudioDecoder>(new AacDecoder());
  return nullptr;
}

}

bool NdspAudioPlayer::init() {
  if (initialized_)
    return true;
  if (ndspInit() != 0)
    return false;
  ndspSetOutputMode(NDSP_OUTPUT_STEREO);
  ndspChnSetInterp(audio_channel, NDSP_INTERP_LINEAR);
  ndspChnSetRate(audio_channel, 44100.0f);
  ndspChnSetFormat(audio_channel, audio_format);
  initialized_ = true;
  apply_volume();
  for (std::size_t slot = 0; slot < 2; ++slot) {
    storage_[slot] = static_cast<std::int16_t*>(linearAlloc(buffer_samples * sizeof(std::int16_t)));
    if (storage_[slot] == nullptr) {
      shutdown();
      return false;
    }
    std::memset(&buffers_[slot], 0, sizeof(buffers_[slot]));
    buffers_[slot].data_vaddr = storage_[slot];
    buffers_[slot].nsamples = buffer_frames;
    buffers_[slot].status = NDSP_WBUF_DONE;
  }
  return true;
}

void NdspAudioPlayer::shutdown() {
  stop();
  for (auto& storage : storage_) {
    if (storage != nullptr) {
      linearFree(storage);
      storage = nullptr;
    }
  }
  if (initialized_) {
    ndspExit();
    initialized_ = false;
  }
}

bool NdspAudioPlayer::load(const Track& track) {
  if (!initialized_)
    return false;
  stop();
  decoder_ = make_decoder(track.format);
  if (decoder_ == nullptr || !decoder_->open(track)) {
    decoder_.reset();
    state_ = PlaybackState::Error;
    return false;
  }
  state_ = PlaybackState::Stopped;
  frames_played_ = 0;
  position_offset_ms_ = 0;
  sample_rate_ = 0;
  channels_ = 0;
  loaded_track_ = track;
  decoder_positioned_ = false;
  return true;
}

void NdspAudioPlayer::play() {
  if (!initialized_ || decoder_ == nullptr || state_ == PlaybackState::Error)
    return;
  if (state_ == PlaybackState::Stopped) {
    update();
    if (!decoder_positioned_)
      decoder_->reset();
    decoder_positioned_ = false;
    frames_played_ = 0;
    for (auto& buffer : buffers_) {
      buffer.status = NDSP_WBUF_DONE;
    }
    stop_requested_ = false;
    running_ = true;
    thread_ = threadCreate(worker_entry, this, 32 * 1024, 0x30, -2, false);
    if (thread_ == nullptr) {
      running_ = false;
      state_ = PlaybackState::Error;
      return;
    }
  }
  pause_requested_ = false;
  state_ = PlaybackState::Playing;
  aptSetSleepAllowed(background_playback_);
}

void NdspAudioPlayer::pause() {
  if (state_ != PlaybackState::Playing)
    return;
  pause_requested_ = true;
  ndspChnWaveBufClear(audio_channel);
  state_ = PlaybackState::Paused;
  aptSetSleepAllowed(true);
}

void NdspAudioPlayer::stop() {
  stop_requested_ = true;
  running_ = false;
  pause_requested_ = false;
  if (initialized_) {
    ndspChnWaveBufClear(audio_channel);
  }
  join_worker();
  decoder_.reset();
  state_ = PlaybackState::Stopped;
  frames_played_ = 0;
  position_offset_ms_ = 0;
  sample_rate_ = 0;
  channels_ = 0;
  aptSetSleepAllowed(true);
}

void NdspAudioPlayer::update() {
  if (!running_ && thread_ != nullptr) {
    join_worker();
  }
}

void NdspAudioPlayer::set_volume(float volume) {
  volume_ = std::clamp(volume, 0.0f, 1.0f);
  apply_volume();
}

void NdspAudioPlayer::set_background_playback(bool enabled) {
  background_playback_ = enabled;
}

bool NdspAudioPlayer::seek(std::int64_t delta_ms) {
  if (loaded_track_.path.empty() || delta_ms == 0)
    return false;
  const bool resume = state_ == PlaybackState::Playing;
  const std::uint64_t current_ms = snapshot().position_ms;
  const std::uint64_t magnitude = static_cast<std::uint64_t>(delta_ms < 0 ? -delta_ms : delta_ms);
  const std::uint64_t target_ms =
      delta_ms < 0 ? (current_ms > magnitude ? current_ms - magnitude : 0) : current_ms + magnitude;
  stop();
  if (!load(loaded_track_) || decoder_ == nullptr || !decoder_->seek(target_ms))
    return false;
  decoder_positioned_ = true;
  position_offset_ms_ = target_ms;
  if (resume)
    play();
  return true;
}

PlaybackSnapshot NdspAudioPlayer::snapshot() const {
  PlaybackSnapshot result;
  result.state = state_;
  result.position_ms =
      position_offset_ms_ + (sample_rate_ == 0 ? 0 : (frames_played_ * 1000ULL) / sample_rate_);
  result.sample_rate = sample_rate_;
  result.channels = channels_;
  return result;
}

void NdspAudioPlayer::worker_entry(void* data) {
  static_cast<NdspAudioPlayer*>(data)->worker_main();
}

void NdspAudioPlayer::worker_main() {
  while (!stop_requested_) {
    if (pause_requested_) {
      svcSleepThread(1);
      continue;
    }
    std::size_t slot = 2;
    for (std::size_t candidate = 0; candidate < 2; ++candidate) {
      if (buffers_[candidate].status == NDSP_WBUF_DONE) {
        slot = candidate;
        break;
      }
    }
    if (slot == 2) {
      svcSleepThread(1);
      continue;
    }
    if (!fill_buffer(slot)) {
      if (!stop_requested_)
        state_ = PlaybackState::Stopped;
      running_ = false;
      break;
    }
    if (pause_requested_) {
      buffers_[slot].status = NDSP_WBUF_DONE;
      continue;
    }
    ndspChnWaveBufAdd(audio_channel, &buffers_[slot]);
    frames_played_ += buffers_[slot].nsamples;
  }
  running_ = false;
}

bool NdspAudioPlayer::fill_buffer(std::size_t slot) {
  std::size_t written = 0;
  PcmBlockInfo info;
  const DecodeResult result =
      decoder_->decode(decoded_samples_.data(), decoded_samples_.size(), written, info);
  if (result != DecodeResult::FrameReady || (info.channels != 1 && info.channels != 2))
    return false;
  const std::size_t frames = written / info.channels;
  if (frames == 0 || frames > buffer_frames)
    return false;
  if (sample_rate_ == 0) {
    sample_rate_ = info.sample_rate;
    channels_ = 2;
    ndspChnSetRate(audio_channel, static_cast<float>(sample_rate_));
  }
  if (info.channels == 1) {
    for (std::size_t frame = 0; frame < frames; ++frame) {
      storage_[slot][frame * 2] = decoded_samples_[frame];
      storage_[slot][frame * 2 + 1] = decoded_samples_[frame];
    }
  } else {
    std::memcpy(storage_[slot], decoded_samples_.data(), frames * 2 * sizeof(std::int16_t));
  }
  buffers_[slot].nsamples = static_cast<u32>(frames);
  DSP_FlushDataCache(storage_[slot], frames * 2 * sizeof(std::int16_t));
  return true;
}

void NdspAudioPlayer::join_worker() {
  if (thread_ == nullptr)
    return;
  stop_requested_ = true;
  running_ = false;
  threadJoin(thread_, U64_MAX);
  threadFree(thread_);
  thread_ = nullptr;
}

void NdspAudioPlayer::apply_volume() {
  if (!initialized_)
    return;
  float mix[12] = {};
  mix[0] = volume_;
  mix[1] = volume_;
  mix[2] = volume_;
  mix[3] = volume_;
  ndspChnSetMix(audio_channel, mix);
}

}
