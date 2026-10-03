#include "cover_loader.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace threedsmsc {
namespace {

// The lowest priority there is: a cover may arrive a moment late, but must never delay the
// screen (0x30) or the audio (0x28).
constexpr s32 worker_priority = 0x3F;
// stb_image keeps its large tables on the heap; this covers the decoder's own call depth.
constexpr std::size_t worker_stack_bytes = std::size_t{96} * 1024;
constexpr s64 idle_sleep_ns = 50000000;

class Guard {
 public:
  explicit Guard(LightLock& lock) : lock_(lock) { LightLock_Lock(&lock_); }
  Guard(const Guard&) = delete;
  Guard& operator=(const Guard&) = delete;
  ~Guard() { LightLock_Unlock(&lock_); }

 private:
  LightLock& lock_;
};

}

bool CoverLoader::start() {
  if (thread_ != nullptr)
    return true;
  LightLock_Init(&lock_);
  stop_requested_ = false;
  thread_ = threadCreate(entry, this, worker_stack_bytes, worker_priority, -2, false);
  return thread_ != nullptr;
}

void CoverLoader::stop() {
  if (thread_ == nullptr)
    return;
  stop_requested_ = true;
  threadJoin(thread_, U64_MAX);
  threadFree(thread_);
  thread_ = nullptr;
}

void CoverLoader::request(const std::string& path) {
  const Guard guard(lock_);
  pending_path_ = path;
  has_pending_ = !path.empty();
  has_done_ = false;  // a finished result for an older request is stale
}

bool CoverLoader::take(std::string& path, CoverPixels& tiled, bool& ok) {
  const Guard guard(lock_);
  if (!has_done_)
    return false;
  path = done_path_;
  ok = done_ok_;
  if (done_ok_)
    tiled = done_pixels_;
  has_done_ = false;
  return true;
}

void CoverLoader::entry(void* data) {
  static_cast<CoverLoader*>(data)->run();
}

void CoverLoader::run() {
  while (!stop_requested_) {
    std::string path;
    {
      const Guard guard(lock_);
      if (has_pending_) {
        path = std::move(pending_path_);
        has_pending_ = false;
      }
    }
    if (path.empty()) {
      svcSleepThread(idle_sleep_ns);
      continue;
    }
    const bool ok = load_cover(path, linear_);
    const Guard guard(lock_);
    // A newer request arrived while decoding: this picture is already out of date.
    if (has_pending_)
      continue;
    done_path_ = std::move(path);
    done_ok_ = ok;
    if (ok)
      tile_for_gpu(linear_, done_pixels_);
    has_done_ = true;
  }
}

}
