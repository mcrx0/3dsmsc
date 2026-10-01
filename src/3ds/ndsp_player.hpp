#pragma once

#include <3ds.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "3dsmsc/audio/aac_decoder.hpp"
#include "3dsmsc/audio/decoder.hpp"
#include "3dsmsc/audio/equalizer.hpp"
#include "3dsmsc/audio/flac_decoder.hpp"
#include "3dsmsc/audio/mp3_decoder.hpp"
#include "3dsmsc/audio/playback.hpp"
#include "3dsmsc/audio/player.hpp"
#include "3dsmsc/library/track.hpp"

namespace threedsmsc {

class NdspAudioPlayer final : public AudioPlayer {
 public:
  // Starts the DSP. False when it cannot (see error()); the app then runs without sound.
  bool init();
  void shutdown();
  bool available() const override { return initialized_; }
  bool load(const Track& track) override;
  void play() override;
  void pause() override;
  void stop() override;
  void update() override;
  void set_volume(float volume);
  void set_background_playback(bool enabled);
  // Takes effect on the next audio block; safe to call while playing.
  void set_equalizer(bool enabled, const std::int8_t* gains);
  bool seek(std::int64_t delta_ms) override;
  PlaybackSnapshot snapshot() const override;
  // Why init() or the last load() failed, in words fit for the status line.
  const std::string& error() const override { return error_; }
  // Asks the DSP whether headphones are plugged in. A service call, so poll it about once a
  // second from the UI thread rather than every frame.
  void poll_headphones();
  bool headphones() const { return headphones_; }
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
  // Waits until every queued buffer has played, so a track's tail is not cut off.
  void drain_queued_audio();
  // Records how the track ended (finished, or failed) when the worker stops on its own.
  void finish_track(DecodeResult result);
  // Copies one decoded block into destination as stereo, applying the equalizer.
  void append_block(std::int16_t* destination, std::size_t frames, std::uint16_t channels);
  void join_worker();
  void apply_volume();
  void refresh_equalizer();

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
  // The UI thread writes these and bumps eq_revision_ last; the worker reconfigures its filters
  // when it sees a new revision.
  volatile bool eq_enabled_ = false;
  volatile std::int8_t eq_gains_[eq_band_count] = {};
  volatile std::uint32_t eq_revision_ = 1;
  std::uint32_t eq_applied_revision_ = 0;
  std::uint32_t eq_applied_rate_ = 0;
  Equalizer equalizer_;
  bool background_playback_ = true;
  bool initialized_ = false;
  bool headphones_ = false;
  Result init_result_ = 0;
  std::string error_;
};

}
