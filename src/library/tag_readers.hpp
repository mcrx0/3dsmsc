#pragma once

// Private to the library module: one reader per audio container, used by metadata.cpp.
// Each reads from an open file positioned at its start and returns whether it found anything
// useful (a title, artist, album, or a length).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "3dsmsc/library/metadata.hpp"

namespace threedsmsc::tags {

bool read_id3(std::FILE* file, TrackMetadata& metadata);
bool read_flac(std::FILE* file, TrackMetadata& metadata);
bool read_mp4(std::FILE* file, TrackMetadata& metadata);
// An MP3 with no ID3 tag starts straight at its first frame; only its length can be read.
bool read_untagged_mp3(std::FILE* file, TrackMetadata& metadata);

// Length of an MP3 whose audio starts `audio_start` bytes into the file (see tag_mp3.cpp).
std::uint64_t mp3_duration_ms(std::FILE* file, long audio_start);

// Decodes an ID3v2 text frame: `data[0]` is the encoding byte (0 Latin-1, 1 UTF-16 with BOM,
// 2 UTF-16BE, 3 UTF-8), the rest is the text. The result is UTF-8.
std::string decode_id3_text(const unsigned char* data, std::size_t size);

// The leading decimal number of "3/12" or "7", or 0 when there is none or it is out of range.
std::uint16_t parse_number_prefix(const std::string& value);

inline std::uint32_t read_u32_be(const unsigned char* data) {
  return (static_cast<std::uint32_t>(data[0]) << 24) | (static_cast<std::uint32_t>(data[1]) << 16) |
         (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
}

inline std::uint32_t read_u32_le(const unsigned char* data) {
  return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}

// ID3v2.4 sizes use 7 bits per byte.
inline std::uint32_t read_u32_syncsafe(const unsigned char* data) {
  return (static_cast<std::uint32_t>(data[0] & 0x7F) << 21) |
         (static_cast<std::uint32_t>(data[1] & 0x7F) << 14) |
         (static_cast<std::uint32_t>(data[2] & 0x7F) << 7) |
         static_cast<std::uint32_t>(data[3] & 0x7F);
}

}
