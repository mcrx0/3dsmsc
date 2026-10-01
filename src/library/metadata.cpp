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
  } else if (value >= 0x10000) {
    output.push_back(static_cast<char>(0xF0 | (value >> 18)));
    output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
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
    bool big_endian = encoding == 2;
    if (encoding == 1) {
      if (text_size < 2)
        return {};
      const bool bom_be = text[0] == 0xFE && text[1] == 0xFF;
      const bool bom_le = text[0] == 0xFF && text[1] == 0xFE;
      if (!bom_be && !bom_le)
        return {};
      big_endian = bom_be;
      text += 2;
      text_size -= 2;
    }
    text_size -= text_size % 2;
    std::string result;
    for (std::size_t offset = 0; offset < text_size; offset += 2) {
      std::uint32_t value =
          big_endian ? (static_cast<std::uint32_t>(text[offset]) << 8) | text[offset + 1]
                     : (static_cast<std::uint32_t>(text[offset + 1]) << 8) | text[offset];
      if (value == 0)
        break;
      if (value >= 0xD800 && value <= 0xDBFF && offset + 4 <= text_size) {
        const std::uint32_t low =
            big_endian ? (static_cast<std::uint32_t>(text[offset + 2]) << 8) | text[offset + 3]
                       : (static_cast<std::uint32_t>(text[offset + 3]) << 8) | text[offset + 2];
        if (low >= 0xDC00 && low <= 0xDFFF) {
          value = 0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
          offset += 2;
        }
      }
      append_text_character(result, value);
    }
    return result;
  }
  std::size_t end = 0;
  while (end < text_size && text[end] != 0)
    ++end;
  if (encoding == 0) {
    std::string result;
    result.reserve(end);
    for (std::size_t index = 0; index < end; ++index)
      append_text_character(result, text[index]);
    return result;
  }
  return std::string(reinterpret_cast<const char*>(text), end);
}

std::uint16_t parse_number_prefix(std::string_view value) {
  std::uint32_t number = 0;
  bool found = false;
  for (const char c : value) {
    if (c < '0' || c > '9')
      break;
    found = true;
    number = number * 10 + static_cast<std::uint32_t>(c - '0');
    if (number > 9999)
      return 0;
  }
  return found ? static_cast<std::uint16_t>(number) : 0;
}

struct Mp3FrameHeader {
  unsigned bitrate_kbps;
  unsigned sample_rate;
  unsigned samples_per_frame;
  unsigned xing_offset;  // from the start of the frame
  unsigned vbri_offset;
};

bool parse_mp3_header(const unsigned char* h, Mp3FrameHeader& out) {
  if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0)
    return false;
  const unsigned version = (h[1] >> 3) & 3;  // 0: MPEG 2.5, 2: MPEG 2, 3: MPEG 1
  const unsigned layer = (h[1] >> 1) & 3;    // 1: layer III, 2: layer II, 3: layer I
  const unsigned bitrate_index = (h[2] >> 4) & 0xF;
  const unsigned rate_index = (h[2] >> 2) & 3;
  if (version == 1 || layer == 0 || bitrate_index == 0 || bitrate_index == 15 || rate_index == 3)
    return false;
  static const unsigned mpeg1[3][14] = {
      {32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},  // layer I
      {32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},     // layer II
      {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}};     // layer III
  static const unsigned mpeg2[3][14] = {
      {32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
      {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
      {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}};
  static const unsigned rates[3][3] = {{11025, 12000, 8000}, {0, 0, 0}, {22050, 24000, 16000}};
  static const unsigned rates_mpeg1[3] = {44100, 48000, 32000};
  const bool mpeg1_stream = version == 3;
  const unsigned layer_row = layer == 3 ? 0 : (layer == 2 ? 1 : 2);
  out.bitrate_kbps = (mpeg1_stream ? mpeg1 : mpeg2)[layer_row][bitrate_index - 1];
  out.sample_rate = mpeg1_stream ? rates_mpeg1[rate_index] : rates[version][rate_index];
  out.samples_per_frame = layer == 3 ? 384 : (layer == 2 ? 1152 : (mpeg1_stream ? 1152 : 576));
  const bool mono = ((h[3] >> 6) & 3) == 3;
  out.xing_offset = mpeg1_stream ? (mono ? 21 : 36) : (mono ? 13 : 21);
  out.vbri_offset = 36;
  return out.sample_rate != 0;
}

// Length of an MP3 whose audio starts at audio_start. A Xing/Info or VBRI header in the first
// frame holds the exact frame count; otherwise the length is estimated from the bitrate, which
// is exact for constant-bitrate files.
std::uint64_t mp3_duration_ms(std::FILE* file, long audio_start) {
  if (std::fseek(file, 0, SEEK_END) != 0)
    return 0;
  const long file_size = std::ftell(file);
  if (file_size <= audio_start || std::fseek(file, audio_start, SEEK_SET) != 0)
    return 0;
  unsigned char buffer[4096];
  const std::size_t bytes = std::fread(buffer, 1, sizeof(buffer), file);
  for (std::size_t i = 0; i + 4 <= bytes; ++i) {
    Mp3FrameHeader header{};
    if (!parse_mp3_header(buffer + i, header))
      continue;
    const auto tag_at = [&](unsigned offset, const char* name) {
      return i + offset + 4 <= bytes && std::memcmp(buffer + i + offset, name, 4) == 0;
    };
    unsigned frames = 0;
    if ((tag_at(header.xing_offset, "Xing") || tag_at(header.xing_offset, "Info")) &&
        i + header.xing_offset + 12 <= bytes &&
        (read_u32_be(buffer + i + header.xing_offset + 4) & 1u) != 0) {
      frames = read_u32_be(buffer + i + header.xing_offset + 8);
    } else if (tag_at(header.vbri_offset, "VBRI") && i + header.vbri_offset + 18 <= bytes) {
      frames = read_u32_be(buffer + i + header.vbri_offset + 14);
    }
    if (frames != 0) {
      return static_cast<std::uint64_t>(frames) * header.samples_per_frame * 1000ULL /
             header.sample_rate;
    }
    const std::uint64_t audio_bytes = static_cast<std::uint64_t>(file_size - audio_start);
    return audio_bytes * 8ULL / header.bitrate_kbps;  // bits / (kbit/s) = milliseconds
  }
  return 0;
}

// Frames larger than this are skipped, not read: text fields are tiny, while an embedded
// cover (APIC) can be megabytes and reading it for every track makes scans crawl on an SD card.
constexpr std::uint32_t max_text_frame_bytes = 4096;
constexpr std::size_t max_flac_block_bytes = 256 * 1024;

bool read_id3(std::FILE* file, TrackMetadata& metadata) {
  unsigned char header[10];
  if (std::fread(header, 1, sizeof(header), file) != sizeof(header) ||
      std::memcmp(header, "ID3", 3) != 0)
    return false;
  if (header[3] != 3 && header[3] != 4)
    return false;
  const std::uint32_t tag_size = read_u32_syncsafe(header + 6);
  if (tag_size == 0)
    return false;
  std::uint32_t offset = 0;
  if ((header[5] & 0x40) != 0) {  // extended header: skip it
    unsigned char extended[4];
    if (std::fread(extended, 1, sizeof(extended), file) != sizeof(extended))
      return false;
    const std::uint32_t extended_size =
        header[3] == 4 ? read_u32_syncsafe(extended) : read_u32_be(extended) + 4;
    if (extended_size < 4 || extended_size > tag_size ||
        std::fseek(file, static_cast<long>(extended_size - 4), SEEK_CUR) != 0)
      return false;
    offset = extended_size;
  }
  bool has_title = false;
  bool has_artist = false;
  bool has_album = false;
  bool has_track = false;
  bool has_disc = false;
  while (offset + 10 <= tag_size) {
    unsigned char frame[10];
    if (std::fread(frame, 1, sizeof(frame), file) != sizeof(frame) || frame[0] == 0)
      break;  // end of file or the zero padding that closes a tag
    const std::uint32_t frame_size =
        header[3] == 4 ? read_u32_syncsafe(frame + 4) : read_u32_be(frame + 4);
    offset += 10;
    if (frame_size == 0 || frame_size > tag_size - offset)
      break;
    const std::string_view id(reinterpret_cast<const char*>(frame), 4);
    const bool wanted = id == "TIT2" || id == "TPE1" || id == "TALB" || id == "TRCK" || id == "TPOS";
    if (wanted && frame_size <= max_text_frame_bytes) {
      std::vector<unsigned char> body(frame_size);
      if (std::fread(body.data(), 1, body.size(), file) != body.size())
        break;
      const std::string value = decode_text(body.data(), body.size(), body[0]);
      if (id == "TIT2") {
        metadata.title = value;
        has_title = true;
      } else if (id == "TPE1") {
        metadata.artist = value;
        has_artist = true;
      } else if (id == "TALB") {
        metadata.album = value;
        has_album = true;
      } else if (id == "TRCK") {
        metadata.track_number = parse_number_prefix(value);
        has_track = true;
      } else {
        metadata.disc_number = parse_number_prefix(value);
        has_disc = true;
      }
    } else if (std::fseek(file, static_cast<long>(frame_size), SEEK_CUR) != 0) {
      break;
    }
    offset += frame_size;
    if (has_title && has_artist && has_album && has_track && has_disc)
      break;
  }
  const long audio_start = 10 + static_cast<long>(tag_size) + ((header[5] & 0x10) != 0 ? 10 : 0);
  metadata.duration_ms = mp3_duration_ms(file, audio_start);
  return !metadata.title.empty() || !metadata.artist.empty() || !metadata.album.empty() ||
         metadata.duration_ms != 0;
}

// An MP3 with no ID3 tag starts straight at its first frame.
bool read_untagged_mp3(std::FILE* file, TrackMetadata& metadata) {
  metadata.duration_ms = mp3_duration_ms(file, 0);
  return metadata.duration_ms != 0;
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
    const unsigned block_type = block_header[0] & 0x7F;
    // Only STREAMINFO (0) and VORBIS_COMMENT (4) matter. Skip the rest, such as embedded
    // pictures, by seeking instead of reading megabytes into memory.
    if ((block_type != 0 && block_type != 4) || block_size > max_flac_block_bytes) {
      if (std::fseek(file, static_cast<long>(block_size), SEEK_CUR) != 0)
        return found || metadata.duration_ms != 0;
      if ((block_header[0] & 0x80) != 0)
        break;
      continue;
    }
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
            else if (key == "TRACKNUMBER")
              metadata.track_number = parse_number_prefix(value);
            else if (key == "DISCNUMBER")
              metadata.disc_number = parse_number_prefix(value);
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
  else if (has_marker && marker[0] == 0xFF && (marker[1] & 0xE0) == 0xE0)
    result = read_untagged_mp3(file, metadata);
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
  if (metadata.track_number == 0)
    metadata.track_number = fallback_metadata.track_number;
  return primary_found || fallback_found;
}

}
