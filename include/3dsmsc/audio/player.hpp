#pragma once

#include <cstdint>
#include <string>

#include "3dsmsc/audio/playback.hpp"
#include "3dsmsc/library/track.hpp"

namespace threedsmsc {

// What the app needs from an audio backend. The 3DS implementation drives NDSP; tests use a
// fake, so the playback rules (skipping unplayable tracks, repeat, failure limits) run on the host.
class AudioPlayer {
 public:
  virtual ~AudioPlayer() = default;

  // False when the audio engine could not start (for example, no DSP firmware).
  virtual bool available() const = 0;
  virtual bool load(const Track& track) = 0;
  virtual void play() = 0;
  virtual void pause() = 0;
  virtual void stop() = 0;
  virtual void update() = 0;
  // Moves the position by delta_ms; false when the loaded file cannot be seeked.
  virtual bool seek(std::int64_t delta_ms) = 0;
  virtual PlaybackSnapshot snapshot() const = 0;
  // Why the engine or the last load failed, in words fit for the status line.
  virtual const std::string& error() const = 0;
};

}
