#pragma once

// Private to the library module: one picture finder per container, used by embedded_cover.cpp.
// Each reads from an open file and leaves the best picture it finds in a PictureCandidate.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace threedsmsc::covers {

constexpr unsigned picture_type_front_cover = 3;
// A picture frame carries a few header bytes (encoding, MIME type, description) around the image.
constexpr std::size_t picture_header_slack = 1024;

// The best picture seen so far: a front cover wins, otherwise the first one found.
struct PictureCandidate {
  std::vector<unsigned char> image;
  bool found = false;

  // Takes the picture that is `body[offset..]`, reusing the buffer instead of copying a
  // megabyte-sized image. Returns true when it is a front cover, so the search can stop.
  bool offer(unsigned type, std::vector<unsigned char>&& body, std::size_t offset) {
    if (offset >= body.size())
      return false;
    if (type == picture_type_front_cover || !found) {
      body.erase(body.begin(), body.begin() + static_cast<std::ptrdiff_t>(offset));
      image = std::move(body);
      found = true;
    }
    return type == picture_type_front_cover;
  }
};

inline bool read_exact(std::FILE* file, void* destination, std::size_t size) {
  return std::fread(destination, 1, size, file) == size;
}

inline bool skip_bytes(std::FILE* file, std::uint64_t size) {
  return size <= 0x7FFFFFFFu && std::fseek(file, static_cast<long>(size), SEEK_CUR) == 0;
}

// ID3v2 (MP3, and some FLAC files): `header` is the 10 bytes already read from the file start.
bool find_id3_picture(std::FILE* file, const unsigned char* header, std::size_t max_bytes,
                      PictureCandidate& candidate);
// FLAC: the file is positioned just after the "fLaC" marker.
bool find_flac_picture(std::FILE* file, std::size_t max_bytes, PictureCandidate& candidate);
// MP4/M4A: searches the whole file.
bool find_mp4_picture(std::FILE* file, std::size_t max_bytes, PictureCandidate& candidate);

}
