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

constexpr std::size_t max_block_bytes = std::size_t{256} * 1024;
constexpr unsigned block_streaminfo = 0;
constexpr unsigned block_vorbis_comment = 4;
constexpr unsigned char last_block_flag = 0x80;
constexpr std::size_t streaminfo_min_bytes = 18;

struct BlockHeader {
  unsigned type = 0;
  bool last = false;
  std::size_t size = 0;
};

bool read_block_header(std::FILE* file, BlockHeader& header) {
  unsigned char bytes[4];
  if (std::fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes))
    return false;
  header.type = bytes[0] & 0x7Fu;
  header.last = (bytes[0] & last_block_flag) != 0;
  header.size = (static_cast<std::size_t>(bytes[1]) << 16) |
                (static_cast<std::size_t>(bytes[2]) << 8) | bytes[3];
  return true;
}

// STREAMINFO bit layout after the two block sizes (32 bits) and two frame sizes (48 bits):
// sample rate 20 bits at bit 80, channels 3, bits per sample 5, then total samples 36 bits at
// bit 108. Counting the total from bit 104 would swallow part of the bits-per-sample field.
constexpr std::size_t sample_rate_bit = 80;
constexpr std::size_t total_samples_bit = 108;

std::uint64_t read_bits(const unsigned char* data, std::size_t first_bit, std::size_t bit_count) {
  std::uint64_t value = 0;
  for (std::size_t bit = 0; bit < bit_count; ++bit) {
    const std::size_t absolute = first_bit + bit;
    value = (value << 1) | ((data[absolute / 8] >> (7 - absolute % 8)) & 1u);
  }
  return value;
}

void read_streaminfo(const std::vector<unsigned char>& block, TrackMetadata& metadata) {
  if (block.size() < streaminfo_min_bytes)
    return;
  const auto sample_rate = static_cast<std::uint32_t>(read_bits(block.data(), sample_rate_bit, 20));
  const std::uint64_t total_samples = read_bits(block.data(), total_samples_bit, 36);
  if (sample_rate != 0)
    metadata.duration_ms = total_samples * 1000ULL / sample_rate;
}

void store_comment(std::string_view key, std::string_view value, TrackMetadata& metadata) {
  if (key == "TITLE")
    metadata.title = std::string(value);
  else if (key == "ARTIST")
    metadata.artist = std::string(value);
  else if (key == "ALBUM")
    metadata.album = std::string(value);
  else if (key == "TRACKNUMBER")
    metadata.track_number = parse_number_prefix(std::string(value));
  else if (key == "DISCNUMBER")
    metadata.disc_number = parse_number_prefix(std::string(value));
}

// A Vorbis comment block: vendor string, then `count` "KEY=value" entries, all length-prefixed
// little-endian. Returns whether at least one entry was read.
bool read_vorbis_comments(const std::vector<unsigned char>& block, TrackMetadata& metadata) {
  constexpr std::size_t length_bytes = 4;
  if (block.size() < 2 * length_bytes)
    return false;
  std::size_t offset = length_bytes + read_u32_le(block.data());  // skip the vendor string
  if (offset + length_bytes > block.size())
    return false;
  const std::uint32_t count = read_u32_le(block.data() + offset);
  offset += length_bytes;
  bool found = false;
  for (std::uint32_t index = 0; index < count && offset + length_bytes <= block.size(); ++index) {
    const std::uint32_t length = read_u32_le(block.data() + offset);
    offset += length_bytes;
    if (length > block.size() - offset)
      break;
    const std::string_view comment(reinterpret_cast<const char*>(block.data() + offset), length);
    offset += length;
    const std::size_t equals = comment.find('=');
    if (equals == std::string_view::npos)
      continue;
    store_comment(comment.substr(0, equals), comment.substr(equals + 1), metadata);
    found = true;
  }
  return found;
}

}

bool read_flac(std::FILE* file, TrackMetadata& metadata) {
  unsigned char marker[4];
  if (std::fread(marker, 1, sizeof(marker), file) != sizeof(marker) ||
      std::memcmp(marker, "fLaC", 4) != 0)
    return false;
  bool found_comments = false;
  BlockHeader header;
  bool more = true;
  while (more && read_block_header(file, header)) {
    more = !header.last;
    // Only STREAMINFO and VORBIS_COMMENT matter. Skip the rest, such as embedded pictures, by
    // seeking instead of reading megabytes into memory.
    const bool wanted = (header.type == block_streaminfo || header.type == block_vorbis_comment) &&
                        header.size <= max_block_bytes;
    if (!wanted) {
      if (std::fseek(file, static_cast<long>(header.size), SEEK_CUR) != 0)
        break;
      continue;
    }
    std::vector<unsigned char> block(header.size);
    if (header.size != 0 && std::fread(block.data(), 1, block.size(), file) != block.size())
      break;
    if (header.type == block_streaminfo)
      read_streaminfo(block, metadata);
    else if (read_vorbis_comments(block, metadata))
      found_comments = true;
  }
  return found_comments || metadata.duration_ms != 0;
}

}
