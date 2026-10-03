#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "3dsmsc/ui/cover_image.hpp"
#include "test_suites.hpp"

namespace {

using threedsmsc::cover_size;
using threedsmsc::CoverPixels;

constexpr std::uint16_t red = 0xF800;
constexpr std::uint16_t blue = 0x001F;
constexpr std::uint16_t white = 0xFFFF;

std::uint16_t at(const CoverPixels& pixels, int x, int y) {
  return pixels[static_cast<std::size_t>(y) * cover_size + x];
}

// Channel values after JPEG compression are close, not exact.
bool near_color(std::uint16_t actual, std::uint16_t expected) {
  const auto channel = [](std::uint16_t value, int shift, int mask) {
    return static_cast<int>((value >> shift) & mask);
  };
  const auto close = [&](int shift, int mask, int tolerance) {
    const int difference = channel(actual, shift, mask) - channel(expected, shift, mask);
    return difference <= tolerance && difference >= -tolerance;
  };
  return close(11, 0x1F, 3) && close(5, 0x3F, 6) && close(0, 0x1F, 3);
}

// The fixture is 200 x 100: the left half red, the right half blue, and a white band across the
// top 10 rows. The centred 100 x 100 square keeps the red/blue split in the middle and the band.
void check_fixture(const std::string& path, bool exact) {
  CoverPixels pixels{};
  assert(threedsmsc::load_cover(path, pixels));
  const auto matches = [&](std::uint16_t actual, std::uint16_t expected) {
    return exact ? actual == expected : near_color(actual, expected);
  };
  assert(matches(at(pixels, 10, 40), red));
  assert(matches(at(pixels, 53, 40), blue));
  assert(matches(at(pixels, 10, 63), red));
  assert(matches(at(pixels, 53, 63), blue));
  assert(matches(at(pixels, 10, 2), white));  // the band is at the top, so orientation is kept
  assert(matches(at(pixels, 53, 2), white));
}

void test_png_cover_is_cropped_and_shrunk() {
  check_fixture("tests/fixtures/cover.png", true);
}

void test_jpeg_cover_decodes() {
  check_fixture("tests/fixtures/cover.jpg", false);
}

void test_bad_covers_are_rejected() {
  CoverPixels pixels{};
  assert(!threedsmsc::load_cover("tests/fixtures/does-not-exist.png", pixels));
  assert(!threedsmsc::load_cover("tests/fixtures/silence.mp3", pixels));  // not an image
  const std::filesystem::path empty = std::filesystem::temp_directory_path() / "3dsmsc-empty.png";
  std::ofstream(empty).close();
  assert(!threedsmsc::load_cover(empty.string(), pixels));
  // A PNG header that claims a huge size must be refused before any pixel memory is allocated.
  const std::filesystem::path huge = std::filesystem::temp_directory_path() / "3dsmsc-huge.png";
  {
    std::ofstream output(huge, std::ios::binary);
    const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0,    0x0D,
                                 'I',  'H', 'D', 'R', 0,    0,    0x27, 0x10, 0, 0, 0x27, 0x10,
                                 8,    2,   0,   0,   0,    0,    0,    0,    0, 0};
    output.write(reinterpret_cast<const char*>(png), sizeof(png));
  }
  assert(!threedsmsc::load_cover(huge.string(), pixels));
  std::filesystem::remove(empty);
  std::filesystem::remove(huge);
}

// The GPU layout: pixels are moved, never lost or duplicated, and known positions land where the
// hardware expects them (8 x 8 tiles, Morton order inside a tile, rows flipped).
void test_gpu_tiling_is_a_permutation_with_known_positions() {
  CoverPixels linear{};
  for (std::size_t index = 0; index < linear.size(); ++index)
    linear[index] = static_cast<std::uint16_t>(index);
  CoverPixels tiled{};
  threedsmsc::tile_for_gpu(linear, tiled);
  std::set<std::uint16_t> seen(tiled.begin(), tiled.end());
  assert(seen.size() == linear.size());
  // The bottom-left image pixel (x 0, y 63) is texture row 0, so it opens the first tile.
  assert(tiled[0] == linear[63 * cover_size + 0]);
  assert(tiled[1] == linear[63 * cover_size + 1]);  // next pixel in x
  assert(tiled[2] == linear[62 * cover_size + 0]);  // next row up
  assert(tiled[3] == linear[62 * cover_size + 1]);
  assert(tiled[4] == linear[63 * cover_size + 2]);       // bit 1 of x comes after bit 0 of y
  assert(tiled[64] == linear[63 * cover_size + 8]);      // the second tile is the next 8 columns
  assert(tiled[8 * 64] == linear[55 * cover_size + 0]);  // the second tile row is 8 rows higher
}

}

void run_cover_tests() {
  test_png_cover_is_cropped_and_shrunk();
  test_jpeg_cover_decodes();
  test_bad_covers_are_rejected();
  test_gpu_tiling_is_a_permutation_with_known_positions();
}
