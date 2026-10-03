#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "3dsmsc/library/track.hpp"

namespace threedsmsc {

// The cover is shown as a small thumbnail, so it is shrunk to one fixed texture: 64 x 64 pixels,
// RGB565 (8 KB). The GPU needs a power-of-two size.
constexpr int cover_size = 64;
constexpr std::size_t cover_pixel_count = static_cast<std::size_t>(cover_size) * cover_size;
using CoverPixels = std::array<std::uint16_t, cover_pixel_count>;

// Limits that keep decoding within the 3DS's memory: a bigger file or image is not shown at all.
constexpr std::size_t max_cover_file_bytes = std::size_t{2} * 1024 * 1024;
constexpr long max_cover_pixels = 1200L * 1200L;

// Decodes a PNG or JPEG, crops it to a centred square, and shrinks it to cover_size. The result is
// row-major with the top-left pixel first. Returns false for a missing, unreadable, oversized, or
// unsupported file; nothing is thrown.
bool load_cover(const std::string& path, CoverPixels& pixels);

// Same, from a picture already in memory (the PNG or JPEG bytes of an embedded cover).
bool load_cover_from_memory(const unsigned char* data, std::size_t size, CoverPixels& pixels);

// Which tracks share one cover picture, so it is decoded once and stays on screen across a track
// change: tracks whose album has a picture file share that file; otherwise tracks with the same
// artist and album tags share the album's embedded picture; any other track stands alone.
std::string cover_key(const Track& track);

// Where a track's cover came from.
enum class CoverSource { None, Sidecar, Embedded };

// The cover for a track: the picture file next to it (`artwork_path`, may be empty) if that loads,
// otherwise the picture embedded in the audio file itself.
CoverSource load_track_cover(const std::string& artwork_path, const std::string& audio_path,
                             CoverPixels& pixels);

// Rearranges pixels into the layout the 3DS GPU reads: 8 x 8 tiles in Morton order, with the rows
// flipped because texture row 0 is the bottom of the image.
void tile_for_gpu(const CoverPixels& linear, CoverPixels& tiled);

}
