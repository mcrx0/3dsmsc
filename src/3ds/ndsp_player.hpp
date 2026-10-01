#pragma once

#include <3ds.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "3dsmsc/audio/aac_decoder.hpp"
#include "3dsmsc/audio/decoder.hpp"
#include "3dsmsc/audio/flac_decoder.hpp"
#include "3dsmsc/audio/mp3_decoder.hpp"
#include "3dsmsc/audio/playback.hpp"
#include "3dsmsc/library/track.hpp"

namespace threedsmsc {

class NdspAudioPlayer {
 public:
  bool init();
  void shutdown();
  bool load(const Track& track);
  void play();
  void pause();
  void stop();
  void update();
  void set_volume(float volume);
  void set_background_playback(bool enabled);
  bool seek(std::int64_t delta_ms);
  PlaybackSnapshot snapshot() const;
  // Why init() or the last load() failed, in words fit for the status line.
  const std::string& error() const { return error_; }
  Result init_result() const { return init_result_; }

 private:
  // Four buffers of ~2048 frames (~46 ms at 44.1 kHz each) ride out SD card and system stalls
  // that two 26 ms buffers could not. A buffer is topped up with whole decoder blocks until it
  // holds at least target_frames, so it needs room for target_frames plus one more block.
  static constexpr std::size_t buffer_count = 4;
  static constexpr std::size_t target_frames = 2048;
  static constexpr std::size_t buffer_frames = 8192;
  static constexpr std::size_t buffer_samples = buffer_frames * 2;

  static void worker_entry(void* data);
  void worker_main();
  // FrameReady when the slot was filled, EndOfStream at the end of the track, else Error.
  DecodeResult fill_buffer(std::size_t slot);
  bool apply_seek();
  void join_worker();
  void apply_volume();

  Thread thread_ = nullptr;
  ndspWaveBuf buffers_[buffer_count] = {};
  std::int16_t* storage_[buffer_count] = {};
  std::size_t next_slot_ = 0;  // buffers are queued strictly in round-robin order
  std::array<std::int16_t, 8192> decoded_samples_ = {};
  std::unique_ptr<AudioDecoder> decoder_;
  Track loaded_track_;
  volatile bool running_ = false;
  volatile bool stop_requested_ = false;
  PlaybackState state_ = PlaybackState::Stopped;
  // 32-bit so the worker's update and the UI thread's read cannot tear on the 32-bit ARM11.
  volatile std::uint32_t frames_played_ = 0;
  // Set by seek(), carried out on the worker thread; -1 when no seek is pending. 32-bit so the
  // UI thread and the worker cannot tear a value.
  volatile std::int32_t seek_target_ms_ = -1;
  volatile std::uint32_t position_offset_ms_ = 0;
  std::uint32_t sample_rate_ = 0;
  std::uint16_t channels_ = 0;
  float volume_ = 0.8f;
  bool background_playback_ = true;
  bool initialized_ = false;
  Result init_result_ = 0;
  std::string error_;
};

}
