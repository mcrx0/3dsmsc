#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "tag_readers.hpp"

namespace threedsmsc::tags {
namespace {

// Frames larger than this are skipped, not read: text fields are tiny, while an embedded
// cover (APIC) can be megabytes and reading it for every track makes scans crawl on an SD card.
constexpr std::uint32_t max_text_frame_bytes = 4096;
constexpr std::size_t id3_header_bytes = 10;
constexpr std::size_t id3_frame_header_bytes = 10;
constexpr unsigned char flag_extended_header = 0x40;
constexpr unsigned char flag_footer = 0x10;

struct Id3Header {
  unsigned version = 0;        // 3 or 4
  std::uint32_t tag_size = 0;  // bytes after the 10-byte header, footer excluded
  std::uint32_t consumed = 0;  // bytes of the tag body already read (an extended header)
  bool has_footer = false;
};

// The fields this reader wants, and which of them the tag has supplied so far.
struct Id3Progress {
  bool title = false;
  bool artist = false;
  bool album = false;
  bool track = false;
  bool disc = false;

  bool complete() const { return title && artist && album && track && disc; }
};

enum class Id3Field { Other, Title, Artist, Album, Track, Disc };

Id3Field field_for_frame(std::string_view id) {
  if (id == "TIT2")
    return Id3Field::Title;
  if (id == "TPE1")
    return Id3Field::Artist;
  if (id == "TALB")
    return Id3Field::Album;
  if (id == "TRCK")
    return Id3Field::Track;
  if (id == "TPOS")
    return Id3Field::Disc;
  return Id3Field::Other;
}

void store_field(Id3Field field, const std::string& value, TrackMetadata& metadata,
                 Id3Progress& progress) {
  switch (field) {
    case Id3Field::Title:
      metadata.title = value;
      progress.title = true;
      break;
    case Id3Field::Artist:
      metadata.artist = value;
      progress.artist = true;
      break;
    case Id3Field::Album:
      metadata.album = value;
      progress.album = true;
      break;
    case Id3Field::Track:
      metadata.track_number = parse_number_prefix(value);
      progress.track = true;
      break;
    case Id3Field::Disc:
      metadata.disc_number = parse_number_prefix(value);
      progress.disc = true;
      break;
    case Id3Field::Other:
      break;
  }
}

// Reads the 10-byte header and steps over an extended header. False when this is not a tag
// the reader understands (ID3v2.3 or 2.4).
bool read_header(std::FILE* file, Id3Header& header) {
  unsigned char bytes[id3_header_bytes];
  if (std::fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes) ||
      std::memcmp(bytes, "ID3", 3) != 0)
    return false;
  header.version = bytes[3];
  if (header.version != 3 && header.version != 4)
    return false;
  header.tag_size = read_u32_syncsafe(bytes + 6);
  if (header.tag_size == 0)
    return false;
  header.has_footer = (bytes[5] & flag_footer) != 0;
  if ((bytes[5] & flag_extended_header) == 0)
    return true;
  unsigned char extended[4];
  if (std::fread(extended, 1, sizeof(extended), file) != sizeof(extended))
    return false;
  const std::uint32_t extended_size =
      header.version == 4 ? read_u32_syncsafe(extended) : read_u32_be(extended) + 4;
  if (extended_size < 4 || extended_size > header.tag_size ||
      std::fseek(file, static_cast<long>(extended_size - 4), SEEK_CUR) != 0)
    return false;
  header.consumed = extended_size;
  return true;
}

// Reads one frame: a wanted text frame is decoded, anything else is skipped by seeking. Returns
// false when the tag is over (padding, damage, or the end of the file).
bool read_frame(std::FILE* file, const Id3Header& header, TrackMetadata& metadata,
                Id3Progress& progress, std::uint32_t& offset) {
  unsigned char frame[id3_frame_header_bytes];
  if (std::fread(frame, 1, sizeof(frame), file) != sizeof(frame) || frame[0] == 0)
    return false;  // end of file, or the zero padding that closes a tag
  const std::uint32_t frame_size =
      header.version == 4 ? read_u32_syncsafe(frame + 4) : read_u32_be(frame + 4);
  offset += static_cast<std::uint32_t>(id3_frame_header_bytes);
  if (frame_size == 0 || frame_size > header.tag_size - offset)
    return false;
  const Id3Field field = field_for_frame(std::string_view(reinterpret_cast<const char*>(frame), 4));
  if (field != Id3Field::Other && frame_size <= max_text_frame_bytes) {
    std::vector<unsigned char> body(frame_size);
    if (std::fread(body.data(), 1, body.size(), file) != body.size())
      return false;
    store_field(field, decode_id3_text(body.data(), body.size()), metadata, progress);
  } else if (std::fseek(file, static_cast<long>(frame_size), SEEK_CUR) != 0) {
    return false;
  }
  offset += frame_size;
  return true;
}

}

bool read_id3(std::FILE* file, TrackMetadata& metadata) {
  Id3Header header;
  if (!read_header(file, header))
    return false;
  Id3Progress progress;
  std::uint32_t offset = header.consumed;
  while (offset + id3_frame_header_bytes <= header.tag_size && !progress.complete()) {
    if (!read_frame(file, header, metadata, progress, offset))
      break;
  }
  const long footer = header.has_footer ? static_cast<long>(id3_header_bytes) : 0;
  const long audio_start =
      static_cast<long>(id3_header_bytes) + static_cast<long>(header.tag_size) + footer;
  metadata.duration_ms = mp3_duration_ms(file, audio_start);
  return !metadata.title.empty() || !metadata.artist.empty() || !metadata.album.empty() ||
         metadata.duration_ms != 0;
}

}
