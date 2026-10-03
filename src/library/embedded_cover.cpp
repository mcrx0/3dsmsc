#include "3dsmsc/library/embedded_cover.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "3dsmsc/util/unique_file.hpp"
#include "cover_readers.hpp"
#include "tag_readers.hpp"

namespace threedsmsc {
namespace {

using covers::PictureCandidate;

constexpr std::size_t probe_bytes = 10;  // enough to tell the containers apart

bool has_flac_marker(std::FILE* file) {
  unsigned char marker[4];
  return covers::read_exact(file, marker, sizeof(marker)) && std::memcmp(marker, "fLaC", 4) == 0;
}

// An ID3v2 tag, in an MP3 or in front of a FLAC stream.
bool find_in_id3_file(std::FILE* file, const unsigned char* header, std::size_t max_bytes,
                      PictureCandidate& candidate) {
  if (covers::find_id3_picture(file, header, max_bytes, candidate))
    return true;
  const long after_tag =
      static_cast<long>(probe_bytes) + static_cast<long>(tags::read_u32_syncsafe(header + 6));
  return std::fseek(file, after_tag, SEEK_SET) == 0 && has_flac_marker(file) &&
         covers::find_flac_picture(file, max_bytes, candidate);
}

}

bool read_embedded_cover(const std::string& audio_path, std::size_t max_bytes,
                         std::vector<unsigned char>& image) {
  const UniqueFile file = open_file(audio_path, "rb");
  if (!file)
    return false;
  unsigned char header[probe_bytes];
  if (!covers::read_exact(file.get(), header, sizeof(header)))
    return false;
  PictureCandidate candidate;
  bool found = false;
  if (std::memcmp(header, "ID3", 3) == 0) {
    found = find_in_id3_file(file.get(), header, max_bytes, candidate);
  } else if (std::memcmp(header, "fLaC", 4) == 0) {
    found = std::fseek(file.get(), 4, SEEK_SET) == 0 &&
            covers::find_flac_picture(file.get(), max_bytes, candidate);
  } else if (std::memcmp(header + 4, "ftyp", 4) == 0) {
    found = covers::find_mp4_picture(file.get(), max_bytes, candidate);
  }
  if (!found || candidate.image.size() > max_bytes)
    return false;
  image = std::move(candidate.image);
  return true;
}

}
