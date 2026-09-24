#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "3dsmsc/library/metadata.hpp"
#include "minimp4.h"

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

std::uint32_t read_u32_be(const unsigned char* data) {
  return (static_cast<std::uint32_t>(data[0]) << 24) | (static_cast<std::uint32_t>(data[1]) << 16) |
         (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
}

std::uint32_t read_u32_syncsafe(const unsigned char* data) {
  return (static_cast<std::uint32_t>(data[0] & 0x7F) << 21) |
         (static_cast<std::uint32_t>(data[1] & 0x7F) << 14) |
         (static_cast<std::uint32_t>(data[2] & 0x7F) << 7) |
         static_cast<std::uint32_t>(data[3] & 0x7F);
}

std::uint32_t read_u32_le(const unsigned char* data) {
  return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}

void append_text_character(std::string& output, std::uint32_t value) {
  if (value <= 0x7F) {
    output.push_back(static_cast<char>(value));
  } else if (value <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | (value >> 6)));
    output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xE0 | (value >> 12)));
    output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
  }
}

std::string decode_text(const unsigned char* data, std::size_t size, unsigned char encoding) {
  if (size == 0)
    return {};
  const unsigned char* text = data + 1;
  std::size_t text_size = size - 1;
  if (encoding == 1 || encoding == 2) {
    if (text_size < 2)
      return {};
    const bool big_endian = text[0] == 0xFE && text[1] == 0xFF;
    const bool little_endian = text[0] == 0xFF && text[1] == 0xFE;
    if (!big_endian && !little_endian)
      return {};
    text += 2;
    text_size -= 2;
    text_size -= text_size % 2;
    std::string result;
    for (std::size_t offset = 0; offset < text_size; offset += 2) {
      const std::uint32_t value =
          big_endian ? (static_cast<std::uint32_t>(text[offset]) << 8) | text[offset + 1]
                     : (static_cast<std::uint32_t>(text[offset + 1]) << 8) | text[offset];
      if (value == 0)
        break;
      append_text_character(result, value);
    }
    return result;
  }
  std::size_t end = 0;
  while (end < text_size && text[end] != 0)
    ++end;
  return std::string(reinterpret_cast<const char*>(text), end);
}

bool read_id3(std::FILE* file, TrackMetadata& metadata) {
  unsigned char header[10];
  if (std::fread(header, 1, sizeof(header), file) != sizeof(header) ||
      std::memcmp(header, "ID3", 3) != 0)
    return false;
  if (header[3] != 3 && header[3] != 4)
    return false;
  const std::uint32_t tag_size = read_u32_syncsafe(header + 6);
  if (tag_size == 0 || tag_size > 16u * 1024u * 1024u)
    return false;
  std::vector<unsigned char> data(tag_size);
  if (std::fread(data.data(), 1, data.size(), file) != data.size())
    return false;
  std::size_t offset = 0;
  while (offset + 10 <= data.size()) {
    const unsigned char* frame = data.data() + offset;
    const std::uint32_t frame_size =
        header[3] == 4 ? read_u32_syncsafe(frame + 4) : read_u32_be(frame + 4);
    if (frame_size == 0 || frame_size > data.size() - offset - 10)
      break;
    offset += 10;
    const std::string_view id(reinterpret_cast<const char*>(frame), 4);
    if (id == "TIT2" || id == "TPE1" || id == "TALB") {
      const std::string value = decode_text(data.data() + offset, frame_size, data[offset]);
      if (id == "TIT2")
        metadata.title = value;
      else if (id == "TPE1")
        metadata.artist = value;
      else
        metadata.album = value;
    }
    offset += frame_size;
  }
  return !metadata.title.empty() || !metadata.artist.empty() || !metadata.album.empty();
}

std::uint64_t read_be_bits(const unsigned char* data, std::size_t byte_offset,
                           std::size_t bit_count) {
  std::uint64_t value = 0;
  for (std::size_t bit = 0; bit < bit_count; ++bit) {
    const std::size_t absolute = byte_offset * 8 + bit;
    value = (value << 1) | ((data[absolute / 8] >> (7 - absolute % 8)) & 1u);
  }
  return value;
}

bool read_flac(std::FILE* file, TrackMetadata& metadata) {
  unsigned char marker[4];
  if (std::fread(marker, 1, sizeof(marker), file) != sizeof(marker) ||
      std::memcmp(marker, "fLaC", 4) != 0)
    return false;
  bool found = false;
  while (true) {
    unsigned char block_header[4];
    if (std::fread(block_header, 1, sizeof(block_header), file) != sizeof(block_header))
      return found;
    const std::size_t block_size = (static_cast<std::size_t>(block_header[1]) << 16) |
                                   (static_cast<std::size_t>(block_header[2]) << 8) |
                                   block_header[3];
    std::vector<unsigned char> block(block_size);
    if (block_size != 0 && std::fread(block.data(), 1, block.size(), file) != block.size())
      return found;
    if ((block_header[0] & 0x7F) == 0 && block.size() >= 18) {
      const std::uint32_t sample_rate =
          static_cast<std::uint32_t>(read_be_bits(block.data(), 10, 20));
      const std::uint64_t total_samples = read_be_bits(block.data(), 13, 36);
      if (sample_rate != 0)
        metadata.duration_ms = total_samples * 1000ULL / sample_rate;
    }
    if ((block_header[0] & 0x7F) == 4 && block.size() >= 8) {
      std::size_t offset = 0;
      const std::uint32_t vendor_size = read_u32_le(block.data());
      offset += 4;
      if (vendor_size <= block.size() - offset) {
        offset += vendor_size;
        if (offset + 4 <= block.size()) {
          const std::uint32_t count = read_u32_le(block.data() + offset);
          offset += 4;
          for (std::uint32_t index = 0; index < count && offset + 4 <= block.size(); ++index) {
            const std::uint32_t length = read_u32_le(block.data() + offset);
            offset += 4;
            if (length > block.size() - offset)
              break;
            const std::string_view comment(reinterpret_cast<const char*>(block.data() + offset),
                                           length);
            offset += length;
            const std::size_t equals = comment.find('=');
            if (equals == std::string_view::npos)
              continue;
            const std::string_view key = comment.substr(0, equals);
            const std::string_view value = comment.substr(equals + 1);
            if (key == "TITLE")
              metadata.title = std::string(value);
            else if (key == "ARTIST")
              metadata.artist = std::string(value);
            else if (key == "ALBUM")
              metadata.album = std::string(value);
            found = true;
          }
        }
      }
    }
    if ((block_header[0] & 0x80) != 0)
      break;
  }
  return found || metadata.duration_ms != 0;
}

int read_mp4_callback(std::int64_t offset, void* buffer, std::size_t size, void* token) {
  std::FILE* file = static_cast<std::FILE*>(token);
  if (file == nullptr || std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0)
    return -1;
  return std::fread(buffer, 1, size, file) == size ? 0 : -1;
}

bool read_mp4(std::FILE* file, TrackMetadata& metadata) {
  if (std::fseek(file, 0, SEEK_END) != 0)
    return false;
  const long file_size = std::ftell(file);
  if (file_size <= 0 || std::fseek(file, 0, SEEK_SET) != 0)
    return false;
  MP4D_demux_t mp4{};
  if (MP4D_open(&mp4, read_mp4_callback, file, file_size) != 1)
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
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr)
    return false;
  unsigned char marker[4];
  const bool has_marker = std::fread(marker, 1, sizeof(marker), file) == sizeof(marker);
  std::fseek(file, 0, SEEK_SET);
  bool result = false;
  if (has_marker && std::memcmp(marker, "ID3", 3) == 0)
    result = read_id3(file, metadata);
  else if (has_marker && std::memcmp(marker, "fLaC", 4) == 0)
    result = read_flac(file, metadata);
  else
    result = read_mp4(file, metadata);
  std::fclose(file);
  return result;
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
  return primary_found || fallback_found;
}

}
