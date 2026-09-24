#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "3dsmsc/library/track.hpp"

namespace threedsmsc {
namespace {

std::string lowercase_extension(std::string_view path) {
  const std::size_t separator = path.find_last_of("/\\:");
  const std::size_t start = separator == std::string_view::npos ? 0 : separator + 1;
  const std::size_t dot = path.rfind('.');
  if (dot == std::string_view::npos || dot < start || dot + 1 >= path.size()) {
    return {};
  }
  std::string extension(path.substr(dot + 1));
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  return extension;
}

}

AudioFormat format_from_path(std::string_view path) {
  const std::string extension = lowercase_extension(path);
  if (extension == "mp3")
    return AudioFormat::Mp3;
  if (extension == "aac")
    return AudioFormat::Aac;
  if (extension == "m4a")
    return AudioFormat::M4a;
  if (extension == "mp4")
    return AudioFormat::Mp4;
  if (extension == "flac")
    return AudioFormat::Flac;
  return AudioFormat::Unknown;
}

bool is_supported_path(std::string_view path) {
  return format_from_path(path) != AudioFormat::Unknown;
}

}
