#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "3dsmsc/library/metadata.hpp"
#include "3dsmsc/util/unique_file.hpp"
#include "tag_readers.hpp"

namespace threedsmsc {
namespace {

std::string_view basename(std::string_view path) {
  const std::size_t separator = path.find_last_of("/\\:");
  return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

std::string_view parent_name(std::string_view path) {
  const std::size_t separator = path.find_last_of("/\\:");
  if (separator == std::string_view::npos)
    return {};
  const std::string_view parent = path.substr(0, separator);
  const std::size_t parent_separator = parent.find_last_of("/\\:");
  return parent_separator == std::string_view::npos ? parent : parent.substr(parent_separator + 1);
}

std::string without_extension(std::string_view name) {
  const std::size_t dot = name.rfind('.');
  return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
}

// Picks the reader from the first bytes of the file: an ID3 tag, a FLAC stream, a bare MP3
// frame, or (anything else) an MP4 container.
bool read_tags(std::FILE* file, TrackMetadata& metadata) {
  unsigned char marker[4];
  const bool has_marker = std::fread(marker, 1, sizeof(marker), file) == sizeof(marker);
  if (std::fseek(file, 0, SEEK_SET) != 0)
    return false;
  if (has_marker && std::memcmp(marker, "ID3", 3) == 0)
    return tags::read_id3(file, metadata);
  if (has_marker && std::memcmp(marker, "fLaC", 4) == 0)
    return tags::read_flac(file, metadata);
  if (has_marker && marker[0] == 0xFF && (marker[1] & 0xE0) == 0xE0)
    return tags::read_untagged_mp3(file, metadata);
  return tags::read_mp4(file, metadata);
}

}

bool FilenameMetadataReader::read(const std::string& path, TrackMetadata& metadata) {
  metadata = TrackMetadata{};
  if (path.empty())
    return false;
  const std::string_view name = basename(path);
  metadata.title = without_extension(name);
  metadata.album = std::string(parent_name(path));
  return !metadata.title.empty();
}

bool EmbeddedMetadataReader::read(const std::string& path, TrackMetadata& metadata) {
  metadata = TrackMetadata{};
  const UniqueFile file = open_file(path, "rb");
  return file != nullptr && read_tags(file.get(), metadata);
}

bool CompositeMetadataReader::read(const std::string& path, TrackMetadata& metadata) {
  TrackMetadata primary_metadata;
  const bool primary_found = primary_.read(path, primary_metadata);
  TrackMetadata fallback_metadata;
  const bool fallback_found = fallback_.read(path, fallback_metadata);
  metadata = primary_found ? primary_metadata : fallback_metadata;
  if (fallback_found) {
    if (metadata.title.empty())
      metadata.title = fallback_metadata.title;
    if (metadata.artist.empty())
      metadata.artist = fallback_metadata.artist;
    if (metadata.album.empty())
      metadata.album = fallback_metadata.album;
  }
  if (metadata.track_number == 0)
    metadata.track_number = fallback_metadata.track_number;
  return primary_found || fallback_found;
}

}
