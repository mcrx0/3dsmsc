#include <algorithm>
#include <cstring>
#include <utility>

#include "ndsp_player.hpp"

namespace threedsmsc {
namespace {

constexpr u32 audio_channel = 0;
constexpr u32 audio_format = NDSP_FORMAT_STEREO_PCM16;
// svcSleepThread takes nanoseconds; sleeping 1ns just spins and starves the UI thread.
constexpr s64 poll_interval_ns = 2000000;
// Audio runs above the UI thread (0x30) so a slow frame cannot starve the decoder. The stack
// holds minimp3's scratch (~16 KB) and the AAC decoder's working set.
constexpr s32 worker_priority = 0x28;
constexpr std::size_t worker_stack_bytes = 64 * 1024;

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
  init_result_ = ndspInit();
  if (R_FAILED(init_result_)) {
    // 0xD880A7FA: libctru could not find the DSP firmware dump.
    error_ = static_cast<u32>(init_result_) == 0xD880A7FAu
                 ? "No DSP firmware: add sdmc:/3ds/dspfirm.cdc"
                 : "Audio engine failed to start";
    return false;
  }
  ndspSetOutputMode(NDSP_OUTPUT_STEREO);
  ndspChnSetInterp(audio_channel, NDSP_INTERP_LINEAR);
  ndspChnSetRate(audio_channel, 44100.0f);
  ndspChnSetFormat(audio_channel, audio_format);
  initialized_ = true;
  apply_volume();
  for (std::size_t slot = 0; slot < buffer_count; ++slot) {
    storage_[slot] = static_cast<std::int16_t*>(linearAlloc(buffer_samples * sizeof(std::int16_t)));
    if (storage_[slot] == nullptr) {
      shutdown();
      return false;
    }
    std::memset(&buffers_[slot], 0, sizeof(buffers_[slot]));
    buffers_[slot].data_vaddr = storage_[slot];
    buffers_[slot].nsamples = target_frames;
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
  if (!initialized_) {
    if (error_.empty())
      error_ = "Audio engine is not running";
    return false;
  }
  stop();
  decoder_ = make_decoder(track.format);
  if (decoder_ == nullptr) {
    error_ = "Unsupported audio format";
    state_ = PlaybackState::Error;
    return false;
  }
  if (!decoder_->open(track)) {
    error_ = "Cannot open or decode this file";
    decoder_.reset();
    state_ = PlaybackState::Error;
    return false;
  }
  error_.clear();
  state_ = PlaybackState::Stopped;
  frames_played_ = 0;
  position_offset_ms_ = 0;
  sample_rate_ = 0;
  channels_ = 0;
  loaded_track_ = track;
  return true;
}

void NdspAudioPlayer::play() {
  if (!initialized_ || decoder_ == nullptr || state_ == PlaybackState::Error)
    return;
  if (state_ == PlaybackState::Stopped) {
    update();
    // A pending seek repositions the decoder itself once the worker starts.
    if (seek_target_ms_ < 0)
      decoder_->reset();
    frames_played_ = 0;
    for (auto& buffer : buffers_) {
      buffer.status = NDSP_WBUF_DONE;
    }
    next_slot_ = 0;
    stop_requested_ = false;
    running_ = true;
    thread_ = threadCreate(worker_entry, this, worker_stack_bytes, worker_priority, -2, false);
    if (thread_ == nullptr) {
      running_ = false;
      state_ = PlaybackState::Error;
      return;
    }
  }
  ndspChnSetPaused(audio_channel, false);
  state_ = PlaybackState::Playing;
  aptSetSleepAllowed(background_playback_);
}

void NdspAudioPlayer::pause() {
  if (state_ != PlaybackState::Playing)
    return;
  // Pausing the channel keeps the queued audio, so resuming continues exactly where it stopped
  // (clearing the queue would skip the buffered ~0.2 s).
  ndspChnSetPaused(audio_channel, true);
  state_ = PlaybackState::Paused;
  aptSetSleepAllowed(true);
}

void NdspAudioPlayer::stop() {
  stop_requested_ = true;
  running_ = false;
  if (initialized_) {
    ndspChnWaveBufClear(audio_channel);
    ndspChnSetPaused(audio_channel, false);
  }
  join_worker();
  decoder_.reset();
  seek_target_ms_ = -1;
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
  if (!initialized_ || decoder_ == nullptr || loaded_track_.path.empty() || delta_ms == 0 ||
      !decoder_->can_seek())
    return false;
  // Only record the request. Decoding to the new position can take a while and needs a big
  // stack, so the worker thread does it and the UI stays responsive. Repeated requests add up
  // because each one starts from the position the previous one asked for.
  const std::int64_t base =
      seek_target_ms_ >= 0 ? seek_target_ms_ : static_cast<std::int64_t>(snapshot().position_ms);
  std::int64_t target = base + delta_ms;
  if (target < 0)
    target = 0;
  if (target > 0x7FFFFFFF)
    target = 0x7FFFFFFF;
  seek_target_ms_ = static_cast<std::int32_t>(target);
  return true;
}

PlaybackSnapshot NdspAudioPlayer::snapshot() const {
  PlaybackSnapshot result;
  result.state = state_;
  const std::int32_t pending = seek_target_ms_;
  result.position_ms =
      pending >= 0 ? static_cast<std::uint64_t>(pending)
                   : static_cast<std::uint64_t>(position_offset_ms_) +
                         (sample_rate_ == 0 ? 0 : (frames_played_ * 1000ULL) / sample_rate_);
  result.sample_rate = sample_rate_;
  result.channels = channels_;
  return result;
}

void NdspAudioPlayer::worker_entry(void* data) {
  static_cast<NdspAudioPlayer*>(data)->worker_main();
}

void NdspAudioPlayer::worker_main() {
  while (!stop_requested_) {
    if (seek_target_ms_ >= 0 && !apply_seek())
      break;
    const std::size_t slot = next_slot_;
    if (buffers_[slot].status != NDSP_WBUF_DONE) {
      svcSleepThread(poll_interval_ns);
      continue;
    }
    const DecodeResult result = fill_buffer(slot);
    if (result != DecodeResult::FrameReady) {
      if (result == DecodeResult::EndOfStream) {
        // Let the audio already queued finish instead of cutting the track's tail.
        auto draining = [this] {
          for (const ndspWaveBuf& buffer : buffers_) {
            if (buffer.status != NDSP_WBUF_DONE)
              return true;
          }
          return false;
        };
        while (!stop_requested_ && draining())
          svcSleepThread(poll_interval_ns);
      }
      // A decode failure is reported as Error so the app can tell it from a finished track.
      if (!stop_requested_)
        state_ = result == DecodeResult::EndOfStream ? PlaybackState::Stopped : PlaybackState::Error;
      running_ = false;
      break;
    }
    ndspChnWaveBufAdd(audio_channel, &buffers_[slot]);
    frames_played_ += static_cast<std::uint32_t>(buffers_[slot].nsamples);
    next_slot_ = (slot + 1) % buffer_count;
  }
  running_ = false;
}

// Runs on the worker thread: drops the queued audio, moves the decoder, and restarts the
// buffers from the new position. Returns false when playback should end (a seek past the end
// of the track, which the app treats like the track finishing).
bool NdspAudioPlayer::apply_seek() {
  const std::int32_t target = seek_target_ms_;
  ndspChnWaveBufClear(audio_channel);
  // The DSP marks cleared buffers done asynchronously; wait briefly before reusing them.
  for (int attempt = 0; attempt < 50 && !stop_requested_; ++attempt) {
    bool all_done = true;
    for (const ndspWaveBuf& buffer : buffers_) {
      if (buffer.status != NDSP_WBUF_DONE)
        all_done = false;
    }
    if (all_done)
      break;
    svcSleepThread(poll_interval_ns);
  }
  for (ndspWaveBuf& buffer : buffers_)
    buffer.status = NDSP_WBUF_DONE;
  const bool moved = decoder_->seek(static_cast<std::uint64_t>(target));
  if (seek_target_ms_ == target)
    seek_target_ms_ = -1;  // otherwise a newer request arrived meanwhile and is handled next
  if (!moved) {
    if (!stop_requested_)
      state_ = PlaybackState::Stopped;
    running_ = false;
    return false;
  }
  position_offset_ms_ = static_cast<std::uint32_t>(target);
  frames_played_ = 0;
  next_slot_ = 0;
  return true;
}

DecodeResult NdspAudioPlayer::fill_buffer(std::size_t slot) {
  std::size_t filled_frames = 0;
  // Gather whole decoder blocks (MP3 1152 frames, AAC 1024, FLAC up to 4096) until the buffer
  // holds about target_frames: fewer, larger buffers mean fewer wake-ups and DSP calls.
  while (filled_frames < target_frames) {
    std::size_t written = 0;
    PcmBlockInfo info;
    const DecodeResult result =
        decoder_->decode(decoded_samples_.data(), decoded_samples_.size(), written, info);
    if (result != DecodeResult::FrameReady || (info.channels != 1 && info.channels != 2)) {
      if (filled_frames > 0)
        break;  // play what we have; the next call reports the end or the error again
      return result == DecodeResult::EndOfStream ? DecodeResult::EndOfStream : DecodeResult::Error;
    }
    const std::size_t frames = written / info.channels;
    if (frames == 0 || filled_frames + frames > buffer_frames)
      return filled_frames > 0 ? DecodeResult::FrameReady : DecodeResult::Error;
    if (sample_rate_ == 0) {
      sample_rate_ = info.sample_rate;
      channels_ = 2;
      ndspChnSetRate(audio_channel, static_cast<float>(sample_rate_));
    }
    std::int16_t* destination = storage_[slot] + filled_frames * 2;
    if (info.channels == 1) {
      for (std::size_t frame = 0; frame < frames; ++frame) {
        destination[frame * 2] = decoded_samples_[frame];
        destination[frame * 2 + 1] = decoded_samples_[frame];
      }
    } else {
      std::memcpy(destination, decoded_samples_.data(), frames * 2 * sizeof(std::int16_t));
    }
    filled_frames += frames;
  }
  buffers_[slot].nsamples = static_cast<u32>(filled_frames);
  DSP_FlushDataCache(storage_[slot], filled_frames * 2 * sizeof(std::int16_t));
  return DecodeResult::FrameReady;
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
