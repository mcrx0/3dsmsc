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
using tags::read_u32_syncsafe;

constexpr std::size_t id3_header_bytes = 10;
constexpr unsigned char flag_unsynchronised = 0x80;
constexpr unsigned char flag_extended_header = 0x40;

// The end of the text field that starts at `index`: a single zero byte for Latin-1 and UTF-8, a
// pair of zero bytes on a two-byte boundary for UTF-16. Returns the index just past it, or the
// size when the field is not terminated.
std::size_t skip_terminated_text(const std::vector<unsigned char>& body, std::size_t index,
                                 unsigned char encoding) {
  const bool wide = encoding == 1 || encoding == 2;
  const std::size_t step = wide ? 2 : 1;
  for (std::size_t position = index; position + step <= body.size(); position += step) {
    if (body[position] == 0 && (!wide || body[position + 1] == 0))
      return position + step;
  }
  return body.size();
}

// Undoes ID3 "unsynchronisation", which writes 0xFF 0x00 wherever the data had 0xFF.
std::vector<unsigned char> remove_unsynchronisation(const unsigned char* data, std::size_t size) {
  std::vector<unsigned char> result;
  result.reserve(size);
  for (std::size_t index = 0; index < size; ++index) {
    result.push_back(data[index]);
    if (data[index] == 0xFF && index + 1 < size && data[index + 1] == 0)
      ++index;
  }
  return result;
}

// An APIC (v2.3/2.4) or PIC (v2.2) frame body: encoding, MIME type or image format, picture type,
// description, then the image to the end of the frame.
bool offer_picture(std::vector<unsigned char>&& body, unsigned version, bool unsynchronised,
                   PictureCandidate& candidate) {
  if (body.size() < 4)
    return false;
  const unsigned char encoding = body[0];
  std::size_t index = 1;
  if (version == 2) {
    index += 3;  // a three-letter format such as "JPG" or "PNG"
  } else {
    while (index < body.size() && body[index] != 0)
      ++index;
    ++index;  // the MIME type's terminator
  }
  if (index >= body.size())
    return false;
  const unsigned type = body[index++];
  index = skip_terminated_text(body, index, encoding);
  if (index >= body.size())
    return false;
  if (!unsynchronised)
    return candidate.offer(type, std::move(body), index);
  return candidate.offer(type, remove_unsynchronisation(body.data() + index, body.size() - index),
                         0);
}

struct FrameLayout {
  unsigned version = 0;
  std::size_t header_bytes = 10;
  bool unsynchronised = false;
  std::uint32_t tag_end = 0;      // offset of the end of the tag body, from the tag's start
  std::uint32_t first_frame = 0;  // offset of the first frame, after any extended header
};

std::uint32_t frame_size_of(const FrameLayout& layout, const unsigned char* header) {
  if (layout.version == 2) {
    return (static_cast<std::uint32_t>(header[3]) << 16) |
           (static_cast<std::uint32_t>(header[4]) << 8) | header[5];
  }
  return layout.version == 4 ? read_u32_syncsafe(header + 4) : read_u32_be(header + 4);
}

bool is_picture_frame(const FrameLayout& layout, const unsigned char* header) {
  if (layout.version == 2)
    return std::memcmp(header, "PIC", 3) == 0;
  return std::memcmp(header, "APIC", 4) == 0;
}

// Reads the tag's header and steps over an extended header, leaving the file at the first frame.
bool read_layout(std::FILE* file, const unsigned char* header, FrameLayout& layout) {
  layout.version = header[3];
  if (layout.version < 2 || layout.version > 4)
    return false;
  layout.header_bytes = layout.version == 2 ? 6 : 10;
  layout.unsynchronised = (header[5] & flag_unsynchronised) != 0;
  layout.tag_end = id3_header_bytes + read_u32_syncsafe(header + 6);
  layout.first_frame = id3_header_bytes;
  if (layout.version >= 3 && (header[5] & flag_extended_header) != 0) {
    unsigned char extended[4];
    if (!read_exact(file, extended, sizeof(extended)))
      return false;
    const std::uint32_t extended_size =
        layout.version == 4 ? read_u32_syncsafe(extended) : read_u32_be(extended) + 4;
    if (extended_size < 4 || !skip_bytes(file, extended_size - 4))
      return false;
    layout.first_frame += extended_size;
  }
  return layout.first_frame <= layout.tag_end;
}

}

bool find_id3_picture(std::FILE* file, const unsigned char* header, std::size_t max_bytes,
                      PictureCandidate& candidate) {
  FrameLayout layout;
  if (!read_layout(file, header, layout))
    return false;
  std::uint32_t offset = layout.first_frame;
  std::vector<unsigned char> frame(layout.header_bytes);
  while (offset + layout.header_bytes <= layout.tag_end) {
    if (!read_exact(file, frame.data(), frame.size()) || frame[0] == 0)
      break;  // end of file, or the zero padding that closes a tag
    offset += static_cast<std::uint32_t>(layout.header_bytes);
    const std::uint32_t size = frame_size_of(layout, frame.data());
    if (size == 0 || size > layout.tag_end - offset)
      break;
    if (is_picture_frame(layout, frame.data()) && size <= max_bytes + picture_header_slack) {
      std::vector<unsigned char> body(size);
      if (!read_exact(file, body.data(), body.size()))
        break;
      if (offer_picture(std::move(body), layout.version, layout.unsynchronised, candidate))
        return true;
    } else if (!skip_bytes(file, size)) {
      break;
    }
    offset += size;
  }
  return candidate.found;
}

}
