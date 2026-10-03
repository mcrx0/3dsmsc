#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

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

// Rearranges pixels into the layout the 3DS GPU reads: 8 x 8 tiles in Morton order, with the rows
// flipped because texture row 0 is the bottom of the image.
void tile_for_gpu(const CoverPixels& linear, CoverPixels& tiled);

}
