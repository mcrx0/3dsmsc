#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "3dsmsc/library/embedded_cover.hpp"
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

using Bytes = std::vector<unsigned char>;

Bytes read_fixture(const char* path) {
  std::ifstream input(path, std::ios::binary);
  return Bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

void append(Bytes& target, const Bytes& more) {
  target.insert(target.end(), more.begin(), more.end());
}

void append_text(Bytes& target, const std::string& text) {
  target.insert(target.end(), text.begin(), text.end());
}

Bytes u32_be(std::uint32_t value) {
  return {static_cast<unsigned char>(value >> 24), static_cast<unsigned char>(value >> 16),
          static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value)};
}

Bytes syncsafe(std::uint32_t value) {
  return {static_cast<unsigned char>((value >> 21) & 0x7F),
          static_cast<unsigned char>((value >> 14) & 0x7F),
          static_cast<unsigned char>((value >> 7) & 0x7F),
          static_cast<unsigned char>(value & 0x7F)};
}

std::string write_temp(const std::string& name, const Bytes& bytes) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return path.string();
}

// An ID3v2 frame: a four-character id (three for v2.2), a size, flags, and the body.
Bytes id3_frame(unsigned version, const std::string& id, const Bytes& body) {
  Bytes frame;
  append_text(frame, id);
  if (version == 2) {
    frame.push_back(static_cast<unsigned char>(body.size() >> 16));
    frame.push_back(static_cast<unsigned char>(body.size() >> 8));
    frame.push_back(static_cast<unsigned char>(body.size()));
  } else {
    append(frame, version == 4 ? syncsafe(static_cast<std::uint32_t>(body.size()))
                               : u32_be(static_cast<std::uint32_t>(body.size())));
    frame.push_back(0);
    frame.push_back(0);
  }
  append(frame, body);
  return frame;
}

Bytes id3_tag(unsigned version, unsigned char flags, const Bytes& frames) {
  Bytes tag = {'I', 'D', '3', static_cast<unsigned char>(version), 0, flags};
  append(tag, syncsafe(static_cast<std::uint32_t>(frames.size())));
  append(tag, frames);
  return tag;
}

// An APIC body: encoding, MIME type, picture type, description, then the image.
Bytes apic_body(unsigned char encoding, const Bytes& description, unsigned type,
                const Bytes& image) {
  Bytes body = {encoding};
  append_text(body, "image/png");
  body.push_back(0);
  body.push_back(static_cast<unsigned char>(type));
  append(body, description);
  append(body, image);
  return body;
}

constexpr std::size_t max_bytes = 2 * 1024 * 1024;

bool extract(const std::string& path, Bytes& image) {
  return threedsmsc::read_embedded_cover(path, max_bytes, image);
}

void test_id3v23_picture_is_extracted() {
  const Bytes png = read_fixture("tests/fixtures/cover.png");
  Bytes frames = id3_frame(3, "TIT2", {0, 'S', 'o', 'n', 'g'});
  append(frames, id3_frame(3, "APIC", apic_body(0, {0}, 3, png)));
  const std::string path = write_temp("3dsmsc-v23.mp3", id3_tag(3, 0, frames));
  Bytes image;
  assert(extract(path, image));
  assert(image == png);
  // And the whole chain: the track has no picture file, so the embedded one is decoded.
  CoverPixels pixels{};
  assert(threedsmsc::load_track_cover("", path, pixels) == threedsmsc::CoverSource::Embedded);
  assert(at(pixels, 10, 40) == red && at(pixels, 53, 40) == blue);
  std::filesystem::remove(path);
}

void test_front_cover_is_preferred_and_utf16_description_is_skipped() {
  const Bytes png = read_fixture("tests/fixtures/cover.png");
  const Bytes jpg = read_fixture("tests/fixtures/cover.jpg");
  // The first picture is "other" (type 0), the second the front cover (type 3) with a UTF-16
  // description: BOM, one letter, and the two-byte terminator.
  Bytes frames = id3_frame(4, "APIC", apic_body(0, {0}, 0, jpg));
  append(frames, id3_frame(4, "APIC", apic_body(1, {0xFF, 0xFE, 'a', 0, 0, 0}, 3, png)));
  const std::string path = write_temp("3dsmsc-v24.mp3", id3_tag(4, 0, frames));
  Bytes image;
  assert(extract(path, image));
  assert(image == png);
  std::filesystem::remove(path);
}

void test_id3v22_picture_is_extracted() {
  const Bytes png = read_fixture("tests/fixtures/cover.png");
  Bytes body = {0};
  append_text(body, "PNG");
  body.push_back(3);
  body.push_back(0);  // an empty description
  append(body, png);
  const std::string path = write_temp("3dsmsc-v22.mp3", id3_tag(2, 0, id3_frame(2, "PIC", body)));
  Bytes image;
  assert(extract(path, image));
  assert(image == png);
  std::filesystem::remove(path);
}

void test_unsynchronised_picture_is_restored() {
  // The tag-level flag 0x80 means every 0xFF in the data was followed by an extra 0x00.
  const Bytes original = {0xFF, 0xD8, 0xFF, 0xE0, 1, 2, 3};
  const Bytes stuffed = {0xFF, 0x00, 0xD8, 0xFF, 0x00, 0xE0, 1, 2, 3};
  const std::string path = write_temp(
      "3dsmsc-unsync.mp3", id3_tag(3, 0x80, id3_frame(3, "APIC", apic_body(0, {0}, 3, stuffed))));
  Bytes image;
  assert(extract(path, image));
  assert(image == original);
  std::filesystem::remove(path);
}

void test_flac_picture_is_extracted() {
  const Bytes png = read_fixture("tests/fixtures/cover.png");
  Bytes file = {'f', 'L', 'a', 'C', 0x00, 0, 0, 34};  // STREAMINFO, not the last block
  append(file, Bytes(34, 0));
  Bytes picture = u32_be(3);
  append(picture, u32_be(9));
  append_text(picture, "image/png");
  append(picture, u32_be(0));  // no description
  for (int field = 0; field < 4; ++field)
    append(picture, u32_be(0));  // width, height, depth, colours
  append(picture, u32_be(static_cast<std::uint32_t>(png.size())));
  append(picture, png);
  file.push_back(0x86);  // PICTURE, the last block
  file.push_back(static_cast<unsigned char>(picture.size() >> 16));
  file.push_back(static_cast<unsigned char>(picture.size() >> 8));
  file.push_back(static_cast<unsigned char>(picture.size()));
  append(file, picture);
  const std::string path = write_temp("3dsmsc-picture.flac", file);
  Bytes image;
  assert(extract(path, image));
  assert(image == png);
  std::filesystem::remove(path);
}

Bytes mp4_box(const std::string& name, const Bytes& payload) {
  Bytes box = u32_be(static_cast<std::uint32_t>(8 + payload.size()));
  append_text(box, name);
  append(box, payload);
  return box;
}

void test_mp4_picture_is_extracted() {
  const Bytes png = read_fixture("tests/fixtures/cover.png");
  Bytes data_payload = {0, 0, 0, 14, 0, 0, 0, 0};  // type "PNG", locale
  append(data_payload, png);
  Bytes meta_payload = {0, 0, 0, 0};  // version and flags
  append(meta_payload, mp4_box("ilst", mp4_box("covr", mp4_box("data", data_payload))));
  Bytes file = mp4_box("ftyp", {'M', '4', 'A', ' ', 0, 0, 0, 0});
  append(file, mp4_box("mdat", Bytes(50, 7)));
  append(file, mp4_box("moov", mp4_box("udta", mp4_box("meta", meta_payload))));
  const std::string path = write_temp("3dsmsc-picture.m4a", file);
  Bytes image;
  assert(extract(path, image));
  assert(image == png);
  std::filesystem::remove(path);
}

void test_files_without_a_usable_picture_give_nothing() {
  Bytes image;
  assert(!extract("tests/fixtures/silence.mp3", image));
  assert(!extract("tests/fixtures/silence.flac", image));
  assert(!extract("tests/fixtures/silence.m4a", image));
  assert(!extract("tests/fixtures/does-not-exist.mp3", image));
  const Bytes png = read_fixture("tests/fixtures/cover.png");
  const Bytes tag = id3_tag(3, 0, id3_frame(3, "APIC", apic_body(0, {0}, 3, png)));
  // A picture bigger than the limit is refused, and so is a tag cut off in the middle of it.
  const std::string path = write_temp("3dsmsc-limit.mp3", tag);
  assert(!threedsmsc::read_embedded_cover(path, 100, image));
  Bytes cut(tag.begin(), tag.begin() + static_cast<std::ptrdiff_t>(tag.size() / 2));
  assert(!extract(write_temp("3dsmsc-cut.mp3", cut), image));
  std::filesystem::remove(path);
  std::filesystem::remove(std::filesystem::temp_directory_path() / "3dsmsc-cut.mp3");
}

void test_picture_file_beats_the_embedded_picture() {
  const Bytes jpg = read_fixture("tests/fixtures/cover.jpg");
  const Bytes tag = id3_tag(3, 0, id3_frame(3, "APIC", apic_body(0, {0}, 3, jpg)));
  const std::string audio = write_temp("3dsmsc-both.mp3", tag);
  CoverPixels pixels{};
  assert(threedsmsc::load_track_cover("tests/fixtures/cover.png", audio, pixels) ==
         threedsmsc::CoverSource::Sidecar);
  // A picture file that cannot be read falls back to the embedded one.
  assert(threedsmsc::load_track_cover("tests/fixtures/silence.mp3", audio, pixels) ==
         threedsmsc::CoverSource::Embedded);
  assert(threedsmsc::load_track_cover("", "tests/fixtures/silence.mp3", pixels) ==
         threedsmsc::CoverSource::None);
  std::filesystem::remove(audio);
}

}

void run_cover_tests() {
  test_png_cover_is_cropped_and_shrunk();
  test_jpeg_cover_decodes();
  test_bad_covers_are_rejected();
  test_gpu_tiling_is_a_permutation_with_known_positions();
  test_id3v23_picture_is_extracted();
  test_front_cover_is_preferred_and_utf16_description_is_skipped();
  test_id3v22_picture_is_extracted();
  test_unsynchronised_picture_is_restored();
  test_flac_picture_is_extracted();
  test_mp4_picture_is_extracted();
  test_files_without_a_usable_picture_give_nothing();
  test_picture_file_beats_the_embedded_picture();
}
