#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#include "cover_readers.hpp"
#include "tag_readers.hpp"

namespace threedsmsc::covers {
namespace {

using tags::read_u32_be;

constexpr unsigned block_picture = 6;
constexpr unsigned char last_block_flag = 0x80;

// A PICTURE block: type, MIME type, description, width, height, depth, colours, then the image,
// each field a big-endian length followed by that many bytes.
bool offer_picture(std::vector<unsigned char>&& block, std::size_t max_bytes,
                   PictureCandidate& candidate) {
  std::size_t index = 0;
  const auto read_u32 = [&](std::uint32_t& value) {
    if (block.size() - index < 4)
      return false;
    value = read_u32_be(block.data() + index);
    index += 4;
    return true;
  };
  std::uint32_t type = 0;
  std::uint32_t length = 0;
  if (!read_u32(type) || !read_u32(length) || length > block.size() - index)
    return false;
  index += length;  // MIME type
  if (!read_u32(length) || length > block.size() - index)
    return false;
  index += length;  // description
  std::uint32_t ignored = 0;
  for (int field = 0; field < 4; ++field) {  // width, height, depth, colour count
    if (!read_u32(ignored))
      return false;
  }
  if (!read_u32(length) || length > block.size() - index || length > max_bytes)
    return false;
  block.resize(index + length);  // drop anything after the image
  return candidate.offer(type, std::move(block), index);
}

}

bool find_flac_picture(std::FILE* file, std::size_t max_bytes, PictureCandidate& candidate) {
  unsigned char block[4];
  while (read_exact(file, block, sizeof(block))) {
    const unsigned type = block[0] & 0x7Fu;
    const bool last = (block[0] & last_block_flag) != 0;
    const std::size_t size = (static_cast<std::size_t>(block[1]) << 16) |
                             (static_cast<std::size_t>(block[2]) << 8) | block[3];
    if (type == block_picture && size <= max_bytes + picture_header_slack) {
      std::vector<unsigned char> body(size);
      if (!read_exact(file, body.data(), body.size()))
        break;
      if (offer_picture(std::move(body), max_bytes, candidate))
        return true;
    } else if (!skip_bytes(file, size)) {
      break;
    }
    if (last)
      break;
  }
  return candidate.found;
}

}
