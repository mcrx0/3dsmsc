#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace threedsmsc {

enum class AudioFormat : std::uint8_t {
  Unknown,
  Mp3,
  Aac,
  M4a,
  Mp4,
  Flac,
};

struct Track {
  std::string path;
  std::string title;
  std::string artist;
  std::string album;
  std::string artwork_path;
  std::uint64_t duration_ms;
  AudioFormat format;
};

AudioFormat format_from_path(std::string_view path);
bool is_supported_path(std::string_view path);

}
