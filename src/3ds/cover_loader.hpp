#pragma once

#include <3ds.h>

#include <string>

#include "3dsmsc/ui/cover_image.hpp"

namespace threedsmsc {

// Decodes cover images on a background thread, so a large JPEG never stalls the screen or the
// buttons. The UI thread asks for a track's cover with request() and collects the finished
// thumbnail with take(); only the latest request is kept, and a result for an older one is dropped
// by the caller (which compares keys).
class CoverLoader {
 public:
  CoverLoader() = default;
  CoverLoader(const CoverLoader&) = delete;
  CoverLoader& operator=(const CoverLoader&) = delete;
  ~CoverLoader() { stop(); }

  bool start();
  void stop();
  // Replaces the pending request. `key` identifies the picture (it comes back from take()); the
  // sidecar file is tried first, then the picture inside the audio file. An empty key cancels.
  void request(const std::string& key, const std::string& artwork_path,
               const std::string& audio_path);
  // True once per finished decode. `source` says where the picture came from, None if the track
  // has no usable cover; `tiled` is then in GPU layout (see tile_for_gpu).
  bool take(std::string& key, CoverPixels& tiled, CoverSource& source);

 private:
  static void entry(void* data);
  void run();

  Thread thread_ = nullptr;
  LightLock lock_ = {};
  volatile bool stop_requested_ = false;
  // Guarded by lock_.
  struct Request {
    std::string key;
    std::string artwork_path;
    std::string audio_path;
  };
  Request pending_;
  bool has_pending_ = false;
  std::string done_key_;
  CoverPixels done_pixels_ = {};
  CoverSource done_source_ = CoverSource::None;
  volatile bool has_done_ = false;
  // Used only by the worker.
  CoverPixels linear_ = {};
  CoverPixels tiled_ = {};
};

}
