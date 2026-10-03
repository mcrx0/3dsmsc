#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "cover_readers.hpp"
#include "tag_readers.hpp"

namespace threedsmsc::covers {
namespace {

using tags::read_u32_be;

constexpr std::size_t box_header_bytes = 8;
constexpr std::size_t large_box_header_bytes = 16;

struct BoxRange {
  std::uint64_t start = 0;  // first byte of the payload
  std::uint64_t end = 0;    // one past the last byte
};

// Searches the boxes in [range.start, range.end) for `name`. On success `range` becomes that
// box's payload, after skipping `payload_prefix` bytes of fixed fields.
bool find_box(std::FILE* file, BoxRange& range, const char* name, std::uint64_t payload_prefix) {
  std::uint64_t position = range.start;
  while (position + box_header_bytes <= range.end) {
    if (std::fseek(file, static_cast<long>(position), SEEK_SET) != 0)
      return false;
    unsigned char header[large_box_header_bytes];
    if (!read_exact(file, header, box_header_bytes))
      return false;
    std::uint64_t size = read_u32_be(header);
    std::uint64_t header_bytes = box_header_bytes;
    if (size == 1) {  // a 64-bit size follows
      if (!read_exact(file, header + box_header_bytes, box_header_bytes))
        return false;
      size = (static_cast<std::uint64_t>(read_u32_be(header + 8)) << 32) | read_u32_be(header + 12);
      header_bytes = large_box_header_bytes;
    } else if (size == 0) {
      size = range.end - position;  // the last box runs to the end
    }
    if (size < header_bytes || size > range.end - position)
      return false;
    if (std::memcmp(header + 4, name, 4) == 0) {
      range.start = position + header_bytes + payload_prefix;
      range.end = position + size;
      return range.start <= range.end;
    }
    position += size;
  }
  return false;
}

}

bool find_mp4_picture(std::FILE* file, std::size_t max_bytes, PictureCandidate& candidate) {
  if (std::fseek(file, 0, SEEK_END) != 0)
    return false;
  const long length = std::ftell(file);
  if (length <= 0)
    return false;
  BoxRange range{0, static_cast<std::uint64_t>(length)};
  // moov > udta > meta (four bytes of version and flags) > ilst > covr > data (eight bytes of
  // type and locale, then the image).
  if (!find_box(file, range, "moov", 0) || !find_box(file, range, "udta", 0) ||
      !find_box(file, range, "meta", 4) || !find_box(file, range, "ilst", 0) ||
      !find_box(file, range, "covr", 0) || !find_box(file, range, "data", 8)) {
    return false;
  }
  const std::uint64_t size = range.end - range.start;
  if (size == 0 || size > max_bytes ||
      std::fseek(file, static_cast<long>(range.start), SEEK_SET) != 0) {
    return false;
  }
  std::vector<unsigned char> image(static_cast<std::size_t>(size));
  if (!read_exact(file, image.data(), image.size()))
    return false;
  return candidate.offer(picture_type_front_cover, std::move(image), 0);
}

}
