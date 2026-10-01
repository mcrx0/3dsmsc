#include <cstdint>
#include <cstdio>

#include "minimp4.h"
#include "tag_readers.hpp"

namespace threedsmsc::tags {
namespace {

// minimp4 reads the container through this callback, one range at a time.
int read_range(std::int64_t offset, void* buffer, std::size_t size, void* token) {
  auto* file = static_cast<std::FILE*>(token);
  if (file == nullptr || std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0)
    return -1;
  return std::fread(buffer, 1, size, file) == size ? 0 : -1;
}

}

bool read_mp4(std::FILE* file, TrackMetadata& metadata) {
  if (std::fseek(file, 0, SEEK_END) != 0)
    return false;
  const long file_size = std::ftell(file);
  if (file_size <= 0 || std::fseek(file, 0, SEEK_SET) != 0)
    return false;
  MP4D_demux_t mp4{};
  if (MP4D_open(&mp4, read_range, file, file_size) != 1)
    return false;
  if (mp4.tag.title != nullptr)
    metadata.title = reinterpret_cast<const char*>(mp4.tag.title);
  if (mp4.tag.artist != nullptr)
    metadata.artist = reinterpret_cast<const char*>(mp4.tag.artist);
  if (mp4.tag.album != nullptr)
    metadata.album = reinterpret_cast<const char*>(mp4.tag.album);
  if (mp4.timescale != 0) {
    const std::uint64_t duration_units =
        (static_cast<std::uint64_t>(mp4.duration_hi) << 32) | mp4.duration_lo;
    metadata.duration_ms = duration_units * 1000ULL / mp4.timescale;
  }
  MP4D_close(&mp4);
  return !metadata.title.empty() || !metadata.artist.empty() || !metadata.album.empty() ||
         metadata.duration_ms != 0;
}

}
