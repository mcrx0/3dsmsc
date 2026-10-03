#include "3dsmsc/library/embedded_cover.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "3dsmsc/util/unique_file.hpp"
#include "tag_readers.hpp"

namespace threedsmsc {
namespace {

using tags::read_u32_be;
using tags::read_u32_syncsafe;

constexpr unsigned picture_type_front_cover = 3;
// A picture frame carries a few header bytes (encoding, MIME type, description) around the image.
constexpr std::size_t picture_header_slack = 1024;

// The best picture seen so far: a front cover wins, otherwise the first one found.
struct Candidate {
  std::vector<unsigned char> image;
  bool found = false;

  // Returns true when this picture is a front cover, so the search can stop.
  bool offer(unsigned type, const unsigned char* data, std::size_t size) {
    if (size == 0)
      return false;
    if (type == picture_type_front_cover || !found) {
      image.assign(data, data + size);
      found = true;
    }
    return type == picture_type_front_cover;
  }
};

bool read_exact(std::FILE* file, void* destination, std::size_t size) {
  return std::fread(destination, 1, size, file) == size;
}

bool skip(std::FILE* file, std::uint64_t size) {
  return size <= 0x7FFFFFFFu && std::fseek(file, static_cast<long>(size), SEEK_CUR) == 0;
}

// --- ID3v2 -----------------------------------------------------------------------------------

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
bool offer_id3_picture(const std::vector<unsigned char>& body, unsigned version,
                       bool unsynchronised, Candidate& candidate) {
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
  const unsigned char* data = body.data() + index;
  const std::size_t size = body.size() - index;
  if (!unsynchronised)
    return candidate.offer(type, data, size);
  const std::vector<unsigned char> restored = remove_unsynchronisation(data, size);
  return candidate.offer(type, restored.data(), restored.size());
}

struct FrameLayout {
  unsigned version = 0;
  std::size_t header_bytes = 10;
  bool unsynchronised = false;
  std::uint32_t tag_end = 0;  // offset of the end of the tag body, counted from the tag's start
};

std::uint32_t frame_size_of(const FrameLayout& layout, const unsigned char* header) {
  if (layout.version == 2)
    return (static_cast<std::uint32_t>(header[3]) << 16) |
           (static_cast<std::uint32_t>(header[4]) << 8) | header[5];
  return layout.version == 4 ? read_u32_syncsafe(header + 4) : read_u32_be(header + 4);
}

bool is_picture_frame(const FrameLayout& layout, const unsigned char* header) {
  if (layout.version == 2)
    return std::memcmp(header, "PIC", 3) == 0;
  return std::memcmp(header, "APIC", 4) == 0;
}

// Reads the tag's header and steps over an extended header. Leaves the file at the first frame
// and returns its offset from the start of the tag.
bool read_id3_layout(std::FILE* file, const unsigned char* header, FrameLayout& layout,
                     std::uint32_t& first_frame) {
  layout.version = header[3];
  if (layout.version < 2 || layout.version > 4)
    return false;
  layout.header_bytes = layout.version == 2 ? 6 : 10;
  layout.unsynchronised = (header[5] & 0x80) != 0;
  const std::uint32_t size = read_u32_syncsafe(header + 6);
  layout.tag_end = 10 + size;
  std::uint32_t offset = 10;
  if (layout.version >= 3 && (header[5] & 0x40) != 0) {
    unsigned char extended[4];
    if (!read_exact(file, extended, sizeof(extended)))
      return false;
    const std::uint32_t extended_size =
        layout.version == 4 ? read_u32_syncsafe(extended) : read_u32_be(extended) + 4;
    if (extended_size < 4 || !skip(file, extended_size - 4))
      return false;
    offset += extended_size;
  }
  first_frame = offset;
  return offset <= layout.tag_end;
}

// Walks the tag's frames. True when a picture was found; the file is left inside the tag.
bool find_id3_picture(std::FILE* file, const unsigned char* header, std::size_t max_bytes,
                      Candidate& candidate) {
  FrameLayout layout;
  std::uint32_t offset = 0;
  if (!read_id3_layout(file, header, layout, offset))
    return false;
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
      if (offer_id3_picture(body, layout.version, layout.unsynchronised, candidate))
        return true;
    } else if (!skip(file, size)) {
      break;
    }
    offset += size;
  }
  return candidate.found;
}

// --- FLAC ------------------------------------------------------------------------------------

constexpr unsigned flac_block_picture = 6;

// A PICTURE block: type, MIME type, description, width, height, depth, colours, then the image,
// each field a big-endian length followed by that many bytes.
bool offer_flac_picture(const std::vector<unsigned char>& block, std::size_t max_bytes,
                        Candidate& candidate) {
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
  return candidate.offer(type, block.data() + index, length);
}

bool find_flac_picture(std::FILE* file, std::size_t max_bytes, Candidate& candidate) {
  unsigned char block[4];
  while (read_exact(file, block, sizeof(block))) {
    const unsigned type = block[0] & 0x7Fu;
    const bool last = (block[0] & 0x80u) != 0;
    const std::size_t size = (static_cast<std::size_t>(block[1]) << 16) |
                             (static_cast<std::size_t>(block[2]) << 8) | block[3];
    if (type == flac_block_picture && size <= max_bytes + picture_header_slack) {
      std::vector<unsigned char> body(size);
      if (!read_exact(file, body.data(), body.size()))
        break;
      if (offer_flac_picture(body, max_bytes, candidate))
        return true;
    } else if (!skip(file, size)) {
      break;
    }
    if (last)
      break;
  }
  return candidate.found;
}

// --- MP4 -------------------------------------------------------------------------------------

struct BoxRange {
  std::uint64_t start = 0;  // first byte of the payload
  std::uint64_t end = 0;    // one past the last byte
};

// Searches the boxes in [range.start, range.end) for `name`. On success `range` becomes that
// box's payload, after skipping `payload_prefix` bytes of fixed fields.
bool find_box(std::FILE* file, BoxRange& range, const char* name, std::uint64_t payload_prefix) {
  std::uint64_t position = range.start;
  while (position + 8 <= range.end) {
    if (std::fseek(file, static_cast<long>(position), SEEK_SET) != 0)
      return false;
    unsigned char header[16];
    if (!read_exact(file, header, 8))
      return false;
    std::uint64_t size = read_u32_be(header);
    std::uint64_t header_bytes = 8;
    if (size == 1) {
      if (!read_exact(file, header + 8, 8))
        return false;
      size = (static_cast<std::uint64_t>(read_u32_be(header + 8)) << 32) | read_u32_be(header + 12);
      header_bytes = 16;
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

bool find_mp4_picture(std::FILE* file, std::size_t max_bytes, Candidate& candidate) {
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
  if (size == 0 || size > max_bytes || std::fseek(file, static_cast<long>(range.start), SEEK_SET))
    return false;
  std::vector<unsigned char> image(static_cast<std::size_t>(size));
  if (!read_exact(file, image.data(), image.size()))
    return false;
  return candidate.offer(picture_type_front_cover, image.data(), image.size());
}

// --- dispatch --------------------------------------------------------------------------------

bool has_flac_marker(std::FILE* file) {
  unsigned char marker[4];
  return read_exact(file, marker, sizeof(marker)) && std::memcmp(marker, "fLaC", 4) == 0;
}

}

bool read_embedded_cover(const std::string& audio_path, std::size_t max_bytes,
                         std::vector<unsigned char>& image) {
  const UniqueFile file = open_file(audio_path, "rb");
  if (!file)
    return false;
  unsigned char header[10];
  if (!read_exact(file.get(), header, sizeof(header)))
    return false;
  Candidate candidate;
  if (std::memcmp(header, "ID3", 3) == 0) {
    if (!find_id3_picture(file.get(), header, max_bytes, candidate)) {
      // Some FLAC files carry an ID3 tag in front of the stream.
      const long after_tag = static_cast<long>(10 + read_u32_syncsafe(header + 6));
      if (std::fseek(file.get(), after_tag, SEEK_SET) != 0 || !has_flac_marker(file.get()) ||
          !find_flac_picture(file.get(), max_bytes, candidate)) {
        return false;
      }
    }
  } else if (std::memcmp(header, "fLaC", 4) == 0) {
    if (std::fseek(file.get(), 4, SEEK_SET) != 0 ||
        !find_flac_picture(file.get(), max_bytes, candidate))
      return false;
  } else if (std::memcmp(header + 4, "ftyp", 4) == 0) {
    if (!find_mp4_picture(file.get(), max_bytes, candidate))
      return false;
  } else {
    return false;
  }
  if (candidate.image.size() > max_bytes)
    return false;
  image = std::move(candidate.image);
  return true;
}

}
