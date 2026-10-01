#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "3dsmsc/audio/equalizer.hpp"
#include "3dsmsc/library/metadata.hpp"
#include "3dsmsc/queue/playback_queue.hpp"
#include "3dsmsc/ui/browse_navigator.hpp"
#include "test_suites.hpp"
#include "test_support.hpp"

namespace {

using test::entry;
using test::FakeFileSystem;
using test::make_track;

void test_embedded_metadata() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "3dsmsc-tags.mp3";
  std::ofstream output(path, std::ios::binary);
  const char header[] = {'I', 'D', '3', 4, 0, 0, 0, 0, 0, 49};
  output.write(header, sizeof(header));
  const char title[] = {'T', 'I', 'T', '2', 0, 0, 0, 6, 0, 0, 3, 'T', 'i', 't', 'l', 'e'};
  const char artist[] = {'T', 'P', 'E', '1', 0, 0, 0, 7, 0, 0, 3, 'A', 'r', 't', 'i', 's', 't'};
  const char album[] = {'T', 'A', 'L', 'B', 0, 0, 0, 6, 0, 0, 3, 'A', 'l', 'b', 'u', 'm'};
  output.write(title, sizeof(title));
  output.write(artist, sizeof(artist));
  output.write(album, sizeof(album));
  output.close();

  threedsmsc::EmbeddedMetadataReader embedded;
  threedsmsc::TrackMetadata metadata;
  assert(embedded.read(path.string(), metadata));
  assert(metadata.title == "Title");
  assert(metadata.artist == "Artist");
  assert(metadata.album == "Album");

  assert(embedded.read("tests/fixtures/silence.flac", metadata));
  // The 2 second fixture: an offset-by-4-bits bug once produced ~1.46 billion ms and passed `>=`.
  assert(metadata.duration_ms >= 1900 && metadata.duration_ms <= 2100);
  assert(embedded.read("tests/fixtures/silence.m4a", metadata));
  assert(metadata.title == "Fixture Title");
  assert(metadata.artist == "Fixture Artist");
  assert(metadata.album == "Fixture Album");

  threedsmsc::FilenameMetadataReader fallback;
  threedsmsc::CompositeMetadataReader composite(embedded, fallback);
  assert(composite.read("/music/fallback.mp3", metadata));
  assert(metadata.title == "fallback");
  assert(metadata.album == "music");

  std::error_code cleanup_error;
  std::filesystem::remove(path, cleanup_error);
}

std::string id3_frame(const std::string& id, const std::string& body) {
  std::string frame = id;
  frame.push_back(0);
  frame.push_back(0);
  frame.push_back(0);
  frame.push_back(static_cast<char>(body.size()));
  frame.push_back(0);
  frame.push_back(0);
  return frame + body;
}

void test_id3_numbers_and_encodings() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "3dsmsc-enc.mp3";
  std::string frames;
  frames += id3_frame("TIT2", std::string("\x00", 1) + "Caf\xE9");
  frames += id3_frame("TPE1", std::string("\x02\x00"
                                          "A\x00\xE9",
                                          5));
  frames += id3_frame("TALB", std::string("\x01\xFF\xFE"
                                          "A\x00\x3D\xD8\x00\xDE",
                                          9));
  frames += id3_frame("TRCK", std::string("\x03", 1) + "3/12");
  frames += id3_frame("TPOS", std::string("\x03", 1) + "2");
  std::string tag = "ID3";
  tag.push_back(4);
  tag.push_back(0);
  tag.push_back(0);
  tag.push_back(0);
  tag.push_back(0);
  tag.push_back(static_cast<char>((frames.size() >> 7) & 0x7F));
  tag.push_back(static_cast<char>(frames.size() & 0x7F));
  {
    std::ofstream output(path, std::ios::binary);
    output << tag << frames;
  }
  threedsmsc::EmbeddedMetadataReader embedded;
  threedsmsc::TrackMetadata metadata;
  assert(embedded.read(path.string(), metadata));
  assert(metadata.title == "Caf\xC3\xA9");
  assert(metadata.artist == "A\xC3\xA9");
  assert(metadata.album == "A\xF0\x9F\x98\x80");
  assert(metadata.track_number == 3);
  assert(metadata.disc_number == 2);
  std::error_code cleanup_error;
  std::filesystem::remove(path, cleanup_error);
}

std::string syncsafe32(std::uint32_t value) {
  std::string out(4, '\0');
  out[0] = static_cast<char>((value >> 21) & 0x7F);
  out[1] = static_cast<char>((value >> 14) & 0x7F);
  out[2] = static_cast<char>((value >> 7) & 0x7F);
  out[3] = static_cast<char>(value & 0x7F);
  return out;
}

void test_big_cover_is_skipped() {
  // TIT2, then a 300 KB cover (APIC), then TPE1 and TRCK: the tags after the cover must
  // still be found without the cover being read into memory.
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "3dsmsc-cover.mp3";
  const auto frame = [](const std::string& id, const std::string& body) {
    return id + syncsafe32(static_cast<std::uint32_t>(body.size())) + std::string(2, '\0') + body;
  };
  std::string frames;
  frames += frame("TIT2", std::string("\x03", 1) + "Song");
  frames += frame("APIC", std::string(300 * 1024, 'x'));
  frames += frame("TPE1", std::string("\x03", 1) + "Band");
  frames += frame("TRCK", std::string("\x03", 1) + "7/12");
  frames += std::string(64, '\0');  // padding
  {
    std::ofstream output(path, std::ios::binary);
    output << "ID3" << std::string("\x04\x00\x00", 3)
           << syncsafe32(static_cast<std::uint32_t>(frames.size())) << frames;
  }
  threedsmsc::EmbeddedMetadataReader reader;
  threedsmsc::TrackMetadata metadata;
  assert(reader.read(path.string(), metadata));
  assert(metadata.title == "Song");
  assert(metadata.artist == "Band");
  assert(metadata.track_number == 7);
  std::error_code cleanup_error;
  std::filesystem::remove(path, cleanup_error);
}

void test_mp3_duration() {
  std::ifstream source("tests/fixtures/silence.mp3", std::ios::binary);
  std::stringstream stream;
  stream << source.rdbuf();
  const std::string original = stream.str();  // 2.04 s, ID3 tag plus an Info header frame
  const auto read_duration = [](const std::string& name, const std::string& bytes) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    {
      std::ofstream output(path, std::ios::binary);
      output << bytes;
    }
    threedsmsc::EmbeddedMetadataReader reader;
    threedsmsc::TrackMetadata metadata;
    const bool found = reader.read(path.string(), metadata);
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
    return found ? metadata.duration_ms : 0;
  };

  // Constant bitrate, no Xing/Info header (this fixture): estimated from bitrate and size.
  const std::uint64_t estimated = read_duration("3dsmsc-dur-cbr.mp3", original);
  assert(estimated > 1900 && estimated < 2300);

  // A Xing header holds the exact frame count: 100 frames of 1152 samples at 44.1 kHz.
  std::string xing_frame("\xFF\xFB\x90\x00", 4);  // MPEG1 layer III, 128 kbps, 44.1 kHz, stereo
  xing_frame += std::string(32, '\0');            // side information
  xing_frame += "Xing";
  xing_frame += std::string("\x00\x00\x00\x01", 4);  // flags: frame count present
  xing_frame += std::string("\x00\x00\x00\x64", 4);  // 100 frames
  xing_frame.resize(417, '\0');
  const std::uint64_t exact = read_duration("3dsmsc-dur-xing.mp3", xing_frame + xing_frame);
  assert(exact == 100ULL * 1152 * 1000 / 44100);  // 2612 ms, not the 128 kbps size estimate

  // No ID3 tag at all: the file starts with the first frame.
  const std::size_t tag_size = 10 + ((static_cast<unsigned char>(original[6]) << 21) |
                                     (static_cast<unsigned char>(original[7]) << 14) |
                                     (static_cast<unsigned char>(original[8]) << 7) |
                                     static_cast<unsigned char>(original[9]));
  const std::uint64_t untagged = read_duration("3dsmsc-dur-raw.mp3", original.substr(tag_size));
  assert(untagged > 1900 && untagged < 2300);
}

}

void run_tags_tests() {
  test_embedded_metadata();
  test_id3_numbers_and_encodings();
  test_big_cover_is_skipped();
  test_mp3_duration();
}
