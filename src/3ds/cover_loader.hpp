#pragma once

#include <3ds.h>

#include <string>

#include "3dsmsc/ui/cover_image.hpp"

namespace threedsmsc {

// Decodes cover images on a background thread, so a large JPEG never stalls the screen or the
// buttons. The UI thread asks for a file with request() and collects the finished thumbnail with
// take(); only the latest request is kept, and a result for an older one is dropped by the caller.
class CoverLoader {
 public:
  CoverLoader() = default;
  CoverLoader(const CoverLoader&) = delete;
  CoverLoader& operator=(const CoverLoader&) = delete;
  ~CoverLoader() { stop(); }

  bool start();
  void stop();
  // Replaces the pending request; an empty path cancels it.
  void request(const std::string& path);
  // True once per finished decode. `ok` says whether `path` produced a picture; `tiled` is then
  // in GPU layout (see tile_for_gpu).
  bool take(std::string& path, CoverPixels& tiled, bool& ok);

 private:
  static void entry(void* data);
  void run();

  Thread thread_ = nullptr;
  LightLock lock_ = {};
  volatile bool stop_requested_ = false;
  // Guarded by lock_.
  std::string pending_path_;
  bool has_pending_ = false;
  std::string done_path_;
  CoverPixels done_pixels_ = {};
  bool done_ok_ = false;
  bool has_done_ = false;
  // Used only by the worker.
  CoverPixels linear_ = {};
};

}
