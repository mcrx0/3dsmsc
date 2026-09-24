#pragma once

#include <3ds.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

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

 private:
  static constexpr std::size_t buffer_frames = 4096;
  static constexpr std::size_t buffer_samples = buffer_frames * 2;

  static void worker_entry(void* data);
  void worker_main();
  bool fill_buffer(std::size_t slot);
  void join_worker();
  void apply_volume();

  Thread thread_ = nullptr;
  ndspWaveBuf buffers_[2] = {};
  std::int16_t* storage_[2] = {nullptr, nullptr};
  std::array<std::int16_t, 8192> decoded_samples_ = {};
  std::unique_ptr<AudioDecoder> decoder_;
  Track loaded_track_;
  volatile bool running_ = false;
  volatile bool stop_requested_ = false;
  volatile bool pause_requested_ = false;
  PlaybackState state_ = PlaybackState::Stopped;
  std::uint64_t frames_played_ = 0;
  std::uint64_t position_offset_ms_ = 0;
  std::uint32_t sample_rate_ = 0;
  std::uint16_t channels_ = 0;
  float volume_ = 0.8f;
  bool decoder_positioned_ = false;
  bool background_playback_ = true;
  bool initialized_ = false;
};

}
