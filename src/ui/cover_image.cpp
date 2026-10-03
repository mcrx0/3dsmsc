#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "3dsmsc/ui/cover_image.hpp"
#include "3dsmsc/util/unique_file.hpp"
#include "stb_image.h"

namespace threedsmsc {
namespace {

struct FreeDeleter {
  void operator()(void* pointer) const { std::free(pointer); }
};
using Buffer = std::unique_ptr<unsigned char, FreeDeleter>;

// Reads a whole file with malloc, which reports failure as null instead of aborting (the build
// has no exceptions), so a large cover on a low-memory console is skipped rather than fatal.
Buffer read_file(const std::string& path, std::size_t& size) {
  const UniqueFile file = open_file(path, "rb");
  if (!file || std::fseek(file.get(), 0, SEEK_END) != 0)
    return nullptr;
  const long length = std::ftell(file.get());
  if (length <= 0 || static_cast<std::size_t>(length) > max_cover_file_bytes ||
      std::fseek(file.get(), 0, SEEK_SET) != 0) {
    return nullptr;
  }
  size = static_cast<std::size_t>(length);
  Buffer buffer(static_cast<unsigned char*>(std::malloc(size)));
  if (!buffer || std::fread(buffer.get(), 1, size, file.get()) != size)
    return nullptr;
  return buffer;
}

std::uint16_t pack_rgb565(unsigned red, unsigned green, unsigned blue) {
  return static_cast<std::uint16_t>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
}

// Averages the block of source pixels that falls on each output pixel (a box filter, so shrinking
// a large cover does not alias), after cropping the image to its centred square.
void shrink_square(const unsigned char* rgb, int width, int height, CoverPixels& pixels) {
  const int side = std::min(width, height);
  const int left = (width - side) / 2;
  const int top = (height - side) / 2;
  for (int out_y = 0; out_y < cover_size; ++out_y) {
    const int y_begin = top + out_y * side / cover_size;
    const int y_end = std::max(y_begin + 1, top + (out_y + 1) * side / cover_size);
    for (int out_x = 0; out_x < cover_size; ++out_x) {
      const int x_begin = left + out_x * side / cover_size;
      const int x_end = std::max(x_begin + 1, left + (out_x + 1) * side / cover_size);
      unsigned long red = 0;
      unsigned long green = 0;
      unsigned long blue = 0;
      for (int y = y_begin; y < y_end; ++y) {
        const unsigned char* row = rgb + (static_cast<std::size_t>(y) * width + x_begin) * 3;
        for (int x = x_begin; x < x_end; ++x, row += 3) {
          red += row[0];
          green += row[1];
          blue += row[2];
        }
      }
      const auto count =
          static_cast<unsigned long>(y_end - y_begin) * static_cast<unsigned long>(x_end - x_begin);
      pixels[static_cast<std::size_t>(out_y) * cover_size + out_x] =
          pack_rgb565(static_cast<unsigned>(red / count), static_cast<unsigned>(green / count),
                      static_cast<unsigned>(blue / count));
    }
  }
}

// The position of bit-interleaved (x, y) inside an 8 x 8 tile: x0 y0 x1 y1 x2 y2.
std::size_t morton_in_tile(int x, int y) {
  return static_cast<std::size_t>((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) |
                                  ((x & 4) << 2) | ((y & 4) << 3));
}

}

bool load_cover(const std::string& path, CoverPixels& pixels) {
  std::size_t size = 0;
  const Buffer file = read_file(path, size);
  if (!file)
    return false;
  int width = 0;
  int height = 0;
  int channels = 0;
  const int length = static_cast<int>(size);
  if (stbi_info_from_memory(file.get(), length, &width, &height, &channels) == 0 || width <= 0 ||
      height <= 0 || static_cast<long>(width) * height > max_cover_pixels) {
    return false;
  }
  stbi_uc* decoded = stbi_load_from_memory(file.get(), length, &width, &height, &channels, 3);
  if (decoded == nullptr)
    return false;
  shrink_square(decoded, width, height, pixels);
  stbi_image_free(decoded);
  return true;
}

void tile_for_gpu(const CoverPixels& linear, CoverPixels& tiled) {
  constexpr int tiles_per_row = cover_size / 8;
  for (int y = 0; y < cover_size; ++y) {
    const int row = cover_size - 1 - y;
    for (int x = 0; x < cover_size; ++x) {
      const std::size_t tile =
          static_cast<std::size_t>(row / 8) * tiles_per_row + static_cast<std::size_t>(x / 8);
      tiled[tile * 64 + morton_in_tile(x % 8, row % 8)] =
          linear[static_cast<std::size_t>(y) * cover_size + x];
    }
  }
}

}
